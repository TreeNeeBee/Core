/**
 * @file        CConfig.hpp
 * @author      LightAP Team
 * @brief       Unified Configuration Management System with Security
 * @date        2025-10-31
 * @details     Provides centralized configuration management with:
 *              - JSON-based storage (nlohmann::json)
 *              - Triple security verification (CRC32 → Timestamp → HMAC-SHA256)
 *              - Private fields: __crc__, __timestamp__, __hmac__
 *              - HMAC key from environment variable HMAC_SECRET
 *              - Version control and Rollback
 *              - Thread-safe operations
 * @copyright   Copyright (c) 2025
 * @version     2.0
 */

#ifndef LAP_CORE_CCONFIG_HPP
#define LAP_CORE_CCONFIG_HPP

#include "CTypedef.hpp"
#include "CString.hpp"
#include "CResult.hpp"
#include "COptional.hpp"
#include "CCrypto.hpp"

#include "CSync.hpp"
#include "CFunction.hpp"
#include <map>
#include <set>
#include <vector>
#include <stdexcept>
#include <nlohmann/json.hpp>

namespace lap {
namespace core {

/**
 * @brief Configuration exception
 */
class ConfigException : public std::runtime_error {
public:
    explicit ConfigException(const String& msg) : std::runtime_error(msg) {}
};

/**
 * @brief Configuration error codes
 */
enum class ConfigErrc : Int32 {
    kSuccess = 0,
    kFileNotFound = 1,
    kParseError = 2,
    kValidationError = 3,
    kCrcMismatch = 4,
    kTimestampInvalid = 5,
    kHmacMismatch = 6,
    kHmacKeyMissing = 7,
    kNoBackupAvailable = 8,
    kInvalidKey = 9,
    kInternalError = 10
};

/**
 * @brief Configuration value type
 */
enum class ConfigValueType : UInt8 {
    kNull = 0,
    kBoolean,
    kInteger,
    kDouble,
    kString,
    kArray,
    kObject
};

/**
 * @brief Configuration value wrapper
 */
class ConfigValue {
public:
    ConfigValue();
    ~ConfigValue() = default;

    // Type constructors
    explicit ConfigValue(Bool value);
    explicit ConfigValue(Int64 value);
    explicit ConfigValue(Double value);
    explicit ConfigValue(const String& value);
    explicit ConfigValue(const Char* value);

    // Type queries
    ConfigValueType GetType() const noexcept { return m_type; }
    Bool IsNull() const noexcept { return m_type == ConfigValueType::kNull; }
    Bool IsBool() const noexcept { return m_type == ConfigValueType::kBoolean; }
    Bool IsInt() const noexcept { return m_type == ConfigValueType::kInteger; }
    Bool IsDouble() const noexcept { return m_type == ConfigValueType::kDouble; }
    Bool IsString() const noexcept { return m_type == ConfigValueType::kString; }
    Bool IsArray() const noexcept { return m_type == ConfigValueType::kArray; }
    Bool IsObject() const noexcept { return m_type == ConfigValueType::kObject; }

    // Value accessors with default fallback
    Bool AsBool(Bool defaultValue = false) const noexcept;
    Int64 AsInt(Int64 defaultValue = 0) const noexcept;
    Double AsDouble(Double defaultValue = 0.0) const noexcept;
    String AsString(const String& defaultValue = "") const noexcept;

    // Array operations
    Size ArraySize() const noexcept;
    ConfigValue& operator[](Size index);
    const ConfigValue& operator[](Size index) const;
    void Append(const ConfigValue& value);

    // Object operations
    Bool HasKey(const String& key) const noexcept;
    ConfigValue& operator[](const String& key);
    const ConfigValue& operator[](const String& key) const;
    Vector<String> GetKeys() const;

    // Serialization
    String ToJsonString(Bool pretty = false) const;
    static ConfigValue FromJsonString(const String& json);

private:
    void ToJsonString(std::ostream& os, Int32 indent, Bool pretty) const;
    
    ConfigValueType m_type;
    
    // Use union to Save memory
    union {
        Bool m_bValue;
        Int64 m_iValue;
        Double m_dValue;
    };
    
    String m_strValue;
    Vector<ConfigValue> m_vecArrayValue;
    Map<String, ConfigValue> m_mapObjectValue;

