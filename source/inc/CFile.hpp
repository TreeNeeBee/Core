/**
 * @file        CFile.hpp
 * @author      ddkv587 ( ddkv587@gmail.com )
 * @brief       File utilities for file operations
 * @date        2025-10-29
 * @details     A helper class for file operations, do not instantiate
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
 * <tr><td>2025/10/29  <td>2.0      <td>ddkv587         <td>migrate to std::filesystem (C++17)
 * </table>
 */

#ifndef LAP_CORE_FILE_HPP
#define LAP_CORE_FILE_HPP

// Use std::filesystem (C++17) or fallback to boost::filesystem
#if __cplusplus >= 201703L && __has_include(<filesystem>)
    #include <filesystem>
    namespace fs = std::filesystem;
#else
    #include <boost/filesystem.hpp>
    #include <boost/system/error_code.hpp>
    namespace fs = boost::filesystem;
#endif

#include <boost/crc.hpp>
#include <fstream>
#include <string>
#include <regex>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#include "CTypedef.hpp"
#include "CString.hpp"
#include "CMemory.hpp"

namespace lap
{
namespace core
{
    /**
     * @brief File operations class supporting both static utilities and instance-based I/O
     * 
     * Static methods: File system operations (exists, remove, copy, etc.)
     * Instance methods: Low-level file descriptor I/O with RAII
     */
    class File final
    {
    public:
        IMP_OPERATOR_NEW(File)
        
        // ========== Utility Class for Static File Operations ==========
        
        /**
         * @brief Static utility methods for file system operations
         * Access via File::Util::xxx
         */
        class Util final
        {
        public:
            // Non-instantiable utility class
            Util() = delete;
            ~Util() = delete;
            Util(const Util&) = delete;
            Util& operator=(const Util&) = delete;
            
            // Check if a file exists
            static Bool exists(const String& filePath) noexcept
            {
#if __cplusplus >= 201703L && __has_include(<filesystem>)
                std::error_code ec;
#else
                boost::system::error_code ec;
#endif
                return fs::exists(filePath, ec) && !ec;
            }

            // Delete a file
            static Bool remove(const String& filePath) noexcept
            {
#if __cplusplus >= 201703L && __has_include(<filesystem>)
                std::error_code ec;
#else
                boost::system::error_code ec;
#endif
                return fs::remove(filePath, ec) && !ec;
            }

            // Copy a file
            static Bool copy(const String& source, const String& destination) noexcept
            {
#if __cplusplus >= 201703L && __has_include(<filesystem>)
                std::error_code ec;
                fs::copy_file(source, destination, fs::copy_options::overwrite_existing, ec);
#else
                boost::system::error_code ec;
                fs::copy_file(source, destination, fs::copy_option::overwrite_if_exists, ec);
#endif
                return !ec;
            }

            // Move a file
            static Bool move(const String& source, const String& destination) noexcept
            {
#if __cplusplus >= 201703L && __has_include(<filesystem>)
                std::error_code ec;
#else
                boost::system::error_code ec;
#endif
                fs::rename(source, destination, ec);
                return !ec;
            }

            // Create an empty file
            static Bool create(const String& filePath) noexcept
            {
                try {
                    std::ofstream ofs(filePath);
                    return ofs.is_open();
                } catch (const std::exception& e) {
                    return false;
                }
            }

            // Get the size of a file
            static std::size_t size(const String& filePath) noexcept
            {
#if __cplusplus >= 201703L && __has_include(<filesystem>)
                std::error_code ec;
#else
                boost::system::error_code ec;
#endif
                auto fileSize = fs::file_size(filePath, ec);
                return ec ? 0 : fileSize;
            }

            // Remove file extension from path
            static StringView removeExtension(StringView strPath) noexcept
            {
                Size point(strPath.find_last_of('.'));
                return point > 0 && point != StringView::npos ? strPath.substr(0, point) : strPath;
            }

