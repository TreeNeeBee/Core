/**
 * @file        config_test.cpp
 * @brief       Configuration Manager Unit Tests
 * @date        2025-10-31
 */

#include <gtest/gtest.h>
#include <fstream>

// For unit testing: redefine private to public to access private methods
// MUST be placed after system headers but before target headers
#define private public
#define protected public

#include "CConfig.hpp"

#undef private
#undef protected

using namespace lap::core;

class ConfigTest : public ::testing::Test {
protected:
    void SetUp() override {
        testConfigPath_ = "test_config.json";
        testEncryptedPath_ = "test_encrypted.json";
        
        // Clean up any existing test files
        std::remove(testConfigPath_.c_str());
        std::remove(testEncryptedPath_.c_str());
    }
    
    void TearDown() override {
        // Clean up test files
        std::remove(testConfigPath_.c_str());
        std::remove(testEncryptedPath_.c_str());
    }
    
    String testConfigPath_;
    String testEncryptedPath_;
};

TEST_F(ConfigTest, Initialization) {
    ConfigManager& config = ConfigManager::GetInstance();
    
    auto result = config.Initialize(testConfigPath_, false);
    EXPECT_TRUE(result.HasValue());
    
    config.Clear();
}

TEST_F(ConfigTest, SetAndGetBool) {
    ConfigManager& config = ConfigManager::GetInstance();
    config.Initialize(testConfigPath_, false);
    
    auto setResult = config.SetBool("test.bool_value", true);
    EXPECT_TRUE(setResult.HasValue());
    
    Bool value = config.GetBool("test.bool_value");
    EXPECT_TRUE(value);
    
    Bool defaultValue = config.GetBool("test.nonexistent", false);
    EXPECT_FALSE(defaultValue);
    
    config.Clear();
}

TEST_F(ConfigTest, SetAndGetInt) {
    ConfigManager& config = ConfigManager::GetInstance();
    config.Initialize(testConfigPath_, false);
    
    auto setResult = config.SetInt("test.int_value", 12345);
    EXPECT_TRUE(setResult.HasValue());
    
    Int64 value = config.GetInt("test.int_value");
    EXPECT_EQ(value, 12345);
    
    Int64 defaultValue = config.GetInt("test.nonexistent", 999);
    EXPECT_EQ(defaultValue, 999);
    
    config.Clear();
}

TEST_F(ConfigTest, SetAndGetDouble) {
    ConfigManager& config = ConfigManager::GetInstance();
    config.Initialize(testConfigPath_, false);
    
    auto setResult = config.SetDouble("test.double_value", 3.14159);
    EXPECT_TRUE(setResult.HasValue());
    
    Double value = config.GetDouble("test.double_value");
    EXPECT_NEAR(value, 3.14159, 0.00001);
    
    config.Clear();
}

TEST_F(ConfigTest, SetAndGetString) {
    ConfigManager& config = ConfigManager::GetInstance();
    config.Initialize(testConfigPath_, false);
    
    auto setResult = config.SetString("test.string_value", "Hello, World!");
    EXPECT_TRUE(setResult.HasValue());
    
    String value = config.GetString("test.string_value");
    EXPECT_EQ(value, "Hello, World!");
    
    String defaultValue = config.GetString("test.nonexistent", "default");
    EXPECT_EQ(defaultValue, "default");
    
    config.Clear();
}

TEST_F(ConfigTest, HierarchicalKeys) {
    ConfigManager& config = ConfigManager::GetInstance();
    config.Initialize(testConfigPath_, false);
    
    config.SetInt("network.port", 8080);
    config.SetString("network.interface", "eth0");
    config.SetBool("network.enabled", true);
    
    EXPECT_EQ(config.GetInt("network.port"), 8080);
    EXPECT_EQ(config.GetString("network.interface"), "eth0");
    EXPECT_TRUE(config.GetBool("network.enabled"));
    
    config.Clear();
}