    void Clear();
};

/**
 * @brief Configuration metadata (stored as __metadata__ in JSON)
 * @details Contains configuration metadata and security verification fields:
 *          - version: Configuration version number
 *          - description: Configuration description text
 *          - encrypted: Base64 encoding flag (true = data hidden)
 *          - crc: CRC32 checksum (hex string)
 *          - timestamp: Last modification time (ISO format)
 *          - hmac: HMAC-SHA256 authentication code (hex string)
 */
struct ConfigMetadata {
    UInt32 m_iVersion;         ///< Configuration version
    String m_strDescription;     ///< Configuration description
    Bool m_bEncrypted;         ///< Base64 encoding flag (true = data hidden)
    String m_strCrc;             ///< CRC32 checksum (hex string)
    String m_strTimestamp;       ///< Last modification timestamp (ISO format)
    String m_strHmac;            ///< HMAC-SHA256 (hex string)
    
    ConfigMetadata() : m_iVersion(1), m_bEncrypted(false) {}
};

/**
 * @brief Configuration change callback
 */
using ConfigChangeCallback = Function<void(const String& key, const ConfigValue& oldValue, const ConfigValue& newValue)>;

/**
 * @brief Unified Configuration Manager with Triple Security Verification
 * 
 * @details
 * Provides centralized configuration management with:
 * - Hierarchical key-value storage (nlohmann::json)
 * - JSON persistence with private fields (__crc__, __timestamp__, __hmac__)
 * - Triple security verification: CRC32 → Timestamp → HMAC-SHA256
 * - HMAC key from environment variable HMAC_SECRET
 * - Version control and Rollback support
 * - Thread-safe operations
 * - Change notification callbacks
 * 
 * Security Flow:
 * - Save: core_json → compute CRC/timestamp/HMAC → add private fields → Save
 * - Load: parse JSON → extract/Remove private fields → verify CRC → timestamp → HMAC → Load
 * 
 * @usage
 * // Set HMAC_SECRET environment variable first
 * ConfigManager& config = ConfigManager::GetInstance();
 * config.Initialize("/path/to/config.json", true);  // enable_security=true
 * 
 * // Set values
 * config.SetInt("network.port", 8080);
 * config.SetBool("network.enabled", true);
 * 
 * // Get values
 * Int64 port = config.GetInt("network.port", 8080);
 * Bool enabled = config.GetBool("network.enabled", false);
 * 
 * // Save with security verification
 * config.Save();
 * 
 * // Backup and Rollback
 * config.CreateBackup();
 * config.Rollback();
 */
class ConfigManager {
public:
    /**
     * @brief Module update policy when persisting configuration
     * 
     * Controls whether a module's latest in-memory data should be written to disk.
     * - NoUpdate: Never update this module section on Save (except the policy field itself)
     * - FirstUpdate: Write once on the first successful Save, then keep previous persisted data
     * - AlwaysUpdate: Always write the latest data on every Save
     * - OnChangeUpdate: Write only if the module's data changed since last Save
     */
    enum class UpdatePolicy : UInt8 {
        kNoUpdate = 0,
        kFirstUpdate,
        kAlwaysUpdate,
        kOnChangeUpdate
    };

    // Default policy applied to modules without explicit policy (fallback: update on change)
    static constexpr UpdatePolicy kDefaultUpdatePolicy = UpdatePolicy::kOnChangeUpdate;

    /**
     * @brief Get singleton instance
     * @return Reference to the global ConfigManager instance
     * @threadsafe Thread-safe - uses static local variable initialization
     */
    static ConfigManager& GetInstance();

    /**
     * @brief Initialize configuration manager
     * @param configPath Path to configuration file
     * @param enableSecurity Enable triple security verification (CRC/Timestamp/HMAC)
     * @return Result indicating success or error
     * @throws ConfigException if HMAC_SECRET not set when loading file with HMAC
     * @threadsafe Not thread-safe - must be called before multi-threaded access
     */
    Result<void, ConfigErrc> Initialize(const String& configPath, 
                                        Bool enableSecurity = true);

    /**
     * @brief Enable/Disable Base64 encoding for hiding sensitive data
     * @param enable True to enable Base64 encoding, false to disable
     * @note This updates the metadata.encrypted field
     * @threadsafe Thread-safe - uses internal locking
     */
    void SetBase64Encoding(Bool enable);

