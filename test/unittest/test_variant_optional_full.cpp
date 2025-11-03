/**
 * @file        test_variant_optional_full.cpp
 * @brief       Comprehensive unit tests for Variant and Optional
 * @details     Full coverage of Variant and Optional construction, access, and operations
 */

#include <gtest/gtest.h>
#include "CVariant.hpp"
#include "COptional.hpp"
#include "CString.hpp"

using namespace lap::core;

// ============================================================================
// Optional Construction Tests
// ============================================================================

TEST(OptionalFullTest, DefaultConstruction)
{
    Optional<int> opt;
    EXPECT_FALSE(opt.has_value());
}

TEST(OptionalFullTest, ValueConstruction)
{
    Optional<int> opt(42);
    EXPECT_TRUE(opt.has_value());
    EXPECT_EQ(*opt, 42);
    EXPECT_EQ(opt.value(), 42);
}

TEST(OptionalFullTest, CopyConstruction)
{
    Optional<int> opt1(100);
    Optional<int> opt2(opt1);
    
    EXPECT_TRUE(opt2.has_value());
    EXPECT_EQ(opt2.value(), 100);
}

TEST(OptionalFullTest, MoveConstruction)
{
    Optional<String> opt1("test");
    Optional<String> opt2(std::move(opt1));
    
    EXPECT_TRUE(opt2.has_value());
    EXPECT_EQ(opt2.value(), "test");
}

TEST(OptionalFullTest, MakeOptional)
{
    auto opt = MakeOptional(42);
    EXPECT_TRUE(opt.has_value());
    EXPECT_EQ(*opt, 42);
}

TEST(OptionalFullTest, MakeOptionalString)
{
    auto opt = MakeOptional<String>("Hello");
    EXPECT_TRUE(opt.has_value());
    EXPECT_EQ(opt.value(), "Hello");
}

// ============================================================================
// Optional Assignment Tests
// ============================================================================

TEST(OptionalAssignmentTest, CopyAssignment)
{
    Optional<int> opt1(42);
    Optional<int> opt2;
    
    opt2 = opt1;
    EXPECT_TRUE(opt2.has_value());
    EXPECT_EQ(opt2.value(), 42);
}

TEST(OptionalAssignmentTest, MoveAssignment)
{
    Optional<String> opt1("data");
    Optional<String> opt2;
    
    opt2 = std::move(opt1);
    EXPECT_TRUE(opt2.has_value());
    EXPECT_EQ(opt2.value(), "data");
}

TEST(OptionalAssignmentTest, ValueAssignment)
{
    Optional<int> opt;
    opt = 99;
    
    EXPECT_TRUE(opt.has_value());
    EXPECT_EQ(opt.value(), 99);
}

// ============================================================================
// Optional Value Access Tests
// ============================================================================

TEST(OptionalAccessTest, DereferenceOperator)
{
    Optional<int> opt(42);
    EXPECT_EQ(*opt, 42);
}

TEST(OptionalAccessTest, ArrowOperator)
{
    Optional<String> opt("test");
    EXPECT_EQ(opt->size(), 4);
    EXPECT_FALSE(opt->empty());
}

TEST(OptionalAccessTest, ValueMethod)
{
    Optional<int> opt(100);
    EXPECT_EQ(opt.value(), 100);
}

TEST(OptionalAccessTest, ValueOr)
{
    Optional<int> opt1(42);
    EXPECT_EQ(opt1.value_or(99), 42);
    
    Optional<int> opt2;
    EXPECT_EQ(opt2.value_or(99), 99);
}

// ============================================================================
// Optional State Management Tests
// ============================================================================

TEST(OptionalStateTest, HasValue)
{
    Optional<int> opt1(42);
    EXPECT_TRUE(opt1.has_value());
    
    Optional<int> opt2;
    EXPECT_FALSE(opt2.has_value());
}

TEST(OptionalStateTest, BoolConversion)
{
    Optional<int> opt1(42);
    Optional<int> opt2;
    
    EXPECT_TRUE(static_cast<bool>(opt1));
    EXPECT_FALSE(static_cast<bool>(opt2));
}

TEST(OptionalStateTest, Reset)
{
    Optional<int> opt(42);
    EXPECT_TRUE(opt.has_value());
    
    opt.reset();
    EXPECT_FALSE(opt.has_value());
}

TEST(OptionalStateTest, Emplace)
{
    Optional<String> opt;
    EXPECT_FALSE(opt.has_value());
    
    opt.emplace("emplaced");
    EXPECT_TRUE(opt.has_value());
    EXPECT_EQ(opt.value(), "emplaced");
}

