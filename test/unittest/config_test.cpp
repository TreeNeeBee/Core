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
    ConfigManager& config = ConfigManager::getInstance();
    
    auto result = config.initialize(testConfigPath_, false);
    EXPECT_TRUE(result.HasValue());
    
    config.clear();
}

TEST_F(ConfigTest, SetAndGetBool) {
    ConfigManager& config = ConfigManager::getInstance();
    config.initialize(testConfigPath_, false);
    
    auto setResult = config.setBool("test.bool_value", true);
    EXPECT_TRUE(setResult.HasValue());
    
    Bool value = config.getBool("test.bool_value");
    EXPECT_TRUE(value);
    
    Bool defaultValue = config.getBool("test.nonexistent", false);
    EXPECT_FALSE(defaultValue);
    
    config.clear();
}

TEST_F(ConfigTest, SetAndGetInt) {
    ConfigManager& config = ConfigManager::getInstance();
    config.initialize(testConfigPath_, false);
    
    auto setResult = config.setInt("test.int_value", 12345);
    EXPECT_TRUE(setResult.HasValue());
    
    Int64 value = config.getInt("test.int_value");
    EXPECT_EQ(value, 12345);
    
    Int64 defaultValue = config.getInt("test.nonexistent", 999);
    EXPECT_EQ(defaultValue, 999);
    
    config.clear();
}

TEST_F(ConfigTest, SetAndGetDouble) {
    ConfigManager& config = ConfigManager::getInstance();
    config.initialize(testConfigPath_, false);
    
    auto setResult = config.setDouble("test.double_value", 3.14159);
    EXPECT_TRUE(setResult.HasValue());
    
    Double value = config.getDouble("test.double_value");
    EXPECT_NEAR(value, 3.14159, 0.00001);
    
    config.clear();
}

TEST_F(ConfigTest, SetAndGetString) {
    ConfigManager& config = ConfigManager::getInstance();
    config.initialize(testConfigPath_, false);
    
    auto setResult = config.setString("test.string_value", "Hello, World!");
    EXPECT_TRUE(setResult.HasValue());
    
    String value = config.getString("test.string_value");
    EXPECT_EQ(value, "Hello, World!");
    
    String defaultValue = config.getString("test.nonexistent", "default");
    EXPECT_EQ(defaultValue, "default");
    
    config.clear();
}

TEST_F(ConfigTest, HierarchicalKeys) {
    ConfigManager& config = ConfigManager::getInstance();
    config.initialize(testConfigPath_, false);
    
    config.setInt("network.port", 8080);
    config.setString("network.interface", "eth0");
    config.setBool("network.enabled", true);
    
    EXPECT_EQ(config.getInt("network.port"), 8080);
    EXPECT_EQ(config.getString("network.interface"), "eth0");
    EXPECT_TRUE(config.getBool("network.enabled"));
    
    config.clear();
}

TEST_F(ConfigTest, KeyExistence) {
    ConfigManager& config = ConfigManager::getInstance();
    config.initialize(testConfigPath_, false);
    
    config.setInt("test.value", 123);
    
    EXPECT_TRUE(config.exists("test.value"));
    EXPECT_FALSE(config.exists("test.nonexistent"));
    
    config.clear();
}

TEST_F(ConfigTest, SaveAndLoad) {
    ConfigManager& config = ConfigManager::getInstance();
    config.initialize(testConfigPath_, false);
    
    // Set values
    config.setInt("network.port", 8080);
    config.setString("database.host", "localhost");
    config.setBool("logging.enabled", true);
    
    // Note: save() is private but accessible in unit test via #define private public
    auto saveResult = config.save();
    EXPECT_TRUE(saveResult.HasValue());
    
    // Verify file was created
    std::ifstream file(testConfigPath_);
    EXPECT_TRUE(file.good());
    file.close();
    
    // Clear and reload
    config.clear();
    auto loadResult = config.load();
    EXPECT_TRUE(loadResult.HasValue());
    
    // Verify values
    EXPECT_EQ(config.getInt("network.port"), 8080);
    EXPECT_EQ(config.getString("database.host"), "localhost");
    EXPECT_TRUE(config.getBool("logging.enabled"));
    
    config.clear();
}

