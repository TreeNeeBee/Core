#include <gtest/gtest.h>
#include "CSync.hpp"
#include <thread>
#include <chrono>

using namespace lap::core;

TEST(EventTest, SignalAndWait) {
    Event ev;
    bool signaled = false;

    std::thread t([&]{
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        ev.signal();
        signaled = true;
    });

    ev.wait();
    t.join();
    ASSERT_TRUE(signaled);
}

TEST(SemaphoreTest, AcquireRelease) {
    Semaphore sem(0);

    std::thread t([&]{
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        sem.release();
    });

    sem.acquire();
    t.join();
    ASSERT_TRUE(true); // reached after acquire
}

// Note: main() is provided by another test translation unit or gtest_main; avoid duplicate definitions here.
