/**
 * @file        camera_fusion_mpsc_example.cpp
 * @brief       Aii: 三摄像头融合示例 - MPSC模式（多发布者单订阅者）
 * @details     演示MPSC模式下多个摄像头Publisher共享单一通道，Subscriber接收所有数据
 * @author      LightAP Core Team
 * @date        2026-01-30
 *
 * ============================================================================
 * 场景说明
 * ============================================================================
 * 
 * MPSC模式摄像头融合场景：
 * - 3个独立的Camera Publisher进程
 * - 所有Publisher写入同一个共享内存通道 /camera_fusion_stream
 * - 1个Fusion Subscriber接收所有Camera数据
 * - Publisher通过channel_id区分数据来源（camera 0/1/2）
 * 
 * ============================================================================
 * 系统架构 (MPSC)
 * ============================================================================
 * 
 *   [Camera-0 Publisher]    [Camera-1 Publisher]    [Camera-2 Publisher]
 *   channel_id=0            channel_id=1            channel_id=2
 *   1920x720 @ 60FPS        1920x720 @ 60FPS        1920x720 @ 60FPS
 *         |                       |                       |
 *         +-------+---------------+---------------+-------+
 *                 |                               |
 *                 v                               v
 *          /camera_fusion_stream (MPSC共享通道)
 *          3个write channels + 1个read channel
 *                 |
 *                 v
 *         [Fusion Subscriber]
 *         - Scanner线程自动发现活跃Publishers
 *         - 从所有channel读取数据
 *         - 根据channel_id拼接到对应位置
 *         - 双缓存 + BMP保存
 * 
 * ============================================================================
 * MPSC关键特性
 * ============================================================================
 * 
 * 1. 共享内存布局:
 *    - ControlBlock: MPSC类型标记
 *    - write_mask: 跟踪活跃Publishers (3 bits对应3个camera)
 *    - write_seq: Publisher注册/注销时更新，唤醒Subscriber Scanner
 *    - ChannelQueue[30]: 每个Publisher独立队列
 * 
 * 2. Publisher行为:
 *    - 创建时调用RegisterWriteChannel分配唯一channel_id
 *    - 直接Send到自己的channel，无需扫描订阅者
 *    - 支持动态加入/退出
 * 
 * 3. Subscriber行为:
 *    - Scanner线程监听write_seq变化
 *    - 发现新Publisher时自动添加到read_channels_
 *    - Receive()轮询所有活跃Publisher的channel
 *    - 通过Sample.ChannelID()识别数据来源
 * 
 * 4. 优势:
 *    - 单一共享内存段，资源利用率高
 *    - Publisher零配置，自动发现
 *    - 适合sensor fusion场景
 * 
 * ============================================================================
 * 技术规格
 * ============================================================================
 * 
 * - 图像尺寸: 1920x720x4 = 5.3MB per frame
 * - ChunkPool: 16 chunks (支持3个Publisher高速发送)
 * - 队列容量: 256 slots per channel
 * - STMin: 16ms (60 FPS)
 * - 融合布局: 3840x1440 (同SPMC示例)
 * - 双缓存: 42MB
 * 
 * ============================================================================
 * 运行方式
 * ============================================================================
 * 
 * cd /workspace/LightAP/build/modules/Core
 * ./camera_fusion_mpsc_example [duration_sec]
 * 
 * 预期输出:
 * - fusion_mpsc_00000.bmp ~ fusion_mpsc_00009.bmp
 * - 性能统计（每个camera独立FPS）
 */

#include "IPCFactory.hpp"
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

// ============================================================================
// 配置常量
// ============================================================================
constexpr UInt32 kCameraWidth = 1920;
constexpr UInt32 kCameraHeight = 720;
constexpr UInt32 kBytesPerPixel = 4;
constexpr UInt32 kImageSize = kCameraWidth * kCameraHeight * kBytesPerPixel;

constexpr UInt32 kFusionWidth = 3840;
constexpr UInt32 kFusionHeight = 1440;
constexpr UInt32 kFusionSize = kFusionWidth * kFusionHeight * kBytesPerPixel;