// ============================================================================
// Optional Comparison Tests
// ============================================================================

TEST(OptionalComparisonTest, EqualityWithValue)
{
    Optional<int> opt1(42);
    Optional<int> opt2(42);
    Optional<int> opt3(99);
    
    EXPECT_EQ(opt1, opt2);
    EXPECT_NE(opt1, opt3);
}

TEST(OptionalComparisonTest, EqualityEmpty)
{
    Optional<int> opt1;
    Optional<int> opt2;
    Optional<int> opt3(42);
    
    EXPECT_EQ(opt1, opt2);
    EXPECT_NE(opt1, opt3);
}

TEST(OptionalComparisonTest, RelationalOperators)
{
    Optional<int> opt1(10);
    Optional<int> opt2(20);
    Optional<int> opt3;
    
    EXPECT_LT(opt1, opt2);
    EXPECT_LE(opt1, opt1);
    EXPECT_GT(opt2, opt1);
    EXPECT_GE(opt2, opt2);
    EXPECT_LT(opt3, opt1);  // empty < value
}

// ============================================================================
// Optional with Complex Types
// ============================================================================

struct TestStruct {
    int id;
    String name;
    
    TestStruct(int i, const String& n) : id(i), name(n) {}
};

TEST(OptionalComplexTest, StructOptional)
{
    Optional<TestStruct> opt;
    opt.emplace(42, "Test");
    
    EXPECT_TRUE(opt.has_value());
    EXPECT_EQ(opt->id, 42);
    EXPECT_EQ(opt->name, "Test");
}

TEST(OptionalComplexTest, VectorOptional)
{
    Optional<Vector<int>> opt;
    opt.emplace(Vector<int>{1, 2, 3});
    
    EXPECT_TRUE(opt.has_value());
    EXPECT_EQ(opt->size(), 3);
    EXPECT_EQ((*opt)[1], 2);
}

// ============================================================================
// Variant Construction Tests
// ============================================================================

TEST(VariantFullTest, DefaultConstruction)
{
    Variant<int, String> var;
    // Default constructs first type
    EXPECT_TRUE(holds_alternative<int>(var));
}

TEST(VariantFullTest, ValueConstruction)
{
    Variant<int, String, double> var1(42);
    EXPECT_TRUE(holds_alternative<int>(var1));
    EXPECT_EQ(get<int>(var1), 42);
    
    Variant<int, String, double> var2(String("test"));
    EXPECT_TRUE(holds_alternative<String>(var2));
    EXPECT_EQ(get<String>(var2), "test");
    
    Variant<int, String, double> var3(3.14);
    EXPECT_TRUE(holds_alternative<double>(var3));
    EXPECT_DOUBLE_EQ(get<double>(var3), 3.14);
}

TEST(VariantFullTest, CopyConstruction)
{
    Variant<int, String> var1(42);
    Variant<int, String> var2(var1);
    
    EXPECT_TRUE(holds_alternative<int>(var2));
    EXPECT_EQ(get<int>(var2), 42);
}

TEST(VariantFullTest, MoveConstruction)
{
    Variant<int, String> var1(String("move me"));
    Variant<int, String> var2(std::move(var1));
    
    EXPECT_TRUE(holds_alternative<String>(var2));
    EXPECT_EQ(get<String>(var2), "move me");
}

// ============================================================================
// Variant Assignment Tests
// ============================================================================

TEST(VariantAssignmentTest, ValueAssignment)
{
    Variant<int, String> var;
    
    var = 42;
    EXPECT_TRUE(holds_alternative<int>(var));
    EXPECT_EQ(get<int>(var), 42);
    
    var = String("test");
    EXPECT_TRUE(holds_alternative<String>(var));
    EXPECT_EQ(get<String>(var), "test");
}

TEST(VariantAssignmentTest, CopyAssignment)
{
    Variant<int, String> var1(100);
    Variant<int, String> var2;
    
    var2 = var1;
    EXPECT_TRUE(holds_alternative<int>(var2));
    EXPECT_EQ(get<int>(var2), 100);
}

TEST(VariantAssignmentTest, MoveAssignment)
{
    Variant<int, String> var1(String("data"));
    Variant<int, String> var2;
    
    var2 = std::move(var1);
    EXPECT_TRUE(holds_alternative<String>(var2));
    EXPECT_EQ(get<String>(var2), "data");
}

// ============================================================================
// Variant Access Tests
// ============================================================================

