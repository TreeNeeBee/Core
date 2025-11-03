#include <new>
#include <algorithm>
#include <cstring>
#include <thread>
#include <cstdlib>
#include <chrono>
#include <cstdio>
#include <unistd.h>
#include <nlohmann/json.hpp>
#include "CMemory.hpp"
#include "CConfig.hpp"

MEMORY_CONTROL

namespace lap::core {
namespace {
    // ========================================================================
    // System Alignment Configuration
    // ========================================================================
    // Define default alignment based on system architecture
    // This is a RECOMMENDED value, actual alignment can be configured via config file
    #if defined(__LP64__) || defined(_WIN64) || (defined(__WORDSIZE) && __WORDSIZE == 64)
        constexpr UInt32 DEFAULT_ALIGN_BYTE = 8;   // 64-bit system: 8-byte alignment (recommended)
    #else
        constexpr UInt32 DEFAULT_ALIGN_BYTE = 4;   // 32-bit system: 4-byte alignment (recommended)
    #endif
    
    // Compile-time assertion: DEFAULT_ALIGN_BYTE must be power of 2
    static_assert((DEFAULT_ALIGN_BYTE & (DEFAULT_ALIGN_BYTE - 1)) == 0, 
                  "DEFAULT_ALIGN_BYTE must be a power of 2");
    
    // Memory configuration constants
    constexpr UInt32 MAX_POOL_COUNT = 64;                       // Maximum number of memory pools
    constexpr UInt32 MAX_POOL_CONFIG_ENTRIES = 16;              // Maximum pool configuration entries
    constexpr UInt32 MAX_POOL_UNIT_SIZE = 1024;                 // Maximum unit size for pooled allocation
    constexpr UInt32 MIN_POOL_UNIT_SIZE = 4;                    // Minimum unit size for pooled allocation
    constexpr UInt32 DEFAULT_POOL_INIT_COUNT = 4;               // Default initial count for pools
    constexpr UInt32 COMPACT_SIZE_RANGE_STEP = 16;              // Step size for compact size range
    constexpr UInt32 NORMAL_SIZE_RANGE_STEP = 64;               // Step size for normal size range
    constexpr UInt32 CLASS_NAME_MAX_LENGTH = 63;                // Maximum length of class name (excluding null terminator)
    constexpr UInt32 THREAD_NAME_MAX_LENGTH = 63;               // Maximum length of thread name (excluding null terminator)
    constexpr UInt32 REPORT_FILE_MAX_LENGTH = 255;              // Maximum length of report file path (excluding null terminator)
    constexpr UInt32 LEAK_SUMMARY_BUFFER_SIZE = 512 * 1024;     // Buffer size for leak summary formatting

    // Special magic for system-fallback allocations (non-pooled)
   #if defined(__LP64__) || defined(_WIN64) || (defined(__WORDSIZE) && __WORDSIZE == 64)
       constexpr MagicType SYSTEM_UNIT_MAGIC = 0xF17EC0DEF17EC0DEULL;
       constexpr MagicType BLOCK_UNIT_MAGIC_BASE = 0xDEADBEEFCAFEBABEULL;
   #else
       constexpr MagicType SYSTEM_UNIT_MAGIC = 0xF17EC0DEU;
       constexpr MagicType BLOCK_UNIT_MAGIC_BASE = 0xDEADBEEFU;
   #endif

    // Magic marker for pre-initialization system allocations
    constexpr UInt32 PREINIT_MAGIC = 0xBADC0FFEu;
    const String DEFAULT_MEMORY_CONFIG = "memory";
    const String MEMORY_LEAK_LOG_FILE = "memory_leak.log";

    // Generate block header magic with runtime mixing
    inline MagicType makeBlockHeaderMagic() {
        return BLOCK_UNIT_MAGIC_BASE ^ MemManager::getRuntimeXorMask();
    }

    // Header for pre-initialization allocations
    struct PreInitHeader {
        UInt32 magic;
        Size size;
    };

    // Align size to specified boundary
    inline Size alignSize(Size size, UInt32 alignMask) {
        return (size + alignMask) & ~alignMask;
    }

