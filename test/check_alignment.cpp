#include <cstdio>
#include <cstddef>
#include "CMemory.hpp"

using namespace lap::core;

// 复制结构体定义用于测试
struct tagUnitNode {
    void* pool;
    union {
        void* nextUnit;
        uint32_t magic;
    };
};

struct tagBlockHeader {
    uint32_t magic1;
    uint32_t magic2;     // Moved here
    void* next;
    void* prev;
    size_t size;         // Changed to size_t
    uint32_t classId;
    uint32_t threadId;
    uint32_t allocTag;
};

int main() {
    printf("=== Memory Structure Alignment Check ===\n\n");
    
    // 检查基本类型大小
    printf("Basic Types:\n");
    printf("  sizeof(void*) = %zu\n", sizeof(void*));
    printf("  sizeof(uint32_t) = %zu\n", sizeof(uint32_t));
    printf("  sizeof(uint64_t) = %zu\n", sizeof(uint64_t));
    printf("  sizeof(size_t) = %zu\n", sizeof(size_t));
    printf("\n");
    
    // 检查 tagUnitNode
    printf("tagUnitNode:\n");
    printf("  sizeof(tagUnitNode) = %zu bytes\n", sizeof(tagUnitNode));
    printf("  alignof(tagUnitNode) = %zu bytes\n", alignof(tagUnitNode));
    printf("  offsetof(pool) = %zu\n", offsetof(tagUnitNode, pool));
    printf("  offsetof(nextUnit) = %zu\n", offsetof(tagUnitNode, nextUnit));
    printf("  offsetof(magic) = %zu\n", offsetof(tagUnitNode, magic));
    printf("\n");
    
    // 检查 tagBlockHeader
    printf("tagBlockHeader:\n");
    printf("  sizeof(tagBlockHeader) = %zu bytes\n", sizeof(tagBlockHeader));
    printf("  alignof(tagBlockHeader) = %zu bytes\n", alignof(tagBlockHeader));
    printf("  offsetof(magic1) = %zu\n", offsetof(tagBlockHeader, magic1));
    printf("  offsetof(next) = %zu\n", offsetof(tagBlockHeader, next));
    printf("  offsetof(prev) = %zu\n", offsetof(tagBlockHeader, prev));
    printf("  offsetof(size) = %zu\n", offsetof(tagBlockHeader, size));
    printf("  offsetof(classId) = %zu\n", offsetof(tagBlockHeader, classId));
    printf("  offsetof(threadId) = %zu\n", offsetof(tagBlockHeader, threadId));
    printf("  offsetof(magic2) = %zu\n", offsetof(tagBlockHeader, magic2));
    printf("  offsetof(allocTag) = %zu\n", offsetof(tagBlockHeader, allocTag));
    printf("\n");
    
    // 检查潜在的对齐问题
    printf("Alignment Issues:\n");
    if (sizeof(tagUnitNode) % 8 != 0) {
        printf("  [WARNING] tagUnitNode size (%zu) is not 8-byte aligned!\n", sizeof(tagUnitNode));
    } else {
        printf("  [OK] tagUnitNode is 8-byte aligned\n");
    }
    
    if (sizeof(tagBlockHeader) % 8 != 0) {
        printf("  [WARNING] tagBlockHeader size (%zu) is not 8-byte aligned!\n", sizeof(tagBlockHeader));
    } else {
        printf("  [OK] tagBlockHeader is 8-byte aligned\n");
    }
    
    // 检查字段对齐
    printf("\nField Alignment:\n");
    printf("  tagBlockHeader::next ptr alignment: %s\n", 
           (offsetof(tagBlockHeader, next) % sizeof(void*) == 0) ? "OK" : "MISALIGNED");
    printf("  tagBlockHeader::prev ptr alignment: %s\n", 
           (offsetof(tagBlockHeader, prev) % sizeof(void*) == 0) ? "OK" : "MISALIGNED");
    
    return 0;
}