    /**
     * @brief Get current Base64 encoding status
     * @return True if Base64 encoding is enabled
     * @threadsafe Thread-safe - reads under lock
     */
    Bool IsBase64Enabled() const;

    /**
     * @brief Get configuration metadata
     * @return Current configuration metadata
     * @threadsafe Thread-safe - returns copy under lock
     */
    ConfigMetadata GetMetadata() const;

    /**
     * @brief Set configuration version
     * @param version Configuration version number
     */
    void SetVersion(UInt32 version);

    /**
     * @brief Get configuration version
     * @return Current configuration version
     */
    UInt32 GetVersion() const;

    /**
     * @brief Set configuration description
     * @param description Configuration description text
     */
    void SetDescription(const String& description);

    /**
     * @brief Get configuration description
     * @return Current configuration description
     */
    String GetDescription() const;

    /**
     * @brief Load configuration from file with security verification
     * Flow: parse JSON → extract private fields → dump core → verify CRC → timestamp → HMAC
     * @param skipVerification If true, skip security verification (default: false)
     * @return Result indicating success or error
     * @throws ConfigException on verification failure when skipVerification=false
     */
    Result<void, ConfigErrc> Load(Bool skipVerification = false);

    /**
     * @brief Create backup of current configuration
     * @return Result indicating success or error
     */
    Result<void, ConfigErrc> CreateBackup();

    /**
     * @brief Rollback to previous backup
     * @return Result indicating success or error
     */
    Result<void, ConfigErrc> Rollback();

    /**
     * @brief Set configuration value
     * @param key Configuration key (dot notation)
     * @param value Configuration value
     * @return Result indicating success or error
     */
    Result<void, ConfigErrc> SetValue(const String& key, const ConfigValue& value);

    /**
     * @brief Get configuration value
     * @param key Configuration key (dot notation)
     * @return Optional containing value if found
     */
    Optional<ConfigValue> Get(const String& key) const;

    /**
     * @brief Remove configuration key
     * @param key Configuration key (dot notation)
     * @return Result indicating success or error
     */
    Result<void, ConfigErrc> Remove(const String& key);

    /**
     * @brief Check if key Exists
     * @param key Configuration key (dot notation)
     * @return True if key Exists
     */
    Bool Exists(const String& key) const;

    /**
     * @brief Get all keys with optional prefix filter
     * @param prefix Key prefix filter (empty for all)
     * @return Vector of matching keys
     */
    Vector<String> GetKeys(const String& prefix = "") const;

    /**
     * @brief Get module-specific configuration as JSON string
     * @param moduleName Name of the module (e.g., "network", "database")
     * @param pretty Enable pretty printing
     * @return JSON string of module configuration
     */
    String GetModuleConfig(const String& moduleName, Bool pretty = true) const;

    /**
     * @brief Get module-specific configuration as nlohmann::json object
     * @param moduleName Name of the module
     * @return nlohmann::json object (empty object if module not found)
     */
    nlohmann::json GetModuleConfigJson(const String& moduleName) const;

    /**
     * @brief Set module-specific configuration from JSON string
     * @param moduleName Name of the module
     * @param jsonConfig JSON string containing module configuration
     * @return Result indicating success or error
     */
    Result<void, ConfigErrc> SetModuleConfig(const String& moduleName, const String& jsonConfig);

    /**
     * @brief Set module-specific configuration from nlohmann::json object
     * @param moduleName Name of the module
     * @param jsonConfig nlohmann::json object containing module configuration
     * @return Result indicating success or error
     */
    Result<void, ConfigErrc> SetModuleConfigJson(const String& moduleName, const nlohmann::json& jsonConfig);

    // ---------------------------------------------------------------------
    // Update Policy APIs
    // ---------------------------------------------------------------------
    /**
     * @brief Get module update policy (defaults to FirstUpdate if not set)
     */
    UpdatePolicy GetModuleUpdatePolicy(const String& moduleName) const;

    /**
     * @brief Set module update policy (runtime). Will also reflect into config data for persistence.
     */
    Result<void, ConfigErrc> SetModuleUpdatePolicy(const String& moduleName, UpdatePolicy policy);

    /**
     * @brief Set module update policy from string: "none"|"first"|"always"|"on_change"
     */
    Result<void, ConfigErrc> SetModuleUpdatePolicy(const String& moduleName, const String& policyStr);

