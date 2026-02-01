/**
 * @file        property_board_example.cpp
 * @brief       Property看板示例 - 只读共享内存K-V数据库
 * @details     - Property数据库：共享内存只读K-V（外部只读）
 *              - MPSC请求通道：外部 -> 服务端修改指令
 *              - SPMC ACK通道：服务端 -> 外部确认回复
 *
 * Usage:
 *   Server: ./property_board_example --server [duration_sec]
 *   Client: ./property_board_example --client <id> <key> <value> [duration_sec]
 */

#include "IPCFactory.hpp"
#include "CTypedef.hpp"
#include "CCoreErrorDomain.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

using namespace lap::core;
using namespace lap::core::ipc;

// ============================================================================
// 配置
// ============================================================================
constexpr const char* kPropShm = "/property_db";      // 只读K-V数据库
constexpr const char* kReqShm  = "/property_req_mpsc"; // 修改请求
constexpr const char* kAckShm  = "/property_ack_spmc"; // ACK回复

constexpr UInt32 kMaxChunks = 128;
constexpr UInt32 kSTMinUs = 10000;  // 10ms
constexpr UInt32 kDurationDefaultSec = 30;
constexpr UInt32 kRetryMax = 100;
constexpr UInt32 kRetrySleepMs = 50;

// ============================================================================
// Property DB
// ============================================================================
constexpr UInt32 kMaxProperties = 64;
constexpr UInt32 kKeyMaxLen = 32;

struct PropertyEntry
{
    char key[kKeyMaxLen]{};
    Int32 value{0};
    UInt8 in_use{0};
    UInt8 reserved[3]{};
};

struct PropertyDB
{
    UInt32 magic{0x50424F44}; // 'PBOD'
    UInt32 version{1};
    UInt32 count{0};
    PropertyEntry entries[kMaxProperties]{};
};

// ============================================================================
// 消息定义
// ============================================================================
enum class ReqType : UInt8
{
    kSet = 1,
};

struct PropertyRequest
{
    UInt32 magic{0xABCD1234};
    UInt8 type{0};
    UInt8 client_id{0xFF};
    UInt16 reserved{0};
    UInt32 request_id{0};
    char key[kKeyMaxLen]{};
    Int32 value{0};
};

struct PropertyAck
{
    UInt32 magic{0xDCBA4321};
    UInt8 status{0}; // 0=ok, 1=not_found, 2=full
    UInt8 client_id{0xFF};
    UInt16 reserved{0};
    UInt32 request_id{0};
    char key[kKeyMaxLen]{};
    Int32 value{0};
};

constexpr Size kReqSize = sizeof(PropertyRequest);
constexpr Size kAckSize = sizeof(PropertyAck);

// ============================================================================
// Utils
// ============================================================================
PropertyDB* MapPropertyDB(int flags, int prot)
{
    int fd = shm_open(kPropShm, flags, 0666);
    if (fd < 0) return nullptr;

    if (flags & O_CREAT) {
        ftruncate(fd, sizeof(PropertyDB));
    }

    void* addr = mmap(nullptr, sizeof(PropertyDB), prot, MAP_SHARED, fd, 0);
    close(fd);
    if (addr == MAP_FAILED) return nullptr;
    return static_cast<PropertyDB*>(addr);
}

bool IsShmNotReady(const ErrorCode& err)
{
    return err == CoreErrc::kIPCShmNotFound || err == CoreErrc::kIPCShmInvalidMagic;
}