TEST_F(ConfigTest, BackupAndRollback) {
    ConfigManager& config = ConfigManager::getInstance();
    config.initialize(testConfigPath_, false);
    
    // Set initial value
    config.setInt("test.value", 100);
    EXPECT_EQ(config.getInt("test.value"), 100);
    
    // Create backup
    auto backupResult = config.createBackup();
    EXPECT_TRUE(backupResult.HasValue());
    
    // Modify value
    config.setInt("test.value", 200);
    EXPECT_EQ(config.getInt("test.value"), 200);
    
    // Rollback
    auto rollbackResult = config.rollback();
    EXPECT_TRUE(rollbackResult.HasValue());
    
    // Verify restored value
    EXPECT_EQ(config.getInt("test.value"), 100);
    
    config.clear();
}

TEST_F(ConfigTest, ChangeCallback) {
    ConfigManager& config = ConfigManager::getInstance();
    config.initialize(testConfigPath_, false);
    
    Bool callbackTriggered = false;
    String capturedKey;
    Int64 capturedNewValue = 0;
    
    auto callback = [&](const String& key, const ConfigValue& /* oldValue */, const ConfigValue& newValue) {
        callbackTriggered = true;
        capturedKey = key;
        capturedNewValue = newValue.asInt();
    };
    
    // Register callback for network keys
    UInt32 callbackId = config.registerChangeCallback("network", callback);
    
    // Modify a network key
    config.setInt("network.port", 9090);
    
    EXPECT_TRUE(callbackTriggered);
    EXPECT_EQ(capturedKey, "network.port");
    EXPECT_EQ(capturedNewValue, 9090);
    
    // Unregister callback
    config.unregisterChangeCallback(callbackId);
    
    // Reset flag
    callbackTriggered = false;
    
    // Modify again - callback should not be triggered
    config.setInt("network.port", 8080);
    EXPECT_FALSE(callbackTriggered);
    
    config.clear();
}

TEST_F(ConfigTest, ChangeCallbackOldAndNewValues) {
    ConfigManager& config = ConfigManager::getInstance();
    config.initialize(testConfigPath_, false);

    // Seed an initial value
    config.setInt("network.port", 8000);

    Bool callbackTriggered = false;
    Int64 oldV = -1;
    Int64 newV = -1;
    auto cb = [&](const String& key, const ConfigValue& oldValue, const ConfigValue& newValue){
        if (key == "network.port") {
            callbackTriggered = true;
            oldV = oldValue.asInt(-1);
            newV = newValue.asInt(-1);
        }
    };
    UInt32 id = config.registerChangeCallback("network", cb);

    // Change value
    config.setInt("network.port", 8100);

    EXPECT_TRUE(callbackTriggered);
    EXPECT_EQ(oldV, 8000);
    EXPECT_EQ(newV, 8100);

    config.unregisterChangeCallback(id);
    config.clear();
}

TEST_F(ConfigTest, PolicyPersistenceInJson) {
    ConfigManager& config = ConfigManager::getInstance();
    config.initialize(testConfigPath_, false);

    // Create some module configs
    nlohmann::json modA; modA["v"] = 1;
    nlohmann::json modB; modB["v"] = 2;
    config.setModuleConfigJson("modA", modA);
    config.setModuleConfigJson("modB", modB);

    // Set explicit policies
    config.setModuleUpdatePolicy("modA", "first");
    config.setModuleUpdatePolicy("modB", "always");

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

    config.clear();
}

TEST_F(ConfigTest, VerificationFailsOnTamper) {
    ConfigManager& config = ConfigManager::getInstance();
    setenv("HMAC_SECRET", "test-secret-key-32-bytes-long!", 1);
    config.initialize(testConfigPath_, true);

    config.setString("secure.value", "original");
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
    config.clear();
    config.initialize(testConfigPath_, true);
    auto loadResult = config.load(false);
    EXPECT_FALSE(loadResult.HasValue());

    config.clear();
    unsetenv("HMAC_SECRET");
}

