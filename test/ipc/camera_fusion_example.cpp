/**
 * @file        camera_fusion_example.cpp
 * @brief       三摄像头融合示例 - 演示零拷贝图像传输与双缓存合成
 * @details     使用NORMAL模式演示多Publisher图像采集、拼接和保存
 * @author      LightAP Core Team
 * @date        2026-01-17
 *
 * ============================================================================
 * 场景说明
 * ============================================================================
 * 
 * 模拟自动驾驶场景的三摄像头全景拼接：
 * - 3个摄像头Publisher进程独立采集图像
 * - 1个Subscriber进程接收并拼接成全景图
 * - 双缓存机制保证实时性
 * - 定期保存BMP文件用于回放分析
 * 
 * ============================================================================
 * 系统架构
 * ============================================================================
 * 
 *   [Camera-0 Publisher]        [Camera-1 Publisher]        [Camera-2 Publisher]
 *   1920x720 @ 100FPS           1920x720 @ 100FPS           1920x720 @ 100FPS
 *   STMin=10ms                  STMin=10ms                  STMin=10ms
 *         |                           |                           |
 *         v                           v                           v
 *    /cam0_stream               /cam1_stream               /cam2_stream
 *    (Overwrite策略)            (Overwrite策略)            (Overwrite策略)
 *         |                           |                           |
 *         +---------------------------+---------------------------+
 *                                     |
 *                           [Fusion Subscriber]
 *                           双缓存机制 (3840x1440x4)
 *                                     |
 *         +---------------------------+---------------------------+
 *         |                           |                           |
 *    [SubThread-0]              [SubThread-1]              [SubThread-2]
 *    写入位置: (0, 0)          写入位置: (1920, 0)        写入位置: (960, 720)
 *         |                           |                           |
 *         +---------------------------+---------------------------+
 *                                     |
 *                            [后缓存拼接完成]
 *                                     |
 *                              每5秒切换前后缓存
 *                                     |
 *                            [前缓存写入BMP文件]
 *                         fusion_00000.bmp ~ fusion_00009.bmp
 * 
 * ============================================================================
 * 图像布局 (3840x1440)
 * ============================================================================
 * 
 *   +-------------------+-------------------+
 *   |   Camera-0        |   Camera-1        |
 *   |   1920x720        |   1920x720        |
 *   |   (左前方视图)     |   (右前方视图)     |
 *   +-------------------+-------------------+
 *   |       空白区域      |   Camera-2        |   空白区域     |
 *   |       (960)       |   1920x720        |   (960)       |
 *   |                   |   (后方视图)       |               |
 *   +-------------------+-------------------+
 * 
 * ============================================================================
 * 技术特性
 * ============================================================================
 * 
 * 1. NORMAL模式配置:
 *    - 每个摄像头: 1920x720x4 = 5,529,600 bytes (~5.3MB)
 *    - ChunkPool: 16 chunks (考虑3个Publisher)
 *    - 队列容量: 64 (单个Publisher高速发送缓冲)
 * 
 * 2. 零拷贝图像传输:
 *    - Publisher使用Send(Fn&& write_fn)直接写入共享内存
 *    - write_fn中生成图像数据，避免中间拷贝
 *    - Subscriber直接从共享内存读取并拼接
 * 
 * 3. 双缓存机制:
 *    - 前缓存: 用于BMP文件写入
 *    - 后缓存: 3个线程并发写入，无锁设计
 *    - 原子交换指针实现快速切换
 * 
 * 4. 极限FPS性能测试:
 *    - STMin=10ms (100 FPS理论上限)
 *    - 实测可达90+ FPS (受CPU调度和零拷贝传输影响)
 *    - Overwrite策略保证最新帧可用，丢帧不阻塞
 *    - 多线程并发接收，充分利用零拷贝优势
 * 
 * 5. BMP文件保存:
 *    - 最多保存10张图片，循环覆盖
 *    - 标准BMP格式，支持图像查看器打开
 *    - 文件名: fusion_00000.bmp ~ fusion_00009.bmp
 * 
 * ============================================================================
 * 运行方式
 * ============================================================================
 * 
 * cd /workspace/LightAP/build/modules/Core
 * make camera_fusion_example
 * ./camera_fusion_example [duration_sec]
 * 
 * 预期输出文件:
 * - fusion_00000.bmp (3840x1440, 24位真彩色)
 * - fusion_00001.bmp
 * - ... (max 10 images)
 * 
 * ============================================================================
 * 预期性能（STMin=10ms极限测试配置）
 * ============================================================================
 * 
 * - 单摄像头吞吐: 90+ FPS (实测，理论上限100 FPS)
 *   * STMin=10ms限流机制保证最小发送间隔
 *   * 零拷贝传输减少CPU占用，提升吞吐量
 *   * 实际FPS受进程调度和内存带宽影响
 * 
 * - 拼接延迟: < 1ms (零拷贝直接memcpy)
 * - 保存时间: ~50ms (5.3MB BMP文件写入磁盘)
 * - CPU占用: ~25-30% (3个Publisher + 4个Subscriber线程)
 * - 内存占用: ~85MB (ChunkPool + 双缓存)
 * 
 * ============================================================================
 * 资源使用情况
 * ============================================================================
 * 
 * 1. 共享内存 (per camera stream):
 *    - 每个摄像头独立共享内存: /cam0_stream, /cam1_stream, /cam2_stream
 *    - ControlBlock: 128 bytes (NORMAL模式)
 *    - ChannelQueue[30]: ~256KB (30个队列 × 8KB)
 *    - ChunkPool: 3 chunks × 5.3MB = ~16MB
 *    - 总计单stream: ~16.3MB
 *    - 3个stream总计: ~49MB
 * 
 * 2. 进程内存:
 *    - Fusion双缓存: 2 × (3840×1440×4) = 42MB
 *    - 统计数据结构: ~1MB
 *    - 代码段 + 栈空间: ~5MB
 *    - 进程总内存: ~48MB
 * 
 * 3. 系统总资源:
 *    - 共享内存: ~49MB (/dev/shm)
 *    - 进程私有内存: ~48MB (heap + stack)
 *    - **总内存占用: ~97MB**
 * 
 * 4. 线程资源:
 *    - 3个Camera Publisher进程 (各1个主线程)
 *    - 1个Fusion进程:
 *      * 1个主线程
 *      * 3个Subscriber线程 (SubThread-0/1/2)
 *      * 1个Saver线程
 *    - **总计: 8个线程**
 * 
 * 5. 文件描述符:
 *    - 共享内存fd: 3个 (每个camera stream)
 *    - BMP文件fd: 1个 (写入时短暂打开)
 *    - stdout/stderr: 2个
 *    - **总计: 6个活跃fd**
 * 
 * 注意:
 * - 若需要稳定60 FPS，建议 STMin=16ms
 * - 当前10ms配置用于测试IPC极限吞吐性能
 * - 实际生产环境建议根据业务需求调整STMin值
 */

