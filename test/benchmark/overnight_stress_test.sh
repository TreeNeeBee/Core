#!/bin/bash
# =============================================================================
# SPMC 过夜压力测试 - 简化版
# =============================================================================
# 用法: ./overnight_stress_test.sh [测试时长(秒)] [最大订阅者数] [发送速率]
# 示例: ./overnight_stress_test.sh 28800 100 1000  # 8小时, 100订阅者, 1000 msg/s
# =============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PUB_BIN="${SCRIPT_DIR}/../../../../build/modules/Core/publisher_process"
SUB_BIN="${SCRIPT_DIR}/../../../../build/modules/Core/subscriber_process"

# Check if running from build directory
if [ -f "${SCRIPT_DIR}/publisher_process" ]; then
    PUB_BIN="${SCRIPT_DIR}/publisher_process"
    SUB_BIN="${SCRIPT_DIR}/subscriber_process"
fi

# ===== 配置参数 =====
TEST_DURATION=${1:-28800}   # 默认8小时
MAX_SUBSCRIBERS=${2:-100}   # 默认100个订阅者
PUB_RATE=${3:-1000}         # 默认1000 msg/s
MIN_SUB_LIFETIME=30         # 订阅者最小存活时间（秒）
MAX_SUB_LIFETIME=300        # 订阅者最大存活时间（秒）
SUB_START_INTERVAL=2        # 订阅者启动间隔（秒）
STATS_INTERVAL=300          # 统计打印间隔（秒, 5分钟）

SERVICE_NAME="overnight_stress"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
STATS_DIR="/tmp/overnight_stats_${TIMESTAMP}"
LOG_DIR="/tmp/overnight_logs_${TIMESTAMP}"
PROM_METRICS_FILE="/tmp/stress_test_metrics.prom"

mkdir -p "${STATS_DIR}" "${LOG_DIR}"

MAIN_LOG="${LOG_DIR}/main.log"
PUB_STATS="${STATS_DIR}/publisher.stats"
declare -a SUB_PIDS
declare -a SUB_STATS_FILES
SUB_COUNTER=0
TEST_START_TIME=$(date +%s)

# ===== 日志函数 =====
log() {
    echo "[$(date '+%H:%M:%S')] $*" | tee -a "${MAIN_LOG}"
}

# ===== 清理函数 =====
cleanup() {
    log ""
    log "========================================"
    log "收到终止信号，正在清理..."
    log "========================================"
    
    # 停止所有订阅者
    for pid in "${SUB_PIDS[@]}"; do
        if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
            log "停止订阅者进程 PID=$pid"
            kill -TERM "$pid" 2>/dev/null || true
        fi
    done
    
    # 停止发布者
    if [ -n "${PUB_PID}" ] && kill -0 "${PUB_PID}" 2>/dev/null; then
        log "停止发布者进程 PID=${PUB_PID}"
        kill -TERM "${PUB_PID}" 2>/dev/null || true
    fi
    
    # 等待进程退出
    sleep 3
    
    # 强制kill残留进程
    for pid in "${SUB_PIDS[@]}"; do
        [ -n "$pid" ] && kill -9 "$pid" 2>/dev/null || true
    done
    [ -n "${PUB_PID}" ] && kill -9 "${PUB_PID}" 2>/dev/null || true
    
    print_final_stats
    
    log "清理完成！"
    exit 0
}

trap cleanup SIGINT SIGTERM EXIT