TEST_F(ConfigTest, ConfigValueFromJsonString) {
    // Array
    ConfigValue arr = ConfigValue::fromJsonString("[1,2,3]");
    EXPECT_TRUE(arr.isArray());
    EXPECT_EQ(arr.arraySize(), 3u);
    EXPECT_EQ(arr[0].asInt(), 1);
    EXPECT_EQ(arr[1].asInt(), 2);
    EXPECT_EQ(arr[2].asInt(), 3);

    // Object
    ConfigValue obj = ConfigValue::fromJsonString("{\"a\":true,\"b\":\"x\"}");
    EXPECT_TRUE(obj.isObject());
    EXPECT_TRUE(obj.hasKey("a"));
    EXPECT_TRUE(obj.hasKey("b"));
    EXPECT_TRUE(obj["a"].asBool());
    EXPECT_EQ(obj["b"].asString(), "x");
}

TEST_F(ConfigTest, GetReturnsArraysAndObjects) {
    ConfigManager& config = ConfigManager::getInstance();
    config.initialize(testConfigPath_, false);

    nlohmann::json arr = nlohmann::json::array({1,2,3});
    nlohmann::json obj; obj["k1"]=1; obj["k2"]=2;
    config.setModuleConfigJson("amod", arr);
    config.setModuleConfigJson("omod", obj);

    auto a = config.get("amod");
    ASSERT_TRUE(a.has_value());
    EXPECT_TRUE(a.value().isArray());
    EXPECT_EQ(a.value().arraySize(), 3u);

    auto o = config.get("omod");
    ASSERT_TRUE(o.has_value());
    EXPECT_TRUE(o.value().isObject());
    EXPECT_TRUE(o.value().hasKey("k1"));
    EXPECT_EQ(o.value()["k1"].asInt(), 1);

    config.clear();
}

TEST_F(ConfigTest, Metadata) {
    ConfigManager& config = ConfigManager::getInstance();
    config.initialize(testConfigPath_, true);  // Enable security to generate CRC and timestamp
    
    config.setInt("test.value", 123);
    // Use private access to call save() before getting metadata
    auto saveResult = config.save(true);
    EXPECT_TRUE(saveResult.HasValue());
    
    ConfigMetadata metadata = config.getMetadata();
    
    EXPECT_GT(metadata.version, 0u);
    EXPECT_FALSE(metadata.crc.empty());
    EXPECT_FALSE(metadata.timestamp.empty());
    EXPECT_FALSE(metadata.encrypted);
    
    config.clear();
}

TEST_F(ConfigTest, JsonExport) {
    ConfigManager& config = ConfigManager::getInstance();
    config.initialize(testConfigPath_, false);
    
    config.setInt("network.port", 8080);
    config.setString("database.host", "localhost");
    config.setBool("logging.enabled", true);
    
    String json = config.toJson(true);
    
    EXPECT_FALSE(json.empty());
    EXPECT_NE(json.find("network"), String::npos);
    EXPECT_NE(json.find("database"), String::npos);
    EXPECT_NE(json.find("logging"), String::npos);
    
    config.clear();
}

TEST_F(ConfigTest, ConfigValueTypes) {
    ConfigValue boolVal(true);
    EXPECT_TRUE(boolVal.isBool());
    EXPECT_TRUE(boolVal.asBool());
    
    ConfigValue intVal(static_cast<Int64>(42));
    EXPECT_TRUE(intVal.isInt());
    EXPECT_EQ(intVal.asInt(), 42);
    
    ConfigValue doubleVal(3.14);
    EXPECT_TRUE(doubleVal.isDouble());
    EXPECT_NEAR(doubleVal.asDouble(), 3.14, 0.001);
    
    ConfigValue stringVal("test");
    EXPECT_TRUE(stringVal.isString());
    EXPECT_EQ(stringVal.asString(), "test");
    
    ConfigValue nullVal;
    EXPECT_TRUE(nullVal.isNull());
}

