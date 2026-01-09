#!/bin/bash
# =============================================================================
# Prometheus 指标验证脚本
# =============================================================================
# 用途: 验证压力测试是否正确导出Prometheus指标
# 用法: ./verify_prometheus_metrics.sh
# =============================================================================

METRICS_FILE="/tmp/stress_test_metrics.prom"

echo "==================================="
echo "Prometheus 指标验证工具"
echo "==================================="
echo ""

# 检查文件是否存在
if [ ! -f "${METRICS_FILE}" ]; then
    echo "❌ 错误: 指标文件不存在"
    echo "   期望路径: ${METRICS_FILE}"
    echo ""
    echo "请先运行压力测试: ./overnight_stress_test.sh"
    exit 1
fi

echo "✓ 指标文件存在: ${METRICS_FILE}"
echo ""

# 检查文件最后修改时间
FILE_AGE=$(stat -c %Y "${METRICS_FILE}" 2>/dev/null || stat -f %m "${METRICS_FILE}" 2>/dev/null)
CURRENT_TIME=$(date +%s)
AGE_SECONDS=$((CURRENT_TIME - FILE_AGE))

echo "文件信息:"
echo "  最后更新: $(date -d @${FILE_AGE} '+%Y-%m-%d %H:%M:%S' 2>/dev/null || date -r ${FILE_AGE} '+%Y-%m-%d %H:%M:%S' 2>/dev/null)"
echo "  距今时间: ${AGE_SECONDS} 秒"
echo ""

if [ ${AGE_SECONDS} -gt 600 ]; then
    echo "⚠️  警告: 指标文件超过10分钟未更新，可能测试已结束"
    echo ""
fi

# 统计指标数量
TOTAL_METRICS=$(grep -c "^ipc_" "${METRICS_FILE}")
echo "✓ 发现 ${TOTAL_METRICS} 个指标"
echo ""

# 验证关键指标
echo "==================================="
echo "关键指标验证"
echo "==================================="

check_metric() {
    local metric_name=$1
    local metric_display=$2
    local value=$(grep "^${metric_name} " "${METRICS_FILE}" 2>/dev/null | awk '{print $2}')
    
    if [ -z "$value" ]; then
        echo "❌ ${metric_display}: 缺失"
        return 1
    else
        echo "✓ ${metric_display}: ${value}"
        return 0
    fi
}

ERRORS=0

# 测试信息
check_metric "ipc_stress_test_uptime_seconds" "测试运行时长" || ((ERRORS++))
check_metric "ipc_test_duration_target_seconds" "目标测试时长" || ((ERRORS++))
check_metric "ipc_test_completion_ratio" "测试完成比例" || ((ERRORS++))

echo ""

# 发布者指标
check_metric "ipc_publisher_messages_sent_total" "发送消息总数" || ((ERRORS++))
check_metric "ipc_publisher_errors_total" "发送错误总数" || ((ERRORS++))
check_metric "ipc_publisher_send_rate_msgs_per_sec" "发送速率" || ((ERRORS++))
check_metric "ipc_publisher_throughput_bytes_per_sec" "发送吞吐量" || ((ERRORS++))

echo ""

# 订阅者指标
check_metric "ipc_subscribers_active" "活跃订阅者数" || ((ERRORS++))
check_metric "ipc_subscribers_created_total" "创建订阅者总数" || ((ERRORS++))
check_metric "ipc_subscribers_completed_total" "完成订阅者数" || ((ERRORS++))
check_metric "ipc_subscribers_messages_received_total" "接收消息总数" || ((ERRORS++))
check_metric "ipc_subscribers_messages_lost_total" "丢失消息总数" || ((ERRORS++))

echo ""

# 质量指标
check_metric "ipc_message_loss_rate_percent" "消息丢失率" || ((ERRORS++))
check_metric "ipc_subscriber_fanout_ratio" "订阅者扇出比率" || ((ERRORS++))

echo ""
echo "==================================="

if [ ${ERRORS} -eq 0 ]; then
    echo "✅ 所有指标验证通过！"
else
    echo "❌ 发现 ${ERRORS} 个错误"
fi

echo "==================================="
echo ""

# 显示性能摘要
echo "==================================="
echo "性能摘要"
echo "==================================="

