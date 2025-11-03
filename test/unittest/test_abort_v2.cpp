/**
 * @file        test_abort_v2.cpp
 * @brief       Comprehensive unit tests for AUTOSAR AP compliant Abort functionality
 * @date        2025-11-03
 * @details     Tests all aspects of abort handling, signal handling, and thread safety
 */

#include <gtest/gtest.h>
#include "CAbort.hpp"
#include <fstream>
#include <string>
#include <chrono>
#include <thread>
#include <atomic>
#include <vector>
#include <csignal>

using namespace lap::core;

namespace {
    // Global state for testing
    static std::string g_tmp_path;
    static std::atomic<int> g_handler_call_count{0};
    static std::atomic<bool> g_handler_called{false};
    
    // Reset global state before each test
    void ResetTestState() {
        g_handler_call_count.store(0);
        g_handler_called.store(false);
        SetAbortHandler(nullptr);
    }
    
    // Test abort handler that writes to file
    void TestAbortHandlerFile() noexcept {
        std::ofstream ofs(g_tmp_path, std::ios::app);
        ofs << "abort_handler_called\n";
        ofs.flush();
    }
    
    // Test abort handler that increments counter
    void TestAbortHandlerCounter() noexcept {
        g_handler_call_count.fetch_add(1);
        g_handler_called.store(true);
    }
    
    // Signal handlers for testing
    void TestSignalHandlerSIGTERM() noexcept {
        std::ofstream ofs(g_tmp_path, std::ios::app);
        ofs << "SIGTERM_handler_called\n";
        ofs.flush();
    }
    
    void TestSignalHandlerSIGINT() noexcept {
        std::ofstream ofs(g_tmp_path, std::ios::app);
        ofs << "SIGINT_handler_called\n";
        ofs.flush();
    }
    
    void TestSignalHandlerSIGHUP() noexcept {
        std::ofstream ofs(g_tmp_path, std::ios::app);
        ofs << "SIGHUP_handler_called\n";
        ofs.flush();
    }
} // anonymous namespace

// ============================================================================
// Abort Handler Tests
// ============================================================================

class AbortTestV2 : public ::testing::Test {
protected:
    void SetUp() override {
        ResetTestState();
    }
    
    void TearDown() override {
        ResetTestState();
        if (!g_tmp_path.empty()) {
            std::remove(g_tmp_path.c_str());
        }
    }
};

TEST_F(AbortTestV2, SetAbortHandlerReturnsNull) {
    // Initially, handler should be nullptr
    auto prev = SetAbortHandler(nullptr);
    EXPECT_EQ(prev, nullptr);
}

TEST_F(AbortTestV2, SetAbortHandlerReturnsPrevious) {
    // Set first handler
    auto p1 = SetAbortHandler(&TestAbortHandlerCounter);
    EXPECT_EQ(p1, nullptr);
    
    // Set second handler, should return first
    auto p2 = SetAbortHandler(&TestAbortHandlerFile);
    EXPECT_EQ(p2, &TestAbortHandlerCounter);
    
    // Set nullptr, should return second
    auto p3 = SetAbortHandler(nullptr);
    EXPECT_EQ(p3, &TestAbortHandlerFile);
}

TEST_F(AbortTestV2, GetAbortHandlerReturnsCurrentHandler) {
    EXPECT_EQ(GetAbortHandler(), nullptr);
    
    SetAbortHandler(&TestAbortHandlerCounter);
    EXPECT_EQ(GetAbortHandler(), &TestAbortHandlerCounter);
    
    SetAbortHandler(&TestAbortHandlerFile);
    EXPECT_EQ(GetAbortHandler(), &TestAbortHandlerFile);
    
    SetAbortHandler(nullptr);
    EXPECT_EQ(GetAbortHandler(), nullptr);
}