TEST_F(ConfigTest, KeyExistence) {
    ConfigManager& config = ConfigManager::GetInstance();
    config.Initialize(testConfigPath_, false);
    
    config.SetInt("test.value", 123);
    
    EXPECT_TRUE(config.Exists("test.value"));
    EXPECT_FALSE(config.Exists("test.nonexistent"));
    
    config.Clear();
}

TEST_F(ConfigTest, SaveAndLoad) {
    ConfigManager& config = ConfigManager::GetInstance();
    config.Initialize(testConfigPath_, false);
    
    // Set values
    config.SetInt("network.port", 8080);
    config.SetString("database.host", "localhost");
    config.SetBool("logging.enabled", true);
    
    // Note: save() is private but accessible in unit test via #define private public
    auto saveResult = config.save();
    EXPECT_TRUE(saveResult.HasValue());
    
    // Verify file was created
    std::ifstream file(testConfigPath_);
    EXPECT_TRUE(file.good());
    file.close();
    
    // Clear and reload
    config.Clear();
    auto loadResult = config.Load();
    EXPECT_TRUE(loadResult.HasValue());
    
    // Verify values
    EXPECT_EQ(config.GetInt("network.port"), 8080);
    EXPECT_EQ(config.GetString("database.host"), "localhost");
    EXPECT_TRUE(config.GetBool("logging.enabled"));
    
    config.Clear();
}

TEST_F(ConfigTest, BackupAndRollback) {
    ConfigManager& config = ConfigManager::GetInstance();
    config.Initialize(testConfigPath_, false);
    
    // Set initial value
    config.SetInt("test.value", 100);
    EXPECT_EQ(config.GetInt("test.value"), 100);
    
    // Create backup
    auto backupResult = config.CreateBackup();
    EXPECT_TRUE(backupResult.HasValue());
    
    // Modify value
    config.SetInt("test.value", 200);
    EXPECT_EQ(config.GetInt("test.value"), 200);
    
    // Rollback
    auto rollbackResult = config.Rollback();
    EXPECT_TRUE(rollbackResult.HasValue());
    
    // Verify restored value
    EXPECT_EQ(config.GetInt("test.value"), 100);
    
    config.Clear();
}

TEST_F(ConfigTest, ChangeCallback) {
    ConfigManager& config = ConfigManager::GetInstance();
    config.Initialize(testConfigPath_, false);
    
    Bool callbackTriggered = false;
    String capturedKey;
    Int64 capturedNewValue = 0;
    
    auto callback = [&](const String& key, const ConfigValue& /* oldValue */, const ConfigValue& newValue) {
        callbackTriggered = true;
        capturedKey = key;
        capturedNewValue = newValue.AsInt();
    };
    
    // Register callback for network keys
    UInt32 callbackId = config.RegisterChangeCallback("network", callback);
    
    // Modify a network key
    config.SetInt("network.port", 9090);
    
    EXPECT_TRUE(callbackTriggered);
    EXPECT_EQ(capturedKey, "network.port");
    EXPECT_EQ(capturedNewValue, 9090);
    
    // Unregister callback
    config.UnregisterChangeCallback(callbackId);
    
    // Reset flag
    callbackTriggered = false;
    
    // Modify again - callback should not be triggered
    config.SetInt("network.port", 8080);
    EXPECT_FALSE(callbackTriggered);
    
    config.Clear();
}

TEST_F(ConfigTest, ChangeCallbackOldAndNewValues) {
    ConfigManager& config = ConfigManager::GetInstance();
    config.Initialize(testConfigPath_, false);

    // Seed an initial value
    config.SetInt("network.port", 8000);

    Bool callbackTriggered = false;
    Int64 oldV = -1;
    Int64 newV = -1;
    auto cb = [&](const String& key, const ConfigValue& oldValue, const ConfigValue& newValue){
        if (key == "network.port") {
            callbackTriggered = true;
            oldV = oldValue.AsInt(-1);
            newV = newValue.AsInt(-1);
        }
    };
    UInt32 id = config.RegisterChangeCallback("network", cb);

    // Change value
    config.SetInt("network.port", 8100);

    EXPECT_TRUE(callbackTriggered);
    EXPECT_EQ(oldV, 8000);
    EXPECT_EQ(newV, 8100);

    config.UnregisterChangeCallback(id);
    config.Clear();
}

