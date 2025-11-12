/**
 * @file test_functional_classes.cpp
 * @brief Test IMP_OPERATOR_NEW for Result, Error, Future, Promise, File, Path, Exception, ErrorDomain
 * Note: SyncObject (Mutex) is excluded as it creates circular dependency with CMemory.hpp
 */

#include "CMemory.hpp"
#include "CResult.hpp"
#include "CErrorCode.hpp"
#include "CCoreErrorDomain.hpp"
#include "CFuture.hpp"
#include "CPromise.hpp"
#include "CFile.hpp"
#include "CPath.hpp"
#include "CException.hpp"
#include "CFutureErrorDomain.hpp"
#include "CInitialization.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <fcntl.h>
#include <vector>

using namespace lap::core;

// 打印内存统计
void printMemoryStats(const char* label) {
    auto stats = MemoryManager::getInstance()->getMemoryStats();
    std::cout << "[" << label << "] Memory Statistics:\n"
              << "  Current Alloc Size: " << stats.currentAllocSize << " bytes\n"
              << "  Current Alloc Count: " << stats.currentAllocCount << "\n"
              << "  Pool Memory: " << stats.totalPoolMemory << " bytes\n"
              << "  Pool Count: " << stats.poolCount << "\n" << std::endl;
}

// 测试 Result 类
void testResult() {
    std::cout << "=== Test 1: Result Class ===" << std::endl;
    
    // 创建 Result 对象
    auto* r1 = new Result<int>(42);
    auto* r2 = new Result<String>(String("Hello"));
    auto* r3 = new Result<int>(ErrorCode(1, GetCoreErrorDomain()));
    
    std::cout << "Created 3 Result objects\n";
    std::cout << "r1 has value: " << r1->HasValue() << ", value: " << r1->Value() << "\n";
    std::cout << "r2 has value: " << r2->HasValue() << ", value: " << r2->Value() << "\n";
    std::cout << "r3 has value: " << r3->HasValue() << "\n";
    
    printMemoryStats("After Result Creation");
    
    delete r1;
    delete r2;
    delete r3;
    
    printMemoryStats("After Result Deletion");
}

// 测试 ErrorCode 类
void testErrorCode() {
    std::cout << "\n=== Test 2: ErrorCode Class ===" << std::endl;
    
    // 创建 ErrorCode 对象
    auto* e1 = new ErrorCode(1, GetCoreErrorDomain());
    auto* e2 = new ErrorCode(2, GetCoreErrorDomain());
    
    std::cout << "Created 2 ErrorCode objects\n";
    std::cout << "e1 value: " << e1->Value() << ", message: " << e1->Message() << "\n";
    std::cout << "e2 value: " << e2->Value() << "\n";
    
    printMemoryStats("After ErrorCode Creation");
    
    delete e1;
    delete e2;
    
    printMemoryStats("After ErrorCode Deletion");
}

// 测试 Future/Promise 类
void testFuturePromise() {
    std::cout << "\n=== Test 3: Future/Promise Classes ===" << std::endl;
    
    auto* promise = new Promise<int>();
    
    // 启动异步任务
    std::thread t([promise]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        promise->set_value(123);
    });
    
    Future<int> future = promise->get_future();
    
    std::cout << "Future valid: " << future.valid() << "\n";
    future.wait();
    
    auto result = future.GetResult();
    if (result.HasValue()) {
        std::cout << "Future result: " << result.Value() << "\n";
    }
    
    t.join();
    
    printMemoryStats("After Future/Promise");
    
    delete promise;
    
    printMemoryStats("After Promise Deletion");
}

// 测试 File 类
void testFile() {
    std::cout << "\n=== Test 4: File Class ===" << std::endl;
    
    const char* testPath = "/tmp/test_file.txt";
    
    // 创建 File 对象（实例模式）
    auto* file = new File();
    
    // 使用静态方法
    bool exists = File::Util::exists(testPath);
    std::cout << "File exists (before): " << exists << "\n";
    
    // 创建测试文件
    if (file->open(testPath, O_CREAT | O_WRONLY, 0644)) {
        String content = "Test content\n";
        file->write(content.data(), content.size());
        file->close();
        std::cout << "Test file created\n";
    }
    
    exists = File::Util::exists(testPath);
    std::cout << "File exists (after): " << exists << "\n";
    
    printMemoryStats("After File Operations");
    
    // 清理
    File::Util::remove(testPath);
    delete file;
    
    printMemoryStats("After File Deletion");
}