TEST_F(ConfigTest, ConfigValueArray) {
    ConfigValue arrayVal;
    arrayVal.append(ConfigValue(static_cast<Int64>(1)));
    arrayVal.append(ConfigValue(static_cast<Int64>(2)));
    arrayVal.append(ConfigValue(static_cast<Int64>(3)));
    
    EXPECT_TRUE(arrayVal.isArray());
    EXPECT_EQ(arrayVal.arraySize(), 3u);
    EXPECT_EQ(arrayVal[0].asInt(), 1);
    EXPECT_EQ(arrayVal[1].asInt(), 2);
    EXPECT_EQ(arrayVal[2].asInt(), 3);
}

TEST_F(ConfigTest, ConfigValueObject) {
    ConfigValue objVal;
    objVal["name"] = ConfigValue("test");
    objVal["value"] = ConfigValue(static_cast<Int64>(123));
    objVal["enabled"] = ConfigValue(true);
    
    EXPECT_TRUE(objVal.isObject());
    EXPECT_TRUE(objVal.hasKey("name"));
    EXPECT_TRUE(objVal.hasKey("value"));
    EXPECT_TRUE(objVal.hasKey("enabled"));
    EXPECT_FALSE(objVal.hasKey("nonexistent"));
    
    EXPECT_EQ(objVal["name"].asString(), "test");
    EXPECT_EQ(objVal["value"].asInt(), 123);
    EXPECT_TRUE(objVal["enabled"].asBool());
}

// ============================================================================
// Update Policy Tests (moved from examples)
// ============================================================================

TEST_F(ConfigTest, DefaultUpdatePolicy) {
    ConfigManager& config = ConfigManager::getInstance();
    config.initialize(testConfigPath_, false);
    
    // Module without explicit policy should use default (on_change)
    auto policy = config.getModuleUpdatePolicy("newModule");
    EXPECT_EQ(policy, ConfigManager::UpdatePolicy::kOnChangeUpdate);
    
    config.clear();
}

TEST_F(ConfigTest, SetModuleUpdatePolicy) {
    ConfigManager& config = ConfigManager::getInstance();
    config.initialize(testConfigPath_, false);
    
    // Set different policies
    auto result1 = config.setModuleUpdatePolicy("modA", ConfigManager::UpdatePolicy::kFirstUpdate);
    EXPECT_TRUE(result1.HasValue());
    
    auto result2 = config.setModuleUpdatePolicy("modB", ConfigManager::UpdatePolicy::kAlwaysUpdate);
    EXPECT_TRUE(result2.HasValue());
    
    auto result3 = config.setModuleUpdatePolicy("modC", ConfigManager::UpdatePolicy::kNoUpdate);
    EXPECT_TRUE(result3.HasValue());
    
    // Verify policies
    EXPECT_EQ(config.getModuleUpdatePolicy("modA"), ConfigManager::UpdatePolicy::kFirstUpdate);
    EXPECT_EQ(config.getModuleUpdatePolicy("modB"), ConfigManager::UpdatePolicy::kAlwaysUpdate);
    EXPECT_EQ(config.getModuleUpdatePolicy("modC"), ConfigManager::UpdatePolicy::kNoUpdate);
    
    config.clear();
}

TEST_F(ConfigTest, SetModuleUpdatePolicyByString) {
    ConfigManager& config = ConfigManager::getInstance();
    config.initialize(testConfigPath_, false);
    
    auto result1 = config.setModuleUpdatePolicy("modA", "first");
    EXPECT_TRUE(result1.HasValue());
    
    auto result2 = config.setModuleUpdatePolicy("modB", "always");
    EXPECT_TRUE(result2.HasValue());
    
    auto result3 = config.setModuleUpdatePolicy("modC", "none");
    EXPECT_TRUE(result3.HasValue());
    
    auto result4 = config.setModuleUpdatePolicy("modD", "on_change");
    EXPECT_TRUE(result4.HasValue());
    
    // Invalid policy string
    auto result5 = config.setModuleUpdatePolicy("modE", "invalid");
    EXPECT_FALSE(result5.HasValue());
    
    // Verify
    EXPECT_EQ(config.getModuleUpdatePolicy("modA"), ConfigManager::UpdatePolicy::kFirstUpdate);
    EXPECT_EQ(config.getModuleUpdatePolicy("modB"), ConfigManager::UpdatePolicy::kAlwaysUpdate);
    EXPECT_EQ(config.getModuleUpdatePolicy("modC"), ConfigManager::UpdatePolicy::kNoUpdate);
    EXPECT_EQ(config.getModuleUpdatePolicy("modD"), ConfigManager::UpdatePolicy::kOnChangeUpdate);
    
    config.clear();
}

