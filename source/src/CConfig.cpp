/**
 * @file        CConfig.cpp
 * @author      LightAP Team  
 * @brief       Unified Configuration Management with Triple Security
 * @date        2025-10-31
 * @details     - JSON operations using nlohmann/json
 *              - Triple security: CRC32 → Timestamp → HMAC-SHA256
 *              - Optional Base64 encoding to hide sensitive data
 *              - Module-level configuration access
 * @copyright   Copyright (c) 2025
 * @version     3.0
 */

#include "CConfig.hpp"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <cstring>
#include <algorithm>
#include <cstdlib>
#include <nlohmann/json.hpp>
#include <zlib.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/crypto.h>
#include <openssl/err.h>

using json = nlohmann::json;

namespace lap {
namespace core {

namespace {
    // Default configuration file name
    const String DEFAULT_CONFIG_FILE = "config.json";
    
    // HMAC Secret Configuration
    // - If REQUIRE_HMAC_SECRET_ENV is defined: ENV variable is mandatory, program exits if not found
    // - If not defined (default): Use built-in secret with warning if ENV not found
    constexpr const char* ENV_HMAC_SECRET = "HMAC_SECRET";
    constexpr const char* BUILTIN_HMAC_SECRET = "LightAP-Default-HMAC-Secret-2025-DO-NOT-USE-IN-PRODUCTION";
    
    // Private field names
    constexpr const char* FIELD_METADATA = "__metadata__";
    constexpr const char* META_VERSION = "version";
    constexpr const char* META_DESCRIPTION = "description";
    constexpr const char* META_ENCRYPTED = "encrypted";
    constexpr const char* META_CRC = "crc";
    constexpr const char* META_TIMESTAMP = "timestamp";
    constexpr const char* META_HMAC = "hmac";

    // Update policy: single top-level mapping
    constexpr const char* FIELD_UPDATE_POLICY = "__update_policy__";    // top-level mapping: module -> policy string
    constexpr const char* POLICY_DEFAULT_KEY = "default";                // special key for default policy
    
    // Base64 encoding
    String base64Encode(const String& input) {
        BIO *bio, *b64;
        BUF_MEM *bufferPtr;
        
        b64 = BIO_new(BIO_f_base64());
        bio = BIO_new(BIO_s_mem());
        bio = BIO_push(b64, bio);
        
        BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
        BIO_write(bio, input.c_str(), input.length());
        BIO_flush(bio);
        BIO_get_mem_ptr(bio, &bufferPtr);
        
        String result(bufferPtr->data, bufferPtr->length);
        BIO_free_all(bio);
        
        return result;
    }
    
    String base64Decode(const String& input) {
        BIO *bio, *b64;
        
        size_t decodeLen = input.length();
        char* buffer = new char[decodeLen];
        
        bio = BIO_new_mem_buf(input.c_str(), input.length());
        b64 = BIO_new(BIO_f_base64());
        bio = BIO_push(b64, bio);
        
        BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
        size_t length = BIO_read(bio, buffer, decodeLen);
        BIO_free_all(bio);
        
        String result(buffer, length);
        delete[] buffer;
        
        return result;
    }
    