// 测试 Path 类
void testPath() {
    std::cout << "\n=== Test 5: Path Class ===" << std::endl;
    
    // Path 只有静态方法，删除构造函数，所以不能动态分配
    std::cout << "Note: Path class has deleted constructor (static-only utility)\n";
    
    String appFolder = Path::getApplicationFolder();
    std::cout << "Application folder: " << appFolder << "\n";
    
    StringView basename = Path::getBaseName("/usr/local/bin/test");
    std::cout << "Basename: " << basename << "\n";
    
    StringView folder = Path::getFolder("/usr/local/bin/test");
    std::cout << "Folder: " << folder << "\n";
    
    printMemoryStats("After Path Operations");
}

// 测试 SyncObject (Mutex) - SKIPPED
// Note: SyncObject cannot use IMP_OPERATOR_NEW due to circular dependency
// CMemory.hpp includes CSync.hpp, so CSync.hpp cannot include CMemory.hpp
void testSyncObject() {
    std::cout << "\n=== Test 6: SyncObject (SKIPPED - Circular Dependency) ===" << std::endl;
    std::cout << "SyncObject classes (Mutex, RecursiveMutex, etc.) are designed as\n";
    std::cout << "member variables or stack objects, not for dynamic allocation.\n";
    std::cout << "Adding IMP_OPERATOR_NEW would create circular dependency:\n";
    std::cout << "  CMemory.hpp -> CSync.hpp -> CMemory.hpp (X)\n";
    printMemoryStats("SyncObject Test Skipped");
}

// 测试数组分配
void testArrayAllocations() {
    std::cout << "\n=== Test 7: Array Allocations ===" << std::endl;
    
    // Result 数组
    auto* results = new Result<int>[5]{
        Result<int>(1),
        Result<int>(2),
        Result<int>(3),
        Result<int>(4),
        Result<int>(5)
    };
    
    std::cout << "Created Result array[5]\n";
    printMemoryStats("After Array Creation");
    
    delete[] results;
    
    printMemoryStats("After Array Deletion");
}

// 故意泄漏测试
void testIntentionalLeak() {
    std::cout << "\n=== Test 8: Intentional Leak (for leak detection) ===" << std::endl;
    
    // 故意不删除，验证泄漏检测
    [[maybe_unused]] auto* leaked_result = new Result<int>(999);
    [[maybe_unused]] auto* leaked_error = new ErrorCode(99, GetCoreErrorDomain());
    
    std::cout << "Created leaked objects (Result and ErrorCode)\n";
    std::cout << "These will be detected in memory_leak.log\n";
    
    printMemoryStats("After Intentional Leak");
}

// ========== Exception & ErrorDomain Tests ==========

// 自定义 ErrorDomain 用于测试
class TestErrorDomain final : public ErrorDomain {
public:
    constexpr TestErrorDomain() noexcept : ErrorDomain(0xDEADBEEF) {}
    
    const Char* Name() const noexcept override {
        return "TestErrorDomain";
    }
    
    const Char* Message(CodeType errorCode) const noexcept override {
        switch (errorCode) {
            case 1: return "Test error 1";
            case 2: return "Test error 2";
            case 3: return "Test error 3";
            default: return "Unknown test error";
        }
    }
    
    void ThrowAsException(const ErrorCode& errorCode) const noexcept(false) override {
        throw Exception(errorCode);
    }
};

// 获取 TestErrorDomain 单例
const ErrorDomain& GetTestErrorDomain() noexcept {
    static const TestErrorDomain domain;
    return domain;
}

// 测试 Exception 类的动态分配
void testExceptionAllocation() {
    std::cout << "\n=== Test 9: Exception Dynamic Allocation ===" << std::endl;
    
    // 创建 Exception 对象
    auto* ex1 = new Exception(ErrorCode(1, GetCoreErrorDomain()));
    auto* ex2 = new Exception(ErrorCode(2, GetTestErrorDomain()));
    auto* ex3 = new Exception(ErrorCode(3, GetTestErrorDomain()));
    
    std::cout << "Created 3 Exception objects\n";
    std::cout << "ex1 what(): " << ex1->what() << "\n";
    std::cout << "ex2 what(): " << ex2->what() << "\n";
    std::cout << "ex3 what(): " << ex3->what() << "\n";
    std::cout << "ex2 error value: " << ex2->Error().Value() << "\n";
    
    printMemoryStats("After Exception Creation");
    
    delete ex1;
    delete ex2;
    delete ex3;
    
    printMemoryStats("After Exception Deletion");
}

