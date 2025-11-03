#!/usr/bin/env python3
"""
Config File Editor with CRC32 and HMAC-SHA256 Generator

This tool allows you to modify JSON config files and automatically
generates the correct CRC32 checksum and HMAC-SHA256 signature.

Usage:
    python config_editor.py <config_file> [--secret SECRET] [--set key=value] [--update]
    
Examples:
    # Set memory checker to enabled
    python config_editor.py config.json --secret test123 --set memory.check_enable=true
    
    # Update multiple fields
    python config_editor.py config.json --secret test123 --set memory.check_enable=true --set memory.align=4
    
    # Interactive mode (edit file directly)
    python config_editor.py config.json --secret test123 --update
"""

import sys
import json
import hashlib
import hmac
import binascii
import os
from pathlib import Path
from typing import Any, Dict, Optional
import argparse


def calculate_crc32(data: str) -> str:
    """Calculate CRC32 checksum for the given data (zlib-compatible)

    Matches C++ ConfigManager::computeCrc32 which uses zlib's crc32 with
    initial value 0 and processes the raw UTF-8 bytes of the compact JSON.
    """
    crc = binascii.crc32(data.encode('utf-8')) & 0xFFFFFFFF
    return f"{crc:08x}"


def calculate_hmac_sha256(data: str, secret: str) -> str:
    """Calculate HMAC-SHA256 for the given data with secret"""
    h = hmac.new(secret.encode('utf-8'), data.encode('utf-8'), hashlib.sha256)
    return h.hexdigest()


def dump_compact_sorted(obj: Any) -> str:
    """Dump JSON in compact form with keys sorted.

    Matches nlohmann::json's default object_t (std::map) ordering used by ConfigManager,
    which results in lexicographically sorted keys at each object level.
    """
    return json.dumps(obj, separators=(',', ':'), ensure_ascii=False, sort_keys=True)


def set_nested_key(data: Dict, path: str, value: Any) -> None:
    """Set a nested key in dictionary using dot notation
    
    Example:
        set_nested_key(data, 'memory.check_enable', True)
        sets data['memory']['check_enable'] = True
    """
    keys = path.split('.')
    current = data
    
    # Navigate to the parent of the target key
    for key in keys[:-1]:
        if key not in current:
            current[key] = {}
        current = current[key]
    
    # Set the value, converting string representations to proper types
    final_key = keys[-1]
    if isinstance(value, str):
        # Try to parse as JSON for proper type conversion
        if value.lower() == 'true':
            value = True
        elif value.lower() == 'false':
            value = False
        elif value.lower() == 'null':
            value = None
        elif value.isdigit():
            value = int(value)
        else:
            try:
                value = float(value)
            except ValueError:
                pass  # Keep as string
    
    current[final_key] = value


def get_nested_key(data: Dict, path: str) -> Any:
    """Get a nested key from dictionary using dot notation"""
    keys = path.split('.')
    current = data
    
    for key in keys:
        if not isinstance(current, dict) or key not in current:
            return None
        current = current[key]
    
    return current


def load_config(filepath: str) -> Dict:
    """Load config file"""
    with open(filepath, 'r', encoding='utf-8') as f:
        return json.load(f)


def save_config(filepath: str, data: Dict, secret: Optional[str] = None) -> None:
    """Save config file with CRC32 and HMAC if secret provided

    Aligns with C++ ConfigManager:
    - CRC is computed over compact JSON of the core config (excluding __metadata__ and __update_policy__),
      preserving key order.
    - HMAC is computed over the same compact JSON string (not including version or crc),
      using HMAC-SHA256.
    - Metadata fields: crc (hex), hmac (hex), version (int), description (string), encrypted (bool), timestamp (ISO 8601).
    """
    # Remove existing metadata fields for clean calculation
    config_copy = data.copy()
    metadata = config_copy.pop('__metadata__', {})
    # Exclude policy field from security operations
    config_copy.pop('__update_policy__', None)

    # Calculate CRC32 on the core config (excluding metadata and policy), compact format, preserve order
    core_json = dump_compact_sorted(config_copy)
    crc_hex = calculate_crc32(core_json)

    # Build/normalize metadata
    if not isinstance(metadata, dict):
        metadata = {}
    # Use field names expected by ConfigManager
    metadata['crc'] = crc_hex
    # Keep version as integer if possible; default to 1
    try:
        metadata['version'] = int(metadata.get('version', 1))
    except Exception:
        metadata['version'] = 1
    # Optional descriptive fields for compatibility
    metadata['description'] = metadata.get('description', '')
    metadata['encrypted'] = bool(metadata.get('encrypted', False))

    # Calculate HMAC if secret provided (over core_json only)
    if secret:
        metadata['hmac'] = calculate_hmac_sha256(core_json, secret)
    else:
        # Remove stale HMAC if we cannot recompute it
        metadata.pop('hmac', None)

    # Optional timestamp in "YYYY-MM-DD HH:MM:SS" to match C++
    try:
        from datetime import datetime
        metadata['timestamp'] = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
    except Exception:
        pass

    # Reconstruct the final config with metadata at top
    final_config = {'__metadata__': metadata}
    
    # Add update policy if it existed
    if '__update_policy__' in data:
        final_config['__update_policy__'] = data['__update_policy__']
    
    # Add all other keys
    for key, value in config_copy.items():
        final_config[key] = value
    
    # Write to file with pretty printing
    with open(filepath, 'w', encoding='utf-8') as f:
        json.dump(final_config, f, indent=4, ensure_ascii=False)
        f.write('\n')  # Add trailing newline
    
    print(f"✓ Config saved to: {filepath}")
    print(f"  CRC32: {crc_hex}")
    if secret:
        print(f"  HMAC:  {metadata['hmac'][:16]}...")


