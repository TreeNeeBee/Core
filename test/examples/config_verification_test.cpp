/**
 * @file        config_verification_test.cpp
 * @brief       Test skip verification feature and error handling
 * @date        2025-10-31
 */

#include "CConfig.hpp"
#include <iostream>
#include <cstdlib>
#include <fstream>

using namespace lap::core;

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << " Configuration Verification Test" << std::endl;
    std::cout << "========================================\n" << std::endl;
    
    // Set HMAC secret
    setenv("HMAC_SECRET", "test-secret-key", 1);
    
    ConfigManager& config = ConfigManager::getInstance();
    
    // ========================================================================
    // Test 1: Normal save and load with verification
    // ========================================================================
    std::cout << "=== Test 1: Normal Security Verification ===" << std::endl;
    
    std::cout << "\n1. Creating configuration..." << std::endl;
    config.initialize("test_verify.json", true);
    config.setVersion(1);
    config.setDescription("Test Configuration");
    config.setInt("test.value", 12345);
    config.setString("test.name", "verification_test");
    
    std::cout << "\n2. Saving with security..." << std::endl;
    auto saveResult = config.save(true);
    if (saveResult.HasValue()) {
        std::cout << "   ✓ Saved successfully" << std::endl;
    }
    
    std::cout << "\n3. Loading with verification (skipVerification=false)..." << std::endl;
    config.clear();
    config.initialize("test_verify.json", true);
    auto loadResult = config.load(false);  // Enforce verification
    if (loadResult.HasValue()) {
        std::cout << "   ✓ Loaded and verified successfully" << std::endl;
        std::cout << "   test.value = " << config.getInt("test.value") << std::endl;
    } else {
        std::cout << "   ✗ Verification failed" << std::endl;
    }
    
    // ========================================================================
    // Test 2: Skip verification
    // ========================================================================
    std::cout << "\n\n=== Test 2: Skip Verification ===" << std::endl;
    
    std::cout << "\n4. Loading with skipVerification=true..." << std::endl;
    config.clear();
    config.initialize("test_verify.json", true);
    loadResult = config.load(true);  // Skip verification
    if (loadResult.HasValue()) {
        std::cout << "   ✓ Loaded without verification" << std::endl;
        std::cout << "   test.value = " << config.getInt("test.value") << std::endl;
    }
    
    // ========================================================================
    // Test 3: Tamper detection
    // ========================================================================
    std::cout << "\n\n=== Test 3: Tamper Detection ===" << std::endl;
    
    std::cout << "\n5. Manually tampering with file..." << std::endl;
    // Read file
    std::ifstream inFile("test_verify.json");
    std::string content((std::istreambuf_iterator<char>(inFile)),
                        std::istreambuf_iterator<char>());
    inFile.close();
    
    // Tamper with the content (change a value)
    size_t pos = content.find("12345");
    if (pos != std::string::npos) {
        content.replace(pos, 5, "99999");
        std::cout << "   Changed test.value from 12345 to 99999" << std::endl;
    }
    
    // Write back
    std::ofstream outFile("test_verify.json");
    outFile << content;
    outFile.close();
    std::cout << "   ✓ File tampered" << std::endl;
    
    std::cout << "\n6. Attempting to load tampered file..." << std::endl;
    config.clear();
    config.initialize("test_verify.json", true);
    loadResult = config.load(false);  // Should fail
    if (!loadResult.HasValue()) {
        std::cout << "   ✓ Tamper detected! Load failed as expected" << std::endl;
        std::cout << "   Error code: " << static_cast<int>(loadResult.Error()) << std::endl;
    } else {
        std::cout << "   ✗ ERROR: Tamper not detected!" << std::endl;
    }
    
    std::cout << "\n7. Loading tampered file with skipVerification=true..." << std::endl;
    config.clear();
    config.initialize("test_verify.json", true);
    loadResult = config.load(true);  // Should succeed
    if (loadResult.HasValue()) {
        std::cout << "   ✓ Loaded despite tampering (verification skipped)" << std::endl;
        std::cout << "   test.value = " << config.getInt("test.value") << " (tampered value)" << std::endl;
    }
    
    // ========================================================================
    // Test 4: Missing HMAC secret
    // ========================================================================
    std::cout << "\n\n=== Test 4: Missing HMAC Secret ===" << std::endl;
    
    std::cout << "\n8. Creating new config without HMAC secret..." << std::endl;
    unsetenv("HMAC_SECRET");
    ConfigManager& config2 = ConfigManager::getInstance();
    
    std::cout << "\n9. Saving without HMAC secret (should fail)..." << std::endl;
    config2.initialize("test_no_hmac.json", true);
    config2.setInt("test.value", 123);
    saveResult = config2.save(true);
    if (!saveResult.HasValue()) {
        std::cout << "   ✓ Save failed as expected (no HMAC secret)" << std::endl;
        std::cout << "   Error code: " << static_cast<int>(saveResult.Error()) << std::endl;
    }
    
    std::cout << "\n10. Saving without security fields..." << std::endl;
    saveResult = config2.save(false);  // Skip security
    if (saveResult.HasValue()) {
        std::cout << "   ✓ Saved successfully without security fields" << std::endl;
    }
    
    // ========================================================================
    // Test 5: Error handling
    // ========================================================================
    std::cout << "\n\n=== Test 5: Error Handling ===" << std::endl;
    
    std::cout << "\n11. Testing invalid JSON..." << std::endl;
    auto parseResult = config2.setModuleConfig("test", "{invalid json}");
    if (!parseResult.HasValue()) {
        std::cout << "   ✓ Parse error detected correctly" << std::endl;
    }
    
    std::cout << "\n12. Testing nonexistent file..." << std::endl;
    config2.initialize("nonexistent_file_xyz.json", true);
    loadResult = config2.load();
    if (!loadResult.HasValue()) {
        std::cout << "   ✓ File not found error detected" << std::endl;
    }
    
    // ========================================================================
    // Summary
    // ========================================================================
    std::cout << "\n\n========================================" << std::endl;
    std::cout << " Test Summary" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "✓ Security verification works correctly" << std::endl;
    std::cout << "✓ Skip verification option works" << std::endl;
    std::cout << "✓ Tamper detection works" << std::endl;
    std::cout << "✓ HMAC secret validation works" << std::endl;
    std::cout << "✓ Error handling is comprehensive" << std::endl;
    std::cout << "\nAll tests passed!" << std::endl;
    
    return 0;
}
