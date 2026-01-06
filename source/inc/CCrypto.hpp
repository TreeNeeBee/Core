/**
 * @file        CCrypto.hpp
 * @author      ddkv587 ( ddkv587@gmail.com )
 * @brief       Cryptographic utilities for checksums, hashing, and HMAC
 * @date        2025-11-11
 * @details     Provides cryptographic operations including:
 *              - CRC32 checksum calculation (table-based)
 *              - SHA256 hashing
 *              - HMAC-SHA256 authentication
 *              - Static utility methods via Crypto::Util
 *              - Instance-based HMAC with key management
 * @copyright   Copyright (c) 2025
 * @note        Uses OpenSSL 3.0+ EVP interface
 * @version     1.0
 */

#ifndef LAP_CORE_CRYPTO_HPP
#define LAP_CORE_CRYPTO_HPP

#include "CTypedef.hpp"
#include "CString.hpp"
#include "CResult.hpp"
#include "COptional.hpp"
#include "CFunction.hpp"
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <functional>

namespace lap
{
namespace core
{
    /**
     * @brief Cryptographic operations class
     * 
     * Provides both static utility methods and instance-based HMAC operations:
     * - Util::computeCrc32() - CRC32 checksum
     * - Util::computeSha256() - SHA256 hash
     * - Instance methods for HMAC-SHA256 with automatic key loading
     * 
     * @note HMAC key is automatically loaded from environment variable on construction
     *       Environment variable: HMAC_SECRET
     *       If not set, uses built-in default key (warning logged)
     */
    class Crypto final
    {
    public:
        
        // Environment variable name for HMAC secret key
        static constexpr const Char* ENV_HMAC_SECRET = "HMAC_SECRET";

        // Built-in default HMAC secret (not secure for production)
        static constexpr const Char* BUILTIN_HMAC_SECRET = 
            "LightAP-Default-HMAC-Secret-2025-DO-NOT-USE-IN-PRODUCTION";

    // Minimal length threshold for HMAC secret (override via -DHMAC_SECRET_MIN_LEN=<N>)
#ifndef HMAC_SECRET_MIN_LEN
#  define HMAC_SECRET_MIN_LEN 16
#endif
        
        // ========== Utility Class for Static Cryptographic Operations ==========
        
        /**
         * @brief Static utility methods for cryptographic operations
         * Access via Crypto::Util::xxx
         */
        class Util final
        {
        public:
            // Non-instantiable utility class
            Util() = delete;
            ~Util() = delete;
            Util(const Util&) = delete;
            Util& operator=(const Util&) = delete;
            
            /**
             * @brief Compute CRC32 checksum using table-based algorithm
             * @param data Input data buffer
             * @param size Size of data in bytes
             * @return CRC32 checksum value
             * @note Uses standard polynomial 0xEDB88320
             * @threadsafe Thread-safe - uses read-only static lookup table
             */
            static UInt32 computeCrc32(const UInt8* data, Size size) noexcept;
            
            /**
             * @brief Compute CRC32 checksum from string
             * @param str Input string
             * @return CRC32 checksum value
             * @threadsafe Thread-safe - uses read-only static lookup table
             */
            static UInt32 computeCrc32(const String& str) noexcept
            {
                return computeCrc32(reinterpret_cast<const UInt8*>(str.data()), str.size());
            }
            
            /**
             * @brief Compute SHA256 hash
             * @param data Input data buffer
             * @param size Size of data in bytes
             * @return SHA256 hash as hex string (64 chars)
             * @note Returns empty string on error
             * @threadsafe Thread-safe - creates new EVP context per call
             */
            static String computeSha256(const UInt8* data, Size size) noexcept;
            
            /**
             * @brief Compute SHA256 hash from string
             * @param str Input string
             * @return SHA256 hash as hex string (64 chars)
             * @threadsafe Thread-safe - creates new EVP context per call
             */
            static String computeSha256(const String& str) noexcept
            {
                return computeSha256(reinterpret_cast<const UInt8*>(str.data()), str.size());
            }
            
            /**
             * @brief Convert bytes to hex string
             * @param data Byte array
             * @param size Size of array
             * @return Hex string (lowercase)
             * @threadsafe Thread-safe - uses local stringstream
             */
            static String bytesToHex(const UInt8* data, Size size) noexcept;
            
            /**
             * @brief Convert hex string to bytes
             * @param hex Hex string (case insensitive)
             * @param outData Output byte vector
             * @return true on success, false on invalid hex
             * @threadsafe Thread-safe - no shared state
             */
            static Bool hexToBytes(const String& hex, Vector<UInt8>& outData) noexcept;

            /**
             * @brief Base64-encode input bytes into a string (no newlines)
             * @param data Input buffer
             * @param size Number of bytes
             * @return Base64 string (without line breaks); empty on error
             */
            static String base64Encode(const UInt8* data, Size size) noexcept;

            /**
             * @brief Base64-encode input string (no newlines)
             */
            static String base64Encode(const String& s) noexcept
            {
                return base64Encode(reinterpret_cast<const UInt8*>(s.data()), s.size());
            }

