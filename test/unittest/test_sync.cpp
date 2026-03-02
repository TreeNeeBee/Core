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
        ev.Signal();
        signaled = true;
    });

    ev.Wait();
    t.join();
    ASSERT_TRUE(signaled);
}

TEST(SemaphoreTest, AcquireRelease) {
    Semaphore sem(0);

    std::thread t([&]{
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        sem.Release();
    });

    sem.Acquire();
    t.join();
    ASSERT_TRUE(true); // reached after acquire
}

TEST(ScopedLockTest, SingleMutex) {
    Mutex mtx;
    int counter = 0;

    {
        ScopedLock< Mutex > lock( mtx );
        ++counter;
    }
    ASSERT_EQ( counter, 1 );
}

TEST(ScopedLockTest, MultiMutex) {
    Mutex mtx1;
    Mutex mtx2;
    int counter = 0;

    {
        ScopedLock< Mutex, Mutex > lock( mtx1, mtx2 );
        ++counter;
    }
    ASSERT_EQ( counter, 1 );
}

TEST(LockGuardTest, BasicUsage) {
    Mutex mtx;
    int counter = 0;

    {
        LockGuard lock( mtx );
        ++counter;
    }
    ASSERT_EQ( counter, 1 );
}

TEST(ReadWriteLockTest, BasicUsage) {
    RWLock rwLock;
    int sharedData = 0;

    {
        WriteLockGuard wlock( rwLock );
        sharedData = 42;
    }

    {
        ReadLockGuard rlock( rwLock );
        ASSERT_EQ( sharedData, 42 );
    }
}

// Note: main() is provided by another test translation unit or gtest_main; avoid duplicate definitions here.
