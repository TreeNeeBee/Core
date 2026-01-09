#!/bin/bash
# Quick validation test for SPMC stress test
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PUB_BIN="${SCRIPT_DIR}/../../../../build/modules/Core/publisher_process"
SUB_BIN="${SCRIPT_DIR}/../../../../build/modules/Core/subscriber_process"

# Check if running from build directory
if [ -f "${SCRIPT_DIR}/publisher_process" ]; then
    PUB_BIN="${SCRIPT_DIR}/publisher_process"
    SUB_BIN="${SCRIPT_DIR}/subscriber_process"
fi

TEST_DURATION=${1:-30}      # 默认30秒
NUM_SUBSCRIBERS=${2:-3}     # 默认3个订阅者
PUB_RATE=${3:-1000}         # 默认1000 msg/s

SERVICE_NAME="quick_test"
STATS_DIR="/tmp/quick_test_stats"
LOG_DIR="/tmp/quick_test_logs"

mkdir -p "${STATS_DIR}" "${LOG_DIR}"
rm -f "${STATS_DIR}"/*.stats

echo "========================================
  快速验证测试 - SPMC
========================================"
echo "配置:"
echo "  测试时长: ${TEST_DURATION} 秒"
echo "  订阅者数: ${NUM_SUBSCRIBERS}"  
echo "  发送速率: ${PUB_RATE} msg/s"
echo "========================================"

# 清理函数
cleanup() {
    echo ""
    echo "正在停止所有进程..."
    
    # 停止所有订阅者
    for pid in ${SUB_PIDS[@]}; do
        kill -TERM $pid 2>/dev/null || true
    done
    
    # 停止发布者
    if [ -n "${PUB_PID}" ]; then
        kill -TERM ${PUB_PID} 2>/dev/null || true
    fi
    
    sleep 2
    
    # 打印统计
    echo ""
    echo "========================================
  测试统计
========================================"
    
    if [ -f "${STATS_DIR}/publisher.stats" ]; then
        echo "发布者:"
        cat "${STATS_DIR}/publisher.stats" | grep -E "total_sent|total_errors|rate"
        echo ""
    fi
    
    echo "订阅者:"
    local total_received=0
    local total_lost=0
    for stats_file in "${STATS_DIR}"/subscriber_*.stats; do
        if [ -f "${stats_file}" ]; then
            local sub_id=$(basename "${stats_file}" .stats | sed 's/subscriber_//')
            local received=$(grep "^total_received=" "${stats_file}" | cut -d= -f2)
            local lost=$(grep "^total_lost=" "${stats_file}" | cut -d= -f2)
            echo "  订阅者 ${sub_id}: 接收=${received}, 丢失=${lost}"
            total_received=$((total_received + ${received:-0}))
            total_lost=$((total_lost + ${lost:-0}))
        fi
    done
    
    echo "  总计: 接收=${total_received}, 丢失=${total_lost}"
    
    if [ ${total_received} -gt 0 ]; then
        local loss_rate=$(awk "BEGIN {printf \"%.4f\", (${total_lost} * 100.0) / (${total_received} + ${total_lost})}")
        echo "  丢失率: ${loss_rate}%"
    fi
    
    echo "========================================"
    echo "日志目录: ${LOG_DIR}"
    echo "统计目录: ${STATS_DIR}"
    echo "========================================"
}

trap cleanup EXIT INT TERM

# 启动发布者
echo "启动发布者..."
"${PUB_BIN}" "${SERVICE_NAME}" "${PUB_RATE}" "${STATS_DIR}/publisher.stats" \
    > "${LOG_DIR}/publisher.log" 2>&1 &
PUB_PID=$!
echo "  PID=${PUB_PID}"

# 等待发布者初始化
sleep 1

# 启动订阅者
echo "启动 ${NUM_SUBSCRIBERS} 个订阅者..."
SUB_PIDS=()
for i in $(seq 0 $((NUM_SUBSCRIBERS - 1))); do
    "${SUB_BIN}" "${SERVICE_NAME}" "${i}" "${STATS_DIR}/subscriber_${i}.stats" \
        > "${LOG_DIR}/subscriber_${i}.log" 2>&1 &
    SUB_PIDS+=($!)
    echo "  订阅者 ${i}: PID=${SUB_PIDS[$i]}"
    sleep 0.1
done

echo ""
echo "测试运行中，等待 ${TEST_DURATION} 秒..."
echo "按 Ctrl+C 提前终止"
echo ""

# 等待测试完成
sleep ${TEST_DURATION}

# 清理由trap自动调用