TEST_F(AbortTestV2, AbortWithTextTriggersHandler) {
    g_tmp_path = std::string("/tmp/lap_abort_v2_test_") + 
                 std::to_string(::getpid()) + "_" + 
                 std::to_string(::time(nullptr)) + ".log";
    
    SetAbortHandler(&TestAbortHandlerFile);
    
    // EXPECT_DEATH will fork, child calls Abort, parent verifies
    EXPECT_DEATH({
        Abort("Critical error occurred");
    }, "");
    
    // Give filesystem time to flush
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    // Verify handler was called
    std::ifstream ifs(g_tmp_path);
    ASSERT_TRUE(ifs.good());
    std::string line;
    std::getline(ifs, line);
    EXPECT_EQ(line, "abort_handler_called");
}

TEST_F(AbortTestV2, AbortWithoutTextWorks) {
    SetAbortHandler(&TestAbortHandlerCounter);
    
    EXPECT_DEATH({
        Abort(); // Overload without text parameter
    }, "");
}

TEST_F(AbortTestV2, AbortWithNullTextWorks) {
    SetAbortHandler(&TestAbortHandlerCounter);
    
    EXPECT_DEATH({
        Abort(nullptr);
    }, "");
}

TEST_F(AbortTestV2, AbortWithoutHandlerTerminates) {
    // No handler installed, should still abort
    EXPECT_DEATH({
        Abort("No handler installed");
    }, "");
}

// ============================================================================
// Thread Safety Tests
// ============================================================================

TEST_F(AbortTestV2, ConcurrentSetAbortHandlerIsSafe) {
    constexpr int NUM_THREADS = 10;
    constexpr int ITERATIONS = 100;
    
    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};
    
    // Handler functions for testing
    void (*handlers[])(void) noexcept = {
        &TestAbortHandlerCounter,
        &TestAbortHandlerFile,
        nullptr
    };
    
    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < ITERATIONS; ++i) {
                auto handler = handlers[i % 3];
                SetAbortHandler(handler);
                std::this_thread::yield();
            }
            success_count.fetch_add(1);
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    EXPECT_EQ(success_count.load(), NUM_THREADS);
}

TEST_F(AbortTestV2, ConcurrentGetAbortHandlerIsSafe) {
    constexpr int NUM_THREADS = 10;
    constexpr int ITERATIONS = 1000;
    
    SetAbortHandler(&TestAbortHandlerCounter);
    
    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};
    
    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&]() {
            for (int i = 0; i < ITERATIONS; ++i) {
                auto handler = GetAbortHandler();
                (void)handler; // Use the value to prevent optimization
                std::this_thread::yield();
            }
            success_count.fetch_add(1);
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    EXPECT_EQ(success_count.load(), NUM_THREADS);
}

// ============================================================================
// Signal Handler Tests
// ============================================================================

class SignalTestV2 : public ::testing::Test {
protected:
    void SetUp() override {
        ResetTestState();
        UnregisterSignalHandlers();
    }
    
    void TearDown() override {
        UnregisterSignalHandlers();
        if (!g_tmp_path.empty()) {
            std::remove(g_tmp_path.c_str());
        }
    }
};

TEST_F(SignalTestV2, RegisterSignalHandlerInstallsHandler) {
    RegisterSignalHandler();
    
    // We can't easily test if the handler is installed without raising signals,
    // but we can at least verify the function doesn't crash
    SUCCEED();
}

TEST_F(SignalTestV2, UnregisterSignalHandlersClearsHandlers) {
    RegisterSignalHandler();
    UnregisterSignalHandlers();
    
    // Verify custom handlers are cleared
    EXPECT_FALSE(IsSignalHandlerRegistered(SIGTERM));
    EXPECT_FALSE(IsSignalHandlerRegistered(SIGINT));
    EXPECT_FALSE(IsSignalHandlerRegistered(SIGHUP));
}

TEST_F(SignalTestV2, SetSignalSIGTERMHandlerWorks) {
    g_tmp_path = std::string("/tmp/lap_signal_v2_test_") + 
                 std::to_string(::getpid()) + "_" + 
                 std::to_string(::time(nullptr)) + ".log";
    
    RegisterSignalHandler();
    auto prev = SetSignalSIGTERMHandler(&TestSignalHandlerSIGTERM);
    EXPECT_EQ(prev, nullptr);
    
    EXPECT_DEATH({
        std::raise(SIGTERM);
    }, "");
    
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    std::ifstream ifs(g_tmp_path);
    ASSERT_TRUE(ifs.good());
    std::string line;
    std::getline(ifs, line);
    EXPECT_EQ(line, "SIGTERM_handler_called");
}