            // Check if file path is valid
            static Bool checkValid(StringView strFile) noexcept
            {
                ::std::regex regFile("([\\w\\.\\/]+)$");
                return ::std::regex_match(strFile.data(), regFile);
            }

            // Calculate CRC32 checksum of a file (returns 0 on error)
            static UInt32 crc(StringView strFile, Bool isHeaderOnly = true) noexcept
            {
                boost::crc_32_type crc;
                Vector<Char> buffer(4096);
                std::ifstream stream(strFile.data(), std::ios::in | std::ios::binary);
                
                if (!stream) {
                    return 0;  // File open error
                }

                do {
                    stream.read(&buffer[0], buffer.size());
                    Size byte_cnt = static_cast<Size>(stream.gcount());
                    crc.process_bytes(&buffer[0], byte_cnt);
                } while (stream && !isHeaderOnly);

                if (stream.eof() || isHeaderOnly) {
                    return crc.checksum();
                } else {
                    return 0;  // Integrity error
                }
            }

            // Delete file with validation
            static Bool deleteFile(StringView strFile) noexcept
            {
                // Treat as success if invalid path
                if (!checkValid(strFile)) {
                    return true;
                }
                return remove(String(strFile));
            }

            // Rename/move file (wrapper for POSIX rename)
            static Bool rename(const String& oldPath, const String& newPath) noexcept
            {
                return ::rename(oldPath.c_str(), newPath.c_str()) == 0;
            }

            // Get file status
            static Bool stat(const String& path, struct stat* st) noexcept
            {
                return ::stat(path.c_str(), st) == 0;
            }
            
            /**
             * @brief Read entire file content as binary data
             * @param filePath Path to the file to read
             * @param outData Vector to store the read data (will be resized)
             * @return true on success, false on failure
             */
            static Bool ReadBinary(const String& filePath, Vector<UInt8>& outData) noexcept
            {
                try {
                    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
                    if (!file.is_open()) {
                        return false;
                    }
                    
                    std::streamsize fileSize = file.tellg();
                    if (fileSize < 0) {
                        return false;
                    }
                    
                    file.seekg(0, std::ios::beg);
                    outData.resize(static_cast<Size>(fileSize));
                    
                    if (!file.read(reinterpret_cast<char*>(outData.data()), fileSize)) {
                        return false;
                    }
                    
                    return true;
                } catch (...) {
                    return false;
                }
            }
        }; // class Util
        
        // ========== Instance-Based File Descriptor I/O ==========
        
        // Open modes for instance operations
        enum class OpenMode : UInt32
        {
            ReadOnly    = 0x0001,   // O_RDONLY
            WriteOnly   = 0x0002,   // O_WRONLY
            ReadWrite   = 0x0004,   // O_RDWR
            Create      = 0x0008,   // O_CREAT
            Append      = 0x0010,   // O_APPEND
            Truncate    = 0x0020,   // O_TRUNC
            Exclusive   = 0x0040,   // O_EXCL
            CloseOnExec = 0x0080,   // O_CLOEXEC
            Sync        = 0x0100,   // O_SYNC - synchronous writes (no OS cache)
            Direct      = 0x0200    // O_DIRECT - direct I/O bypassing page cache
        };

        // Default constructor (no file opened)
        File() noexcept : m_fd(-1) {}
        
        // Constructor with file open
        explicit File(const String& path, UInt32 flags, UInt32 mode = 0644) noexcept
            : m_fd(-1)
        {
            open(path, flags, mode);
        }

        // Destructor - auto-close file
        ~File() noexcept
        {
            close();
        }

        // Non-copyable
        File(const File&) = delete;
        File& operator=(const File&) = delete;

        // Movable
        File(File&& other) noexcept : m_fd(other.m_fd)
        {
            other.m_fd = -1;
        }