# ===== Prometheus 指标输出函数 =====
export_prometheus_metrics() {
    local elapsed=$1
    local timestamp=$(date +%s)
    
    # 读取发布者统计
    local pub_sent=0
    local pub_errors=0
    local pub_rate=0
    if [ -f "${PUB_STATS}" ]; then
        pub_sent=$(grep "^total_sent=" "${PUB_STATS}" 2>/dev/null | cut -d= -f2)
        pub_errors=$(grep "^total_errors=" "${PUB_STATS}" 2>/dev/null | cut -d= -f2)
        pub_rate=$(grep "^rate=" "${PUB_STATS}" 2>/dev/null | cut -d= -f2)
    fi
    
    # 统计订阅者
    local active_subs=0
    local total_received=0
    local total_lost=0
    local completed_subs=0
    
    for pid in "${SUB_PIDS[@]}"; do
        if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
            ((active_subs++))
        fi
    done
    
    for stats_file in "${STATS_DIR}"/subscriber_*.stats; do
        if [ -f "${stats_file}" ]; then
            local received=$(grep "^total_received=" "${stats_file}" 2>/dev/null | cut -d= -f2)
            local lost=$(grep "^total_lost=" "${stats_file}" 2>/dev/null | cut -d= -f2)
            local status=$(grep "^status=" "${stats_file}" 2>/dev/null | cut -d= -f2)
            total_received=$((total_received + ${received:-0}))
            total_lost=$((total_lost + ${lost:-0}))
            if [ "${status}" = "completed" ]; then
                ((completed_subs++))
            fi
        fi
    done
    
    # 计算丢失率
    local loss_rate=0
    if [ $((total_received + total_lost)) -gt 0 ]; then
        loss_rate=$(awk "BEGIN {printf \"%.8f\", (${total_lost} * 100.0) / (${total_received} + ${total_lost})}")
    fi
    
    # 生成Prometheus格式的指标
    cat > "${PROM_METRICS_FILE}" << EOF
# HELP ipc_stress_test_info Test information
# TYPE ipc_stress_test_info gauge
ipc_stress_test_info{service="${SERVICE_NAME}",test_id="${TIMESTAMP}",message_size="4096"} 1

# HELP ipc_stress_test_uptime_seconds Test uptime in seconds
# TYPE ipc_stress_test_uptime_seconds gauge
ipc_stress_test_uptime_seconds ${elapsed}

# HELP ipc_publisher_messages_sent_total Total messages sent by publisher
# TYPE ipc_publisher_messages_sent_total counter
ipc_publisher_messages_sent_total ${pub_sent:-0}

# HELP ipc_publisher_errors_total Total send errors
# TYPE ipc_publisher_errors_total counter
ipc_publisher_errors_total ${pub_errors:-0}

# HELP ipc_publisher_send_rate_msgs_per_sec Current send rate in messages per second
# TYPE ipc_publisher_send_rate_msgs_per_sec gauge
ipc_publisher_send_rate_msgs_per_sec ${pub_rate:-0}

# HELP ipc_publisher_throughput_bytes_per_sec Data throughput in bytes per second
# TYPE ipc_publisher_throughput_bytes_per_sec gauge
ipc_publisher_throughput_bytes_per_sec $(awk "BEGIN {printf \"%.2f\", ${pub_rate:-0} * 4096}")

# HELP ipc_subscribers_active Current number of active subscribers
# TYPE ipc_subscribers_active gauge
ipc_subscribers_active ${active_subs}

# HELP ipc_subscribers_created_total Total subscribers created
# TYPE ipc_subscribers_created_total counter
ipc_subscribers_created_total ${SUB_COUNTER}

# HELP ipc_subscribers_completed_total Total subscribers completed successfully
# TYPE ipc_subscribers_completed_total counter
ipc_subscribers_completed_total ${completed_subs}

# HELP ipc_subscribers_messages_received_total Total messages received by all subscribers
# TYPE ipc_subscribers_messages_received_total counter
ipc_subscribers_messages_received_total ${total_received}

# HELP ipc_subscribers_messages_lost_total Total messages lost
# TYPE ipc_subscribers_messages_lost_total counter
ipc_subscribers_messages_lost_total ${total_lost}

# HELP ipc_message_loss_rate_percent Message loss rate percentage
# TYPE ipc_message_loss_rate_percent gauge
ipc_message_loss_rate_percent ${loss_rate}

# HELP ipc_subscriber_fanout_ratio Average subscriber fanout (received/sent)
# TYPE ipc_subscriber_fanout_ratio gauge
ipc_subscriber_fanout_ratio $(awk "BEGIN {printf \"%.2f\", ${pub_sent:-1} > 0 ? ${total_received}/${pub_sent:-1} : 0}")

# HELP ipc_test_duration_target_seconds Target test duration
# TYPE ipc_test_duration_target_seconds gauge
ipc_test_duration_target_seconds ${TEST_DURATION}

# HELP ipc_test_completion_ratio Test completion ratio (0-1)
# TYPE ipc_test_completion_ratio gauge
ipc_test_completion_ratio $(awk "BEGIN {printf \"%.4f\", ${elapsed}/${TEST_DURATION}}")

# Timestamp
# scraped_at ${timestamp}
EOF
    
    log "✓ Prometheus 指标已导出到: ${PROM_METRICS_FILE}"
}

