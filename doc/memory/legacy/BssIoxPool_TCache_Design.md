# BssIoxPool v11.0 + Safe TCache — 完整设计方案（生产级）

**版本**：v11.0 + Safe TCache 设计草案  
**作者**：自动生成（交付给内存子系统团队）  
**说明**：在不破坏 BssIoxPool v11.0 已有安全语义（handle/offset、RedZone/Canary、memprotect、lease）下，引入 jemalloc-style thread-local cache（tcache）。本文档为可交付实现级设计，包含数据结构、伪码、并发模型、回收与一致性协议、测试/监控与 rollout 指南。

---

## 目录
1. 目标与设计约束  
2. 总体架构（概览）  
3. 主要数据结构（详尽）  
4. 内存布局与元数据（页/region/slot）  
5. 核心操作流程（alloc / free / refill / drain）与伪码  
6. Epoch / Flush / memprotect 协议（一致性核心）  
7. Quarantine / Canary 校验 / Lease 回收流程  
8. 线程生命周期、fork/exec、signal 处理  
9. 并发细节、锁策略与性能优化  
10. 参数与默认值（可调）  
11. 安全性与故障处理（mprotect、dump、forensic）  
12. 性能评估与开销估算  
13. 监控指标、日志与告警策略  
14. 测试、验证与 rollout 步骤  
15. 附录：关键伪码与示例实现片段

---

## 1. 目标与设计约束

### 目标
- 将 tcache（thread-local freelist）引入 BssIoxPool v11.0，以改善多线程场景下的 alloc/free 吞吐与延迟。
- **不破坏**原有安全机制（RedZone/Canary、memprotect、handle/offset、lease）。
- 在 memprotect 或 lease 回收时能**快速且确定性地**失效/flush 本地缓存。
- 保持跨进程 shared-pool semantics（shared pools 不使用本地 tcache）。

### 硬性约束
- 外部露出 API 不变：`bssiox_alloc() / bssiox_free()` 仍以 `handle` 为唯一标识。
- tcache 仅保存 `uint64_t handle`（及少量元信息），不要暴露裸指针。
- 当发生 memprotect/lease 回收时，必须能在可控时间窗口内保证所有线程停止使用受影响的 handles（epoch + flush）。
- shared pool allocations 禁止进入 local tcache（policy 强制）。
- 线程退出或 fork 必须 flush 本地 tcache。

---

## 2. 总体架构（概览）

```
┌──────────────────────────────────────────────────────────────┐
│ Global Arena / BssIoxPool v11.0                              │
│  ├─ ResidentPool (常驻)                                       │
│  └─ UnifiedBurstPool (Burst/临时)                             │
│                                                              │
│ + Metadata Region (最后 64KiB)                                │
└──────────────────────────────────────────────────────────────┘
       ↑
       │  handle = base_id(4bit) + offset(60bit)
       │
Thread-local tcache (per-thread)      Central pool / Quarantine
┌──────────────┐    <---- batch ---->  ┌──────────────┐
│ TCacheBin[SC]│                     ->│ Central free │
│ (stack of N) │    <---- batch ---->  └──────────────┘
└──────────────┘
```

说明：
- 每个线程有 `TCache`，按 size-class 分 `TCACHE_BIN`。
- Central pool 按 size-class 管理 region / chunk 列表与 quarantine。
- Global `global_epoch` 用于 memprotect/lease/time-based flush 信号。

---

## 3. 主要数据结构（详尽）

### 基础类型
```c
typedef uint64_t bss_handle_t; // base_id:4 | offset:60
```

### Chunk header（每 chunk 起始处）
```c
struct ChunkHeader {
    uint16_t size_class;      // 索引
    uint16_t flags;           // e.g., SHARED, RESIDENT, etc.
    uint32_t generation;      // reuse counter
    uint64_t canary;          // canary
    uint64_t alloc_ts_ms;     // optional: last alloc timestamp
    // padding to align header
};
```

### TCacheEntry（线程本地条目）
```c
struct TCacheEntry {
    bss_handle_t handle;
    uint32_t generation; // cached at free time
};
```

### TCacheBin（每 size-class）
```c
struct TCacheBin {
    TCacheEntry entries[TCACHE_CAP]; // LIFO stack
    uint16_t count;
};
```