    // Convert bytes to hex string
    String bytesToHex(const UInt8* data, Size len) {
        std::ostringstream oss;
        oss << std::hex << std::setfill('0');
        for (Size i = 0; i < len; ++i) {
            oss << std::setw(2) << static_cast<Int32>(data[i]);
        }
        return oss.str();
    }
}

// ============================================================================
// ConfigValue Implementation (kept for API compatibility)
// ============================================================================

ConfigValue::ConfigValue() : type_(ConfigValueType::kNull) {}

ConfigValue::ConfigValue(Bool value) 
    : type_(ConfigValueType::kBoolean), boolValue_(value) {}

ConfigValue::ConfigValue(Int64 value)
    : type_(ConfigValueType::kInteger), intValue_(value) {}

ConfigValue::ConfigValue(Double value)
    : type_(ConfigValueType::kDouble), doubleValue_(value) {}

ConfigValue::ConfigValue(const String& value)
    : type_(ConfigValueType::kString), stringValue_(value) {}

ConfigValue::ConfigValue(const Char* value)
    : type_(ConfigValueType::kString), stringValue_(value) {}

Bool ConfigValue::asBool(Bool defaultValue) const noexcept {
    if (type_ == ConfigValueType::kBoolean) return boolValue_;
    return defaultValue;
}

Int64 ConfigValue::asInt(Int64 defaultValue) const noexcept {
    if (type_ == ConfigValueType::kInteger) return intValue_;
    return defaultValue;
}

Double ConfigValue::asDouble(Double defaultValue) const noexcept {
    if (type_ == ConfigValueType::kDouble) return doubleValue_;
    return defaultValue;
}

String ConfigValue::asString(const String& defaultValue) const noexcept {
    if (type_ == ConfigValueType::kString) return stringValue_;
    return defaultValue;
}

Size ConfigValue::arraySize() const noexcept {
    if (type_ == ConfigValueType::kArray) return arrayValue_.size();
    return 0;
}

ConfigValue& ConfigValue::operator[](Size index) {
    if (type_ != ConfigValueType::kArray) {
        type_ = ConfigValueType::kArray;
        arrayValue_.clear();
    }
    if (index >= arrayValue_.size()) {
        arrayValue_.resize(index + 1);
    }
    return arrayValue_[index];
}

const ConfigValue& ConfigValue::operator[](Size index) const {
    static ConfigValue null;
    if (type_ != ConfigValueType::kArray || index >= arrayValue_.size()) {
        return null;
    }
    return arrayValue_[index];
}

void ConfigValue::append(const ConfigValue& value) {
    if (type_ != ConfigValueType::kArray) {
        type_ = ConfigValueType::kArray;
        arrayValue_.clear();
    }
    arrayValue_.push_back(value);
}

Bool ConfigValue::hasKey(const String& key) const noexcept {
    if (type_ != ConfigValueType::kObject) return false;
    return objectValue_.find(key) != objectValue_.end();
}

ConfigValue& ConfigValue::operator[](const String& key) {
    if (type_ != ConfigValueType::kObject) {
        type_ = ConfigValueType::kObject;
        objectValue_.clear();
    }
    return objectValue_[key];
}

const ConfigValue& ConfigValue::operator[](const String& key) const {
    static ConfigValue null;
    if (type_ != ConfigValueType::kObject) return null;
    auto it = objectValue_.find(key);
    if (it == objectValue_.end()) return null;
    return it->second;
}

Vector<String> ConfigValue::getKeys() const {
    Vector<String> keys;
    if (type_ == ConfigValueType::kObject) {
        for (const auto& pair : objectValue_) {
            keys.push_back(pair.first);
        }
    }
    return keys;
}

String ConfigValue::toJsonString(Bool pretty) const {
    std::ostringstream oss;
    toJsonString(oss, 0, pretty);
    return oss.str();
}

void ConfigValue::toJsonString(std::ostream& os, Int32 indent, Bool pretty) const {
    auto doIndent = [&](Int32 level) {
        if (pretty) {
            for (Int32 i = 0; i < level * 2; ++i) os << ' ';
        }
    };

    switch (type_) {
        case ConfigValueType::kNull:
            os << "null";
            break;
        case ConfigValueType::kBoolean:
            os << (boolValue_ ? "true" : "false");
            break;
        case ConfigValueType::kInteger:
            os << intValue_;
            break;
        case ConfigValueType::kDouble:
            os << std::fixed << std::setprecision(6) << doubleValue_;
            break;
        case ConfigValueType::kString:
            os << "\"" << stringValue_ << "\"";
            break;
        case ConfigValueType::kArray:
            os << "[";
            if (pretty) os << "\n";
            for (Size i = 0; i < arrayValue_.size(); ++i) {
                if (pretty) doIndent(indent + 1);
                arrayValue_[i].toJsonString(os, indent + 1, pretty);
                if (i < arrayValue_.size() - 1) os << ",";
                if (pretty) os << "\n";
            }
            if (pretty) doIndent(indent);
            os << "]";
            break;
        case ConfigValueType::kObject:
            os << "{";
            if (pretty) os << "\n";
            Size count = 0;
            for (const auto& pair : objectValue_) {
                if (pretty) doIndent(indent + 1);
                os << "\"" << pair.first << "\": ";
                pair.second.toJsonString(os, indent + 1, pretty);
                if (++count < objectValue_.size()) os << ",";
                if (pretty) os << "\n";
            }
            if (pretty) doIndent(indent);
            os << "}";
            break;
    }
}

ConfigValue ConfigValue::fromJsonString(const String& json) {
    (void)json;
    ConfigValue result;
    return result;
}

// ============================================================================
// ConfigManager Implementation
// ============================================================================

ConfigManager::ConfigManager()
    : enableSecurity_(true)
    , initialized_(false)
    , nextCallbackId_(1)
    , defaultPolicy_(UpdatePolicy::kOnChangeUpdate)
{
    // Load HMAC secret with fallback strategy
    const char* secret = std::getenv(ENV_HMAC_SECRET);
    
#ifdef REQUIRE_HMAC_SECRET_ENV
    // Strict mode: ENV variable is mandatory
    if (!secret || std::strlen(secret) == 0) {
        INNER_CORE_LOG("[ConfigManager] FATAL: %s environment variable not set or empty!\n", ENV_HMAC_SECRET);
        INNER_CORE_LOG("[ConfigManager] REQUIRE_HMAC_SECRET_ENV is enabled, program cannot continue.\n");
        INNER_CORE_LOG("[ConfigManager] Please set %s environment variable before starting.\n", ENV_HMAC_SECRET);
        std::abort(); // Force exit
    }
    hmacSecret_ = secret;
    INNER_CORE_LOG("[ConfigManager] Loaded HMAC secret from environment variable (strict mode)\n");
#else
    // Lenient mode: Use built-in secret as fallback
    if (secret && std::strlen(secret) > 0) {
        hmacSecret_ = secret;
        INNER_CORE_LOG("[ConfigManager] Loaded HMAC secret from environment variable\n");
    } else {
        hmacSecret_ = BUILTIN_HMAC_SECRET;
        INNER_CORE_LOG("[ConfigManager] WARNING: %s environment variable not set!\n", ENV_HMAC_SECRET);
        INNER_CORE_LOG("[ConfigManager] WARNING: Using built-in default HMAC secret (NOT secure for production)\n");
        INNER_CORE_LOG("[ConfigManager] WARNING: Please set %s for production deployments\n", ENV_HMAC_SECRET);
    }
#endif
    
    // Disable OpenSSL automatic cleanup at exit
    // We will manually cleanup in destructor to control the order
#if OPENSSL_VERSION_NUMBER >= 0x10100000L  // OpenSSL 1.1+
    OPENSSL_init_crypto(OPENSSL_INIT_NO_ATEXIT, NULL);
#else
    OPENSSL_config(NULL);
#endif
    
    // Initialize with empty JSON object
    configData_ = json::object();
    
    // Initialize metadata with defaults
    metadata_.version = 1;
    metadata_.encrypted = false;
    metadata_.description = "";
    
    // Automatically initialize with default config file
    // This will load config.json if it exists, otherwise start with empty config
    auto result = initialize(DEFAULT_CONFIG_FILE, enableSecurity_);
    if (!result.HasValue()) {
        INNER_CORE_LOG("[ConfigManager] Initialize with default config file failed, starting with empty config\n");
    }
}

ConfigManager::~ConfigManager() {
    // Automatically save configuration on destruction with full security
    // OpenSSL cleanup is disabled at init, so it's safe to use HMAC here
    if (initialized_ && !configPath_.empty()) {
        INNER_CORE_LOG("[ConfigManager] Auto-saving configuration on destruction\n");
        auto result = save(true);
        if (!result.HasValue()) {
            INNER_CORE_LOG("[ConfigManager] Failed to save configuration on destruction: error=%d\n", 
                           static_cast<int>(result.Error()));
        } else {
            INNER_CORE_LOG("[ConfigManager] Configuration saved successfully on destruction\n");
        }
    }
    
    // Manually cleanup OpenSSL resources after our operations complete
    // This ensures proper cleanup order (our work first, then OpenSSL)
#if OPENSSL_VERSION_NUMBER >= 0x10100000L  // OpenSSL 1.1+
    OPENSSL_cleanup();
#else
    EVP_cleanup();
    CRYPTO_cleanup_all_ex_data();
#endif
    INNER_CORE_LOG("[ConfigManager] OpenSSL cleanup completed\n");
}

ConfigManager& ConfigManager::getInstance() {
    static ConfigManager instance;
    return instance;
}

Result<void, ConfigErrc> ConfigManager::initialize(const String& configPath, Bool enableSecurity) {
    LockGuard lock(mutex_);
    
    configPath_ = configPath;
    enableSecurity_ = enableSecurity;
    initialized_ = true;
    
    // Try to load existing configuration
    auto loadResult = load();
    if (!loadResult.HasValue() && loadResult.Error() != ConfigErrc::kFileNotFound) {
        return loadResult;
    }
    
    return Result<void, ConfigErrc>::FromValue();
}

void ConfigManager::setBase64Encoding(Bool enable) {
    LockGuard lock(mutex_);
    metadata_.encrypted = enable;
}

Bool ConfigManager::isBase64Enabled() const {
    LockGuard lock(mutex_);
    return metadata_.encrypted;
}

ConfigMetadata ConfigManager::getMetadata() const {
    LockGuard lock(mutex_);
    return metadata_;
}

void ConfigManager::setVersion(UInt32 version) {
    LockGuard lock(mutex_);
    metadata_.version = version;
}

UInt32 ConfigManager::getVersion() const {
    LockGuard lock(mutex_);
    return metadata_.version;
}

void ConfigManager::setDescription(const String& description) {
    LockGuard lock(mutex_);
    metadata_.description = description;
}

String ConfigManager::getDescription() const {
    LockGuard lock(mutex_);
    return metadata_.description;
}

Result<void, ConfigErrc> ConfigManager::load(Bool skipVerification) {
    LockGuard lock(mutex_);
    
    if (!initialized_) {
        INNER_CORE_LOG("[ConfigManager] Load error: Not initialized\n");
        return Result<void, ConfigErrc>::FromError(ConfigErrc::kInternalError);
    }
    
    // Read file
    auto fileData = readFile(configPath_);
    if (!fileData.HasValue()) {
        INNER_CORE_LOG("[ConfigManager] Load error: Cannot read file '%s'\n", configPath_.c_str());
        return Result<void, ConfigErrc>::FromError(fileData.Error());
    }
    
    try {
        String jsonStr = fileData.Value();
        
        // Decode from Base64 if encrypted flag is set in metadata
        if (metadata_.encrypted && jsonStr.length() > 0 && jsonStr[0] != '{') {
            try {
                jsonStr = base64Decode(jsonStr);
            } catch (const std::exception& e) {
                INNER_CORE_LOG("[ConfigManager] Base64 decode error: %s\n", e.what());
                return Result<void, ConfigErrc>::FromError(ConfigErrc::kParseError);
            }
        }
        
        // Parse JSON
        json fullJson = json::parse(jsonStr);
        
        // Extract metadata and security checksums
        String storedCrc, storedTimestamp, storedHmac;
        if (fullJson.contains(FIELD_METADATA)) {
            const json& metaJson = fullJson[FIELD_METADATA];
            if (metaJson.contains(META_VERSION)) {
                metadata_.version = metaJson[META_VERSION].get<UInt32>();
            }
            if (metaJson.contains(META_DESCRIPTION)) {
                metadata_.description = metaJson[META_DESCRIPTION].get<String>();
            }
            if (metaJson.contains(META_ENCRYPTED)) {
                metadata_.encrypted = metaJson[META_ENCRYPTED].get<Bool>();
            }
            if (metaJson.contains(META_CRC)) {
                storedCrc = metaJson[META_CRC].get<String>();
            }
            if (metaJson.contains(META_TIMESTAMP)) {
                storedTimestamp = metaJson[META_TIMESTAMP].get<String>();
            }
            if (metaJson.contains(META_HMAC)) {
                storedHmac = metaJson[META_HMAC].get<String>();
            }
        }
        
        // Prepare core JSON for security verification (exclude __metadata__ and __update_policy__)
        json jsonForSecurity = fullJson;
        jsonForSecurity.erase(FIELD_METADATA);
        jsonForSecurity.erase(FIELD_UPDATE_POLICY);
        String coreJson = jsonForSecurity.dump();
        
        // Security verification if enabled and not skipped
        if (enableSecurity_ && !skipVerification && !storedCrc.empty()) {
            // Step 1: Verify CRC32
            UInt32 computedCrc = computeCrc32(coreJson);
            std::ostringstream crcHex;
            crcHex << std::hex << std::setw(8) << std::setfill('0') << computedCrc;
            String computedCrcStr = crcHex.str();
            
            if (computedCrcStr != storedCrc) {
                INNER_CORE_LOG("[ConfigManager] CRC32 verification failed: expected=%s, computed=%s\n", 
                               storedCrc.c_str(), computedCrcStr.c_str());
                return Result<void, ConfigErrc>::FromError(ConfigErrc::kCrcMismatch);
            }
            
            // Step 2: Verify timestamp
            if (!storedTimestamp.empty() && !validateTimestamp(storedTimestamp)) {
                INNER_CORE_LOG("[ConfigManager] Timestamp validation failed: %s\n", storedTimestamp.c_str());
                return Result<void, ConfigErrc>::FromError(ConfigErrc::kTimestampInvalid);
            }
            
            // Step 3: Verify HMAC
            if (!storedHmac.empty()) {
                if (hmacSecret_.empty()) {
                    INNER_CORE_LOG("[ConfigManager] HMAC verification failed: %s not set\n", ENV_HMAC_SECRET);
                    return Result<void, ConfigErrc>::FromError(ConfigErrc::kHmacKeyMissing);
                }
                
                String computedHmac = computeHmacSha256(coreJson, hmacSecret_);
                if (computedHmac != storedHmac) {
                    INNER_CORE_LOG("[ConfigManager] HMAC verification failed\n");
                    return Result<void, ConfigErrc>::FromError(ConfigErrc::kHmacMismatch);
                }
            }
        }
        
        // Store core configuration
        configData_ = fullJson;
        lastPersistedData_ = configData_;
        
        // Update metadata security fields
        metadata_.crc = storedCrc;
        metadata_.timestamp = storedTimestamp;
        metadata_.hmac = storedHmac;

        // Refresh policies from config fields and initialize tracking baselines
        refreshPoliciesFromConfigLocked();
        moduleSavedOnce_.clear();
        moduleLastCrc_.clear();
        for (auto it = configData_.begin(); it != configData_.end(); ++it) {
            const String moduleName = it.key();
            if (moduleName == FIELD_UPDATE_POLICY || moduleName == FIELD_METADATA) continue;
            // mark as already saved once if present in persisted file
            moduleSavedOnce_.insert(moduleName);
            if (it.value().is_object() || it.value().is_array() || it.value().is_primitive()) {
                moduleLastCrc_[moduleName] = computeModuleCrcLocked(it.value());
            }
        }
        
        return Result<void, ConfigErrc>::FromValue();
        
    } catch (const json::exception& e) {
        INNER_CORE_LOG("[ConfigManager] JSON parse error: %s (file: %s)\n", e.what(), configPath_.c_str());
        return Result<void, ConfigErrc>::FromError(ConfigErrc::kParseError);
    } catch (const ConfigException& e) {
        INNER_CORE_LOG("[ConfigManager] Configuration error: %s\n", e.what());
        return Result<void, ConfigErrc>::FromError(ConfigErrc::kValidationError);
    } catch (const std::exception& e) {
        INNER_CORE_LOG("[ConfigManager] Load error: %s\n", e.what());
        return Result<void, ConfigErrc>::FromError(ConfigErrc::kInternalError);
    }
}

Result<void, ConfigErrc> ConfigManager::save(Bool enableSecurity) {
    LockGuard lock(mutex_);
    
    if (!initialized_) {
        INNER_CORE_LOG("[ConfigManager] Save error: Not initialized\n");
        return Result<void, ConfigErrc>::FromError(ConfigErrc::kInternalError);
    }
    
    try {
        // Build core JSON to persist according to per-module update policies
        json toPersist = json::object();

        // Preserve existing top-level policy mapping (will be updated)
        if (configData_.contains(FIELD_UPDATE_POLICY)) {
            toPersist[FIELD_UPDATE_POLICY] = configData_[FIELD_UPDATE_POLICY];
        }

        for (auto it = configData_.begin(); it != configData_.end(); ++it) {
            const String moduleName = it.key();
            if (moduleName == FIELD_UPDATE_POLICY || moduleName == FIELD_METADATA) {
                // handled separately
                continue;
            }

            const json& currentModule = it.value();
            json previousModule = lastPersistedData_.contains(moduleName) ? lastPersistedData_[moduleName] : json();
            UpdatePolicy policy = getModuleUpdatePolicy(moduleName);

            json selected = currentModule; // default

            switch (policy) {
                case UpdatePolicy::kNoUpdate: {
                    if (!previousModule.is_null()) {
                        selected = previousModule;
                    } else {
                        // No previous data, must persist something
                        selected = currentModule;
                    }
                    break;
                }
                case UpdatePolicy::kFirstUpdate: {
                    Bool alreadySaved = lastPersistedData_.contains(moduleName);
                    if (alreadySaved) {
                        selected = previousModule;
                    } else {
                        selected = currentModule;
                    }
                    break;
                }
                case UpdatePolicy::kAlwaysUpdate: {
                    selected = currentModule;
                    break;
                }
                case UpdatePolicy::kOnChangeUpdate: {
                    UInt32 curCrc = computeModuleCrcLocked(currentModule);
                    auto itC = moduleLastCrc_.find(moduleName);
                    if (itC != moduleLastCrc_.end() && itC->second == curCrc) {
                        selected = previousModule;
                    } else {
                        selected = currentModule;
                    }
                    break;
                }
            }

            toPersist[moduleName] = selected;
            // Ensure policy is materialized for persistence
            materializePolicyFieldLocked(moduleName, toPersist);
        }

        // After all modules processed, ensure __update_policy__ contains all module policies
        if (!toPersist.contains(FIELD_UPDATE_POLICY)) {
            toPersist[FIELD_UPDATE_POLICY] = json::object();
        }
        // Preserve existing module policies from configData_
        if (configData_.contains(FIELD_UPDATE_POLICY) && configData_[FIELD_UPDATE_POLICY].is_object()) {
            for (auto it = configData_[FIELD_UPDATE_POLICY].begin(); 
                 it != configData_[FIELD_UPDATE_POLICY].end(); ++it) {
                toPersist[FIELD_UPDATE_POLICY][it.key()] = it.value();
            }
        }
        // Ensure default policy key is present
        toPersist[FIELD_UPDATE_POLICY][POLICY_DEFAULT_KEY] = policyToString(defaultPolicy_);

        // Build core JSON (exclude __update_policy__ and __metadata__ from security ops)
        json coreForSecurity = toPersist;
        if (coreForSecurity.contains(FIELD_UPDATE_POLICY)) {
            coreForSecurity.erase(FIELD_UPDATE_POLICY);
        }
        // Get core JSON (compact format for consistent CRC)
        String coreJson = coreForSecurity.dump();
        
        // Create full JSON with core data
        json fullJson = toPersist;
        
        // Create __metadata__ object
        json metaJson = json::object();
        metaJson[META_VERSION] = metadata_.version;
        metaJson[META_DESCRIPTION] = metadata_.description;
        metaJson[META_ENCRYPTED] = metadata_.encrypted;
        
        if (enableSecurity && enableSecurity_) {
            // HMAC secret is always available (from ENV or built-in)
            
            // Compute security fields
            UInt32 crc = computeCrc32(coreJson);
            String timestamp = getCurrentTimestamp();
            String hmac = computeHmacSha256(coreJson, hmacSecret_);
            
            // Convert CRC to hex string
            std::ostringstream crcStream;
            crcStream << std::hex << std::setw(8) << std::setfill('0') << crc;
            
            // Add security fields to metadata
            metaJson[META_CRC] = crcStream.str();
            metaJson[META_TIMESTAMP] = timestamp;
            metaJson[META_HMAC] = hmac;
            
            // Update internal metadata
            metadata_.crc = metaJson[META_CRC];
            metadata_.timestamp = timestamp;
            metadata_.hmac = hmac;
        }
        
        // Add __metadata__ to full JSON
        fullJson[FIELD_METADATA] = metaJson;
        
        // Serialize to string (pretty format for human readability)
        String jsonOutput = fullJson.dump(4);
        
        // Encode to Base64 if encrypted flag is set
        if (metadata_.encrypted) {
            try {
                jsonOutput = base64Encode(jsonOutput);
            } catch (const std::exception& e) {
                INNER_CORE_LOG("[ConfigManager] Base64 encode error: %s\n", e.what());
                return Result<void, ConfigErrc>::FromError(ConfigErrc::kInternalError);
            }
        }
        
        auto writeResult = writeFile(configPath_, jsonOutput);
        if (!writeResult.HasValue()) {
            INNER_CORE_LOG("[ConfigManager] Save error: Cannot write to file '%s'\n", configPath_.c_str());
        }

        if (writeResult.HasValue()) {
            // Update persisted snapshot and CRCs after successful save
            lastPersistedData_ = toPersist;
            moduleLastCrc_.clear();
            for (auto it = toPersist.begin(); it != toPersist.end(); ++it) {
                const String moduleName = it.key();
                if (moduleName == FIELD_UPDATE_POLICY) continue;
                moduleLastCrc_[moduleName] = computeModuleCrcLocked(it.value());
            }
            // Update FirstUpdate tracking
            for (auto it = toPersist.begin(); it != toPersist.end(); ++it) {
                const String moduleName = it.key();
                if (moduleName == FIELD_UPDATE_POLICY) continue;
                moduleSavedOnce_.insert(moduleName);
            }
        }
        return writeResult;
        
    } catch (const json::exception& e) {
        INNER_CORE_LOG("[ConfigManager] JSON serialization error: %s\n", e.what());
        return Result<void, ConfigErrc>::FromError(ConfigErrc::kInternalError);
    } catch (const std::exception& e) {
        INNER_CORE_LOG("[ConfigManager] Save error: %s\n", e.what());
        return Result<void, ConfigErrc>::FromError(ConfigErrc::kInternalError);
    }
}

Result<void, ConfigErrc> ConfigManager::createBackup() {
    LockGuard lock(mutex_);
    
    backupStack_.push_back(configData_);
    
    // Keep max 10 backups
    if (backupStack_.size() > 10) {
        backupStack_.erase(backupStack_.begin());
    }
    
    return Result<void, ConfigErrc>::FromValue();
}

Result<void, ConfigErrc> ConfigManager::rollback() {
    LockGuard lock(mutex_);
    
    if (backupStack_.empty()) {
        INNER_CORE_LOG("[ConfigManager] Rollback error: No backup available");
        return Result<void, ConfigErrc>::FromError(ConfigErrc::kNoBackupAvailable);
    }
    
    configData_ = backupStack_.back();
    backupStack_.pop_back();
    
    return Result<void, ConfigErrc>::FromValue();
}

Result<void, ConfigErrc> ConfigManager::set(const String& key, const ConfigValue& value) {
    LockGuard lock(mutex_);
    
    try {
        // Split key by '.'
        Vector<String> parts;
        std::istringstream iss(key);
        String part;
        while (std::getline(iss, part, '.')) {
            parts.push_back(part);
        }
        
        // Navigate to the target location
        json* current = &configData_;
        for (size_t i = 0; i < parts.size() - 1; ++i) {
            if (!current->contains(parts[i])) {
                (*current)[parts[i]] = json::object();
            }
            current = &(*current)[parts[i]];
        }
        
        // Get old value for callback
        ConfigValue oldValue;
        if (current->contains(parts.back())) {
            // TODO: Convert json to ConfigValue
        }
        
        // Set new value
        const String& lastKey = parts.back();
        if (value.isBool()) {
            (*current)[lastKey] = value.asBool();
        } else if (value.isInt()) {
            (*current)[lastKey] = value.asInt();
        } else if (value.isDouble()) {
            (*current)[lastKey] = value.asDouble();
        } else if (value.isString()) {
            (*current)[lastKey] = value.asString();
        } else {
            (*current)[lastKey] = nullptr;
        }
        
        // Notify callbacks
        for (const auto& pair : callbacks_) {
            const String& prefix = pair.second.first;
            if (key.find(prefix) == 0 || prefix.empty()) {
                pair.second.second(key, oldValue, value);
            }
        }
        
        return Result<void, ConfigErrc>::FromValue();
        
    } catch (const json::exception& e) {
        INNER_CORE_LOG("[ConfigManager] Set error: %s (key: %s)\n", e.what(), key.c_str());
        return Result<void, ConfigErrc>::FromError(ConfigErrc::kInvalidKey);
    } catch (const std::exception& e) {
        INNER_CORE_LOG("[ConfigManager] Set error: %s (key: %s)\n", e.what(), key.c_str());
        return Result<void, ConfigErrc>::FromError(ConfigErrc::kInternalError);
    }
}

Optional<ConfigValue> ConfigManager::get(const String& key) const {
    LockGuard lock(mutex_);
    
    try {
        // Split key by '.'
        Vector<String> parts;
        std::istringstream iss(key);
        String part;
        while (std::getline(iss, part, '.')) {
            parts.push_back(part);
        }
        
        // Navigate to the value
        const json* current = &configData_;
        for (const auto& p : parts) {
            if (!current->contains(p)) {
                return Optional<ConfigValue>();
            }
            current = &(*current)[p];
        }
        
        // Convert json to ConfigValue
        if (current->is_boolean()) {
            return Optional<ConfigValue>(ConfigValue(current->get<Bool>()));
        } else if (current->is_number_integer()) {
            return Optional<ConfigValue>(ConfigValue(current->get<Int64>()));
        } else if (current->is_number_float()) {
            return Optional<ConfigValue>(ConfigValue(current->get<Double>()));
        } else if (current->is_string()) {
            return Optional<ConfigValue>(ConfigValue(current->get<String>()));
        }
        
        return Optional<ConfigValue>();
        
    } catch (const std::exception&) {
        return Optional<ConfigValue>();
    }
}

Result<void, ConfigErrc> ConfigManager::remove(const String& key) {
    LockGuard lock(mutex_);
    
    try {
        // Split key by '.'
        Vector<String> parts;
        std::istringstream iss(key);
        String part;
        while (std::getline(iss, part, '.')) {
            parts.push_back(part);
        }
        
        // Navigate to parent
        json* current = &configData_;
        for (size_t i = 0; i < parts.size() - 1; ++i) {
            if (!current->contains(parts[i])) {
                return Result<void, ConfigErrc>::FromError(ConfigErrc::kInvalidKey);
            }
            current = &(*current)[parts[i]];
        }
        
        // Remove the key
        current->erase(parts.back());
        return Result<void, ConfigErrc>::FromValue();
        
    } catch (const json::exception& e) {
        INNER_CORE_LOG("[ConfigManager] Remove error: %s (key: %s)\n", e.what(), key.c_str());
        return Result<void, ConfigErrc>::FromError(ConfigErrc::kInvalidKey);
    } catch (const std::exception& e) {
        INNER_CORE_LOG("[ConfigManager] Remove error: %s (key: %s)\n", e.what(), key.c_str());
        return Result<void, ConfigErrc>::FromError(ConfigErrc::kInternalError);
    }
}

Bool ConfigManager::exists(const String& key) const {
    return get(key).has_value();
}

Vector<String> ConfigManager::getKeys(const String& prefix) const {
    LockGuard lock(mutex_);
    Vector<String> result;
    
    try {
        const json* current = &configData_;
        
        if (!prefix.empty()) {
            // Navigate to prefix
            Vector<String> parts;
            std::istringstream iss(prefix);
            String part;
            while (std::getline(iss, part, '.')) {
                parts.push_back(part);
            }
            
            for (const auto& p : parts) {
                if (!current->contains(p)) {
                    return result;
                }
                current = &(*current)[p];
            }
        }
        
        // Get keys at this level
        if (current->is_object()) {
            for (auto it = current->begin(); it != current->end(); ++it) {
                result.push_back(it.key());
            }
        }
        
    } catch (const std::exception&) {
        // Return empty vector
    }
    
    return result;
}

String ConfigManager::getModuleConfig(const String& moduleName, Bool pretty) const {
    LockGuard lock(mutex_);
    
    try {
        if (configData_.contains(moduleName)) {
            if (pretty) {
                return configData_[moduleName].dump(4);
            } else {
                return configData_[moduleName].dump();
            }
        }
        return "{}";
    } catch (const std::exception& e) {
        INNER_CORE_LOG("[ConfigManager] getModuleConfig error: %s\n", e.what());
        return "{}";
    }
}

Result<void, ConfigErrc> ConfigManager::setModuleConfig(const String& moduleName, const String& jsonConfig) {
    LockGuard lock(mutex_);
    
    try {
        json moduleJson = json::parse(jsonConfig);
        configData_[moduleName] = moduleJson;
        return Result<void, ConfigErrc>::FromValue();
    } catch (const json::parse_error& e) {
        INNER_CORE_LOG("[ConfigManager] setModuleConfig parse error: %s (module: %s, byte: %zu)\n", 
                       e.what(), moduleName.c_str(), static_cast<size_t>(e.byte));
        return Result<void, ConfigErrc>::FromError(ConfigErrc::kParseError);
    } catch (const std::exception& e) {
        INNER_CORE_LOG("[ConfigManager] setModuleConfig error: %s (module: %s)\n", e.what(), moduleName.c_str());
        return Result<void, ConfigErrc>::FromError(ConfigErrc::kInternalError);
    }
}

json ConfigManager::getModuleConfigJson(const String& moduleName) const {
    LockGuard lock(mutex_);
    
    try {
        if (configData_.contains(moduleName)) {
            return configData_[moduleName];
        }
        return json::object();
    } catch (const std::exception& e) {
        INNER_CORE_LOG("[ConfigManager] getModuleConfigJson error: %s\n", e.what());
        return json::object();
    }
}

Result<void, ConfigErrc> ConfigManager::setModuleConfigJson(const String& moduleName, const json& jsonConfig) {
    LockGuard lock(mutex_);
    
    try {
        if (!jsonConfig.is_object() && !jsonConfig.is_array()) {
            INNER_CORE_LOG("[ConfigManager] setModuleConfigJson error: Config must be object or array (module: %s)\n", 
                           moduleName.c_str());
            return Result<void, ConfigErrc>::FromError(ConfigErrc::kValidationError);
        }
        configData_[moduleName] = jsonConfig;
        
        // Set update policy for this module to "default" (kOnChangeUpdate)
        modulePolicies_[moduleName] = UpdatePolicy::kOnChangeUpdate;  // "default" policy
        explicitPolicyModules_.insert(moduleName);
        
        // Update __update_policy__ in configData_ for consistency
        if (!configData_.contains(FIELD_UPDATE_POLICY)) {
            configData_[FIELD_UPDATE_POLICY] = json::object();
        }
        if (configData_[FIELD_UPDATE_POLICY].is_object()) {
            configData_[FIELD_UPDATE_POLICY][moduleName] = POLICY_DEFAULT_KEY;
        }
        
        // Keep policy materialized after module content change
        materializePolicyFieldLocked(moduleName, configData_);
        return Result<void, ConfigErrc>::FromValue();
    } catch (const std::exception& e) {
        INNER_CORE_LOG("[ConfigManager] setModuleConfigJson error: %s (module: %s)\n", e.what(), moduleName.c_str());
        return Result<void, ConfigErrc>::FromError(ConfigErrc::kInternalError);
    }
}

Bool ConfigManager::getBool(const String& key, Bool defaultValue) const {
    auto value = get(key);
    return value.has_value() ? value.value().asBool(defaultValue) : defaultValue;
}

Int64 ConfigManager::getInt(const String& key, Int64 defaultValue) const {
    auto value = get(key);
    return value.has_value() ? value.value().asInt(defaultValue) : defaultValue;
}

Double ConfigManager::getDouble(const String& key, Double defaultValue) const {
    auto value = get(key);
    return value.has_value() ? value.value().asDouble(defaultValue) : defaultValue;
}

String ConfigManager::getString(const String& key, const String& defaultValue) const {
    auto value = get(key);
    return value.has_value() ? value.value().asString(defaultValue) : defaultValue;
}

Result<void, ConfigErrc> ConfigManager::setBool(const String& key, Bool value) {
    return set(key, ConfigValue(value));
}

Result<void, ConfigErrc> ConfigManager::setInt(const String& key, Int64 value) {
    return set(key, ConfigValue(value));
}

Result<void, ConfigErrc> ConfigManager::setDouble(const String& key, Double value) {
    return set(key, ConfigValue(value));
}

Result<void, ConfigErrc> ConfigManager::setString(const String& key, const String& value) {
    return set(key, ConfigValue(value));
}

UInt32 ConfigManager::registerChangeCallback(const String& prefix, ConfigChangeCallback callback) {
    LockGuard lock(mutex_);
    UInt32 id = nextCallbackId_++;
    callbacks_[id] = std::make_pair(prefix, callback);
    return id;
}

void ConfigManager::unregisterChangeCallback(UInt32 callbackId) {
    LockGuard lock(mutex_);
    callbacks_.erase(callbackId);
}

String ConfigManager::toJson(Bool pretty) const {
    LockGuard lock(mutex_);
    if (pretty) {
        return configData_.dump(4);
    } else {
        return configData_.dump();
    }
}

// ============================================================================
// Update Policy: public APIs
// ============================================================================

const Char* ConfigManager::policyToString(UpdatePolicy p) {
    switch (p) {
        case UpdatePolicy::kNoUpdate:      return "none";
        case UpdatePolicy::kFirstUpdate:   return "first";
        case UpdatePolicy::kAlwaysUpdate:  return "always";
        case UpdatePolicy::kOnChangeUpdate:return "on_change";
        default:                           return "first";
    }
}

Optional<ConfigManager::UpdatePolicy> ConfigManager::parsePolicyString(const String& s) {
    if (s == "none") return UpdatePolicy::kNoUpdate;
    if (s == "first") return UpdatePolicy::kFirstUpdate;
    if (s == "always") return UpdatePolicy::kAlwaysUpdate;
    if (s == "on_change") return UpdatePolicy::kOnChangeUpdate;
    return Optional<UpdatePolicy>();
}

ConfigManager::UpdatePolicy ConfigManager::getModuleUpdatePolicy(const String& moduleName) const {
    LockGuard lock(mutex_);
    auto it = modulePolicies_.find(moduleName);
    if (it != modulePolicies_.end()) return it->second;
    return defaultPolicy_; // use runtime default instead of static constant
}

Result<void, ConfigErrc> ConfigManager::setModuleUpdatePolicy(const String& moduleName, UpdatePolicy policy) {
    LockGuard lock(mutex_);
    modulePolicies_[moduleName] = policy;
    explicitPolicyModules_.insert(moduleName);
    // Reflect policy into current config data for persistence / manual editing
    materializePolicyFieldLocked(moduleName, configData_);
    return Result<void, ConfigErrc>::FromValue();
}

Result<void, ConfigErrc> ConfigManager::setModuleUpdatePolicy(const String& moduleName, const String& policyStr) {
    auto p = parsePolicyString(policyStr);
    if (!p.has_value()) {
        return Result<void, ConfigErrc>::FromError(ConfigErrc::kValidationError);
    }
    return setModuleUpdatePolicy(moduleName, p.value());
}

// ============================================================================
// Update Policy: internals
// ============================================================================

void ConfigManager::refreshPoliciesFromConfigLocked() {
    modulePolicies_.clear();
    // First, load top-level mapping if present
    Map<String, UpdatePolicy> topMap;
    explicitPolicyModules_.clear();
    
    // Reset default policy to fallback
    defaultPolicy_ = UpdatePolicy::kOnChangeUpdate;
    
    try {
        if (configData_.contains(FIELD_UPDATE_POLICY) && configData_[FIELD_UPDATE_POLICY].is_object()) {
            const json& m = configData_[FIELD_UPDATE_POLICY];
            
            // Read default policy if present
            if (m.contains(POLICY_DEFAULT_KEY) && m[POLICY_DEFAULT_KEY].is_string()) {
                auto defPol = parsePolicyString(m[POLICY_DEFAULT_KEY].get<String>());
                if (defPol.has_value()) {
                    defaultPolicy_ = defPol.value();
                }
            }
            
            for (auto it = m.begin(); it != m.end(); ++it) {
                if (it.key() == POLICY_DEFAULT_KEY) continue; // skip default key
                if (it.value().is_string()) {
                    auto pp = parsePolicyString(it.value().get<String>());
                    if (pp.has_value()) {
                        topMap[it.key()] = pp.value();
                        explicitPolicyModules_.insert(it.key());
                    }
                }
            }
        }
    } catch (...) {
        // ignore mapping errors
    }

    for (auto it = configData_.begin(); it != configData_.end(); ++it) {
        const String moduleName = it.key();
        if (moduleName == FIELD_UPDATE_POLICY || moduleName == FIELD_METADATA) continue;
        UpdatePolicy pol = defaultPolicy_; // use loaded default
        try {
            if (it.value().is_object()) {
                // No per-module policy field; use top-level mapping
                auto itTop = topMap.find(moduleName);
                if (itTop != topMap.end()) pol = itTop->second;
            } else {
                // Non-object module: use top-level mapping if any
                auto itTop = topMap.find(moduleName);
                if (itTop != topMap.end()) pol = itTop->second;
            }
        } catch (...) {
            pol = kDefaultUpdatePolicy;
        }
        modulePolicies_[moduleName] = pol;
    }
}

UInt32 ConfigManager::computeModuleCrcLocked(const json& moduleJson) const {
    try {
        if (moduleJson.is_object()) {
            json tmp = moduleJson;
            // Strip legacy embedded policy field if present (for backward compatibility)
            if (tmp.contains("__update_policy__")) tmp.erase("__update_policy__");
            return computeCrc32(tmp.dump());
        }
        return computeCrc32(moduleJson.dump());
    } catch (...) {
        return 0u;
    }
}

void ConfigManager::materializePolicyFieldLocked(const String& moduleName, json& rootJson) {
    UpdatePolicy pol = defaultPolicy_; // use runtime default
    auto itP = modulePolicies_.find(moduleName);
    if (itP != modulePolicies_.end()) pol = itP->second;

    // Only persist explicit policies; default ones are omitted
    if (explicitPolicyModules_.find(moduleName) != explicitPolicyModules_.end()) {
        if (!rootJson.contains(FIELD_UPDATE_POLICY)) {
            rootJson[FIELD_UPDATE_POLICY] = json::object();
        }
        rootJson[FIELD_UPDATE_POLICY][moduleName] = policyToString(pol);
    } else if (rootJson.contains(FIELD_UPDATE_POLICY) && rootJson[FIELD_UPDATE_POLICY].is_object()) {
        // Remove this module from policy map if it was there but now uses default
        rootJson[FIELD_UPDATE_POLICY].erase(moduleName);
    }

    // Ensure no embedded policy fields remain in module objects (clean legacy)
    if (rootJson.contains(moduleName) && rootJson[moduleName].is_object()) {
        if (rootJson[moduleName].contains("__update_policy__")) {
            rootJson[moduleName].erase("__update_policy__");
        }
    }
}

Result<void, ConfigErrc> ConfigManager::fromJson(const String& jsonStr) {
    LockGuard lock(mutex_);
    
    try {
        json parsed = json::parse(jsonStr);
        if (!parsed.is_object()) {
            INNER_CORE_LOG("[ConfigManager] fromJson error: Root must be a JSON object\n");
            return Result<void, ConfigErrc>::FromError(ConfigErrc::kValidationError);
        }
        configData_ = parsed;
        return Result<void, ConfigErrc>::FromValue();
    } catch (const json::parse_error& e) {
        INNER_CORE_LOG("[ConfigManager] fromJson parse error\n");
        INNER_CORE_LOG("  Error: %s\n", e.what());
        INNER_CORE_LOG("  Position: byte %zu\n", static_cast<size_t>(e.byte));
        return Result<void, ConfigErrc>::FromError(ConfigErrc::kParseError);
    } catch (const std::exception& e) {
        INNER_CORE_LOG("[ConfigManager] fromJson error: %s\n", e.what());
        return Result<void, ConfigErrc>::FromError(ConfigErrc::kInternalError);
    }
}

void ConfigManager::clear() {
    LockGuard lock(mutex_);
    configData_ = json::object();
    backupStack_.clear();
    callbacks_.clear();
}

// ============================================================================
// Private Helpers
// ============================================================================

UInt32 ConfigManager::computeCrc32(const String& data) const {
    uLong crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, reinterpret_cast<const Bytef*>(data.c_str()), data.length());
    return static_cast<UInt32>(crc);
}

String ConfigManager::computeHmacSha256(const String& data, const String& key) const {
    UInt8 hash[EVP_MAX_MD_SIZE];
    UInt32 hashLen = 0;
    
    HMAC(EVP_sha256(), 
         key.c_str(), key.length(),
         reinterpret_cast<const UInt8*>(data.c_str()), data.length(),
         hash, &hashLen);
    
    return bytesToHex(hash, hashLen);
}

String ConfigManager::getCurrentTimestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

Bool ConfigManager::validateTimestamp(const String& timestamp) const {
    // Simple validation: check format and reasonable range
    if (timestamp.length() != 19) return false;
    return true;
}

Result<String, ConfigErrc> ConfigManager::readFile(const String& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return Result<String, ConfigErrc>::FromError(ConfigErrc::kFileNotFound);
    }
    
    std::ostringstream oss;
    oss << file.rdbuf();
    return Result<String, ConfigErrc>::FromValue(oss.str());
}

Result<void, ConfigErrc> ConfigManager::writeFile(const String& path, const String& data) {
    std::ofstream file(path);
    if (!file.is_open()) {
        return Result<void, ConfigErrc>::FromError(ConfigErrc::kInternalError);
    }
    
    file << data;
    return Result<void, ConfigErrc>::FromValue();
}

} // namespace core
} // namespace lap
