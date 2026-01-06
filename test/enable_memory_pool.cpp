#include "CConfig.hpp"
#include <iostream>
#include <nlohmann/json.hpp>

using namespace lap::core;
using json = nlohmann::json;

int main() {
    // Get ConfigManager instance
    ConfigManager& config = ConfigManager::getInstance();
    
    // Create memory configuration with pool_enable=true
    json memoryConfig = {
        {"pool_enable", true},
        {"check_enable", false},
        {"align", 8}
    };
    
    // Set memory configuration
    auto result = config.setModuleConfigJson("memory", memoryConfig);
    if (!result.HasValue()) {
        std::cerr << "Failed to set memory configuration" << std::endl;
        return 1;
    }
    
    std::cout << "Memory configuration set successfully with pool_enable=true" << std::endl;
    
    // ConfigManager will auto-save on destruction
    return 0;
}
