#!/bin/bash
# =============================================================================
# SPMC 压力测试脚本 - 多进程版本
# =============================================================================
# 用法: ./stress_test.sh [测试时长(小时)] [最大订阅者数] [发送速率]
# =============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PUB_BIN="${SCRIPT_DIR}/publisher_process"
SUB_BIN="${SCRIPT_DIR}/subscriber_process"

# ===== 配置参数 =====
TEST_HOURS=${1:-8}          # 默认8小时
MAX_SUBSCRIBERS=${2:-100}   # 默认100个订阅者
PUB_RATE=${3:-1000}         # 默认1000 msg/s
MIN_SUB_LIFETIME=30         # 订阅者最小存活时间（秒）
MAX_SUB_LIFETIME=300        # 订阅者最大存活时间（秒）
SUB_START_INTERVAL=2        # 订阅者启动间隔（秒）
STATS_INTERVAL=60           # 统计打印间隔（秒）

SERVICE_NAME="stress_test_spmc"
# Convert hours to seconds (support decimal hours like 0.1)
TEST_DURATION=$(awk "BEGIN {printf \"%.0f\", ${TEST_HOURS} * 3600}")

# ===== 目录设置 =====
STATS_DIR="${SCRIPT_DIR}/stress_stats"
LOG_DIR="${SCRIPT_DIR}/stress_logs"
mkdir -p "${STATS_DIR}" "${LOG_DIR}"

TIMESTAMP=$(date +%Y%m%d_%H%M%S)
MAIN_LOG="${LOG_DIR}/stress_${TIMESTAMP}.log"
PUB_STATS="${STATS_DIR}/publisher.stats"
declare -A SUB_PIDS
declare -A SUB_STATS
SUB_COUNTER=0

# ===== 信号处理 =====
cleanup() {
    echo "" | tee -a "${MAIN_LOG}"
    echo "========================================"  | tee -a "${MAIN_LOG}"
    echo "收到终止信号，正在清理..."  | tee -a "${MAIN_LOG}"
    echo "========================================" | tee -a "${MAIN_LOG}"
    
    # 停止所有订阅者
    for pid in "${!SUB_PIDS[@]}"; do
        if kill -0 "$pid" 2>/dev/null; then
            echo "[清理] 停止订阅者进程 PID=$pid" | tee -a "${MAIN_LOG}"
            kill -TERM "$pid" 2>/dev/null || true
        fi
    done
    
    # 停止发布者
    if [ -n "${PUB_PID}" ] && kill -0 "${PUB_PID}" 2>/dev/null; then
        echo "[清理] 停止发布者进程 PID=${PUB_PID}" | tee -a "${MAIN_LOG}"
        kill -TERM "${PUB_PID}" 2>/dev/null || true
    fi
    
    # 等待进程退出
    sleep 2
    
    # 强制kill残留进程
    for pid in "${!SUB_PIDS[@]}"; do
        kill -9 "$pid" 2>/dev/null || true
    done
    [ -n "${PUB_PID}" ] && kill -9 "${PUB_PID}" 2>/dev/null || true
    
    print_final_stats
    
    echo "清理完成！" | tee -a "${MAIN_LOG}"
    exit 0
}

trap cleanup SIGINT SIGTERM

