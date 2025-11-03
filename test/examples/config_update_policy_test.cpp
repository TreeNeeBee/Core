#include "CConfig.hpp"
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <nlohmann/json.hpp>

using namespace lap::core;
using json = nlohmann::json;

static json readJsonFile(const char* path) {
    std::ifstream in(path);
    if (!in.is_open()) return json::object();
    std::string s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    return json::parse(s);
}

int main() {
    std::cout << "==== Config Update Policy Test ====" << std::endl;
    const char* filePath = "config_policy.json";

    // Ensure deterministic start
    std::remove(filePath);
    setenv("HMAC_SECRET", "policy-secret", 1);

    auto& cfg = ConfigManager::getInstance();
    auto init = cfg.initialize(filePath, true);
    if (!init.HasValue()) {
        std::cerr << "Initialize failed" << std::endl;
        return 1;
    }

    // Setup initial modules
    json modA = { {"counter", 1} };
    json modB = { {"data", "v1"} };
    json modC = json::array({1,2,3});
    json modD = { {"val", "orig"} };

    cfg.setModuleConfigJson("modA", modA);
    cfg.setModuleConfigJson("modB", modB);
    cfg.setModuleConfigJson("modC", modC);
    cfg.setModuleConfigJson("modD", modD);

    // Policies: note modC intentionally left to default (on_change)
    cfg.setModuleUpdatePolicy("modA", ConfigManager::UpdatePolicy::kFirstUpdate);
    cfg.setModuleUpdatePolicy("modB", ConfigManager::UpdatePolicy::kAlwaysUpdate);
    cfg.setModuleUpdatePolicy("modD", ConfigManager::UpdatePolicy::kNoUpdate);

    // First save - should persist all
    auto s1 = cfg.save(true);
    if (!s1.HasValue()) {
        std::cerr << "First save failed" << std::endl;
        return 2;
    }

    // Mutate in-memory
    json modA2 = cfg.getModuleConfigJson("modA");
    modA2["counter"] = 2;
    cfg.setModuleConfigJson("modA", modA2);

    json modB2 = cfg.getModuleConfigJson("modB");
    modB2["data"] = "v2";
    cfg.setModuleConfigJson("modB", modB2);

    // modC unchanged

    json modD2 = cfg.getModuleConfigJson("modD");
    modD2["val"] = "changed";
    cfg.setModuleConfigJson("modD", modD2);

    // Second save - apply policies
    auto s2 = cfg.save(true);
    if (!s2.HasValue()) {
        std::cerr << "Second save failed" << std::endl;
        return 3;
    }

    // Read file and verify
    json full = readJsonFile(filePath);
    if (!full.is_object()) {
        std::cerr << "Persisted file not JSON object" << std::endl;
        return 4;
    }
    // Remove __metadata__
    full.erase("__metadata__");

    // modA FirstUpdate: should keep original counter=1
    if (!full.contains("modA") || full["modA"].is_null()) { std::cerr << "Missing modA" << std::endl; return 5; }
    if (full["modA"]["counter"].get<int>() != 1) { std::cerr << "modA counter mismatch" << std::endl; return 6; }

    // modB AlwaysUpdate: should be v2
    if (!full.contains("modB")) { std::cerr << "Missing modB" << std::endl; return 8; }
    if (full["modB"]["data"].get<std::string>() != std::string("v2")) { std::cerr << "modB data mismatch" << std::endl; return 9; }

    // modC OnChangeUpdate (default) with no changes: array unchanged
    if (!full.contains("modC") || !full["modC"].is_array()) { std::cerr << "Missing modC" << std::endl; return 11; }

    // modD NoUpdate: should remain orig
    if (!full.contains("modD") || !full["modD"].is_object()) { std::cerr << "Missing modD" << std::endl; return 13; }
    if (full["modD"]["val"].get<std::string>() != std::string("orig")) { std::cerr << "modD value should not update" << std::endl; return 14; }

    // Top-level mapping must exist and contain default + A,B,D; C omitted
    if (!full.contains("__update_policy__") || !full["__update_policy__"].is_object()) { std::cerr << "top-level mapping missing" << std::endl; return 15; }
    auto& up = full["__update_policy__"];
    if (!up.contains("default") || up["default"] != "on_change") { std::cerr << "default policy missing/wrong" << std::endl; return 16; }
    if (!up.contains("modA") || up["modA"] != "first") { std::cerr << "modA policy wrong" << std::endl; return 17; }
    if (!up.contains("modB") || up["modB"] != "always") { std::cerr << "modB policy wrong" << std::endl; return 18; }
    if (up.contains("modC")) { std::cerr << "modC policy should not be explicitly set" << std::endl; return 19; }
    if (!up.contains("modD") || up["modD"] != "none") { std::cerr << "modD policy wrong" << std::endl; return 20; }

    std::cout << "All update policy checks passed." << std::endl;
    return 0;
}
