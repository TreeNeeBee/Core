/**
 * @file        CCrypto.cpp
 * @author      ddkv587 ( ddkv587@gmail.com )
 * @brief       Implementation of cryptographic utilities
 * @date        2025-11-11
 */

#include "CCrypto.hpp"
#include "CViolation.hpp"
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <cstdlib>

namespace lap
{
namespace core
{
    // Initialize static provider storage
    Crypto::KeyFetchCallback Crypto::s_keyFetchCb_ = nullptr;
    Optional<String> Crypto::s_keyFilePath_;
    // ========================================================================
    // Constructor Implementation
    // ========================================================================
    
    Crypto::Crypto() noexcept
    {
        loadKeyFromProviders(true);  // Try providers -> env with built-in fallback
    }
    
    Crypto::Crypto(const String& key) noexcept
    {
        if (!key.empty()) {
            m_key = key;
        }
    }
    
    void Crypto::setKey(const String& key) noexcept
    {
        if (!key.empty()) {
            m_key = key;
        } else {
            m_key.reset();
        }
    }
    
    void Crypto::loadKeyFromEnvOnly(Bool useBuiltinFallback) noexcept
    {
        const Char* secret = std::getenv(ENV_HMAC_SECRET);

#ifdef REQUIRE_HMAC_SECRET_ENV
        // Strict mode: ENV variable must exist and meet length threshold
        if (!secret || std::strlen(secret) < HMAC_SECRET_MIN_LEN) {
            LAP_RAISE_VIOLATION(
                ViolationType::kConfigurationMissing,
                "HMAC_SECRET missing or too short for strict mode"
            );
        }
        setKey(secret);
        INNER_CORE_LOG("[Crypto] Loaded HMAC secret from environment variable (strict mode)\n");
#else
        if (secret && std::strlen(secret) > 0) {
            Optional<String> v = ValidateSecret(String(secret));
            if (v.has_value()) {
                m_key = v.value();
                INNER_CORE_LOG("[Crypto] Loaded HMAC secret from %s environment variable\n", ENV_HMAC_SECRET);
                return;
            }
        }
        if (useBuiltinFallback) {
            m_key = String(BUILTIN_HMAC_SECRET);
            INNER_CORE_LOG("[Crypto] WARNING: %s not set or too short; using built-in default (NOT for production)\n", ENV_HMAC_SECRET);
        } else {
            m_key.reset();
            INNER_CORE_LOG("[Crypto] WARNING: %s not set and fallback disabled\n", ENV_HMAC_SECRET);
        }
#endif
    }

    void Crypto::loadKeyFromProviders(Bool useBuiltinFallback) noexcept
    {
        // 1) Try custom callback provider
        if (s_keyFetchCb_) {
            try {
                Optional<String> s = s_keyFetchCb_();
                if (s.has_value()) {
                    Optional<String> v = ValidateSecret(s.value());
                    if (v.has_value()) { setKey(v.value()); return; }
#ifdef REQUIRE_HMAC_SECRET_ENV
                    // In strict builds, a provided but invalid secret is a violation
                    LAP_RAISE_VIOLATION(
                        ViolationType::kConfigurationMissing,
                        "Custom key provider returned empty/short secret in strict mode"
                    );
#endif
                }
            } catch (...) {
                INNER_CORE_LOG("[Crypto] KeyFetchCallback threw exception; ignoring\n");
            }
        }

        // 2) Try file path if configured
        if (s_keyFilePath_.has_value()) {
            Optional<String> fs = LoadKeyFromFile();
            if (fs.has_value()) {
                Optional<String> v = ValidateSecret(fs.value());
                if (v.has_value()) { setKey(v.value()); return; }
#ifdef REQUIRE_HMAC_SECRET_ENV
                LAP_RAISE_VIOLATION(
                    ViolationType::kConfigurationMissing,
                    "Key file provided but secret empty/short in strict mode"
                );
#endif
            }
        }

        // 3) Fallback to environment
        loadKeyFromEnvOnly(useBuiltinFallback);
    }
    
    // ========================================================================
    // CRC32 Implementation
    // ========================================================================
    
