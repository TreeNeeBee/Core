/**
 * @file        CMemory.hpp
 * @author      ddkv587 ( ddkv587@gmail.com )
 * @brief       Memory management utilities for AUTOSAR Adaptive Platform
 * @date        2025-10-27
 * @details     Provides memory allocation and tracking capabilities with custom allocators
 * @copyright   Copyright (c) 2025
 * @note
 * sdk:
 * platform:
 * project:     LightAP
 * @version
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2023/07/16  <td>1.0      <td>ddkv587         <td>init version
 * <tr><td>2025/10/27  <td>1.1      <td>ddkv587         <td>update header format
 * </table>
 */
#ifndef LAP_CORE_MEMORY_HPP
#define LAP_CORE_MEMORY_HPP

#include <new>
#include <pthread.h>
#include <cstddef>
#include "CTypedef.hpp"
#include "CString.hpp"
// Avoid STL mutex in Memory; use C-style pthread mutexes to prevent allocations

// Global operator new/delete overloads
void* operator new(std::size_t size);
void* operator new[](std::size_t size);
void operator delete(void* ptr) noexcept;
void operator delete[](void* ptr) noexcept;

namespace lap
{
namespace core
{
    // Simple C-style lock guard for pthread_mutex_t (no STL, no allocation)
    struct CMutexGuard {
        explicit CMutexGuard(pthread_mutex_t* m) noexcept : mtx(m) { if (mtx) ::pthread_mutex_lock(mtx); }
        ~CMutexGuard() noexcept { if (mtx) ::pthread_mutex_unlock(mtx); }
        CMutexGuard(const CMutexGuard&) = delete;
        CMutexGuard& operator=(const CMutexGuard&) = delete;
        pthread_mutex_t* mtx;
    };
    // Define magic type based on pointer size
    #if defined(__LP64__) || defined(_WIN64) || (defined(__WORDSIZE) && __WORDSIZE == 64)
        // 64-bit system
        using MagicType = UInt64;
        #define MAGIC_XOR_VALUE 0x5A5A5A5A5A5A5A5AULL  // Base constant for backward compatibility
    #else
        // 32-bit system
        using MagicType = UInt32;
        #define MAGIC_XOR_VALUE 0x5A5A5A5AU  // Base constant for backward compatibility
    #endif

    // Forward declaration for MemManager
    class MemManager;

    // Use runtime-generated XOR mask for enhanced security
    #define MAKE_UNIT_NODE_MAGIC(pUnitNode) \
        (reinterpret_cast<uintptr_t>(pUnitNode) ^ lap::core::MemManager::getRuntimeXorMask())

    using PCREATEOBJCALLBACK = void* (*)();

    class MemAllocator {
    public:
        struct MemoryPoolState {
            UInt32 unitAvailableSize;
            UInt32 maxCount;
            UInt32 currentCount;
            UInt32 freeCount;
            UInt32 memoryCost;
            UInt32 appendCount;
        };

        struct PoolConfig {
            UInt32 unitSize;
            UInt32 initCount;
            UInt32 maxCount;
            UInt32 appendCount;
        };

    private:
        struct tagMemPool;
        struct tagUnitNode {
            tagMemPool* pool;
            tagUnitNode* nextUnit;
            MagicType magic;
        } __attribute__((packed));
        using UnitNodePtr = tagUnitNode*;

        struct tagPoolBlock {
            UInt32 blockSize;
            UInt32 unitCount;
            UInt32 usedCursor;
            tagPoolBlock* nextBlock;
        } __attribute__((packed));
        using PoolBlockPtr = tagPoolBlock*;

        struct tagMemPool {
            UInt32 unitChunkSize;
            UInt32 unitAvailableSize;
            UInt32 initCount;
            UInt32 maxCount;
            UInt32 appendCount;
            UInt32 currentCount;
            PoolBlockPtr firstBlock;
            UnitNodePtr freeList;
        } __attribute__((packed));
        using MemoryPoolPtr = tagMemPool*;

    public:
        MemAllocator();
        virtual ~MemAllocator();

        void initialize(UInt32 alignByte, UInt32 maxPoolCount);
        Bool createPool(UInt32 unitSize, UInt32 initCount, UInt32 maxCount, UInt32 appendCount);
        UInt32 getPoolCount();
        Bool getPoolState(UInt32 index, MemoryPoolState& poolState);

        void* malloc(Size size);
        void free(void* ptr);

    private:
        friend class MemManager;

        Bool addPoolBlock(tagMemPool* pool);
        tagMemPool* findFitPool(Size size);
        void freeAllPool();
        void* allocUnit(tagMemPool* pool);