### TCache（per-thread）
```c
struct TCache {
    uint64_t local_epoch;  // tcache epoch snapshot
    TCacheBin bins[NUM_SIZE_CLASSES];
    // stats
    uint32_t hits, misses, refill_cnt, drain_cnt;
};
```

### Central structures
```c
struct CentralFreeList {
    // lock-protected or lock-free batch stacks
    atomic_ptr free_head;
    atomic_uint64_t count;
    // quarantine queue (pending validation)
    queue_t quarantine;
};
```

### Global control
```c
atomic_uint64_t global_epoch;
```

---

## 4. 内存布局与元数据（页/region/slot）

- Region (region_size = power_of_two, e.g. 64KB/256KB) 对齐，便于通过 `(addr & ~REGION_MASK)` 找 header。
- Region header（存放 metadata、bitmap、free_count）放在 region 起始页。
- Chunk size = size_class + header + redzone前后（实际 payload area 返回给用户）。
- Shared pools：明确标记 `CHUNK_FLAG_SHARED`，不得进入 local tcache。

示例 region layout：
```
[ region_header | bitmap | chunk0(header+payload) | chunk1 | ... ]
```

---

## 5. 核心操作流程（伪码）

### Alloc (fast path)
```c
void* bssiox_alloc(size_t size) {
    sc = size_to_class(size);
    TCache* tc = get_thread_tcache();
    if (tc->local_epoch != atomic_load(&global_epoch)) flush_tcache(tc);

    TCacheBin* bin = &tc->bins[sc];
    if (bin->count > 0) {
        entry = bin->entries[--bin->count];
        // quick generation validation
        if (entry.generation == read_chunk_generation(entry.handle)) {
            return from_handle(entry.handle);
        } else {
            // generation mismatch: drop and continue to slow path
        }
    }
    // slow path
    return alloc_slow(sc, tc);
}
```

### Free (fast path)
```c
void bssiox_free(void* p) {
    bss_handle_t h = to_handle(p);
    sc = handle_to_class(h);
    TCache* tc = get_thread_tcache();
    if (tc->local_epoch != atomic_load(&global_epoch)) flush_tcache(tc);
    TCacheBin* bin = &tc->bins[sc];

    // optional immediate canary check in SAFE mode
    if (CHECK_CANARY_ON_FREE) {
       if (!validate_canary(h)) goto quarantine_and_return;
    }
    // push to local tcache
    if (bin->count < TCACHE_CAP) {
        bin->entries[bin->count++] = {h, read_generation(h)};
        return;
    } else {
        // drain half to central quarantine
        batch = pop_batch(bin, BATCH);
        central_push_quarantine(sc, batch);
        bin->entries[bin->count++] = {h, read_generation(h)};
    }
}
```

### alloc_slow / refill / drain
- `alloc_slow` grabs batch from central free list (lock-protected batch pop), validates generation/canary as needed, populates tcache and returns one.

- `central_push_quarantine` 将 batch 放入 quarantine 队列，由后台线程验证 canary/poison，并将合格条目 push 到 `CentralFreeList.free_head`。

---

## 6. Epoch / Flush / memprotect 协议（一致性核心）

### global_epoch semantics
- `global_epoch` 用于全局失效广播（memprotect / lease_reclaim / admin 操作时 bump）。
- 每个 thread 的 `tcache.local_epoch` 记录上次 sync 的 epoch。On alloc/free, we check equality.

### Flush protocol
1. Admin triggers `atomic_fetch_add(&global_epoch, 1)`.
2. Each thread on next alloc/free sees `local_epoch != global_epoch` and performs `flush_tcache()`:
   - Batch-return all entries to central quarantine (not directly to free list).
   - Clear local bins.
   - Update `tcache.local_epoch = global_epoch`.
3. Central quarantine validated by background thread; when validated, items returned to central free list.

### Guaranteed safety
- After epoch bump, no thread will reuse stale handle (they must flush and validation occurs before reuse).
- For stronger guarantee in short windows, admin can send signal to threads to force immediate flush (optional).

---

## 7. Quarantine / Canary 校验 / Lease 回收

### Quarantine policy
- Central quarantine holds items returned from tcache or from free path.
- Validation thread pops items, reads `ChunkHeader.generation` and `canary`:
  - If canary valid: push to central free list.
  - If invalid: trigger `mprotect` of corresponding region/pool and start forensic dump.
- Quarantine can be size-based (e.g. hold up to Q_MAX per class) or time-based (max 100ms).

