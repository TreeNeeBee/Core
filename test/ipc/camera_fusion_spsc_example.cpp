/**
 * @file        camera_fusion_spsc_example.cpp
 * @brief       三摄像头融合示例 - SPSC模式（3个独立通道，3 Pub + 3 Sub）
 * @details     对比MPSC模式，测试SPSC独立通道的性能表现
 */

#include "ipc/Publisher.hpp"
#include "ipc/Subscriber.hpp"
#include "ipc/SharedMemoryManager.hpp"
#include "CTypedef.hpp"
#include <cstdio>
#include <cstring>
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>
#include <algorithm>

using namespace lap::core;
using namespace lap::core::ipc;

// ============================================================================
// 常量定义
// ============================================================================
constexpr UInt32 kCameraWidth = 1920;
constexpr UInt32 kCameraHeight = 720;
constexpr UInt32 kFusionWidth = 3840;
constexpr UInt32 kFusionHeight = 1440;
constexpr UInt32 kBytesPerPixel = 4;  // RGBA
constexpr Size kImageSize = kCameraWidth * kCameraHeight * kBytesPerPixel;
constexpr Size kFusionSize = kFusionWidth * kFusionHeight * kBytesPerPixel;

// 每个Camera使用独立的共享内存通道
constexpr const char* kSharedMemoryPath[3] = {
    "/camera_spsc_0",
    "/camera_spsc_1", 
    "/camera_spsc_2"
};

constexpr UInt32 kMaxChunks = 2;  // SPSC模式：2个chunk足够 (1 Pub + 1 Sub)，总内存：3×11MB=33MB
constexpr UInt32 kSTMinMs = 10;  // 10ms限流，理论100 FPS
constexpr UInt32 kMaxLatencySamples = 10000;  // 减少采样数量以节省共享内存 (~0.5MB per stats)
constexpr UInt32 kSavePeriodSec = 5;
constexpr UInt32 kMaxSavedImages = 10;

// ============================================================================
// 共享统计结构（用于进程间通信统计数据）
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
// 简单图像编解码器（用于生成测试图像）
// ============================================================================
class SimpleImageCodec
{
public:
    explicit SimpleImageCodec(UInt8 camera_id) 
        : camera_id_(camera_id)
        , frame_counter_(0)
    {
        // 每个摄像头使用不同的基础颜色
        if (camera_id == 0) {
            base_color_ = 0xFF0000FF;  // 红色
        } else if (camera_id == 1) {
            base_color_ = 0xFF00FF00;  // 绿色  
        } else {
            base_color_ = 0xFFFF0000;  // 蓝色
        }
    }