TEST_F(SignalTestV2, SetSignalSIGINTHandlerWorks) {
    g_tmp_path = std::string("/tmp/lap_signal_v2_test_") + 
                 std::to_string(::getpid()) + "_" + 
                 std::to_string(::time(nullptr)) + ".log";
    
    RegisterSignalHandler();
    SetSignalSIGINTHandler(&TestSignalHandlerSIGINT);
    
    EXPECT_DEATH({
        std::raise(SIGINT);
    }, "");
    
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    std::ifstream ifs(g_tmp_path);
    ASSERT_TRUE(ifs.good());
    std::string line;
    std::getline(ifs, line);
    EXPECT_EQ(line, "SIGINT_handler_called");
}

TEST_F(SignalTestV2, SetSignalSIGHUPHandlerWorks) {
    g_tmp_path = std::string("/tmp/lap_signal_v2_test_") + 
                 std::to_string(::getpid()) + "_" + 
                 std::to_string(::time(nullptr)) + ".log";
    
    RegisterSignalHandler();
    SetSignalSIGHUPHandler(&TestSignalHandlerSIGHUP);
    
    EXPECT_DEATH({
        std::raise(SIGHUP);
    }, "");
    
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    std::ifstream ifs(g_tmp_path);
    ASSERT_TRUE(ifs.good());
    std::string line;
    std::getline(ifs, line);
    EXPECT_EQ(line, "SIGHUP_handler_called");
}

TEST_F(SignalTestV2, MultipleSignalHandlersWork) {
    auto prev1 = SetSignalSIGTERMHandler(&TestSignalHandlerSIGTERM);
    auto prev2 = SetSignalSIGINTHandler(&TestSignalHandlerSIGINT);
    
    EXPECT_EQ(prev1, nullptr);
    EXPECT_EQ(prev2, nullptr);
    
    EXPECT_TRUE(IsSignalHandlerRegistered(SIGTERM));
    EXPECT_TRUE(IsSignalHandlerRegistered(SIGINT));
    EXPECT_FALSE(IsSignalHandlerRegistered(SIGHUP));
}

TEST_F(SignalTestV2, SignalHandlerCanBeCleared) {
    SetSignalSIGTERMHandler(&TestSignalHandlerSIGTERM);
    EXPECT_TRUE(IsSignalHandlerRegistered(SIGTERM));
    
    auto prev = SetSignalSIGTERMHandler(nullptr);
    EXPECT_EQ(prev, &TestSignalHandlerSIGTERM);
    EXPECT_FALSE(IsSignalHandlerRegistered(SIGTERM));
}

// ============================================================================
// Utility Function Tests
// ============================================================================

TEST_F(SignalTestV2, GetSignalNameReturnsCorrectNames) {
    EXPECT_STREQ(GetSignalName(SIGTERM), "SIGTERM");
    EXPECT_STREQ(GetSignalName(SIGINT), "SIGINT");
    EXPECT_STREQ(GetSignalName(SIGHUP), "SIGHUP");
    EXPECT_STREQ(GetSignalName(SIGQUIT), "SIGQUIT");
    EXPECT_STREQ(GetSignalName(SIGABRT), "SIGABRT");
    EXPECT_STREQ(GetSignalName(SIGFPE), "SIGFPE");
    EXPECT_STREQ(GetSignalName(SIGILL), "SIGILL");
    EXPECT_STREQ(GetSignalName(SIGSEGV), "SIGSEGV");
}

TEST_F(SignalTestV2, GetSignalNameReturnsUnknownForInvalidSignal) {
    EXPECT_STREQ(GetSignalName(9999), "UNKNOWN");
    EXPECT_STREQ(GetSignalName(-1), "UNKNOWN");
}