            /**
             * @brief Base64-decode string to raw bytes
             * @param input Base64 string (no newlines required)
             * @param outBytes Output buffer with decoded bytes
             * @return true on success
             */
            static Bool base64Decode(const String& input, Vector<UInt8>& outBytes) noexcept;

            /**
             * @brief Base64-decode string to std::string (binary-safe)
             * @param input Base64 string
             * @return decoded data as string; empty on error
             */
            static String base64DecodeToString(const String& input) noexcept;
            
        private:
            // CRC32 lookup table (initialized at first use)
            static const UInt32* getCrc32Table() noexcept;
        };
        
        // ========== Instance-Based HMAC Operations ==========
        
        /**
         * @brief Default constructor - automatically loads HMAC key from environment
         * @note Loads key from HMAC_SECRET environment variable
         *       If not set, uses built-in default key with warning
         * @threadsafe Not thread-safe during construction - do not share across threads
         */
        Crypto() noexcept;
        
        /**
         * @brief Constructor with explicit HMAC key (for testing/development)
         * @param key HMAC secret key
         * @note This bypasses environment variable loading
         * @threadsafe Not thread-safe during construction - do not share across threads
         */
        explicit Crypto(const String& key) noexcept;
        
        /**
         * @brief Check if HMAC key is set
         * @return true if key is set
         * @threadsafe Thread-safe - reads immutable state after construction
         */
        Bool hasKey() const noexcept
        {
            return m_key.has_value();
        }
        
        /**
         * @brief Compute HMAC-SHA256
         * @param data Input data buffer
         * @param size Size of data in bytes
         * @return HMAC-SHA256 as hex string (64 chars), empty on error
         * @note Requires key to be set
         * @threadsafe Thread-safe - const method, reads immutable key
         */
        String computeHmac(const UInt8* data, Size size) const noexcept;
        
        /**
         * @brief Compute HMAC-SHA256 from string
         * @param data Input string
         * @return HMAC-SHA256 as hex string (64 chars)
         * @threadsafe Thread-safe - const method, reads immutable key
         */
        String computeHmac(const String& data) const noexcept
        {
            return computeHmac(reinterpret_cast<const UInt8*>(data.data()), data.size());
        }
        
        /**
         * @brief Verify HMAC-SHA256
         * @param data Input data buffer
         * @param size Size of data in bytes
         * @param expectedHmac Expected HMAC hex string
         * @return true if HMAC matches
         * @threadsafe Thread-safe - const method, uses constant-time comparison
         */
        Bool verifyHmac(const UInt8* data, Size size, const String& expectedHmac) const noexcept;
        
        /**
         * @brief Verify HMAC-SHA256 from string
         * @param data Input string
         * @param expectedHmac Expected HMAC hex string
         * @return true if HMAC matches
         * @threadsafe Thread-safe - const method, uses constant-time comparison
         */
        Bool verifyHmac(const String& data, const String& expectedHmac) const noexcept
        {
            return verifyHmac(reinterpret_cast<const UInt8*>(data.data()), data.size(), expectedHmac);
        }

        // ===================== Key Provider Hooks =====================
        /**
         * @brief Callback type for fetching HMAC secret from custom sources (e.g., remote KMS)
         * @return Optional secret string; empty Optional if not available
         */
    using KeyFetchCallback = Function< Optional<String>() >;

        /**
         * @brief Register a global key-fetch callback. If set, it will be tried before file/env.
         * @note Thread-unsafe for registration; register at process startup before Crypto instances are created.
         */
        static void SetKeyFetchCallback(KeyFetchCallback cb) noexcept;

        /**
         * @brief Set a key file path. If set, file content will be used when callback returns empty.
         * @note File will be read as a whole; trailing newlines/spaces will be trimmed.
         */
        static void SetKeyFilePath(const String& path) noexcept;

        /**
         * @brief Clear registered providers (callback and file path).
         */
        static void ClearKeyProviders() noexcept;
        
    private:
        /**
         * @brief Set HMAC key (private - only used internally during construction)
         * @param key HMAC secret key
         */
        void setKey(const String& key) noexcept;
        
        /**
         * @brief Load HMAC key via providers -> env -> optional fallback (private)
         * @param useBuiltinFallback If true, use built-in key as fallback (disabled in strict builds)
         */
        void loadKeyFromProviders(Bool useBuiltinFallback = true) noexcept;

        /**
         * @brief Load HMAC key from environment only (helper)
         */
        void loadKeyFromEnvOnly(Bool useBuiltinFallback) noexcept;

        /**
         * @brief Try read key from configured file path
         */
        static Optional<String> LoadKeyFromFile() noexcept;

        /**
         * @brief Normalize/validate secret; returns empty Optional if too short or empty (in strict mode aborts)
         */
        static Optional<String> ValidateSecret(const String& key) noexcept;
        
        Optional<String> m_key;  // HMAC secret key (empty if not set)

        // Global providers (shared across instances)
        static KeyFetchCallback s_keyFetchCb_;
        static Optional<String> s_keyFilePath_;
    };

} // namespace core
} // namespace lap

#endif // LAP_CORE_CRYPTO_HPP
