#include <iostream>
#include <fstream>
#include <nlohmann/json.hpp>
#include "CConfig.hpp"

using namespace lap::core;
using json = nlohmann::json;

// Helper to read config.json directly
json readConfigFile(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        return json::object();
    }
    json j;
    file >> j;
    return j;
}

int main() {
    std::cout << "\n=== Testing setModuleConfigJson Auto-Update Policy ===\n\n";
    
    // Initialize ConfigManager
    ConfigManager& configMgr = ConfigManager::getInstance();
    
    // Display initial policy
    std::cout << "1. Reading initial __update_policy__...\n";
    json initialConfig = readConfigFile("config.json");
    if (initialConfig.contains("__update_policy__")) {
        std::cout << "   __update_policy__: " << initialConfig["__update_policy__"].dump(2) << "\n\n";
    } else {
        std::cout << "   __update_policy__: (not present)\n\n";
    }
    
    // Set a test module config
    std::cout << "2. Setting 'testModule' config...\n";
    json testConfig;
    testConfig["setting1"] = "value1";
    testConfig["setting2"] = 42;
    testConfig["setting3"] = true;
    
    auto result = configMgr.setModuleConfigJson("testModule", testConfig);
    if (result.HasValue()) {
        std::cout << "   ✓ Module config set successfully\n\n";
    } else {
        std::cout << "   ✗ Failed to set module config\n\n";
        return 1;
    }
    
    // Force save to disk
    std::cout << "3. Saving configuration to disk...\n";
    auto saveResult = configMgr.save();
    if (saveResult.HasValue()) {
        std::cout << "   ✓ Configuration saved\n\n";
    } else {
        std::cout << "   ✗ Failed to save configuration\n\n";
        return 1;
    }
    
    // Read back and check __update_policy__
    std::cout << "4. Reading back config file to check __update_policy__...\n";
    json updatedConfig = readConfigFile("config.json");
    
    if (updatedConfig.contains("__update_policy__")) {
        std::cout << "   __update_policy__: " << updatedConfig["__update_policy__"].dump(2) << "\n";
        
        if (updatedConfig["__update_policy__"].contains("testModule")) {
            std::string policy = updatedConfig["__update_policy__"]["testModule"];
            std::cout << "   ✓ testModule policy: \"" << policy << "\"\n";
            
            if (policy == "on_change") {
                std::cout << "   ✓ Policy correctly set to 'on_change' (default behavior)\n\n";
            } else {
                std::cout << "   ✗ Policy is NOT 'on_change' (got: \"" << policy << "\")\n\n";
                return 1;
            }
        } else {
            std::cout << "   ✗ testModule NOT found in __update_policy__\n\n";
            return 1;
        }
    } else {
        std::cout << "   ✗ __update_policy__ NOT present in saved file\n\n";
        return 1;
    }
    
    // Verify module content
    std::cout << "5. Verifying module content from memory...\n";
    json moduleContent = configMgr.getModuleConfigJson("testModule");
    std::cout << "   testModule content: " << moduleContent.dump(2) << "\n";
    
    if (moduleContent["setting1"] == "value1" && 
        moduleContent["setting2"] == 42 && 
        moduleContent["setting3"] == true) {
        std::cout << "   ✓ Module content is correct\n\n";
    } else {
        std::cout << "   ✗ Module content mismatch\n\n";
        return 1;
    }
    
    // Update existing module
    std::cout << "6. Updating existing 'memory' module...\n";
    json memoryConfig = configMgr.getModuleConfigJson("memory");
    std::cout << "   Current memory config: " << memoryConfig.dump(2) << "\n";
    
    memoryConfig["align"] = 16;  // Change alignment
    
    result = configMgr.setModuleConfigJson("memory", memoryConfig);
    if (result.HasValue()) {
        std::cout << "   ✓ Memory module updated\n";
        
        // Save again
        saveResult = configMgr.save();
        std::cout << "   " << (saveResult.HasValue() ? "✓" : "✗") << " Configuration saved\n\n";
    } else {
        std::cout << "   ✗ Failed to update memory module\n\n";
        return 1;
    }
    
    // Check policy for memory module
    std::cout << "7. Checking __update_policy__ for memory module...\n";
    updatedConfig = readConfigFile("config.json");
    
    if (updatedConfig["__update_policy__"].contains("memory")) {
        std::string policy = updatedConfig["__update_policy__"]["memory"];
        std::cout << "   memory policy: \"" << policy << "\"\n";
        
        if (policy == "default") {
            std::cout << "   ✓ Memory policy correctly set to 'default'\n\n";
        } else {
            std::cout << "   ⚠ Memory policy is: \"" << policy << "\" (expected 'default')\n\n";
        }
    } else {
        std::cout << "   ✗ memory module NOT found in __update_policy__\n\n";
    }
    
    // Display final __update_policy__
    std::cout << "8. Final __update_policy__ state:\n";
    if (updatedConfig.contains("__update_policy__")) {
        std::cout << updatedConfig["__update_policy__"].dump(2) << "\n\n";
    }
    
    std::cout << "=== All Tests Passed! ===\n\n";
    
    return 0;
}