constexpr UInt32 kTargetFPS = 60;
constexpr UInt32 kSTMinUs = 10000;  // 10ms (100 FPS)
constexpr UInt32 kSavePeriodSec = 5;
constexpr UInt32 kMaxSavedImages = 10;
constexpr UInt32 kMaxLatencySamples = 10000;

const char* kSharedMemoryPath = "/camera_fusion_stream";  // MPSC共享通道

// ============================================================================
// 性能统计数据结构（共享内存）
// ============================================================================
struct CameraStats
{
    std::atomic<UInt64> frames_sent{0};
    std::atomic<UInt64> send_failures{0};
    std::atomic<UInt64> total_send_time_us{0};
    std::atomic<UInt32> latency_count{0};
    UInt64 latencies_us[kMaxLatencySamples];
    std::atomic<UInt64> start_timestamp_us{0};  // 该Camera的统计开始时间
};

struct FusionStats
{
    std::atomic<UInt64> frames_received[3]{{0}, {0}, {0}};
    std::atomic<UInt64> receive_failures[3]{{0}, {0}, {0}};
    std::atomic<UInt32> latency_count[3]{{0}, {0}, {0}};
    UInt64 latencies_us[3][kMaxLatencySamples];
};

struct SharedStats
{
    CameraStats cameras[3];
    FusionStats fusion;
};

// ============================================================================
// 图像生成器（彩色渐变 + 帧序号）
// ============================================================================
class SimpleImageCodec
{
public:
    explicit SimpleImageCodec(UInt32 camera_id) 
        : camera_id_(camera_id), frame_count_(0) {}
    
    void GenerateFrame(Byte* buffer, Size buffer_size)
    {
        if (buffer_size < kImageSize) {
            return;
        }
        
        UInt32* pixels = reinterpret_cast<UInt32*>(buffer);
        
        // 为每个camera使用不同的基础色调
        UInt8 base_r = (camera_id_ == 0) ? 200 : (camera_id_ == 1) ? 100 : 50;
        UInt8 base_g = (camera_id_ == 0) ? 100 : (camera_id_ == 1) ? 200 : 50;
        UInt8 base_b = (camera_id_ == 0) ? 50  : (camera_id_ == 1) ? 100 : 200;
        
        for (UInt32 y = 0; y < kCameraHeight; ++y) {
            for (UInt32 x = 0; x < kCameraWidth; ++x) {
                UInt8 r = static_cast<UInt8>((base_r + x * 55 / kCameraWidth) % 256);
                UInt8 g = static_cast<UInt8>((base_g + y * 55 / kCameraHeight) % 256);
                UInt8 b = base_b;
                UInt8 a = 255;
                
                pixels[y * kCameraWidth + x] = (a << 24) | (b << 16) | (g << 8) | r;
            }
        }

        // 添加摄像头ID标识 (居中显示，黑色)
        DrawCameraID(pixels);

        // 添加运动色块 (模拟移动物体)
        UInt32 block_size = 80;
        UInt32 block_x = (frame_count_ * 10) % (kCameraWidth - block_size);
        UInt32 block_y = (frame_count_ * 3) % (kCameraHeight - block_size);

        for (UInt32 dy = 0; dy < block_size; ++dy) {
            for (UInt32 dx = 0; dx < block_size; ++dx) {
                UInt32 idx = (block_y + dy) * kCameraWidth + (block_x + dx);
                pixels[idx] = 0xFFFFFFFF;  // 白色方块 (RGBA)
            }
        }

        ++frame_count_;
    }
    
    UInt32 GetFrameCount() const { return frame_count_; }

private:
    void DrawCameraID(UInt32* pixels)
    {
        UInt32 center_x = kCameraWidth / 2;
        UInt32 center_y = kCameraHeight / 2;
        DrawBigDigit(pixels, camera_id_, center_x - 50, center_y - 50, 0xFF000000);  // 黑色
    }

