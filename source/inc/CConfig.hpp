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
 *              - Version control and rollback
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
#include <map>
#include <set>
#include <vector>
#include <functional>
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
    ConfigValueType getType() const noexcept { return type_; }
    Bool isNull() const noexcept { return type_ == ConfigValueType::kNull; }
    Bool isBool() const noexcept { return type_ == ConfigValueType::kBoolean; }
    Bool isInt() const noexcept { return type_ == ConfigValueType::kInteger; }
    Bool isDouble() const noexcept { return type_ == ConfigValueType::kDouble; }
    Bool isString() const noexcept { return type_ == ConfigValueType::kString; }
    Bool isArray() const noexcept { return type_ == ConfigValueType::kArray; }
    Bool isObject() const noexcept { return type_ == ConfigValueType::kObject; }

    // Value accessors with default fallback
    Bool asBool(Bool defaultValue = false) const noexcept;
    Int64 asInt(Int64 defaultValue = 0) const noexcept;
    Double asDouble(Double defaultValue = 0.0) const noexcept;
    String asString(const String& defaultValue = "") const noexcept;

    // Array operations
    Size arraySize() const noexcept;
    ConfigValue& operator[](Size index);
    const ConfigValue& operator[](Size index) const;
    void append(const ConfigValue& value);

    // Object operations
    Bool hasKey(const String& key) const noexcept;
    ConfigValue& operator[](const String& key);
    const ConfigValue& operator[](const String& key) const;
    Vector<String> getKeys() const;

    // Serialization
    String toJsonString(Bool pretty = false) const;
    static ConfigValue fromJsonString(const String& json);

private:
    void toJsonString(std::ostream& os, Int32 indent, Bool pretty) const;
    
    ConfigValueType type_;
    
    // Use union to save memory
    union {
        Bool boolValue_;
        Int64 intValue_;
        Double doubleValue_;
    };
    
    String stringValue_;
    Vector<ConfigValue> arrayValue_;
    Map<String, ConfigValue> objectValue_;

    void clear();
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
    UInt32 version;         ///< Configuration version
    String description;     ///< Configuration description
    Bool encrypted;         ///< Base64 encoding flag (true = data hidden)
    String crc;             ///< CRC32 checksum (hex string)
    String timestamp;       ///< Last modification timestamp (ISO format)
    String hmac;            ///< HMAC-SHA256 (hex string)
    
    ConfigMetadata() : version(1), encrypted(false) {}
};

/**
 * @brief Configuration change callback
 */
using ConfigChangeCallback = std::function<void(const String& key, const ConfigValue& oldValue, const ConfigValue& newValue)>;

/**
 * @brief Unified Configuration Manager with Triple Security Verification
 * 
 * @details
 * Provides centralized configuration management with:
 * - Hierarchical key-value storage (nlohmann::json)
 * - JSON persistence with private fields (__crc__, __timestamp__, __hmac__)
 * - Triple security verification: CRC32 → Timestamp → HMAC-SHA256
 * - HMAC key from environment variable HMAC_SECRET
 * - Version control and rollback support
 * - Thread-safe operations
 * - Change notification callbacks
 * 
 * Security Flow:
 * - Save: core_json → compute CRC/timestamp/HMAC → add private fields → save
 * - Load: parse JSON → extract/remove private fields → verify CRC → timestamp → HMAC → load
 * 
 * @usage
 * // Set HMAC_SECRET environment variable first
 * ConfigManager& config = ConfigManager::getInstance();
 * config.initialize("/path/to/config.json", true);  // enable_security=true
 * 
 * // Set values
 * config.setInt("network.port", 8080);
 * config.setBool("network.enabled", true);
 * 
 * // Get values
 * Int64 port = config.getInt("network.port", 8080);
 * Bool enabled = config.getBool("network.enabled", false);
 * 
 * // Save with security verification
 * config.save();
 * 
 * // Backup and rollback
 * config.createBackup();
 * config.rollback();
 */