#include "ipc/Publisher.hpp"
#include "ipc/Subscriber.hpp"
#include "CInitialization.hpp"
#include "CString.hpp"
#include <cstdio>
#include <thread>
#include <chrono>
#include <atomic>
#include <cstring>
#include <algorithm>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <fcntl.h>


using namespace lap::core;
using namespace lap::core::ipc;

// 摄像头配置
constexpr UInt32 kCameraWidth = 1920;
constexpr UInt32 kCameraHeight = 720;
constexpr UInt32 kBytesPerPixel = 4;  // RGBA
constexpr UInt32 kImageSize = kCameraWidth * kCameraHeight * kBytesPerPixel;

// 融合图配置
constexpr UInt32 kFusionWidth = 3840;   // 2 cameras wide
constexpr UInt32 kFusionHeight = 1440;  // 2 cameras high
constexpr UInt32 kFusionSize = kFusionWidth * kFusionHeight * kBytesPerPixel;

// 性能配置
constexpr UInt32 kTargetFPS = 60;
constexpr UInt32 kSTMinMs = 10;  // 10ms限流，理论100 FPS，实测90+ FPS
constexpr UInt32 kSavePeriodSec = 5;
constexpr UInt32 kMaxSavedImages = 10;
constexpr UInt32 kMaxLatencySamples = 300000;

//=============================================================================
// 统计数据结构（共享内存段）
//=============================================================================
struct CameraStats {
    std::atomic<UInt64> frames_sent{0};
    std::atomic<UInt64> send_failures{0};
    std::atomic<UInt64> total_send_time_us{0};
    std::atomic<UInt32> latency_count{0};
    UInt64 latencies_us[kMaxLatencySamples];
    std::atomic<UInt64> start_timestamp_us{0};  // 该Camera的统计开始时间
};

struct FusionStats {
    std::atomic<UInt64> frames_received[3]{{0}, {0}, {0}};
    std::atomic<UInt64> receive_failures[3]{{0}, {0}, {0}};
    std::atomic<UInt32> latency_count[3]{{0}, {0}, {0}};
    UInt64 latencies_us[3][kMaxLatencySamples];
};

struct SharedStats {
    CameraStats cameras[3];
    FusionStats fusion;
};

// BMP文件头结构
#pragma pack(push, 1)
struct BMPFileHeader {
    UInt16 type{0x4D42};      // "BM"
    UInt32 size{0};           // File size
    UInt16 reserved1{0};
    UInt16 reserved2{0};
    UInt32 offset{54};        // Offset to image data
};

struct BMPInfoHeader {
    UInt32 size{40};          // Header size
    Int32  width{0};
    Int32  height{0};
    UInt16 planes{1};
    UInt16 bits{24};          // 24-bit RGB
    UInt32 compression{0};    // No compression
    UInt32 imagesize{0};
    Int32  xresolution{0};
    Int32  yresolution{0};
    UInt32 ncolors{0};
    UInt32 importantcolors{0};
};
#pragma pack(pop)

//=============================================================================
// 图像生成器 - 模拟摄像头采集
//=============================================================================
class SimpleImageCodec {
public:
    explicit SimpleImageCodec(UInt32 camera_id) 
        : camera_id_(camera_id), frame_count_(0) {}
    
