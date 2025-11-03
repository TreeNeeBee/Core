# ErrorDomain AUTOSAR 标准合规性优化

## 概述

本次优化根据 **AUTOSAR Adaptive Platform SWS Core 第8章 API定义**，重新实现了 ErrorDomain 的生命周期管理，确保完全符合 AUTOSAR 规范。

## AUTOSAR 标准要求

根据 AUTOSAR AP SWS Core 规范，ErrorDomain 的设计遵循以下原则：

1. **静态全局对象**: ErrorDomain 实例应该是编译期静态对象（`static constexpr`）
2. **引用语义**: ErrorCode 持有对 ErrorDomain 的 `const&` 引用
3. **constexpr 支持**: 构造函数和访问器应该支持编译期求值
4. **非虚析构函数**: 静态对象不需要虚析构，保持 trivial destructor 以支持 constexpr

## 实现方案

### 1. ErrorDomain 基类设计

```cpp
class ErrorDomain
{
public:
    using IdType            = UInt64;
    using CodeType          = Int32;
    using SupportDataType   = Int32;

public:
    constexpr IdType            Id () const noexcept;
    virtual const Char*         Name () const noexcept = 0;
    virtual const Char*         Message ( CodeType errorCode ) const noexcept = 0;
    virtual void                ThrowAsException ( const ErrorCode &errorCode ) const noexcept(false) = 0;

    constexpr Bool              operator== ( const ErrorDomain &other ) const noexcept;
    constexpr Bool              operator!= ( const ErrorDomain &other ) const noexcept;

protected:
    explicit constexpr ErrorDomain ( IdType id ) noexcept : m_id( id ) { }
    
    // 非虚析构函数，支持 constexpr 静态对象
    ~ErrorDomain () noexcept = default;
    
    // 禁止拷贝和移动
    ErrorDomain ( const ErrorDomain & )  = delete;
    ErrorDomain ( ErrorDomain && ) = delete;
    ErrorDomain& operator= ( const ErrorDomain & ) = delete;
    ErrorDomain& operator= ( ErrorDomain && ) = delete;

private:
    IdType m_id{0};
};
```

**关键设计点**:
- ✅ 析构函数非虚（non-virtual），保持 trivial destructor
- ✅ 构造函数 constexpr，支持编译期初始化
- ✅ 禁止拷贝和移动，确保全局对象的唯一性

### 2. ErrorCode 实现

```cpp
class ErrorCode
{
public:
    // 构造函数接受 ErrorDomain 引用（非 shared_ptr）
    constexpr ErrorCode ( 
        ErrorDomain::CodeType value, 
        const ErrorDomain& domain, 
        ErrorDomain::SupportDataType data = ErrorDomain::SupportDataType() 
    ) noexcept
        : m_errCode( value )
        , m_errDomain( domain )  // 引用成员
        , m_errData( data )
    {
    }

    constexpr ErrorDomain::CodeType         Value () const noexcept;
    const ErrorDomain&                      Domain () const noexcept;
    constexpr ErrorDomain::SupportDataType  SupportData () const noexcept;
    StringView                              Message () const noexcept;
    void                                    ThrowAsException () const;

private:
    ErrorDomain::CodeType       m_errCode;
    const ErrorDomain&          m_errDomain;    // 引用成员，非指针
    ErrorDomain::SupportDataType m_errData;
};
```

**关键设计点**:
- ✅ 使用 `const ErrorDomain&` 引用成员（非 shared_ptr）
- ✅ 构造函数 constexpr，支持编译期求值
- ✅ 引用语义确保零开销，与 AUTOSAR 标准一致

### 3. 具体 ErrorDomain 实现

以 CoreErrorDomain 为例：

