# ErrorDomain 生命周期简化方案 (Phase 1)

## 📋 问题分析

### 当前实现 (v1)

```cpp
class ErrorCode {
    ErrorDomain const& m_errDomain;  // ⚠️ 引用类型
};
```

**问题**：
1. **生命周期依赖**：`ErrorCode`依赖`ErrorDomain`的生命周期
2. **不可移动**：引用成员导致不能真正移动
3. **必须静态**：只能使用全局静态`ErrorDomain`对象
4. **测试困难**：无法mock或替换`ErrorDomain`

**当前的"安全"保证**：
```cpp
static constexpr CoreErrorDomain g_coreErrorDomain;  // 全局静态，永不析构
constexpr const ErrorDomain& GetCoreErrorDomain() {
    return g_coreErrorDomain;
}
```

## ✅ 改进方案

### 方案1：使用 `shared_ptr` (推荐)

**优点**：
- ✅ 完全的值语义，可拷贝/可移动
- ✅ 无生命周期问题
- ✅ 支持动态创建的`ErrorDomain`
- ✅ 易于测试和mock

**缺点**：
- ⚠️ 额外的`shared_ptr`开销（24字节）
- ⚠️ 引用计数的原子操作

**实现**：
```cpp
class ErrorCode {
    ErrorDomain::CodeType                   m_errCode;
    std::shared_ptr<const ErrorDomain>      m_errDomain;  // 共享所有权
    ErrorDomain::SupportDataType            m_errData;
};
```

**使用示例**：
```cpp
// v1: 必须使用全局静态
ErrorCode ec1(CoreErrc::kInvalidArgument);  // 引用 g_coreErrorDomain

// v2: 更灵活
auto domain = std::make_shared<CoreErrorDomain>();
ErrorCode ec2(CoreErrc::kInvalidArgument, domain);

// 或使用全局注册表（优化）
auto domain = GetRegisteredDomain(CoreErrorDomain::kDomainId);
ErrorCode ec3(CoreErrc::kInvalidArgument, domain);
```

### 方案2：使用 `ErrorDomain*` + 全局注册表

**优点**：
- ✅ 零开销（只存指针）
- ✅ 可以支持动态域
- ✅ 保持轻量级

**缺点**：
- ⚠️ 需要全局注册表管理
- ⚠️ 必须保证注册的域不被析构

**实现**：
```cpp
class ErrorDomainRegistry {
public:
    static void Register(ErrorDomain::IdType id, const ErrorDomain* domain);
    static const ErrorDomain* Get(ErrorDomain::IdType id);
};

class ErrorCode {
    ErrorDomain::CodeType           m_errCode;
    const ErrorDomain*              m_errDomain;  // 非拥有指针
    ErrorDomain::SupportDataType    m_errData;
};
```

### 方案3：存储 `DomainId` 而非引用/指针

**优点**：
- ✅ 最小开销（8字节ID）
- ✅ 完全的值语义
- ✅ 线程安全

**缺点**：
- ⚠️ 每次访问需要查表
- ⚠️ 需要全局注册表

**实现**：
```cpp
class ErrorCode {
    ErrorDomain::CodeType           m_errCode;
    ErrorDomain::IdType             m_domainId;   // 只存ID
    ErrorDomain::SupportDataType    m_errData;

    const ErrorDomain& Domain() const {
        return ErrorDomainRegistry::Get(m_domainId);  // 查表
    }
};
```

## 🎯 推荐实现（混合方案）

结合方案1和方案2的优点：

```cpp
class ErrorCode {
    ErrorDomain::CodeType                   m_errCode;
    std::shared_ptr<const ErrorDomain>      m_errDomain;
    ErrorDomain::SupportDataType            m_errData;
};

// 对于静态域，使用非拥有的 shared_ptr（零开销）
class ErrorDomainRegistry {
    std::shared_ptr<const ErrorDomain> GetBuiltinDomain(ErrorDomain::IdType id) {
        // 返回非拥有shared_ptr，使用空deleter
        auto it = m_static_domains.find(id);
        if (it != m_static_domains.end()) {
            return std::shared_ptr<const ErrorDomain>(it->second, [](const ErrorDomain*){});
        }
        return nullptr;
    }
};
```

**好处**：
1. 静态域：零额外开销（shared_ptr不真正拥有）
2. 动态域：正常的shared_ptr语义
3. 统一的API
4. 完全向后兼容

