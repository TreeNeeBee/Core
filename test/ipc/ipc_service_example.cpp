/**
 * @file        ipc_service_example.cpp
 * @brief       IPC标准服务示例 - Field / RPC / Event
 * @details     使用两个共享内存通道：
 *              - MPSC通道接收外部请求 (client -> server)
 *              - SPMC通道发送ACK、RPC返回、Field与Event (server -> clients)
 *
 * ============================================================================
 * 实例说明
 * ============================================================================
 * 1) 通道设计
 *    - /svc_req_mpsc : MPSC（多客户端 -> 单服务端）
 *    - /svc_rsp_spmc : SPMC（单服务端 -> 多客户端）
 *
 * 2) 消息类型
 *    - FieldUpdate : 服务端周期发布字段值（广播）
 *    - EventNotify : 服务端周期发布事件（广播）
 *    - RpcRequest  : 客户端请求（定向到服务端）
 *    - Ack         : 服务端对请求确认（按client_id回送）
 *    - RpcResponse : 服务端返回RPC结果（按client_id回送）
 *
 * 3) 行为约定
 *    - Field：每500ms更新一次，广播给所有客户端
 *    - Event：每2秒触发一次，广播给所有客户端
 *    - RPC：客户端每100ms发起请求，服务端回ACK+RPC响应
 *
 * 4) 运行方式
 *    Server: ./ipc_service_example --server [duration_sec]
 *    Client: ./ipc_service_example --client <id> [duration_sec]
 *
 * 5) 预期输出（示例）
 *    [Client-1] FIELD id=0 val=4 payload=field=5
 *    [Client-1] EVENT id=0 val=2 payload=event=3
 *    [Client-1] ACK id=10 val=0 payload=ack=10
 *    [Client-1] RPC-RSP id=10 val=20 payload=resp=20
 */

#include "IPCFactory.hpp"
#include "CTypedef.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>
#include <sys/mman.h>
#include <memory>

using namespace lap::core;
using namespace lap::core::ipc;

// ============================================================================
// 配置
// ============================================================================
constexpr const char* kRequestShm = "/svc_req_mpsc";  // MPSC: clients -> server
constexpr const char* kResponseShm = "/svc_rsp_spmc"; // SPMC: server -> clients

constexpr UInt32 kMaxChunks = 128;
constexpr UInt32 kSTMinUs = 10000;  // 10ms
constexpr UInt32 kDurationDefaultSec = 30;

// ============================================================================
// 消息定义
// ============================================================================
enum class MsgType : UInt8
{
    kFieldUpdate = 1,
    kEventNotify = 2,
    kRpcRequest  = 3,
    kRpcResponse = 4,
    kAck         = 5,
};

struct ServiceMessage
{
    UInt32 magic{0xA1B2C3D4};
    UInt8 type{0};
    UInt8 client_id{0xFF};  // 0xFF: broadcast
    UInt16 reserved{0};
    UInt32 request_id{0};
    UInt64 timestamp_us{0};
    Int32 value{0};
    char payload[64]{};
};

constexpr Size kMsgSize = sizeof(ServiceMessage);