TEST_F(ConfigTest, PolicyPersistenceInJson) {
    ConfigManager& config = ConfigManager::GetInstance();
    config.Initialize(testConfigPath_, false);

    // Create some module configs
    nlohmann::json modA; modA["v"] = 1;
    nlohmann::json modB; modB["v"] = 2;
    config.SetModuleConfigJson("modA", modA);
    config.SetModuleConfigJson("modB", modB);

    // Set explicit policies
    config.SetModuleUpdatePolicy("modA", "first");
    config.SetModuleUpdatePolicy("modB", "always");

    // Save (private method exposed via macro at top of file)
    auto saveResult = config.save(true);
    EXPECT_TRUE(saveResult.HasValue());

    // Read file and verify __update_policy__ mapping
    std::ifstream ifs(testConfigPath_);
    ASSERT_TRUE(ifs.good());
    std::stringstream buffer; buffer << ifs.rdbuf();
    auto saved = nlohmann::json::parse(buffer.str());
    ASSERT_TRUE(saved.contains("__update_policy__"));
    const auto& pol = saved["__update_policy__"];
    ASSERT_TRUE(pol.is_object());
    EXPECT_EQ(pol["modA"], "first");
    EXPECT_EQ(pol["modB"], "always");
    // default key must exist
    ASSERT_TRUE(pol.contains("default"));

    config.Clear();
}

TEST_F(ConfigTest, VerificationFailsOnTamper) {
    ConfigManager& config = ConfigManager::GetInstance();
    setenv("HMAC_SECRET", "test-secret-key-32-bytes-long!", 1);
    config.Initialize(testConfigPath_, true);

    config.SetString("secure.value", "original");
    auto saveResult = config.save(true);
    EXPECT_TRUE(saveResult.HasValue());

    // Tamper the core JSON content (not metadata) and rewrite file
    std::ifstream ifs1(testConfigPath_);
    ASSERT_TRUE(ifs1.good());
    nlohmann::json j = nlohmann::json::parse(ifs1);
    ifs1.close();
    j["secure"]["value"] = "tampered"; // change core data
    std::ofstream ofs(testConfigPath_);
    ofs << j.dump(4);
    ofs.close();

    // Reload with verification -> should fail (HMAC/CRC mismatch)
    config.Clear();
    config.Initialize(testConfigPath_, true);
    auto loadResult = config.Load(false);
    EXPECT_FALSE(loadResult.HasValue());

    config.Clear();
    unsetenv("HMAC_SECRET");
}

TEST_F(ConfigTest, ConfigValueFromJsonString) {
    // Array
    ConfigValue arr = ConfigValue::FromJsonString("[1,2,3]");
    EXPECT_TRUE(arr.IsArray());
    EXPECT_EQ(arr.ArraySize(), 3u);
    EXPECT_EQ(arr[0].AsInt(), 1);
    EXPECT_EQ(arr[1].AsInt(), 2);
    EXPECT_EQ(arr[2].AsInt(), 3);

    // Object
    ConfigValue obj = ConfigValue::FromJsonString("{\"a\":true,\"b\":\"x\"}");
    EXPECT_TRUE(obj.IsObject());
    EXPECT_TRUE(obj.HasKey("a"));
    EXPECT_TRUE(obj.HasKey("b"));
    EXPECT_TRUE(obj["a"].AsBool());
    EXPECT_EQ(obj["b"].AsString(), "x");
}

