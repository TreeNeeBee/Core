#include <gtest/gtest.h>
#include "CResult.hpp"
#include "CCoreErrorDomain.hpp"
#include "CString.hpp"

using namespace lap::core;

// Helpers
static Result<int> IncIfPositive(int x) {
    if (x >= 0) return Result<int>::FromValue(x + 1);
    return Result<int>::FromError(MakeErrorCode(CoreErrc::kInvalidArgument));
}

// (removed unused helper)

// AndThen tests
TEST(ResultAndThenTest, ValueChainsToValue) {
    auto r = Result<int>::FromValue(3);
    auto r2 = r.AndThen(IncIfPositive);
    EXPECT_TRUE(r2.HasValue());
    EXPECT_EQ(r2.Value(), 4);
}

TEST(ResultAndThenTest, ErrorShortCircuits) {
    auto e = Result<int>::FromError(MakeErrorCode(CoreErrc::kInvalidArgument));
    auto r2 = e.AndThen(IncIfPositive);
    EXPECT_FALSE(r2.HasValue());
    EXPECT_EQ(r2.Error().Value(), static_cast<ErrorDomain::CodeType>(CoreErrc::kInvalidArgument));
}

// OrElse tests
TEST(ResultOrElseTest, ValueBypassesFallback) {
    auto r = Result<int>::FromValue(10);
    auto r2 = r.OrElse([](const ErrorCode&) {
        return Result<int>::FromValue(42);
    });
    EXPECT_TRUE(r2.HasValue());
    EXPECT_EQ(r2.Value(), 10);
}

TEST(ResultOrElseTest, ErrorInvokesFallback) {
    auto e = Result<int>::FromError(MakeErrorCode(CoreErrc::kInvalidArgument));
    auto r2 = e.OrElse([](const ErrorCode&) {
        return Result<int>::FromValue(42);
    });
    EXPECT_TRUE(r2.HasValue());
    EXPECT_EQ(r2.Value(), 42);
}

// Match tests
TEST(ResultMatchTest, MatchOnValue) {
    auto r = Result<int>::FromValue(7);
    auto s = r.Match(
        [](int v) { return ToString(v); },
        [](const ErrorCode& e) { return ToString(e.Value()); }
    );
    EXPECT_EQ(s, String("7"));
}

TEST(ResultMatchTest, MatchOnError) {
    auto e = Result<int>::FromError(MakeErrorCode(CoreErrc::kInvalidMetaModelPath));
    auto s = e.Match(
        [](int v) { return ToString(v); },
        [](const ErrorCode& err) { return ToString(err.Value()); }
    );
    EXPECT_EQ(s, ToString(static_cast<ErrorDomain::CodeType>(CoreErrc::kInvalidMetaModelPath)));
}

// MapError tests
TEST(ResultMapErrorTest, TransformErrorType) {
    auto e = Result<int>::FromError(MakeErrorCode(CoreErrc::kInvalidArgument));
    auto r2 = e.MapError([](const ErrorCode& ec) {
        return ToString(ec.Value()); // map ErrorCode -> String
    });
    EXPECT_FALSE(r2.HasValue());
    EXPECT_EQ(r2.Error(), ToString(static_cast<ErrorDomain::CodeType>(CoreErrc::kInvalidArgument)));
}

TEST(ResultMapErrorTest, PreserveValueOnMapError) {
    auto r = Result<int>::FromValue(5);
    auto r2 = r.MapError([](const ErrorCode& ec) {
        return ToString(ec.Value());
    });
    EXPECT_TRUE(r2.HasValue());
    EXPECT_EQ(r2.Value(), 5);
}

// Resolve tests
TEST(ResultResolveTest, ResolveReturnsValue) {
    auto r = Result<int>::FromValue(9);
    int out = r.Resolve([](const ErrorCode&) { return -1; });
    EXPECT_EQ(out, 9);
}

TEST(ResultResolveTest, ResolveUsesFallbackOnError) {
    auto e = Result<int>::FromError(MakeErrorCode(CoreErrc::kInvalidArgument));
    int out = e.Resolve([](const ErrorCode&) { return 123; });
    EXPECT_EQ(out, 123);
}

// Ok / Err tests
TEST(ResultOptionalViewTest, OkErrAccess) {
    auto r = Result<int>::FromValue(3);
    auto ok = r.Ok();
    auto err = r.Err();
    EXPECT_TRUE(ok.has_value());
    EXPECT_FALSE(err.has_value());
    EXPECT_EQ(*ok, 3);

    auto e = Result<int>::FromError(MakeErrorCode(CoreErrc::kInvalidMetaModelPath));
    auto ok2 = e.Ok();
    auto err2 = e.Err();
    EXPECT_FALSE(ok2.has_value());
    EXPECT_TRUE(err2.has_value());
    EXPECT_EQ(err2->Value(), static_cast<ErrorDomain::CodeType>(CoreErrc::kInvalidMetaModelPath));
}

