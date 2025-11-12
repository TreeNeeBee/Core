#include "CConfig.hpp"
#include "CInitialization.hpp"
#include <iostream>
#include <nlohmann/json.hpp>

using namespace lap::core;

int main() {
    // AUTOSAR-compliant initialization
    auto initResult = Initialize();
    if (!initResult.HasValue()) {
        std::cerr << "Failed to initialize Core: " << initResult.Error().Message() << "\n";
        return 1;
    }
    
    ConfigManager& cfg = ConfigManager::getInstance();
    auto res = cfg.initialize("policy_demo.json", true);
    if (!res.HasValue()) {
        std::cerr << "init failed\n";
        return 1;
    }

    // Prepare modules
    nlohmann::json modA; modA["value"] = 100;
    nlohmann::json modB; modB["value"] = 200;
    cfg.setModuleConfigJson("modA", modA);
    cfg.setModuleConfigJson("modB", modB);

    // Set policies
    cfg.setModuleUpdatePolicy("modA", "first");
    cfg.setModuleUpdatePolicy("modB", "on_change");

    // Trigger a save (private in library, but examples rely on load/save externally).
    // We'll export JSON via toJson() and print the __update_policy__ using getModuleConfig.
    // In practice, config is auto-saved in destructor; for demo, print runtime JSON.
    std::cout << "Current config (core view):\n";
    std::cout << cfg.toJson(true) << "\n";

    std::cout << "modA policy=" << (int)cfg.getModuleUpdatePolicy("modA")
              << ", modB policy=" << (int)cfg.getModuleUpdatePolicy("modB") << "\n";

    // Change modB to demonstrate on_change
    nlohmann::json modB2 = modB; modB2["value"] = 201;
    cfg.setModuleConfigJson("modB", modB2);

    std::cout << "Updated modB: " << cfg.getModuleConfig("modB", true) << "\n";

    return 0;
}