# ===== 统计函数 =====
print_stats() {
    local elapsed=$1
    local hours=$((elapsed / 3600))
    local minutes=$(((elapsed % 3600) / 60))
    local seconds=$((elapsed % 60))
    
    log ""
    log "========================================"
    log "运行统计"
    log "========================================"
    log "运行时长: ${hours}h ${minutes}m ${seconds}s"
    log "----------------------------------------"
    
    # 发布者统计
    if [ -f "${PUB_STATS}" ]; then
        local pub_sent=$(grep "^total_sent=" "${PUB_STATS}" 2>/dev/null | cut -d= -f2)
        local pub_errors=$(grep "^total_errors=" "${PUB_STATS}" 2>/dev/null | cut -d= -f2)
        local pub_rate=$(grep "^rate=" "${PUB_STATS}" 2>/dev/null | cut -d= -f2)
        log "发布者: PID=${PUB_PID}, 发送=${pub_sent:-0}, 错误=${pub_errors:-0}, 速率=${pub_rate:-0} msg/s"
    fi
    
    log "----------------------------------------"
    
    # 订阅者统计
    local active_subs=0
    local total_received=0
    local total_lost=0
    
    for pid in "${SUB_PIDS[@]}"; do
        if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
            ((active_subs++))
        fi
    done
    
    for stats_file in "${STATS_DIR}"/subscriber_*.stats; do
        if [ -f "${stats_file}" ]; then
            local received=$(grep "^total_received=" "${stats_file}" 2>/dev/null | cut -d= -f2)
            local lost=$(grep "^total_lost=" "${stats_file}" 2>/dev/null | cut -d= -f2)
            total_received=$((total_received + ${received:-0}))
            total_lost=$((total_lost + ${lost:-0}))
        fi
    done
    
    log "订阅者: 活跃=${active_subs}, 总创建=${SUB_COUNTER}, 总接收=${total_received}, 总丢失=${total_lost}"
    
    if [ $((total_received + total_lost)) -gt 0 ]; then
        local loss_rate=$(awk "BEGIN {printf \"%.4f\", (${total_lost} * 100.0) / (${total_received} + ${total_lost})}")
        log "丢失率: ${loss_rate}%"
    fi
    
    log "========================================"
    
    # 导出Prometheus指标
    export_prometheus_metrics ${elapsed}
}

print_final_stats() {
    log ""
    log "========================================"
    log "最终统计"
    log "========================================"
    
    # 汇总所有统计
    local total_received=0
    local total_lost=0
    local completed_subs=0
    
    for stats_file in "${STATS_DIR}"/subscriber_*.stats; do
        if [ -f "${stats_file}" ]; then
            local received=$(grep "^total_received=" "${stats_file}" 2>/dev/null | cut -d= -f2)
            local lost=$(grep "^total_lost=" "${stats_file}" 2>/dev/null | cut -d= -f2)
            total_received=$((total_received + ${received:-0}))
            total_lost=$((total_lost + ${lost:-0}))
            ((completed_subs++))
        fi
    done
    
    if [ -f "${PUB_STATS}" ]; then
        local pub_sent=$(grep "^total_sent=" "${PUB_STATS}" 2>/dev/null | cut -d= -f2)
        local pub_errors=$(grep "^total_errors=" "${PUB_STATS}" 2>/dev/null | cut -d= -f2)
        log "发布者: 发送=${pub_sent:-0}, 错误=${pub_errors:-0}"
    fi
    
    log "订阅者: 总创建=${SUB_COUNTER}, 完成=${completed_subs}, 总接收=${total_received}, 总丢失=${total_lost}"
    
    if [ $((total_received + total_lost)) -gt 0 ]; then
        local loss_rate=$(awk "BEGIN {printf \"%.4f\", (${total_lost} * 100.0) / (${total_received} + ${total_lost})}")
        log "最终丢失率: ${loss_rate}%"
    fi
    
    log "========================================"
    log "日志目录: ${LOG_DIR}"
    log "统计目录: ${STATS_DIR}"
    log "========================================"
}

