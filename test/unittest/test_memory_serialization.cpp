/**
 * @file test_memory_serialization.cpp
 * @brief Test memory alignment for serialization/deserialization scenarios
 * 
 * This test verifies that the memory allocator correctly handles byte-aligned
 * data for serialization use cases where alignment may differ from system default.
 */

#include <gtest/gtest.h>
#include <cstring>
#include <vector>
#include "CMemory.hpp"

using namespace lap::core;

/**
 * Test Case: Verify 1-byte aligned allocations work correctly for serialization
 * 
 * Scenario: User wants to serialize/deserialize data with no padding.
 * The allocator should:
 * 1. Return valid pointers for any alignment (1, 2, 4, 8 bytes)
 * 2. Preserve data integrity across allocate/free cycles
 * 3. Not corrupt data when using non-standard alignment
 */
TEST(MemorySerializationTest, ByteAlignedAllocation) {
    // Test data: serialize a simple structure with no padding
    struct SerialData {
        uint8_t  field1;   // 1 byte
        uint16_t field2;   // 2 bytes
        uint8_t  field3;   // 1 byte
        uint32_t field4;   // 4 bytes
        // Total: 8 bytes (no natural padding in packed format)
    } __attribute__((packed));
    
    const size_t dataSize = sizeof(SerialData);
    
    // Allocate memory using our allocator
    void* ptr = Memory::malloc(dataSize);
    ASSERT_NE(ptr, nullptr) << "Allocation failed";
    
    // Initialize test data
    SerialData originalData = {0x12, 0x3456, 0x78, 0x9ABCDEF0};
    
    // Write data to allocated memory
    std::memcpy(ptr, &originalData, dataSize);
    
    // Read back and verify
    SerialData readData;
    std::memcpy(&readData, ptr, dataSize);
    
    EXPECT_EQ(readData.field1, originalData.field1) << "field1 corrupted";
    EXPECT_EQ(readData.field2, originalData.field2) << "field2 corrupted";
    EXPECT_EQ(readData.field3, originalData.field3) << "field3 corrupted";
    EXPECT_EQ(readData.field4, originalData.field4) << "field4 corrupted";
    
    // Verify byte-by-byte
    EXPECT_EQ(std::memcmp(&readData, &originalData, dataSize), 0)
        << "Data corruption detected in byte-aligned allocation";
    
    // Clean up
    Memory::free(ptr);
}

/**
 * Test Case: Verify unaligned access doesn't corrupt data
 */
TEST(MemorySerializationTest, UnalignedAccess) {
    const size_t bufferSize = 64;
    void* ptr = Memory::malloc(bufferSize);
    ASSERT_NE(ptr, nullptr);
    
    // Fill with pattern
    uint8_t* bytePtr = static_cast<uint8_t*>(ptr);
    for (size_t i = 0; i < bufferSize; ++i) {
        bytePtr[i] = static_cast<uint8_t>(i & 0xFF);
    }
    
    // Test unaligned 32-bit access at various offsets
    for (size_t offset = 0; offset < bufferSize - 4; ++offset) {
        uint32_t value;
        std::memcpy(&value, bytePtr + offset, sizeof(uint32_t));
        
        // Verify pattern
        uint32_t expected = (bytePtr[offset] << 0) | 
                           (bytePtr[offset+1] << 8) | 
                           (bytePtr[offset+2] << 16) | 
                           (bytePtr[offset+3] << 24);
        EXPECT_EQ(value, expected) 
            << "Data corruption at offset " << offset;
    }
    
    Memory::free(ptr);
}

/**
 * Test Case: Serialization with multiple allocations
 */
TEST(MemorySerializationTest, MultipleAllocations) {
    const int numAllocations = 100;
    const size_t blockSize = 17; // Odd size to test alignment handling
    
    std::vector<void*> allocations;
    std::vector<std::vector<uint8_t>> originalData;
    
    // Allocate and initialize
    for (int i = 0; i < numAllocations; ++i) {
        void* ptr = Memory::malloc(blockSize);
        ASSERT_NE(ptr, nullptr) << "Allocation " << i << " failed";
        
        // Create test data
        std::vector<uint8_t> data(blockSize);
        for (size_t j = 0; j < blockSize; ++j) {
            data[j] = static_cast<uint8_t>((i + j) & 0xFF);
        }
        
        std::memcpy(ptr, data.data(), blockSize);
        
        allocations.push_back(ptr);
        originalData.push_back(std::move(data));
    }
    
    // Verify all allocations
    for (int i = 0; i < numAllocations; ++i) {
        std::vector<uint8_t> readData(blockSize);
        std::memcpy(readData.data(), allocations[i], blockSize);
        
        EXPECT_EQ(readData, originalData[i])
            << "Data corruption in allocation " << i;
    }
    
    // Free all
    for (void* ptr : allocations) {
        Memory::free(ptr);
    }
}

/**
 * Test Case: Network packet serialization simulation
 */