TEST_F(ConfigTest, GetReturnsArraysAndObjects) {
    ConfigManager& config = ConfigManager::GetInstance();
    config.Initialize(testConfigPath_, false);

    nlohmann::json arr = nlohmann::json::array({1,2,3});
    nlohmann::json obj; obj["k1"]=1; obj["k2"]=2;
    config.SetModuleConfigJson("amod", arr);
    config.SetModuleConfigJson("omod", obj);

    auto a = config.Get("amod");
    ASSERT_TRUE(a.has_value());
    EXPECT_TRUE(a.value().IsArray());
    EXPECT_EQ(a.value().ArraySize(), 3u);

    auto o = config.Get("omod");
    ASSERT_TRUE(o.has_value());
    EXPECT_TRUE(o.value().IsObject());
    EXPECT_TRUE(o.value().HasKey("k1"));
    EXPECT_EQ(o.value()["k1"].AsInt(), 1);

    config.Clear();
}

TEST_F(ConfigTest, Metadata) {
    ConfigManager& config = ConfigManager::GetInstance();
    config.Initialize(testConfigPath_, true);  // Enable security to generate CRC and timestamp
    
    config.SetInt("test.value", 123);
    // Use private access to call save() before getting metadata
    auto saveResult = config.save(true);
    EXPECT_TRUE(saveResult.HasValue());
    
    ConfigMetadata metadata = config.GetMetadata();
    
    EXPECT_GT(metadata.version, 0u);
    EXPECT_FALSE(metadata.crc.empty());
    EXPECT_FALSE(metadata.timestamp.empty());
    EXPECT_FALSE(metadata.encrypted);
    
    config.Clear();
}

TEST_F(ConfigTest, JsonExport) {
    ConfigManager& config = ConfigManager::GetInstance();
    config.Initialize(testConfigPath_, false);
    
    config.SetInt("network.port", 8080);
    config.SetString("database.host", "localhost");
    config.SetBool("logging.enabled", true);
    
    String json = config.ToJson(true);
    
    EXPECT_FALSE(json.empty());
    EXPECT_NE(json.find("network"), String::npos);
    EXPECT_NE(json.find("database"), String::npos);
    EXPECT_NE(json.find("logging"), String::npos);
    
    config.Clear();
}

TEST_F(ConfigTest, ConfigValueTypes) {
    ConfigValue boolVal(true);
    EXPECT_TRUE(boolVal.IsBool());
    EXPECT_TRUE(boolVal.AsBool());
    
    ConfigValue intVal(static_cast<Int64>(42));
    EXPECT_TRUE(intVal.IsInt());
    EXPECT_EQ(intVal.AsInt(), 42);
    
    ConfigValue doubleVal(3.14);
    EXPECT_TRUE(doubleVal.IsDouble());
    EXPECT_NEAR(doubleVal.AsDouble(), 3.14, 0.001);
    
    ConfigValue stringVal("test");
    EXPECT_TRUE(stringVal.IsString());
    EXPECT_EQ(stringVal.AsString(), "test");
    
    ConfigValue nullVal;
    EXPECT_TRUE(nullVal.IsNull());
}

TEST_F(ConfigTest, ConfigValueArray) {
    ConfigValue arrayVal;
    arrayVal.Append(ConfigValue(static_cast<Int64>(1)));
    arrayVal.Append(ConfigValue(static_cast<Int64>(2)));
    arrayVal.Append(ConfigValue(static_cast<Int64>(3)));
    
    EXPECT_TRUE(arrayVal.IsArray());
    EXPECT_EQ(arrayVal.ArraySize(), 3u);
    EXPECT_EQ(arrayVal[0].AsInt(), 1);
    EXPECT_EQ(arrayVal[1].AsInt(), 2);
    EXPECT_EQ(arrayVal[2].AsInt(), 3);
}