    // CRC32 lookup table (standard polynomial 0xEDB88320)
    static const UInt32 crc32_table[256] = {
        0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f,
        0xe963a535, 0x9e6495a3, 0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
        0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91, 0x1db71064, 0x6ab020f2,
        0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
        0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9,
        0xfa0f3d63, 0x8d080df5, 0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
        0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b, 0x35b5a8fa, 0x42b2986c,
        0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
        0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423,
        0xcfba9599, 0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
        0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d, 0x76dc4190, 0x01db7106,
        0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
        0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d,
        0x91646c97, 0xe6635c01, 0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
        0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950,
        0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
        0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7,
        0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
        0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9, 0x5005713c, 0x270241aa,
        0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
        0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81,
        0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
        0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683, 0xe3630b12, 0x94643b84,
        0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
        0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb,
        0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
        0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5, 0xd6d6a3e8, 0xa1d1937e,
        0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
        0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55,
        0x316e8eef, 0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
        0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe, 0xb2bd0b28,
        0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
        0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f,
        0x72076785, 0x05005713, 0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
        0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242,
        0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
        0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69,
        0x616bffd3, 0x166ccf45, 0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
        0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc,
        0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
        0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693,
        0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
        0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
    };
    
    const UInt32* Crypto::Util::getCrc32Table() noexcept
    {
        return crc32_table;
    }
    
    UInt32 Crypto::Util::computeCrc32(const UInt8* data, Size size) noexcept
    {
        if (!data || size == 0) {
            return 0;
        }
        
        const UInt32* table = getCrc32Table();
        UInt32 crc = 0xFFFFFFFF;
        
        for (Size i = 0; i < size; ++i) {
            crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
        }
        
        return crc ^ 0xFFFFFFFF;
    }
    
    // ========================================================================
    // SHA256 Implementation
    // ========================================================================
    
    String Crypto::Util::computeSha256(const UInt8* data, Size size) noexcept
    {
        if (!data || size == 0) {
            return String();
        }
        
        EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
        if (!mdctx) {
            return String();
        }
        
        UInt8 hash[32];  // SHA256 produces 32 bytes
        unsigned int hashLen = 0;
        
        if (EVP_DigestInit_ex(mdctx, EVP_sha256(), nullptr) != 1) {
            EVP_MD_CTX_free(mdctx);
            return String();
        }
        
        if (EVP_DigestUpdate(mdctx, data, size) != 1) {
            EVP_MD_CTX_free(mdctx);
            return String();
        }
        
        if (EVP_DigestFinal_ex(mdctx, hash, &hashLen) != 1) {
            EVP_MD_CTX_free(mdctx);
            return String();
        }
        
        EVP_MD_CTX_free(mdctx);
        return bytesToHex(hash, hashLen);
    }
    
    // ========================================================================
    // Hex Conversion
    // ========================================================================
    
    String Crypto::Util::bytesToHex(const UInt8* data, Size size) noexcept
    {
        if (!data || size == 0) {
            return String();
        }
        
        std::ostringstream oss;
        oss << std::hex << std::setfill('0');
        
        for (Size i = 0; i < size; ++i) {
            oss << std::setw(2) << static_cast<int>(data[i]);
        }
        
        return oss.str();
    }
    
    Bool Crypto::Util::hexToBytes(const String& hex, Vector<UInt8>& outData) noexcept
    {
        if (hex.length() % 2 != 0) {
            return false;
        }
        
        outData.clear();
        outData.reserve(hex.length() / 2);
        
        for (Size i = 0; i < hex.length(); i += 2) {
            String byteString = hex.substr(i, 2);
            char* end = nullptr;
            UInt8 byte = static_cast<UInt8>(std::strtol(byteString.c_str(), &end, 16));
            
            if (end != byteString.c_str() + 2) {
                return false;
            }
            
            outData.push_back(byte);
        }
        
        return true;
    }
    
    // ========================================================================
    // Base64 helpers (no newlines)
    // ========================================================================

    String Crypto::Util::base64Encode(const UInt8* data, Size size) noexcept
    {
        if (!data || size == 0) return String();
        String result;
        BIO* b64 = BIO_new(BIO_f_base64());
        BIO* mem = BIO_new(BIO_s_mem());
        if (!b64 || !mem) {
            if (b64) BIO_free(b64);
            if (mem) BIO_free(mem);
            return String();
        }
        BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
        BIO* bio = BIO_push(b64, mem);
        int written = BIO_write(bio, data, static_cast<int>(size));
        if (written <= 0 || BIO_flush(bio) != 1) {
            BIO_free_all(bio);
            return String();
        }
        BUF_MEM* bufferPtr = nullptr;
        BIO_get_mem_ptr(bio, &bufferPtr);
        if (bufferPtr && bufferPtr->length > 0) {
            result.assign(bufferPtr->data, bufferPtr->length);
        }
        BIO_free_all(bio);
        return result;
    }

