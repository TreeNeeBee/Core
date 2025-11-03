#include <gtest/gtest.h>
#include "CPromise.hpp"
#include "CFuture.hpp"
#include "CFutureErrorDomain.hpp"

using namespace lap::core;

TEST(PromiseTest, SetErrorAndSetResult) {
    Promise<int> p;
    auto f = p.get_future();

    // Set an error by value
    p.SetError( MakeErrorCode(future_errc::promise_already_satisfied) );

    auto r = f.GetResult();
    EXPECT_FALSE(r.HasValue());
    EXPECT_EQ(r.Error().Value(), static_cast<ErrorDomain::CodeType>(future_errc::promise_already_satisfied));

    // New promise/future pair to test SetResult
    Promise<int> p2;
    auto f2 = p2.get_future();
    p2.SetResult( Result<int, ErrorCode>( 123 ) );
    auto r2 = f2.GetResult();
    EXPECT_TRUE(r2.HasValue());
    EXPECT_EQ(r2.Value(), 123);
}