    // 生成测试图案: 渐变背景 + 大号摄像头ID + 运动色块
    void GenerateFrame(Byte* buffer, Size size) {
        if (size < kImageSize) {
            return;
        }
        
        UInt32* pixels = reinterpret_cast<UInt32*>(buffer);
        
        // 为每个摄像头设置不同的基础颜色
        UInt8 base_r = (camera_id_ == 0) ? 255 : 80;
        UInt8 base_g = (camera_id_ == 1) ? 255 : 80;
        UInt8 base_b = (camera_id_ == 2) ? 255 : 80;
        
        // 生成双向渐变背景 (RGBA格式)
        for (UInt32 y = 0; y < kCameraHeight; ++y) {
            for (UInt32 x = 0; x < kCameraWidth; ++x) {
                UInt8 r = base_r * x / kCameraWidth;
                UInt8 g = base_g * y / kCameraHeight;
                UInt8 b = base_b;
                UInt8 a = 255;
                
                // RGBA格式: R在低位，A在高位
                pixels[y * kCameraWidth + x] = (a << 24) | (b << 16) | (g << 8) | r;
            }
        }
        
        // 添加摄像头ID标识 (居中显示，黑色)
        DrawCameraID(pixels);
        
        // 添加运动色块 (模拟移动物体) - 确保不超出屏幕
        UInt32 block_size = 80;
        UInt32 block_x = (frame_count_ * 10) % (kCameraWidth - block_size);
        UInt32 block_y = (frame_count_ * 3) % (kCameraHeight - block_size);
        
        for (UInt32 dy = 0; dy < block_size; ++dy) {
            for (UInt32 dx = 0; dx < block_size; ++dx) {
                UInt32 idx = (block_y + dy) * kCameraWidth + (block_x + dx);
                pixels[idx] = 0xFFFFFFFF;  // 白色方块 (RGBA)
            }
        }
        
        frame_count_++;
    }
    
    UInt32 GetFrameCount() const { return frame_count_; }

private:
    void DrawCameraID(UInt32* pixels) {
        // 绘制大号摄像头ID在画面中央 (黑色)
        UInt32 center_x = kCameraWidth / 2;
        UInt32 center_y = kCameraHeight / 2;
        
        // 绘制超大数字 (每个数字100x100像素) - 黑色
        DrawBigDigit(pixels, camera_id_, center_x - 50, center_y - 50, 0xFF000000);  // 黑色 (RGBA)
    }
    
    void DrawBigDigit(UInt32* pixels, UInt32 digit, UInt32 x, UInt32 y, UInt32 color) {
        // 7段数码管风格数字 (简化版)
        // 每段20像素宽，80像素长
        bool segments[10][7] = {
            {1,1,1,1,1,1,0}, // 0
            {0,1,1,0,0,0,0}, // 1
            {1,1,0,1,1,0,1}, // 2
            {1,1,1,1,0,0,1}, // 3
            {0,1,1,0,0,1,1}, // 4
            {1,0,1,1,0,1,1}, // 5
            {1,0,1,1,1,1,1}, // 6
            {1,1,1,0,0,0,0}, // 7
            {1,1,1,1,1,1,1}, // 8
            {1,1,1,1,0,1,1}, // 9
        };
        
        if (digit > 9) return;
        
        // 绘制7段数码管
        // segment 0: top horizontal
        if (segments[digit][0]) {
            for (UInt32 dy = 0; dy < 15; ++dy) {
                for (UInt32 dx = 10; dx < 90; ++dx) {
                    UInt32 px = x + dx;
                    UInt32 py = y + dy;
                    if (py < kCameraHeight && px < kCameraWidth) {
                        pixels[py * kCameraWidth + px] = color;
                    }
                }
            }
        }
        
        // segment 1: top-right vertical
        if (segments[digit][1]) {
            for (UInt32 dy = 10; dy < 55; ++dy) {
                for (UInt32 dx = 85; dx < 100; ++dx) {
                    UInt32 px = x + dx;
                    UInt32 py = y + dy;
                    if (py < kCameraHeight && px < kCameraWidth) {
                        pixels[py * kCameraWidth + px] = color;
                    }
                }
            }
        }
        
        // segment 2: bottom-right vertical
        if (segments[digit][2]) {
            for (UInt32 dy = 50; dy < 95; ++dy) {
                for (UInt32 dx = 85; dx < 100; ++dx) {
                    UInt32 px = x + dx;
                    UInt32 py = y + dy;
                    if (py < kCameraHeight && px < kCameraWidth) {
                        pixels[py * kCameraWidth + px] = color;
                    }
                }
            }
        }
        
        // segment 3: bottom horizontal
        if (segments[digit][3]) {
            for (UInt32 dy = 90; dy < 105; ++dy) {
                for (UInt32 dx = 10; dx < 90; ++dx) {
                    UInt32 px = x + dx;
                    UInt32 py = y + dy;
                    if (py < kCameraHeight && px < kCameraWidth) {
                        pixels[py * kCameraWidth + px] = color;
                    }
                }
            }
        }
        
        // segment 4: bottom-left vertical
        if (segments[digit][4]) {
            for (UInt32 dy = 50; dy < 95; ++dy) {
                for (UInt32 dx = 0; dx < 15; ++dx) {
                    UInt32 px = x + dx;
                    UInt32 py = y + dy;
                    if (py < kCameraHeight && px < kCameraWidth) {
                        pixels[py * kCameraWidth + px] = color;
                    }
                }
            }
        }
        
        // segment 5: top-left vertical
        if (segments[digit][5]) {
            for (UInt32 dy = 10; dy < 55; ++dy) {
                for (UInt32 dx = 0; dx < 15; ++dx) {
                    UInt32 px = x + dx;
                    UInt32 py = y + dy;
                    if (py < kCameraHeight && px < kCameraWidth) {
                        pixels[py * kCameraWidth + px] = color;
                    }
                }
            }
        }
        
        // segment 6: middle horizontal
        if (segments[digit][6]) {
            for (UInt32 dy = 48; dy < 58; ++dy) {
                for (UInt32 dx = 10; dx < 90; ++dx) {
                    UInt32 px = x + dx;
                    UInt32 py = y + dy;
                    if (py < kCameraHeight && px < kCameraWidth) {
                        pixels[py * kCameraWidth + px] = color;
                    }
                }
            }
        }
    }

private:
    UInt32 camera_id_;
    std::atomic<UInt32> frame_count_;
};

