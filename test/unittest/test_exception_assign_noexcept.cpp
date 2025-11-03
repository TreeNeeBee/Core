#include <gtest/gtest.h>
#include <type_traits>
#include <utility>
#include "CException.hpp"
#include "CCoreErrorDomain.hpp"

using namespace lap::core;

// Local test helper: expose protected operator= for testing
class TestableException : public Exception {
public:
    using Exception::Exception;
    using Exception::operator=;
};

TEST(ExceptionAssignTest, CopyAssignmentPreservesValueAndIsNoexcept) {
    TestableException a(MakeErrorCode(CoreErrc::kInvalidArgument));
    TestableException b(MakeErrorCode(CoreErrc::kInvalidMetaModelPath));

    // Check noexcept property at compile time
    static_assert(noexcept(std::declval<TestableException&>() = std::declval<const TestableException&>()),
                  "Exception::operator= must be noexcept");

    b = a; // assignment

    EXPECT_EQ(a.Error().Value(), b.Error().Value());
    EXPECT_STREQ(a.what(), b.what());
}
