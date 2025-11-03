# HMAC Secret Configuration

## Overview

ConfigManager uses HMAC-SHA256 for configuration file integrity verification. The HMAC secret can be provided through environment variable or use a built-in default.

## Configuration Modes

### 1. Lenient Mode (Default)

**Behavior:**
- If `HMAC_SECRET` environment variable is set → use it
- If `HMAC_SECRET` is NOT set → use built-in default with WARNING messages
- Program continues to run in both cases

**Usage:**
```bash
# Without ENV (uses built-in default)
./your_application

# With ENV (uses provided secret)
export HMAC_SECRET="your-production-secret-key"
./your_application
```

**Warning Messages (when ENV not set):**
```
[ConfigManager] WARNING: HMAC_SECRET environment variable not set!
[ConfigManager] WARNING: Using built-in default HMAC secret (NOT secure for production)
[ConfigManager] WARNING: Please set HMAC_SECRET for production deployments
```

### 2. Strict Mode (Production)

**Behavior:**
- `HMAC_SECRET` environment variable is **MANDATORY**
- If not set or empty → **program aborts with error**
- No fallback to built-in default

**Enable Strict Mode:**

In `modules/Core/CMakeLists.txt`, uncomment:
```cmake
target_compile_definitions(lap_core PRIVATE REQUIRE_HMAC_SECRET_ENV)
```

**Usage:**
```bash
# Must set ENV, otherwise program exits
export HMAC_SECRET="your-production-secret-key"
./your_application

# Without ENV → FATAL ERROR and program abort
./your_application  # ❌ Will exit immediately
```

**Error Messages (strict mode without ENV):**
```
[ConfigManager] FATAL: HMAC_SECRET environment variable not set or empty!
[ConfigManager] REQUIRE_HMAC_SECRET_ENV is enabled, program cannot continue.
[ConfigManager] Please set HMAC_SECRET environment variable before starting.
```

## Built-in Default Secret

**Default Value:** `LightAP-Default-HMAC-Secret-2025-DO-NOT-USE-IN-PRODUCTION`

**⚠️ Security Warning:**
- The built-in secret is **NOT secure** for production environments
- Only use for development and testing
- Always set `HMAC_SECRET` environment variable in production

## Best Practices

### Development Environment
```bash
# Lenient mode is fine for development
./your_dev_application
```

### Production Environment
```bash
# Option 1: Set environment variable
export HMAC_SECRET="$(openssl rand -hex 32)"
./your_production_application

# Option 2: Use systemd service with environment file
# /etc/systemd/system/your-app.service
[Service]
EnvironmentFile=/etc/your-app/secrets.env
ExecStart=/usr/bin/your_application

# /etc/your-app/secrets.env
HMAC_SECRET=your-secure-random-secret-key
```

### Generate Secure Secret
```bash
# Generate a random 256-bit secret
openssl rand -hex 32

# Or base64 encoded
openssl rand -base64 32
```

## Testing

### Test Lenient Mode (Default)
```bash
cd build/modules/Core
./core_memory_example
# Should see WARNING messages but program continues
```

### Test with Custom Secret
```bash
cd build/modules/Core
HMAC_SECRET="my-test-key" ./core_memory_example
# Should see: "Loaded HMAC secret from environment variable"
```

### Test Strict Mode
```bash
# 1. Enable REQUIRE_HMAC_SECRET_ENV in CMakeLists.txt
# 2. Rebuild
cd build && cmake --build . --target lap_core

# 3. Test without ENV (should abort)
cd modules/Core
./core_memory_example  # ❌ FATAL ERROR

# 4. Test with ENV (should work)
HMAC_SECRET="test" ./core_memory_example  # ✓ OK
```

## Implementation Details

### Code Location
- **Implementation:** `modules/Core/source/src/CConfig.cpp`
- **Built-in Secret:** Line ~48
- **Load Logic:** ConfigManager constructor (lines 275-305)

### Key Constants
```cpp
constexpr const char* ENV_HMAC_SECRET = "HMAC_SECRET";
constexpr const char* BUILTIN_HMAC_SECRET = 
    "LightAP-Default-HMAC-Secret-2025-DO-NOT-USE-IN-PRODUCTION";
```

### Compile-time Macro
```cpp
#ifdef REQUIRE_HMAC_SECRET_ENV
    // Strict mode: ENV mandatory
#else
    // Lenient mode: fallback to built-in
#endif
```

## Migration Guide

### Upgrading from Old Version

**Old behavior:** No HMAC_SECRET → Verification fails

**New behavior:** 
- Lenient mode: No HMAC_SECRET → Uses built-in with warnings
- Strict mode: No HMAC_SECRET → Program aborts

**Migration steps:**
1. Development: No changes needed, warnings will appear
2. Production: Set `HMAC_SECRET` environment variable
3. Security-critical: Enable strict mode in CMakeLists.txt

## FAQ

**Q: What happens if I have an old config.json with different HMAC?**
A: HMAC verification will fail, but ConfigManager continues with empty config. Set correct HMAC_SECRET or regenerate config.json.

**Q: Can I disable HMAC verification entirely?**
A: Yes, call `save(false)` to skip security features, but **NOT recommended** for production.

**Q: How do I know which mode is active?**
A: Check log messages:
- Lenient: "Loaded HMAC secret from environment variable" (with ENV) or "WARNING: Using built-in default..." (without ENV)
- Strict: "Loaded HMAC secret from environment variable (strict mode)"

**Q: Performance impact?**
A: Minimal. HMAC computation happens only during config save/load operations.