    Bool Crypto::Util::base64Decode(const String& input, Vector<UInt8>& outBytes) noexcept
    {
        outBytes.clear();
        if (input.empty()) return true;
        BIO* b64 = BIO_new(BIO_f_base64());
        BIO* bio = BIO_new_mem_buf(input.data(), static_cast<int>(input.size()));
        if (!b64 || !bio) {
            if (b64) BIO_free(b64);
            if (bio) BIO_free(bio);
            return false;
        }
        BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
        bio = BIO_push(b64, bio);
        // Rough upper bound
        outBytes.resize(input.size());
        int len = BIO_read(bio, outBytes.data(), static_cast<int>(outBytes.size()));
        if (len < 0) {
            BIO_free_all(bio);
            outBytes.clear();
            return false;
        }
        outBytes.resize(static_cast<size_t>(len));
        BIO_free_all(bio);
        return true;
    }

    String Crypto::Util::base64DecodeToString(const String& input) noexcept
    {
        Vector<UInt8> bytes;
        if (!base64Decode(input, bytes)) return String();
        return String(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }

    // ========================================================================
    // HMAC-SHA256 Implementation
    // ========================================================================
    
    String Crypto::computeHmac(const UInt8* data, Size size) const noexcept
    {
        if (!m_key.has_value() || !data || size == 0) {
            return String();
        }
        
        UInt8 hmac[32];  // SHA256 HMAC produces 32 bytes
        unsigned int hmacLen = 0;
        
        const String& keyRef = m_key.value();
        const UInt8* result = HMAC(
            EVP_sha256(),
            keyRef.data(), keyRef.size(),
            data, size,
            hmac, &hmacLen
        );
        
        if (!result || hmacLen != 32) {
            return String();
        }
        
        return Util::bytesToHex(hmac, hmacLen);
    }

    // ===================== Providers and utilities =====================

    void Crypto::SetKeyFetchCallback(KeyFetchCallback cb) noexcept
    {
        s_keyFetchCb_ = std::move(cb);
    }

    void Crypto::SetKeyFilePath(const String& path) noexcept
    {
        if (!path.empty()) s_keyFilePath_ = path; else s_keyFilePath_.reset();
    }

    void Crypto::ClearKeyProviders() noexcept
    {
        s_keyFetchCb_ = nullptr;
        s_keyFilePath_.reset();
    }

    Optional<String> Crypto::LoadKeyFromFile() noexcept
    {
        if (!s_keyFilePath_.has_value() || s_keyFilePath_.value().empty()) return Optional<String>();
        const String& path = s_keyFilePath_.value();
        std::ifstream ifs(path);
        if (!ifs.is_open()) {
            INNER_CORE_LOG("[Crypto] Cannot open key file: %s\n", path.c_str());
            return Optional<String>();
        }
        std::stringstream buffer; buffer << ifs.rdbuf();
        String k = buffer.str();
        // trim spaces and newlines
        while (!k.empty() && (k.back()=='\n' || k.back()=='\r' || k.back()==' ' || k.back()=='\t')) k.pop_back();
        size_t s = 0; while (s<k.size() && (k[s]=='\n'||k[s]=='\r'||k[s]==' '||k[s]=='\t')) ++s; if (s>0) k.erase(0,s);
        if (k.empty()) return Optional<String>();
        return Optional<String>(k);
    }

    Optional<String> Crypto::ValidateSecret(const String& key) noexcept
    {
        if (key.empty()) return Optional<String>();
        if (key.size() < static_cast<size_t>(HMAC_SECRET_MIN_LEN)) {
#ifdef REQUIRE_HMAC_SECRET_ENV
            LAP_RAISE_VIOLATION(
                ViolationType::kSecurityViolation,
                "HMAC secret below minimum length threshold in strict mode"
            );
#else
            INNER_CORE_LOG("[Crypto] WARNING: HMAC secret shorter than %d bytes threshold\n", HMAC_SECRET_MIN_LEN);
#endif
            if (key.size() < static_cast<size_t>(HMAC_SECRET_MIN_LEN)) {
                // In lenient mode, still accept (but warn); return it.
            }
        }
        return Optional<String>(key);
    }
    
    Bool Crypto::verifyHmac(const UInt8* data, Size size, const String& expectedHmac) const noexcept
    {
        if (!m_key.has_value()) {
            return false;
        }
        
        String computedHmac = computeHmac(data, size);
        if (computedHmac.empty()) {
            return false;
        }
        
        // Constant-time comparison to prevent timing attacks
        if (computedHmac.length() != expectedHmac.length()) {
            return false;
        }
        
        int result = 0;
        for (Size i = 0; i < computedHmac.length(); ++i) {
            result |= computedHmac[i] ^ expectedHmac[i];
        }
        
        return result == 0;
    }

} // namespace core
} // namespace lap