    // Round up to power-of-two in [MIN_POOL_UNIT_SIZE, MAX_POOL_UNIT_SIZE]; return 0 if > MAX_POOL_UNIT_SIZE
    inline UInt32 roundUpPow2Clamp(UInt32 x) {
        if (x <= MIN_POOL_UNIT_SIZE) return MIN_POOL_UNIT_SIZE;
        if (x > MAX_POOL_UNIT_SIZE) return 0u;
        // round up to next power of two
        --x;
        x |= x >> 1; x |= x >> 2; x |= x >> 4; x |= x >> 8; x |= x >> 16;
        ++x;
        if (x < MIN_POOL_UNIT_SIZE) x = MIN_POOL_UNIT_SIZE;
        if (x > MAX_POOL_UNIT_SIZE) return 0u;
        return x;
    }
} // anonymous namespace

// MemAllocator implementation
MemAllocator::MemAllocator()
    : maxPoolCount_(0)
    , currentPoolCount_(0)
    , alignMask_(DEFAULT_ALIGN_BYTE - 1)
    , blockDataOffset_( alignSize( ( sizeof(tagPoolBlock) + sizeof(tagUnitNode) ), alignMask_) - sizeof(tagUnitNode) )
    , systemChunkHeaderSize_( alignSize(sizeof(tagUnitNode), alignMask_) )  // Will be set in initialize()
{
    // Initialize C-style mutexes
    ::pthread_mutex_init(&mutex_, nullptr);
}

MemAllocator::~MemAllocator() {
    freeAllPool();
    
    // Manually destruct and free the pools map since it was allocated with placement new + malloc
    if (pools_) {
        Map<UInt32, tagMemPool>* map_ptr = pools_.release();
        if (map_ptr) {
            map_ptr->~Map<UInt32, tagMemPool>();
            std::free(map_ptr);
        }
    }
    ::pthread_mutex_destroy(&mutex_);
}

void MemAllocator::initialize(UInt32 alignByte, UInt32 maxPoolCount) {
    CMutexGuard lock(&mutex_);
    maxPoolCount_ = maxPoolCount;
    alignMask_ = alignByte - 1;
    
    // Calculate aligned header sizes based on alignment requirement
    // systemChunkHeaderSize_ = aligned size of tagUnitNode header
    systemChunkHeaderSize_ = alignSize(sizeof(tagUnitNode), alignMask_);
    
    // blockDataOffset_ = aligned(PoolBlock + UnitNode) - UnitNode
    // This ensures the data area after PoolBlock is properly aligned
    blockDataOffset_ = alignSize( ( sizeof(tagPoolBlock) + sizeof(tagUnitNode) ), alignMask_) - sizeof(tagUnitNode);
    
    // Allocate memory using std::malloc to avoid triggering global operator new
    void* raw_map = std::malloc(sizeof(Map<UInt32, tagMemPool>));
    if (!raw_map) {
        INNER_CORE_LOG("Failed to allocate pools map");
        throw std::bad_alloc();
    }
    
    try {
        // Use placement new to construct Map in pre-allocated memory
        Map<UInt32, tagMemPool>* map_ptr = new (raw_map) Map<UInt32, tagMemPool>();
        if (!map_ptr) {
            std::free(raw_map);
            raw_map = nullptr;
            INNER_CORE_LOG("Placement new failed for pools map");
            throw std::bad_alloc();
        }

        // Use custom deleter for unique_ptr
        pools_.reset(map_ptr);
        
        if (!pools_) {
            map_ptr->~Map<UInt32, tagMemPool>();
            std::free(raw_map);
            raw_map = nullptr;
            INNER_CORE_LOG("Unique_ptr reset failed for pools map");
            throw std::bad_alloc();
        }
    } catch (...) {
        if (raw_map) { std::free(raw_map); raw_map = nullptr; }
        throw;
    }
}

Bool MemAllocator::createPool(UInt32 unitSize, UInt32 initCount, UInt32 maxCount, UInt32 appendCount) {
    CMutexGuard lock(&mutex_);
    if (pools_->size() >= static_cast<size_t>(maxPoolCount_)) {
        INNER_CORE_LOG("Max pool count exceeded");
        return false;
    }

    // Check for existing pool entry for this unit size
    auto it = pools_->find(unitSize);
    if (it != pools_->end()) {
        tagMemPool& existing = it->second;
        existing.initCount = std::max(existing.initCount, initCount);
        existing.maxCount = std::max(existing.maxCount, maxCount);
        existing.appendCount = std::max(existing.appendCount, appendCount);
        while (existing.currentCount < std::min(existing.initCount, existing.maxCount)) {
            if (!addPoolBlock(&existing)) break;
        }
        return true;
    }

    // Insert new pool into the map (key = unitSize)
    tagMemPool poolVal;
    poolVal.unitChunkSize = alignSize(unitSize + systemChunkHeaderSize_, alignMask_);
    poolVal.unitAvailableSize = unitSize;
    poolVal.initCount = initCount;
    poolVal.maxCount = maxCount;
    poolVal.appendCount = appendCount;
    poolVal.currentCount = 0;
    poolVal.firstBlock = nullptr;
    poolVal.freeList = nullptr;

    auto res = pools_->emplace(unitSize, std::move(poolVal));
    if (!res.second) {
        INNER_CORE_LOG("Failed to insert pool into map");
        return false;
    }

    tagMemPool& inserted = res.first->second;
    if (!addPoolBlock(&inserted)) {
        INNER_CORE_LOG("Failed to add pool block for unitSize=%d\n", unitSize);
        // erase inserted entry
        pools_->erase(res.first);
        return false;
    }
    while (inserted.currentCount < std::min(inserted.initCount, inserted.maxCount)) {
        if (!addPoolBlock(&inserted)) break;
    }

    currentPoolCount_ = static_cast<UInt32>(pools_->size());
    return true;
}

UInt32 MemAllocator::getPoolCount() {
    CMutexGuard lock(&mutex_);
    return pools_ ? static_cast<UInt32>(pools_->size()) : 0u;
}

Bool MemAllocator::getPoolState(UInt32 index, MemoryPoolState& poolState) {
    CMutexGuard lock(&mutex_);
    if (!pools_ || index >= pools_->size()) {
        INNER_CORE_LOG("Invalid pool index: %d\n", index);
        return false;
    }
    // iterate to nth element in ordered map
    auto it = pools_->begin();
    std::advance(it, index);
    tagMemPool* pool = &it->second;
    poolState.unitAvailableSize = pool->unitAvailableSize;
    poolState.maxCount = pool->maxCount;
    poolState.currentCount = pool->currentCount;
    poolState.freeCount = 0;
    for (UnitNodePtr node = pool->freeList; node; node = node->nextUnit) {
        ++poolState.freeCount;
    }
    poolState.memoryCost = pool->currentCount * pool->unitChunkSize;
    poolState.appendCount = pool->appendCount;
    return true;
}

void* MemAllocator::malloc(Size size) {
    // Use pools for sizes up to MAX_POOL_UNIT_SIZE; larger will fallback to system allocation
    if (DEF_LAP_IF_UNLIKELY(size > MAX_POOL_UNIT_SIZE)) {
        Size total = size + systemChunkHeaderSize_;
        tagUnitNode* unit = reinterpret_cast<tagUnitNode*>(::std::malloc(total));
        if (!unit) return nullptr;
        unit->pool = nullptr;
        unit->nextUnit = nullptr;
        unit->magic = SYSTEM_UNIT_MAGIC;
        return reinterpret_cast<char*>(unit) + systemChunkHeaderSize_;
    }
    tagMemPool* pool = findFitPool(size);
    if ( DEF_LAP_IF_UNLIKELY(!pool) ) {
        // As a safety net, fallback to system allocation to avoid failure
        Size total = size + systemChunkHeaderSize_;
        tagUnitNode* unit = reinterpret_cast<tagUnitNode*>(::std::malloc(total));
        if (!unit) return nullptr;
        unit->pool = nullptr;
        unit->nextUnit = nullptr;
        unit->magic = SYSTEM_UNIT_MAGIC;
        return reinterpret_cast<char*>(unit) + systemChunkHeaderSize_;
    }
    return allocUnit(pool);
}

void MemAllocator::free(void* ptr) {
    if (!ptr) return;

    tagUnitNode* unit = reinterpret_cast<tagUnitNode*>(static_cast<char*>(ptr) - systemChunkHeaderSize_);
    if (!unit->pool) {
        // System-fallback allocation - must free from unit address, not ptr
        if (unit->magic == SYSTEM_UNIT_MAGIC) {
            ::std::free(unit);
        } else {
            // Invalid magic - this memory was NOT allocated by us
            // Do NOT free it - it's either already freed (double-free) or never allocated by MemAllocator
            // Use fprintf to stderr to avoid memory allocation in error path
            INNER_CORE_LOG("[ERROR] MemAllocator::free: Invalid magic 0x%016lX at ptr=%p (expected SYSTEM_UNIT_MAGIC=0x%016lX). Possible double-free or memory not allocated by MemAllocator.\n", 
                           (unsigned long)unit->magic, ptr, (unsigned long)SYSTEM_UNIT_MAGIC);
            // Do NOT call ::std::free(unit) - the unit pointer is likely invalid
        }
        return;
    }
    if (unit->magic != MAKE_UNIT_NODE_MAGIC(unit)) {
        // Memory corruption detected in pooled allocation
        // Use fprintf to stderr to avoid memory allocation
        INNER_CORE_LOG("[ERROR] MemAllocator::free: Memory corruption detected at ptr=%p, magic=0x%016lX, expected=0x%016lX. Block not returned to pool to prevent further corruption.\n", 
                       ptr, (unsigned long)unit->magic, (unsigned long)MAKE_UNIT_NODE_MAGIC(unit));
        // Do NOT return unit to pool - it's corrupted
        return;
    }
    {
        CMutexGuard lock(&mutex_);
        tagMemPool* pool = unit->pool;
        unit->nextUnit = pool->freeList;
        pool->freeList = unit;
    }
}

Bool MemAllocator::addPoolBlock(tagMemPool* pool) {
    if ( !pool || ( ( pool->currentCount >= pool->maxCount ) && ( pool->maxCount != 0 ) ) ) {
        INNER_CORE_LOG("Cannot add block: pool null or max count reached");
        return false;
    }

    UInt32 count = pool->appendCount;
    if (count == 0) {
        INNER_CORE_LOG("Cannot add block: pool append count is 0");
        return false;
    }
    if ( pool->maxCount != 0 ) {
        count = std::min(count, pool->maxCount - pool->currentCount);
    }

    // Use blockDataOffset_ instead of sizeof(tagPoolBlock) for proper alignment
    Size blockSize = pool->unitChunkSize * count + blockDataOffset_ + sizeof(tagUnitNode);
    void* allocBuf = ::std::malloc(blockSize);
    if (!allocBuf) {
        INNER_CORE_LOG("Failed to allocate block buffer of size=%zu\n", blockSize);
        return false;
    }
    tagPoolBlock* block = new (allocBuf) tagPoolBlock();
    if (!block) {
        INNER_CORE_LOG("Failed to allocate block of size=%zu\n", blockSize);
        return false;
    }

    block->blockSize = blockSize;
    block->unitCount = count;
    block->usedCursor = 0;
    block->nextBlock = pool->firstBlock;
    pool->firstBlock = block;
    pool->currentCount += count;

    // Use blockDataOffset_ to get properly aligned data start position
    char* data = reinterpret_cast<char*>(block) + blockDataOffset_;
    for (UInt32 i = 0; i < count; ++i) {
        tagUnitNode* unit = reinterpret_cast<tagUnitNode*>(data + i * pool->unitChunkSize);
        unit->pool = pool;
        unit->nextUnit = pool->freeList;
        pool->freeList = unit;
    }
    return true;
}

MemAllocator::tagMemPool* MemAllocator::findFitPool(Size size) {
    if (!pools_ || pools_->empty()) {
        return nullptr;
    }

    // Use map.lower_bound to find the first key >= size
    auto it = pools_->lower_bound(static_cast<UInt32>(size));
    if (it == pools_->end()) {
        return nullptr;
    }
    return &it->second;
}

void MemAllocator::freeAllPool() {
    CMutexGuard lock(&mutex_);
    if (!pools_) return;  // Safety check
    
    for (auto &kv : *pools_) {
        tagPoolBlock* block = kv.second.firstBlock;
        while (block) {
            tagPoolBlock* next = block->nextBlock;
            ::std::free(block);
            block = next;
        }
    }
    pools_->clear();
    currentPoolCount_ = 0;
}

void* MemAllocator::allocUnit(tagMemPool* pool) {
    CMutexGuard lock(&mutex_);

    if (!pool->freeList && !addPoolBlock(pool)) {
        INNER_CORE_LOG("Failed to allocate unit from pool");
        return nullptr;
    }
    tagUnitNode* unit = pool->freeList;
    pool->freeList = unit->nextUnit;
    unit->nextUnit = nullptr;
    unit->magic = MAKE_UNIT_NODE_MAGIC(unit);
    // Use systemChunkHeaderSize_ for properly aligned user pointer
    return reinterpret_cast<char*>(unit) + systemChunkHeaderSize_;
}

// MemChecker implementation
const Int32 MemChecker::SIZE_INFO_MAX_COUNT;

MemChecker::MemChecker()
    : memAllocator_(nullptr)
    , reportId_(0)
    , blockList_(nullptr)
    , compactSizeRange_(false)
    , badPtrAccessCount_(0)
    , threadCount_(0)
    , classCount_(0)
{
    std::memset(classNames_, 0, sizeof(classNames_));
    std::memset(threadNames_, 0, sizeof(threadNames_));
    // Initialize pthread mutex for MemChecker
    ::pthread_mutex_init(&syncMutex_, nullptr);
}

MemChecker::~MemChecker() {
    reportMemoryLeaks();
    CMutexGuard lock(&syncMutex_);
    while (blockList_) {
        unlinkBlock(blockList_);
    }
    ::pthread_mutex_destroy(&syncMutex_);
}

void MemChecker::initialize(Bool compactSizeRange, MemAllocator* memAllocator) {
    CMutexGuard lock(&syncMutex_);
    compactSizeRange_ = compactSizeRange;
    memAllocator_ = memAllocator;
    initSizeRange();
}

void* MemChecker::malloc(Size size, UInt32 classId) {
    CMutexGuard lock(&syncMutex_);
    return hookMalloc(size, classId);
}

void MemChecker::free(void* ptr) {
    if (!ptr) return;
    CMutexGuard lock(&syncMutex_);
    hookFree(ptr);
}

Int32 MemChecker::checkPtr(void* ptr, const Char* hint) {
    (void)hint; // Unused parameter
    CMutexGuard lock(&syncMutex_);
    if (!ptr) return -1;
    BlockHeaderPtr header = reinterpret_cast<BlockHeaderPtr>(static_cast<char*>(ptr) - sizeof(tagBlockHeader));
    EBlockStatus status = checkBlock(header);
    if (status != EBlockStatus::Ok) {
        ++badPtrAccessCount_;
        return static_cast<Int32>(status);
    }
    return 0;
}

UInt32 MemChecker::registerClassName(const Char* className)
{
    CMutexGuard lock(&syncMutex_);
    if (classCount_ >= MAX_CLASSES - 1) {
        return 0; // No more space
    }
    UInt32 classId = ++classCount_;
    std::strncpy(classNames_[classId], className, CLASS_NAME_MAX_LENGTH);
    classNames_[classId][CLASS_NAME_MAX_LENGTH] = '\0';
    return classId;
}

void MemChecker::registerThreadName(UInt32 threadId, const String& threadName) {
    CMutexGuard lock(&syncMutex_);
    
    // Find existing slot or allocate new one
    Int32 slotIndex = -1;
    
    // First, check if this thread ID already has a slot
    for (UInt32 i = 0; i < threadCount_ && i < SIZE_INFO_MAX_COUNT; ++i) {
        if (threadSizes_[i].threadId == threadId) {
            slotIndex = i;
            break;
        }
    }
    
    // If not found, allocate a new slot
    if (slotIndex == -1 && threadCount_ < SIZE_INFO_MAX_COUNT) {
        slotIndex = threadCount_;
        threadSizes_[slotIndex].threadId = threadId;
        threadSizes_[slotIndex].size = 0;
        threadCount_++;
    }
    
    // Store the thread name in the allocated slot
    if (slotIndex >= 0) {
        std::strncpy(threadNames_[slotIndex], threadName.c_str(), THREAD_NAME_MAX_LENGTH);
        threadNames_[slotIndex][THREAD_NAME_MAX_LENGTH] = '\0';
    }
}

void MemChecker::setReportFile(const String& reportFile) {
    CMutexGuard lock(&syncMutex_);
    std::strncpy(reportFile_, reportFile.c_str(), REPORT_FILE_MAX_LENGTH);
    reportFile_[REPORT_FILE_MAX_LENGTH] = '\0';
}

Bool MemChecker::outputState(UInt32 gpuMemorySize) {
    CMutexGuard lock(&syncMutex_);
    return outputStateToLogger(gpuMemorySize);
}

UInt32 MemChecker::getBlockExtSize() {
    return sizeof(tagBlockHeader);
}

UInt32 MemChecker::getThreadCount() {
    CMutexGuard lock(&syncMutex_);
    return threadCount_;
}

UInt32 MemChecker::getThreadID(UInt32 index) {
    CMutexGuard lock(&syncMutex_);
    return index < threadCount_ ? threadSizes_[index].threadId : 0;
}

Size MemChecker::getThreadSize(UInt32 index) {
    CMutexGuard lock(&syncMutex_);
    return index < threadCount_ ? threadSizes_[index].size : 0;
}

// Note: legacy lock/unlock removed; internal code uses CMutexGuard or direct pthread calls

void* MemChecker::hookMalloc(Size size, UInt32 classId) {
    Size totalSize = size + sizeof(tagBlockHeader);
    BlockHeaderPtr header = nullptr;
    Bool usedPool = false;
    if ( memAllocator_ ) {
        header = reinterpret_cast<BlockHeaderPtr>( memAllocator_->malloc(totalSize) );
        usedPool = true;
    } else {
        header = reinterpret_cast<BlockHeaderPtr>(::std::malloc(totalSize));
    }

    if (!header) return nullptr;

    // Set block header magic (mixed with runtime XOR mask for security)
    header->magic = makeBlockHeaderMagic();
    header->size = size;
    header->classId = classId;
    header->threadId = std::hash<std::thread::id>{}(std::this_thread::get_id());
    header->allocTag = usedPool ? 1u : 0u;
    // class name can be derived from classId on demand; avoid storing std::string in header
    linkBlock(header);
    logAllocSize(size);
    return reinterpret_cast<char*>(header) + sizeof(tagBlockHeader);
}

void MemChecker::hookFree(void* ptr) {
    if (!ptr) return;
    BlockHeaderPtr header = reinterpret_cast<BlockHeaderPtr>(static_cast<char*>(ptr) - sizeof(tagBlockHeader));
    if (checkBlock(header) != EBlockStatus::Ok) {
        ++badPtrAccessCount_;
        return;
    }
    logFreedSize(header->size);
    unlinkBlock(header);
       // When allocTag==1, header is the "user pointer" from MemAllocator's perspective
       // MemAllocator::free will subtract tagUnitNode size to find the actual allocation start
       if (memAllocator_ && header->allocTag == 1u) {
           memAllocator_->free(header);
       } else {
           ::std::free(header);
       }
}

MemChecker::EBlockStatus MemChecker::checkBlock(BlockHeaderPtr header) {
    if (!header) {
        return EBlockStatus::HeaderError;
    }
    if ( header->magic != makeBlockHeaderMagic() ) {
        return EBlockStatus::HeaderError;
    }
    return EBlockStatus::Ok;
}

MemChecker::ELinkStatus MemChecker::checkAllBlock(UInt32& errorBlockCount) {
    CMutexGuard lock(&syncMutex_);
    errorBlockCount = 0;
    for (BlockHeaderPtr p = blockList_; p; p = p->next) {
        if (checkBlock(p) != EBlockStatus::Ok) {
            ++errorBlockCount;
        }
    }
    return errorBlockCount > 0 ? ELinkStatus::HasBlockError : ELinkStatus::Ok;
}

void MemChecker::linkBlock(BlockHeaderPtr header) {
    header->next = blockList_;
    header->prev = nullptr;
    if (blockList_) {
        blockList_->prev = header;
    }
    blockList_ = header;

    blockStatAll_.currentCount++;
    blockStatAll_.currentSize += header->size;
    blockStatAll_.allocTimes++;
    if (blockStatAll_.currentCount > blockStatAll_.peakCount) {
        blockStatAll_.peakCount = blockStatAll_.currentCount;
        blockStatAll_.peakSize = blockStatAll_.currentSize;
    }

    Int32 index = calcRangeIndex(header->size);
    if (index >= 0) {
        blockStats_[index].currentCount++;
        blockStats_[index].currentSize += header->size;
        blockStats_[index].allocTimes++;
        if (blockStats_[index].currentCount > blockStats_[index].peakCount) {
            blockStats_[index].peakCount = blockStats_[index].currentCount;
            blockStats_[index].peakSize = blockStats_[index].currentSize;
        }
    }

    // Update per-thread size statistics using slot lookup
    Int32 slotIndex = -1;
    for (UInt32 i = 0; i < threadCount_ && i < SIZE_INFO_MAX_COUNT; ++i) {
        if (threadSizes_[i].threadId == header->threadId) { slotIndex = static_cast<Int32>(i); break; }
    }
    if (slotIndex == -1 && threadCount_ < SIZE_INFO_MAX_COUNT) {
        slotIndex = static_cast<Int32>(threadCount_);
        threadSizes_[threadCount_].threadId = header->threadId;
        threadSizes_[threadCount_].size = 0;
        ++threadCount_;
    }
    if (slotIndex >= 0) {
        threadSizes_[slotIndex].size += header->size;
    }
}

void MemChecker::unlinkBlock(BlockHeaderPtr header) {
    if (header->prev) {
        header->prev->next = header->next;
    } else {
        blockList_ = header->next;
    }
    if (header->next) {
        header->next->prev = header->prev;
    }

    blockStatAll_.currentCount--;
    blockStatAll_.currentSize -= header->size;

    Int32 index = calcRangeIndex(header->size);
    if (index >= 0) {
        blockStats_[index].currentCount--;
        blockStats_[index].currentSize -= header->size;
    }

    // Update per-thread size statistics using slot lookup
    Int32 slotIndex = -1;
    for (UInt32 i = 0; i < threadCount_ && i < SIZE_INFO_MAX_COUNT; ++i) {
        if (threadSizes_[i].threadId == header->threadId) { slotIndex = static_cast<Int32>(i); break; }
    }
    if (slotIndex >= 0) {
        threadSizes_[slotIndex].size -= header->size;
    }
}

Bool MemChecker::outputStateToLogger(UInt32 gpuMemorySize) {
    INNER_CORE_LOG("[INFO] Memory state: TotalSize=%zu, Count=%u, GPU=%u\n",
                  blockStatAll_.currentSize,
                  blockStatAll_.currentCount,
                  gpuMemorySize);
    return true;
}

void MemChecker::reportMemoryLeaks() {
    CMutexGuard lock(&syncMutex_);
    
    // Open a leak report file using C stdio APIs (required)
    FILE* logFile = ::std::fopen(MEMORY_LEAK_LOG_FILE.c_str(), "w");
    if (!logFile) {
        INNER_CORE_LOG("[ERROR] Failed to open memory leak log file: %s\n", MEMORY_LEAK_LOG_FILE.c_str());
    }
    
    if (!blockList_) {  
        INNER_CORE_LOG("[INFO] No memory leaks detected\n");
        if (logFile) {
            ::std::fprintf(logFile, "%s", "[INFO] No memory leaks detected\n");
            ::std::fclose(logFile);
        }
        return;
    }

    // Calculate total leaks
    Size totalLeaked = 0;
    UInt32 totalBlocks = 0;
    for (BlockHeaderPtr p = blockList_; p; p = p->next) {
        if (checkBlock(p) != EBlockStatus::Ok) continue;
        totalLeaked += p->size;
        totalBlocks++;
    }

    // Write leak summary
    INNER_CORE_LOG("[ERROR] LEAK SUMMARY: ==PID== definitely lost: %zu bytes in %u blocks\n", totalLeaked, totalBlocks);
    if (logFile) {
        ::std::fprintf(logFile,
                       "[ERROR] LEAK SUMMARY: ==PID== definitely lost: %zu bytes in %u blocks\n",
                       totalLeaked, totalBlocks);
    }

    // Helper lambda to find thread name by ID
    auto getThreadName = [this](UInt32 threadId) -> const char* {
        // Search through registered thread IDs
        for (UInt32 i = 0; i < threadCount_ && i < SIZE_INFO_MAX_COUNT; ++i) {
            if (threadSizes_[i].threadId == threadId && threadNames_[i][0] != '\0') {
                return threadNames_[i];
            }
        }
        return nullptr; // Not found
    };

    // Write each leaked block directly without aggregation
    for (BlockHeaderPtr p = blockList_; p; p = p->next) {
        if (checkBlock(p) != EBlockStatus::Ok) continue;
        
        const char* className = (p->classId == 0 || p->classId >= classCount_) 
                                ? "Global" 
                                : classNames_[p->classId];
        void* userPtr = static_cast<char*>(static_cast<void*>(p)) + sizeof(tagBlockHeader);
        const char* threadName = getThreadName(p->threadId);
        
        if (threadName) {
            INNER_CORE_LOG("[ERROR] Leaked: class=%s, ptr=0x%lx, size=%u, thread=%x(%s)\n",
                           className,
                           reinterpret_cast<uintptr_t>(userPtr),
                           static_cast<UInt32>(p->size),
                           p->threadId,
                           threadName);
            if (logFile) {
                ::std::fprintf(logFile,
                               "[ERROR] Leaked: class=%s, ptr=0x%lx, size=%u, thread=%x(%s)\n",
                               className,
                               reinterpret_cast<uintptr_t>(userPtr),
                               static_cast<UInt32>(p->size),
                               p->threadId,
                               threadName);
            }
            
        } else {
            INNER_CORE_LOG("[ERROR] Leaked: class=%s, ptr=0x%lx, size=%u, thread=%x\n",
                           className,
                           reinterpret_cast<uintptr_t>(userPtr),
                           static_cast<UInt32>(p->size),
                           p->threadId);
            if (logFile) {
                ::std::fprintf(logFile,
                               "[ERROR] Leaked: class=%s, ptr=0x%lx, size=%u, thread=%x\n",
                               className,
                               reinterpret_cast<uintptr_t>(userPtr),
                               static_cast<UInt32>(p->size),
                               p->threadId);
            }
        }
    }
    
    if (logFile) {
        ::std::fclose(logFile);
        INNER_CORE_LOG("[INFO] Memory leak report written to: %s\n", MEMORY_LEAK_LOG_FILE.c_str());
    }
}

void MemChecker::initSizeRange() {
    if (compactSizeRange_) {
        for (Int32 i = 0; i < SIZE_INFO_MAX_COUNT; ++i) {
            blockStats_[i].beginSize = i * COMPACT_SIZE_RANGE_STEP;
            blockStats_[i].endSize = (i + 1) * COMPACT_SIZE_RANGE_STEP - 1;
        }
    } else {
        for (Int32 i = 0; i < SIZE_INFO_MAX_COUNT; ++i) {
            blockStats_[i].beginSize = i * NORMAL_SIZE_RANGE_STEP;
            blockStats_[i].endSize = (i + 1) * NORMAL_SIZE_RANGE_STEP - 1;
        }
    }
}

Int32 MemChecker::calcRangeIndex(Size size) {
    if (compactSizeRange_) {
        return size / COMPACT_SIZE_RANGE_STEP >= SIZE_INFO_MAX_COUNT ? -1 : static_cast<Int32>(size / COMPACT_SIZE_RANGE_STEP);
    }
    return size / NORMAL_SIZE_RANGE_STEP >= SIZE_INFO_MAX_COUNT ? -1 : static_cast<Int32>(size / NORMAL_SIZE_RANGE_STEP);
}

void MemChecker::logAllocSize(Size size) {
    Int32 index = calcRangeIndex(size);
    if (index >= 0) {
        blockStats_[index].allocTimes++;
    }
}

void MemChecker::logFreedSize(Size size) {
    (void)size; // Handled in unlinkBlock
}

void MemChecker::buildClassStat(MapThreadStat& threadStats) {
    CMutexGuard lock(&syncMutex_);
    for (BlockHeaderPtr p = blockList_; p; p = p->next) {
        const char* className = (p->classId == 0 || p->classId >= classCount_) 
                                ? "Global" 
                                : classNames_[p->classId];
        threadStats[p->threadId][className].instanceCount++;
        threadStats[p->threadId][className].totalSize += p->size;
    }
}

// MemManager implementation
MemManager* MemManager::getInstance() {
    static MemManager instance;

     return &instance;
}

MemManager::MemManager()
    : listener_(nullptr)
    , memAllocator_(nullptr)
    , memChecker_(nullptr)
    , callbackActive_(false)
    , initialized_(false)
    , checkEnabled_(false)
    , alignByte_(DEFAULT_ALIGN_BYTE)  // Use system-appropriate alignment
    , runtimeXorMask_(0)
{
    // Initialize pthread mutex for MemManager callbacks
    ::pthread_mutex_init(&callbackMutex_, nullptr);
    // Generate runtime XOR mask early in constructor
    generateRuntimeXorMask();
    
    void* allocBuf = std::malloc(sizeof(MemAllocator));
    if (allocBuf) memAllocator_ = new (allocBuf) MemAllocator();

    // memAllocator_->initialize(alignByte_, MAX_POOL_COUNT);
}

MemManager::~MemManager() {
    if (memChecker_) {
        memChecker_->~MemChecker();
        std::free(memChecker_);
    }
    if (memAllocator_) {
        memAllocator_->~MemAllocator();
        std::free(memAllocator_);
    }
    ::pthread_mutex_destroy(&callbackMutex_);
}

void MemManager::uninitialize() {
    // Save pool configuration to ConfigManager before cleanup
    // Call savePoolConfig without holding lock (it acquires its own lock)
    if (memAllocator_) {
        savePoolConfig(DEFAULT_MEMORY_CONFIG);
    }
    
    // Now acquire lock to update initialized_ flag
    {
        CMutexGuard lock(&callbackMutex_);
        initialized_ = false;
    }
}

void MemManager::initialize()
{
    // Minimal, non-config initialization: set up allocator with defaults only.
    // Configuration (align/check_enable/pools) should be explicitly loaded via loadPoolConfig.
    
    // Check if already initialized
    {
        CMutexGuard lock(&callbackMutex_);
        if ( initialized_ ) return;
    }

    // Load config and create pools WITHOUT holding lock to avoid deadlock
    MemAllocator::PoolConfig localConfigs[MAX_POOL_CONFIG_ENTRIES];
    size_t cfgCount = loadPoolConfig(localConfigs, MAX_POOL_CONFIG_ENTRIES, DEFAULT_MEMORY_CONFIG);

    memAllocator_->initialize(alignByte_, MAX_POOL_COUNT);

    for (size_t i = 0; i < cfgCount; ++i) {
        const auto& c = localConfigs[i];
        (void)memAllocator_->createPool(c.unitSize, c.initCount, c.maxCount, c.appendCount);
    }

    // Construct-only: enable checker when explicitly requested; do not destroy on false/missing
    if ( checkEnabled_ && !memChecker_ ) {
        void* checkerBuf = std::malloc(sizeof(MemChecker));
        if (checkerBuf) {
            memChecker_ = new (checkerBuf) MemChecker();
            memChecker_->initialize(true, memAllocator_);
        }
    }

    // Mark as initialized
    {
        CMutexGuard lock(&callbackMutex_);
        initialized_ = true;
    }
}

void MemManager::setListener(IMemListener* listener) { 
    CMutexGuard lock(&callbackMutex_);
    listener_ = listener;
}

UInt32 MemManager::getCurrentAllocCount()
{
    CMutexGuard lock(&callbackMutex_);
    if (!memChecker_) return 0u;
    return memChecker_->getCurrentAllocCount();
}

Size MemManager::getCurrentAllocSize() {
    
    CMutexGuard lock(&callbackMutex_);
    if (!memChecker_) return 0;
    return memChecker_->getCurrentAllocSize();
}

UInt32 MemManager::getThreadCount() {
    
    CMutexGuard lock(&callbackMutex_);
    if (!memChecker_) return 0u;
    return memChecker_->getThreadCount();
}

UInt32 MemManager::getThreadID(UInt32 index) {
    
    CMutexGuard lock(&callbackMutex_);
    if (!memChecker_) return 0u;
    return memChecker_->getThreadID(index);
}

Size MemManager::getThreadSize(UInt32 index) {
    
    CMutexGuard lock(&callbackMutex_);
    if (!memChecker_) return 0;
    return memChecker_->getThreadSize(index);
}

MemoryStats MemManager::getMemoryStats() {
    MemoryStats stats;
    stats.currentAllocSize = 0;
    stats.currentAllocCount = 0;
    stats.totalPoolMemory = 0;
    stats.poolCount = 0;
    stats.threadCount = 0;
    
    
    CMutexGuard lock(&callbackMutex_);
    
    // Get pool statistics directly from allocator
    if (memAllocator_) {
        stats.poolCount = memAllocator_->getPoolCount();
        
        // Iterate through all pools to sum up memory usage
        for (UInt32 i = 0; i < stats.poolCount; ++i) {
            MemAllocator::MemoryPoolState poolState;
            if (memAllocator_->getPoolState(i, poolState)) {
                // Calculate allocated blocks in this pool
                UInt32 allocatedCount = poolState.currentCount - poolState.freeCount;
                Size allocatedSize = allocatedCount * poolState.unitAvailableSize;
                
                stats.currentAllocCount += allocatedCount;
                stats.currentAllocSize += allocatedSize;
                stats.totalPoolMemory += poolState.memoryCost;
            }
        }
    }
    
    // Get thread count from checker if available
    if (memChecker_) {
        stats.threadCount = memChecker_->getThreadCount();
    }
    
    return stats;
}

void* MemManager::malloc(Size size, const Char* className, UInt32 classId)
{
    // Fast path: check initialized without lock first
    // During pre-initialization (before MemManager::initialize completes),
    // use system malloc with PREINIT_MAGIC marker to avoid deadlock/crash    
    if ( DEF_LAP_IF_UNLIKELY( !initialized_) ) {
        // Pre-initialization: use system malloc with magic marker
        Size totalSize = size + sizeof(PreInitHeader);
        PreInitHeader* header = reinterpret_cast<PreInitHeader*>(std::malloc(totalSize));
        if (!header) return nullptr;
        header->magic = PREINIT_MAGIC;
        header->size = size;
        return reinterpret_cast<char*>(header) + sizeof(PreInitHeader);
    }

    void* ptr = nullptr;
    {
        if (memChecker_) {
            ptr = memChecker_->malloc(size, classId ? classId : className ? registerClassName(className) : 0);
        } else {
            ptr = memAllocator_->malloc(size);
        }
    }

    if ( ptr != nullptr ) {
        return ptr;
    }

    if ( listener_ != nullptr && !callbackActive_ ) {
    CMutexGuard lock(&callbackMutex_);

        callbackActive_ = true;

        listener_->onOutOfMemory(size);
    }

    return ptr;
}

void MemManager::free(void* ptr) {
    if (!ptr) return;
    
    // Check for pre-initialization allocation by inspecting magic field
    // Pre-init blocks have PREINIT_MAGIC and should use system free
    PreInitHeader* preInitHeader = reinterpret_cast<PreInitHeader*>(static_cast<char*>(ptr) - sizeof(PreInitHeader));
    if (preInitHeader->magic == PREINIT_MAGIC) {
        // This was allocated before initialization - use system free
        std::free(preInitHeader);
        return;
    }

    if (memChecker_) {
        memChecker_->free(ptr);
    } else if (memAllocator_) {
        memAllocator_->free(ptr);
    } else {
        std::free(ptr);
    }
}

Int32 MemManager::checkPtr(void* ptr, const Char* hint) {
    
    CMutexGuard lock(&callbackMutex_);
    if (!memChecker_) {
        return 0; // No checking if checker is disabled
    }
    return memChecker_->checkPtr(ptr, hint);
}

UInt32 MemManager::registerClassName(const Char* className) {
    
    CMutexGuard lock(&callbackMutex_);
    if (!memChecker_) return 0u;
    return memChecker_->registerClassName(className);
}

Bool MemManager::outputState(UInt32 gpuMemorySize) {
    
    CMutexGuard lock(&callbackMutex_);
    if (!memChecker_) {
        return false;
    }
    return memChecker_->outputState(gpuMemorySize);
}

void MemManager::registerThreadName(UInt32 threadId, const String& threadName) {
    
    CMutexGuard lock(&callbackMutex_);
    if (memChecker_) {
        memChecker_->registerThreadName(threadId, threadName);
    }
}

Bool MemManager::savePoolConfig(const String& moduleName) {
    CMutexGuard lock(&callbackMutex_);
    
    try {
        // Build JSON configuration object
        nlohmann::json config;
        config["align"] = alignByte_;
        config["check_enable"] = (memChecker_ != nullptr);
        
        // Build pools array
        nlohmann::json poolsArray = nlohmann::json::array();
        UInt32 poolCount = memAllocator_->getPoolCount();
        for (UInt32 i = 0; i < poolCount; ++i) {
            MemAllocator::MemoryPoolState st{};
            if (!memAllocator_->getPoolState(i, st)) continue;

            UInt32 initCountOut = st.currentCount; // fallback
            if (!memChecker_) {
                // When checker is disabled, record "real used" units
                initCountOut = (st.currentCount > st.freeCount) ? (st.currentCount - st.freeCount) : 0u;
            }
            
            nlohmann::json poolObj;
            poolObj["unitSize"] = st.unitAvailableSize;
            poolObj["initCount"] = initCountOut;
            poolObj["maxCount"] = st.maxCount;
            poolObj["appendCount"] = st.appendCount;
            poolsArray.push_back(poolObj);
        }
        config["pools"] = poolsArray;
        
        // Save to ConfigManager
        ConfigManager& configMgr = ConfigManager::getInstance();
        auto result = configMgr.setModuleConfigJson(moduleName, config);
        if (!result.HasValue()) {
            INNER_CORE_LOG("[ERROR] Failed to save memory config to ConfigManager module '%s'\n", moduleName.c_str());
            return false;
        }
        
        INNER_CORE_LOG("[INFO] Saved memory config to ConfigManager module '%s'\n", moduleName.c_str());
        return true;
    } catch (...) {
        INNER_CORE_LOG("[ERROR] Exception while saving memory config\n");
        return false;
    }
}

size_t MemManager::loadPoolConfig(MemAllocator::PoolConfig* out, size_t maxCount, const String& fileName) {
    // Load configuration directly from ConfigManager module "memory"
    // ConfigManager must be initialized before calling this
    (void)fileName; // Unused parameter, kept for API compatibility
    if (!out || maxCount == 0) {
        return 0;
    }
    
    // Use fixed array instead of std::map to avoid memory allocation
    struct PoolEntry {
        UInt32 unitSize;
        MemAllocator::PoolConfig config;
        bool used;
    };
    PoolEntry agg[MAX_POOL_CONFIG_ENTRIES]; // Support up to MAX_POOL_CONFIG_ENTRIES different pool sizes
    UInt32 aggCount = 0;
    
    // Initialize with zeros
    std::memset(agg, 0, sizeof(agg));

    // Default pools configuration
    for (UInt32 s = MIN_POOL_UNIT_SIZE; s <= MAX_POOL_UNIT_SIZE; s <<= 1) {
        if (aggCount >= MAX_POOL_CONFIG_ENTRIES) break;
        agg[aggCount].unitSize = s;
        agg[aggCount].config.unitSize = s;
        agg[aggCount].config.initCount = DEFAULT_POOL_INIT_COUNT;
        agg[aggCount].config.maxCount = 0;
        agg[aggCount].config.appendCount = DEFAULT_POOL_INIT_COUNT;
        agg[aggCount].used = true;
        aggCount++;
    }
    
    try {
        // Get configuration from ConfigManager module "memory"
        nlohmann::json j = ConfigManager::getInstance().getModuleConfigJson("memory");

        if ( DEF_LAP_IF_UNLIKELY( j.is_null() || j.empty() ) ) {
            // No memory configuration found, use defaults
            INNER_CORE_LOG("MemManager: No 'memory' module config found, using defaults\n");
        } else {
            INNER_CORE_LOG("MemManager: Loaded configuration from ConfigManager module 'memory'\n");
            
            // Parse check_enable flag
            if (j.contains("check_enable") && j["check_enable"].is_boolean()) {
                checkEnabled_ = j["check_enable"].get<bool>();
            }
            
            // Parse align setting with validation
            if (j.contains("align") && j["align"].is_number_unsigned()) {
                UInt32 configAlign = j["align"].get<UInt32>();
                
                // Validate alignment: must be power of 2 and non-zero
                if (configAlign == 0 || (configAlign & (configAlign - 1)) != 0) {
                    INNER_CORE_LOG("[WARNING] Invalid align value %u in config (must be power of 2), using DEFAULT_ALIGN_BYTE=%u\n",
                                   configAlign, DEFAULT_ALIGN_BYTE);
                    alignByte_ = DEFAULT_ALIGN_BYTE;
                } else {
                    // Accept the configured value, even if less than system default
                    // This allows flexibility for special use cases (e.g., byte-aligned serialization)
                    if (configAlign < DEFAULT_ALIGN_BYTE) {
                        INNER_CORE_LOG("[WARNING] Config align value %u is less than system recommended %u. "
                                       "This may impact performance but will be honored for compatibility.\n",
                                       configAlign, DEFAULT_ALIGN_BYTE);
                    } else if (configAlign > DEFAULT_ALIGN_BYTE) {
                        INNER_CORE_LOG("[INFO] Using custom alignment %u bytes (system default: %u)\n",
                                       configAlign, DEFAULT_ALIGN_BYTE);
                    }
                    alignByte_ = configAlign;
                }
            }

            // Determine pools array: prefer j["pools"], fall back to top-level array for legacy
            const nlohmann::json* poolsNode = nullptr;
            if (j.contains("pools") && j["pools"].is_array()) {
                poolsNode = &j["pools"];
            } else if (j.is_array()) {
                poolsNode = &j; // legacy top-level array format
            }

            if (poolsNode) {
                for (const auto& node : *poolsNode) {
                    if (!node.is_object()) continue;
                    // Required fields
                    if (!node.contains("unitSize") || !node.contains("initCount") ||
                        !node.contains("maxCount") || !node.contains("appendCount")) {
                        continue;
                    }
                    // Type checks
                    if (!node["unitSize"].is_number_unsigned() ||
                        !node["initCount"].is_number_unsigned() ||
                        !node["maxCount"].is_number_unsigned() ||
                        !node["appendCount"].is_number_unsigned()) {
                        continue;
                    }

                    UInt32 unitSize = node["unitSize"].get<UInt32>();
                    UInt32 initCount = node["initCount"].get<UInt32>();
                    UInt32 maxCount = node["maxCount"].get<UInt32>();
                    UInt32 appendCount = node["appendCount"].get<UInt32>();

                    if (unitSize == 0u || ((maxCount != 0u) && (initCount > maxCount))) continue;

                    UInt32 norm = roundUpPow2Clamp(unitSize);
                    if (norm == 0u) continue; // >MAX_POOL_UNIT_SIZE: skip pool, will use system allocation

                    // Find or create entry for this unitSize
                    Int32 idx = -1;
                    for (UInt32 i = 0; i < aggCount; i++) {
                        if (agg[i].unitSize == norm) {
                            idx = static_cast<Int32>(i);
                            break;
                        }
                    }
                    if (idx == -1 && aggCount < MAX_POOL_CONFIG_ENTRIES) {
                        idx = static_cast<Int32>(aggCount);
                        aggCount++;
                        agg[idx].unitSize = norm;
                        agg[idx].config.unitSize = norm;
                        agg[idx].config.initCount = initCount;
                        agg[idx].config.maxCount = maxCount;
                        agg[idx].config.appendCount = appendCount;
                        agg[idx].used = true;
                    } else if (idx >= 0) {
                        agg[idx].config.initCount = std::max(agg[idx].config.initCount, initCount);
                        agg[idx].config.maxCount = std::max(agg[idx].config.maxCount, maxCount);
                        agg[idx].config.appendCount = std::max(agg[idx].config.appendCount, appendCount);
                    }
                }
            }
        }
    } catch (...) {
        INNER_CORE_LOG("MemManager: Exception loading config, using defaults\n");
    }

    size_t written = 0;
    for (UInt32 i = 0; i < aggCount && written < maxCount; i++) {
        if (agg[i].used) {
            out[written++] = agg[i].config;
        }
    }

    return written;
}

void MemManager::generateRuntimeXorMask() {
    // Mix multiple runtime values for enhanced randomness:
    // 1. Process ID
    // 2. High-resolution timestamp (nanoseconds)
    // 3. Thread ID
    // 4. Stack address (ASLR contribution)
    // 5. Heap address (another ASLR source)
    
    MagicType mask = MAGIC_XOR_VALUE;  // Start with base constant
    
    // Mix in process ID
    pid_t pid = getpid();
    mask ^= static_cast<MagicType>(pid);
    mask = (mask << 13) | (mask >> (sizeof(MagicType)*8 - 13));  // rotate
    
    // Mix in high-resolution timestamp
    auto now = std::chrono::high_resolution_clock::now();
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
    mask ^= static_cast<MagicType>(ns);
    mask = (mask << 7) | (mask >> (sizeof(MagicType)*8 - 7));  // rotate
    
    // Mix in thread ID
    auto tid = std::this_thread::get_id();
    mask ^= std::hash<std::thread::id>{}(tid);
    mask = (mask << 17) | (mask >> (sizeof(MagicType)*8 - 17));  // rotate
    
    // Mix in stack address (ASLR)
    int stackVar;
    mask ^= reinterpret_cast<uintptr_t>(&stackVar);
    mask = (mask << 11) | (mask >> (sizeof(MagicType)*8 - 11));  // rotate
    
    // Mix in this object's address (heap ASLR) - safe, no allocation needed
    mask ^= reinterpret_cast<uintptr_t>(this);
    mask = (mask << 5) | (mask >> (sizeof(MagicType)*8 - 5));  // rotate
    
    // Final scramble: multiply by a large prime and XOR with shifted version
    #if defined(__LP64__) || defined(_WIN64) || (defined(__WORDSIZE) && __WORDSIZE == 64)
        mask *= 0x5DEECE66DUL;  // Large prime from LCG
        mask ^= (mask >> 32);
    #else
        mask *= 0x5DEECE66D;
        mask ^= (mask >> 16);
    #endif
    
    runtimeXorMask_ = mask;
    
    #ifdef LAP_STRICT_LAYOUT
        // Log the generated mask in debug builds (redacted for security in release)
        char maskStr[32];
        snprintf(maskStr, sizeof(maskStr), "0x%lx", static_cast<unsigned long>(mask));
        INNER_CORE_LOG("MemManager: Generated runtime XOR mask");
    #endif
}

MagicType MemManager::getRuntimeXorMask() noexcept {
    return getInstance()->runtimeXorMask_;
}

// Memory implementation
void* Memory::malloc(Size size, const Char* className, UInt32 classId) noexcept {
    try {
        return MemManager::getInstance()->malloc(size, className, classId);
    } catch (...) {
        return nullptr;
    }
}

void Memory::free(void* ptr) noexcept {
    try {
        MemManager::getInstance()->free(ptr);
    } catch (...) {
        // swallow
    }
}

Int32 Memory::checkPtr(void* ptr, const Char* hint) noexcept {
    try {
        return MemManager::getInstance()->checkPtr(ptr, hint);
    } catch (...) {
        return -1;
    }
}

UInt32 Memory::registerClassName(const Char* className) noexcept {
    try {
        return MemManager::getInstance()->registerClassName(className);
    } catch (...) {
        return 0u;
    }
}

MemoryStats Memory::getMemoryStats() noexcept {
    try {
        return MemManager::getInstance()->getMemoryStats();
    } catch (...) {
        // Return empty stats on error
        MemoryStats stats;
        stats.currentAllocSize = 0;
        stats.currentAllocCount = 0;
        stats.totalPoolMemory = 0;
        stats.poolCount = 0;
        stats.threadCount = 0;
        return stats;
    }
}

} // namespace lap::core
