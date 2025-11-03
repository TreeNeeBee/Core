#include <gtest/gtest.h>
#include <type_traits>
#include <utility>

#include "CSync.hpp"
#include "CTime.hpp"
#include "CPath.hpp"
#include "CInstanceSpecifier.hpp"
#include "CMemory.hpp"
#include "CResult.hpp"
#include "CErrorCode.hpp"

using namespace lap::core;

// Compile-time checks for nothrow where reasonable.
static_assert(noexcept(Time::getCurrentTime()), "Time::getCurrentTime must be noexcept");

static_assert(noexcept(std::declval<Mutex&>().lock()), "Mutex::lock should be noexcept");
static_assert(noexcept(std::declval<Mutex&>().try_lock()), "Mutex::try_lock should be noexcept");
static_assert(noexcept(std::declval<Mutex&>().unlock()), "Mutex::unlock should be noexcept");
static_assert(noexcept(std::declval<RecursiveMutex&>().lock()), "RecursiveMutex::lock should be noexcept");
static_assert(noexcept(std::declval<RecursiveMutex&>().try_lock()), "RecursiveMutex::try_lock should be noexcept");
static_assert(noexcept(std::declval<RecursiveMutex&>().unlock()), "RecursiveMutex::unlock should be noexcept");

static_assert(noexcept(std::declval<Event&>().signal()), "Event::signal should be noexcept");
static_assert(noexcept(std::declval<Event&>().reset()), "Event::reset should be noexcept");

static_assert(noexcept(std::declval<Semaphore&>().try_acquire()), "Semaphore::try_acquire should be noexcept");
static_assert(noexcept(std::declval<Semaphore&>().release()), "Semaphore::release should be noexcept");

// Path helpers often marked noexcept in this code base
// Path helper noexcept checks avoided because implicit conversions to StringView
// may not be noexcept in all boost implementations; leave as a runtime smoke instead.

// Result types (some constructors and destructors)
static_assert(std::is_nothrow_destructible<Result<int>>::value, "Result<int> must be nothrow destructible");
static_assert(std::is_nothrow_destructible<Result<void>>::value, "Result<void> must be nothrow destructible");

// ErrorCode small accessors
static_assert(noexcept(std::declval<ErrorCode const&>().Value()), "ErrorCode::Value should be noexcept");
static_assert(noexcept(std::declval<ErrorCode const&>().Domain()), "ErrorCode::Domain should be noexcept");

// Memory-related inline getters now noexcept
static_assert(noexcept(std::declval<MemManager&>().hasMemChecker()), "MemManager::hasMemChecker should be noexcept");
static_assert(noexcept(std::declval<MemChecker&>().getCurrentAllocSize()), "MemChecker::getCurrentAllocSize should be noexcept");
static_assert(noexcept(std::declval<MemChecker&>().getCurrentAllocCount()), "MemChecker::getCurrentAllocCount should be noexcept");

TEST(NoexceptExtended, Sanity) {
    // runtime smoke to ensure headers compile into the test binary
    Mutex m;
    EXPECT_NO_THROW(m.lock());
    m.unlock();

    RecursiveMutex rm;
    EXPECT_NO_THROW(rm.lock());
    rm.unlock();

    Event e;
    e.reset();
    e.signal();

    Semaphore s(1);
    EXPECT_TRUE(s.try_acquire());
    s.release();
}
