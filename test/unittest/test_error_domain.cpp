#include <gtest/gtest.h>
#include "CCoreErrorDomain.hpp"

using namespace lap::core;

TEST(CoreErrorDomainTest, MessageForKnownCodes) {
    EXPECT_STREQ(GetCoreErrorDomain().Message(static_cast<ErrorDomain::CodeType>(CoreErrc::kInvalidArgument)), "An invalid argument was passed to a function");
    EXPECT_STREQ(GetCoreErrorDomain().Message(static_cast<ErrorDomain::CodeType>(CoreErrc::kInvalidMetaModelShortname)), "Given string is not a valid model element shortname");
    EXPECT_STREQ(GetCoreErrorDomain().Message(static_cast<ErrorDomain::CodeType>(CoreErrc::kInvalidMetaModelPath)), "Missing or invalid path to model element");
}

TEST(CoreErrorDomainTest, ThrowAsException) {
    ErrorCode code = MakeErrorCode(CoreErrc::kInvalidArgument);
    try {
        GetCoreErrorDomain().ThrowAsException(code);
        FAIL() << "Expected exception";
    } catch (const CoreException& ex) {
        EXPECT_STREQ(ex.what(), "An invalid argument was passed to a function");
    } catch (...) {
        FAIL() << "Unexpected exception type";
    }
}

// Note: main is provided by the test_sync.cpp runner; do not define main here.