class ConfigManager {
public:
    /**
     * @brief Module update policy when persisting configuration
     * 
     * Controls whether a module's latest in-memory data should be written to disk.
     * - NoUpdate: Never update this module section on save (except the policy field itself)
     * - FirstUpdate: Write once on the first successful save, then keep previous persisted data
     * - AlwaysUpdate: Always write the latest data on every save
     * - OnChangeUpdate: Write only if the module's data changed since last save
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
    static ConfigManager& getInstance();

    /**
     * @brief Initialize configuration manager
     * @param configPath Path to configuration file
     * @param enableSecurity Enable triple security verification (CRC/Timestamp/HMAC)
     * @return Result indicating success or error
     * @throws ConfigException if HMAC_SECRET not set when loading file with HMAC
     * @threadsafe Not thread-safe - must be called before multi-threaded access
     */
    Result<void, ConfigErrc> initialize(const String& configPath, 
                                        Bool enableSecurity = true);

    /**
     * @brief Enable/Disable Base64 encoding for hiding sensitive data
     * @param enable True to enable Base64 encoding, false to disable
     * @note This updates the metadata.encrypted field
     * @threadsafe Thread-safe - uses internal locking
     */
    void setBase64Encoding(Bool enable);

    /**
     * @brief Get current Base64 encoding status
     * @return True if Base64 encoding is enabled
     * @threadsafe Thread-safe - reads under lock
     */
    Bool isBase64Enabled() const;

    /**
     * @brief Get configuration metadata
     * @return Current configuration metadata
     * @threadsafe Thread-safe - returns copy under lock
     */
    ConfigMetadata getMetadata() const;

    /**
     * @brief Set configuration version
     * @param version Configuration version number
     */
    void setVersion(UInt32 version);

    /**
     * @brief Get configuration version
     * @return Current configuration version
     */
    UInt32 getVersion() const;

    /**
     * @brief Set configuration description
     * @param description Configuration description text
     */
    void setDescription(const String& description);

    /**
     * @brief Get configuration description
     * @return Current configuration description
     */
    String getDescription() const;

    /**
     * @brief Load configuration from file with security verification
     * Flow: parse JSON → extract private fields → dump core → verify CRC → timestamp → HMAC
     * @param skipVerification If true, skip security verification (default: false)
     * @return Result indicating success or error
     * @throws ConfigException on verification failure when skipVerification=false
     */
    Result<void, ConfigErrc> load(Bool skipVerification = false);

    /**
     * @brief Create backup of current configuration
     * @return Result indicating success or error
     */
    Result<void, ConfigErrc> createBackup();

    /**
     * @brief Rollback to previous backup
     * @return Result indicating success or error
     */
    Result<void, ConfigErrc> rollback();

    /**
     * @brief Set configuration value
     * @param key Configuration key (dot notation)
     * @param value Configuration value
     * @return Result indicating success or error
     */
    Result<void, ConfigErrc> set(const String& key, const ConfigValue& value);

    /**
     * @brief Get configuration value
     * @param key Configuration key (dot notation)
     * @return Optional containing value if found
     */
    Optional<ConfigValue> get(const String& key) const;

    /**
     * @brief Remove configuration key
     * @param key Configuration key (dot notation)
     * @return Result indicating success or error
     */
    Result<void, ConfigErrc> remove(const String& key);

    /**
     * @brief Check if key exists
     * @param key Configuration key (dot notation)
     * @return True if key exists
     */
    Bool exists(const String& key) const;

    /**
     * @brief Get all keys with optional prefix filter
     * @param prefix Key prefix filter (empty for all)
     * @return Vector of matching keys
     */
    Vector<String> getKeys(const String& prefix = "") const;

    /**
     * @brief Get module-specific configuration as JSON string
     * @param moduleName Name of the module (e.g., "network", "database")
     * @param pretty Enable pretty printing
     * @return JSON string of module configuration
     */
    String getModuleConfig(const String& moduleName, Bool pretty = true) const;

    /**
     * @brief Get module-specific configuration as nlohmann::json object
     * @param moduleName Name of the module
     * @return nlohmann::json object (empty object if module not found)
     */
    nlohmann::json getModuleConfigJson(const String& moduleName) const;

    /**
     * @brief Set module-specific configuration from JSON string
     * @param moduleName Name of the module
     * @param jsonConfig JSON string containing module configuration
     * @return Result indicating success or error
     */
    Result<void, ConfigErrc> setModuleConfig(const String& moduleName, const String& jsonConfig);

