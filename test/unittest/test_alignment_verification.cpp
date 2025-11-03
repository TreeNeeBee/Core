/**
 * @file test_alignment_verification.cpp
 * @brief Verify memory alignment with different configuration values
 */

#include <gtest/gtest.h>
#include <iomanip>
#include <iostream>
#include "CMemory.hpp"
#include "CConfig.hpp"
#include <nlohmann/json.hpp>

using namespace lap::core;

class AlignmentVerificationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize memory manager if needed
    }
    
    void TearDown() override {
        // Cleanup
    }
    
    // Helper function to check if address is aligned
    bool isAligned(void* ptr, size_t alignment) {
        return (reinterpret_cast<uintptr_t>(ptr) % alignment) == 0;
    }
    
    // Helper to print address info
    void printAddressInfo(const std::string& label, void* ptr, size_t alignment) {
        uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
        bool aligned = isAligned(ptr, alignment);
        
        std::cout << label << ": "
                  << "0x" << std::hex << std::setw(16) << std::setfill('0') << addr
                  << " (mod " << std::dec << alignment << " = " << (addr % alignment) << ")"
                  << " [" << (aligned ? "✓ ALIGNED" : "✗ NOT ALIGNED") << "]"
                  << std::endl;
    }
};

TEST_F(AlignmentVerificationTest, VerifyCurrentAlignment) {
    std::cout << "\n=== Current Alignment Configuration ===" << std::endl;
    
    // Allocate various sizes and check alignment
    const size_t testSizes[] = {1, 7, 16, 31, 64, 127, 256, 512, 1000};
    
    for (size_t size : testSizes) {
        void* ptr = Memory::malloc(size);
        ASSERT_NE(ptr, nullptr) << "Failed to allocate " << size << " bytes";
        
        // Check if address is aligned to various boundaries
        std::cout << "\nAllocation of " << size << " bytes:" << std::endl;
        printAddressInfo("  Address", ptr, 1);
        
        // Check common alignments
        if (isAligned(ptr, 8)) {
            std::cout << "  ✓ 8-byte aligned" << std::endl;
        } else if (isAligned(ptr, 4)) {
            std::cout << "  ✓ 4-byte aligned" << std::endl;
        } else if (isAligned(ptr, 2)) {
            std::cout << "  ✓ 2-byte aligned" << std::endl;
        } else {
            std::cout << "  ✓ 1-byte aligned (no alignment)" << std::endl;
        }
        
        // Verify we can write to all bytes
        uint8_t* bytePtr = static_cast<uint8_t*>(ptr);
        for (size_t i = 0; i < size; ++i) {
            bytePtr[i] = static_cast<uint8_t>(i & 0xFF);
        }
        
        // Verify data integrity
        for (size_t i = 0; i < size; ++i) {
            EXPECT_EQ(bytePtr[i], static_cast<uint8_t>(i & 0xFF))
                << "Data corruption at byte " << i;
        }
        
        Memory::free(ptr);
    }
}

TEST_F(AlignmentVerificationTest, VerifyMultipleAllocations) {
    std::cout << "\n=== Multiple Allocations Address Pattern ===" << std::endl;
    
    const int numAllocs = 10;
    const size_t allocSize = 17; // Odd size to test alignment
    void* allocations[numAllocs];
    
    std::cout << "Allocating " << numAllocs << " blocks of " << allocSize << " bytes each:\n" << std::endl;
    
    for (int i = 0; i < numAllocs; ++i) {
        allocations[i] = Memory::malloc(allocSize);
        ASSERT_NE(allocations[i], nullptr);
        
        uintptr_t addr = reinterpret_cast<uintptr_t>(allocations[i]);
        std::cout << "Alloc[" << i << "]: 0x" << std::hex << std::setw(16) << std::setfill('0') 
                  << addr << std::dec;
        
        // Check alignment
        if (addr % 8 == 0) {
            std::cout << " [8-byte aligned]";
        } else if (addr % 4 == 0) {
            std::cout << " [4-byte aligned]";
        } else if (addr % 2 == 0) {
            std::cout << " [2-byte aligned]";
        } else {
            std::cout << " [1-byte aligned]";
        }
        
        // Calculate gap from previous allocation
        if (i > 0) {
            uintptr_t prevAddr = reinterpret_cast<uintptr_t>(allocations[i-1]);
            intptr_t gap = addr - prevAddr;
            std::cout << " (gap: " << gap << " bytes)";
        }
        std::cout << std::endl;
    }
    
    // Free all
    for (int i = 0; i < numAllocs; ++i) {
        Memory::free(allocations[i]);
    }
}

