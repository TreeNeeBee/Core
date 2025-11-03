/**
 * @file        config_example_v4.cpp
 * @brief       Configuration Manager v4.0 - Complete Feature Demonstration
 * @date        2025-10-31
 * @details     Demonstrates:
 *              - __metadata__ structure (version, description, encrypted, crc, timestamp, hmac)
 *              - Direct nlohmann::json object operations
 *              - Base64 encoding controlled by metadata.encrypted
 *              - Module-level configuration with both string and json interfaces
 */

#include "CConfig.hpp"
#include <iostream>
#include <cstdlib>
#include <nlohmann/json.hpp>

using namespace lap::core;
using json = nlohmann::json;

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << " Configuration Manager v4.0" << std::endl;
    std::cout << " __metadata__ Structure & JSON API" << std::endl;
    std::cout << "========================================\n" << std::endl;
    
    // Set HMAC secret
    setenv("HMAC_SECRET", "production-secret-key-2025", 1);
    
    ConfigManager& config = ConfigManager::getInstance();
    
    // ========================================================================
    // Part 1: Initialize and Set Metadata
    // ========================================================================
    std::cout << "=== Part 1: Metadata Management ===" << std::endl;
    
    std::cout << "\n1. Initializing configuration..." << std::endl;
    auto initResult = config.initialize("config_v4.json", true);
    if (!initResult.HasValue()) {
        std::cerr << "Failed to initialize" << std::endl;
        return 1;
    }
    
    std::cout << "\n2. Setting metadata..." << std::endl;
    // Note: Cannot directly set crc/hmac/timestamp (security fields are auto-generated)
    // We can only set version, description, and encrypted flag
    config.setVersion(2);
    config.setDescription("Production Configuration v2.0");
    
    std::cout << "   Version: " << config.getVersion() << std::endl;
    std::cout << "   Description: " << config.getDescription() << std::endl;
    std::cout << "   Encrypted: " << (config.isBase64Enabled() ? "Yes" : "No") << std::endl;
    
    // ========================================================================
    // Part 2: Module Configuration with JSON Objects
    // ========================================================================
    std::cout << "\n\n=== Part 2: Direct JSON Object Operations ===" << std::endl;
    
    std::cout << "\n3. Creating network config with nlohmann::json..." << std::endl;
    json networkConfig = {
        {"interface", "eth0"},
        {"port", 8080},
        {"ssl_enabled", true},
        {"timeout_ms", 5000},
        {"max_connections", 1000},
        {"buffer_size_kb", 64}
    };
    auto setResult = config.setModuleConfigJson("network", networkConfig);
    if (setResult.HasValue()) {
        std::cout << "   ✓ Network module configured with JSON object" << std::endl;
    }
    
    std::cout << "\n4. Creating database config with nlohmann::json..." << std::endl;
    json dbConfig = {
        {"host", "192.168.1.100"},
        {"port", 5432},
        {"database", "production_db"},
        {"username", "admin"},
        {"password", "SuperSecretPassword!2025"},
        {"ssl_mode", "require"},
        {"pool_size", 20},
        {"connection_timeout", 30}
    };
    config.setModuleConfigJson("database", dbConfig);
    std::cout << "   ✓ Database module configured" << std::endl;
    
    std::cout << "\n5. Creating logging config with nested structure..." << std::endl;
    json logConfig = {
        {"level", "DEBUG"},
        {"file", "/var/log/app.log"},
        {"rotate", true},
        {"max_size_mb", 100},
        {"sinks", {
            {{"type", "console"}, {"enabled", true}},
            {{"type", "file"}, {"enabled", true}, {"path", "/var/log/app.log"}},
            {{"type", "syslog"}, {"enabled", false}}
        }}
    };
    config.setModuleConfigJson("logging", logConfig);
    std::cout << "   ✓ Logging module configured with nested arrays" << std::endl;
    
    // ========================================================================
    // Part 3: Retrieve Module Config as JSON Object
    // ========================================================================
    std::cout << "\n\n=== Part 3: Retrieving JSON Objects ===" << std::endl;
    
    std::cout << "\n6. Getting network config as JSON object..." << std::endl;
    json retrievedNetwork = config.getModuleConfigJson("network");
    std::cout << "   Port: " << retrievedNetwork["port"] << std::endl;
    std::cout << "   Max connections: " << retrievedNetwork["max_connections"] << std::endl;
    std::cout << "   SSL enabled: " << (retrievedNetwork["ssl_enabled"].get<bool>() ? "Yes" : "No") << std::endl;
    
    std::cout << "\n7. Getting database config as JSON object..." << std::endl;
    json retrievedDb = config.getModuleConfigJson("database");
    std::cout << "   Host: " << retrievedDb["host"] << std::endl;
    std::cout << "   Database: " << retrievedDb["database"] << std::endl;
    std::cout << "   Pool size: " << retrievedDb["pool_size"] << std::endl;
    
    std::cout << "\n8. Iterating through logging sinks..." << std::endl;
    json retrievedLog = config.getModuleConfigJson("logging");
    if (retrievedLog.contains("sinks") && retrievedLog["sinks"].is_array()) {
        std::cout << "   Found " << retrievedLog["sinks"].size() << " sinks:" << std::endl;
        for (const auto& sink : retrievedLog["sinks"]) {
            std::cout << "   - " << sink["type"] << ": " 
                     << (sink["enabled"].get<bool>() ? "enabled" : "disabled") << std::endl;
        }
    }
    
    // ========================================================================
    // Part 4: Modify JSON Object and Update
    // ========================================================================
    std::cout << "\n\n=== Part 4: Modifying JSON Objects ===" << std::endl;
    
    std::cout << "\n9. Modifying network configuration..." << std::endl;
    json modifiedNetwork = config.getModuleConfigJson("network");
    modifiedNetwork["port"] = 9090;
    modifiedNetwork["max_connections"] = 2000;
    modifiedNetwork["new_feature_enabled"] = true;
    config.setModuleConfigJson("network", modifiedNetwork);
    std::cout << "   ✓ Network port changed to: " << modifiedNetwork["port"] << std::endl;
    std::cout << "   ✓ Max connections increased to: " << modifiedNetwork["max_connections"] << std::endl;
    std::cout << "   ✓ New feature added" << std::endl;
    
    // ========================================================================
    // Part 5: Save Without Base64 (readable)
    // ========================================================================
    std::cout << "\n\n=== Part 5: Save with __metadata__ ===" << std::endl;
    
    std::cout << "\n10. Saving configuration (readable format)..." << std::endl;
    config.save(true);
    std::cout << "   ✓ Saved to config_v4.json" << std::endl;
    std::cout << "   ✓ __metadata__ contains:" << std::endl;
    std::cout << "      - version: 2" << std::endl;
    std::cout << "      - description: Production Configuration v2.0" << std::endl;
    std::cout << "      - encrypted: false" << std::endl;
    std::cout << "      - crc: (computed)" << std::endl;
    std::cout << "      - timestamp: (current time)" << std::endl;
    std::cout << "      - hmac: (computed)" << std::endl;
    
    // ========================================================================
    // Part 6: Enable Base64 Encoding
    // ========================================================================
    std::cout << "\n\n=== Part 6: Base64 Encoding ===" << std::endl;
    
    std::cout << "\n11. Enabling Base64 encoding..." << std::endl;
    config.setBase64Encoding(true);
    std::cout << "   ✓ Base64 encoding enabled" << std::endl;
    std::cout << "   ✓ metadata.encrypted = " << (config.isBase64Enabled() ? "true" : "false") << std::endl;
    
    std::cout << "\n12. Saving with Base64..." << std::endl;
    config.save(true);
    std::cout << "   ✓ File now Base64 encoded - data is hidden!" << std::endl;
    
    std::cout << "\n13. Reloading from Base64 file..." << std::endl;
    config.clear();
    config.setBase64Encoding(true);
    config.initialize("config_v4.json", true);
    auto loadResult = config.load();
    if (loadResult.HasValue()) {
        std::cout << "   ✓ Successfully loaded and decoded" << std::endl;
        std::cout << "   ✓ Triple security verified" << std::endl;
        
        // Verify data integrity
        json verifyNetwork = config.getModuleConfigJson("network");
        std::cout << "   ✓ Network port verified: " << verifyNetwork["port"] << std::endl;
    }
    
    // ========================================================================
    // Part 7: Metadata Retrieval
    // ========================================================================
    std::cout << "\n\n=== Part 7: Metadata Retrieval ===" << std::endl;
    
    std::cout << "\n14. Getting current metadata..." << std::endl;
    ConfigMetadata currentMeta = config.getMetadata();
    std::cout << "   Version: " << currentMeta.version << std::endl;
    std::cout << "   Description: " << currentMeta.description << std::endl;
    std::cout << "   Encrypted: " << (currentMeta.encrypted ? "Yes" : "No") << std::endl;
    std::cout << "   CRC: " << currentMeta.crc << std::endl;
    std::cout << "   Timestamp: " << currentMeta.timestamp << std::endl;
    std::cout << "   HMAC: " << currentMeta.hmac.substr(0, 16) << "..." << std::endl;
    
    // ========================================================================
    // Part 8: Save Readable Version for Inspection
    // ========================================================================
    std::cout << "\n\n=== Part 8: Save Readable Version ===" << std::endl;
    
    std::cout << "\n15. Disabling Base64 for readable output..." << std::endl;
    config.setBase64Encoding(false);
    config.save(true);
    std::cout << "   ✓ Saved to config_v4.json (readable format)" << std::endl;
    std::cout << "   ✓ Open the file to see __metadata__ structure!" << std::endl;
    
    // ========================================================================
    // Summary
    // ========================================================================
    std::cout << "\n\n========================================" << std::endl;
    std::cout << " Configuration Manager v4.0 Summary" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "✓ __metadata__ Structure:" << std::endl;
    std::cout << "  - version, description, encrypted" << std::endl;
    std::cout << "  - crc, timestamp, hmac (security)" << std::endl;
    std::cout << "\n✓ Direct JSON Object API:" << std::endl;
    std::cout << "  - getModuleConfigJson(name) → nlohmann::json" << std::endl;
    std::cout << "  - setModuleConfigJson(name, json)" << std::endl;
    std::cout << "\n✓ Base64 Encoding:" << std::endl;
    std::cout << "  - Controlled by metadata.encrypted" << std::endl;
    std::cout << "  - setBase64Encoding(bool) updates metadata" << std::endl;
    std::cout << "\n✓ Backward Compatible:" << std::endl;
    std::cout << "  - Loads legacy __crc__, __timestamp__, __hmac__" << std::endl;
    std::cout << "  - String-based getModuleConfig() still works" << std::endl;
    std::cout << "\nCheck config_v4.json to see the __metadata__ structure!" << std::endl;
    
    return 0;
}
