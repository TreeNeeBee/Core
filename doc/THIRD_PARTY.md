# Core Module – Third-Party Dependencies

This document summarizes third-party dependencies used by the Core module, where they are used, and their purpose. It reflects both build-time (CMake) and source-level includes.

> Scope: `modules/Core` only. Test-only dependencies are marked accordingly.

## Summary Table

- Boost (filesystem, regex) — Filesystem operations and regex utilities; linked in examples/tests
- Zlib — CRC32 computation used for configuration integrity
- OpenSSL (SSL, Crypto) — HMAC-SHA256 and Base64 helpers for configuration authentication/encoding
- nlohmann/json — JSON parsing/serialization for configuration and memory module configs
- Threads::Threads (pthread) — concurrency primitives used by tests/examples
- GoogleTest (test-only) — unit test framework
- Boost (optional, variant, beast::span) — Fallbacks used by our wrapper headers when C++17/20 features are unavailable

---

## Details

 
### Boost (filesystem, regex)
 
- Link targets: `Boost::filesystem`, `Boost::regex`
- Declared in: `modules/Core/CMakeLists.txt`
  - `set(MODULE_EXTERNAL_LIB Boost::filesystem Boost::regex ...)`
  - Linked to examples/tests (e.g., `global_operator_test`, `config_example_v4`, etc.)
- Source references:
  - `source/inc/CFile.hpp` and `source/inc/CPath.hpp` use `std::filesystem` with fallback notes to `boost::filesystem`.
  - Regex usages in `CFile.hpp` (e.g., `::std::regex` mentions; historically used Boost as fallback).
- Purpose: filesystem operations (path existence, copy, size) and regex utilities.

### Zlib
 
- Link target: `ZLIB::ZLIB`
- Declared in: `modules/Core/CMakeLists.txt`
- Source references:
  - `source/src/CConfig.cpp` includes `<zlib.h>` and uses `crc32()` in `computeCrc32()`.
- Purpose: Fast CRC32 checksum for config integrity.

### OpenSSL (SSL, Crypto)
 
- Link targets: `OpenSSL::SSL`, `OpenSSL::Crypto`
- Declared in: `modules/Core/CMakeLists.txt`
- Source references:
  - `source/src/CConfig.cpp`: `<openssl/hmac.h>`, `<openssl/evp.h>`, `<openssl/bio.h>`, `<openssl/buffer.h>`, `<openssl/crypto.h>`, `<openssl/err.h>`.
  - Used for HMAC-SHA256 (`HMAC(EVP_sha256(), ...)`) and Base64 encode/decode helpers via BIO.
- Purpose: Strong authentication (HMAC) and optional Base64 obfuscation for persisted configs.

### nlohmann/json
 
- Header-only include: `<nlohmann/json.hpp>`
- Source references:
  - `source/src/CConfig.cpp`: central config load/save/verify
  - `source/src/CMemory.cpp`: emits memory config JSON for persistence
  - Public API returns/accepts `nlohmann::json` in some places (e.g., `getModuleConfigJson`)
- Purpose: JSON serialization/deserialization.

### Threads::Threads (pthread)
 
- Link target: `Threads::Threads`
- Declared in: `modules/Core/CMakeLists.txt` for examples/tests
- Source references:
  - Concurrency in Core uses STL headers (e.g., `<thread>`, mutexes). Linked via CMake for test targets.
- Purpose: threading support for tests/examples.

### GoogleTest (tests only)
 
- Link targets: `GTest::GTest`, `GTest::Main`
- Declared in: `modules/Core/CMakeLists.txt` under `MODULE_EXTERNAL_TEST_LIB`
- Purpose: Unit testing framework for Core tests (when enabled).

### Boost (optional, variant, beast::span) — wrapper fallbacks
 
- Source references:
  - `source/inc/COptional.hpp` — wraps `std::optional` (C++17) or `boost::optional`
  - `source/inc/CVariant.hpp` — wraps `std::variant` (C++17) or `boost::variant`
  - `source/inc/CSpan.hpp` — wraps `std::span` (C++20) or `boost::beast::span`
- Purpose: Provide unified types across toolchains; falls back to Boost when newer C++ features are unavailable.

---

## Notes
 
- The Core library itself primarily needs Zlib, OpenSSL, and nlohmann/json at runtime for configuration; Boost filesystem/regex are mostly used in helper headers and example/test targets.
- If you vendor or mirror dependencies, ensure include paths and CMake targets remain stable (e.g., `nlohmann/json` is header-only).
- Licenses should be tracked at the repo level; if needed, add a LICENSES/ directory and collect notices for Boost, Zlib, OpenSSL, and nlohmann/json.