### Lease reclamation
- Lease thread identifies chunks to reclaim (lease timeout).
- On reclaim: bump `global_epoch`, wait for flush window (timeout or confirmation), then reclaim validated entries.

---

## 8. 线程生命周期、fork/exec、signal 处理

### Thread exit
- On thread exit hook: `flush_tcache()` synchronously.

### fork
- POSIX fork: in child, disable tcache (set `tcache.disabled = true`) until exec or manual reinit. Alternatively, flush in parent before fork.

### signal
- Avoid performing heavy operations in signal handler. Use signal to wake a thread to perform flush in safe context.

---

## 9. 并发细节、锁策略与性能优化

### Central free list (batching)
- Use lock-protected stack but operations are batched to amortize locking cost.
- Batch pop: central returns up to BATCH items in single lock.

### Atomic bitmap per region
- Allocation within region uses atomic bit test-and-set on words; minimizes locking.

### NUMA-aware allocation
- For high throughput, maintain per-NUMA central lists and route tcache refill to local NUMA central.

### Prefetching
- Bulk refill includes prefetch of chunk headers to reduce cache miss.

---

## 10. 参数与默认值（可调）

- TCACHE_CAP = 32 entries per size-class (default)
- BATCH = 16
- NUM_SIZE_CLASSES = your existing 8 classes (4..512)
- Q_MAX = 256 entries per class in quarantine
- QUARANTINE_TIMEOUT_MS = 100ms
- EPOCH_FLUSH_TIMEOUT_MS = 50ms (max waiting window for synchronous flush)
- CHECK_CANARY_ON_FREE = true (default in SAFE mode)

---

## 11. 安全性与故障处理

### Canary/RedZone 校验
- 强制在 central validation 阶段进行完整校验。
- 可选在 free 时进行快速校验（performance tradeoff）。

### mprotect on breach
- On breach detection: `mprotect(pool_base, pool_size, PROT_NONE)`.
- After protection: generate diagnostic dump (last N alloc/free traces).
- Admin may choose to terminate process to avoid data leak.

### Forensics
- Maintain ring buffer of last N allocations (thread-safe minimal ring).
- On breach, record ring for off-line analysis.

---

## 12. 性能评估与开销估算

### 预期收益（相对无 tcache 的 v11 baseline）
- 多线程小对象吞吐：+35% ~ +70%（依 workload）
- P50 latency：基本不变或微降（fast-path）。
- P99 latency：显著下降（减少 central lock contention）。
- TLB / cache：稍有改善（在热点场景）。

### 额外开销
- 内存：每线程 `NUM_SIZE_CLASSES * TCACHE_CAP * avg_slot_bytes`。例如：
  - 8 classes, TCACHE_CAP=32, avg slot 64B → per-thread cache ~ 8 * 32 * 64 = 16 KiB
- CPU：epoch checks (cheap compare) on fast-path.
- Background validation thread CPU: 0.1%–1% typical（depends on reclaim rate & validation).

---

## 13. 监控指标、日志与告警策略

### 必要指标
- tcache_hit_ratio per size-class
- refill_count / drain_count
- central_free_count per size-class
- quarantine_size & validation_rate
- epoch_bumps / memprotect_events
- canary_failures, mprotect_triggers

### 告警策略
- canary_failures > 0 → P1 alert (immediate)
- epoch_bumps excessive → investigate Lease thrashing
- central_free_count low + refill_rate high → memory pressure

---

## 14. 测试、验证与 rollout 步骤

### Unit tests
- single-thread fast-path correctness
- generation mismatch detection
- canary corruption injection
- tcache flush correctness

### Stress tests
- multi-thread high-QPS alloc/free
- simulate memprotect events during heavy tcache usage
- thread create/exit tests

### Fault injection
- corrupt chunk canary to ensure mprotect and forensic pipeline
- force fork and exec to validate tcache behaviour

### Rollout plan
1. Implement and test in dev/staging with TCACHE_MODE=MINIMAL.
2. Rollout to low-traffic services with TCACHE_MODE=SAFE.
3. Monitor metrics and gradually enable across fleet.
4. For high QPS backend, enable aggressive tuning iteratively.

---

## 15. 附录：关键伪码与示例实现片段

（见前文伪码，进一步提供 C 风格实现片断可在实际实现阶段迭代）

---

# 结语

