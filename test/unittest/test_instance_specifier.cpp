#include <gtest/gtest.h>
#include "CInstanceSpecifier.hpp"

using namespace lap::core;

class InstanceSpecifierTest : public ::testing::Test {
protected:
    void SetUp() override {
        validIdentifier = "valid/meta_model";
        invalidIdentifier = "invalid@meta_model";
    }

    std::string validIdentifier;
    std::string invalidIdentifier;
};

TEST_F(InstanceSpecifierTest, Constructor_ValidIdentifier) {
    EXPECT_NO_THROW({ InstanceSpecifier s(validIdentifier); });
}

TEST_F(InstanceSpecifierTest, Constructor_InvalidIdentifier) {
    EXPECT_THROW({ InstanceSpecifier s(invalidIdentifier); }, CoreException);
}

TEST_F(InstanceSpecifierTest, Create_ValidIdentifier) {
    auto result = InstanceSpecifier::Create(validIdentifier);
    EXPECT_TRUE(result.HasValue());
    EXPECT_EQ(result.Value().ToString(), validIdentifier);
}

TEST_F(InstanceSpecifierTest, Create_InvalidIdentifier) {
    auto result = InstanceSpecifier::Create(invalidIdentifier);
    EXPECT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error(), CoreErrc::kInvalidMetaModelPath);
}

TEST_F(InstanceSpecifierTest, EqualityOperators) {
    InstanceSpecifier specifier1(validIdentifier);
    InstanceSpecifier specifier2(validIdentifier);
    EXPECT_TRUE(specifier1 == specifier2);
    EXPECT_FALSE(specifier1 != specifier2);
}

TEST_F(InstanceSpecifierTest, InequalityOperators) {
    InstanceSpecifier specifier1(validIdentifier);
    InstanceSpecifier specifier2("different/meta_model");
    EXPECT_TRUE(specifier1 != specifier2);
    EXPECT_FALSE(specifier1 == specifier2);
}