    void GenerateFrame(Byte* buffer, Size size)
    {
        if (size < kImageSize) return;
        
        UInt32* pixels = reinterpret_cast<UInt32*>(buffer);
        UInt32 total_pixels = kCameraWidth * kCameraHeight;
        
        // 生成渐变图案
        for (UInt32 i = 0; i < total_pixels; ++i) {
            UInt32 brightness = (i * 255) / total_pixels;
            UInt32 r = ((base_color_ >> 0) & 0xFF) * brightness / 255;
            UInt32 g = ((base_color_ >> 8) & 0xFF) * brightness / 255;
            UInt32 b = ((base_color_ >> 16) & 0xFF) * brightness / 255;
            pixels[i] = 0xFF000000 | (b << 16) | (g << 8) | r;
        }

        // 添加摄像头ID标识 (居中显示，黑色)
        DrawCameraID(pixels);

        // 添加运动色块 (模拟移动物体)
        UInt32 block_size = 80;
        UInt32 block_x = (frame_counter_ * 10) % (kCameraWidth - block_size);
        UInt32 block_y = (frame_counter_ * 3) % (kCameraHeight - block_size);

        for (UInt32 dy = 0; dy < block_size; ++dy) {
            for (UInt32 dx = 0; dx < block_size; ++dx) {
                UInt32 idx = (block_y + dy) * kCameraWidth + (block_x + dx);
                pixels[idx] = 0xFFFFFFFF;  // 白色方块 (RGBA)
            }
        }

        ++frame_counter_;
    }

private:
    void DrawCameraID(UInt32* pixels)
    {
        UInt32 center_x = kCameraWidth / 2;
        UInt32 center_y = kCameraHeight / 2;
        DrawBigDigit(pixels, camera_id_, center_x - 50, center_y - 50, 0xFF000000);
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

        if (segments[digit][0]) draw_rect(x + seg_w, y, seg_h, seg_w);
        if (segments[digit][1]) draw_rect(x + seg_w + seg_h + gap, y + seg_w, seg_w, seg_h);
        if (segments[digit][2]) draw_rect(x + seg_w + seg_h + gap, y + seg_w + seg_h + gap, seg_w, seg_h);
        if (segments[digit][3]) draw_rect(x + seg_w, y + 2 * seg_w + 2 * seg_h + 2 * gap, seg_h, seg_w);
        if (segments[digit][4]) draw_rect(x, y + seg_w + seg_h + gap, seg_w, seg_h);
        if (segments[digit][5]) draw_rect(x, y + seg_w, seg_w, seg_h);
        if (segments[digit][6]) draw_rect(x + seg_w, y + seg_w + seg_h + gap, seg_h, seg_w);
    }

    UInt8 camera_id_;
    UInt32 base_color_;
    UInt64 frame_counter_;
};

// ============================================================================
// BMP文件保存
// ============================================================================
bool SaveBMP(const char* filename, const Byte* buffer, UInt32 width, UInt32 height)
{
    FILE* f = fopen(filename, "wb");
    if (!f) return false;
    
    UInt32 file_size = 54 + width * height * 4;
    UInt8 bmp_header[54] = {
        'B', 'M',                      // Magic
        0, 0, 0, 0,                    // File size (filled below)
        0, 0, 0, 0,                    // Reserved
        54, 0, 0, 0,                   // Offset to pixel data
        40, 0, 0, 0,                   // DIB header size
        0, 0, 0, 0,                    // Width (filled below)
        0, 0, 0, 0,                    // Height (filled below)
        1, 0,                          // Color planes
        32, 0,                         // Bits per pixel
        0, 0, 0, 0,                    // Compression (none)
        0, 0, 0, 0,                    // Image size (can be 0)
        0x13, 0x0B, 0, 0,              // X pixels per meter
        0x13, 0x0B, 0, 0,              // Y pixels per meter
        0, 0, 0, 0,                    // Colors in palette
        0, 0, 0, 0                     // Important colors
    };
    
    *reinterpret_cast<UInt32*>(&bmp_header[2]) = file_size;
    *reinterpret_cast<UInt32*>(&bmp_header[18]) = width;
    *reinterpret_cast<UInt32*>(&bmp_header[22]) = height;
    
    fwrite(bmp_header, 1, 54, f);
    
    // BMP从底部开始存储
    for (int y = height - 1; y >= 0; --y) {
        fwrite(buffer + y * width * 4, 1, width * 4, f);
    }
    
    fclose(f);
    return true;
}