// CheckError and ValueOr/ErrorOr tests
TEST(ResultStateTest, CheckErrorAndValueOrErrorOr) {
    auto e = Result<int>::FromError(MakeErrorCode(CoreErrc::kInvalidArgument));
    EXPECT_TRUE(e.CheckError(MakeErrorCode(CoreErrc::kInvalidArgument)));
    EXPECT_FALSE(e.CheckError(MakeErrorCode(CoreErrc::kInvalidMetaModelShortname)));

    auto v = Result<int>::FromValue(21);
    EXPECT_EQ(v.ValueOr(99), 21);         // lvalue overload
    EXPECT_EQ(Result<int>::FromValue(5).ValueOr(7), 5); // rvalue overload

    // ErrorOr returns provided default when there is no error
    auto defaultErr = MakeErrorCode(CoreErrc::kInvalidMetaModelPath);
    auto gotErr = v.ErrorOr(defaultErr);
    EXPECT_EQ(gotErr.Value(), defaultErr.Value());
}

// Struct for testing multi-arg construction
struct Foo {
    int x{0};
    int y{0};
    int sum() const { return x + y; }
};

// EmplaceValue and EmplaceError tests (now that reset() is fixed)
TEST(ResultEmplaceTest, EmplaceValueReplacesError) {
    auto r = Result<int>::FromError(MakeErrorCode(CoreErrc::kInvalidArgument));
    EXPECT_FALSE(r.HasValue());
    
    r.EmplaceValue(42);
    EXPECT_TRUE(r.HasValue());
    EXPECT_EQ(r.Value(), 42);
}

TEST(ResultEmplaceTest, EmplaceErrorReplacesValue) {
    auto r = Result<int>::FromValue(10);
    EXPECT_TRUE(r.HasValue());
    
    r.EmplaceError(MakeErrorCode(CoreErrc::kInvalidMetaModelPath));
    EXPECT_FALSE(r.HasValue());
    EXPECT_EQ(r.Error().Value(), static_cast<ErrorDomain::CodeType>(CoreErrc::kInvalidMetaModelPath));
}

TEST(ResultEmplaceTest, EmplaceValueWithMultipleArgs) {
    // Test with int, which is simple
    auto r = Result<int>::FromError(MakeErrorCode(CoreErrc::kInvalidArgument));
    r.EmplaceValue(99);  // Single arg
    EXPECT_TRUE(r.HasValue());
    EXPECT_EQ(r.Value(), 99);
}

// Operator* and -> tests
TEST(ResultAccessTest, DereferenceAndArrow) {
    Result<Foo> r = Result<Foo>::FromValue(Foo{2, 3});
    EXPECT_EQ((*r).x, 2);
    EXPECT_EQ(r->sum(), 5);
}

// Void specialization tests
TEST(ResultVoidTest, FromValueAndError) {
    auto r = Result<void>::FromValue();
    EXPECT_TRUE(r.HasValue());

    auto e = Result<void>::FromError(MakeErrorCode(CoreErrc::kInvalidArgument));
    EXPECT_FALSE(e.HasValue());
    EXPECT_EQ(e.Error().Value(), static_cast<ErrorDomain::CodeType>(CoreErrc::kInvalidArgument));
}

TEST(ResultVoidTest, ValueOrThrowThrowsOnError) {
    auto e = Result<void>::FromError(MakeErrorCode(CoreErrc::kInvalidArgument));
    try {
        e.ValueOrThrow();
        FAIL() << "Expected exception";
    } catch (const CoreException &ex) {
        EXPECT_STREQ(ex.what(), "An invalid argument was passed to a function");
    } catch (...) {
        FAIL() << "Unexpected exception type";
    }
}

// TRY macro tests
static Result<int> parse_positive(int v) {
    if (v < 0) return Result<int>::FromError(MakeErrorCode(CoreErrc::kInvalidArgument));
    return Result<int>::FromValue(v);
}

static Result<int> sum_two(int a, int b) {
    int x = TRY(parse_positive(a));
    int y = TRY(parse_positive(b));
    return Result<int>::FromValue(x + y);
}

TEST(ResultTryMacroTest, PropagatesError) {
    auto ok = sum_two(2, 3);
    EXPECT_TRUE(ok.HasValue());
    EXPECT_EQ(ok.Value(), 5);

    auto bad = sum_two(-1, 3);
    EXPECT_FALSE(bad.HasValue());
    EXPECT_EQ(bad.Error().Value(), static_cast<ErrorDomain::CodeType>(CoreErrc::kInvalidArgument));
}