    // Convenience accessors
    Bool GetBool(const String& key, Bool defaultValue = false) const;
    Int64 GetInt(const String& key, Int64 defaultValue = 0) const;
    Double GetDouble(const String& key, Double defaultValue = 0.0) const;
    String GetString(const String& key, const String& defaultValue = "") const;

    // Convenience setters
    Result<void, ConfigErrc> SetBool(const String& key, Bool value);
    Result<void, ConfigErrc> SetInt(const String& key, Int64 value);
    Result<void, ConfigErrc> SetDouble(const String& key, Double value);
    Result<void, ConfigErrc> SetString(const String& key, const String& value);

    /**
     * @brief Register change callback
     * @param key Configuration key to monitor (empty for all)
     * @param callback Callback function
     * @return Callback ID for unregistration
     */
    UInt32 RegisterChangeCallback(const String& key, ConfigChangeCallback callback);

    /**
     * @brief Unregister change callback
     * @param callbackId Callback ID from registration
     */
    void UnregisterChangeCallback(UInt32 callbackId);

    /**
     * @brief Get configuration metadata
     * @return Configuration metadata
     */
    /**
     * @brief Get configuration as JSON string (core only, no private fields)
     * @param pretty Enable pretty printing
     * @return JSON string
     */
    String ToJson(Bool pretty = true) const;

    /**
     * @brief Import configuration from JSON string
     * @param json JSON string
     * @return Result indicating success or error
     */
    Result<void, ConfigErrc> FromJson(const String& json);

    /**
     * @brief Clear all configuration data
     */
    void Clear();

private:
    ConfigManager();
    ~ConfigManager();
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;

    /**
     * @brief Save configuration to file with security fields (RAII - called in destructor)
     * Flow: dump core → compute CRC/timestamp/HMAC → add private fields → Save
     * @param enableSecurity If false, skip security field generation (for initial creation)
     * @return Result indicating success or error
     * @note This method is private and automatically called in destructor
     */
    Result<void, ConfigErrc> Save(Bool enableSecurity = true);

    // Internal storage (using nlohmann::json)
    nlohmann::json m_configData;          // JSON object (core data)
    ConfigMetadata m_metadata;            // Metadata (version, description, security)
    Vector<nlohmann::json> m_vecBackupStack; // JSON backup stack
    
    // Configuration
    String m_strConfigPath;
    Bool m_bEnableSecurity;
    Crypto m_crypto;             // Cryptographic utilities (HMAC key managed internally)
    Bool m_bInitialized;

    // Change tracking
    Map<UInt32, std::pair<String, ConfigChangeCallback>> m_mapCallbacks;
    UInt32 m_iNextCallbackId;

    // Thread safety
    mutable RecursiveMutex m_mutex;

    // Security helpers
    String getCurrentTimestamp() const;
    Bool validateTimestamp(const String& timestamp) const;
    
    // File I/O
    Result<String, ConfigErrc> readFile(const String& path);
    Result<void, ConfigErrc> writeFile(const String& path, const String& data);

    // ---------------------------------------------------------------------
    // Update Policy internals
    // ---------------------------------------------------------------------
    // Per-module update policies
    Map<String, UpdatePolicy> m_mapModulePolicies;
    // Default policy for new/unspecified modules (loaded from __update_policy__.default)
    UpdatePolicy m_defaultPolicy;
    // Track modules that have already been saved once (for FirstUpdate)
    Set<String> m_setModuleSavedOnce;
    // Track last-saved CRC per module (for OnChangeUpdate)
    Map<String, UInt32> m_mapModuleLastCrc;
    // Last fully persisted core json (without __metadata__)
    nlohmann::json m_lastPersistedData;
    // Modules with explicitly Set policies (persisted under top-level __update_policy__)
    Set<String> m_setExplicitPolicyModules;

    // Helpers (assume m_mutex is held)
    void refreshPoliciesFromConfigLocked();
    static const Char* policyToString(UpdatePolicy p);
    static Optional<UpdatePolicy> parsePolicyString(const String& s);
    // Compute CRC for a module config excluding private policy field
    UInt32 computeModuleCrcLocked(const nlohmann::json& moduleJson) const;
    // Ensure policy is materialized in config (either as field inside object or via top-level mapping)
    void materializePolicyFieldLocked(const String& moduleName, nlohmann::json& rootJson);
};

} // namespace core
} // namespace lap

#endif // LAP_CORE_CCONFIG_HPP