# ===== 启动订阅者 =====
start_subscriber() {
    local sub_id=${SUB_COUNTER}
    ((SUB_COUNTER++))
    
    local stats_file="${STATS_DIR}/subscriber_${sub_id}.stats"
    local log_file="${LOG_DIR}/subscriber_${sub_id}.log"
    local lifetime=$((RANDOM % (MAX_SUB_LIFETIME - MIN_SUB_LIFETIME + 1) + MIN_SUB_LIFETIME))
    
    # 后台启动订阅者进程
    timeout "${lifetime}s" "${SUB_BIN}" "${SERVICE_NAME}" "${sub_id}" "${stats_file}" \
        >> "${log_file}" 2>&1 &
    
    local pid=$!
    SUB_PIDS+=($pid)
    SUB_STATS_FILES+=("${stats_file}")
    
    log "启动订阅者 ID=${sub_id}, PID=${pid}, 存活=${lifetime}秒"
}

# ===== 清理已退出的订阅者 =====
cleanup_dead_subs() {
    local new_pids=()
    for pid in "${SUB_PIDS[@]}"; do
        if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
            new_pids+=($pid)
        fi
    done
    SUB_PIDS=("${new_pids[@]}")
}

# ===== 主程序 =====
echo "========================================" | tee "${MAIN_LOG}"
echo "  SPMC 过夜压力测试" | tee -a "${MAIN_LOG}"
echo "========================================" | tee -a "${MAIN_LOG}"
echo "测试配置:" | tee -a "${MAIN_LOG}"
echo "  测试时长: $((TEST_DURATION / 3600))小时 ($((TEST_DURATION / 60))分钟)" | tee -a "${MAIN_LOG}"
echo "  最大订阅者数: ${MAX_SUBSCRIBERS}" | tee -a "${MAIN_LOG}"
echo "  发送速率: ${PUB_RATE} msg/s" | tee -a "${MAIN_LOG}"
echo "  订阅者存活时间: ${MIN_SUB_LIFETIME}-${MAX_SUB_LIFETIME} 秒" | tee -a "${MAIN_LOG}"
echo "  统计间隔: ${STATS_INTERVAL} 秒" | tee -a "${MAIN_LOG}"
echo "========================================" | tee -a "${MAIN_LOG}"
echo "" | tee -a "${MAIN_LOG}"
echo "按 Ctrl+C 提前终止测试" | tee -a "${MAIN_LOG}"
echo "" | tee -a "${MAIN_LOG}"

# 启动发布者
log "启动发布者进程..."
"${PUB_BIN}" "${SERVICE_NAME}" "${PUB_RATE}" "${PUB_STATS}" \
    >> "${LOG_DIR}/publisher.log" 2>&1 &
PUB_PID=$!
log "发布者 PID=${PUB_PID}"

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
        log "测试时长已达 $((TEST_DURATION / 3600)) 小时，准备退出..."
        break
    fi
    
    # 清理已退出的订阅者
    cleanup_dead_subs
    
    # 如果未达到最大数，启动新订阅者
    if [ ${#SUB_PIDS[@]} -lt ${MAX_SUBSCRIBERS} ]; then
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

# cleanup由trap自动调用