## 📊 性能对比

| 方案 | 大小 | 拷贝开销 | 访问开销 | 灵活性 |
|------|------|----------|----------|--------|
| **v1 (引用)** | 16字节 | 不可拷贝 | 0 | ⭐⭐ |
| **v2 (shared_ptr)** | 32字节 | 原子操作 | 1次解引用 | ⭐⭐⭐⭐⭐ |
| **非拥有指针** | 16字节 | memcpy | 1次解引用 | ⭐⭐⭐ |
| **ID查表** | 16字节 | memcpy | 哈希查表 | ⭐⭐⭐⭐ |

## 🔧 迁移步骤

### Step 1: 添加v2命名空间

```cpp
namespace lap::core::v2 {
    // 新的ErrorDomain和ErrorCode
}
```

### Step 2: 渐进式迁移

```cpp
// 旧代码继续工作
using namespace lap::core;  // v1
ErrorCode ec1 = MakeErrorCode(CoreErrc::kInvalidArgument);

// 新代码使用v2
using namespace lap::core::v2;
auto domain = GetCoreErrorDomain();  // 返回 shared_ptr
ErrorCode ec2(CoreErrc::kInvalidArgument, domain);
```

### Step 3: 更新所有ErrorDomain实现

```cpp
// CoreErrorDomain.hpp
namespace lap::core::v2 {
    class CoreErrorDomain final : public ErrorDomain { ... };
    
    inline std::shared_ptr<const ErrorDomain> GetCoreErrorDomain() {
        static auto domain = std::make_shared<CoreErrorDomain>();
        return domain;
    }
}
```

### Step 4: 弃用v1

```cpp
namespace lap::core {
    // [[deprecated("Use lap::core::v2::ErrorCode instead")]]
    class ErrorCode { ... };
}
```

## ✅ Phase 1 实施建议

考虑到Phase 1时间限制（1-2周），建议采用**最小侵入式方案**：

### 简化方案：添加辅助构造函数

```cpp
// CErrorCode.hpp - 保持现有实现不变

// 添加新的辅助类（可选）
class ErrorCodeHolder {
    ErrorCode m_code;
    std::shared_ptr<const ErrorDomain> m_domain_owner;  // 保证生命周期
    
public:
    ErrorCodeHolder(ErrorDomain::CodeType code, std::shared_ptr<const ErrorDomain> domain)
        : m_code(code, *domain)
        , m_domain_owner(std::move(domain))
    {}
    
    const ErrorCode& Get() const { return m_code; }
};
```

这样：
- ✅ 不破坏现有代码
- ✅ 为需要动态域的场景提供解决方案
- ✅ 最小改动量
- ✅ 为Phase 2的完整重构铺路

## 📝 文档更新

### 使用指南

```cpp
// ❌ 危险：动态创建的域
ErrorCode BadExample() {
    CoreErrorDomain local_domain;  // 栈对象
    return ErrorCode(CoreErrc::kInvalidArgument, local_domain);  // 悬空引用！
}

// ✅ 安全：使用全局静态域
ErrorCode GoodExample() {
    return MakeErrorCode(CoreErrc::kInvalidArgument);  // 引用全局 g_coreErrorDomain
}

// ✅ 未来（v2）：使用shared_ptr
ErrorCode FutureExample() {
    auto domain = GetCoreErrorDomain();  // shared_ptr
    return ErrorCode(CoreErrc::kInvalidArgument, domain);  // 安全
}
```

## 🎉 总结

**Phase 1 建议**：
1. ✅ 添加 `CErrorDomain_v2.hpp`（新API）
2. ✅ 保持v1不变（向后兼容）
3. ✅ 更新文档说明生命周期要求
4. ✅ 新代码逐步使用v2

**Phase 2 完整重构**：
1. 迁移所有ErrorDomain到v2
2. 弃用v1 API
3. 性能基准测试
4. 完整的单元测试覆盖

**收益**：
- 🚀 更灵活的错误处理
- 🔒 消除生命周期bug隐患
- 🧪 更易测试
- 📦 更好的模块化

---

**状态**：📋 提案  
**优先级**：🟡 中（Phase 1可选，Phase 2推荐）  
**风险**：🟢 低（有v1作为fallback）  
**工作量**：⏱️ 2-3天（v2实现） + 1周（迁移）