TEST_F(ConfigTest, ModuleConfigJson) {
    ConfigManager& config = ConfigManager::getInstance();
    config.initialize(testConfigPath_, false);
    
    // Set module config
    nlohmann::json moduleConfig;
    moduleConfig["host"] = "localhost";
    moduleConfig["port"] = 8080;
    moduleConfig["enabled"] = true;
    
    auto result = config.setModuleConfigJson("database", moduleConfig);
    EXPECT_TRUE(result.HasValue());
    
    // Get module config
    nlohmann::json retrieved = config.getModuleConfigJson("database");
    EXPECT_EQ(retrieved["host"], "localhost");
    EXPECT_EQ(retrieved["port"], 8080);
    EXPECT_TRUE(retrieved["enabled"]);
    
    config.clear();
}

TEST_F(ConfigTest, SetModuleConfigJsonUpdatesPolicy) {
    ConfigManager& config = ConfigManager::getInstance();
    config.initialize(testConfigPath_, false);
    
    nlohmann::json moduleConfig;
    moduleConfig["value"] = 123;
    
    config.setModuleConfigJson("testModule", moduleConfig);
    
    // Should auto-set policy to default (on_change)
    auto policy = config.getModuleUpdatePolicy("testModule");
    EXPECT_EQ(policy, ConfigManager::UpdatePolicy::kOnChangeUpdate);
    
    config.clear();
}

// ============================================================================
// Verification Tests (moved from examples)
// ============================================================================


TEST_F(ConfigTest, SkipVerificationOnLoad) {
    ConfigManager& config = ConfigManager::getInstance();
    config.initialize(testConfigPath_, true);
    
    // Set some data
    config.setInt("test.value", 12345);
    config.setString("test.name", "verification_test");
    
    // Save using private method (accessible via #define private public)
    auto saveResult = config.save(true);
    EXPECT_TRUE(saveResult.HasValue());
    
    // Clear and reload with skipVerification=true
    config.clear();
    config.initialize(testConfigPath_, true);
    auto loadResult = config.load(true);  // Skip verification
    EXPECT_TRUE(loadResult.HasValue());
    
    EXPECT_EQ(config.getInt("test.value"), 12345);
    EXPECT_EQ(config.getString("test.name"), "verification_test");
    
    config.clear();
}

TEST_F(ConfigTest, VerificationWithCorrectHMAC) {
    ConfigManager& config = ConfigManager::getInstance();
    
    // Set HMAC secret
    setenv("HMAC_SECRET", "test-secret-key-32-bytes-long!", 1);
    
    config.initialize(testConfigPath_, true);
    config.setInt("secure.value", 9999);
    
    // Save using private method (accessible via #define private public)
    auto saveResult = config.save(true);
    EXPECT_TRUE(saveResult.HasValue());
    
    // Clear and reload with verification
    config.clear();
    config.initialize(testConfigPath_, true);
    auto loadResult = config.load(false);  // Enforce verification
    EXPECT_TRUE(loadResult.HasValue());
    
    EXPECT_EQ(config.getInt("secure.value"), 9999);
    
    config.clear();
    unsetenv("HMAC_SECRET");
}