// ============================================================================
// Camera Publisher进程（SPSC模式 - 每个Camera独立通道）
// ============================================================================
void CameraPublisherProcess(UInt8 camera_id, SharedStats* stats, UInt32 duration_sec)
{
    // 延迟启动，避免同时初始化
    std::this_thread::sleep_for(std::chrono::milliseconds(500 + camera_id * 300));
    
    printf("\n========================================\n");
    printf("Aii: 三摄像头融合示例 - SPSC模式\n");
    printf("========================================\n");
    printf("摄像头配置: %ux%u @ 100 FPS (STMin=10ms)\n", kCameraWidth, kCameraHeight);
    printf("融合图尺寸: %ux%u\n", kFusionWidth, kFusionHeight);
    printf("单帧大小: %zu MB\n", kImageSize / 1024 / 1024);
    printf("SPSC共享通道: %s\n", kSharedMemoryPath[camera_id]);
    printf("测试时长: %u 秒\n", duration_sec);
    printf("========================================\n\n");
    
    // 配置Publisher（SPSC模式）
    PublisherConfig config{};
    config.max_chunks = kMaxChunks;
    config.chunk_size = kImageSize;
    config.ipc_type = IPCType::kSPSC;  // SPSC模式
    config.channel_id = kInvalidChannelID;
    config.loan_policy = LoanPolicy::kError;
    
    auto pub_result = Publisher::Create(kSharedMemoryPath[camera_id], config);
    if (!pub_result.HasValue()) {
        fprintf(stderr, "[Camera-%u] Failed to create Publisher, error code: %d\n", 
                camera_id, pub_result.Error().Value());
        return;
    }
    auto publisher = std::move(pub_result).Value();
    
    printf("[Camera-%u] SPSC Publisher created, path=%s\n", 
           camera_id, kSharedMemoryPath[camera_id]);
    
    auto now_us = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    stats->cameras[camera_id].start_timestamp_us.store(now_us);
    
    SimpleImageCodec codec(camera_id);
    CameraStats& cam_stats = stats->cameras[camera_id];
    
    auto start_time = std::chrono::steady_clock::now();
    
    while (std::chrono::steady_clock::now() - start_time < std::chrono::seconds(duration_sec)) {
        auto send_start = std::chrono::high_resolution_clock::now();
        
        auto result = publisher.Send([&codec](UInt8 /*channel_id*/, Byte* chunk_ptr, Size chunk_size) -> Size {
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
            
            // 每10帧采样一次延迟
            if (frame_num % 10 == 0) {
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
// Subscriber进程（SPSC模式 - 每个Camera独立Subscriber）
// ============================================================================
void SubscriberProcess(UInt8 camera_id, SharedStats* stats, 
                       Byte* fusion_buffer, std::atomic<UInt32>& /*save_counter*/,
                       UInt32 duration_sec)
{
    // 等待Publisher启动
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    
    SubscriberConfig config{};
    config.max_chunks = kMaxChunks;
    config.chunk_size = kImageSize;
    config.ipc_type = IPCType::kSPSC;
    config.channel_id = kInvalidChannelID;
    config.STmin = kSTMinMs;
    config.timeout = 100'000'000;  // 100ms
    config.empty_policy = SubscribePolicy::kSkip;
    
    auto sub_result = Subscriber::Create(kSharedMemoryPath[camera_id], config);
    if (!sub_result.HasValue()) {
        fprintf(stderr, "[Subscriber-%u] Failed to create Subscriber\n", camera_id);
        return;
    }
    auto subscriber = std::move(sub_result).Value();
    subscriber.Connect();
    
    printf("[Subscriber-%u] Connected to %s\n", camera_id, kSharedMemoryPath[camera_id]);
    
    auto start_time = std::chrono::steady_clock::now();
    
    // 计算拼接位置
    UInt32 offset_x = 0, offset_y = 0;
    if (camera_id == 0) {
        offset_x = 0; offset_y = 0;
    } else if (camera_id == 1) {
        offset_x = kCameraWidth; offset_y = 0;
    } else {
        offset_x = 960; offset_y = kCameraHeight;
    }
    
    while (std::chrono::steady_clock::now() - start_time < std::chrono::seconds(duration_sec)) {
        auto recv_start = std::chrono::high_resolution_clock::now();
        
        auto result = subscriber.Receive([&](UInt8 /*ch_id*/, Byte* data, Size size) -> Size {
            if (size != kImageSize) return 0;
            
            auto recv_end = std::chrono::high_resolution_clock::now();
            UInt64 recv_time_us = std::chrono::duration_cast<std::chrono::microseconds>(
                recv_end - recv_start).count();
            
            // 拼接到fusion buffer
            const UInt32* src_pixels = reinterpret_cast<const UInt32*>(data);
            UInt32* dst_pixels = reinterpret_cast<UInt32*>(fusion_buffer);
            
            for (UInt32 y = 0; y < kCameraHeight; ++y) {
                UInt32 dst_y = offset_y + y;
                if (dst_y >= kFusionHeight) break;
                
                UInt32 src_offset = y * kCameraWidth;
                UInt32 dst_offset = dst_y * kFusionWidth + offset_x;
                
                for (UInt32 x = 0; x < kCameraWidth && (offset_x + x) < kFusionWidth; ++x) {
                    dst_pixels[dst_offset + x] = src_pixels[src_offset + x];
                }
            }
            
            UInt64 frame_num = stats->fusion.frames_received[camera_id].fetch_add(1, std::memory_order_relaxed);
            
            // 每10帧采样一次延迟
            if (frame_num % 10 == 0) {
                UInt32 idx = stats->fusion.latency_count[camera_id].fetch_add(1, std::memory_order_relaxed);
                if (idx < kMaxLatencySamples) {
                    stats->fusion.latencies_us[camera_id][idx] = recv_time_us;
                }
            }
            
            return size;
        });
        
        if (!result.HasValue() || result.Value() == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    
    printf("[Subscriber-%u] Completed: %lu frames\n",
           camera_id, stats->fusion.frames_received[camera_id].load());
}

// ============================================================================
// Saver进程（定期保存融合图像）
// ============================================================================
void SaverProcess(Byte* fusion_buffer, std::atomic<bool>& running, UInt32 duration_sec)
{
    printf("[Saver] Started (save every %us)\n", kSavePeriodSec);
    
    UInt32 save_counter = 0;
    auto start_time = std::chrono::steady_clock::now();
    
    while (running.load() && 
           std::chrono::steady_clock::now() - start_time < std::chrono::seconds(duration_sec)) {
        std::this_thread::sleep_for(std::chrono::seconds(kSavePeriodSec));
        
        if (!running.load()) break;
        
        char filename[64];
        snprintf(filename, sizeof(filename), "fusion_spsc_%05u.bmp", save_counter);
        
        if (SaveBMP(filename, fusion_buffer, kFusionWidth, kFusionHeight)) {
            printf("[Saver] Saved %s\n", filename);
        }
        
        save_counter = (save_counter + 1) % kMaxSavedImages;
    }
    
    printf("[Saver] Stopped\n");
}

// ============================================================================
// 统计打印
// ============================================================================
void PrintStatsSummary(SharedStats* stats, UInt32 duration_sec)
{
    printf("\n========================================\n");
    printf("性能统计汇总 (SPSC模式)\n");
    printf("========================================\n");
    printf("总运行时长: %us\n", duration_sec);
    printf("========================================\n\n");
    
    // Camera Publishers统计
    printf("[ Camera Publishers ]\n");
    printf("┌─────────┬────────────┬─────────────┬──────────┬─────────────┬─────────────┬─────────────┬─────────────┐\n");
    printf("│ Camera  │ Frames Sent│ Send Errors │ FPS      │   Avg (us)  │   P50 (us)  │   P99 (us)  │   Max (us)  │\n");
    printf("├─────────┼────────────┼─────────────┼──────────┼─────────────┼─────────────┼─────────────┼─────────────┤\n");
    
    for (int i = 0; i < 3; ++i) {
        UInt64 frames = stats->cameras[i].frames_sent.load();
        UInt64 failures = stats->cameras[i].send_failures.load();
        UInt64 total_time = stats->cameras[i].total_send_time_us.load();
        UInt32 count = stats->cameras[i].latency_count.load();
        
        double fps = frames / static_cast<double>(duration_sec);
        UInt64 avg = frames > 0 ? total_time / frames : 0;
        
        std::vector<UInt64> latencies;
        for (UInt32 j = 0; j < count && j < kMaxLatencySamples; ++j) {
            latencies.push_back(stats->cameras[i].latencies_us[j]);
        }
        std::sort(latencies.begin(), latencies.end());
        
        UInt64 p50 = latencies.empty() ? 0 : latencies[latencies.size() / 2];
        UInt64 p99 = latencies.empty() ? 0 : latencies[latencies.size() * 99 / 100];
        UInt64 max_lat = latencies.empty() ? 0 : latencies.back();
        
        printf("│ Cam-%d   │ %10lu │ %11lu │ %8.1f │ %11lu │ %11lu │ %11lu │ %11lu │\n",
               i, frames, failures, fps, avg, p50, p99, max_lat);
    }
    printf("└─────────┴────────────┴─────────────┴──────────┴─────────────┴─────────────┴─────────────┴─────────────┘\n\n");
    
    // Subscribers统计
    printf("[ Subscribers ]\n");
    printf("┌─────────┬────────────┬──────────┬──────────┬─────────────┬─────────────┬─────────────┬─────────────┐\n");
    printf("│ Stream  │ Frames Recv│ FPS      │ STMin(ms)│   Avg (us)  │   P50 (us)  │   P99 (us)  │   Max (us)  │\n");
    printf("├─────────┼────────────┼──────────┼──────────┼─────────────┼─────────────┼─────────────┼─────────────┤\n");
    
    for (int i = 0; i < 3; ++i) {
        UInt64 frames = stats->fusion.frames_received[i].load();
        UInt32 count = stats->fusion.latency_count[i].load();
        
        double fps = frames / static_cast<double>(duration_sec);
        UInt64 avg = 0;
        
        std::vector<UInt64> latencies;
        for (UInt32 j = 0; j < count && j < kMaxLatencySamples; ++j) {
            latencies.push_back(stats->fusion.latencies_us[i][j]);
        }
        std::sort(latencies.begin(), latencies.end());
        
        if (!latencies.empty()) {
            UInt64 sum = 0;
            for (auto lat : latencies) sum += lat;
            avg = sum / latencies.size();
        }
        
        UInt64 p50 = latencies.empty() ? 0 : latencies[latencies.size() / 2];
        UInt64 p99 = latencies.empty() ? 0 : latencies[latencies.size() * 99 / 100];
        UInt64 max_lat = latencies.empty() ? 0 : latencies.back();
        
        printf("│ Cam-%d   │ %10lu │ %8.1f │ %8u │ %11lu │ %11lu │ %11lu │ %11lu │\n",
               i, frames, fps, kSTMinMs, avg, p50, p99, max_lat);
    }
    printf("└─────────┴────────────┴──────────┴──────────┴─────────────┴─────────────┴─────────────┴─────────────┘\n");
}

// ============================================================================
// 主函数
// ============================================================================
int main(int argc, char* argv[])
{
    UInt32 duration_sec = 30;  // 默认30秒
    if (argc > 1) {
        duration_sec = std::atoi(argv[1]);
    }
    
    // 预先创建3个SPSC共享内存通道（保持存活直到程序结束）
    printf("[Main] Pre-creating 3 SPSC shared memory channels...\n");
    std::vector<std::unique_ptr<SharedMemoryManager>> shm_managers;
    
    for (int i = 0; i < 3; ++i) {
        auto shm = std::make_unique<SharedMemoryManager>();
        
        SharedMemoryConfig shm_config{};
        shm_config.max_chunks = kMaxChunks;
        shm_config.chunk_size = kImageSize;
        shm_config.ipc_type = IPCType::kSPSC;
        
        auto result = shm->Create(kSharedMemoryPath[i], shm_config);
        if (!result) {
            fprintf(stderr, "[Main] Failed to create shm %s, error: %d\n", 
                    kSharedMemoryPath[i], result.Error().Value());
            return 1;
        }
        printf("[Main] Created shared memory: %s\n", kSharedMemoryPath[i]);
        shm_managers.push_back(std::move(shm));  // 保持存活
    }
    
    // 短暂延迟确保共享内存完全初始化
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    printf("[Main] Creating shared stats memory...\n");
    // 创建共享统计内存
    const char* stats_shm_name = "/camera_fusion_spsc_stats";
    int shm_fd = shm_open(stats_shm_name, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open failed");
        return 1;
    }
    
    ftruncate(shm_fd, sizeof(SharedStats));
    SharedStats* stats = static_cast<SharedStats*>(
        mmap(nullptr, sizeof(SharedStats), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0)
    );
    
    if (stats == MAP_FAILED) {
        perror("mmap failed");
        return 1;
    }
    
    printf("[Main] Initializing stats structure...\n");
    new (stats) SharedStats();  // 初始化
    
    printf("[Main] Creating fusion buffer memory...\n");
    // 创建融合buffer共享内存
    const char* fusion_shm_name = "/camera_fusion_spsc_buffer";
    int fusion_fd = shm_open(fusion_shm_name, O_CREAT | O_RDWR, 0666);
    if (fusion_fd == -1) {
        perror("fusion shm_open failed");
        return 1;
    }
    if (ftruncate(fusion_fd, kFusionSize) == -1) {
        perror("fusion ftruncate failed");
        return 1;
    }
    Byte* fusion_buffer = static_cast<Byte*>(
        mmap(nullptr, kFusionSize, PROT_READ | PROT_WRITE, MAP_SHARED, fusion_fd, 0)
    );
    if (fusion_buffer == MAP_FAILED) {
        perror("fusion mmap failed");
        return 1;
    }
    printf("[Main] Fusion buffer mapped at %p, size=%zu MB\n", (void*)fusion_buffer, kFusionSize / 1024 / 1024);
    printf("[Main] Clearing fusion buffer...\n");
    memset(fusion_buffer, 0, kFusionSize);
    
    std::atomic<bool> running{true};
    std::atomic<UInt32> save_counter{0};
    std::vector<pid_t> child_pids;
    
    // Fork 3个Camera Publisher进程
    for (int i = 0; i < 3; ++i) {
        pid_t pid = fork();
        if (pid == 0) {
            CameraPublisherProcess(i, stats, duration_sec);
            exit(0);
        }
        child_pids.push_back(pid);
    }
    
    // Fork 3个Subscriber进程
    for (int i = 0; i < 3; ++i) {
        pid_t pid = fork();
        if (pid == 0) {
            SubscriberProcess(i, stats, fusion_buffer, save_counter, duration_sec);
            exit(0);
        }
        child_pids.push_back(pid);
    }
    
    // Fork 1个Saver进程
    pid_t saver_pid = fork();
    if (saver_pid == 0) {
        SaverProcess(fusion_buffer, running, duration_sec);
        exit(0);
    }
    child_pids.push_back(saver_pid);
    
    printf("[Main] Started %zu processes (3 Pub + 3 Sub + 1 Saver)\n", child_pids.size());
    printf("[Main] Waiting for completion...\n\n");
    
    // 等待所有子进程
    int status;
    for (auto pid : child_pids) {
        waitpid(pid, &status, 0);
    }
    
    running.store(false);
    
    printf("\n========================================\n");
    printf("Aii: 三摄像头融合示例完成 (SPSC)\n");
    printf("========================================\n");
    
    PrintStatsSummary(stats, duration_sec);
    
    printf("\n请检查生成的BMP文件: fusion_spsc_00000.bmp ~ fusion_spsc_%05d.bmp\n", 
           kMaxSavedImages - 1);
    
    // 清理
    munmap(stats, sizeof(SharedStats));
    munmap(fusion_buffer, kFusionSize);
    close(shm_fd);
    close(fusion_fd);
    shm_unlink(stats_shm_name);
    shm_unlink(fusion_shm_name);
    
    return 0;
}
