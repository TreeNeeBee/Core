#!/bin/bash

# /dev/zero -> /dev/null 多进程压力测试启动脚本
# 用法: ./run_devzero_multiproc.sh <duration_sec> <num_subs> <msg_size> [send_rate]

if [ $# -lt 3 ]; then
    echo "用法: $0 <测试时长(秒)> <订阅者数量> <消息大小(字节)> [发送速率(msg/s)]"
    echo "示例:"
    echo "  $0 60 10 4096       # 60秒, 10订阅者, 4KB"
    echo "  $0 300 20 8192 1000 # 5分钟, 20订阅者, 8KB, 1000 msg/s"
    exit 1
fi

DURATION=$1
NUM_SUBS=$2
MSG_SIZE=$3
SEND_RATE=${4:-0}

# 设置库路径
export LD_LIBRARY_PATH=/workspace/LightAP/build/modules/Core:$LD_LIBRARY_PATH

# 可执行文件路径
TEST_BIN=/workspace/LightAP/build/modules/Core/devzero_stress_test

# 清理旧的共享内存
echo "清理旧的共享内存..."
rm -f /dev/shm/lightap_ipc_* /dev/shm/lightap_devzero_stats 2>/dev/null

echo "========================================"
echo "/dev/zero -> /dev/null 压力测试 (多进程)"
echo "========================================"
echo "配置:"
echo "  测试时长: $DURATION 秒"
echo "  订阅者数: $NUM_SUBS"
echo "  消息大小: $MSG_SIZE 字节"
echo "  发送速率: $([ $SEND_RATE -eq 0 ] && echo '无限制' || echo ${SEND_RATE} msg/s)"
echo "========================================"
echo

# 存储所有子进程PID
declare -a PIDS

# 启动发布者进程
echo "[主脚本] 启动发布者进程..."
if [ $SEND_RATE -eq 0 ]; then
    $TEST_BIN pub $DURATION $NUM_SUBS $MSG_SIZE 2>&1 | grep -v "^\[DEBUG\]" &
else
    $TEST_BIN pub $DURATION $NUM_SUBS $MSG_SIZE $SEND_RATE 2>&1 | grep -v "^\[DEBUG\]" &
fi
PUB_PID=$!
PIDS+=($PUB_PID)
echo "[主脚本] 发布者PID=$PUB_PID"

# 等待发布者就绪
sleep 1

# 启动订阅者进程
for ((i=0; i<$NUM_SUBS; i++)); do
    echo "[主脚本] 启动订阅者#$i..."
    if [ $SEND_RATE -eq 0 ]; then
        $TEST_BIN sub $DURATION $NUM_SUBS $MSG_SIZE $i 2>&1 | grep -v "^\[DEBUG\]" &
    else
        $TEST_BIN sub $DURATION $NUM_SUBS $MSG_SIZE $i $SEND_RATE 2>&1 | grep -v "^\[DEBUG\]" &
    fi
    SUB_PID=$!
    PIDS+=($SUB_PID)
    echo "[主脚本] 订阅者#$i PID=$SUB_PID"
    sleep 0.1
done

# 启动监控进程
echo "[主脚本] 启动监控进程..."
if [ $SEND_RATE -eq 0 ]; then
    $TEST_BIN monitor $DURATION $NUM_SUBS $MSG_SIZE 2>&1 | grep -v "^\[DEBUG\]" &
else
    $TEST_BIN monitor $DURATION $NUM_SUBS $MSG_SIZE $SEND_RATE 2>&1 | grep -v "^\[DEBUG\]" &
fi
MON_PID=$!
PIDS+=($MON_PID)
echo "[主脚本] 监控PID=$MON_PID"

echo
echo "[主脚本] 所有进程已启动，总共 ${#PIDS[@]} 个进程"
echo "[主脚本] 等待测试完成 (${DURATION}秒)..."
echo

# 等待所有进程完成
for pid in "${PIDS[@]}"; do
    wait $pid 2>/dev/null
done

echo
echo "[主脚本] 所有进程已完成"

# 清理共享内存
rm -f /dev/shm/lightap_devzero_stats 2>/dev/null

exit 0
