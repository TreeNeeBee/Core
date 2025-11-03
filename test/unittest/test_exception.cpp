#include <gtest/gtest.h>
#include "CException.hpp"
#include "CCoreErrorDomain.hpp"

using namespace lap::core;

// A small test helper that exposes protected assignment for testing purposes.
class TestableException : public Exception {
public:
    using Exception::Exception; // inherit constructor
    // Expose protected operator= as public for testing
    using Exception::operator=;
};

TEST(ExceptionTest, CopyAssignmentPreservesError) {
    ErrorCode code = MakeErrorCode(CoreErrc::kInvalidArgument);
    TestableException a(code);

    TestableException b = a; // copy-construct
    // create another and use operator=
    TestableException c(MakeErrorCode(CoreErrc::kInvalidMetaModelPath));
    c = b; // uses protected operator= exposed as public

    EXPECT_STREQ(a.what(), b.what());
    EXPECT_STREQ(a.what(), c.what());
    EXPECT_EQ(a.Error().Value(), b.Error().Value());
    EXPECT_EQ(a.Error().Value(), c.Error().Value());
}
