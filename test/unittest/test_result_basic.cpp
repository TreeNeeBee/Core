#include <gtest/gtest.h>
#include "CResult.hpp"
#include "CCoreErrorDomain.hpp"

using namespace lap::core;

TEST(ResultBasicTest, ValueAndError) {
    auto r = Result<int>::FromValue(10);
    EXPECT_TRUE(r.HasValue());
    EXPECT_EQ(r.Value(), 10);

    auto e = Result<int>::FromError( MakeErrorCode(CoreErrc::kInvalidArgument) );
    EXPECT_FALSE(e.HasValue());
    EXPECT_EQ(e.Error().Value(), static_cast<ErrorDomain::CodeType>(CoreErrc::kInvalidArgument));
}

TEST(ResultBasicTest, ValueOrAndErrorOr) {
    auto r = Result<int>::FromValue(5);
    EXPECT_EQ(r.ValueOr(7), 5);

    auto e = Result<int>::FromError( MakeErrorCode(CoreErrc::kInvalidArgument) );
    EXPECT_EQ(e.ValueOr(7), 7);

    EXPECT_EQ(r.ErrorOr(MakeErrorCode(CoreErrc::kInvalidArgument)).Value(), static_cast<ErrorDomain::CodeType>(CoreErrc::kInvalidArgument));
}

TEST(ResultBasicTest, Map) {
    auto r = Result<int>::FromValue(3);
    auto r2 = r.Map([](int v){ return v * 2; });
    EXPECT_TRUE(r2.HasValue());
    EXPECT_EQ(r2.Value(), 6);

    auto e = Result<int>::FromError( MakeErrorCode(CoreErrc::kInvalidArgument) );
    auto e2 = e.Map([](int v){ return v * 2; });
    EXPECT_FALSE(e2.HasValue());
    EXPECT_EQ(e2.Error().Value(), static_cast<ErrorDomain::CodeType>(CoreErrc::kInvalidArgument));
}

TEST(ResultBasicTest, ValueOrThrow) {
    auto r = Result<int>::FromValue(9);
    EXPECT_EQ(r.ValueOrThrow(), 9);

    auto e = Result<int>::FromError( MakeErrorCode(CoreErrc::kInvalidArgument) );
    try {
        e.ValueOrThrow();
        FAIL() << "Expected exception";
    } catch (const CoreException &ex) {
        EXPECT_STREQ(ex.what(), "An invalid argument was passed to a function");
    } catch (...) {
        FAIL() << "Unexpected exception type";
    }
}