    void DrawBigDigit(UInt32* pixels, UInt32 digit, UInt32 x, UInt32 y, UInt32 color)
    {
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

        const UInt32 seg_w = 20;
        const UInt32 seg_h = 80;
        const UInt32 gap = 5;

        auto draw_rect = [&](UInt32 rx, UInt32 ry, UInt32 w, UInt32 h) {
            for (UInt32 yy = ry; yy < ry + h && yy < kCameraHeight; ++yy) {
                for (UInt32 xx = rx; xx < rx + w && xx < kCameraWidth; ++xx) {
                    pixels[yy * kCameraWidth + xx] = color;
                }
            }
        };

        if (segments[digit][0]) draw_rect(x + seg_w, y, seg_h, seg_w);                    // 上
        if (segments[digit][1]) draw_rect(x + seg_w + seg_h + gap, y + seg_w, seg_w, seg_h); // 右上
        if (segments[digit][2]) draw_rect(x + seg_w + seg_h + gap, y + seg_w + seg_h + gap, seg_w, seg_h); // 右下
        if (segments[digit][3]) draw_rect(x + seg_w, y + 2 * seg_w + 2 * seg_h + 2 * gap, seg_h, seg_w); // 下
        if (segments[digit][4]) draw_rect(x, y + seg_w + seg_h + gap, seg_w, seg_h);      // 左下
        if (segments[digit][5]) draw_rect(x, y + seg_w, seg_w, seg_h);                    // 左上
        if (segments[digit][6]) draw_rect(x + seg_w, y + seg_w + seg_h + gap, seg_h, seg_w); // 中
    }

    UInt32 camera_id_;
    UInt32 frame_count_;
};

// ============================================================================
// BMP文件保存
// ============================================================================
#pragma pack(push, 1)
struct BMPHeader
{
    UInt16 type{0x4D42};
    UInt32 size{0};
    UInt16 reserved1{0};
    UInt16 reserved2{0};
    UInt32 offset{54};
};

struct BMPInfoHeader
{
    UInt32 size{40};
    Int32 width{0};
    Int32 height{0};
    UInt16 planes{1};
    UInt16 bits{24};
    UInt32 compression{0};
    UInt32 imagesize{0};
    Int32 xresolution{0};
    Int32 yresolution{0};
    UInt32 ncolours{0};
    UInt32 importantcolours{0};
};
#pragma pack(pop)

bool SaveBMP(const char* filename, const Byte* rgba_buffer, 
             UInt32 width, UInt32 height)
{
    FILE* fp = fopen(filename, "wb");
    if (!fp) {
        return false;
    }
    
    UInt32 row_size = ((width * 3 + 3) / 4) * 4;
    UInt32 image_size = row_size * height;
    
    BMPHeader header;
    header.size = 54 + image_size;
    
    BMPInfoHeader info;
    info.width = static_cast<Int32>(width);
    info.height = static_cast<Int32>(height);
    info.imagesize = image_size;
    
    fwrite(&header, sizeof(header), 1, fp);
    fwrite(&info, sizeof(info), 1, fp);
    
    std::vector<Byte> row_buffer(row_size);
    const UInt32* src = reinterpret_cast<const UInt32*>(rgba_buffer);
    
    for (Int32 y = static_cast<Int32>(height) - 1; y >= 0; --y) {
        for (UInt32 x = 0; x < width; ++x) {
            UInt32 pixel = src[y * width + x];
            row_buffer[x * 3 + 0] = static_cast<Byte>(pixel & 0xFF);
            row_buffer[x * 3 + 1] = static_cast<Byte>((pixel >> 8) & 0xFF);
            row_buffer[x * 3 + 2] = static_cast<Byte>((pixel >> 16) & 0xFF);
        }
        fwrite(row_buffer.data(), row_size, 1, fp);
    }
    
    fclose(fp);
    return true;
}