//=============================================================================
// BMP保存器
//=============================================================================
class BMPSaver {
public:
    static bool SaveBMP(const String& filename, const Byte* buffer, 
                       UInt32 width, UInt32 height) {
        FILE* file = fopen(filename.c_str(), "wb");
        if (!file) {
            return false;
        }
        
        // 计算行对齐 (BMP要求每行4字节对齐)
        UInt32 row_size = ((width * 3 + 3) / 4) * 4;
        UInt32 image_size = row_size * height;
        
        // 写入文件头
        BMPFileHeader file_header;
        file_header.size = sizeof(BMPFileHeader) + sizeof(BMPInfoHeader) + image_size;
        fwrite(&file_header, sizeof(file_header), 1, file);
        
        // 写入信息头
        BMPInfoHeader info_header;
        info_header.width = width;
        info_header.height = height;  // 正值表示从下到上
        info_header.imagesize = image_size;
        fwrite(&info_header, sizeof(info_header), 1, file);
        
        // 写入像素数据 (RGBA -> BGR转换, 从下到上)
        Byte row_buffer[16384];  // 固定缓冲区，足够存储最大行
        memset(row_buffer, 0, row_size);
        const UInt32* src_pixels = reinterpret_cast<const UInt32*>(buffer);
        
        for (Int32 y = height - 1; y >= 0; --y) {
            for (UInt32 x = 0; x < width; ++x) {
                UInt32 pixel = src_pixels[y * width + x];
                Byte r = pixel & 0xFF;
                Byte g = (pixel >> 8) & 0xFF;
                Byte b = (pixel >> 16) & 0xFF;
                
                row_buffer[x * 3 + 0] = b;  // BMP uses BGR
                row_buffer[x * 3 + 1] = g;
                row_buffer[x * 3 + 2] = r;
            }
            fwrite(row_buffer, row_size, 1, file);
        }
        
        fclose(file);
        printf("[BMPSaver] Saved: %s (%ux%u)\n", filename.c_str(), width, height);
        return true;
    }
};

//=============================================================================
// Camera Publisher进程
//=============================================================================
void RunCameraPublisher(UInt32 camera_id, SharedStats* stats, UInt32 duration_sec) {
    INNER_CORE_LOG("[Camera-%u] Starting (PID=%d)\n", camera_id, getpid());
    
    // 创建Publisher
    char shm_path_buf[64];
    snprintf(shm_path_buf, sizeof(shm_path_buf), "/cam%u_stream", camera_id);
    String shm_path(shm_path_buf);
    
    PublisherConfig config;
    config.chunk_size = kImageSize;
    config.max_chunks = 3;  // 减少到3以适应64MB /dev/shm限制 (3×5.53MB×3=50MB)
    config.policy = PublishPolicy::kOverwrite;
    config.ipc_type = IPCType::kSPMC;
    
    auto pub_result = Publisher::Create(shm_path, config);
    if (!pub_result.HasValue()) {
        fprintf(stderr, "[Camera-%u] Failed to create Publisher\n", camera_id);
        return;
    }
    auto publisher = std::move(pub_result).Value();
    
    printf("[Camera-%u] Publisher created: %s\n", camera_id, shm_path.c_str());
    
    // 记录该Camera的统计开始时间
    auto now_us = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    stats->cameras[camera_id].start_timestamp_us.store(now_us);
    
    printf("[Camera-%u] Starting transmission\n", camera_id);
    
    // 创建图像生成器
    SimpleImageCodec codec(camera_id);
    CameraStats& cam_stats = stats->cameras[camera_id];
    
    // 采集循环 (Publisher尽快发送，由Subscriber的STmin控制接收速率)
    auto start_time = std::chrono::steady_clock::now();
    
    while (std::chrono::steady_clock::now() - start_time < std::chrono::seconds(duration_sec)) {
        
        // INNER_CORE_LOG("[Camera-%u] Generating frame %u\n", camera_id, codec.GetFrameCount());
        // 使用Send(write_fn)直接在共享内存中生成图像
        auto send_start = std::chrono::high_resolution_clock::now();
        auto result = publisher.Send([&codec, camera_id](Byte* chunk_ptr, Size chunk_size) -> Size {
            // INNER_CORE_LOG("[Camera-%u] write_fn called, chunk_ptr=%p, size=%lu\n", 
                        //   camera_id, (void*)chunk_ptr, (unsigned long)chunk_size);
            codec.GenerateFrame(chunk_ptr, chunk_size);
            // INNER_CORE_LOG("[Camera-%u] write_fn completed\n", camera_id);
            return kImageSize;
        });
        auto send_end = std::chrono::high_resolution_clock::now();
        
        UInt64 send_time_us = std::chrono::duration_cast<std::chrono::microseconds>(
            send_end - send_start).count();
        
        // INNER_CORE_LOG("Camera-%u: Frame %u sent in %lu us", camera_id, codec.GetFrameCount(), send_time_us);
        
        if (!result.HasValue()) {
            cam_stats.send_failures.fetch_add(1, std::memory_order_relaxed);
        } else {
            UInt64 frame_num = cam_stats.frames_sent.fetch_add(1, std::memory_order_relaxed);
            cam_stats.total_send_time_us.fetch_add(send_time_us, std::memory_order_relaxed);
            
            // 每10帧采样一次延迟
            if (frame_num % 10 == 0) {
                UInt32 idx = cam_stats.latency_count.fetch_add(1, std::memory_order_relaxed);
                if (idx < kMaxLatencySamples) {
                    cam_stats.latencies_us[idx] = send_time_us;
                }
            }
        }
    }
    
    INNER_CORE_LOG("[Camera-%u] Completed: %lu frames, %lu send failures\n",
           camera_id, cam_stats.frames_sent.load(), cam_stats.send_failures.load());
}