# ===== 统计函数 =====
print_stats() {
    local elapsed=$1
    local hours=$((elapsed / 3600))
    local minutes=$(((elapsed % 3600) / 60))
    local seconds=$((elapsed % 60))
    
    echo "" | tee -a "${MAIN_LOG}"
    echo "========================================" | tee -a "${MAIN_LOG}"
    echo "运行统计 - $(date '+%Y-%m-%d %H:%M:%S')" | tee -a "${MAIN_LOG}"
    echo "========================================" | tee -a "${MAIN_LOG}"
    echo "运行时长: ${hours}h ${minutes}m ${seconds}s" | tee -a "${MAIN_LOG}"
    echo "----------------------------------------" | tee -a "${MAIN_LOG}"
    
    # 发布者统计
    if [ -f "${PUB_STATS}" ]; then
        local pub_sent=$(grep "^total_sent=" "${PUB_STATS}" | cut -d= -f2)
        local pub_errors=$(grep "^total_errors=" "${PUB_STATS}" | cut -d= -f2)
        local pub_rate=$(grep "^rate=" "${PUB_STATS}" | cut -d= -f2)
        echo "发布者:" | tee -a "${MAIN_LOG}"
        echo "  PID: ${PUB_PID}" | tee -a "${MAIN_LOG}"
        echo "  发送: ${pub_sent:-0} 消息" | tee -a "${MAIN_LOG}"
        echo "  错误: ${pub_errors:-0}" | tee -a "${MAIN_LOG}"
        echo "  速率: ${pub_rate:-0} msg/s" | tee -a "${MAIN_LOG}"
    fi
    
    echo "----------------------------------------" | tee -a "${MAIN_LOG}"
    
    # 订阅者统计
    local active_subs=0
    local total_received=0
    local total_lost=0
    
    for pid in "${!SUB_PIDS[@]}"; do
        if kill -0 "$pid" 2>/dev/null; then
            ((active_subs++))
            local stats_file="${SUB_STATS[$pid]}"
            if [ -f "${stats_file}" ]; then
                local received=$(grep "^total_received=" "${stats_file}" | cut -d= -f2)
                local lost=$(grep "^total_lost=" "${stats_file}" | cut -d= -f2)
                total_received=$((total_received + ${received:-0}))
                total_lost=$((total_lost + ${lost:-0}))
            fi
        fi
    done
    
    echo "订阅者:" | tee -a "${MAIN_LOG}"
    echo "  活跃数: ${active_subs}" | tee -a "${MAIN_LOG}"
    echo "  总创建: ${SUB_COUNTER}" | tee -a "${MAIN_LOG}"
    echo "  总接收: ${total_received} 消息" | tee -a "${MAIN_LOG}"
    echo "  总丢失: ${total_lost} 消息" | tee -a "${MAIN_LOG}"
    if [ ${total_received} -gt 0 ]; then
        local loss_rate=$(awk "BEGIN {printf \"%.4f\", (${total_lost} * 100.0) / (${total_received} + ${total_lost})}")
        echo "  丢失率: ${loss_rate}%" | tee -a "${MAIN_LOG}"
    fi
    echo "========================================" | tee -a "${MAIN_LOG}"
}

print_final_stats() {
    echo "" | tee -a "${MAIN_LOG}"
    echo "========================================" | tee -a "${MAIN_LOG}"
    echo "最终统计" | tee -a "${MAIN_LOG}"
    echo "========================================" | tee -a "${MAIN_LOG}"
    
    # 汇总所有订阅者统计
    local total_received=0
    local total_lost=0
    local completed_subs=0
    
    for stats_file in "${STATS_DIR}"/subscriber_*.stats; do
        if [ -f "${stats_file}" ]; then
            local received=$(grep "^total_received=" "${stats_file}" | cut -d= -f2)
            local lost=$(grep "^total_lost=" "${stats_file}" | cut -d= -f2)
            total_received=$((total_received + ${received:-0}))
            total_lost=$((total_lost + ${lost:-0}))
            ((completed_subs++))
        fi
    done
    
    if [ -f "${PUB_STATS}" ]; then
        local pub_sent=$(grep "^total_sent=" "${PUB_STATS}" | cut -d= -f2)
        local pub_errors=$(grep "^total_errors=" "${PUB_STATS}" | cut -d= -f2)
        echo "发布者总计:" | tee -a "${MAIN_LOG}"
        echo "  发送: ${pub_sent:-0} 消息" | tee -a "${MAIN_LOG}"
        echo "  错误: ${pub_errors:-0}" | tee -a "${MAIN_LOG}"
    fi
    
    echo "订阅者总计:" | tee -a "${MAIN_LOG}"
    echo "  总创建: ${SUB_COUNTER}" | tee -a "${MAIN_LOG}"
    echo "  完成退出: ${completed_subs}" | tee -a "${MAIN_LOG}"
    echo "  总接收: ${total_received} 消息" | tee -a "${MAIN_LOG}"
    echo "  总丢失: ${total_lost} 消息" | tee -a "${MAIN_LOG}"
    
    echo "========================================" | tee -a "${MAIN_LOG}"
    echo "日志文件: ${MAIN_LOG}" | tee -a "${MAIN_LOG}"
    echo "统计目录: ${STATS_DIR}" | tee -a "${MAIN_LOG}"
    echo "========================================" | tee -a "${MAIN_LOG}"
}

