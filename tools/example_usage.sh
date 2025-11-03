#!/bin/bash
# Config Editor 使用示例

TOOL="python3 tools/config_editor.py"
CONFIG="example_config.json"
SECRET="test123"

echo "========================================="
echo "Config Editor 使用示例"
echo "========================================="
echo

echo "1. 创建新配置文件"
$TOOL $CONFIG --create --secret $SECRET \
    --set memory.check_enable=true \
    --set memory.align=4
echo

echo "2. 查看配置"
$TOOL $CONFIG --display
echo

echo "3. 验证配置完整性"
$TOOL $CONFIG --verify --secret $SECRET
echo

echo "4. 添加更多字段"
$TOOL $CONFIG --secret $SECRET \
    --set logging.level=debug \
    --set logging.enabled=true \
    --set database.host=localhost \
    --set database.port=5432
echo

echo "5. 再次查看配置"
$TOOL $CONFIG --display
echo

echo "6. 再次验证"
$TOOL $CONFIG --verify --secret $SECRET
echo

echo "========================================="
echo "示例完成！生成的配置文件: $CONFIG"
echo "========================================="