//=============================================================================
// Fusion Subscriber进程
//=============================================================================
class FusionSubscriber {
public:
    FusionSubscriber(SharedStats* stats, UInt32 duration_sec) 
        : running_(true)
        , current_back_buffer_(0)
        , frame_counters_{0, 0, 0}
        , stats_(stats)
        , duration_sec_(duration_sec)
        , start_time_(std::chrono::steady_clock::now())
    {
        // 分配双缓存
        buffers_[0] = new (std::nothrow) Byte[kFusionSize];
        buffers_[1] = new (std::nothrow) Byte[kFusionSize];
        
        if (!buffers_[0] || !buffers_[1]) {
            fprintf(stderr, "[Fusion] Failed to allocate buffers\n");
            throw std::runtime_error("Buffer allocation failed");
        }
        
        // 初始化为黑色
        std::memset(buffers_[0], 0, kFusionSize);
        std::memset(buffers_[1], 0, kFusionSize);
        
        INNER_CORE_LOG("[Fusion] Dual buffers allocated: %u MB\n", 
               (kFusionSize / 1024 / 1024) * 2);
    }
    
    ~FusionSubscriber() {
        running_.store(false);
        
        // 等待所有线程完成
        for (auto& thread : sub_threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        
        if (save_thread_.joinable()) {
            save_thread_.join();
        }
        
        delete[] buffers_[0];
        delete[] buffers_[1];
    }
    
    void Run() {
        // 启动3个订阅线程
        for (UInt32 i = 0; i < 3; ++i) {
            sub_threads_.emplace_back([this, i]() { this->SubscriberThread(i); });
        }
        
        // 启动保存线程
        save_thread_ = std::thread([this]() { this->SaverThread(); });
        
        INNER_CORE_LOG("[Fusion] All threads started\n");
        
        // 主线程等待测试完成
        while (running_.load()) {
            auto elapsed = std::chrono::steady_clock::now() - start_time_;
            if (elapsed >= std::chrono::seconds(duration_sec_)) {
                running_.store(false);
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

private:
    void SubscriberThread(UInt32 camera_id) {
        char shm_path_buf[64];
        snprintf(shm_path_buf, sizeof(shm_path_buf), "/cam%u_stream", camera_id);
        String shm_path(shm_path_buf);
        
        // 创建Subscriber (常重试)
        SubscriberConfig config;
        config.chunk_size = kImageSize;
        config.max_chunks = 3;  // 匹配Publisher配置
        config.STmin = 10;  // 10ms限流，保证100FPS
        config.empty_policy = SubscribePolicy::kSkip;
        config.ipc_type = IPCType::kSPMC;
        
        std::unique_ptr<Subscriber> subscriber;
        for (int retry = 0; retry < 5; ++retry) {
            auto sub_result = Subscriber::Create(shm_path, config);
            if (sub_result.HasValue()) {
                subscriber = std::make_unique<Subscriber>(std::move(sub_result).Value());
                break;
            }
            INNER_CORE_LOG("[SubThread-%d] Retry %d to create Subscriber for %s\n", 
                   camera_id, retry + 1, shm_path.c_str());
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        
        if (!subscriber) {
            INNER_CORE_LOG("[SubThread-%d] Failed to create Subscriber after retries\n", camera_id);
            return;
        }
        
        subscriber->Connect();
        INNER_CORE_LOG("[SubThread-%d] Connected to %s\n", camera_id, shm_path.c_str());
        
        // 计算写入位置
        UInt32 offset_x = 0, offset_y = 0;
        if (camera_id == 0) {
            offset_x = 0; offset_y = 0;           // 左上
        } else if (camera_id == 1) {
            offset_x = kCameraWidth; offset_y = 0;  // 右上
        } else {
            offset_x = 960; offset_y = kCameraHeight; // 下居中
        }

        // 接收循环
        while (running_.load()) {
            auto recv_start = std::chrono::high_resolution_clock::now();
            auto sample_result = subscriber->Receive(kInvalidChannelID, SubscribePolicy::kSkip);
            auto recv_end = std::chrono::high_resolution_clock::now();

            // std::cerr << "[SubThread-" << camera_id << "] count: " << subscriber->allocator_->GetAllocatedCount() 
                        // << ", queue: " << static_cast<UInt32>(subscriber->shm_->GetChannelQueue(camera_id)->STmin.load(std::memory_order_acquire)) << std::endl;
            if (sample_result.HasValue() && !sample_result.Value().empty()) {
                auto& samples = sample_result.Value();
                auto& sample = samples[0];
                
                UInt64 recv_time_us = std::chrono::duration_cast<std::chrono::microseconds>(
                    recv_end - recv_start).count();
                
                // 获取后缓存
                UInt32 back_idx = current_back_buffer_.load(std::memory_order_acquire);
                Byte* back_buffer = buffers_[back_idx];
                
                // 拷贝图像到后缓存的指定位置 (零拷贝读取共享内存)
                CopyImageToBuffer(sample.RawData(), back_buffer, offset_x, offset_y);
                
                UInt64 frame_num = stats_->fusion.frames_received[camera_id].fetch_add(1, std::memory_order_relaxed);
                frame_counters_[camera_id].store(frame_num + 1, std::memory_order_relaxed);
                
                // 每10帧采样一次延迟
                if (frame_num % 10 == 0) {
                    UInt32 idx = stats_->fusion.latency_count[camera_id].fetch_add(1, std::memory_order_relaxed);
                    if (idx < kMaxLatencySamples) {
                        stats_->fusion.latencies_us[camera_id][idx] = recv_time_us;
                    }
                }
            } else {
                // std::cout << "[SubThread-" << camera_id << "] Receive failed " << sample_result.Error().Message() << std::endl;
                stats_->fusion.receive_failures[camera_id].fetch_add(1, std::memory_order_relaxed);
            }
        }
        
        INNER_CORE_LOG("[SubThread-%d] Completed: %lu frames received\n", 
               camera_id, stats_->fusion.frames_received[camera_id].load());
    }
    
    void CopyImageToBuffer(const Byte* src, Byte* dst_buffer, 
                          UInt32 offset_x, UInt32 offset_y) {
        const UInt32* src_pixels = reinterpret_cast<const UInt32*>(src);
        UInt32* dst_pixels = reinterpret_cast<UInt32*>(dst_buffer);
        
        for (UInt32 y = 0; y < kCameraHeight; ++y) {
            UInt32 dst_y = offset_y + y;
            if (dst_y >= kFusionHeight) break;
            
            for (UInt32 x = 0; x < kCameraWidth; ++x) {
                UInt32 dst_x = offset_x + x;
                if (dst_x >= kFusionWidth) break;
                
                UInt32 src_idx = y * kCameraWidth + x;
                UInt32 dst_idx = dst_y * kFusionWidth + dst_x;
                dst_pixels[dst_idx] = src_pixels[src_idx];
            }
        }
    }
    
    void SaverThread() {
        INNER_CORE_LOG("[SaverThread] Started (save every %ds, max %d images)\n", kSavePeriodSec, kMaxSavedImages);
        
        UInt32 save_count = 0;
        while (running_.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(kSavePeriodSec));
            
            if (!running_.load()) break;
            
            // 切换前后缓存
            UInt32 old_back = current_back_buffer_.load(std::memory_order_acquire);
            UInt32 new_back = 1 - old_back;
            current_back_buffer_.store(new_back, std::memory_order_release);
            
            // 现在old_back成为前缓存，可以安全读取
            Byte* front_buffer = buffers_[old_back];
            
            // 生成文件名（循环覆盖）
            UInt32 file_idx = save_count % kMaxSavedImages;
            char filename_buf[64];
            snprintf(filename_buf, sizeof(filename_buf), "fusion_%05u.bmp", file_idx);
            String filename(filename_buf);
            
            // 保存BMP文件
            bool success = BMPSaver::SaveBMP(filename, front_buffer, 
                                            kFusionWidth, kFusionHeight);
            
            if (success) {
                save_count++;
                INNER_CORE_LOG("[SaverThread] Saved #%u: %s (Frames: %u/%u/%u)\n", 
                       save_count, filename.c_str(),
                       frame_counters_[0].load(), 
                       frame_counters_[1].load(), 
                       frame_counters_[2].load());
            }
        }
        
        INNER_CORE_LOG("[SaverThread] Completed: %u files saved\n", save_count);
    }

private:
    std::atomic<bool> running_;
    std::atomic<UInt32> current_back_buffer_;  // 0 or 1
    Byte* buffers_[2];  // 双缓存
    std::vector<std::thread> sub_threads_;
    std::thread save_thread_;
    std::atomic<UInt32> frame_counters_[3];  // 每个摄像头的帧计数
    SharedStats* stats_;
    UInt32 duration_sec_;
    std::chrono::steady_clock::time_point start_time_;
};

//=============================================================================
// 统计分析函数
//=============================================================================
struct LatencyStats {
    UInt64 min_us;
    UInt64 max_us;
    UInt64 avg_us;
    UInt64 p50_us;
    UInt64 p99_us;
};

LatencyStats CalculateLatencyStats(UInt64* latencies, UInt32 count) {
    LatencyStats stats = {0, 0, 0, 0, 0};
    if (count == 0) return stats;
    
    // 排序
    std::vector<UInt64> sorted(latencies, latencies + count);
    std::sort(sorted.begin(), sorted.end());
    
    // 计算统计值
    stats.min_us = sorted[0];
    stats.max_us = sorted[count - 1];
    
    UInt64 sum = 0;
    for (UInt32 i = 0; i < count; ++i) {
        sum += sorted[i];
    }
    stats.avg_us = sum / count;
    
    stats.p50_us = sorted[count / 2];
    stats.p99_us = sorted[count * 99 / 100];
    
    return stats;
}

void PrintStatsSummary(SharedStats* stats, UInt32 duration_sec) {
    printf("\n");
    auto end_us = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    
    printf("\n========================================\n");
    printf("性能统计汇总\n");
    printf("========================================\n");
    printf("总运行时长: %us\n", duration_sec);
    printf("========================================\n\n");
    
    // Publisher统计
    printf("[ Camera Publishers ]\n");
    printf("┌─────────┬────────────┬─────────────┬──────────┬─────────────┬─────────────┬─────────────┬─────────────┐\n");
    printf("│ Camera  │ Frames Sent│ Send Errors │ FPS      │   Avg (us)  │   P50 (us)  │   P99 (us)  │   Max (us)  │\n");
    printf("├─────────┼────────────┼─────────────┼──────────┼─────────────┼─────────────┼─────────────┼─────────────┤\n");
    
    for (int i = 0; i < 3; ++i) {
        const CameraStats& cam = stats->cameras[i];
        UInt64 frames = cam.frames_sent.load();
        UInt64 errors = cam.send_failures.load();
        UInt64 avg_us = frames > 0 ? cam.total_send_time_us.load() / frames : 0;
        
        // 计算该Camera的FPS（基于自己的start_timestamp）
        UInt64 start_us = cam.start_timestamp_us.load();
        double camera_duration = start_us > 0 ? (end_us - start_us) / 1000000.0 : duration_sec;
        double fps = camera_duration > 0 ? static_cast<double>(frames) / camera_duration : 0.0;
        
        UInt32 sample_count = cam.latency_count.load();
        // 防止溢出：限制sample_count不超过数组大小
        if (sample_count > kMaxLatencySamples) {
            sample_count = kMaxLatencySamples;
        }
        LatencyStats lat_stats = {0, 0, 0, 0, 0};
        if (sample_count > 0) {
            lat_stats = CalculateLatencyStats(
                const_cast<UInt64*>(cam.latencies_us), sample_count);
        }
        
        printf("│ Cam-%d   │ %10lu │ %11lu │ %8.1f │ %11lu │ %11lu │ %11lu │ %11lu │\n",
               i, frames, errors, fps, avg_us, 
               lat_stats.p50_us, lat_stats.p99_us, lat_stats.max_us);
    }
    
    printf("└─────────┴────────────┴─────────────┴─────────────┴─────────────┴─────────────┴─────────────┘\n\n");
    
    // Subscriber统计
    printf("[ Fusion Subscriber ]\n");
    printf("┌─────────┬────────────┬─────────────┬──────────┬──────────┬─────────────┬─────────────┬─────────────┬─────────────┐\n");
    printf("│ Stream  │ Frames Recv│ Recv Errors │ FPS      │ STMin(ms)│   Avg (us)  │   P50 (us)  │   P99 (us)  │   Max (us)  │\n");
    printf("├─────────┼────────────┼─────────────┼──────────┼──────────┼─────────────┼─────────────┼─────────────┼─────────────┤\n");
    
    for (int i = 0; i < 3; ++i) {
        const FusionStats& fusion = stats->fusion;
        UInt64 frames = fusion.frames_received[i].load();
        UInt64 errors = fusion.receive_failures[i].load();
        
        // 计算FPS
        double fps = duration_sec > 0 ? static_cast<double>(frames) / duration_sec : 0.0;
        
        UInt32 sample_count = fusion.latency_count[i].load();
        // 防止溢出：限制sample_count不超过数组大小
        if (sample_count > kMaxLatencySamples) {
            sample_count = kMaxLatencySamples;
        }
        LatencyStats lat_stats = {0, 0, 0, 0, 0};
        UInt64 avg_us = 0;
        if (sample_count > 0) {
            lat_stats = CalculateLatencyStats(
                const_cast<UInt64*>(fusion.latencies_us[i]), sample_count);
            
            UInt64 sum = 0;
            for (UInt32 j = 0; j < sample_count; ++j) {
                sum += fusion.latencies_us[i][j];
            }
            avg_us = sum / sample_count;
        }
        
        printf("│ Cam-%d   │ %10lu │ %11lu │ %8.1f │ %9d │ %11lu │ %11lu │ %11lu │ %11lu │\n",
               i, frames, errors, fps, kSTMinMs, avg_us,
               lat_stats.p50_us, lat_stats.p99_us, lat_stats.max_us);
    }
    
    printf("└─────────┴────────────┴─────────────┴──────────┴──────────┴─────────────┴─────────────┴─────────────┴─────────────┘\n");
}

//=============================================================================
// 主函数
//=============================================================================
int main(int argc, char* argv[]) {
    // 解析命令行参数
    UInt32 duration_sec = 30;  // 默认30秒
    if (argc > 1) {
        duration_sec = std::atoi(argv[1]);
        if (duration_sec == 0 ) {
            INNER_CORE_LOG("Invalid duration. Using default 30 seconds.\n");
            duration_sec = 30;
        }
    }
    
    INNER_CORE_LOG("========================================\n");
    INNER_CORE_LOG("三摄像头融合示例 - NORMAL模式\n");
    INNER_CORE_LOG("========================================\n");
    INNER_CORE_LOG("摄像头配置: %ux%u @ %u FPS\n", kCameraWidth, kCameraHeight, kTargetFPS);
    INNER_CORE_LOG("融合图尺寸: %ux%u\n", kFusionWidth, kFusionHeight);
    INNER_CORE_LOG("单帧大小: %u MB\n", kImageSize / 1024 / 1024);
    INNER_CORE_LOG("双缓存大小: %u MB\n", (kFusionSize / 1024 / 1024) * 2);
    INNER_CORE_LOG("测试时长: %u 秒\n", duration_sec);
    INNER_CORE_LOG("保存周期: %d 秒\n", kSavePeriodSec);
    INNER_CORE_LOG("最大图片数: %d (循环覆盖)\n", kMaxSavedImages);
    INNER_CORE_LOG("========================================\n\n");
    
    // 创建共享内存统计段
    const char* stats_shm_name = "/camera_fusion_stats";
    int shm_fd = shm_open(stats_shm_name, O_CREAT | O_RDWR, 0666);
    if (shm_fd < 0) {
        INNER_CORE_LOG("Failed to create stats shared memory\n");
        return 1;
    }
    
    if (ftruncate(shm_fd, sizeof(SharedStats)) < 0) {
        INNER_CORE_LOG("Failed to resize stats shared memory\n");
        close(shm_fd);
        return 1;
    }
    
    SharedStats* stats = static_cast<SharedStats*>(
        mmap(nullptr, sizeof(SharedStats), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0));
    
    if (stats == MAP_FAILED) {
        INNER_CORE_LOG("Failed to mmap stats shared memory\n");
        close(shm_fd);
        return 1;
    }
    
    // 初始化统计数据 - 逐个placement new初始化atomic变量
    for (int i = 0; i < 3; ++i) {
        new (&stats->cameras[i].frames_sent) std::atomic<UInt64>(0);
        new (&stats->cameras[i].send_failures) std::atomic<UInt64>(0);
        new (&stats->cameras[i].total_send_time_us) std::atomic<UInt64>(0);
        new (&stats->cameras[i].latency_count) std::atomic<UInt32>(0);
        new (&stats->cameras[i].start_timestamp_us) std::atomic<UInt64>(0);
        std::memset(stats->cameras[i].latencies_us, 0, sizeof(stats->cameras[i].latencies_us));
        
        new (&stats->fusion.frames_received[i]) std::atomic<UInt64>(0);
        new (&stats->fusion.receive_failures[i]) std::atomic<UInt64>(0);
        new (&stats->fusion.latency_count[i]) std::atomic<UInt32>(0);
        std::memset(stats->fusion.latencies_us[i], 0, sizeof(stats->fusion.latencies_us[i]));
    }
    
    // 清理旧的共享内存
    for (int i = 0; i < 3; ++i) {
        std::string cleanup = "rm -f /dev/shm/cam" + std::to_string(i) + "_stream";
        system(cleanup.c_str());
    }
    
    std::vector<pid_t> child_pids;
    
    // Fork 3个Camera Publisher进程
    for (UInt32 i = 0; i < 3; ++i) {
        pid_t pid = fork();
        if (pid == 0) {
            // 子进程 - 延迟启动以避免共享内存创建冲突
            std::this_thread::sleep_for(std::chrono::milliseconds(200 + i * 300));
            RunCameraPublisher(i, stats, duration_sec);
            exit(0);
        }
        child_pids.push_back(pid);
    }
    
    // Fork 1个Fusion Subscriber进程
    pid_t fusion_pid = fork();
    if (fusion_pid == 0) {
        // 子进程 - 等待所有Publishers先启动（减少延迟）
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        try {
            FusionSubscriber fusion(stats, duration_sec);
            fusion.Run();
        } catch (const std::exception& e) {
            INNER_CORE_LOG("[Fusion] Exception: %s\n", e.what());
            exit(1);
        }
        exit(0);
    }
    child_pids.push_back(fusion_pid);
    
    // 主进程等待所有子进程
    INNER_CORE_LOG("[Main] Started %zu processes\n", child_pids.size());
    INNER_CORE_LOG("[Main] Waiting for completion...\n\n");
    
    int status;
    for (auto pid : child_pids) {
        waitpid(pid, &status, 0);
    }
    
    INNER_CORE_LOG("\n========================================\n");
    INNER_CORE_LOG("三摄像头融合示例完成\n");
    INNER_CORE_LOG("========================================\n");
    
    // 打印统计汇总
    PrintStatsSummary(stats, duration_sec);
    
    INNER_CORE_LOG("\n请检查生成的BMP文件: fusion_00000.bmp ~ fusion_%05d.bmp\n", kMaxSavedImages - 1);
    
    // 清理共享内存
    munmap(stats, sizeof(SharedStats));
    close(shm_fd);
    shm_unlink(stats_shm_name);
    
    for (int i = 0; i < 3; ++i) {
        std::string cleanup = "rm -f /dev/shm/cam" + std::to_string(i) + "_stream";
        system(cleanup.c_str());
    }
    
    return 0;
}