// 测试 Exception 数组分配
void testExceptionArrayAllocation() {
    std::cout << "\n=== Test 10: Exception Array Allocation ===" << std::endl;
    
    std::vector<Exception*> exceptions;
    
    for (int i = 1; i <= 5; ++i) {
        exceptions.push_back(new Exception(ErrorCode(i, GetTestErrorDomain())));
    }
    
    std::cout << "Created 5 Exception objects via vector\n";
    for (size_t i = 0; i < exceptions.size(); ++i) {
        std::cout << "  Exception[" << i << "]: " << exceptions[i]->what() << "\n";
    }
    
    printMemoryStats("After Exception Array Creation");
    
    for (auto* ex : exceptions) {
        delete ex;
    }
    
    printMemoryStats("After Exception Array Deletion");
}

// 测试 Exception 拷贝语义
void testExceptionCopy() {
    std::cout << "\n=== Test 11: Exception Copy Semantics ===" << std::endl;
    
    auto* original = new Exception(ErrorCode(42, GetTestErrorDomain()));
    std::cout << "Original exception: " << original->what() << "\n";
    
    // 测试拷贝构造
    auto* copied = new Exception(*original);
    std::cout << "Copied exception: " << copied->what() << "\n";
    std::cout << "Same error code: " << (original->Error() == copied->Error()) << "\n";
    
    printMemoryStats("After Exception Copy");
    
    delete original;
    delete copied;
    
    printMemoryStats("After Exception Copy Deletion");
}

// 测试 Exception 异常抛出和捕获
void testExceptionThrow() {
    std::cout << "\n=== Test 12: Exception Throw/Catch ===" << std::endl;
    
    try {
        auto* ex = new Exception(ErrorCode(999, GetTestErrorDomain()));
        std::cout << "Created exception: " << ex->what() << "\n";
        
        // 抛出异常（会创建一个拷贝）
        Exception to_throw = *ex;
        delete ex;  // 删除原始对象
        
        throw to_throw;
    } catch (const Exception& e) {
        std::cout << "Caught exception: " << e.what() << "\n";
        std::cout << "Error code: " << e.Error().Value() << "\n";
    }
    
    printMemoryStats("After Exception Throw/Catch");
}

// 测试 ErrorDomain 使用
void testErrorDomainUsage() {
    std::cout << "\n=== Test 13: ErrorDomain Usage ===" << std::endl;
    
    const ErrorDomain& testDomain = GetTestErrorDomain();
    const ErrorDomain& coreDomain = GetCoreErrorDomain();
    const ErrorDomain& futureDomain = GetFutureErrorDomain();
    
    std::cout << "TestErrorDomain ID: " << std::hex << testDomain.Id() << std::dec << "\n";
    std::cout << "TestErrorDomain Name: " << testDomain.Name() << "\n";
    std::cout << "CoreErrorDomain Name: " << coreDomain.Name() << "\n";
    std::cout << "FutureErrorDomain Name: " << futureDomain.Name() << "\n";
    
    // 测试消息获取
    std::cout << "Test message 1: " << testDomain.Message(1) << "\n";
    std::cout << "Test message 2: " << testDomain.Message(2) << "\n";
    
    // 测试 ErrorDomain 比较
    std::cout << "TestDomain == CoreDomain: " << (testDomain == coreDomain) << "\n";
    std::cout << "TestDomain != CoreDomain: " << (testDomain != coreDomain) << "\n";
    
    printMemoryStats("After ErrorDomain Usage");
}

