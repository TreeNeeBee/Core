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
#ifndef LAP_CORE_MEMORY_MANAGER_HPP
#define LAP_CORE_MEMORY_MANAGER_HPP

#include <new>
#include <pthread.h>
#include <cstddef>
#include <memory>
#include <cstdlib>
#include "CTypedef.hpp"
#include "CString.hpp"

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

    // Forward declaration for MemoryManager
    class MemoryManager;

    // Use runtime-generated XOR mask for enhanced security
    #define MAKE_UNIT_NODE_MAGIC(pUnitNode) \
        (reinterpret_cast<uintptr_t>(pUnitNode) ^ lap::core::MemoryManager::getRuntimeXorMask())

    using PCREATEOBJCALLBACK = void* (*)();

    /**
     * @brief Pool-based memory allocator for small objects
     * @details PoolAllocator manages multiple memory pools, each optimized for
     *          a specific object size. Features:
     *          - Fast O(1) allocation/deallocation from pools
     *          - Automatic pool expansion when capacity reached
     *          - Minimal fragmentation for same-sized allocations
     *          - Thread-safe operations
     * 
     * @note Used internally by MemoryManager. Not intended for direct use.
     */
    class PoolAllocator {
    public:
        /**
         * @brief Memory pool state information
         */
        struct MemoryPoolState {
            UInt32 unitAvailableSize;   ///< User-available bytes per unit
            UInt32 maxCount;            ///< Maximum units allowed
            UInt32 currentCount;        ///< Current total units allocated
            UInt32 freeCount;           ///< Current free units
            UInt32 memoryCost;          ///< Total memory consumed (bytes)
            UInt32 appendCount;         ///< Units to add when expanding
        };

        /**
         * @brief Pool configuration for initialization
         */
        struct PoolConfig {
            UInt32 unitSize;        ///< Size of each unit (bytes)
            UInt32 initCount;       ///< Initial number of units
            UInt32 maxCount;        ///< Maximum units (0 = unlimited)
            UInt32 appendCount;     ///< Units to add per expansion
        };
    
    public:
        /**
         * @brief System allocator (malloc/free) for internal use
         * @details Avoids recursion by directly calling system malloc/free
         *          Used for internal data structures like the pools map
         */
        template <typename U>
        class SystemAllocator
        {
        public:
            using value_type = U;
            using pointer = U*;
            using const_pointer = const U*;
            using size_type = ::std::size_t;
            using difference_type = ::std::ptrdiff_t;

            template <typename V>
            struct rebind { using other = SystemAllocator<V>; };

            constexpr SystemAllocator() noexcept = default;
            template <typename V>
            constexpr SystemAllocator(const SystemAllocator<V>&) noexcept {}

            [[nodiscard]] pointer allocate(size_type n) {
                if (n > (static_cast<size_type>(-1) / sizeof(U))) {
                    throw std::bad_alloc();
                }
                void* p = ::std::malloc(n * sizeof(U));
                if (!p) throw std::bad_alloc();
                return static_cast<pointer>(p);
            }

            void deallocate(pointer p, size_type /*n*/) noexcept {
                ::std::free(static_cast<void*>(p));
            }

            template <typename V, typename... Args>
            void construct(V* p, Args&&... args) {
                ::new (static_cast<void*>(p)) V(::std::forward<Args>(args)...);
            }

            template <typename V>
            void destroy(V* p) noexcept { p->~V(); }

            using propagate_on_container_move_assignment = ::std::true_type;
            using is_always_equal = ::std::true_type;

            template <typename V>
            constexpr bool operator==(const SystemAllocator<V>&) const noexcept { return true; }
            template <typename V>
            constexpr bool operator!=(const SystemAllocator<V>&) const noexcept { return false; }
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
        PoolAllocator();
        virtual ~PoolAllocator();

        /**
         * @brief Initialize the pool allocator
         * @param alignByte Memory alignment (must be power of 2)
         * @param maxPoolCount Maximum number of pools allowed
         */
        void initialize(UInt32 alignByte, UInt32 maxPoolCount);
        
        /**
         * @brief Create or merge a memory pool
         * @param unitSize Size of each unit in the pool
         * @param initCount Initial number of units to allocate
         * @param maxCount Maximum units (0 = unlimited)
         * @param appendCount Units to add when expanding
         * @return true on success, false on failure
         */
        Bool createPool(UInt32 unitSize, UInt32 initCount, UInt32 maxCount, UInt32 appendCount);
        
        /**
         * @brief Get total number of pools
         * @return Pool count
         */
        UInt32 getPoolCount();
        
        /**
         * @brief Get state of a specific pool by index
         * @param index Pool index [0, getPoolCount())
         * @param poolState Output parameter for pool state
         * @return true on success, false if index out of range
         */
        Bool getPoolState(UInt32 index, MemoryPoolState& poolState);

        /**
         * @brief Allocate memory from pools
         * @param size Size to allocate
         * @return Pointer to allocated memory, or nullptr on failure
         */
        void* malloc(Size size);
        
        /**
         * @brief Free memory back to pools
         * @param ptr Pointer to free (must have been allocated by this allocator)
         */
        void free(void* ptr);

    private:
        friend class MemoryManager;

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
        // Typedef for the pools map that uses SystemAllocator to avoid recursion
        using PoolsMap = Map<UInt32, tagMemPool, ::std::less<UInt32>, SystemAllocator< Pair<const UInt32, tagMemPool> > >;

        // Apply SystemAllocator to the map storing pools
        UniqueHandle< PoolsMap > pools_;
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

    class MemoryTracker {
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
            UInt32 allocTag; // 1: allocated by PoolAllocator, 0: system new/delete
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

        MemoryTracker();
        virtual ~MemoryTracker();

        void initialize(Bool compactSizeRange, PoolAllocator* poolAllocator = nullptr);
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
        PoolAllocator* poolAllocator_;
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

        // Compile-time layout checks for MemoryTracker structures
        struct _LayoutCheck {
            static_assert(offsetof(tagBlockHeader, next) >= sizeof(MagicType),
                          "tagBlockHeader: next offset invalid");
            static_assert(offsetof(tagBlockHeader, size) >= sizeof(MagicType),
                          "tagBlockHeader: size offset invalid");
        };
    };

    /**
     * @brief Memory statistics structure
     * @details Provides comprehensive statistics about memory usage and pools
     */
    struct MemoryStats {
        Size currentAllocSize;      ///< Current total allocated bytes (user data)
        UInt32 currentAllocCount;   ///< Current number of allocated blocks
        Size totalPoolMemory;       ///< Total memory consumed by pools (including overhead)
        UInt32 poolCount;           ///< Number of active memory pools
        UInt32 threadCount;         ///< Number of tracked threads (if MemoryTracker enabled)
    };

    /**
     * @brief Central memory management system (Singleton)
     * @details MemoryManager provides:
     *          - Pool-based allocation for small objects (≤ 1024 bytes)
     *          - System fallback for large allocations (> 1024 bytes)
     *          - Optional memory tracking and leak detection (MemoryTracker)
     *          - Thread-safe operations with minimal lock contention
     *          - Configuration via ConfigManager
     * 
     * @note This is a singleton class. Access via getInstance().
     * 
     * @usage
     * @code
     * // Initialization (optional, auto-initialized on first use)
     * MemoryManager::getInstance()->initialize();
     * 
     * // Allocate memory (usually through global operator new or Memory::malloc)
     * void* ptr = MemoryManager::getInstance()->malloc(size);
     * 
     * // Free memory
     * MemoryManager::getInstance()->free(ptr);
     * 
     * // Get statistics
     * auto stats = MemoryManager::getInstance()->getMemoryStats();
     * 
     * // Cleanup before exit
     * MemoryManager::getInstance()->uninitialize();
     * @endcode
     */
    class MemoryManager {
    public:
        /**
         * @brief Interface for memory event callbacks
         * @details Implement this interface to receive notifications about
         *          memory events such as OOM or memory errors
         */
        class IMemListener {
        public:
            /**
             * @brief Called when allocation fails due to insufficient memory
             * @param size Size of the failed allocation request
             */
            virtual void onOutOfMemory(UInt32 size) = 0;
            
            /**
             * @brief Called when memory corruption or invalid operation is detected
             */
            virtual void onMemoryError() = 0;
            
            virtual ~IMemListener() noexcept = default;
        };

    private:
        MemoryManager();
        MemoryManager(const MemoryManager&) = delete;
        MemoryManager& operator=(const MemoryManager&) = delete;
        MemoryManager(MemoryManager&&) = delete;
        MemoryManager& operator=(MemoryManager&&) = delete;

    public:
        /**
         * @brief Get the singleton instance
         * @return Pointer to MemoryManager singleton (never nullptr)
         */
        static MemoryManager* getInstance();

        virtual ~MemoryManager();

        /**
         * @brief Set memory event listener
         * @param listener Pointer to IMemListener implementation (can be nullptr to clear)
         */
        void setListener(IMemListener* listener);

        /**
         * @brief Allocate memory with optional tracking
         * @param size Size in bytes to allocate
         * @param className Optional class name for tracking (max 63 chars)
         * @param classId Optional class ID for tracking
         * @return Pointer to allocated memory, or nullptr on failure
         */
        void* malloc(Size size, const Char* className = nullptr, UInt32 classId = 0);
        
        /**
         * @brief Free previously allocated memory
         * @param ptr Pointer to memory to free (nullptr-safe)
         */
        void free(void* ptr);
        
        /**
         * @brief Validate pointer (debug builds with MemoryTracker enabled)
         * @param ptr Pointer to validate
         * @param hint Optional hint string for error messages
         * @return 0 if valid, non-zero error code otherwise
         */
        Int32 checkPtr(void* ptr, const Char* hint = nullptr);

        /**
         * @brief Register a class name for memory tracking
         * @param className Class name string (max 63 characters)
         * @return Unique class ID for use in malloc()
         */
        UInt32 registerClassName(const Char* className);
        
        /**
         * @brief Output memory state report
         * @param gpuMemorySize Additional GPU memory to include in report (bytes)
         * @return true on success, false on failure
         */
        Bool outputState(UInt32 gpuMemorySize = 0);

        /**
         * @brief Check if memory tracking is enabled
         * @return true if MemoryTracker is active
         */
        Bool hasMemoryTracker() noexcept { 
            return memoryTracker_ != nullptr; 
        }

        /**
         * @brief Get current allocation count
         * @return Number of currently allocated blocks
         */
        UInt32 getCurrentAllocCount();
        
        /**
         * @brief Get current allocated size
         * @return Total bytes currently allocated (user data only)
         */
        Size getCurrentAllocSize();

        /**
         * @brief Get tracked thread count
         * @return Number of threads with allocations (requires MemoryTracker)
         */
        UInt32 getThreadCount();
        
        /**
         * @brief Get thread ID by index
         * @param index Thread index [0, getThreadCount())
         * @return Thread ID
         */
        UInt32 getThreadID(UInt32 index);
        
        /**
         * @brief Get allocated size for a specific thread
         * @param index Thread index [0, getThreadCount())
         * @return Total bytes allocated by thread
         */
        Size getThreadSize(UInt32 index);

        /**
         * @brief Get comprehensive memory statistics
         * @return MemoryStats structure with current state
         */
        MemoryStats getMemoryStats();

        /**
         * @brief Register a custom name for a thread ID
         * @param threadId Thread ID to name
         * @param threadName Human-readable thread name
         */
        void registerThreadName(UInt32 threadId, const String& threadName);

        /**
         * @brief Initialize memory manager and load pool configuration
         * @details Should be called at program start to load configuration from ConfigManager.
         *          Safe to call multiple times (will only initialize once).
         *          If not called explicitly, will auto-initialize on first malloc().
         */
        void initialize();

        /**
         * @brief Uninitialize memory manager and save configuration
         * @details Should be called before program exits to ensure configuration is saved.
         *          Must be called before ConfigManager destructs.
         */
        void uninitialize();

        /**
         * @brief Get runtime XOR mask for magic value generation
         * @return Runtime-generated XOR mask (unique per process)
         * @note Used internally for enhanced security of memory headers
         */
        static MagicType getRuntimeXorMask() noexcept;

    private:
        Bool savePoolConfig(const String& fileName = MEM_CONFIG_FILE);
        size_t loadPoolConfig(PoolAllocator::PoolConfig* out, size_t maxCount, const String& fileName = MEM_CONFIG_FILE);
        void generateRuntimeXorMask();

    private:
        IMemListener* listener_;
        PoolAllocator* poolAllocator_;
        MemoryTracker* memoryTracker_;
        pthread_mutex_t callbackMutex_;
        Bool callbackActive_;
        Bool initialized_;
        Bool checkEnabled_;
        UInt32 alignByte_;
        MagicType runtimeXorMask_;
    };
} // namespace core
} // namespace lap

#endif // LAP_CORE_MEMORY_MANAGER_HPP
