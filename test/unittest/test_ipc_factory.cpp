/**
 * @file        test_ipc_factory.cpp
 * @brief       Unit tests for IPCFactory
 */

#include <gtest/gtest.h>
#include "IPCFactory.hpp"
#include "CInitialization.hpp"
#include <thread>
#include <chrono>
#include <unistd.h>

using namespace lap::core;
using namespace lap::core::ipc;

class IPCFactoryTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        Initialize();
        shm_path_ = "/test_factory_" + std::to_string(::getpid());
    }

    void TearDown() override
    {
        shm_unlink(shm_path_.c_str());
        Deinitialize();
    }

    String shm_path_;
};

TEST_F(IPCFactoryTest, CreateAndDestroySHM)
{
    SharedMemoryConfig shm_config{};
    shm_config.max_chunks = 32;
    shm_config.chunk_size = 256;
    shm_config.ipc_type = IPCType::kSPMC;

    auto shm_result = IPCFactory::CreateSHM(shm_path_, shm_config);
    ASSERT_TRUE(shm_result.HasValue());

    auto shm = std::move(shm_result).Value();
    EXPECT_NE(shm.get(), nullptr);

    IPCFactory::DestroySHM(shm);
    EXPECT_EQ(shm.get(), nullptr);
}

TEST_F(IPCFactoryTest, CreatePublisherSubscriber)
{
    SharedMemoryConfig shm_config{};
    shm_config.max_chunks = 64;
    shm_config.chunk_size = 256;
    shm_config.ipc_type = IPCType::kSPMC;

    auto shm_result = IPCFactory::CreateSHM(shm_path_, shm_config);
    ASSERT_TRUE(shm_result.HasValue());
    auto shm = std::move(shm_result).Value();

    PublisherConfig pub_config{};
    pub_config.max_chunks = 64;
    pub_config.chunk_size = 256;
    pub_config.ipc_type = IPCType::kSPMC;

    SubscriberConfig sub_config{};
    sub_config.max_chunks = 64;
    sub_config.chunk_size = 256;
    sub_config.ipc_type = IPCType::kSPMC;
    sub_config.empty_policy = SubscribePolicy::kSkip;

    auto pub_result = IPCFactory::CreatePublisher(shm_path_, pub_config);
    ASSERT_TRUE(pub_result.HasValue());
    auto publisher = std::move(pub_result).Value();

    auto sub_result = IPCFactory::CreateSubscriber(shm_path_, sub_config);
    ASSERT_TRUE(sub_result.HasValue());
    auto subscriber = std::move(sub_result).Value();

    subscriber->Connect();

    UInt32 value = 1234;
    auto send_result = publisher->Send(reinterpret_cast<Byte*>(&value), sizeof(value));
    ASSERT_TRUE(send_result.HasValue());

    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    bool received = false;
    UInt32 read_value = 0;

    for (int attempt = 0; attempt < 20 && !received; ++attempt) {
        auto recv_result = subscriber->Receive(SubscribePolicy::kSkip);
        if (recv_result.HasValue()) {
            auto samples = std::move(recv_result).Value();
            if (!samples.empty()) {
                samples.front().Read(reinterpret_cast<Byte*>(&read_value), sizeof(read_value));
                received = true;
            }
        }
        if (!received) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    ASSERT_TRUE(received);
    EXPECT_EQ(read_value, value);

    subscriber->Disconnect();
    IPCFactory::DestroySubscriber(subscriber);
    IPCFactory::DestroyPublisher(publisher);
    IPCFactory::DestroySHM(shm);

    EXPECT_EQ(subscriber.get(), nullptr);
    EXPECT_EQ(publisher.get(), nullptr);
    EXPECT_EQ(shm.get(), nullptr);
}