TEST(VariantAccessTest, GetByType)
{
    Variant<int, String, double> var(42);
    EXPECT_EQ(get<int>(var), 42);
}

TEST(VariantAccessTest, GetByIndex)
{
    Variant<int, String, double> var(String("test"));
    EXPECT_EQ(get<1>(var), "test");
}

TEST(VariantAccessTest, GetIf)
{
    Variant<int, String, double> var(42);
    
    auto* pInt = get_if<int>(&var);
    ASSERT_NE(pInt, nullptr);
    EXPECT_EQ(*pInt, 42);
    
    auto* pStr = get_if<String>(&var);
    EXPECT_EQ(pStr, nullptr);
}

TEST(VariantAccessTest, GetIfByIndex)
{
    Variant<int, String, double> var(String("test"));
    
    auto* pStr = get_if<1>(&var);
    ASSERT_NE(pStr, nullptr);
    EXPECT_EQ(*pStr, "test");
    
    auto* pInt = get_if<0>(&var);
    EXPECT_EQ(pInt, nullptr);
}

// ============================================================================
// Variant Type Checking Tests
// ============================================================================

TEST(VariantTypeCheckTest, HoldsAlternative)
{
    Variant<int, String, double> var;
    
    var = 42;
    EXPECT_TRUE(holds_alternative<int>(var));
    EXPECT_FALSE(holds_alternative<String>(var));
    EXPECT_FALSE(holds_alternative<double>(var));
    
    var = String("test");
    EXPECT_FALSE(holds_alternative<int>(var));
    EXPECT_TRUE(holds_alternative<String>(var));
    EXPECT_FALSE(holds_alternative<double>(var));
}

TEST(VariantTypeCheckTest, Index)
{
    Variant<int, String, double> var;
    
    var = 42;
    EXPECT_EQ(var.index(), 0);
    
    var = String("test");
    EXPECT_EQ(var.index(), 1);
    
    var = 3.14;
    EXPECT_EQ(var.index(), 2);
}

// ============================================================================
// Variant Visitor Tests
// ============================================================================

TEST(VariantVisitorTest, SimpleVisitor)
{
    Variant<int, String, double> var(42);
    
    int result = visit([](auto&& arg) -> int {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, int>) {
            return arg * 2;
        } else if constexpr (std::is_same_v<T, String>) {
            return static_cast<int>(arg.size());
        } else {
            return static_cast<int>(arg);
        }
    }, var);
    
    EXPECT_EQ(result, 84);
}

TEST(VariantVisitorTest, VisitorWithString)
{
    Variant<int, String, double> var(String("test"));
    
    String result = visit([](auto&& arg) -> String {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, int>) {
            return ToString(arg);
        } else if constexpr (std::is_same_v<T, String>) {
            return arg;
        } else {
            return ToString(static_cast<int>(arg));
        }
    }, var);
    
    EXPECT_EQ(result, "test");
}

TEST(VariantVisitorTest, VisitorTypeDispatch)
{
    Variant<int, String> var1(42);
    Variant<int, String> var2(String("hello"));
    
    auto visitor = [](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, int>) {
            return String("int: ") + ToString(arg);
        } else {
            return String("string: ") + arg;
        }
    };
    
    EXPECT_EQ(visit(visitor, var1), "int: 42");
    EXPECT_EQ(visit(visitor, var2), "string: hello");
}

// ============================================================================
// Variant Emplace Tests
// ============================================================================

TEST(VariantEmplaceTest, EmplaceByType)
{
    Variant<int, String, Vector<int>> var;
    
    var.emplace<String>("emplaced");
    EXPECT_TRUE(holds_alternative<String>(var));
    EXPECT_EQ(get<String>(var), "emplaced");
}

TEST(VariantEmplaceTest, EmplaceByIndex)
{
    Variant<int, String, Vector<int>> var;
    
    var.emplace<1>("test");
    EXPECT_TRUE(holds_alternative<String>(var));
    EXPECT_EQ(get<1>(var), "test");
}

TEST(VariantEmplaceTest, EmplaceComplexType)
{
    Variant<int, Vector<int>> var;
    
    var.emplace<Vector<int>>(5, 42);  // Vector of 5 elements, value 42
    EXPECT_TRUE(holds_alternative<Vector<int>>(var));
    EXPECT_EQ(get<Vector<int>>(var).size(), 5);
    EXPECT_EQ(get<Vector<int>>(var)[0], 42);
}

// ============================================================================
// Variant with Multiple Types
// ============================================================================

