#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include "CFuture.hpp"
#include "CPromise.hpp"

using namespace lap::core;

TEST(FutureBasicTest, WaitForAndGetResult) {
    Promise<int> promise;
    auto future = promise.get_future();

    // Initially not ready
    EXPECT_FALSE(future.is_ready());

    // Launch a thread that sets the value after a short sleep
    std::thread setter([&promise]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        promise.set_value(42);
    });

    // Wait for up to 200ms
    auto status = future.wait_for(std::chrono::milliseconds(200));
    EXPECT_EQ(status, future_status::ready);
    EXPECT_TRUE(future.is_ready());

    auto result = future.GetResult();
    EXPECT_TRUE(result.HasValue());
    EXPECT_EQ(result.Value(), 42);

    setter.join();
}