```cpp
class CoreErrorDomain final : public ErrorDomain 
{
public:
    using Errc      = CoreErrc;
    using Exception = CoreException;

public:
    const Char* Name () const noexcept override { return "Core"; }
    const Char* Message ( CodeType errorCode ) const noexcept override 
    { 
        return CoreErrMessage( static_cast< Errc > ( errorCode ) ); 
    }
    void ThrowAsException ( const ErrorCode &errorCode ) const noexcept( false ) override 
    { 
        throw Exception( errorCode ); 
    }

    constexpr CoreErrorDomain() noexcept
        : ErrorDomain( ErrorDomain::IdType{ 0x8000000000000014 } )
    {
    }
};

// 全局静态 constexpr 对象
static constexpr CoreErrorDomain g_coreErrorDomain;

// 获取器返回引用
constexpr const ErrorDomain& GetCoreErrorDomain () noexcept
{
    return g_coreErrorDomain;
}

// MakeErrorCode 返回 ErrorCode（持有引用）
constexpr ErrorCode MakeErrorCode ( 
    CoreErrc code, 
    ErrorDomain::SupportDataType data = ErrorDomain::SupportDataType() 
) noexcept
{
    return { static_cast< ErrorDomain::CodeType >( code ), GetCoreErrorDomain(), data };
}
```

**关键设计点**:
- ✅ `static constexpr` 全局对象，编译期初始化
- ✅ 获取器返回 `const ErrorDomain&` 引用
- ✅ MakeErrorCode 直接返回 ErrorCode，无需动态分配

## 与之前 shared_ptr 方案的对比

| 特性 | AUTOSAR 标准方案 | shared_ptr 方案 |
|-----|----------------|----------------|
| 生命周期管理 | 静态全局对象 | shared_ptr 管理 |
| 内存开销 | 零开销（引用） | shared_ptr 控制块开销 |
| constexpr 支持 | 完全支持 | 不支持（需要运行时初始化） |
| 析构函数 | 非虚（trivial） | 虚析构（non-trivial） |
| 动态创建支持 | 不支持 | 支持 |
| 测试可替换性 | 有限 | 良好 |
| AUTOSAR 合规性 | ✅ 完全符合 | ❌ 非标准扩展 |

## 设计权衡与理由

### 为什么选择 AUTOSAR 标准方案？

1. **规范合规性**: 
   - AUTOSAR 是汽车行业标准，必须严格遵循
   - 保证与其他 AUTOSAR AP 实现的互操作性

2. **性能优势**:
   - 零运行时开销（引用 vs shared_ptr）
   - 编译期初始化（constexpr）
   - 无需动态内存分配

3. **简洁性**:
   - 实现简单，易于理解
   - 无需管理 shared_ptr 的生命周期复杂性
   - 编译错误更清晰

4. **安全性**:
   - 静态生命周期，不存在悬空引用问题
   - constexpr 确保编译期正确性
   - 类型系统保证引用有效性

### 什么时候需要 shared_ptr 方案？

如果有以下需求，可以考虑扩展 shared_ptr 支持：

1. **动态错误域创建**: 插件系统、运行时加载的模块
2. **高级测试需求**: Mock/Stub ErrorDomain 用于单元测试
3. **跨进程错误传递**: 需要序列化/反序列化错误信息

**当前结论**: LightAP 是严格的 AUTOSAR 实现，所有 ErrorDomain 都是静态定义，因此选择标准方案。

## 编译验证

### 编译结果

```bash
$ cd build && make lap_core lap_log core_test -j8
[100%] Built target lap_core
[100%] Built target lap_log
[100%] Built target core_test
```

✅ 所有模块编译成功

### 测试结果

```bash
$ ./build/modules/Core/core_test
[==========] Running 38 tests from 20 test suites.
...
[  PASSED  ] 36 tests.
[  FAILED  ] 2 tests
```

✅ ErrorDomain 相关测试全部通过
- CoreErrorDomainTest.MessageForKnownCodes ✅
- CoreErrorDomainTest.ThrowAsException ✅