    /**
     * @brief Set module-specific configuration from nlohmann::json object
     * @param moduleName Name of the module
     * @param jsonConfig nlohmann::json object containing module configuration
     * @return Result indicating success or error
     */
    Result<void, ConfigErrc> setModuleConfigJson(const String& moduleName, const nlohmann::json& jsonConfig);

    // ---------------------------------------------------------------------
    // Update Policy APIs
    // ---------------------------------------------------------------------
    /**
     * @brief Get module update policy (defaults to FirstUpdate if not set)
     */
    UpdatePolicy getModuleUpdatePolicy(const String& moduleName) const;

    /**
     * @brief Set module update policy (runtime). Will also reflect into config data for persistence.
     */
    Result<void, ConfigErrc> setModuleUpdatePolicy(const String& moduleName, UpdatePolicy policy);

    /**
     * @brief Set module update policy from string: "none"|"first"|"always"|"on_change"
     */
    Result<void, ConfigErrc> setModuleUpdatePolicy(const String& moduleName, const String& policyStr);

    // Convenience accessors
    Bool getBool(const String& key, Bool defaultValue = false) const;
    Int64 getInt(const String& key, Int64 defaultValue = 0) const;
    Double getDouble(const String& key, Double defaultValue = 0.0) const;
    String getString(const String& key, const String& defaultValue = "") const;

    // Convenience setters
    Result<void, ConfigErrc> setBool(const String& key, Bool value);
    Result<void, ConfigErrc> setInt(const String& key, Int64 value);
    Result<void, ConfigErrc> setDouble(const String& key, Double value);
    Result<void, ConfigErrc> setString(const String& key, const String& value);

    /**
     * @brief Register change callback
     * @param key Configuration key to monitor (empty for all)
     * @param callback Callback function
     * @return Callback ID for unregistration
     */
    UInt32 registerChangeCallback(const String& key, ConfigChangeCallback callback);

    /**
     * @brief Unregister change callback
     * @param callbackId Callback ID from registration
     */
    void unregisterChangeCallback(UInt32 callbackId);

    /**
     * @brief Get configuration metadata
     * @return Configuration metadata
     */
    /**
     * @brief Get configuration as JSON string (core only, no private fields)
     * @param pretty Enable pretty printing
     * @return JSON string
     */
    String toJson(Bool pretty = true) const;

    /**
     * @brief Import configuration from JSON string
     * @param json JSON string
     * @return Result indicating success or error
     */
    Result<void, ConfigErrc> fromJson(const String& json);

    /**
     * @brief Clear all configuration data
     */
    void clear();

private:
    ConfigManager();
    ~ConfigManager();
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;

    /**
     * @brief Save configuration to file with security fields (RAII - called in destructor)
     * Flow: dump core → compute CRC/timestamp/HMAC → add private fields → save
     * @param enableSecurity If false, skip security field generation (for initial creation)
     * @return Result indicating success or error
     * @note This method is private and automatically called in destructor
     */
    Result<void, ConfigErrc> save(Bool enableSecurity = true);

    // Internal storage (using nlohmann::json)
    nlohmann::json configData_;          // JSON object (core data)
    ConfigMetadata metadata_;            // Metadata (version, description, security)
    Vector<nlohmann::json> backupStack_; // JSON backup stack
    
    // Configuration
    String configPath_;
    Bool enableSecurity_;
    Crypto crypto_;             // Cryptographic utilities (HMAC key managed internally)
    Bool initialized_;

    // Change tracking
    Map<UInt32, std::pair<String, ConfigChangeCallback>> callbacks_;
    UInt32 nextCallbackId_;

    // Thread safety
    mutable RecursiveMutex mutex_;

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
    Map<String, UpdatePolicy> modulePolicies_;
    // Default policy for new/unspecified modules (loaded from __update_policy__.default)
    UpdatePolicy defaultPolicy_;
    // Track modules that have already been saved once (for FirstUpdate)
    Set<String> moduleSavedOnce_;
    // Track last-saved CRC per module (for OnChangeUpdate)
    Map<String, UInt32> moduleLastCrc_;
    // Last fully persisted core json (without __metadata__)
    nlohmann::json lastPersistedData_;
    // Modules with explicitly set policies (persisted under top-level __update_policy__)
    Set<String> explicitPolicyModules_;

    // Helpers (assume mutex_ is held)
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
