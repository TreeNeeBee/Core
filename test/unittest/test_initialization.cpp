/**
 * @file test_initialization.cpp
 * @brief Unit tests for lap::core::Initialize and lap::core::Deinitialize
 * @date 2025-11-12
 * 
 * NOTE: This test suite is prefixed with 'A' to ensure it runs FIRST,
 * before any other tests that might depend on the initialization system.
 */

#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>
#include "CInitialization.hpp"
#include "CCoreErrorDomain.hpp"

using namespace lap::core;

// Test suite prefixed with 'A' to guarantee execution order (alphabetical)
class AInitializationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Ensure clean state before each test
        // Note: This is tricky since we can't easily reset the singleton state
        // For comprehensive testing, we'd need to run each test in a separate process
    }

    void TearDown() override {
        // Attempt to deinitialize after each test
        // Ignore errors as some tests may leave the system in different states
        Deinitialize();
    }
};

// Test basic initialization
TEST_F(AInitializationTest, BasicInitialization) {
    auto result = Initialize();
    ASSERT_TRUE(result.HasValue()) << "Initialize() should succeed";
}

// Test basic deinitialization
TEST_F(AInitializationTest, BasicDeinitialization) {
    auto initResult = Initialize();
    ASSERT_TRUE(initResult.HasValue());

    auto deinitResult = Deinitialize();
    ASSERT_TRUE(deinitResult.HasValue()) << "Deinitialize() should succeed";
}

// Test double initialization (should return error)
TEST_F(AInitializationTest, DoubleInitialization) {
    auto result1 = Initialize();
    ASSERT_TRUE(result1.HasValue());

    auto result2 = Initialize();
    ASSERT_FALSE(result2.HasValue()) << "Second Initialize() should fail";
    EXPECT_EQ(result2.Error().Value(), 
              static_cast<ErrorDomain::CodeType>(CoreErrc::kAlreadyInitialized))
        << "Error should be kAlreadyInitialized";
}

// Test initialization with command line arguments
TEST_F(AInitializationTest, InitializationWithArguments) {
    int argc = 3;
    const char* argv_data[] = {"test_program", "--option1", "--option2", nullptr};
    char** argv = const_cast<char**>(argv_data);

    auto result = Initialize(argc, argv);
    ASSERT_TRUE(result.HasValue()) << "Initialize(argc, argv) should succeed";
}

// Test deinitialization without initialization
TEST_F(AInitializationTest, DeinitializeWithoutInitialize) {
    // First deinitialize any existing state
    Deinitialize();
    
    // Now try to deinitialize again
    auto result = Deinitialize();
    ASSERT_FALSE(result.HasValue()) << "Deinitialize() without Initialize() should fail";
    EXPECT_EQ(result.Error().Value(),
              static_cast<ErrorDomain::CodeType>(CoreErrc::kNotInitialized))
        << "Error should be kNotInitialized";
}

// Test initialization -> deinitialization -> re-initialization cycle
TEST_F(AInitializationTest, InitDeinitReinitCycle) {
    // First cycle
    auto init1 = Initialize();
    ASSERT_TRUE(init1.HasValue());

    auto deinit1 = Deinitialize();
    ASSERT_TRUE(deinit1.HasValue());

    // Second cycle - should work after proper deinitialization
    auto init2 = Initialize();
    EXPECT_TRUE(init2.HasValue()) << "Re-initialization should succeed after Deinitialize()";

    auto deinit2 = Deinitialize();
    EXPECT_TRUE(deinit2.HasValue());
}

// Test error handling
TEST_F(AInitializationTest, ErrorHandling) {
    auto result = Initialize();
    ASSERT_TRUE(result.HasValue());

    // Try to initialize again
    auto errorResult = Initialize();
    ASSERT_FALSE(errorResult.HasValue());

    // Verify error code can be retrieved
    auto errorCode = errorResult.Error();
    EXPECT_EQ(errorCode.Domain().Id(), GetCoreErrorDomain().Id());
    
    // Verify error message is available
    auto message = errorCode.Message();
    EXPECT_FALSE(String(message.data()).empty());
}

// Test thread safety (basic check)
TEST_F(AInitializationTest, ThreadSafety) {
    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};
    std::atomic<int> already_init_count{0};

    // Launch multiple threads trying to initialize
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back([&]() {
            auto result = Initialize();
            if (result.HasValue()) {
                success_count++;
            } else if (result.Error().Value() == 
                       static_cast<ErrorDomain::CodeType>(CoreErrc::kAlreadyInitialized)) {
                already_init_count++;
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    // Only one thread should succeed, others should get kAlreadyInitialized
    EXPECT_EQ(success_count.load(), 1) << "Exactly one thread should succeed";
    EXPECT_EQ(already_init_count.load(), 9) << "Nine threads should get kAlreadyInitialized";
}
