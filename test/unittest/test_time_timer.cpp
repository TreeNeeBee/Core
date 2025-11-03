#include <gtest/gtest.h>
#include <atomic>
#include <chrono>

#include "CTime.hpp"
#include "CTimer.hpp"

using namespace lap::core;

TEST(TimeBasics, NowAndConversion) {
    auto s1 = Time::nowSteady();
    auto s2 = Time::nowSteady();
    EXPECT_LE(s1, s2);

    auto sys_now = Time::nowSystem();
    auto ms = Time::toUnixMillis(sys_now);
    auto back = Time::fromUnixMillis(ms);
    // allow small truncation differences within 1 second boundary
    auto diff = back - sys_now;
    auto ad = diff >= decltype(diff)::zero() ? diff : -diff;
    EXPECT_LT(Time::toMillis(ad), 1000u);
}

TEST(Timer, OneShotSteady) {
    SteadyTimer t;
    std::atomic<int> fired{0};
    t.startAfter(std::chrono::milliseconds(30), [&]{ fired.fetch_add(1); });

    auto deadline = SteadyClock::now() + std::chrono::milliseconds(500);
    while (SteadyClock::now() < deadline && fired.load() == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    EXPECT_EQ(fired.load(), 1);
    t.stop();
}

TEST(Timer, PeriodicSystem) {
    SystemTimer t;
    std::atomic<int> cnt{0};
    t.startAfter(std::chrono::milliseconds(10), [&]{
        int c = ++cnt;
        if (c >= 3) {
            // Safe to call stop within callback
            t.stop();
        }
    }, std::chrono::milliseconds(10));

    auto deadline = SystemClock::now() + std::chrono::milliseconds(1000);
    while (SystemClock::now() < deadline && cnt.load() < 3) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    EXPECT_GE(cnt.load(), 3);
    t.stop();
}