def display_config(data: Dict, prefix: str = '') -> None:
    """Display config in a readable format"""
    for key, value in data.items():
        if key.startswith('__'):
            continue
        full_key = f"{prefix}.{key}" if prefix else key
        if isinstance(value, dict):
            print(f"{full_key}:")
            display_config(value, full_key)
        else:
            print(f"  {full_key} = {value}")


def verify_config(filepath: str, secret: Optional[str] = None) -> bool:
    """Verify config file integrity"""
    data = load_config(filepath)
    metadata = data.get('__metadata__', {})
    
    if not metadata:
        print("✗ No metadata found in config")
        return False
    
    stored_crc = metadata.get('crc') or metadata.get('crc32')
    stored_hmac = metadata.get('hmac')
    
    # Remove metadata for calculation
    config_copy = data.copy()
    config_copy.pop('__metadata__', None)
    config_copy.pop('__update_policy__', None)
    
    # Calculate CRC32 (compact JSON preserving order)
    core_json = dump_compact_sorted(config_copy)
    calc_crc = calculate_crc32(core_json)
    
    crc_valid = stored_crc == calc_crc
    print(f"{'✓' if crc_valid else '✗'} CRC32: {stored_crc} {'==' if crc_valid else '!='} {calc_crc}")
    
    # Verify HMAC if secret provided
    if secret and stored_hmac:
        # HMAC is computed over the same core JSON string
        calc_hmac = calculate_hmac_sha256(core_json, secret)
        
        hmac_valid = stored_hmac == calc_hmac
        print(f"{'✓' if hmac_valid else '✗'} HMAC: {stored_hmac[:16]}... {'==' if hmac_valid else '!='} {calc_hmac[:16]}...")
        
        return crc_valid and hmac_valid
    
    return crc_valid


def main():
    parser = argparse.ArgumentParser(
        description='Config File Editor with CRC32 and HMAC-SHA256 Generator',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # View config
  %(prog)s config.json
  
  # Verify integrity
  %(prog)s config.json --verify --secret test123
  
  # Enable memory checker
  %(prog)s config.json --secret test123 --set memory.check_enable=true
  
  # Update multiple fields
  %(prog)s config.json --secret test123 \\
      --set memory.check_enable=true \\
      --set memory.align=4 \\
      --set memory.pools[0].unitSize=8
        """
    )
    
    parser.add_argument('config_file', help='Path to config JSON file')
    parser.add_argument('--secret', '-s', help='HMAC secret key (or set HMAC_SECRET env var)')
    parser.add_argument('--set', '-S', action='append', dest='sets', metavar='KEY=VALUE',
                        help='Set a config value (can be used multiple times)')
    parser.add_argument('--verify', '-v', action='store_true',
                        help='Verify config integrity')
    parser.add_argument('--display', '-d', action='store_true',
                        help='Display config contents')
    parser.add_argument('--create', '-c', action='store_true',
                        help='Create new config file if not exists')
    
    args = parser.parse_args()
    
    # Get secret from env if not provided
    secret = args.secret or os.environ.get('HMAC_SECRET')
    
    # Check if file exists
    config_path = Path(args.config_file)
    if not config_path.exists():
        if args.create:
            print(f"Creating new config file: {config_path}")
            data = {
                '__metadata__': {
                    'version': '1.0.0',
                    'crc32': '',
                    'hmac': ''
                },
                '__update_policy__': {
                    'default': 'on_change'
                }
            }
        else:
            print(f"Error: Config file not found: {config_path}")
            print("Use --create to create a new file")
            return 1
    else:
        data = load_config(args.config_file)
    
    # Handle verify command
    if args.verify:
        if not secret:
            print("Warning: No secret provided, HMAC verification skipped")
        return 0 if verify_config(args.config_file, secret) else 1
    
    # Handle display command
    if args.display:
        print(f"Config: {args.config_file}")
        print("=" * 60)
        display_config(data)
        return 0
    
    # Handle set commands
    modified = False
    if args.sets:
        for assignment in args.sets:
            if '=' not in assignment:
                print(f"Error: Invalid assignment format: {assignment}")
                print("Expected format: key=value or key.nested=value")
                return 1
            
            key, value = assignment.split('=', 1)
            print(f"Setting {key} = {value}")
            set_nested_key(data, key, value)
            modified = True
    
    # If no modifications and not creating, just display
    if not modified and not args.create:
        print(f"Config: {args.config_file}")
        print("=" * 60)
        display_config(data)
        print("\nUse --set KEY=VALUE to modify config")
        print("Use --verify to check integrity")
        return 0
    
    # Save if modified or creating
    if modified or args.create:
        # It's okay to save without a secret (HMAC omitted). CRC is still generated.
        
        save_config(args.config_file, data, secret)
    
    return 0


if __name__ == '__main__':
    sys.exit(main())