TEST_F(SignalTestV2, IsSignalHandlerRegisteredWorks) {
    EXPECT_FALSE(IsSignalHandlerRegistered(SIGTERM));
    
    SetSignalSIGTERMHandler(&TestSignalHandlerSIGTERM);
    EXPECT_TRUE(IsSignalHandlerRegistered(SIGTERM));
    
    SetSignalSIGTERMHandler(nullptr);
    EXPECT_FALSE(IsSignalHandlerRegistered(SIGTERM));
}

// ============================================================================
// Edge Case Tests
// ============================================================================

TEST_F(AbortTestV2, RepeatedSetHandlerWorks) {
    for (int i = 0; i < 100; ++i) {
        if (i % 2 == 0) {
            SetAbortHandler(&TestAbortHandlerCounter);
        } else {
            SetAbortHandler(nullptr);
        }
    }
    SUCCEED();
}

TEST_F(SignalTestV2, RepeatedRegisterUnregisterWorks) {
    for (int i = 0; i < 10; ++i) {
        RegisterSignalHandler();
        UnregisterSignalHandlers();
    }
    SUCCEED();
}

TEST_F(SignalTestV2, AllSignalHandlerSettersWork) {
    void (*handler)(void) noexcept = &TestSignalHandlerSIGTERM;
    
    SetSignalSIGHUPHandler(handler);
    SetSignalSIGINTHandler(handler);
    SetSignalSIGQUITHandler(handler);
    SetSignalSIGABRTHandler(handler);
    SetSignalSIGFPEHandler(handler);
    SetSignalSIGILLHandler(handler);
    SetSignalSIGSEGVHandler(handler);
    SetSignalSIGTERMHandler(handler);
    
    EXPECT_TRUE(IsSignalHandlerRegistered(SIGHUP));
    EXPECT_TRUE(IsSignalHandlerRegistered(SIGINT));
    EXPECT_TRUE(IsSignalHandlerRegistered(SIGQUIT));
    EXPECT_TRUE(IsSignalHandlerRegistered(SIGABRT));
    EXPECT_TRUE(IsSignalHandlerRegistered(SIGFPE));
    EXPECT_TRUE(IsSignalHandlerRegistered(SIGILL));
    EXPECT_TRUE(IsSignalHandlerRegistered(SIGSEGV));
    EXPECT_TRUE(IsSignalHandlerRegistered(SIGTERM));
}

// ============================================================================
// AUTOSAR Compliance Tests
// ============================================================================

class AutosarComplianceTestV2 : public ::testing::Test {
protected:
    void SetUp() override {
        ResetTestState();
    }
    
    void TearDown() override {
        ResetTestState();
    }
};

TEST_F(AutosarComplianceTestV2, AbortIsNoReturn) {
    // This test verifies [[noreturn]] attribute behavior
    // If Abort returns, the test will fail
    EXPECT_DEATH({
        Abort("Test abort");
        // This line should never execute
        FAIL() << "Abort returned!";
    }, "");
}

TEST_F(AutosarComplianceTestV2, AbortIsNoexcept) {
    // Verify function signature is noexcept
    EXPECT_TRUE(noexcept(Abort("test")));
    EXPECT_TRUE(noexcept(Abort()));
}

TEST_F(AutosarComplianceTestV2, SetAbortHandlerIsNoexcept) {
    EXPECT_TRUE(noexcept(SetAbortHandler(nullptr)));
    EXPECT_TRUE(noexcept(GetAbortHandler()));
}

TEST_F(AutosarComplianceTestV2, SignalFunctionsAreNoexcept) {
    EXPECT_TRUE(noexcept(RegisterSignalHandler()));
    EXPECT_TRUE(noexcept(UnregisterSignalHandlers()));
    EXPECT_TRUE(noexcept(SetSignalSIGTERMHandler(nullptr)));
    EXPECT_TRUE(noexcept(GetSignalName(SIGTERM)));
    EXPECT_TRUE(noexcept(IsSignalHandlerRegistered(SIGTERM)));
}

TEST_F(AutosarComplianceTestV2, AbortHandlerSignatureMatches) {
    // Verify AbortHandler type matches prototype
    AbortHandler handler = &AbortHandlerPrototype;
    EXPECT_NE(handler, nullptr);
}
