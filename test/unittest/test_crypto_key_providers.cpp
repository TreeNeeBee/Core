#include <gtest/gtest.h>
#include <fstream>
#include "CCrypto.hpp"

using namespace lap::core;

class CryptoKeyProviderTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Ensure clean providers before each test
        Crypto::ClearKeyProviders();
        // Unset env to ensure not interfering
        unsetenv(Crypto::ENV_HMAC_SECRET);
        // Prepare a temp key file path
        tempPath_ = "test_hmac.key";
        std::remove(tempPath_.c_str());
    }
    void TearDown() override {
        Crypto::ClearKeyProviders();
        std::remove(tempPath_.c_str());
    }
    std::string tempPath_;
};

TEST_F(CryptoKeyProviderTest, EnvFallbackWhenNoProviders) {
    // With no providers and no env, constructor will fallback to builtin (lenient mode)
    Crypto c;
    EXPECT_TRUE(c.hasKey());
}

TEST_F(CryptoKeyProviderTest, FileProviderUsedWhenSet) {
    // Write a key to file
    {
        std::ofstream ofs(tempPath_);
        ofs << "key-from-file-123456" << std::endl;
    }
    Crypto::SetKeyFilePath(tempPath_);
    Crypto c;
    EXPECT_TRUE(c.hasKey());
    auto h = c.computeHmac("data");
    EXPECT_FALSE(h.empty());
}

TEST_F(CryptoKeyProviderTest, CallbackProviderOverridesFile) {
    // Provide callback that returns a key
    Crypto::SetKeyFetchCallback([]() -> Optional<String> {
        return Optional<String>(String("callback-secret-abcdef"));
    });
    // Also set a file; callback should take precedence
    {
        std::ofstream ofs(tempPath_);
        ofs << "file-secret-should-not-be-used" << std::endl;
    }
    Crypto::SetKeyFilePath(tempPath_);

    Crypto c;
    auto h = c.computeHmac("hello");
    EXPECT_FALSE(h.empty());
}

TEST_F(CryptoKeyProviderTest, EnvIsUsedIfProvidersEmpty) {
    setenv(Crypto::ENV_HMAC_SECRET, "env-secret-xyzxyzxyz", 1);
    Crypto c;
    auto h = c.computeHmac("world");
    EXPECT_FALSE(h.empty());
}
