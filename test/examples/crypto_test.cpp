/**
 * @file        crypto_test.cpp
 * @brief       Test program for Crypto class
 * @date        2025-11-11
 */

#include "CCrypto.hpp"
#include "CInitialization.hpp"
#include <iostream>
#include <cassert>

using namespace lap::core;

int main() {
    // AUTOSAR-compliant initialization
    auto initResult = Initialize();
    if (!initResult.HasValue()) {
        std::cerr << "Failed to initialize Core: " << initResult.Error().Message() << "\n";
        return 1;
    }
    
    std::cout << "=== Crypto Class Test ===" << std::endl;
    
    // Test 1: CRC32
    std::cout << "\n[Test 1] CRC32 Computation" << std::endl;
    String testData = "Hello, World!";
    UInt32 crc = Crypto::Util::computeCrc32(
        reinterpret_cast<const UInt8*>(testData.c_str()),
        testData.length()
    );
    std::cout << "Data: " << testData << std::endl;
    std::cout << "CRC32: 0x" << std::hex << crc << std::dec << std::endl;
    
    // Test 2: SHA256
    std::cout << "\n[Test 2] SHA256 Hash" << std::endl;
    String sha256Hash = Crypto::Util::computeSha256(
        reinterpret_cast<const UInt8*>(testData.c_str()),
        testData.length()
    );
    std::cout << "SHA256: " << sha256Hash << std::endl;
    assert(!sha256Hash.empty());
    assert(sha256Hash.length() == 64);  // SHA256 produces 64 hex characters
    
    // Test 3: Hex Conversion
    std::cout << "\n[Test 3] Hex Conversion" << std::endl;
    UInt8 bytes[] = {0xDE, 0xAD, 0xBE, 0xEF};
    String hex = Crypto::Util::bytesToHex(bytes, 4);
    std::cout << "Bytes to Hex: " << hex << std::endl;
    assert(hex == "deadbeef");
    
    Vector<UInt8> decodedBytes;
    Bool success = Crypto::Util::hexToBytes(hex, decodedBytes);
    std::cout << "Hex to Bytes: ";
    for (auto b : decodedBytes) {
        std::cout << std::hex << static_cast<int>(b) << " ";
    }
    std::cout << std::dec << std::endl;
    assert(success);
    assert(decodedBytes.size() == 4);
    assert(decodedBytes[0] == 0xDE);
    assert(decodedBytes[1] == 0xAD);
    assert(decodedBytes[2] == 0xBE);
    assert(decodedBytes[3] == 0xEF);
    
    // Test 4: HMAC-SHA256
    std::cout << "\n[Test 4] HMAC-SHA256" << std::endl;
    Crypto crypto("my-secret-key");  // Use explicit key constructor for testing
    
    String hmac = crypto.computeHmac(
        reinterpret_cast<const UInt8*>(testData.c_str()),
        testData.length()
    );
    std::cout << "HMAC: " << hmac << std::endl;
    assert(!hmac.empty());
    assert(hmac.length() == 64);  // HMAC-SHA256 produces 64 hex characters
    
    // Test 5: HMAC Verification
    std::cout << "\n[Test 5] HMAC Verification" << std::endl;
    Bool valid = crypto.verifyHmac(
        reinterpret_cast<const UInt8*>(testData.c_str()),
        testData.length(),
        hmac
    );
    std::cout << "Verification (correct HMAC): " << (valid ? "PASS" : "FAIL") << std::endl;
    assert(valid);
    
    Bool invalid = crypto.verifyHmac(
        reinterpret_cast<const UInt8*>(testData.c_str()),
        testData.length(),
        "0000000000000000000000000000000000000000000000000000000000000000"
    );
    std::cout << "Verification (wrong HMAC): " << (!invalid ? "PASS" : "FAIL") << std::endl;
    assert(!invalid);
    
    // Test 6: Multiple Crypto instances with different keys
    std::cout << "\n[Test 6] Multiple Keys" << std::endl;
    Crypto crypto1("key1");  // Use explicit key constructor
    Crypto crypto2("key2");  // Use explicit key constructor
    
    String hmac1 = crypto1.computeHmac(
        reinterpret_cast<const UInt8*>(testData.c_str()),
        testData.length()
    );
    String hmac2 = crypto2.computeHmac(
        reinterpret_cast<const UInt8*>(testData.c_str()),
        testData.length()
    );
    
    std::cout << "HMAC with key1: " << hmac1 << std::endl;
    std::cout << "HMAC with key2: " << hmac2 << std::endl;
    assert(hmac1 != hmac2);
    std::cout << "Different keys produce different HMACs: PASS" << std::endl;
    
    std::cout << "\n=== All Tests PASSED ===" << std::endl;
    
    // AUTOSAR-compliant deinitialization
    auto deinitResult = Deinitialize();
    (void)deinitResult;
    
    return 0;
}
