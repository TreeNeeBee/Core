/**
 * @file        config_example.cpp
 * @brief       Simple ConfigManager usage example
 * @date        2025-11-04
 * @details     Demonstrates basic configuration management with RAII pattern
 */

#include <iostream>
#include "CConfig.hpp"

using namespace lap::core;

int main() {
    std::cout << "\n=== ConfigManager Basic Usage Example ===\n\n";
    
    // Get ConfigManager singleton instance
    ConfigManager& config = ConfigManager::getInstance();
    
    // ========================================================================
    // 1. Initialize configuration
    // ========================================================================
    std::cout << "1. Initializing configuration...\n";
    auto initResult = config.initialize("example_config.json", true);
    if (!initResult.HasValue()) {
        std::cerr << "   Failed to initialize\n";
        return 1;
    }
    std::cout << "   ✓ Configuration initialized with security enabled\n";
    
    // ========================================================================
    // 2. Set configuration values
    // ========================================================================
    std::cout << "\n2. Setting configuration values...\n";
    
    config.setVersion(1);
    config.setDescription("Example Configuration");
    
    // Basic types
    config.setString("app.name", "MyApplication");
    config.setInt("app.version", 100);
    config.setBool("app.debug_mode", false);
    config.setDouble("app.timeout", 30.5);
    
    // Nested configuration
    config.setString("database.host", "localhost");
    config.setInt("database.port", 5432);
    config.setString("database.name", "mydb");
    
    // Network settings
    config.setInt("network.max_connections", 100);
    config.setBool("network.ssl_enabled", true);
    
    std::cout << "   ✓ Configuration values set\n";
    
    // ========================================================================
    // 3. Read configuration values
    // ========================================================================
    std::cout << "\n3. Reading configuration values...\n";
    
    String appName = config.getString("app.name");
    Int64 appVersion = config.getInt("app.version");
    Bool debugMode = config.getBool("app.debug_mode");
    Double timeout = config.getDouble("app.timeout");
    
    std::cout << "   App Name: " << appName << "\n";
    std::cout << "   App Version: " << appVersion << "\n";
    std::cout << "   Debug Mode: " << (debugMode ? "enabled" : "disabled") << "\n";
    std::cout << "   Timeout: " << timeout << " seconds\n";
    
    // ========================================================================
    // 4. Module configuration (JSON format)
    // ========================================================================
    std::cout << "\n4. Working with module configuration...\n";
    
    nlohmann::json logConfig;
    logConfig["level"] = "info";
    logConfig["output"] = "file";
    logConfig["file_path"] = "/var/log/app.log";
    logConfig["max_size_mb"] = 100;
    
    config.setModuleConfigJson("logging", logConfig);
    std::cout << "   ✓ Logging module configured\n";
    
    // Retrieve module config
    nlohmann::json retrievedLogConfig = config.getModuleConfigJson("logging");
    std::cout << "   Log level: " << retrievedLogConfig["level"] << "\n";
    std::cout << "   Log output: " << retrievedLogConfig["output"] << "\n";
    
    // ========================================================================
    // 5. Configuration existence check
    // ========================================================================
    std::cout << "\n5. Checking configuration keys...\n";
    
    if (config.exists("database.host")) {
        std::cout << "   ✓ database.host exists\n";
    }
    
    if (!config.exists("nonexistent.key")) {
        std::cout << "   ✓ nonexistent.key does not exist\n";
    }
    
    // ========================================================================
    // 6. Update policy for modules
    // ========================================================================
    std::cout << "\n6. Setting update policies...\n";
    
    // Set different update policies for modules
    config.setModuleUpdatePolicy("logging", ConfigManager::UpdatePolicy::kAlwaysUpdate);
    config.setModuleUpdatePolicy("database", ConfigManager::UpdatePolicy::kOnChangeUpdate);
    
    std::cout << "   ✓ Update policies configured\n";
    
    // ========================================================================
    // 7. Export configuration as JSON
    // ========================================================================
    std::cout << "\n7. Exporting configuration...\n";
    
    String jsonOutput = config.toJson(true);  // pretty print
    std::cout << "   Configuration exported (" << jsonOutput.length() << " bytes)\n";
    
    // ========================================================================
    // Summary
    // ========================================================================
    std::cout << "\n=== Configuration Complete ===\n";
    std::cout << "✓ All configuration operations successful\n";
    std::cout << "✓ Configuration will be auto-saved on program exit (RAII)\n";
    std::cout << "✓ Check 'example_config.json' after program exits\n\n";
    
    // Configuration automatically saved by destructor (RAII pattern)
    return 0;
}