// ============================================================================
// Camera Publisher进程（MPSC模式）
// ============================================================================
void RunCameraPublisher(UInt32 camera_id, SharedStats* stats, UInt32 duration_sec)
{
    printf("[Camera-%u] Starting MPSC Publisher (PID=%d)\n", camera_id, getpid());
    
    // MPSC配置：所有Publisher使用同一个共享内存路径
    PublisherConfig config;
    config.chunk_size = kImageSize;
    config.max_chunks = 9;  // MPSC模式共享ChunkPool
    config.policy = PublishPolicy::kOverwrite;
    config.ipc_type = IPCType::kMPSC;
    config.channel_id = 0xFF;  // 自动分配
    
    auto pub_result = IPCFactory::CreatePublisher(kSharedMemoryPath, config);
    if (!pub_result.HasValue()) {
        fprintf(stderr, "[Camera-%u] Failed to create Publisher, error code: %d\n", 
                camera_id, pub_result.Error().Value());
        return;
    }
    auto publisher = std::move(pub_result).Value();
    
    printf("[Camera-%u] MPSC Publisher created, path=%s\n", 
           camera_id, kSharedMemoryPath);
    
    auto now_us = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    stats->cameras[camera_id].start_timestamp_us.store(now_us);
    
    SimpleImageCodec codec(camera_id);
    CameraStats& cam_stats = stats->cameras[camera_id];
    
    auto start_time = std::chrono::steady_clock::now();
    
    while (std::chrono::steady_clock::now() - start_time < std::chrono::seconds(duration_sec)) {
        auto send_start = std::chrono::high_resolution_clock::now();
        
        // Send到自己的channel（自动分配的channel_id）
        auto result = publisher->Send([&codec](UInt8 /*channel_id*/, Byte* chunk_ptr, Size chunk_size) -> Size {
            codec.GenerateFrame(chunk_ptr, chunk_size);
            return kImageSize;
        });
        
        auto send_end = std::chrono::high_resolution_clock::now();
        UInt64 send_time_us = std::chrono::duration_cast<std::chrono::microseconds>(
            send_end - send_start).count();
        
        if (!result.HasValue()) {
            cam_stats.send_failures.fetch_add(1, std::memory_order_relaxed);
        } else {
            UInt64 frame_num = cam_stats.frames_sent.fetch_add(1, std::memory_order_relaxed);
            cam_stats.total_send_time_us.fetch_add(send_time_us, std::memory_order_relaxed);
            
            if (frame_num % 100 == 0) {
                UInt32 idx = cam_stats.latency_count.fetch_add(1, std::memory_order_relaxed);
                if (idx < kMaxLatencySamples) {
                    cam_stats.latencies_us[idx] = send_time_us;
                }
            }
        }
    }
    
    printf("[Camera-%u] Completed: %lu frames, %lu failures\n",
           camera_id, cam_stats.frames_sent.load(), cam_stats.send_failures.load());
}

// ============================================================================
// Fusion Subscriber进程（MPSC模式）
// ============================================================================
class FusionSubscriber
{
public:
    FusionSubscriber(SharedStats* stats, UInt32 duration_sec)
        : stats_(stats)
        , duration_sec_(duration_sec)
        , running_(true)
        , current_back_buffer_(0)
        , save_counter_(0)
    {
        buffers_[0] = new Byte[kFusionSize];
        buffers_[1] = new Byte[kFusionSize];
        std::memset(buffers_[0], 0, kFusionSize);
        std::memset(buffers_[1], 0, kFusionSize);
        
        for (int i = 0; i < 3; ++i) {
            frame_counters_[i].store(0);
        }
        
        printf("[Fusion] Dual buffers allocated: %u MB\n", (kFusionSize * 2) / 1024 / 1024);
    }
    