# ===== 启动订阅者 =====
start_subscriber() {
    local sub_id=$((SUB_COUNTER++))
    local stats_file="${STATS_DIR}/subscriber_${sub_id}.stats"
    local lifetime=$((RANDOM % (MAX_SUB_LIFETIME - MIN_SUB_LIFETIME + 1) + MIN_SUB_LIFETIME))
    
    # 后台启动订阅者进程
    timeout "${lifetime}s" "${SUB_BIN}" "${SERVICE_NAME}" "${sub_id}" "${stats_file}" \
        >> "${LOG_DIR}/subscriber_${sub_id}.log" 2>&1 &
    
    local pid=$!
    SUB_PIDS[$pid]=$sub_id
    SUB_STATS[$pid]=$stats_file
    
    echo "[$(date '+%H:%M:%S')] 启动订阅者 ID=${sub_id}, PID=${pid}, 存活=${lifetime}秒" | tee -a "${MAIN_LOG}"
}

# ===== 主程序 =====
echo "========================================" | tee "${MAIN_LOG}"
echo "  SPMC 压力测试 - 多进程版本" | tee -a "${MAIN_LOG}"
echo "========================================" | tee -a "${MAIN_LOG}"
echo "测试配置:" | tee -a "${MAIN_LOG}"
echo "  测试时长:       ${TEST_HOURS} 小时" | tee -a "${MAIN_LOG}"
echo "  最大订阅者数:   ${MAX_SUBSCRIBERS}" | tee -a "${MAIN_LOG}"
echo "  发送速率:       ${PUB_RATE} msg/s" | tee -a "${MAIN_LOG}"
echo "  订阅者存活时间: ${MIN_SUB_LIFETIME}-${MAX_SUB_LIFETIME} 秒" | tee -a "${MAIN_LOG}"
echo "  统计间隔:       ${STATS_INTERVAL} 秒" | tee -a "${MAIN_LOG}"
echo "========================================" | tee -a "${MAIN_LOG}"
echo "" | tee -a "${MAIN_LOG}"
echo "按 Ctrl+C 提前终止测试" | tee -a "${MAIN_LOG}"
echo "" | tee -a "${MAIN_LOG}"

# 清理旧的统计文件
rm -f "${STATS_DIR}"/*.stats

# 启动发布者
echo "[启动] 发布者进程..." | tee -a "${MAIN_LOG}"
"${PUB_BIN}" "${SERVICE_NAME}" "${PUB_RATE}" "${PUB_STATS}" \
    >> "${LOG_DIR}/publisher.log" 2>&1 &
PUB_PID=$!

echo "[启动] 发布者 PID=${PUB_PID}" | tee -a "${MAIN_LOG}"

# 等待发布者初始化
sleep 2

# 主循环
START_TIME=$(date +%s)
LAST_STATS_TIME=${START_TIME}

while true; do
    CURRENT_TIME=$(date +%s)
    ELAPSED=$((CURRENT_TIME - START_TIME))
    
    # 检查是否超时
    if [ ${ELAPSED} -ge ${TEST_DURATION} ]; then
        echo "" | tee -a "${MAIN_LOG}"
        echo "[主循环] 测试时长已达 ${TEST_HOURS} 小时，准备退出..." | tee -a "${MAIN_LOG}"
        break
    fi
    
    # 清理已退出的订阅者
    for pid in "${!SUB_PIDS[@]}"; do
        if ! kill -0 "$pid" 2>/dev/null; then
            local sub_id="${SUB_PIDS[$pid]}"
            echo "[$(date '+%H:%M:%S')] 订阅者 ID=${sub_id}, PID=${pid} 已退出" | tee -a "${MAIN_LOG}"
            unset SUB_PIDS[$pid]
            unset SUB_STATS[$pid]
        fi
    done
    
    # 如果未达到最大数，启动新订阅者
    local active_subs=${#SUB_PIDS[@]}
    if [ "${active_subs}" -lt "${MAX_SUBSCRIBERS}" ]; then
        start_subscriber
    fi
    
    # 定期打印统计
    if [ $((CURRENT_TIME - LAST_STATS_TIME)) -ge ${STATS_INTERVAL} ]; then
        print_stats ${ELAPSED}
        LAST_STATS_TIME=${CURRENT_TIME}
    fi
    
    # 短暂休眠
    sleep ${SUB_START_INTERVAL}
done

# 正常退出
cleanup
