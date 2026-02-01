/**
 * @file        camera_fusion_mpmc_example.cpp
 * @brief       三摄像头融合示例 - MPMC模式（3个独立通道，3 Pub + 3 Sub）
 * @details     每个Camera独立通道，每个Subscriber独立进程带保存线程
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
#include <mutex>

using namespace lap::core;
using namespace lap::core::ipc;

// ============================================================================
// 常量定义
// ============================================================================
constexpr UInt32 kCameraWidth = 1920;
constexpr UInt32 kCameraHeight = 720;
constexpr UInt32 kFusionWidth = 3840;  // 和SPMC一致
constexpr UInt32 kFusionHeight = 1440;  // 和SPMC一致
constexpr UInt32 kBytesPerPixel = 4;  // RGBA
constexpr Size kImageSize = kCameraWidth * kCameraHeight * kBytesPerPixel;
constexpr Size kFusionSize = kFusionWidth * kFusionHeight * kBytesPerPixel;

// 3个独立通道，每个Publisher往所有通道广播
constexpr const char* kSharedMemoryPath[3] = {
    "/camera_mpmc_0",
    "/camera_mpmc_1", 
    "/camera_mpmc_2"
};

constexpr UInt32 kMaxChunks = 3;
constexpr UInt32 kSTMinMs = 10;
constexpr UInt32 kMaxLatencySamples = 10000;
constexpr UInt32 kSavePeriodSec = 5;
constexpr UInt32 kMaxSavedImages = 10;

// ============================================================================
// 共享统计结构
// ============================================================================
struct CameraStats
{
    std::atomic<UInt64> frames_sent{0};
    std::atomic<UInt64> send_failures{0};
    std::atomic<UInt64> total_send_time_us{0};
    std::atomic<UInt32> latency_count{0};
    UInt64 latencies_us[kMaxLatencySamples];
    std::atomic<UInt64> start_timestamp_us{0};
};

struct FusionStats
{
    std::atomic<UInt64> frames_received[3][3]{};  // [channel][publisher]
    std::atomic<UInt64> receive_failures[3][3]{};
    std::atomic<UInt32> latency_count[3][3]{};
    UInt64 latencies_us[3][3][kMaxLatencySamples];
};

struct SharedStats
{
    CameraStats cameras[3];
    FusionStats fusion;
};

// ============================================================================
// 图像编解码器
// ============================================================================
class SimpleImageCodec
{
public:
    explicit SimpleImageCodec(UInt8 camera_id) 
        : camera_id_(camera_id), frame_counter_(0)
    {
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
        UInt8 r = base_color_ & 0xFF;
        UInt8 g = (base_color_ >> 8) & 0xFF;
        UInt8 b = (base_color_ >> 16) & 0xFF;
        
        // 渐变背景
        for (UInt32 y = 0; y < kCameraHeight; ++y) {
            for (UInt32 x = 0; x < kCameraWidth; ++x) {
                UInt8 intensity = (x * 255 / kCameraWidth + y * 255 / kCameraHeight) / 2;
                UInt8 pr = (r * intensity) / 255;
                UInt8 pg = (g * intensity) / 255;
                UInt8 pb = (b * intensity) / 255;
                pixels[y * kCameraWidth + x] = 0xFF000000 | (pb << 16) | (pg << 8) | pr;
            }
        }
        
        // 绘制大号Camera ID（居中，黑色，100x100）
        DrawCameraID(pixels);
        
        // 运动白色方块
        UInt32 block_size = 80;
        UInt32 block_x = (frame_counter_ * 10) % (kCameraWidth - block_size);
        UInt32 block_y = (frame_counter_ * 3) % (kCameraHeight - block_size);
        
        for (UInt32 dy = 0; dy < block_size; ++dy) {
            for (UInt32 dx = 0; dx < block_size; ++dx) {
                UInt32 idx = (block_y + dy) * kCameraWidth + (block_x + dx);
                pixels[idx] = 0xFFFFFFFF;
            }
        }
        
        frame_counter_++;
    }
    
    void DrawCameraID(UInt32* pixels)
    {
        // 画面中央绘制大号Camera ID (100x100像素黑色数字)
        UInt32 center_x = kCameraWidth / 2;
        UInt32 center_y = kCameraHeight / 2;
        UInt32 color = 0xFF000000;  // 黑色
        
        // 简化7段数码管 - 只画camera_id对应的数字
        if (camera_id_ == 0) {
            // 画"0": 矩形框
            for (UInt32 y = center_y - 50; y < center_y + 50; ++y) {
                for (UInt32 x = center_x - 50; x < center_x + 50; ++x) {
                    if ((y < center_y - 35 || y > center_y + 35) || 
                        (x < center_x - 35 || x > center_x + 35)) {
                        if (y < kCameraHeight && x < kCameraWidth) {
                            pixels[y * kCameraWidth + x] = color;
                        }
                    }
                }
            }
        } else if (camera_id_ == 1) {
            // 画"1": 垂直线
            for (UInt32 y = center_y - 50; y < center_y + 50; ++y) {
                for (UInt32 x = center_x - 10; x < center_x + 10; ++x) {
                    if (y < kCameraHeight && x < kCameraWidth) {
                        pixels[y * kCameraWidth + x] = color;
                    }
                }
            }
        } else if (camera_id_ == 2) {
            // 画"2": 上横、右上竖、中横、左下竖、下横
            for (UInt32 dy = 0; dy < 15; ++dy) {
                // 上横
                for (UInt32 dx = 0; dx < 100; ++dx) {
                    UInt32 x = center_x - 50 + dx;
                    UInt32 y = center_y - 50 + dy;
                    if (y < kCameraHeight && x < kCameraWidth) pixels[y * kCameraWidth + x] = color;
                }
                // 中横
                for (UInt32 dx = 0; dx < 100; ++dx) {
                    UInt32 x = center_x - 50 + dx;
                    UInt32 y = center_y - 7 + dy;
                    if (y < kCameraHeight && x < kCameraWidth) pixels[y * kCameraWidth + x] = color;
                }
                // 下横
                for (UInt32 dx = 0; dx < 100; ++dx) {
                    UInt32 x = center_x - 50 + dx;
                    UInt32 y = center_y + 35 + dy;
                    if (y < kCameraHeight && x < kCameraWidth) pixels[y * kCameraWidth + x] = color;
                }
            }
            // 右上竖
            for (UInt32 dy = 0; dy < 45; ++dy) {
                for (UInt32 dx = 0; dx < 15; ++dx) {
                    UInt32 x = center_x + 35 + dx;
                    UInt32 y = center_y - 50 + dy;
                    if (y < kCameraHeight && x < kCameraWidth) pixels[y * kCameraWidth + x] = color;
                }
            }
            // 左下竖
            for (UInt32 dy = 0; dy < 45; ++dy) {
                for (UInt32 dx = 0; dx < 15; ++dx) {
                    UInt32 x = center_x - 50 + dx;
                    UInt32 y = center_y + 5 + dy;
                    if (y < kCameraHeight && x < kCameraWidth) pixels[y * kCameraWidth + x] = color;
                }
            }
        }
    }

private:
    UInt8 camera_id_;
    UInt32 frame_counter_;
    UInt32 base_color_;
};

// ============================================================================
// BMP保存工具
// ============================================================================
bool SaveBMP(const char* filename, const Byte* buffer, UInt32 width, UInt32 height)
{
    FILE* f = fopen(filename, "wb");
    if (!f) return false;
    
    // BMP要求每行4字节对齐
    UInt32 row_size = ((width * 3 + 3) / 4) * 4;
    UInt32 image_size = row_size * height;
    UInt32 file_size = 54 + image_size;
    
    UInt8 bmp_header[54] = {
        'B', 'M',
        0, 0, 0, 0,        // 文件大小
        0, 0, 0, 0,        // 保留字段
        54, 0, 0, 0,       // 像素数据偏移
        40, 0, 0, 0,       // 信息头大小
        0, 0, 0, 0,        // 图像宽度
        0, 0, 0, 0,        // 图像高度
        1, 0,              // 颜色平面数
        24, 0,             // 每像素位数（24位RGB）
        0, 0, 0, 0,        // 压缩方式（0=不压缩）
        0, 0, 0, 0,        // 图像大小
        0x13, 0x0B, 0, 0,  // 水平分辨率
        0x13, 0x0B, 0, 0,  // 垂直分辨率
        0, 0, 0, 0,        // 调色板颜色数
        0, 0, 0, 0         // 重要颜色数
    };
    
    *reinterpret_cast<UInt32*>(&bmp_header[2]) = file_size;
    *reinterpret_cast<UInt32*>(&bmp_header[18]) = width;
    *reinterpret_cast<UInt32*>(&bmp_header[22]) = height;
    
    fwrite(bmp_header, 1, 54, f);
    
    // 转换RGBA到BGR并写入（BMP从下到上存储）
    const UInt32* src_pixels = reinterpret_cast<const UInt32*>(buffer);
    Byte* row_buffer = new Byte[row_size];
    
    for (int y = height - 1; y >= 0; --y) {
        for (UInt32 x = 0; x < width; ++x) {
            UInt32 pixel = src_pixels[y * width + x];
            row_buffer[x * 3 + 0] = (pixel >> 16) & 0xFF;  // B
            row_buffer[x * 3 + 1] = (pixel >> 8) & 0xFF;   // G
            row_buffer[x * 3 + 2] = pixel & 0xFF;          // R
        }
        // 填充对齐字节
        for (UInt32 x = width * 3; x < row_size; ++x) {
            row_buffer[x] = 0;
        }
        fwrite(row_buffer, 1, row_size, f);
    }
    
    delete[] row_buffer;
    fclose(f);
    return true;
}

// ============================================================================
// Camera Publisher进程
// ============================================================================
void CameraPublisherProcess(UInt8 camera_id, SharedStats* stats, UInt32 duration_sec)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(500 + camera_id * 300));
    
    printf("\n========================================\n");
    printf("Camera-%u Publisher - MPMC模式\n", camera_id);
    printf("========================================\n");
    printf("摄像头配置: %ux%u @ 100 FPS (STMin=10ms)\n", kCameraWidth, kCameraHeight);
    printf("MPMC共享通道: %s\n", kSharedMemoryPath[camera_id]);
    printf("测试时长: %u 秒\n", duration_sec);
    printf("========================================\n\n");
    
    // 创建3个Publisher，往3个通道广播
    std::vector<Publisher> publishers;
    for (int ch = 0; ch < 3; ++ch) {
        PublisherConfig config{};
        config.max_chunks = kMaxChunks;
        config.chunk_size = kImageSize;
        config.ipc_type = IPCType::kMPMC;
        config.channel_id = camera_id;  // 使用camera_id作为channel标识
        config.loan_policy = LoanPolicy::kError;
        
        auto pub_result = Publisher::Create(kSharedMemoryPath[ch], config);
        if (!pub_result.HasValue()) {
            fprintf(stderr, "[Camera-%u] Failed to create Publisher for channel %d, error: %d\n", 
                    camera_id, ch, pub_result.Error().Value());
            return;
        }
        publishers.push_back(std::move(pub_result).Value());
    }
    
    printf("[Camera-%u] Created %zu publishers (broadcasting to all channels)\n", camera_id, publishers.size());
    
    auto now_us = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    stats->cameras[camera_id].start_timestamp_us.store(now_us);
    
    SimpleImageCodec codec(camera_id);
    CameraStats& cam_stats = stats->cameras[camera_id];
    
    auto start_time = std::chrono::steady_clock::now();
    
    // 分配临时buffer用于生成一次图像再广播
    Byte* frame_buffer = new Byte[kImageSize];
    
    while (std::chrono::steady_clock::now() - start_time < std::chrono::seconds(duration_sec)) {
        auto send_start = std::chrono::high_resolution_clock::now();
        
        // 先生成一帧图像（只调用一次）
        codec.GenerateFrame(frame_buffer, kImageSize);
        
        // 广播到所有通道
        bool all_success = true;
        for (auto& publisher : publishers) {
            auto result = publisher.Send([&frame_buffer](UInt8 /*channel_id*/, Byte* chunk_ptr, Size chunk_size) -> Size {
                UNUSED(chunk_size);
                std::memcpy(chunk_ptr, frame_buffer, kImageSize);
                return kImageSize;
            });
            
            if (!result.HasValue()) {
                all_success = false;
            }
        }
        
        auto send_end = std::chrono::high_resolution_clock::now();
        UInt64 send_time_us = std::chrono::duration_cast<std::chrono::microseconds>(
            send_end - send_start).count();
        
        if (!all_success) {
            cam_stats.send_failures.fetch_add(1, std::memory_order_relaxed);
        } else {
            UInt64 frame_num = cam_stats.frames_sent.fetch_add(1, std::memory_order_relaxed);
            cam_stats.total_send_time_us.fetch_add(send_time_us, std::memory_order_relaxed);
            
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
// Subscriber进程（带Saver线程）
// ============================================================================
void SubscriberProcess(UInt8 camera_id, SharedStats* stats, UInt32 duration_sec)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    
    printf("\n========================================\n");
    printf("Subscriber-%u - MPMC模式\n", camera_id);
    printf("订阅通道: %s\n", kSharedMemoryPath[camera_id]);
    printf("========================================\n\n");
    
    SubscriberConfig config{};
    config.max_chunks = kMaxChunks;
    config.chunk_size = kImageSize;
    config.ipc_type = IPCType::kMPMC;
    config.STmin = kSTMinMs;
    config.empty_policy = SubscribePolicy::kSkip;
    
    auto sub_result = Subscriber::Create(kSharedMemoryPath[camera_id], config);
    if (!sub_result.HasValue()) {
        fprintf(stderr, "[Subscriber-%u] Failed to create Subscriber, error: %d\n",
                camera_id, sub_result.Error().Value());
        return;
    }
    auto subscriber = std::move(sub_result).Value();
    subscriber.Connect();
    
    printf("[Subscriber-%u] Connected\n", camera_id);
    
    // 分配融合图buffer
    Byte* fusion_buffer = new Byte[kFusionSize];
    std::memset(fusion_buffer, 0, kFusionSize);

    std::mutex fusion_mutex;
    
    std::atomic<bool> running{true};
    std::atomic<bool> buffer_updated{false};
    
    // 启动Saver线程
    std::thread saver_thread([&]() {
        printf("[Subscriber-%u Saver] Started (save every %us)\n", camera_id, kSavePeriodSec);
        
        UInt32 save_counter = 0;
        auto start_time = std::chrono::steady_clock::now();
        std::vector<Byte> snapshot(kFusionSize);
        
        while (std::chrono::steady_clock::now() - start_time < std::chrono::seconds(duration_sec)) {
            std::this_thread::sleep_for(std::chrono::seconds(kSavePeriodSec));
            
            if (!running.load()) break;
            
            if (buffer_updated.load()) {
                UInt32 file_idx = save_counter % kMaxSavedImages;
                char filename[128];
                snprintf(filename, sizeof(filename), "fusion_mpmc_%u_%05u.bmp", camera_id, file_idx);

                {
                    std::lock_guard<std::mutex> lock(fusion_mutex);
                    std::memcpy(snapshot.data(), fusion_buffer, kFusionSize);
                }

                if (SaveBMP(filename, snapshot.data(), kFusionWidth, kFusionHeight)) {
                    printf("[Subscriber-%u Saver] Saved: %s\n", camera_id, filename);
                    save_counter++;
                }
            }
        }
        
        printf("[Subscriber-%u Saver] Stopped\n", camera_id);
    });
    
    // 接收循环
    auto start_time = std::chrono::steady_clock::now();
    
    while (std::chrono::steady_clock::now() - start_time < std::chrono::seconds(duration_sec)) {
        auto recv_start = std::chrono::high_resolution_clock::now();
        
        auto result = subscriber.Receive([&](UInt8 ch_id, Byte* data, Size size) -> Size {
            if (size != kImageSize) return 0;
            
            auto recv_end = std::chrono::high_resolution_clock::now();
            UInt64 recv_time_us = std::chrono::duration_cast<std::chrono::microseconds>(
                recv_end - recv_start).count();
            
            // 根据channel_id拼接（和SPMC一致的布局）
            UInt32 offset_x = 0, offset_y = 0;
            if (ch_id == 0) {
                offset_x = 0; offset_y = 0;  // 左上
            } else if (ch_id == 1) {
                offset_x = kCameraWidth; offset_y = 0;  // 右上
            } else if (ch_id == 2) {
                offset_x = 960; offset_y = kCameraHeight;  // 下居中
            }
            
            // 验证数据：检查中心像素验证Camera ID
            // const UInt32* verify_pixels = reinterpret_cast<const UInt32*>(data);
            // UInt32 center_pixel = verify_pixels[(kCameraHeight/2) * kCameraWidth + (kCameraWidth/2)];
            // UInt8 pixel_r = center_pixel & 0xFF;
            // UInt8 pixel_g = (center_pixel >> 8) & 0xFF;
            // UInt8 pixel_b = (center_pixel >> 16) & 0xFF;
            
            // // 验证颜色与Camera ID匹配
            // bool data_valid = false;
            // if (ch_id == 0 && pixel_r > 200 && pixel_g < 50 && pixel_b < 50) data_valid = true;  // 红色
            // if (ch_id == 1 && pixel_g > 200 && pixel_r < 50 && pixel_b < 50) data_valid = true;  // 绿色
            // if (ch_id == 2 && pixel_b > 200 && pixel_r < 50 && pixel_g < 50) data_valid = true;  // 蓝色
            
            // if (!data_valid) {
            //     printf("[Subscriber-%u] WARNING: Data mismatch! ch_id=%u but pixel=(%u,%u,%u)\n",
            //            camera_id, ch_id, pixel_r, pixel_g, pixel_b);
            // }
            
            // 拼接到融合图
            const UInt32* src_pixels = reinterpret_cast<const UInt32*>(data);

            {
                std::lock_guard<std::mutex> lock(fusion_mutex);
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
            }
            
            buffer_updated.store(true);
            
            UInt8 pub_id = ch_id;
            UInt64 frame_num = stats->fusion.frames_received[camera_id][pub_id].fetch_add(1, std::memory_order_relaxed);
            
            if (frame_num % 10 == 0) {
                UInt32 idx = stats->fusion.latency_count[camera_id][pub_id].fetch_add(1, std::memory_order_relaxed);
                if (idx < kMaxLatencySamples) {
                    stats->fusion.latencies_us[camera_id][pub_id][idx] = recv_time_us;
                }
            }
            
            return size;
        });
        
        if (!result.HasValue() || result.Value() == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    
    running.store(false);
    saver_thread.join();
    
    UInt64 total_frames = 0;
    for (int p = 0; p < 3; ++p) {
        total_frames += stats->fusion.frames_received[camera_id][p].load();
    }
    printf("[Subscriber-%u] Completed: %lu frames\n",
           camera_id, total_frames);
    
    delete[] fusion_buffer;
}

// ============================================================================
// 统计打印
// ============================================================================
void PrintStatsSummary(SharedStats* stats, UInt32 duration_sec)
{
    auto end_us = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    
    printf("\n========================================\n");
    printf("性能统计汇总 (MPMC模式)\n");
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
        
        UInt64 start_us = cam.start_timestamp_us.load();
        double camera_duration = start_us > 0 ? (end_us - start_us) / 1000000.0 : duration_sec;
        double fps = camera_duration > 0 ? static_cast<double>(frames) / camera_duration : 0.0;
        
        UInt32 count = cam.latency_count.load();
        if (count > kMaxLatencySamples) count = kMaxLatencySamples;
        
        std::vector<UInt64> latencies;
        for (UInt32 j = 0; j < count; ++j) {
            latencies.push_back(cam.latencies_us[j]);
        }
        std::sort(latencies.begin(), latencies.end());
        
        UInt64 p50 = latencies.empty() ? 0 : latencies[latencies.size() / 2];
        UInt64 p99 = latencies.empty() ? 0 : latencies[latencies.size() * 99 / 100];
        UInt64 max_lat = latencies.empty() ? 0 : latencies.back();
        
        printf("│ Cam-%d   │ %10lu │ %11lu │ %8.1f │ %11lu │ %11lu │ %11lu │ %11lu │\n",
               i, frames, errors, fps, avg_us, p50, p99, max_lat);
    }
    
    printf("└─────────┴────────────┴─────────────┴──────────┴─────────────┴─────────────┴─────────────┴─────────────┘\n\n");
    
    // Subscriber统计
    printf("[ Subscribers ]\n");
    printf("┌─────────┬──────────┬────────────┬──────────┬──────────┬─────────────┬─────────────┬─────────────┬─────────────┐\n");
    printf("│ Stream  │ Pub-ID   │ Frames Recv│ FPS      │ STMin(ms)│   Avg (us)  │   P50 (us)  │   P99 (us)  │   Max (us)  │\n");
    printf("├─────────┼──────────┼────────────┼──────────┼──────────┼─────────────┼─────────────┼─────────────┼─────────────┤\n");

    for (int i = 0; i < 3; ++i) {
        for (int p = 0; p < 3; ++p) {
            UInt64 frames = stats->fusion.frames_received[i][p].load();
            double fps = frames / static_cast<double>(duration_sec);
            
            UInt32 count = stats->fusion.latency_count[i][p].load();
            if (count > kMaxLatencySamples) count = kMaxLatencySamples;
            
            std::vector<UInt64> latencies;
            for (UInt32 j = 0; j < count; ++j) {
                latencies.push_back(stats->fusion.latencies_us[i][p][j]);
            }
            std::sort(latencies.begin(), latencies.end());
            
            UInt64 avg = 0;
            if (!latencies.empty()) {
                UInt64 sum = 0;
                for (auto lat : latencies) sum += lat;
                avg = sum / latencies.size();
            }
            
            UInt64 p50 = latencies.empty() ? 0 : latencies[latencies.size() / 2];
            UInt64 p99 = latencies.empty() ? 0 : latencies[latencies.size() * 99 / 100];
            UInt64 max_lat = latencies.empty() ? 0 : latencies.back();
            
            printf("│ Cam-%d   │ Pub-%d   │ %10lu │ %8.1f │ %8u │ %11lu │ %11lu │ %11lu │ %11lu │\n",
                   i, p, frames, fps, kSTMinMs, avg, p50, p99, max_lat);
        }
    }
    
    printf("└─────────┴──────────┴────────────┴──────────┴──────────┴─────────────┴─────────────┴─────────────┴─────────────┘\n");
}

// ============================================================================
// 主函数
// ============================================================================
int main(int argc, char* argv[])
{
    UInt32 duration_sec = 30;
    if (argc > 1) {
        duration_sec = std::atoi(argv[1]);
    }
    
    // 预先创建3个MPMC共享内存通道
    printf("[Main] Pre-creating 3 MPMC shared memory channels...\n");
    std::vector<std::unique_ptr<SharedMemoryManager>> shm_managers;
    
    for (int i = 0; i < 3; ++i) {
        auto shm = std::make_unique<SharedMemoryManager>();
        
        SharedMemoryConfig shm_config{};
        shm_config.max_chunks = kMaxChunks;
        shm_config.chunk_size = kImageSize;
        shm_config.ipc_type = IPCType::kMPMC;
        
        auto result = shm->Create(kSharedMemoryPath[i], shm_config);
        if (!result) {
            fprintf(stderr, "[Main] Failed to create shm %s, error: %d\n", 
                    kSharedMemoryPath[i], result.Error().Value());
            return 1;
        }
        printf("[Main] Created shared memory: %s\n", kSharedMemoryPath[i]);
        shm_managers.push_back(std::move(shm));
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // 创建共享统计内存
    const char* stats_shm_name = "/camera_fusion_mpmc_stats";
    int shm_fd = shm_open(stats_shm_name, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open failed");
        return 1;
    }
    
    if (ftruncate(shm_fd, sizeof(SharedStats)) == -1) {
        perror("ftruncate failed");
        return 1;
    }
    
    SharedStats* stats = static_cast<SharedStats*>(
        mmap(nullptr, sizeof(SharedStats), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0)
    );
    
    if (stats == MAP_FAILED) {
        perror("mmap failed");
        return 1;
    }
    
    new (stats) SharedStats();
    
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
            SubscriberProcess(i, stats, duration_sec);
            exit(0);
        }
        child_pids.push_back(pid);
    }
    
    printf("[Main] Started %zu processes (3 Pub + 3 Sub)\n", child_pids.size());
    printf("[Main] Waiting for completion...\n\n");
    
    // 等待所有子进程
    int status;
    for (auto pid : child_pids) {
        waitpid(pid, &status, 0);
    }
    
    printf("\n========================================\n");
    printf("三摄像头示例完成 (MPMC)\n");
    printf("========================================\n");
    
    PrintStatsSummary(stats, duration_sec);
    
    printf("\n请检查生成的BMP文件:\n");
    printf("  fusion_mpmc_0_00000.bmp ~ fusion_mpmc_0_%05d.bmp (Sub-0的融合图)\n", kMaxSavedImages - 1);
    printf("  fusion_mpmc_1_00000.bmp ~ fusion_mpmc_1_%05d.bmp (Sub-1的融合图)\n", kMaxSavedImages - 1);
    printf("  fusion_mpmc_2_00000.bmp ~ fusion_mpmc_2_%05d.bmp (Sub-2的融合图)\n", kMaxSavedImages - 1);
    
    // 清理
    munmap(stats, sizeof(SharedStats));
    close(shm_fd);
    shm_unlink(stats_shm_name);
    
    return 0;
}