TEST_F(ConfigTest, ConfigValueObject) {
    ConfigValue objVal;
    objVal["name"] = ConfigValue("test");
    objVal["value"] = ConfigValue(static_cast<Int64>(123));
    objVal["enabled"] = ConfigValue(true);
    
    EXPECT_TRUE(objVal.IsObject());
    EXPECT_TRUE(objVal.HasKey("name"));
    EXPECT_TRUE(objVal.HasKey("value"));
    EXPECT_TRUE(objVal.HasKey("enabled"));
    EXPECT_FALSE(objVal.HasKey("nonexistent"));
    
    EXPECT_EQ(objVal["name"].AsString(), "test");
    EXPECT_EQ(objVal["value"].AsInt(), 123);
    EXPECT_TRUE(objVal["enabled"].AsBool());
}

// ============================================================================
// Update Policy Tests (moved from examples)
// ============================================================================

TEST_F(ConfigTest, DefaultUpdatePolicy) {
    ConfigManager& config = ConfigManager::GetInstance();
    config.Initialize(testConfigPath_, false);
    
    // Module without explicit policy should use default (on_change)
    auto policy = config.GetModuleUpdatePolicy("newModule");
    EXPECT_EQ(policy, ConfigManager::UpdatePolicy::kOnChangeUpdate);
    
    config.Clear();
}

TEST_F(ConfigTest, SetModuleUpdatePolicy) {
    ConfigManager& config = ConfigManager::GetInstance();
    config.Initialize(testConfigPath_, false);
    
    // Set different policies
    auto result1 = config.SetModuleUpdatePolicy("modA", ConfigManager::UpdatePolicy::kFirstUpdate);
    EXPECT_TRUE(result1.HasValue());
    
    auto result2 = config.SetModuleUpdatePolicy("modB", ConfigManager::UpdatePolicy::kAlwaysUpdate);
    EXPECT_TRUE(result2.HasValue());
    
    auto result3 = config.SetModuleUpdatePolicy("modC", ConfigManager::UpdatePolicy::kNoUpdate);
    EXPECT_TRUE(result3.HasValue());
    
    // Verify policies
    EXPECT_EQ(config.GetModuleUpdatePolicy("modA"), ConfigManager::UpdatePolicy::kFirstUpdate);
    EXPECT_EQ(config.GetModuleUpdatePolicy("modB"), ConfigManager::UpdatePolicy::kAlwaysUpdate);
    EXPECT_EQ(config.GetModuleUpdatePolicy("modC"), ConfigManager::UpdatePolicy::kNoUpdate);
    
    config.Clear();
}

TEST_F(ConfigTest, SetModuleUpdatePolicyByString) {
    ConfigManager& config = ConfigManager::GetInstance();
    config.Initialize(testConfigPath_, false);
    
    auto result1 = config.SetModuleUpdatePolicy("modA", "first");
    EXPECT_TRUE(result1.HasValue());
    
    auto result2 = config.SetModuleUpdatePolicy("modB", "always");
    EXPECT_TRUE(result2.HasValue());
    
    auto result3 = config.SetModuleUpdatePolicy("modC", "none");
    EXPECT_TRUE(result3.HasValue());
    
    auto result4 = config.SetModuleUpdatePolicy("modD", "on_change");
    EXPECT_TRUE(result4.HasValue());
    
    // Invalid policy string
    auto result5 = config.SetModuleUpdatePolicy("modE", "invalid");
    EXPECT_FALSE(result5.HasValue());
    
    // Verify
    EXPECT_EQ(config.GetModuleUpdatePolicy("modA"), ConfigManager::UpdatePolicy::kFirstUpdate);
    EXPECT_EQ(config.GetModuleUpdatePolicy("modB"), ConfigManager::UpdatePolicy::kAlwaysUpdate);
    EXPECT_EQ(config.GetModuleUpdatePolicy("modC"), ConfigManager::UpdatePolicy::kNoUpdate);
    EXPECT_EQ(config.GetModuleUpdatePolicy("modD"), ConfigManager::UpdatePolicy::kOnChangeUpdate);
    
    config.Clear();
}