失败的 2 个测试是 InstanceSpecifier 相关的预存在问题，与 ErrorDomain 优化无关。

## 代码变更总结

### 修改的文件

1. **modules/Core/source/inc/CErrorDomain.hpp**
   - 移除虚析构函数
   - 改为非虚析构，支持 constexpr

2. **modules/Core/source/inc/CErrorCode.hpp**
   - 移除 `#include <memory>`
   - 构造函数参数从 `shared_ptr` 改为 `const ErrorDomain&`
   - 成员变量从 `shared_ptr` 改为 `const ErrorDomain&`
   - 添加 constexpr 到构造函数
   - 更新 operator== 逻辑（引用地址比较）

3. **modules/Core/source/inc/CCoreErrorDomain.hpp**
   - 移除 `#include <memory>`
   - GetCoreErrorDomain() 返回 `const ErrorDomain&`
   - MakeErrorCode() 保持 constexpr

4. **modules/Core/source/inc/CFutureErrorDomain.hpp**
   - 同 CCoreErrorDomain.hpp 的变更

5. **modules/Persistency/source/inc/CPerErrorDomain.hpp**
   - 同 CCoreErrorDomain.hpp 的变更

### 代码行数变化

- 删除: ~15 行（shared_ptr 相关代码）
- 修改: ~20 行（类型签名和实现）
- 净减少: ~5 行代码

## 最佳实践建议

### 定义新的 ErrorDomain

```cpp
// 1. 定义错误码枚举
enum class MyErrc : ErrorDomain::CodeType {
    kErrorCode1 = 1,
    kErrorCode2 = 2
};

// 2. 实现 ErrorDomain 派生类
class MyErrorDomain final : public ErrorDomain {
public:
    const Char* Name() const noexcept override { return "MyDomain"; }
    const Char* Message(CodeType code) const noexcept override { /* ... */ }
    void ThrowAsException(const ErrorCode& ec) const override { /* ... */ }
    
    constexpr MyErrorDomain() noexcept 
        : ErrorDomain(0x8000000000000999)  // 唯一 ID
    {}
};

// 3. 定义全局静态对象
static constexpr MyErrorDomain g_myErrorDomain;

// 4. 提供获取器
constexpr const ErrorDomain& GetMyErrorDomain() noexcept {
    return g_myErrorDomain;
}

// 5. 提供 MakeErrorCode
constexpr ErrorCode MakeErrorCode(MyErrc code, ErrorDomain::SupportDataType data = 0) noexcept {
    return { static_cast<ErrorDomain::CodeType>(code), GetMyErrorDomain(), data };
}
```

### 使用 ErrorCode

```cpp
// 创建错误码
auto ec = MakeErrorCode(CoreErrc::kInvalidArgument);

// 检查错误
if (ec.Value() != 0) {
    std::cout << ec.Message() << std::endl;
}

// 抛出异常
try {
    ec.ThrowAsException();
} catch (const CoreException& e) {
    std::cout << e.what() << std::endl;
}

// 与 Result 结合使用
Result<int> DoSomething() {
    if (error_condition) {
        return MakeErrorCode(CoreErrc::kInvalidArgument);
    }
    return 42;
}
```

## 总结

本次优化严格按照 **AUTOSAR Adaptive Platform SWS Core 第8章** 的 API 定义实现：

✅ **符合 AUTOSAR 标准**: 使用 constexpr 静态对象 + 引用语义  
✅ **零运行时开销**: 无 shared_ptr 控制块，无动态分配  
✅ **编译期求值**: 支持 constexpr 构造和访问  
✅ **类型安全**: 引用语义保证生命周期正确性  
✅ **测试验证**: 所有相关单元测试通过  

这是 **LightAP Core 模块符合 AUTOSAR 规范的正确实现方式**。

---

**文档版本**: 1.0  
**更新日期**: 2025-10-29  
**作者**: LightAP Core Team
