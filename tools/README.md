# Core Module - Configuration Editor Tool

A command-line tool for editing LightAP Core configuration files with automatic CRC32 and HMAC-SHA256 generation.

## Overview

The Configuration Editor (`config_editor.py`) provides a safe and convenient way to modify Core module configuration files while maintaining integrity through automatic checksum and HMAC signature generation.

## Features

- ✅ Modify JSON configuration fields with type-safe operations
- ✅ Automatic CRC32 checksum calculation
- ✅ Automatic HMAC-SHA256 signature generation
- ✅ Configuration integrity verification
- ✅ Support for nested fields (dot notation)
- ✅ Automatic type conversion (boolean, number, string)
- ✅ Create new configuration files from scratch

## Prerequisites

- Python 3.6+
- No external dependencies (uses standard library only)

## Quick Start

### View Configuration

```bash
python3 tools/config_editor.py config.json
```

Display detailed content:
```bash
python3 tools/config_editor.py config.json --display
```

### Verify Configuration Integrity

```bash
# Using command-line secret
python3 tools/config_editor.py config.json --verify --secret your_secret_key

# Using environment variable (recommended)
export HMAC_SECRET=your_secret_key
python3 tools/config_editor.py config.json --verify
```

### Modify Configuration Fields

Enable memory checker:
```bash
python3 tools/config_editor.py config.json --secret your_secret_key \
    --set memory.check_enable=true
```

Modify multiple fields:
```bash
python3 tools/config_editor.py config.json --secret your_secret_key \
    --set memory.check_enable=true \
    --set memory.align=8 \
    --set logging.level=info
```

Add nested objects:
```bash
python3 tools/config_editor.py config.json --secret your_secret_key \
    --set database.host=localhost \
    --set database.port=5432 \
    --set database.enabled=true
```

### Create New Configuration File

```bash
python3 tools/config_editor.py new_config.json --create --secret your_secret_key \
    --set app.name=MyApp \
    --set app.version=1.0.0 \
    --set app.debug=false
```

## 配置文件格式

工具生成的配置文件包含三个部分：

```jsonc
{
    "__metadata__": {
        "version": 1,
        "description": "",
        "encrypted": false,
        "timestamp": "2025-11-01 12:00:00",
        "crc": "91668e54",
        "hmac": "ec731c2691b1afc18d6eb540a2d37c08..."
    },
    "__update_policy__": {
        "default": "on_change"
    },
    "memory": {
        "check_enable": true,
        "align": 4,
        "pools": []
    }
## Configuration File Format

The tool generates configuration files with three sections:

```json
{
    "__metadata__": {
        "version": 1,
        "description": "Core module configuration",
        "encrypted": false,
        "timestamp": "2025-11-03 12:00:00",
        "crc": "91668e54",
        "hmac": "ec731c2691b1afc18d6eb540a2d37c08..."
    },
    "__update_policy__": {
        "default": "on_change"
    },
    "memory": {
        "check_enable": true,
        "align": 8,
        "pool_count": 5
    }
}
```

### Metadata Fields

- `__metadata__`: Contains version, CRC checksum, HMAC signature, timestamp, encryption flag, description
- `__update_policy__`: Configuration update policy settings
- Application data: Actual configuration fields (e.g., `memory`, `logging`)

### CRC32 and HMAC Calculation

1. **CRC32**: Calculated on core JSON (excluding `__metadata__` and `__update_policy__`)
   - Object keys sorted alphabetically
   - Arrays maintain original order
   
2. **HMAC-SHA256**: Calculated on the same core JSON string using provided secret key

## Environment Variables

- `HMAC_SECRET`: Default HMAC secret key (avoids specifying in command line)

```bash
export HMAC_SECRET=your_secret_key
python3 tools/config_editor.py config.json --verify
```

## Automatic Type Conversion

The tool automatically converts string values to appropriate types:

| Input | Converted Type | Example |
|-------|----------------|---------|
| `true`/`false` | Boolean | `--set enabled=true` → `true` |
| `null` | Null | `--set value=null` → `null` |
| Pure numbers | Integer | `--set port=8080` → `8080` |
| Decimal | Float | `--set rate=1.5` → `1.5` |
| Others | String | `--set name=app` → `"app"` |

## Usage Examples

### Example 1: Configure Memory Manager

```bash
# Enable memory checker and set alignment
python3 tools/config_editor.py config.json --secret your_key \
    --set memory.check_enable=true \
    --set memory.align=8 \
    --set memory.pool_count=5

# Verify configuration
python3 tools/config_editor.py config.json --verify --secret your_key
```

### Example 2: Configure Logging

```bash
python3 tools/config_editor.py config.json --secret your_key \
    --set logging.level=info \
    --set logging.file=/var/log/lightap.log \
    --set logging.enabled=true
```

### Example 3: CI/CD Integration

```bash
#!/bin/bash
# Update configuration before deployment
export HMAC_SECRET=${CI_CONFIG_SECRET}

python3 tools/config_editor.py config.json \
    --set app.environment=production \
    --set app.debug=false \
    --set database.host=${DB_HOST}

# Verify integrity
if python3 tools/config_editor.py config.json --verify; then
    echo "✓ Configuration verified"
    ./deploy.sh
else
    echo "✗ Configuration verification failed!"
    exit 1
fi
```

## Error Handling

### CRC32 Verification Failed

```
✗ CRC32: 91668e54 != 05199353
```

**Cause**: Configuration modified without regenerating CRC32

**Solution**: Re-save configuration with tool:
```bash
python3 tools/config_editor.py config.json --secret your_key --set dummy=value
```

### HMAC Verification Failed

```
✗ HMAC: ec731c26... != d5517842...
```

**Causes**:
1. Wrong secret key
2. Configuration tampered

**Solution**: Verify secret key or regenerate with correct key

## Best Practices

1. **Secret Management**: Never hardcode HMAC secrets in source code
   - Use environment variables
   - Use key management systems (e.g., HashiCorp Vault)

2. **Backup**: Always backup configuration files before modification

3. **Version Control**: Don't commit sensitive configuration to Git
   - Use `.gitignore` for `*.json` config files
   - Store templates without secrets instead

4. **Validation**: Always verify configuration after modification
   ```bash
   python3 tools/config_editor.py config.json --verify
   ```

## Troubleshooting

If you encounter issues, check:

1. ✓ Python version >= 3.6
2. ✓ JSON file format is valid
3. ✓ File permissions allow read/write
4. ✓ `HMAC_SECRET` environment variable is set correctly
5. ✓ Configuration file path is correct

## Integration with Core Module

This tool is designed for use with the Core module's ConfigManager:

- See [HMAC_SECRET_CONFIG.md](../doc/HMAC_SECRET_CONFIG.md) for security setup
- See [QUICK_START.md](../doc/QUICK_START.md) for configuration usage
- See [README.md](../README.md) for Core module overview

## Related Documentation

- [Core Module README](../README.md)
- [HMAC Security Configuration](../doc/HMAC_SECRET_CONFIG.md)
- [Configuration Quick Start](../doc/QUICK_START.md)

## License

Part of the LightAP project. See project license for details.

---

**Version**: 1.0.0  
**Last Updated**: 2025-11-03