    ~FusionSubscriber()
    {
        running_.store(false);
        
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
    
    void Run()
    {
        // MPSC模式：单个Subscriber自动接收所有Publishers
        SubscriberConfig config;
        config.chunk_size = kImageSize;
        config.max_chunks = 9;
        config.STmin = kSTMinUs;
        config.empty_policy = SubscribePolicy::kSkip;
        config.ipc_type = IPCType::kMPSC;
        
        // 等待Publishers启动
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        
        auto sub_result = IPCFactory::CreateSubscriber(kSharedMemoryPath, config);
        if (!sub_result.HasValue()) {
            fprintf(stderr, "[Fusion] Failed to create Subscriber, error code: %d\n",
                    sub_result.Error().Value());
            return;
        }
        auto subscriber = std::move(sub_result).Value();
        
        subscriber->Connect();
        printf("[Fusion] MPSC Subscriber connected to %s\n", kSharedMemoryPath);
        
        // 启动保存线程
        start_time_ = std::chrono::steady_clock::now();
        save_thread_ = std::thread([this]() { this->SaverThread(); });
        
        printf("[Fusion] Started receiving from all cameras...\n");
        
        // 主接收循环
        while (running_.load()) {
            auto elapsed = std::chrono::steady_clock::now() - start_time_;
            if (elapsed >= std::chrono::seconds(duration_sec_)) {
                running_.store(false);
                break;
            }
            
            // MPSC模式：使用read_fn接口接收数据
            auto recv_start = std::chrono::high_resolution_clock::now();
            
            // 使用lambda作为read_fn直接处理数据
            // read_fn签名: Size(UInt8 channel_id, Byte* data, Size size)
            auto result = subscriber->Receive(
                [this, recv_start](UInt8 channel_id, Byte* data, Size size) -> Size {
                    UNUSED(data);
                    auto recv_end = std::chrono::high_resolution_clock::now();
                    
                    if (channel_id >= 3 || size != kImageSize) {
                        return 0;  // 无效数据
                    }
                    
                    UInt64 recv_time_us = std::chrono::duration_cast<std::chrono::microseconds>(
                        recv_end - recv_start).count();
                    
                    // 计算拼接位置
                    UInt32 offset_x = 0, offset_y = 0;
                    if (channel_id == 0) {
                        offset_x = 0; offset_y = 0;
                    } else if (channel_id == 1) {
                        offset_x = kCameraWidth; offset_y = 0;
                    } else {
                        offset_x = 960; offset_y = kCameraHeight;
                    }
                    
                    // 拼接到后缓存（零拷贝，直接读取共享内存）
                    UInt32 back_idx = current_back_buffer_.load(std::memory_order_acquire);
                    Byte* back_buffer = buffers_[back_idx];
                    CopyImageToBuffer(data, back_buffer, offset_x, offset_y);
                    
                    UInt64 frame_num = stats_->fusion.frames_received[channel_id].fetch_add(1, std::memory_order_relaxed);
                    frame_counters_[channel_id].store(frame_num + 1, std::memory_order_relaxed);
                    
                    if (frame_num % 100 == 0) {
                        UInt32 idx = stats_->fusion.latency_count[channel_id].fetch_add(1, std::memory_order_relaxed);
                        if (idx < kMaxLatencySamples) {
                            stats_->fusion.latencies_us[channel_id][idx] = recv_time_us;
                        }
                    }
                    
                    return size;  // 返回实际处理的字节数
                }
            );
            
            if (!result.HasValue() || result.Value() == 0) {
                // 没有数据，短暂休眠
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        
        printf("[Fusion] Receive loop completed\n");
    }

private:
    void CopyImageToBuffer(const Byte* src, Byte* dst_buffer, 
                          UInt32 offset_x, UInt32 offset_y)
    {
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
    
    void SaverThread()
    {
        printf("[SaverThread] Started (save every %us, max %u images)\n",
               kSavePeriodSec, kMaxSavedImages);
        
        while (running_.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(kSavePeriodSec));
            
            if (!running_.load()) break;
            
            // 交换前后缓存
            UInt32 old_back = current_back_buffer_.load();
            UInt32 new_back = (old_back + 1) % 2;
            current_back_buffer_.store(new_back);
            
            // 保存前缓存
            UInt32 front_idx = old_back;
            char filename[64];
            snprintf(filename, sizeof(filename), "fusion_mpsc_%05u.bmp", save_counter_);
            
            if (SaveBMP(filename, buffers_[front_idx], kFusionWidth, kFusionHeight)) {
                printf("[SaverThread] Saved %s\n", filename);
            } else {
                fprintf(stderr, "[SaverThread] Failed to save %s\n", filename);
            }
            
            save_counter_ = (save_counter_ + 1) % kMaxSavedImages;
        }
        
        printf("[SaverThread] Stopped\n");
    }
    
    SharedStats* stats_;
    UInt32 duration_sec_;
    std::atomic<bool> running_;
    std::chrono::steady_clock::time_point start_time_;
    
    Byte* buffers_[2];
    std::atomic<UInt32> current_back_buffer_;
    std::atomic<UInt64> frame_counters_[3];
    
    std::vector<std::thread> sub_threads_;
    std::thread save_thread_;
    UInt32 save_counter_;
};

// ============================================================================
// 性能统计
// ============================================================================
struct LatencyStats
{
    UInt64 avg_us;
    UInt64 p50_us;
    UInt64 p99_us;
    UInt64 max_us;
};

LatencyStats CalculateLatencyStats(UInt64* latencies, UInt32 count)
{
    LatencyStats result{0, 0, 0, 0};
    
    if (count == 0) {
        return result;
    }
    
    std::vector<UInt64> sorted(latencies, latencies + count);
    std::sort(sorted.begin(), sorted.end());
    
    UInt64 sum = 0;
    for (UInt32 i = 0; i < count; ++i) {
        sum += sorted[i];
    }
    result.avg_us = sum / count;
    result.p50_us = sorted[count / 2];
    result.p99_us = sorted[(count * 99) / 100];
    result.max_us = sorted[count - 1];
    
    return result;
}

void PrintStatsSummary(SharedStats* stats, UInt32 duration_sec)
{
    printf("\n========================================\n");
    printf("性能统计汇总 (MPSC模式)\n");
    printf("========================================\n");
    printf("总运行时长: %us\n", duration_sec);
    printf("========================================\n\n");
    
    printf("[ Camera Publishers ]\n");
    printf("┌─────────┬────────────┬─────────────┬──────────┬─────────────┬─────────────┬─────────────┬─────────────┐\n");
    printf("│ Camera  │ Frames Sent│ Send Errors │ FPS      │   Avg (us)  │   P50 (us)  │   P99 (us)  │   Max (us)  │\n");
    printf("├─────────┼────────────┼─────────────┼──────────┼─────────────┼─────────────┼─────────────┼─────────────┤\n");
    
    for (int i = 0; i < 3; ++i) {
        UInt64 frames = stats->cameras[i].frames_sent.load();
        UInt64 errors = stats->cameras[i].send_failures.load();
        double fps = static_cast<double>(frames) / duration_sec;
        
        UInt32 lat_count = stats->cameras[i].latency_count.load();
        auto lat_stats = CalculateLatencyStats(stats->cameras[i].latencies_us, lat_count);
        UInt64 avg_us = (frames > 0) ? stats->cameras[i].total_send_time_us.load() / frames : 0;
        
        printf("│ Cam-%d   │ %10lu │ %11lu │ %8.1f │ %11lu │ %11lu │ %11lu │ %11lu │\n",
               i, frames, errors, fps, avg_us,
               lat_stats.p50_us, lat_stats.p99_us, lat_stats.max_us);
    }
    
    printf("└─────────┴────────────┴─────────────┴──────────┴─────────────┴─────────────┴─────────────┴─────────────┘\n\n");
    
    printf("[ Fusion Subscriber ]\n");
    printf("┌─────────┬────────────┬──────────┬──────────┬─────────────┬─────────────┬─────────────┬─────────────┐\n");
    printf("│ Stream  │ Frames Recv│ FPS      │ STMin(us)│   Avg (us)  │   P50 (us)  │   P99 (us)  │   Max (us)  │\n");
    printf("├─────────┼────────────┼──────────┼──────────┼─────────────┼─────────────┼─────────────┼─────────────┤\n");
    
    for (int i = 0; i < 3; ++i) {
        UInt64 frames = stats->fusion.frames_received[i].load();
        double fps = static_cast<double>(frames) / duration_sec;
        
        UInt32 lat_count = stats->fusion.latency_count[i].load();
        auto lat_stats = CalculateLatencyStats(stats->fusion.latencies_us[i], lat_count);
        
        printf("│ Cam-%d   │ %10lu │ %8.1f │ %9u │ %11lu │ %11lu │ %11lu │ %11lu │\n",
               i, frames, fps, kSTMinUs,
               lat_stats.avg_us, lat_stats.p50_us, lat_stats.p99_us, lat_stats.max_us);
    }
    
    printf("└─────────┴────────────┴──────────┴──────────┴─────────────┴─────────────┴─────────────┴─────────────┘\n");
}

// ============================================================================
// 主函数
// ============================================================================
int main(int argc, char* argv[])
{
    UInt32 duration_sec = 30;
    if (argc > 1) {
        duration_sec = std::atoi(argv[1]);
        if (duration_sec == 0) {
            printf("Invalid duration. Using default 30 seconds.\n");
            duration_sec = 30;
        }
    }
    
    printf("========================================\n");
    printf("Aii: 三摄像头融合示例 - MPSC模式\n");
    printf("========================================\n");
    printf("摄像头配置: %ux%u @ %u FPS\n", kCameraWidth, kCameraHeight, kTargetFPS);
    printf("融合图尺寸: %ux%u\n", kFusionWidth, kFusionHeight);
    printf("单帧大小: %u MB\n", kImageSize / 1024 / 1024);
    printf("MPSC共享通道: %s\n", kSharedMemoryPath);
    printf("测试时长: %u 秒\n", duration_sec);
    printf("========================================\n\n");
    
    // 创建共享内存统计段
    const char* stats_shm_name = "/camera_fusion_mpsc_stats";
    int shm_fd = shm_open(stats_shm_name, O_CREAT | O_RDWR, 0666);
    if (shm_fd < 0) {
        printf("Failed to create stats shared memory\n");
        return 1;
    }
    
    if (ftruncate(shm_fd, sizeof(SharedStats)) < 0) {
        printf("Failed to resize stats shared memory\n");
        close(shm_fd);
        return 1;
    }
    
    SharedStats* stats = static_cast<SharedStats*>(
        mmap(nullptr, sizeof(SharedStats), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0));
    
    if (stats == MAP_FAILED) {
        printf("Failed to mmap stats shared memory\n");
        close(shm_fd);
        return 1;
    }
    
    // 初始化统计数据
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
    system("rm -f /dev/shm/camera_fusion_stream");

    SharedMemoryConfig shm_config{};
    shm_config.max_chunks   = 9;           // MPSC模式共享ChunkPool
    shm_config.chunk_size   = kImageSize;   // Payload size only
    shm_config.ipc_type     = IPCType::kMPSC;
    
    auto shm_res = IPCFactory::CreateSHM( kSharedMemoryPath, shm_config );

    if ( !shm_res )
    {
        INNER_CORE_LOG("[ERROR] Failed to create shared memory segment for MPSC channel, error code: %d\n", shm_res.Error().Value() );
        munmap(stats, sizeof(SharedStats));
        close(shm_fd);
        shm_unlink(stats_shm_name);
        return 1;
    }

    std::vector<pid_t> child_pids;
    
    // Fork 3个Camera Publisher进程
    for (UInt32 i = 0; i < 3; ++i) {
        pid_t pid = fork();
        if (pid == 0) {
            // 子进程 - 错开启动时间避免MPSC共享内存初始化竞争
            // 第一个Publisher需要时间创建和初始化shared memory
            std::this_thread::sleep_for(std::chrono::milliseconds(500 + i * 300));
            RunCameraPublisher(i, stats, duration_sec);
            exit(0);
        }
        child_pids.push_back(pid);
    }
    
    // Fork 1个Fusion Subscriber进程
    pid_t fusion_pid = fork();
    if (fusion_pid == 0) {
        // 子进程 - 等待所有Publishers启动并初始化完成
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        try {
            FusionSubscriber fusion(stats, duration_sec);
            fusion.Run();
        } catch (const std::exception& e) {
            fprintf(stderr, "[Fusion] Exception: %s\n", e.what());
            exit(1);
        }
        exit(0);
    }
    child_pids.push_back(fusion_pid);
    
    // 主进程等待所有子进程
    printf("[Main] Started %zu processes\n", child_pids.size());
    printf("[Main] Waiting for completion...\n\n");
    
    int status;
    for (auto pid : child_pids) {
        waitpid(pid, &status, 0);
    }
    
    printf("\n========================================\n");
    printf("Aii: 三摄像头融合示例完成\n");
    printf("========================================\n");
    
    PrintStatsSummary(stats, duration_sec);
    
    printf("\n请检查生成的BMP文件: fusion_mpsc_00000.bmp ~ fusion_mpsc_%05d.bmp\n", 
           kMaxSavedImages - 1);
    
    // 清理共享内存
    munmap(stats, sizeof(SharedStats));
    close(shm_fd);
    shm_unlink(stats_shm_name);
    system("rm -f /dev/shm/camera_fusion_stream");
    
    return 0;
}