    private:
        pthread_mutex_t mutex_;
        // Map from unitAvailableSize -> pool. Map keeps keys ordered, which
        // allows efficient lookup of the smallest pool >= requested size via
        // lower_bound. We keep the map owned by a UniqueHandle to allow
        // late construction/destruction.
        UniqueHandle< Map<UInt32, tagMemPool> > pools_;
        UInt32 maxPoolCount_;
        UInt32 currentPoolCount_;
        UInt32 alignMask_;
        UInt32 blockDataOffset_;
        UInt32 systemChunkHeaderSize_;

        // Compile-time layout checks for internal structures
        struct _LayoutCheck {
            static_assert(sizeof(void*) == 8 || sizeof(void*) == 4, 
                          "Unsupported pointer size");
            static_assert(sizeof(MagicType) == sizeof(uintptr_t) || sizeof(MagicType) == 4,
                          "MagicType size mismatch");
            
            // Verify packed structures have expected sizes
            // tagUnitNode: 2 pointers + 1 magic (packed, no padding)
            static_assert(sizeof(tagUnitNode) == (sizeof(void*)*2 + sizeof(MagicType)),
                          "tagUnitNode: packed attribute not working as expected");
            
            // tagPoolBlock: 3 UInt32 + 1 pointer (packed, may not align on 64-bit)
            // This is intentional - internal metadata, not user data
            static_assert(sizeof(tagPoolBlock) <= (sizeof(UInt32)*3 + sizeof(void*) + 4),
                          "tagPoolBlock: unexpected padding in packed structure");
            
            // tagMemPool: 6 UInt32 + 2 pointers (packed)
            static_assert(sizeof(tagMemPool) == (sizeof(UInt32)*6 + sizeof(void*)*2),
                          "tagMemPool: packed attribute not working as expected");
        };
    };

    class MemChecker {
    public:
        #define MEM_CONFIG_FILE     "mem_config.json"
        static const Int32 SIZE_INFO_MAX_COUNT = 151;
        static const UInt32 MAX_CLASSES = 4096;

    private:
        enum class EBlockStatus {
            Ok = 0,
            Freed = 1,
            HeaderError = 2,
            TailError = 3
        };

        enum class ELinkStatus {
            Ok = 0,
            HasBlockError = 1,
            LinkCrashed = 2
        };

        struct tagBlockHeader {
            MagicType magic;
            tagBlockHeader* next;
            tagBlockHeader* prev;
            Size size;
            UInt32 classId;
            UInt32 threadId;
            UInt32 allocTag; // 1: allocated by MemAllocator, 0: system new/delete
        } __attribute__((packed));
        using BlockHeaderPtr = tagBlockHeader*;

        struct BlockStat {
            Size beginSize;
            Size endSize;
            Int64 allocTimes;
            UInt32 currentCount;
            Size currentSize;
            UInt32 peakCount;
            Size peakSize;

            BlockStat()
                : beginSize(0), endSize(0), allocTimes(0), currentCount(0),
                currentSize(0), peakCount(0), peakSize(0) {}
        };

        struct ThreadSize {
            UInt32 threadId;
            Size size;
        };

        struct ClassStat {
            UInt32 instanceCount;
            Size totalSize;

            ClassStat() : instanceCount(0), totalSize(0) {}
        };
        using MapClassStat = Map<String, ClassStat>;
        using MapThreadStat = Map<UInt32, MapClassStat>;

        // Note: Removed unused ClassLeakInfo (avoids std::tuple and aggregation overhead)

        // Note: Legacy AutoLocker removed. Use CMutexGuard on syncMutex_ or explicit
        // pthread mutex calls where needed. No external lock/unlock API is exposed.

    public:
        static UInt32 getBlockExtSize();

        MemChecker();
        virtual ~MemChecker();

        void initialize(Bool compactSizeRange, MemAllocator* memAllocator = nullptr);
        void* malloc(Size size, UInt32 classId = 0);
        void free(void* ptr);
        Int32 checkPtr(void* ptr, const Char* hint = nullptr);

        UInt32 registerClassName(const Char* className);
        void registerThreadName(UInt32 threadId, const String& threadName);
        void setReportFile(const String& reportFile);
        Bool outputState(UInt32 gpuMemorySize = 0);

        Size getCurrentAllocSize() noexcept { return blockStatAll_.currentSize; }
        UInt32 getCurrentAllocCount() noexcept { return blockStatAll_.currentCount; }

        UInt32 getThreadCount();
        UInt32 getThreadID(UInt32 index);
        Size getThreadSize(UInt32 index);

    private:
    // Removed legacy lock/unlock APIs (redundant). Internal code uses CMutexGuard
    // or direct pthread_mutex_* calls on syncMutex_.

        void* hookMalloc(Size size, UInt32 classId);
        void hookFree(void* ptr);

        EBlockStatus checkBlock(BlockHeaderPtr header);
        ELinkStatus checkAllBlock(UInt32& errorBlockCount);
        void linkBlock(BlockHeaderPtr header);
        void unlinkBlock(BlockHeaderPtr header);

        Bool outputStateToLogger(UInt32 gpuMemorySize);
        void reportMemoryLeaks();