// Test private save() method directly
TEST_F(ConfigTest, PrivateSaveMethod) {
    ConfigManager& config = ConfigManager::getInstance();
    config.initialize(testConfigPath_, true);
    
    config.setInt("test.value", 42);
    
    // Access private save() method
    auto result = config.save(true);
    EXPECT_TRUE(result.HasValue());
    
    // Verify file was created
    std::ifstream file(testConfigPath_);
    EXPECT_TRUE(file.good());
    file.close();
    
    config.clear();
}

// Test private member variables
TEST_F(ConfigTest, PrivateMemberAccess) {
    ConfigManager& config = ConfigManager::getInstance();
    config.initialize(testConfigPath_, false);
    
    config.setInt("test.value", 100);
    
    // Access private members directly
    EXPECT_FALSE(config.configData_.empty());
    EXPECT_TRUE(config.initialized_);
    EXPECT_EQ(config.configPath_, testConfigPath_);
    
    config.clear();
}

// Test internal CRC computation
TEST_F(ConfigTest, InternalCrcComputation) {
    ConfigManager& config = ConfigManager::getInstance();
    config.initialize(testConfigPath_, true);
    
    config.setInt("test.value", 123);
    
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
    
    config.clear();
}

// Test internal policy refresh
TEST_F(ConfigTest, InternalPolicyRefresh) {
    ConfigManager& config = ConfigManager::getInstance();
    config.initialize(testConfigPath_, false);
    
    // Set some module configs
    nlohmann::json modConfig;
    modConfig["value"] = 1;
    config.setModuleConfigJson("testMod", modConfig);
    
    // Access private method to refresh policies
    config.refreshPoliciesFromConfigLocked();
    
    // Verify policies were loaded
    auto policy = config.getModuleUpdatePolicy("testMod");
    EXPECT_EQ(policy, ConfigManager::UpdatePolicy::kOnChangeUpdate);
    
    config.clear();
}

// Test module CRC computation
TEST_F(ConfigTest, ModuleCrcComputation) {
    ConfigManager& config = ConfigManager::getInstance();
    config.initialize(testConfigPath_, false);
    
    nlohmann::json moduleData;
    moduleData["key1"] = "value1";
    moduleData["key2"] = 42;
    
    // Access private computeModuleCrcLocked method
    UInt32 crc = config.computeModuleCrcLocked(moduleData);
    EXPECT_GT(crc, 0u);
    
    // Same data should produce same CRC
    UInt32 crc2 = config.computeModuleCrcLocked(moduleData);
    EXPECT_EQ(crc, crc2);
    
    config.clear();
}

TEST_F(ConfigTest, Base64Encoding) {
    ConfigManager& config = ConfigManager::getInstance();
    config.initialize(testConfigPath_, false);
    
    // Set data
    config.setString("secret.data", "sensitive information");
    
    // Enable Base64 encoding
    config.setBase64Encoding(true);
    EXPECT_TRUE(config.isBase64Enabled());
    
    // Disable Base64
    config.setBase64Encoding(false);
    EXPECT_FALSE(config.isBase64Enabled());
    
    config.clear();
}

TEST_F(ConfigTest, EncryptedSaveAndLoad) {
    ConfigManager& config = ConfigManager::getInstance();
    
    // Set HMAC secret for security
    setenv("HMAC_SECRET", "test-encryption-key-32-bytes-!", 1);
    
    config.initialize(testEncryptedPath_, true);
    
    config.setString("secure.password", "super-secret");
    config.setString("secure.api_key", "key-12345");
    
    // Enable Base64 encoding for sensitive data
    config.setBase64Encoding(true);
    EXPECT_TRUE(config.isBase64Enabled());
    
    // Use private access to save
    auto saveResult = config.save(true);
    EXPECT_TRUE(saveResult.HasValue());
    
    // Clear and reload
    config.clear();
    config.initialize(testEncryptedPath_, true);
    auto loadResult = config.load(false);  // Enforce verification
    EXPECT_TRUE(loadResult.HasValue());
    
    EXPECT_EQ(config.getString("secure.password"), "super-secret");
    EXPECT_EQ(config.getString("secure.api_key"), "key-12345");
    
    config.clear();
    unsetenv("HMAC_SECRET");
}