本设计在**保留 BssIoxPool v11.0 安全语义**前提下，引入了 **可配置、安全且高效的 tcache**。推荐默认采用 **SAFE 模式（方案 B）**，并通过 feature flag 控制（`BSSIOX_TCACHE_MODE={OFF,MINIMAL,SAFE,AGGRESSIVE}`）。实现需重点覆盖 epoch/flush/validation 与 fork/thread-exit 场景。

# 优化

一、 内存布局与基础架构加固
|方面|优化方案 (v11.1 策略)|优势/必要性|
|:-:|:-:|:-:|
|Metadata 区域|保持现状： 将 Metadata/自举区 放在内存段的结束位置（最后 64 KiB），且永久隔离，不参与用户分配。|隔离性与故障诊断： 物理上防止 UnifiedBurstPool 上溢越界破坏核心元数据。允许在 mprotect 触发时豁免此区域，确保故障诊断 (Forensics) Dump 可用。|
|ResidentPool 策略|强制隔离： ResidentPool 的 Chunks 永不进入 TCache。强制为其设置 lease_ms = 0 (永不回收)，并从后台 Lease 巡检中豁免。| 系统稳定性： 确保 ECU 核心算法所需内存的生命周期不受 Lease 机制干扰，保障系统长期运行的稳定性。|
|共享内存 (SHM) 兼容|严格区分： 在 bssiox_alloc 接口层引入标记（is_shared），所有共享内存分配（如 Iceoryx2 生产/消费内存）在 bssiox_free 时，必须强制绕过 TCache，直接进入 Central Quarantine/Free List。|Iceoryx2 兼容性： 保证共享内存的句柄语义，防止一个进程将 SHM 块缓存到本地 TCache，导致其他进程无法访问或回收。|

二、 TCache 性能与确定性优化（运行时）
|方面|优化方案 (v11.1 策略)|优势/必要性|
|:-:|:-:|:-:|
|核心亲和性 (Affinity)|Per-Core Region： Central Free List 划分为 Per-Core Region。线程启动时绑定 Core，其 TCache Refill 强制且优先从该 Core 对应的 Central Region 拉取 Chunk。|确定性与低延迟： 消除跨 Core/Memory Bank 的远程访存，提高内存访问的本地性，是满足 ECU P99.9 延迟要求的关键。|
|Epoch 校验优化|高速通道豁免： 对于高实时性线程，Fast Path 中的 alloc/free 跳过 local_epoch != global_epoch 的原子检查。此检查只在 Refill 或 Drain 的 Slow Path 中执行。|性能极致优化： 进一步压榨 Fast Path 性能，降低控制环路的延迟。|
|中央并发模型|Ticket Lock + Wait-Free Queue： Central Free List 采用 Ticket Lock 配合 Batching；Quarantine 队列 采用 MPMC/MPSC Wait-Free Queue。|降低长尾延迟： Ticket Lock 保证高并发下的锁等待公平性，Wait-Free 队列保证 Quarantine 推送/弹出效率，避免新的锁竞争。|

三、 安全性与启动流程优化
|方面|优化方案 (v11.1 策略)|优势/必要性|
|:-:|:-:|:-:|
|TCache 预热|批量 Refill： 在进程启动阶段，在每个线程的初始化钩子中，执行一次批量 Refill 操作，将每个 TCacheBin 预先填充到 TCACHE_CAP/2。|启动速度与确定性： 显著提高 ECU 应用的启动速度，消除首次运行时大量 Central Lock 竞争导致的抖动。|
|Fast Canary 校验|Magic Check： 在 bssiox_free 的 Fast Path 中，如果启用校验，仅进行 Chunk Header 内的 Magic Canary 快速校验。完整的 RedZone 校验保留在后台 Quarantine 验证线程中。|即时发现破坏： 在保证 Fast Path 性能的前提下，尽可能早地发现内存破坏并触发 mprotect。|
|fork() 行为|强制同步 Flush： 强制在 Parent 进程执行 POSIX fork() 调用前，同步执行 flush_tcache()。|安全隔离： 彻底消除子进程继承父进程 TCache handles 的安全隐患。|
|mprotect 效率|分块保护： 触发 mprotect 时，将 UnifiedBurstPool 划分为逻辑 Regions，只对受影响的 Region 设置 PROT_NONE。|实时性保护： 减少 mprotect 触发时的系统调用开销和 TLB 冲击，最大限度地减少对其他实时任务的影响。|