        void initSizeRange();
        Int32 calcRangeIndex(Size size);
        void logAllocSize(Size size);
        void logFreedSize(Size size);

        void buildClassStat(MapThreadStat& threadStats);

    private:
        MemAllocator* memAllocator_;
        char reportFile_[256];
        UInt32 reportId_;
        BlockHeaderPtr blockList_;
        Bool compactSizeRange_;
        BlockStat blockStatAll_;
        BlockStat blockStats_[SIZE_INFO_MAX_COUNT];
        UInt32 badPtrAccessCount_;
    pthread_mutex_t syncMutex_;
        UInt32 threadCount_;
        ThreadSize threadSizes_[SIZE_INFO_MAX_COUNT];
        char classNames_[MAX_CLASSES][64];
        UInt32 classCount_;
        char threadNames_[SIZE_INFO_MAX_COUNT][64];

        // Compile-time layout checks for MemChecker structures
        struct _LayoutCheck {
            static_assert(offsetof(tagBlockHeader, next) >= sizeof(MagicType),
                          "tagBlockHeader: next offset invalid");
            static_assert(offsetof(tagBlockHeader, size) >= sizeof(MagicType),
                          "tagBlockHeader: size offset invalid");
        };
    };

    struct MemoryStats {
        Size currentAllocSize;   // current total allocated bytes from all pools
        UInt32 currentAllocCount; // current allocated block count from all pools
        Size totalPoolMemory;    // total memory cost by all pools
        UInt32 poolCount;        // number of memory pools
        UInt32 threadCount;      // number of tracked threads (from checker if available)
    };

    class MemManager {
    public:
        class IMemListener {
        public:
            virtual void onOutOfMemory(UInt32 size) = 0;
            virtual void onMemoryError() = 0;
            virtual ~IMemListener() noexcept = default;
        };

    private:
        MemManager();
        MemManager(const MemManager&) = delete;
        MemManager& operator=(const MemManager&) = delete;

    public:
        static MemManager* getInstance();

        virtual ~MemManager();

        void setListener(IMemListener* listener);

        void* malloc(Size size, const Char* className = nullptr, UInt32 classId = 0);
        void free(void* ptr);
        Int32 checkPtr(void* ptr, const Char* hint = nullptr);

        UInt32 registerClassName(const Char* className);
        Bool outputState(UInt32 gpuMemorySize = 0);

        Bool hasMemChecker() noexcept { return memChecker_ != nullptr; }

        UInt32 getCurrentAllocCount();
        Size getCurrentAllocSize();

        UInt32 getThreadCount();
        UInt32 getThreadID(UInt32 index);
        Size getThreadSize(UInt32 index);

        MemoryStats getMemoryStats();

        void registerThreadName(UInt32 threadId, const String& threadName);

        /**
         * @brief Initialize memory manager and load pool configuration
         * @details Should be called at program start to load configuration from ConfigManager
         *          Safe to call multiple times (will only initialize once)
         */
        void initialize();

        /**
         * @brief Uninitialize memory manager and save configuration
         * @details Should be called before program exits to ensure configuration is saved
         *          Must be called before ConfigManager destructs
         */
        void uninitialize();

        /**
         * @brief Get runtime XOR mask for magic generation
         * @return Runtime-generated XOR mask based on PID, timestamp, etc.
         */
        static MagicType getRuntimeXorMask() noexcept;

    private:
    Bool savePoolConfig(const String& fileName = MEM_CONFIG_FILE);
    // Load pool configs into caller-provided buffer to avoid internal heap allocations
    // Returns the number of entries written to 'out' (up to maxCount)
    size_t loadPoolConfig(MemAllocator::PoolConfig* out, size_t maxCount, const String& fileName = MEM_CONFIG_FILE);

    private:
        void generateRuntimeXorMask();

    private:
        IMemListener* listener_;
        MemAllocator* memAllocator_;
        MemChecker* memChecker_;
    pthread_mutex_t callbackMutex_;
        Bool callbackActive_;
        Bool initialized_;
        Bool checkEnabled_;
        UInt32 alignByte_;
        MagicType runtimeXorMask_;
    };

    class Memory final {
    public:
        static void* malloc(Size size, const Char* className = nullptr, UInt32 classId = 0) noexcept;
        static void free(void* ptr) noexcept;
        static Int32 checkPtr(void* ptr, const Char* hint = nullptr) noexcept;
        static UInt32 registerClassName(const Char* className) noexcept;
        static MemoryStats getMemoryStats() noexcept;
        // static Bool loadPoolConfig(const String& fileName) noexcept;
        // static Bool savePoolConfig(const String& fileName) noexcept;

    private:
        Memory() = delete;
        Memory(const Memory&) = delete;
        Memory& operator=(const Memory&) = delete;
    };

} // namespace core
} // namespace lap

#endif // LAP_CORE_MEMORY_HPP