        File& operator=(File&& other) noexcept
        {
            if (this != &other) {
                close();
                m_fd = other.m_fd;
                other.m_fd = -1;
            }
            return *this;
        }

        // Open file with flags
        Bool open(const String& path, UInt32 flags, UInt32 mode = 0644) noexcept
        {
            close();  // Close existing fd if any

            int sysFlags = convertFlags(flags);
            m_fd = ::open(path.c_str(), sysFlags, mode);
            return m_fd >= 0;
        }

        // Close file
        void close() noexcept
        {
            if (m_fd >= 0) {
                ::close(m_fd);
                m_fd = -1;
            }
        }

        // Write data (returns bytes written, -1 on error)
        Int64 write(const void* buf, Size len) noexcept
        {
            if (m_fd < 0) return -1;
            return ::write(m_fd, buf, len);
        }

        // Read data (returns bytes read, -1 on error)
        Int64 read(void* buf, Size len) noexcept
        {
            if (m_fd < 0) return -1;
            return ::read(m_fd, buf, len);
        }

        // Sync to disk
        Bool fsync() noexcept
        {
            if (m_fd < 0) return false;
            return ::fsync(m_fd) == 0;
        }

        // Get file status
        Bool fstat(struct stat* st) noexcept
        {
            if (m_fd < 0) return false;
            return ::fstat(m_fd, st) == 0;
        }

        // Advisory file locking (blocking)
        Bool lock(Bool exclusive = true) noexcept
        {
            if (m_fd < 0) return false;
            int operation = exclusive ? LOCK_EX : LOCK_SH;
            return ::flock(m_fd, operation) == 0;
        }

        // Advisory file locking (non-blocking)
        Bool tryLock(Bool exclusive = true) noexcept
        {
            if (m_fd < 0) return false;
            int operation = exclusive ? (LOCK_EX | LOCK_NB) : (LOCK_SH | LOCK_NB);
            return ::flock(m_fd, operation) == 0;
        }

        // Unlock file
        Bool unlock() noexcept
        {
            if (m_fd < 0) return false;
            return ::flock(m_fd, LOCK_UN) == 0;
        }

        // Check if file is open
        Bool isOpen() const noexcept { return m_fd >= 0; }
        
        // Get raw file descriptor
        int get() const noexcept { return m_fd; }

    private:
        // Convert OpenMode flags to system flags
        static int convertFlags(UInt32 flags) noexcept
        {
            int result = 0;
            if (flags & static_cast<UInt32>(OpenMode::ReadOnly))    result |= O_RDONLY;
            if (flags & static_cast<UInt32>(OpenMode::WriteOnly))   result |= O_WRONLY;
            if (flags & static_cast<UInt32>(OpenMode::ReadWrite))   result |= O_RDWR;
            if (flags & static_cast<UInt32>(OpenMode::Create))      result |= O_CREAT;
            if (flags & static_cast<UInt32>(OpenMode::Append))      result |= O_APPEND;
            if (flags & static_cast<UInt32>(OpenMode::Truncate))    result |= O_TRUNC;
            if (flags & static_cast<UInt32>(OpenMode::Exclusive))   result |= O_EXCL;
            if (flags & static_cast<UInt32>(OpenMode::CloseOnExec)) result |= O_CLOEXEC;
            if (flags & static_cast<UInt32>(OpenMode::Sync))        result |= O_SYNC;
#ifdef O_DIRECT
            if (flags & static_cast<UInt32>(OpenMode::Direct))      result |= O_DIRECT;
#endif
            return result;
        }

        int m_fd;  // File descriptor (-1 = not open)
    };

    // Bitwise operators for OpenMode
    inline File::OpenMode operator|(File::OpenMode a, File::OpenMode b) noexcept
    {
        return static_cast<File::OpenMode>(
            static_cast<core::UInt32>(a) | static_cast<core::UInt32>(b)
        );
    }

} // namespace core
} // namespace lap

#endif