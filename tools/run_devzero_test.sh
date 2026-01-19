#!/bin/bash
# Run devzero stress test without DEBUG output

if [ $# -lt 3 ]; then
    echo "用法: $0 <测试时长(秒)> <订阅者数量> <消息大小(字节)> [发送速率(msg/s)]"
    echo "示例:"
    echo "  $0 60 10 4096        # 60秒, 10订阅者, 4KB消息"
    echo "  $0 300 20 8192 1000  # 5分钟, 20订阅者, 8KB消息, 1000 msg/s"
    exit 1
fi

# Clean up old shared memory
rm -f /dev/shm/sem.devzero_stress* /dev/shm/devzero_stress* 2>/dev/null

# Redirect DEBUG to /dev/null using environment variable or sed
export INNER_CORE_LOG_LEVEL=ERROR

# Run the test, filtering DEBUG output more aggressively
./modules/Core/devzero_stress_test "$@" 2>&1 | grep -v "^\[DEBUG\]" | grep -v "^[[:space:]]*$"