TEST_F(ConfigTest, ModuleConfigJson) {
    ConfigManager& config = ConfigManager::GetInstance();
    config.Initialize(testConfigPath_, false);
    
    // Set module config
    nlohmann::json moduleConfig;
    moduleConfig["host"] = "localhost";
    moduleConfig["port"] = 8080;
    moduleConfig["enabled"] = true;
    
    auto result = config.SetModuleConfigJson("database", moduleConfig);
    EXPECT_TRUE(result.HasValue());
    
    // Get module config
    nlohmann::json retrieved = config.GetModuleConfigJson("database");
    EXPECT_EQ(retrieved["host"], "localhost");
    EXPECT_EQ(retrieved["port"], 8080);
    EXPECT_TRUE(retrieved["enabled"]);
    
    config.Clear();
}

TEST_F(ConfigTest, SetModuleConfigJsonUpdatesPolicy) {
    ConfigManager& config = ConfigManager::GetInstance();
    config.Initialize(testConfigPath_, false);
    
    nlohmann::json moduleConfig;
    moduleConfig["value"] = 123;
    
    config.SetModuleConfigJson("testModule", moduleConfig);
    
    // Should auto-set policy to default (on_change)
    auto policy = config.GetModuleUpdatePolicy("testModule");
    EXPECT_EQ(policy, ConfigManager::UpdatePolicy::kOnChangeUpdate);
    
    config.Clear();
}

// ============================================================================
// Verification Tests (moved from examples)
// ============================================================================


TEST_F(ConfigTest, SkipVerificationOnLoad) {
    ConfigManager& config = ConfigManager::GetInstance();
    config.Initialize(testConfigPath_, true);
    
    // Set some data
    config.SetInt("test.value", 12345);
    config.SetString("test.name", "verification_test");
    
    // Save using private method (accessible via #define private public)
    auto saveResult = config.save(true);
    EXPECT_TRUE(saveResult.HasValue());
    
    // Clear and reload with skipVerification=true
    config.Clear();
    config.Initialize(testConfigPath_, true);
    auto loadResult = config.Load(true);  // Skip verification
    EXPECT_TRUE(loadResult.HasValue());
    
    EXPECT_EQ(config.GetInt("test.value"), 12345);
    EXPECT_EQ(config.GetString("test.name"), "verification_test");
    
    config.Clear();
}

TEST_F(ConfigTest, VerificationWithCorrectHMAC) {
    ConfigManager& config = ConfigManager::GetInstance();
    
    // Set HMAC secret
    setenv("HMAC_SECRET", "test-secret-key-32-bytes-long!", 1);
    
    config.Initialize(testConfigPath_, true);
    config.SetInt("secure.value", 9999);
    
    // Save using private method (accessible via #define private public)
    auto saveResult = config.save(true);
    EXPECT_TRUE(saveResult.HasValue());
    
    // Clear and reload with verification
    config.Clear();
    config.Initialize(testConfigPath_, true);
    auto loadResult = config.Load(false);  // Enforce verification
    EXPECT_TRUE(loadResult.HasValue());
    
    EXPECT_EQ(config.GetInt("secure.value"), 9999);
    
    config.Clear();
    unsetenv("HMAC_SECRET");
}

// Test private save() method directly
TEST_F(ConfigTest, PrivateSaveMethod) {
    ConfigManager& config = ConfigManager::GetInstance();
    config.Initialize(testConfigPath_, true);
    
    config.SetInt("test.value", 42);
    
    // Access private save() method
    auto result = config.save(true);
    EXPECT_TRUE(result.HasValue());
    
    // Verify file was created
    std::ifstream file(testConfigPath_);
    EXPECT_TRUE(file.good());
    file.close();
    
    config.Clear();
}

// Test private member variables
TEST_F(ConfigTest, PrivateMemberAccess) {
    ConfigManager& config = ConfigManager::GetInstance();
    config.Initialize(testConfigPath_, false);
    
    config.SetInt("test.value", 100);
    
    // Access private members directly
    EXPECT_FALSE(config.configData_.empty());
    EXPECT_TRUE(config.initialized_);
    EXPECT_EQ(config.configPath_, testConfigPath_);
    
    config.Clear();
}