TEST_F(AlignmentVerificationTest, VerifyStructAlignment) {
    std::cout << "\n=== Structure Alignment Test ===" << std::endl;
    
    // Test with packed structure
    struct __attribute__((packed)) PackedStruct {
        uint8_t  a;  // 1 byte
        uint32_t b;  // 4 bytes
        uint16_t c;  // 2 bytes
        // Total: 7 bytes (no padding)
    };
    
    // Test with natural structure
    struct NaturalStruct {
        uint8_t  a;  // 1 byte + 3 padding
        uint32_t b;  // 4 bytes
        uint16_t c;  // 2 bytes + 2 padding
        // Total: 12 bytes (with padding)
    };
    
    std::cout << "Packed struct size: " << sizeof(PackedStruct) << " bytes" << std::endl;
    std::cout << "Natural struct size: " << sizeof(NaturalStruct) << " bytes" << std::endl;
    
    // Allocate packed structure
    PackedStruct* packed = static_cast<PackedStruct*>(Memory::malloc(sizeof(PackedStruct)));
    ASSERT_NE(packed, nullptr);
    printAddressInfo("Packed struct", packed, 1);
    
    // Initialize and verify
    packed->a = 0x12;
    packed->b = 0x34567890;
    packed->c = 0xABCD;
    
    EXPECT_EQ(packed->a, 0x12);
    EXPECT_EQ(packed->b, 0x34567890);
    EXPECT_EQ(packed->c, 0xABCD);
    
    Memory::free(packed);
    
    // Allocate natural structure
    NaturalStruct* natural = static_cast<NaturalStruct*>(Memory::malloc(sizeof(NaturalStruct)));
    ASSERT_NE(natural, nullptr);
    printAddressInfo("Natural struct", natural, 1);
    
    natural->a = 0x12;
    natural->b = 0x34567890;
    natural->c = 0xABCD;
    
    EXPECT_EQ(natural->a, 0x12);
    EXPECT_EQ(natural->b, 0x34567890);
    EXPECT_EQ(natural->c, 0xABCD);
    
    Memory::free(natural);
}

TEST_F(AlignmentVerificationTest, VerifyUnalignedMemoryAccess) {
    std::cout << "\n=== Unaligned Memory Access Test ===" << std::endl;
    
    const size_t bufferSize = 128;
    uint8_t* buffer = static_cast<uint8_t*>(Memory::malloc(bufferSize));
    ASSERT_NE(buffer, nullptr);
    
    printAddressInfo("Buffer base", buffer, 1);
    
    // Fill buffer with pattern
    for (size_t i = 0; i < bufferSize; ++i) {
        buffer[i] = static_cast<uint8_t>(i);
    }
    
    // Test unaligned 32-bit read at various offsets
    std::cout << "\nTesting unaligned 32-bit reads:" << std::endl;
    for (size_t offset = 0; offset < 16; ++offset) {
        uint32_t value;
        std::memcpy(&value, buffer + offset, sizeof(uint32_t));
        
        uint32_t expected = (buffer[offset] << 0) |
                           (buffer[offset+1] << 8) |
                           (buffer[offset+2] << 16) |
                           (buffer[offset+3] << 24);
        
        std::cout << "  Offset " << offset << ": 0x" << std::hex << std::setw(8) << std::setfill('0') 
                  << value << " (expected: 0x" << expected << ")" << std::dec;
        
        if (value == expected) {
            std::cout << " ✓" << std::endl;
        } else {
            std::cout << " ✗" << std::endl;
        }
        
        EXPECT_EQ(value, expected) << "Unaligned read failed at offset " << offset;
    }
    
    Memory::free(buffer);
}

TEST_F(AlignmentVerificationTest, CompareAlignmentBehaviors) {
    std::cout << "\n=== Alignment Behavior Summary ===" << std::endl;
    
    const size_t testSize = 100;
    void* ptr = Memory::malloc(testSize);
    ASSERT_NE(ptr, nullptr);
    
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    
    std::cout << "Allocated " << testSize << " bytes at address: 0x" 
              << std::hex << std::setw(16) << std::setfill('0') << addr << std::dec << std::endl;
    std::cout << "\nAlignment check:" << std::endl;
    std::cout << "  1-byte aligned: " << (addr % 1 == 0 ? "YES ✓" : "NO ✗") << std::endl;
    std::cout << "  2-byte aligned: " << (addr % 2 == 0 ? "YES ✓" : "NO ✗") << std::endl;
    std::cout << "  4-byte aligned: " << (addr % 4 == 0 ? "YES ✓" : "NO ✗") << std::endl;
    std::cout << "  8-byte aligned: " << (addr % 8 == 0 ? "YES ✓" : "NO ✗") << std::endl;
    std::cout << "  16-byte aligned: " << (addr % 16 == 0 ? "YES ✓" : "NO ✗") << std::endl;
    
    // Determine effective alignment
    size_t effectiveAlignment = 1;
    for (size_t align = 2; align <= 16; align *= 2) {
        if (addr % align == 0) {
            effectiveAlignment = align;
        } else {
            break;
        }
    }
    
    std::cout << "\nEffective alignment: " << effectiveAlignment << " bytes" << std::endl;
    
    // Note about configuration
    std::cout << "\nNote: The actual alignment is determined by:" << std::endl;
    std::cout << "  1. Configuration file 'memory_config.json' align field" << std::endl;
    std::cout << "  2. System default (8 bytes for 64-bit, 4 bytes for 32-bit)" << std::endl;
    std::cout << "  3. Allocator implementation (may provide better alignment)" << std::endl;
    
    Memory::free(ptr);
}
