#include "CConfig.hpp"
#include <iostream>
#include <cstdlib>

using namespace lap::core;

int main() {
    std::cout << "=== Test Default Policy Loading ===" << std::endl;
    setenv("HMAC_SECRET", "policy-secret", 1);

    auto& cfg = ConfigManager::getInstance();
    cfg.initialize("test_default_policy.json", false); // skip security for this test
    cfg.load(true);

    auto policy = cfg.getModuleUpdatePolicy("newMod");
    const char* policyStr = nullptr;
    switch (policy) {
        case ConfigManager::UpdatePolicy::kNoUpdate: policyStr = "none"; break;
        case ConfigManager::UpdatePolicy::kFirstUpdate: policyStr = "first"; break;
        case ConfigManager::UpdatePolicy::kAlwaysUpdate: policyStr = "always"; break;
        case ConfigManager::UpdatePolicy::kOnChangeUpdate: policyStr = "on_change"; break;
    }

    std::cout << "newMod policy: " << policyStr << std::endl;

    if (policy != ConfigManager::UpdatePolicy::kFirstUpdate) {
        std::cerr << "Expected first, got " << policyStr << std::endl;
        return 1;
    }

    std::cout << "Default policy loading works!" << std::endl;
    return 0;
}