// Test internal CRC computation
TEST_F(ConfigTest, InternalCrcComputation) {
    ConfigManager& config = ConfigManager::GetInstance();
    config.Initialize(testConfigPath_, true);
    
    config.SetInt("test.value", 123);
    
    // Use Crypto::Util::computeCrc32 (CRC computation moved to Crypto utility)
    String testData = "test data for CRC";
    UInt32 crc = Crypto::Util::computeCrc32(testData);
    EXPECT_GT(crc, 0u);
    
    // Same data should produce same CRC
    UInt32 crc2 = Crypto::Util::computeCrc32(testData);
    EXPECT_EQ(crc, crc2);
    
    // Different data should produce different CRC
    String differentData = "different test data";
    UInt32 crc3 = Crypto::Util::computeCrc32(differentData);
    EXPECT_NE(crc, crc3);
    
    config.Clear();
}

// Test internal policy refresh
TEST_F(ConfigTest, InternalPolicyRefresh) {
    ConfigManager& config = ConfigManager::GetInstance();
    config.Initialize(testConfigPath_, false);
    
    // Set some module configs
    nlohmann::json modConfig;
    modConfig["value"] = 1;
    config.SetModuleConfigJson("testMod", modConfig);
    
    // Access private method to refresh policies
    config.refreshPoliciesFromConfigLocked();
    
    // Verify policies were loaded
    auto policy = config.GetModuleUpdatePolicy("testMod");
    EXPECT_EQ(policy, ConfigManager::UpdatePolicy::kOnChangeUpdate);
    
    config.Clear();
}

// Test module CRC computation
TEST_F(ConfigTest, ModuleCrcComputation) {
    ConfigManager& config = ConfigManager::GetInstance();
    config.Initialize(testConfigPath_, false);
    
    nlohmann::json moduleData;
    moduleData["key1"] = "value1";
    moduleData["key2"] = 42;
    
    // Access private computeModuleCrcLocked method
    UInt32 crc = config.computeModuleCrcLocked(moduleData);
    EXPECT_GT(crc, 0u);
    
    // Same data should produce same CRC
    UInt32 crc2 = config.computeModuleCrcLocked(moduleData);
    EXPECT_EQ(crc, crc2);
    
    config.Clear();
}

TEST_F(ConfigTest, Base64Encoding) {
    ConfigManager& config = ConfigManager::GetInstance();
    config.Initialize(testConfigPath_, false);
    
    // Set data
    config.SetString("secret.data", "sensitive information");
    
    // Enable Base64 encoding
    config.SetBase64Encoding(true);
    EXPECT_TRUE(config.IsBase64Enabled());
    
    // Disable Base64
    config.SetBase64Encoding(false);
    EXPECT_FALSE(config.IsBase64Enabled());
    
    config.Clear();
}

TEST_F(ConfigTest, EncryptedSaveAndLoad) {
    ConfigManager& config = ConfigManager::GetInstance();
    
    // Set HMAC secret for security
    setenv("HMAC_SECRET", "test-encryption-key-32-bytes-!", 1);
    
    config.Initialize(testEncryptedPath_, true);
    
    config.SetString("secure.password", "super-secret");
    config.SetString("secure.api_key", "key-12345");
    
    // Enable Base64 encoding for sensitive data
    config.SetBase64Encoding(true);
    EXPECT_TRUE(config.IsBase64Enabled());
    
    // Use private access to save
    auto saveResult = config.save(true);
    EXPECT_TRUE(saveResult.HasValue());
    
    // Clear and reload
    config.Clear();
    config.Initialize(testEncryptedPath_, true);
    auto loadResult = config.Load(false);  // Enforce verification
    EXPECT_TRUE(loadResult.HasValue());
    
    EXPECT_EQ(config.GetString("secure.password"), "super-secret");
    EXPECT_EQ(config.GetString("secure.api_key"), "key-12345");
    
    config.Clear();
    unsetenv("HMAC_SECRET");
}