// 测试 ErrorDomain 的 ThrowAsException
void testErrorDomainThrow() {
    std::cout << "\n=== Test 14: ErrorDomain ThrowAsException ===" << std::endl;
    
    try {
        ErrorCode ec(5, GetTestErrorDomain());
        std::cout << "Created ErrorCode: value=" << ec.Value() 
                  << ", message=" << ec.Message() << "\n";
        
        // 通过 ErrorDomain 抛出异常
        ec.ThrowAsException();
        
    } catch (const Exception& e) {
        std::cout << "Caught exception from ErrorDomain: " << e.what() << "\n";
        std::cout << "Error code value: " << e.Error().Value() << "\n";
        std::cout << "Error domain name: " << e.Error().Domain().Name() << "\n";
    }
    
    printMemoryStats("After ErrorDomain Throw");
}

// 测试混合场景：Exception + ErrorCode + ErrorDomain
void testMixedExceptionScenario() {
    std::cout << "\n=== Test 15: Mixed Exception Scenario ===" << std::endl;
    
    std::vector<Exception*> exceptions;
    
    // 创建多个不同 ErrorDomain 的 Exception
    exceptions.push_back(new Exception(ErrorCode(1, GetCoreErrorDomain())));
    exceptions.push_back(new Exception(ErrorCode(2, GetTestErrorDomain())));
    exceptions.push_back(new Exception(ErrorCode(future_errc::broken_promise)));
    exceptions.push_back(new Exception(ErrorCode(future_errc::no_state)));
    exceptions.push_back(new Exception(ErrorCode(3, GetTestErrorDomain())));
    
    std::cout << "Created " << exceptions.size() << " exceptions from different domains\n";
    
    for (size_t i = 0; i < exceptions.size(); ++i) {
        std::cout << "  [" << i << "] domain=" << exceptions[i]->Error().Domain().Name()
                  << ", code=" << exceptions[i]->Error().Value()
                  << ", msg=" << exceptions[i]->what() << "\n";
    }
    
    printMemoryStats("After Mixed Exception Creation");
    
    // 清理
    for (auto* ex : exceptions) {
        delete ex;
    }
    
    printMemoryStats("After Mixed Exception Cleanup");
}

// Exception 故意泄漏测试
void testExceptionIntentionalLeak() {
    std::cout << "\n=== Test 16: Exception Intentional Leak ===" << std::endl;
    
    // 故意泄漏 Exception 对象
    [[maybe_unused]] auto* leaked_ex1 = new Exception(ErrorCode(888, GetTestErrorDomain()));
    [[maybe_unused]] auto* leaked_ex2 = new Exception(ErrorCode(999, GetCoreErrorDomain()));
    
    std::cout << "Created 2 leaked Exception objects\n";
    std::cout << "These will be detected in memory_leak.log with class=Exception\n";
    
    printMemoryStats("After Exception Intentional Leak");
}

int main() {
    std::cout << "========================================\n"
              << "Test IMP_OPERATOR_NEW for Functional Classes\n"
              << "Testing: Result, ErrorCode, Future, Promise, File, Path, Exception, ErrorDomain\n"
              << "========================================\n" << std::endl;
    
    // AUTOSAR-compliant initialization (includes MemoryManager)
    auto initResult = Initialize();
    if (!initResult.HasValue()) {
        std::cerr << "Failed to initialize Core: " << initResult.Error().Message() << "\n";
        return 1;
    }
    
    printMemoryStats("Initial");
    
    // Basic functional classes
    testResult();
    testErrorCode();
    testFuturePromise();
    testFile();
    testPath();
    testSyncObject();
    testArrayAllocations();
    testIntentionalLeak();
    
    // Exception and ErrorDomain tests
    testExceptionAllocation();
    testExceptionArrayAllocation();
    testExceptionCopy();
    testExceptionThrow();
    testErrorDomainUsage();
    testErrorDomainThrow();
    testMixedExceptionScenario();
    testExceptionIntentionalLeak();
    
    std::cout << "\n========================================\n"
              << "All tests completed!\n"
              << "Total: 16 test scenarios\n"
              << "Check memory_leak.log for leak report\n"
              << "  - Expected leaks: 4 objects\n"
              << "  - 2x Result/ErrorCode (Test 8)\n"
              << "  - 2x Exception (Test 16)\n"
              << "========================================\n" << std::endl;
    
    // AUTOSAR-compliant deinitialization (includes MemoryManager cleanup)
    auto deinitResult = Deinitialize();
    (void)deinitResult;
    
    return 0;
}