static inline UInt64 NowUs()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// ============================================================================
// Server
// ============================================================================
void RunServer(UInt32 duration_sec)
{
    printf("[Server] Starting...\n");

    // Cleanup stale shared memory segments
    shm_unlink(kRequestShm);
    shm_unlink(kResponseShm);

    std::vector<std::unique_ptr<SharedMemoryManager>> shm_managers;

    // Pre-create shared memory segments
    {
        SharedMemoryConfig req_cfg{};
        req_cfg.max_chunks = kMaxChunks;
        req_cfg.chunk_size = kMsgSize;
        req_cfg.ipc_type = IPCType::kMPSC;
        auto req_shm_res = IPCFactory::CreateSHM(kRequestShm, req_cfg);
        if (!req_shm_res) {
            fprintf(stderr, "[Server] Failed to create request shm: %d\n", req_shm_res.Error().Value());
            return;
        }
        shm_managers.push_back(std::move(req_shm_res).Value());
    }
    {
        SharedMemoryConfig rsp_cfg{};
        rsp_cfg.max_chunks = kMaxChunks;
        rsp_cfg.chunk_size = kMsgSize;
        rsp_cfg.ipc_type = IPCType::kSPMC;
        auto rsp_shm_res = IPCFactory::CreateSHM(kResponseShm, rsp_cfg);
        if (!rsp_shm_res) {
            fprintf(stderr, "[Server] Failed to create response shm: %d\n", rsp_shm_res.Error().Value());
            return;
        }
        shm_managers.push_back(std::move(rsp_shm_res).Value());
    }

    PublisherConfig rsp_pub_cfg{};
    rsp_pub_cfg.max_chunks = kMaxChunks;
    rsp_pub_cfg.chunk_size = kMsgSize;
    rsp_pub_cfg.ipc_type = IPCType::kSPMC;
    rsp_pub_cfg.channel_id = kInvalidChannelID;
    rsp_pub_cfg.loan_policy = LoanPolicy::kError;

    auto rsp_pub_res = IPCFactory::CreatePublisher(kResponseShm, rsp_pub_cfg);
    if (!rsp_pub_res.HasValue()) {
        fprintf(stderr, "[Server] Failed to create response publisher: %d\n", rsp_pub_res.Error().Value());
        return;
    }
    auto rsp_pub = std::move(rsp_pub_res).Value();

    SubscriberConfig req_sub_cfg{};
    req_sub_cfg.max_chunks = kMaxChunks;
    req_sub_cfg.chunk_size = kMsgSize;
    req_sub_cfg.ipc_type = IPCType::kMPSC;
    req_sub_cfg.STmin = kSTMinUs;
    req_sub_cfg.empty_policy = SubscribePolicy::kSkip;

    auto req_sub_res = IPCFactory::CreateSubscriber(kRequestShm, req_sub_cfg);
    if (!req_sub_res.HasValue()) {
        fprintf(stderr, "[Server] Failed to create request subscriber: %d\n", req_sub_res.Error().Value());
        return;
    }
    auto req_sub = std::move(req_sub_res).Value();
    req_sub->Connect();

    std::atomic<bool> running{true};

    // Field/Event publisher thread
    std::thread fe_thread([&]() {
        UInt32 field_value = 0;
        UInt32 event_seq = 0;
        auto start = std::chrono::steady_clock::now();

        while (running.load()) {
            auto elapsed = std::chrono::steady_clock::now() - start;
            if (elapsed >= std::chrono::seconds(duration_sec)) break;

            // Field update every 500ms
            ServiceMessage msg{};
            msg.type = static_cast<UInt8>(MsgType::kFieldUpdate);
            msg.client_id = 0xFF;
            msg.timestamp_us = NowUs();
            msg.value = static_cast<Int32>(field_value++);
            std::snprintf(msg.payload, sizeof(msg.payload), "field=%u", field_value);

            rsp_pub->Send([&msg](UInt8, Byte* ptr, Size size) -> Size {
                if (size < kMsgSize) return 0;
                std::memcpy(ptr, &msg, kMsgSize);
                return kMsgSize;
            });

            // Event notify every 2 seconds
            if (field_value % 4 == 0) {
                ServiceMessage ev{};
                ev.type = static_cast<UInt8>(MsgType::kEventNotify);
                ev.client_id = 0xFF;
                ev.timestamp_us = NowUs();
                ev.value = static_cast<Int32>(event_seq++);
                std::snprintf(ev.payload, sizeof(ev.payload), "event=%u", event_seq);

                rsp_pub->Send([&ev](UInt8, Byte* ptr, Size size) -> Size {
                    if (size < kMsgSize) return 0;
                    std::memcpy(ptr, &ev, kMsgSize);
                    return kMsgSize;
                });
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    });

    // Main receive loop
    auto start_time = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start_time < std::chrono::seconds(duration_sec)) {
        auto result = req_sub->Receive([&](UInt8, Byte* data, Size size) -> Size {
            if (size < kMsgSize) return 0;
            ServiceMessage req{};
            std::memcpy(&req, data, kMsgSize);

            if (req.magic != 0xA1B2C3D4) return 0;
            if (req.type != static_cast<UInt8>(MsgType::kRpcRequest)) return size;

            // ACK
            ServiceMessage ack{};
            ack.type = static_cast<UInt8>(MsgType::kAck);
            ack.client_id = req.client_id;
            ack.request_id = req.request_id;
            ack.timestamp_us = NowUs();
            std::snprintf(ack.payload, sizeof(ack.payload), "ack=%u", req.request_id);

            rsp_pub->Send([&ack](UInt8, Byte* ptr, Size sz) -> Size {
                if (sz < kMsgSize) return 0;
                std::memcpy(ptr, &ack, kMsgSize);
                return kMsgSize;
            });

            // RPC response
            ServiceMessage rsp{};
            rsp.type = static_cast<UInt8>(MsgType::kRpcResponse);
            rsp.client_id = req.client_id;
            rsp.request_id = req.request_id;
            rsp.timestamp_us = NowUs();
            rsp.value = req.value * 2;  // 示例逻辑
            std::snprintf(rsp.payload, sizeof(rsp.payload), "resp=%d", rsp.value);

            rsp_pub->Send([&rsp](UInt8, Byte* ptr, Size sz) -> Size {
                if (sz < kMsgSize) return 0;
                std::memcpy(ptr, &rsp, kMsgSize);
                return kMsgSize;
            });

            return kMsgSize;
        });

        if (!result.HasValue() || result.Value() == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    running.store(false);
    if (fe_thread.joinable()) fe_thread.join();

    printf("[Server] Stopped\n");
}

// ============================================================================
// Client
// ============================================================================
void RunClient(UInt8 client_id, UInt32 duration_sec)
{
    printf("[Client-%u] Starting...\n", client_id);

    PublisherConfig req_pub_cfg{};
    req_pub_cfg.max_chunks = kMaxChunks;
    req_pub_cfg.chunk_size = kMsgSize;
    req_pub_cfg.ipc_type = IPCType::kMPSC;
    req_pub_cfg.channel_id = kInvalidChannelID;
    req_pub_cfg.loan_policy = LoanPolicy::kError;

    auto req_pub_res = IPCFactory::CreatePublisher(kRequestShm, req_pub_cfg);
    if (!req_pub_res.HasValue()) {
        fprintf(stderr, "[Client-%u] Failed to create request publisher: %d\n",
                client_id, req_pub_res.Error().Value());
        return;
    }
    auto req_pub = std::move(req_pub_res).Value();

    SubscriberConfig rsp_sub_cfg{};
    rsp_sub_cfg.max_chunks = kMaxChunks;
    rsp_sub_cfg.chunk_size = kMsgSize;
    rsp_sub_cfg.ipc_type = IPCType::kSPMC;
    rsp_sub_cfg.STmin = kSTMinUs;
    rsp_sub_cfg.empty_policy = SubscribePolicy::kSkip;

    auto rsp_sub_res = IPCFactory::CreateSubscriber(kResponseShm, rsp_sub_cfg);
    if (!rsp_sub_res.HasValue()) {
        fprintf(stderr, "[Client-%u] Failed to create response subscriber: %d\n",
                client_id, rsp_sub_res.Error().Value());
        return;
    }
    auto rsp_sub = std::move(rsp_sub_res).Value();
    rsp_sub->Connect();

    std::atomic<bool> running{true};
    std::thread rx_thread([&]() {
        auto start = std::chrono::steady_clock::now();
        while (running.load()) {
            if (std::chrono::steady_clock::now() - start >= std::chrono::seconds(duration_sec)) break;

            auto result = rsp_sub->Receive([&](UInt8, Byte* data, Size size) -> Size {
                if (size < kMsgSize) return 0;
                ServiceMessage msg{};
                std::memcpy(&msg, data, kMsgSize);
                if (msg.magic != 0xA1B2C3D4) return 0;

                if (msg.client_id != 0xFF && msg.client_id != client_id) return size;

                const char* type_str = "UNKNOWN";
                if (msg.type == static_cast<UInt8>(MsgType::kFieldUpdate)) type_str = "FIELD";
                else if (msg.type == static_cast<UInt8>(MsgType::kEventNotify)) type_str = "EVENT";
                else if (msg.type == static_cast<UInt8>(MsgType::kAck)) type_str = "ACK";
                else if (msg.type == static_cast<UInt8>(MsgType::kRpcResponse)) type_str = "RPC-RSP";

                printf("[Client-%u] %s id=%u val=%d payload=%s\n",
                       client_id, type_str, msg.request_id, msg.value, msg.payload);
                return kMsgSize;
            });

            if (!result.HasValue() || result.Value() == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    });

    UInt32 req_id = 0;
    auto start_time = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start_time < std::chrono::seconds(duration_sec)) {
        ServiceMessage req{};
        req.type = static_cast<UInt8>(MsgType::kRpcRequest);
        req.client_id = client_id;
        req.request_id = req_id++;
        req.timestamp_us = NowUs();
        req.value = static_cast<Int32>(req_id);
        std::snprintf(req.payload, sizeof(req.payload), "req=%u", req.request_id);

        req_pub->Send([&req](UInt8, Byte* ptr, Size size) -> Size {
            if (size < kMsgSize) return 0;
            std::memcpy(ptr, &req, kMsgSize);
            return kMsgSize;
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    running.store(false);
    if (rx_thread.joinable()) rx_thread.join();
    printf("[Client-%u] Stopped\n", client_id);
}

// ============================================================================
// Main
// ============================================================================
int main(int argc, char* argv[])
{
    if (argc < 2) {
        printf("Usage: %s --server [duration_sec] | --client <id> [duration_sec]\n", argv[0]);
        return 0;
    }

    UInt32 duration_sec = kDurationDefaultSec;

    if (std::strcmp(argv[1], "--server") == 0) {
        if (argc >= 3) duration_sec = static_cast<UInt32>(std::atoi(argv[2]));
        RunServer(duration_sec);
        return 0;
    }

    if (std::strcmp(argv[1], "--client") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Missing client id\n");
            return 1;
        }
        UInt8 client_id = static_cast<UInt8>(std::atoi(argv[2]));
        if (argc >= 4) duration_sec = static_cast<UInt32>(std::atoi(argv[3]));
        RunClient(client_id, duration_sec);
        return 0;
    }

    printf("Unknown mode. Use --server or --client\n");
    return 0;
}