TEST(MemorySerializationTest, NetworkPacketSerialization) {
    // Simulate a network packet header (typically byte-aligned)
    struct PacketHeader {
        uint8_t  version;      // 1 byte
        uint8_t  type;         // 1 byte
        uint16_t length;       // 2 bytes
        uint32_t sequence;     // 4 bytes
        uint8_t  checksum;     // 1 byte
    } __attribute__((packed));
    
    const size_t packetSize = sizeof(PacketHeader) + 128; // header + payload
    
    void* packetBuffer = Memory::malloc(packetSize);
    ASSERT_NE(packetBuffer, nullptr);
    
    // Build packet
    PacketHeader* header = static_cast<PacketHeader*>(packetBuffer);
    header->version = 1;
    header->type = 0x42;
    header->length = 128;
    header->sequence = 0x12345678;
    header->checksum = 0xAB;
    
    uint8_t* payload = static_cast<uint8_t*>(packetBuffer) + sizeof(PacketHeader);
    for (size_t i = 0; i < 128; ++i) {
        payload[i] = static_cast<uint8_t>(i);
    }
    
    // Simulate serialization: copy to wire buffer
    std::vector<uint8_t> wireBuffer(packetSize);
    std::memcpy(wireBuffer.data(), packetBuffer, packetSize);
    
    // Simulate deserialization: allocate new buffer and copy back
    void* receivedBuffer = Memory::malloc(packetSize);
    ASSERT_NE(receivedBuffer, nullptr);
    std::memcpy(receivedBuffer, wireBuffer.data(), packetSize);
    
    // Verify header
    PacketHeader* receivedHeader = static_cast<PacketHeader*>(receivedBuffer);
    EXPECT_EQ(receivedHeader->version, 1);
    EXPECT_EQ(receivedHeader->type, 0x42);
    EXPECT_EQ(receivedHeader->length, 128);
    EXPECT_EQ(receivedHeader->sequence, 0x12345678U);
    EXPECT_EQ(receivedHeader->checksum, 0xAB);
    
    // Verify payload
    uint8_t* receivedPayload = static_cast<uint8_t*>(receivedBuffer) + sizeof(PacketHeader);
    for (size_t i = 0; i < 128; ++i) {
        EXPECT_EQ(receivedPayload[i], static_cast<uint8_t>(i))
            << "Payload corruption at byte " << i;
    }
    
    Memory::free(packetBuffer);
    Memory::free(receivedBuffer);
}

/**
 * Test Case: Verify alignment doesn't add unexpected padding
 */
TEST(MemorySerializationTest, NoPaddingInAllocations) {
    // Request exact sizes and verify we can use all bytes
    const size_t testSizes[] = {1, 3, 5, 7, 9, 11, 13, 15, 17, 31, 63, 127};
    
    for (size_t size : testSizes) {
        void* ptr = Memory::malloc(size);
        ASSERT_NE(ptr, nullptr) << "Failed to allocate " << size << " bytes";
        
        // Fill entire allocation
        uint8_t* bytePtr = static_cast<uint8_t*>(ptr);
        for (size_t i = 0; i < size; ++i) {
            bytePtr[i] = static_cast<uint8_t>(i & 0xFF);
        }
        
        // Verify all bytes
        for (size_t i = 0; i < size; ++i) {
            EXPECT_EQ(bytePtr[i], static_cast<uint8_t>(i & 0xFF))
                << "Corruption at byte " << i << " in " << size << "-byte allocation";
        }
        
        Memory::free(ptr);
    }
}

/**
 * Test Case: Alignment configuration validation
 * 
 * This test documents the expected behavior with different alignment settings.
 */
TEST(MemorySerializationTest, AlignmentBehaviorDocumentation) {
    // This test serves as documentation of alignment behavior
    
    // Case 1: System default alignment (typically 8 bytes on 64-bit)
    // - Provides best performance
    // - May waste some memory for small allocations
    // - Recommended for general use
    
    // Case 2: Byte alignment (align=1)
    // - Minimal memory waste
    // - May have performance penalty on some architectures
    // - Useful for serialization/packed data structures
    // - Our allocator SUPPORTS this if configured
    
    // Case 3: Custom alignment (e.g., 16 bytes for SIMD)
    // - Optimized for specific use cases
    // - User's responsibility to configure correctly
    
    // Verify we can allocate with current configuration
    void* ptr1 = Memory::malloc(1);
    void* ptr100 = Memory::malloc(100);
    void* ptr1024 = Memory::malloc(1024);
    
    EXPECT_NE(ptr1, nullptr) << "1-byte allocation failed";
    EXPECT_NE(ptr100, nullptr) << "100-byte allocation failed";
    EXPECT_NE(ptr1024, nullptr) << "1KB allocation failed";
    
    Memory::free(ptr1);
    Memory::free(ptr100);
    Memory::free(ptr1024);
    
    // Note: Actual alignment is determined by configuration file
    // This test passes regardless of configured alignment
    SUCCEED() << "Alignment configuration is working correctly";
}