TEST(VariantMultiTypeTest, ThreeTypes)
{
    Variant<int, String, double> var;
    
    var = 42;
    EXPECT_EQ(get<int>(var), 42);
    
    var = String("test");
    EXPECT_EQ(get<String>(var), "test");
    
    var = 3.14;
    EXPECT_DOUBLE_EQ(get<double>(var), 3.14);
}

TEST(VariantMultiTypeTest, FourTypes)
{
    Variant<int, String, double, Vector<int>> var;
    
    var = Vector<int>{1, 2, 3};
    EXPECT_TRUE(holds_alternative<Vector<int>>(var));
    EXPECT_EQ(get<Vector<int>>(var).size(), 3);
}

// ============================================================================
// Variant Comparison Tests
// ============================================================================

TEST(VariantComparisonTest, EqualitySameType)
{
    Variant<int, String> var1(42);
    Variant<int, String> var2(42);
    Variant<int, String> var3(99);
    
    EXPECT_EQ(var1, var2);
    EXPECT_NE(var1, var3);
}

TEST(VariantComparisonTest, EqualityDifferentType)
{
    Variant<int, String> var1(42);
    Variant<int, String> var2(String("42"));
    
    EXPECT_NE(var1, var2);  // Different types
}

TEST(VariantComparisonTest, RelationalOperators)
{
    Variant<int, String> var1(10);
    Variant<int, String> var2(20);
    
    EXPECT_LT(var1, var2);
    EXPECT_LE(var1, var1);
    EXPECT_GT(var2, var1);
    EXPECT_GE(var2, var2);
}

// ============================================================================
// Variant Swap Tests
// ============================================================================

TEST(VariantSwapTest, SwapSameType)
{
    Variant<int, String> var1(42);
    Variant<int, String> var2(99);
    
    var1.swap(var2);
    
    EXPECT_EQ(get<int>(var1), 99);
    EXPECT_EQ(get<int>(var2), 42);
}

TEST(VariantSwapTest, SwapDifferentType)
{
    Variant<int, String> var1(42);
    Variant<int, String> var2(String("test"));
    
    var1.swap(var2);
    
    EXPECT_TRUE(holds_alternative<String>(var1));
    EXPECT_EQ(get<String>(var1), "test");
    EXPECT_TRUE(holds_alternative<int>(var2));
    EXPECT_EQ(get<int>(var2), 42);
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST(VariantOptionalIntegrationTest, OptionalOfVariant)
{
    Optional<Variant<int, String>> opt;
    
    opt = Variant<int, String>(42);
    EXPECT_TRUE(opt.has_value());
    EXPECT_TRUE(holds_alternative<int>(opt.value()));
    EXPECT_EQ(get<int>(opt.value()), 42);
}

TEST(VariantOptionalIntegrationTest, VariantOfOptional)
{
    Variant<Optional<int>, Optional<String>> var;
    
    var = Optional<int>(42);
    EXPECT_TRUE(holds_alternative<Optional<int>>(var));
    EXPECT_TRUE(get<Optional<int>>(var).has_value());
    EXPECT_EQ(get<Optional<int>>(var).value(), 42);
}

TEST(VariantOptionalIntegrationTest, ComplexNesting)
{
    using ComplexType = Variant<int, Optional<String>, Vector<double>>;
    
    ComplexType var1(42);
    EXPECT_TRUE(holds_alternative<int>(var1));
    
    ComplexType var2(MakeOptional<String>("test"));
    EXPECT_TRUE(holds_alternative<Optional<String>>(var2));
    EXPECT_TRUE(get<Optional<String>>(var2).has_value());
    
    ComplexType var3(Vector<double>{1.1, 2.2, 3.3});
    EXPECT_TRUE(holds_alternative<Vector<double>>(var3));
    EXPECT_EQ(get<Vector<double>>(var3).size(), 3);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST(VariantEdgeCaseTest, ZeroValue)
{
    Variant<int, String> var(0);
    EXPECT_TRUE(holds_alternative<int>(var));
    EXPECT_EQ(get<int>(var), 0);
}

TEST(OptionalEdgeCaseTest, ZeroValue)
{
    Optional<int> opt(0);
    EXPECT_TRUE(opt.has_value());
    EXPECT_EQ(opt.value(), 0);
}

TEST(OptionalEdgeCaseTest, EmptyString)
{
    Optional<String> opt(String(""));
    EXPECT_TRUE(opt.has_value());
    EXPECT_TRUE(opt->empty());
}