SEND_RATE=$(grep "^ipc_publisher_send_rate_msgs_per_sec" "${METRICS_FILE}" | awk '{print $2}')
LOSS_RATE=$(grep "^ipc_message_loss_rate_percent" "${METRICS_FILE}" | awk '{print $2}')
ACTIVE_SUBS=$(grep "^ipc_subscribers_active" "${METRICS_FILE}" | awk '{print $2}')
COMPLETION=$(grep "^ipc_test_completion_ratio" "${METRICS_FILE}" | awk '{printf "%.1f", $2 * 100}')
FANOUT=$(grep "^ipc_subscriber_fanout_ratio" "${METRICS_FILE}" | awk '{print $2}')
THROUGHPUT_MB=$(grep "^ipc_publisher_throughput_bytes_per_sec" "${METRICS_FILE}" | awk '{printf "%.2f", $2/1024/1024}')

echo "发送速率:     ${SEND_RATE} msg/s"
echo "丢失率:       ${LOSS_RATE}%"
echo "活跃订阅者:   ${ACTIVE_SUBS}"
echo "测试进度:     ${COMPLETION}%"
echo "扇出比率:     ${FANOUT}x"
echo "吞吐量:       ${THROUGHPUT_MB} MB/s"
echo ""

# 健康评估
echo "==================================="
echo "健康评估"
echo "==================================="

HEALTH_SCORE=100
HEALTH_ISSUES=()

# 检查丢失率
if (( $(echo "$LOSS_RATE > 0.01" | bc -l 2>/dev/null || echo 0) )); then
    HEALTH_SCORE=$((HEALTH_SCORE - 30))
    HEALTH_ISSUES+=("⚠️  消息丢失率超过0.01%")
fi

# 检查发送速率（假设目标是1000 msg/s）
if (( $(echo "$SEND_RATE < 100" | bc -l 2>/dev/null || [ ${SEND_RATE%.*} -lt 100 ] && echo 1 || echo 0) )); then
    HEALTH_SCORE=$((HEALTH_SCORE - 20))
    HEALTH_ISSUES+=("⚠️  发送速率过低 (<100 msg/s)")
fi

# 检查活跃订阅者
if [ "${ACTIVE_SUBS%.*}" -eq 0 ] 2>/dev/null; then
    HEALTH_SCORE=$((HEALTH_SCORE - 50))
    HEALTH_ISSUES+=("❌ 没有活跃订阅者")
fi

# 输出健康状态
if [ ${HEALTH_SCORE} -ge 90 ]; then
    echo "✅ 健康状态: 优秀 (${HEALTH_SCORE}/100)"
elif [ ${HEALTH_SCORE} -ge 70 ]; then
    echo "✓ 健康状态: 良好 (${HEALTH_SCORE}/100)"
elif [ ${HEALTH_SCORE} -ge 50 ]; then
    echo "⚠️  健康状态: 一般 (${HEALTH_SCORE}/100)"
else
    echo "❌ 健康状态: 差 (${HEALTH_SCORE}/100)"
fi

if [ ${#HEALTH_ISSUES[@]} -gt 0 ]; then
    echo ""
    echo "发现的问题:"
    for issue in "${HEALTH_ISSUES[@]}"; do
        echo "  $issue"
    done
fi

echo ""
echo "==================================="
echo "Prometheus 配置示例"
echo "==================================="
echo ""
echo "使用 node_exporter textfile collector:"
echo "  1. 确保 node_exporter 配置了 textfile 目录:"
echo "     node_exporter --collector.textfile.directory=/tmp"
echo ""
echo "  2. Prometheus 配置:"
echo "     scrape_configs:"
echo "       - job_name: 'node'"
echo "         static_configs:"
echo "           - targets: ['localhost:9100']"
echo ""
echo "  3. 查看指标:"
echo "     curl http://localhost:9100/metrics | grep ipc_"
echo ""

echo "==================================="
echo "Grafana 查询示例"
echo "==================================="
echo ""
echo "发送速率:"
echo "  ipc_publisher_send_rate_msgs_per_sec"
echo ""
echo "消息丢失率:"
echo "  ipc_message_loss_rate_percent"
echo ""
echo "活跃订阅者:"
echo "  ipc_subscribers_active"
echo ""
echo "总吞吐量 (MB/s):"
echo "  ipc_publisher_throughput_bytes_per_sec / 1024 / 1024"
echo ""

exit ${ERRORS}
