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

using json = nlohmann::json;

namespace lap {
namespace core {

namespace {
    // Default configuration file name
    const String DEFAULT_CONFIG_FILE = "config.json";
    
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
    
    // Base64 encoding/decoding now provided by Crypto::Util
}

// ============================================================================
// Helpers: Convert between nlohmann::json and ConfigValue (full: scalar/array/object)
// ============================================================================

static ConfigValue JsonToConfigValue(const json& j) {
    if (j.is_null()) {
        return ConfigValue();
    } else if (j.is_boolean()) {
        return ConfigValue(j.get<Bool>());
    } else if (j.is_number_integer()) {
        return ConfigValue(j.get<Int64>());
    } else if (j.is_number_unsigned()) {
        // Narrow unsigned to signed range; callers should be aware of potential overflow
        UInt64 uv = j.get<UInt64>();
        return ConfigValue(static_cast<Int64>(uv));
    } else if (j.is_number_float()) {
        return ConfigValue(j.get<Double>());
    } else if (j.is_string()) {
        return ConfigValue(j.get<String>());
    } else if (j.is_array()) {
        ConfigValue arr;
        for (const auto& el : j) {
            arr.Append(JsonToConfigValue(el));
        }
        return arr;
    } else if (j.is_object()) {
        ConfigValue obj;
        for (auto it = j.begin(); it != j.end(); ++it) {
            obj[it.key()] = JsonToConfigValue(it.value());
        }
        return obj;
    }
    return ConfigValue();
}

// ============================================================================
// ConfigValue Implementation (kept for API compatibility)
// ============================================================================

ConfigValue::ConfigValue() : m_type(ConfigValueType::kNull) {}

ConfigValue::ConfigValue(Bool value) 
    : m_type(ConfigValueType::kBoolean), m_bValue(value) {}

ConfigValue::ConfigValue(Int64 value)
    : m_type(ConfigValueType::kInteger), m_iValue(value) {}

ConfigValue::ConfigValue(Double value)
    : m_type(ConfigValueType::kDouble), m_dValue(value) {}

ConfigValue::ConfigValue(const String& value)
    : m_type(ConfigValueType::kString), m_strValue(value) {}

ConfigValue::ConfigValue(const Char* value)
    : m_type(ConfigValueType::kString), m_strValue(value) {}

Bool ConfigValue::AsBool(Bool defaultValue) const noexcept {
    if (m_type == ConfigValueType::kBoolean) return m_bValue;
    return defaultValue;
}

Int64 ConfigValue::AsInt(Int64 defaultValue) const noexcept {
    if (m_type == ConfigValueType::kInteger) return m_iValue;
    return defaultValue;
}

Double ConfigValue::AsDouble(Double defaultValue) const noexcept {
    if (m_type == ConfigValueType::kDouble) return m_dValue;
    return defaultValue;
}

String ConfigValue::AsString(const String& defaultValue) const noexcept {
    if (m_type == ConfigValueType::kString) return m_strValue;
    return defaultValue;
}

Size ConfigValue::ArraySize() const noexcept {
    if (m_type == ConfigValueType::kArray) return m_vecArrayValue.size();
    return 0;
}

ConfigValue& ConfigValue::operator[](Size index) {
    if (m_type != ConfigValueType::kArray) {
        m_type = ConfigValueType::kArray;
        m_vecArrayValue.clear();
    }
    if (index >= m_vecArrayValue.size()) {
        m_vecArrayValue.resize(index + 1);
    }
    return m_vecArrayValue[index];
}

const ConfigValue& ConfigValue::operator[](Size index) const {
    static ConfigValue null;
    if (m_type != ConfigValueType::kArray || index >= m_vecArrayValue.size()) {
        return null;
    }
    return m_vecArrayValue[index];
}

void ConfigValue::Append(const ConfigValue& value) {
    if (m_type != ConfigValueType::kArray) {
        m_type = ConfigValueType::kArray;
        m_vecArrayValue.clear();
    }
    m_vecArrayValue.push_back(value);
}

Bool ConfigValue::HasKey(const String& key) const noexcept {
    if (m_type != ConfigValueType::kObject) return false;
    return m_mapObjectValue.find(key) != m_mapObjectValue.end();
}

ConfigValue& ConfigValue::operator[](const String& key) {
    if (m_type != ConfigValueType::kObject) {
        m_type = ConfigValueType::kObject;
        m_mapObjectValue.clear();
    }
    return m_mapObjectValue[key];
}

const ConfigValue& ConfigValue::operator[](const String& key) const {
    static ConfigValue null;
    if (m_type != ConfigValueType::kObject) return null;
    auto it = m_mapObjectValue.find(key);
    if (it == m_mapObjectValue.end()) return null;
    return it->second;
}

Vector<String> ConfigValue::GetKeys() const {
    Vector<String> keys;
    if (m_type == ConfigValueType::kObject) {
        for (const auto& pair : m_mapObjectValue) {
            keys.push_back(pair.first);
        }
    }
    return keys;
}

String ConfigValue::ToJsonString(Bool pretty) const {
    std::ostringstream oss;
    ToJsonString(oss, 0, pretty);
    return oss.str();
}

void ConfigValue::ToJsonString(std::ostream& os, Int32 indent, Bool pretty) const {
    auto doIndent = [&](Int32 level) {
        if (pretty) {
            for (Int32 i = 0; i < level * 2; ++i) os << ' ';
        }
    };

    switch (m_type) {
        case ConfigValueType::kNull:
            os << "null";
            break;
        case ConfigValueType::kBoolean:
            os << (m_bValue ? "true" : "false");
            break;
        case ConfigValueType::kInteger:
            os << m_iValue;
            break;
        case ConfigValueType::kDouble:
            os << std::fixed << std::setprecision(6) << m_dValue;
            break;
        case ConfigValueType::kString:
            os << "\"" << m_strValue << "\"";
            break;
        case ConfigValueType::kArray:
            os << "[";
            if (pretty) os << "\n";
            for (Size i = 0; i < m_vecArrayValue.size(); ++i) {
                if (pretty) doIndent(indent + 1);
                m_vecArrayValue[i].ToJsonString(os, indent + 1, pretty);
                if (i < m_vecArrayValue.size() - 1) os << ",";
                if (pretty) os << "\n";
            }
            if (pretty) doIndent(indent);
            os << "]";
            break;
        case ConfigValueType::kObject:
            os << "{";
            if (pretty) os << "\n";
            Size count = 0;
            for (const auto& pair : m_mapObjectValue) {
                if (pretty) doIndent(indent + 1);
                os << "\"" << pair.first << "\": ";
                pair.second.ToJsonString(os, indent + 1, pretty);
                if (++count < m_mapObjectValue.size()) os << ",";
                if (pretty) os << "\n";
            }
            if (pretty) doIndent(indent);
            os << "}";
            break;
    }
}

ConfigValue ConfigValue::FromJsonString(const String& jsonStr) {
    try {
        json j = json::parse(jsonStr);
        return JsonToConfigValue(j);
    } catch (...) {
        return ConfigValue();
    }
}

// ============================================================================
// ConfigManager Implementation
// ============================================================================

ConfigManager::ConfigManager()
    : m_bEnableSecurity(true)
    , m_bInitialized(false)
    , m_iNextCallbackId(1)
    , m_defaultPolicy(UpdatePolicy::kOnChangeUpdate)
{
    // HMAC key is automatically loaded by Crypto's constructor from environment
    // No manual key loading required here

    // Crypto/OpenSSL lifecycle is managed in Crypto utilities; no Config-level init required
    
    // Initialize with empty JSON object
    m_configData = json::object();
    
    // Initialize metadata with defaults
    m_metadata.m_iVersion = 1;
    m_metadata.m_bEncrypted = false;
    m_metadata.m_strDescription = "";
    
    // Automatically Initialize with default config file
    // This will load config.json if it exists, otherwise start with empty config
    auto result = Initialize(DEFAULT_CONFIG_FILE, m_bEnableSecurity);
    if (!result.HasValue()) {
        INNER_CORE_LOG("[ConfigManager] Initialize with default config file failed, starting with empty config\n");
    }
}

ConfigManager::~ConfigManager() {
    // Automatically Save configuration on destruction with full security
    // OpenSSL cleanup is disabled at init, so it's safe to use HMAC here
    if (m_bInitialized && !m_strConfigPath.empty()) {
        INNER_CORE_LOG("[ConfigManager] Auto-saving configuration on destruction\n");
        auto result = Save(true);
        if (!result.HasValue()) {
            INNER_CORE_LOG("[ConfigManager] Failed to Save configuration on destruction: error=%d\n", 
                           static_cast<int>(result.Error()));
        } else {
            INNER_CORE_LOG("[ConfigManager] Configuration saved successfully on destruction\n");
        }
    }
    
    // Crypto/OpenSSL cleanup is handled by the process/crypto library; no explicit cleanup here
}

ConfigManager& ConfigManager::GetInstance() {
    static ConfigManager instance;
    return instance;
}

Result<void, ConfigErrc> ConfigManager::Initialize(const String& configPath, Bool enableSecurity) {
    RecursiveLockGuard lock(m_mutex);
    
    m_strConfigPath = configPath;
    m_bEnableSecurity = enableSecurity;
    m_bInitialized = true;
    
    // Try to load existing configuration
    auto loadResult = Load();
    if (!loadResult.HasValue() && loadResult.Error() != ConfigErrc::kFileNotFound) {
        return loadResult;
    }
    
    return Result<void, ConfigErrc>::FromValue();
}

void ConfigManager::SetBase64Encoding(Bool enable) {
    RecursiveLockGuard lock(m_mutex);
    m_metadata.m_bEncrypted = enable;
}

Bool ConfigManager::IsBase64Enabled() const {
    RecursiveLockGuard lock(m_mutex);
    return m_metadata.m_bEncrypted;
}

ConfigMetadata ConfigManager::GetMetadata() const {
    RecursiveLockGuard lock(m_mutex);
    return m_metadata;
}

void ConfigManager::SetVersion(UInt32 version) {
    RecursiveLockGuard lock(m_mutex);
    m_metadata.m_iVersion = version;
}

UInt32 ConfigManager::GetVersion() const {
    RecursiveLockGuard lock(m_mutex);
    return m_metadata.m_iVersion;
}

void ConfigManager::SetDescription(const String& description) {
    RecursiveLockGuard lock(m_mutex);
    m_metadata.m_strDescription = description;
}

String ConfigManager::GetDescription() const {
    RecursiveLockGuard lock(m_mutex);
    return m_metadata.m_strDescription;
}

Result<void, ConfigErrc> ConfigManager::Load(Bool skipVerification) {
    RecursiveLockGuard lock(m_mutex);
    
    if (!m_bInitialized) {
        INNER_CORE_LOG("[ConfigManager] Load error: Not initialized\n");
        return Result<void, ConfigErrc>::FromError(ConfigErrc::kInternalError);
    }
    
    // Read file
    auto fileData = readFile(m_strConfigPath);
    if (!fileData.HasValue()) {
        INNER_CORE_LOG("[ConfigManager] Load error: Cannot read file '%s'\n", m_strConfigPath.c_str());
        return Result<void, ConfigErrc>::FromError(fileData.Error());
    }
    
    try {
        String jsonStr = fileData.Value();
        
        // Decode from Base64 if encrypted flag is set in metadata
        if (m_metadata.m_bEncrypted && jsonStr.length() > 0 && jsonStr[0] != '{') {
            String decoded = Crypto::Util::base64DecodeToString(jsonStr);
            if (decoded.empty() && !jsonStr.empty()) {
                INNER_CORE_LOG("[ConfigManager] Base64 decode error\n");
                return Result<void, ConfigErrc>::FromError(ConfigErrc::kParseError);
            }
            jsonStr.swap(decoded);
        }
        
        // Parse JSON
        json fullJson = json::parse(jsonStr);
        
        // Extract metadata and security checksums
        String storedCrc, storedTimestamp, storedHmac;
        if (fullJson.contains(FIELD_METADATA)) {
            const json& metaJson = fullJson[FIELD_METADATA];
            if (metaJson.contains(META_VERSION)) {
                m_metadata.m_iVersion = metaJson[META_VERSION].get<UInt32>();
            }
            if (metaJson.contains(META_DESCRIPTION)) {
                m_metadata.m_strDescription = metaJson[META_DESCRIPTION].get<String>();
            }
            if (metaJson.contains(META_ENCRYPTED)) {
                m_metadata.m_bEncrypted = metaJson[META_ENCRYPTED].get<Bool>();
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
        if (m_bEnableSecurity && !skipVerification && !storedCrc.empty()) {
            // Step 1: Verify CRC32
            UInt32 computedCrc = Crypto::Util::computeCrc32(
                reinterpret_cast<const UInt8*>(coreJson.c_str()), 
                coreJson.length()
            );
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
                if (!m_crypto.verifyHmac(
                    reinterpret_cast<const UInt8*>(coreJson.c_str()),
                    coreJson.length(),
                    storedHmac)) {
                    INNER_CORE_LOG("[ConfigManager] HMAC verification failed\n");
                    return Result<void, ConfigErrc>::FromError(ConfigErrc::kHmacMismatch);
                }
            }
        }
        
        // Store core configuration
        m_configData = fullJson;
        m_lastPersistedData = m_configData;
        
        // Update metadata security fields
        m_metadata.m_strCrc = storedCrc;
        m_metadata.m_strTimestamp = storedTimestamp;
        m_metadata.m_strHmac = storedHmac;

        // Refresh policies from config fields and Initialize tracking baselines
        refreshPoliciesFromConfigLocked();
        m_setModuleSavedOnce.clear();
        m_mapModuleLastCrc.clear();
        for (auto it = m_configData.begin(); it != m_configData.end(); ++it) {
            const String moduleName = it.key();
            if (moduleName == FIELD_UPDATE_POLICY || moduleName == FIELD_METADATA) continue;
            // mark as already saved once if present in persisted file
            m_setModuleSavedOnce.insert(moduleName);
            if (it.value().is_object() || it.value().is_array() || it.value().is_primitive()) {
                m_mapModuleLastCrc[moduleName] = computeModuleCrcLocked(it.value());
            }
        }
        
        return Result<void, ConfigErrc>::FromValue();
        
    } catch (const json::exception& e) {
        INNER_CORE_LOG("[ConfigManager] JSON parse error: %s (file: %s)\n", e.what(), m_strConfigPath.c_str());
        return Result<void, ConfigErrc>::FromError(ConfigErrc::kParseError);
    } catch (const ConfigException& e) {
        INNER_CORE_LOG("[ConfigManager] Configuration error: %s\n", e.what());
        return Result<void, ConfigErrc>::FromError(ConfigErrc::kValidationError);
    } catch (const std::exception& e) {
        INNER_CORE_LOG("[ConfigManager] Load error: %s\n", e.what());
        return Result<void, ConfigErrc>::FromError(ConfigErrc::kInternalError);
    }
}

Result<void, ConfigErrc> ConfigManager::Save(Bool enableSecurity) {
    RecursiveLockGuard lock(m_mutex);
    
    if (!m_bInitialized) {
        INNER_CORE_LOG("[ConfigManager] Save error: Not initialized\n");
        return Result<void, ConfigErrc>::FromError(ConfigErrc::kInternalError);
    }
    
    try {
        // Build core JSON to persist according to per-module update policies
        json toPersist = json::object();

        // Preserve existing top-level policy mapping (will be updated)
        if (m_configData.contains(FIELD_UPDATE_POLICY)) {
            toPersist[FIELD_UPDATE_POLICY] = m_configData[FIELD_UPDATE_POLICY];
        }

        for (auto it = m_configData.begin(); it != m_configData.end(); ++it) {
            const String moduleName = it.key();
            if (moduleName == FIELD_UPDATE_POLICY || moduleName == FIELD_METADATA) {
                // handled separately
                continue;
            }

            const json& currentModule = it.value();
            json previousModule = m_lastPersistedData.contains(moduleName) ? m_lastPersistedData[moduleName] : json();
            UpdatePolicy policy = GetModuleUpdatePolicy(moduleName);

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
                    Bool alreadySaved = m_lastPersistedData.contains(moduleName);
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
                    auto itC = m_mapModuleLastCrc.find(moduleName);
                    if (itC != m_mapModuleLastCrc.end() && itC->second == curCrc) {
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
        // Preserve existing module policies from m_configData
        if (m_configData.contains(FIELD_UPDATE_POLICY) && m_configData[FIELD_UPDATE_POLICY].is_object()) {
            for (auto it = m_configData[FIELD_UPDATE_POLICY].begin(); 
                 it != m_configData[FIELD_UPDATE_POLICY].end(); ++it) {
                toPersist[FIELD_UPDATE_POLICY][it.key()] = it.value();
            }
        }
        // Ensure default policy key is present
        toPersist[FIELD_UPDATE_POLICY][POLICY_DEFAULT_KEY] = policyToString(m_defaultPolicy);

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
        metaJson[META_VERSION] = m_metadata.m_iVersion;
        metaJson[META_DESCRIPTION] = m_metadata.m_strDescription;
        metaJson[META_ENCRYPTED] = m_metadata.m_bEncrypted;
        
        if (enableSecurity && m_bEnableSecurity) {
            // Compute security fields using Crypto utilities
            UInt32 crc = Crypto::Util::computeCrc32(
                reinterpret_cast<const UInt8*>(coreJson.c_str()),
                coreJson.length()
            );
            String timestamp = getCurrentTimestamp();
            String hmac = m_crypto.computeHmac(
                reinterpret_cast<const UInt8*>(coreJson.c_str()),
                coreJson.length()
            );
            
            // Convert CRC to hex string
            std::ostringstream crcStream;
            crcStream << std::hex << std::setw(8) << std::setfill('0') << crc;
            
            // Add security fields to metadata
            metaJson[META_CRC] = crcStream.str();
            metaJson[META_TIMESTAMP] = timestamp;
            metaJson[META_HMAC] = hmac;
            
            // Update internal metadata
            m_metadata.m_strCrc = metaJson[META_CRC];
            m_metadata.m_strTimestamp = timestamp;
            m_metadata.m_strHmac = hmac;
        }
        
        // Add __metadata__ to full JSON
        fullJson[FIELD_METADATA] = metaJson;
        
        // Serialize to string (pretty format for human readability)
        String jsonOutput = fullJson.dump(4);
        
        // Encode to Base64 if encrypted flag is set
        if (m_metadata.m_bEncrypted) {
            jsonOutput = Crypto::Util::base64Encode(jsonOutput);
            if (jsonOutput.empty()) {
                INNER_CORE_LOG("[ConfigManager] Base64 encode error\n");
                return Result<void, ConfigErrc>::FromError(ConfigErrc::kInternalError);
            }
        }
        
        auto writeResult = writeFile(m_strConfigPath, jsonOutput);
        if (!writeResult.HasValue()) {
            INNER_CORE_LOG("[ConfigManager] Save error: Cannot write to file '%s'\n", m_strConfigPath.c_str());
        }

        if (writeResult.HasValue()) {
            // Update persisted snapshot and CRCs after successful Save
            m_lastPersistedData = toPersist;
            m_mapModuleLastCrc.clear();
            for (auto it = toPersist.begin(); it != toPersist.end(); ++it) {
                const String moduleName = it.key();
                if (moduleName == FIELD_UPDATE_POLICY) continue;
                m_mapModuleLastCrc[moduleName] = computeModuleCrcLocked(it.value());
            }
            // Update FirstUpdate tracking
            for (auto it = toPersist.begin(); it != toPersist.end(); ++it) {
                const String moduleName = it.key();
                if (moduleName == FIELD_UPDATE_POLICY) continue;
                m_setModuleSavedOnce.insert(moduleName);
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

Result<void, ConfigErrc> ConfigManager::CreateBackup() {
    RecursiveLockGuard lock(m_mutex);
    
    m_vecBackupStack.push_back(m_configData);
    
    // Keep max 10 backups
    if (m_vecBackupStack.size() > 10) {
        m_vecBackupStack.erase(m_vecBackupStack.begin());
    }
    
    return Result<void, ConfigErrc>::FromValue();
}

Result<void, ConfigErrc> ConfigManager::Rollback() {
    RecursiveLockGuard lock(m_mutex);
    
    if (m_vecBackupStack.empty()) {
        INNER_CORE_LOG("[ConfigManager] Rollback error: No backup available");
        return Result<void, ConfigErrc>::FromError(ConfigErrc::kNoBackupAvailable);
    }
    
    m_configData = m_vecBackupStack.back();
    m_vecBackupStack.pop_back();
    
    return Result<void, ConfigErrc>::FromValue();
}

Result<void, ConfigErrc> ConfigManager::SetValue(const String& key, const ConfigValue& value) {
    RecursiveLockGuard lock(m_mutex);
    
    try {
        // Split key by '.'
        Vector<String> parts;
        std::istringstream iss(key);
        String part;
        while (std::getline(iss, part, '.')) {
            parts.push_back(part);
        }
        
        // Navigate to the target location
        json* current = &m_configData;
        for (size_t i = 0; i < parts.size() - 1; ++i) {
            if (!current->contains(parts[i])) {
                (*current)[parts[i]] = json::object();
            }
            current = &(*current)[parts[i]];
        }
        
        // Get old value for callback (full conversion)
        ConfigValue oldValue;
        if (current->contains(parts.back())) {
            oldValue = JsonToConfigValue((*current)[parts.back()]);
        }
        
        // Set new value
        const String& lastKey = parts.back();
        if (value.IsBool()) {
            (*current)[lastKey] = value.AsBool();
        } else if (value.IsInt()) {
            (*current)[lastKey] = value.AsInt();
        } else if (value.IsDouble()) {
            (*current)[lastKey] = value.AsDouble();
        } else if (value.IsString()) {
            (*current)[lastKey] = value.AsString();
        } else {
            (*current)[lastKey] = nullptr;
        }
        
        // Notify callbacks
        for (const auto& pair : m_mapCallbacks) {
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

Optional<ConfigValue> ConfigManager::Get(const String& key) const {
    RecursiveLockGuard lock(m_mutex);
    
    try {
        // Split key by '.'
        Vector<String> parts;
        std::istringstream iss(key);
        String part;
        while (std::getline(iss, part, '.')) {
            parts.push_back(part);
        }
        
        // Navigate to the value
        const json* current = &m_configData;
        for (const auto& p : parts) {
            if (!current->contains(p)) {
                return Optional<ConfigValue>();
            }
            current = &(*current)[p];
        }
        
        // Convert json to ConfigValue (supports scalars, arrays, and objects)
        return Optional<ConfigValue>(JsonToConfigValue(*current));
        
    } catch (const std::exception&) {
        return Optional<ConfigValue>();
    }
}

Result<void, ConfigErrc> ConfigManager::Remove(const String& key) {
    RecursiveLockGuard lock(m_mutex);
    
    try {
        // Split key by '.'
        Vector<String> parts;
        std::istringstream iss(key);
        String part;
        while (std::getline(iss, part, '.')) {
            parts.push_back(part);
        }
        
        // Navigate to parent
        json* current = &m_configData;
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

Bool ConfigManager::Exists(const String& key) const {
    return Get(key).has_value();
}

Vector<String> ConfigManager::GetKeys(const String& prefix) const {
    RecursiveLockGuard lock(m_mutex);
    Vector<String> result;
    
    try {
        const json* current = &m_configData;
        
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

String ConfigManager::GetModuleConfig(const String& moduleName, Bool pretty) const {
    RecursiveLockGuard lock(m_mutex);
    
    try {
        if (m_configData.contains(moduleName)) {
            if (pretty) {
                return m_configData[moduleName].dump(4);
            } else {
                return m_configData[moduleName].dump();
            }
        }
        return "{}";
    } catch (const std::exception& e) {
        INNER_CORE_LOG("[ConfigManager] GetModuleConfig error: %s\n", e.what());
        return "{}";
    }
}

Result<void, ConfigErrc> ConfigManager::SetModuleConfig(const String& moduleName, const String& jsonConfig) {
    RecursiveLockGuard lock(m_mutex);
    
    try {
        json moduleJson = json::parse(jsonConfig);
        m_configData[moduleName] = moduleJson;
        return Result<void, ConfigErrc>::FromValue();
    } catch (const json::parse_error& e) {
        INNER_CORE_LOG("[ConfigManager] SetModuleConfig parse error: %s (module: %s, byte: %zu)\n", 
                       e.what(), moduleName.c_str(), static_cast<size_t>(e.byte));
        return Result<void, ConfigErrc>::FromError(ConfigErrc::kParseError);
    } catch (const std::exception& e) {
        INNER_CORE_LOG("[ConfigManager] SetModuleConfig error: %s (module: %s)\n", e.what(), moduleName.c_str());
        return Result<void, ConfigErrc>::FromError(ConfigErrc::kInternalError);
    }
}

json ConfigManager::GetModuleConfigJson(const String& moduleName) const {
    RecursiveLockGuard lock(m_mutex);
    
    try {
        if (m_configData.contains(moduleName)) {
            return m_configData[moduleName];
        }
        return json::object();
    } catch (const std::exception& e) {
        INNER_CORE_LOG("[ConfigManager] GetModuleConfigJson error: %s\n", e.what());
        return json::object();
    }
}

Result<void, ConfigErrc> ConfigManager::SetModuleConfigJson(const String& moduleName, const json& jsonConfig) {
    RecursiveLockGuard lock(m_mutex);
    
    try {
        if (!jsonConfig.is_object() && !jsonConfig.is_array()) {
            INNER_CORE_LOG("[ConfigManager] SetModuleConfigJson error: Config must be object or array (module: %s)\n", 
                           moduleName.c_str());
            return Result<void, ConfigErrc>::FromError(ConfigErrc::kValidationError);
        }
        m_configData[moduleName] = jsonConfig;
        
        // Set update policy for this module to "default" (kOnChangeUpdate)
        m_mapModulePolicies[moduleName] = UpdatePolicy::kOnChangeUpdate;  // "default" policy
        m_setExplicitPolicyModules.insert(moduleName);
        
        // Update __update_policy__ in m_configData for consistency
        if (!m_configData.contains(FIELD_UPDATE_POLICY)) {
            m_configData[FIELD_UPDATE_POLICY] = json::object();
        }
        if (m_configData[FIELD_UPDATE_POLICY].is_object()) {
            m_configData[FIELD_UPDATE_POLICY][moduleName] = POLICY_DEFAULT_KEY;
        }
        
        // Keep policy materialized after module content change
        materializePolicyFieldLocked(moduleName, m_configData);
        return Result<void, ConfigErrc>::FromValue();
    } catch (const std::exception& e) {
        INNER_CORE_LOG("[ConfigManager] SetModuleConfigJson error: %s (module: %s)\n", e.what(), moduleName.c_str());
        return Result<void, ConfigErrc>::FromError(ConfigErrc::kInternalError);
    }
}

Bool ConfigManager::GetBool(const String& key, Bool defaultValue) const {
    auto value = Get(key);
    return value.has_value() ? value.value().AsBool(defaultValue) : defaultValue;
}

Int64 ConfigManager::GetInt(const String& key, Int64 defaultValue) const {
    auto value = Get(key);
    return value.has_value() ? value.value().AsInt(defaultValue) : defaultValue;
}

Double ConfigManager::GetDouble(const String& key, Double defaultValue) const {
    auto value = Get(key);
    return value.has_value() ? value.value().AsDouble(defaultValue) : defaultValue;
}

String ConfigManager::GetString(const String& key, const String& defaultValue) const {
    auto value = Get(key);
    return value.has_value() ? value.value().AsString(defaultValue) : defaultValue;
}

Result<void, ConfigErrc> ConfigManager::SetBool(const String& key, Bool value) {
    return SetValue(key, ConfigValue(value));
}

Result<void, ConfigErrc> ConfigManager::SetInt(const String& key, Int64 value) {
    return SetValue(key, ConfigValue(value));
}

Result<void, ConfigErrc> ConfigManager::SetDouble(const String& key, Double value) {
    return SetValue(key, ConfigValue(value));
}

Result<void, ConfigErrc> ConfigManager::SetString(const String& key, const String& value) {
    return SetValue(key, ConfigValue(value));
}

UInt32 ConfigManager::RegisterChangeCallback(const String& prefix, ConfigChangeCallback callback) {
    RecursiveLockGuard lock(m_mutex);
    UInt32 id = m_iNextCallbackId++;
    m_mapCallbacks[id] = std::make_pair(prefix, callback);
    return id;
}

void ConfigManager::UnregisterChangeCallback(UInt32 callbackId) {
    RecursiveLockGuard lock(m_mutex);
    m_mapCallbacks.erase(callbackId);
}

String ConfigManager::ToJson(Bool pretty) const {
    RecursiveLockGuard lock(m_mutex);
    if (pretty) {
        return m_configData.dump(4);
    } else {
        return m_configData.dump();
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

ConfigManager::UpdatePolicy ConfigManager::GetModuleUpdatePolicy(const String& moduleName) const {
    RecursiveLockGuard lock(m_mutex);
    auto it = m_mapModulePolicies.find(moduleName);
    if (it != m_mapModulePolicies.end()) return it->second;
    return m_defaultPolicy; // use runtime default instead of static constant
}

Result<void, ConfigErrc> ConfigManager::SetModuleUpdatePolicy(const String& moduleName, UpdatePolicy policy) {
    RecursiveLockGuard lock(m_mutex);
    m_mapModulePolicies[moduleName] = policy;
    m_setExplicitPolicyModules.insert(moduleName);
    // Reflect policy into current config data for persistence / manual editing
    materializePolicyFieldLocked(moduleName, m_configData);
    return Result<void, ConfigErrc>::FromValue();
}

Result<void, ConfigErrc> ConfigManager::SetModuleUpdatePolicy(const String& moduleName, const String& policyStr) {
    auto p = parsePolicyString(policyStr);
    if (!p.has_value()) {
        return Result<void, ConfigErrc>::FromError(ConfigErrc::kValidationError);
    }
    return SetModuleUpdatePolicy(moduleName, p.value());
}

// ============================================================================
// Update Policy: internals
// ============================================================================

void ConfigManager::refreshPoliciesFromConfigLocked() {
    m_mapModulePolicies.clear();
    // First, load top-level mapping if present
    Map<String, UpdatePolicy> topMap;
    m_setExplicitPolicyModules.clear();
    
    // Reset default policy to fallback
    m_defaultPolicy = UpdatePolicy::kOnChangeUpdate;
    
    try {
        if (m_configData.contains(FIELD_UPDATE_POLICY) && m_configData[FIELD_UPDATE_POLICY].is_object()) {
            const json& m = m_configData[FIELD_UPDATE_POLICY];
            
            // Read default policy if present
            if (m.contains(POLICY_DEFAULT_KEY) && m[POLICY_DEFAULT_KEY].is_string()) {
                auto defPol = parsePolicyString(m[POLICY_DEFAULT_KEY].get<String>());
                if (defPol.has_value()) {
                    m_defaultPolicy = defPol.value();
                }
            }
            
            for (auto it = m.begin(); it != m.end(); ++it) {
                if (it.key() == POLICY_DEFAULT_KEY) continue; // skip default key
                if (it.value().is_string()) {
                    auto pp = parsePolicyString(it.value().get<String>());
                    if (pp.has_value()) {
                        topMap[it.key()] = pp.value();
                        m_setExplicitPolicyModules.insert(it.key());
                    }
                }
            }
        }
    } catch (...) {
        // ignore mapping errors
    }

    for (auto it = m_configData.begin(); it != m_configData.end(); ++it) {
        const String moduleName = it.key();
        if (moduleName == FIELD_UPDATE_POLICY || moduleName == FIELD_METADATA) continue;
        UpdatePolicy pol = m_defaultPolicy; // use loaded default
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
        m_mapModulePolicies[moduleName] = pol;
    }
}

UInt32 ConfigManager::computeModuleCrcLocked(const json& moduleJson) const {
    try {
        if (moduleJson.is_object()) {
            json tmp = moduleJson;
            // Strip legacy embedded policy field if present (for backward compatibility)
            if (tmp.contains("__update_policy__")) tmp.erase("__update_policy__");
            String data = tmp.dump();
            return Crypto::Util::computeCrc32(
                reinterpret_cast<const UInt8*>(data.c_str()),
                data.length()
            );
        }
        String data = moduleJson.dump();
        return Crypto::Util::computeCrc32(
            reinterpret_cast<const UInt8*>(data.c_str()),
            data.length()
        );
    } catch (...) {
        return 0u;
    }
}

void ConfigManager::materializePolicyFieldLocked(const String& moduleName, json& rootJson) {
    UpdatePolicy pol = m_defaultPolicy; // use runtime default
    auto itP = m_mapModulePolicies.find(moduleName);
    if (itP != m_mapModulePolicies.end()) pol = itP->second;

    // Only persist explicit policies; default ones are omitted
    if (m_setExplicitPolicyModules.find(moduleName) != m_setExplicitPolicyModules.end()) {
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

Result<void, ConfigErrc> ConfigManager::FromJson(const String& jsonStr) {
    RecursiveLockGuard lock(m_mutex);
    
    try {
        json parsed = json::parse(jsonStr);
        if (!parsed.is_object()) {
            INNER_CORE_LOG("[ConfigManager] FromJson error: Root must be a JSON object\n");
            return Result<void, ConfigErrc>::FromError(ConfigErrc::kValidationError);
        }
        m_configData = parsed;
        return Result<void, ConfigErrc>::FromValue();
    } catch (const json::parse_error& e) {
        INNER_CORE_LOG("[ConfigManager] FromJson parse error\n");
        INNER_CORE_LOG("  Error: %s\n", e.what());
        INNER_CORE_LOG("  Position: byte %zu\n", static_cast<size_t>(e.byte));
        return Result<void, ConfigErrc>::FromError(ConfigErrc::kParseError);
    } catch (const std::exception& e) {
        INNER_CORE_LOG("[ConfigManager] FromJson error: %s\n", e.what());
        return Result<void, ConfigErrc>::FromError(ConfigErrc::kInternalError);
    }
}

void ConfigManager::Clear() {
    RecursiveLockGuard lock(m_mutex);
    m_configData = json::object();
    m_vecBackupStack.clear();
    m_mapCallbacks.clear();
}

// ============================================================================
// Private Helpers
// ============================================================================

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