int FindEntry(PropertyDB* db, const char* key)
{
    for (UInt32 i = 0; i < kMaxProperties; ++i) {
        if (db->entries[i].in_use && std::strncmp(db->entries[i].key, key, kKeyMaxLen) == 0) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int FindEmpty(PropertyDB* db)
{
    for (UInt32 i = 0; i < kMaxProperties; ++i) {
        if (!db->entries[i].in_use) return static_cast<int>(i);
    }
    return -1;
}

// ============================================================================
// Server
// ============================================================================
void RunServer(UInt32 duration_sec)
{
    printf("[Server] Starting property board...\n");

    shm_unlink(kPropShm);
    shm_unlink(kReqShm);
    shm_unlink(kAckShm);

    // Create property DB
    PropertyDB* db = MapPropertyDB(O_CREAT | O_RDWR, PROT_READ | PROT_WRITE);
    if (!db) {
        fprintf(stderr, "[Server] Failed to create property DB\n");
        return;
    }
    *db = PropertyDB{};
    db->magic = 0x50424F44;
    db->version = 1;

    std::vector<std::unique_ptr<SharedMemoryManager>> shm_managers;

    // Request channel (MPSC)
    {
        SharedMemoryConfig cfg{};
        cfg.max_chunks = kMaxChunks;
        cfg.chunk_size = kReqSize;
        cfg.ipc_type = IPCType::kMPSC;
        auto shm_res = IPCFactory::CreateSHM(kReqShm, cfg);
        if (!shm_res) {
            fprintf(stderr, "[Server] Failed to create req shm: %d\n", shm_res.Error().Value());
            return;
        }
        shm_managers.push_back(std::move(shm_res).Value());
    }

    // Ack channel (SPMC)
    {
        SharedMemoryConfig cfg{};
        cfg.max_chunks = kMaxChunks;
        cfg.chunk_size = kAckSize;
        cfg.ipc_type = IPCType::kSPMC;
        auto shm_res = IPCFactory::CreateSHM(kAckShm, cfg);
        if (!shm_res) {
            fprintf(stderr, "[Server] Failed to create ack shm: %d\n", shm_res.Error().Value());
            return;
        }
        shm_managers.push_back(std::move(shm_res).Value());
    }

    PublisherConfig ack_pub_cfg{};
    ack_pub_cfg.max_chunks = kMaxChunks;
    ack_pub_cfg.chunk_size = kAckSize;
    ack_pub_cfg.ipc_type = IPCType::kSPMC;
    ack_pub_cfg.channel_id = kInvalidChannelID;
    ack_pub_cfg.loan_policy = LoanPolicy::kError;

    auto ack_pub_res = IPCFactory::CreatePublisher(kAckShm, ack_pub_cfg);
    if (!ack_pub_res.HasValue()) {
        fprintf(stderr, "[Server] Failed to create ack publisher: %d\n", ack_pub_res.Error().Value());
        return;
    }
    auto ack_pub = std::move(ack_pub_res).Value();

    SubscriberConfig req_sub_cfg{};
    req_sub_cfg.max_chunks = kMaxChunks;
    req_sub_cfg.chunk_size = kReqSize;
    req_sub_cfg.ipc_type = IPCType::kMPSC;
    req_sub_cfg.STmin = kSTMinUs;
    req_sub_cfg.empty_policy = SubscribePolicy::kSkip;

    auto req_sub_res = IPCFactory::CreateSubscriber(kReqShm, req_sub_cfg);
    if (!req_sub_res.HasValue()) {
        fprintf(stderr, "[Server] Failed to create req subscriber: %d\n", req_sub_res.Error().Value());
        return;
    }
    auto req_sub = std::move(req_sub_res).Value();
    req_sub->Connect();

    auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < std::chrono::seconds(duration_sec)) {
        auto result = req_sub->Receive([&](UInt8, Byte* data, Size size) -> Size {
            if (size < kReqSize) return 0;
            PropertyRequest req{};
            std::memcpy(&req, data, kReqSize);
            if (req.magic != 0xABCD1234 || req.type != static_cast<UInt8>(ReqType::kSet)) return 0;

            PropertyAck ack{};
            ack.magic = 0xDCBA4321;
            ack.client_id = req.client_id;
            ack.request_id = req.request_id;
            std::memcpy(ack.key, req.key, kKeyMaxLen - 1);
            ack.key[kKeyMaxLen - 1] = '\0';

            int idx = FindEntry(db, req.key);
            if (idx < 0) {
                idx = FindEmpty(db);
                if (idx < 0) {
                    ack.status = 2;
                } else {
                    std::memcpy(db->entries[idx].key, req.key, kKeyMaxLen - 1);
                    db->entries[idx].key[kKeyMaxLen - 1] = '\0';
                    db->entries[idx].in_use = 1;
                    db->entries[idx].value = req.value;
                    db->count++;
                    ack.status = 0;
                }
            } else {
                db->entries[idx].value = req.value;
                ack.status = 0;
            }

            ack.value = req.value;

            ack_pub->Send([&](UInt8, Byte* ptr, Size sz) -> Size {
                if (sz < kAckSize) return 0;
                std::memcpy(ptr, &ack, kAckSize);
                return kAckSize;
            });

            return kReqSize;
        });

        if (!result.HasValue() || result.Value() == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    munmap(db, sizeof(PropertyDB));
    printf("[Server] Stopped\n");
}

// ============================================================================
// Client
// ============================================================================
void RunClient(UInt8 client_id, const char* key, Int32 value, UInt32 duration_sec)
{
    printf("[Client-%u] Starting...\n", client_id);

    PropertyDB* db = nullptr;
    for (UInt32 i = 0; i < kRetryMax; ++i) {
        db = MapPropertyDB(O_RDONLY, PROT_READ);
        if (db) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(kRetrySleepMs));
    }
    if (!db) {
        fprintf(stderr, "[Client-%u] Failed to open property DB\n", client_id);
        return;
    }

    PublisherConfig req_pub_cfg{};
    req_pub_cfg.max_chunks = kMaxChunks;
    req_pub_cfg.chunk_size = kReqSize;
    req_pub_cfg.ipc_type = IPCType::kMPSC;
    req_pub_cfg.channel_id = kInvalidChannelID;
    req_pub_cfg.loan_policy = LoanPolicy::kError;

    UniqueHandle<Publisher> req_pub;
    Int32 req_pub_err = 0;
    for (UInt32 i = 0; i < kRetryMax; ++i) {
        auto res = IPCFactory::CreatePublisher(kReqShm, req_pub_cfg);
        if (res.HasValue()) {
            req_pub = std::move(res).Value();
            break;
        }
        req_pub_err = res.Error().Value();
        if (!IsShmNotReady(res.Error())) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(kRetrySleepMs));
    }
    if (!req_pub) {
        fprintf(stderr, "[Client-%u] Failed to create req publisher: %d\n",
                client_id, req_pub_err);
        munmap(db, sizeof(PropertyDB));
        return;
    }

    SubscriberConfig ack_sub_cfg{};
    ack_sub_cfg.max_chunks = kMaxChunks;
    ack_sub_cfg.chunk_size = kAckSize;
    ack_sub_cfg.ipc_type = IPCType::kSPMC;
    ack_sub_cfg.STmin = kSTMinUs;
    ack_sub_cfg.empty_policy = SubscribePolicy::kSkip;

    UniqueHandle<Subscriber> ack_sub;
    Int32 ack_sub_err = 0;
    for (UInt32 i = 0; i < kRetryMax; ++i) {
        auto res = IPCFactory::CreateSubscriber(kAckShm, ack_sub_cfg);
        if (res.HasValue()) {
            ack_sub = std::move(res).Value();
            break;
        }
        ack_sub_err = res.Error().Value();
        if (!IsShmNotReady(res.Error())) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(kRetrySleepMs));
    }
    if (!ack_sub) {
        fprintf(stderr, "[Client-%u] Failed to create ack subscriber: %d\n",
                client_id, ack_sub_err);
        munmap(db, sizeof(PropertyDB));
        return;
    }
    ack_sub->Connect();

    UInt32 req_id = 1;
    auto start = std::chrono::steady_clock::now();

    PropertyRequest req{};
    req.magic = 0xABCD1234;
    req.type = static_cast<UInt8>(ReqType::kSet);
    req.client_id = client_id;
    req.request_id = req_id;
    std::memcpy(req.key, key, kKeyMaxLen - 1);
    req.key[kKeyMaxLen - 1] = '\0';
    req.value = value;

    bool got_ack = false;
    auto last_send = std::chrono::steady_clock::now() - std::chrono::milliseconds(200);

    while (!got_ack && std::chrono::steady_clock::now() - start < std::chrono::seconds(duration_sec)) {
        if (std::chrono::steady_clock::now() - last_send >= std::chrono::milliseconds(100)) {
            auto send_res = req_pub->Send([&](UInt8, Byte* ptr, Size sz) -> Size {
                if (sz < kReqSize) return 0;
                std::memcpy(ptr, &req, kReqSize);
                return kReqSize;
            });
            if (!send_res.HasValue()) {
                fprintf(stderr, "[Client-%u] Send failed: %d\n", client_id, send_res.Error().Value());
            }
            last_send = std::chrono::steady_clock::now();
        }

        auto res = ack_sub->Receive([&](UInt8, Byte* data, Size size) -> Size {
            if (size < kAckSize) return 0;
            PropertyAck ack{};
            std::memcpy(&ack, data, kAckSize);
            if (ack.magic != 0xDCBA4321) return 0;
            if (ack.client_id != client_id) return kAckSize;
            if (ack.request_id != req_id) return kAckSize;

            printf("[Client-%u] ACK key=%s val=%d status=%u\n",
                   client_id, ack.key, ack.value, ack.status);
            got_ack = true;
            return kAckSize;
        });

        if (!res.HasValue() || res.Value() == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    // Read property snapshot
    int idx = FindEntry(db, key);
    if (idx >= 0) {
        printf("[Client-%u] READ key=%s val=%d\n", client_id, db->entries[idx].key, db->entries[idx].value);
    } else {
        printf("[Client-%u] READ key=%s not found\n", client_id, key);
    }

    munmap(db, sizeof(PropertyDB));
    printf("[Client-%u] Stopped\n", client_id);
}

// ============================================================================
// Main
// ============================================================================
int main(int argc, char* argv[])
{
    if (argc < 2) {
        printf("Usage: %s --server [duration_sec] | --client <id> <key> <value> [duration_sec]\n", argv[0]);
        return 0;
    }

    UInt32 duration_sec = kDurationDefaultSec;

    if (std::strcmp(argv[1], "--server") == 0) {
        if (argc >= 3) duration_sec = static_cast<UInt32>(std::atoi(argv[2]));
        RunServer(duration_sec);
        return 0;
    }

    if (std::strcmp(argv[1], "--client") == 0) {
        if (argc < 5) {
            fprintf(stderr, "Missing args: --client <id> <key> <value> [duration_sec]\n");
            return 1;
        }
        UInt8 client_id = static_cast<UInt8>(std::atoi(argv[2]));
        const char* key = argv[3];
        Int32 value = static_cast<Int32>(std::atoi(argv[4]));
        if (argc >= 6) duration_sec = static_cast<UInt32>(std::atoi(argv[5]));
        RunClient(client_id, key, value, duration_sec);
        return 0;
    }

    printf("Unknown mode. Use --server or --client\n");
    return 0;
}