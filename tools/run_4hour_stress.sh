#!/bin/bash

# 4小时压测脚本
DURATION=14400  # 4 hours = 14400 seconds
NUM_SUBS=10
MSG_SIZE=4096
SEND_RATE=1000  # 1000 msg/s

# 创建结果目录
RESULT_DIR="stress_test_results"
mkdir -p "$RESULT_DIR"

# 生成时间戳
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
LOG_FILE="$RESULT_DIR/stress_test_${TIMESTAMP}.log"
STATS_FILE="$RESULT_DIR/stress_test_${TIMESTAMP}_stats.csv"

echo "========================================" | tee -a "$LOG_FILE"
echo "4小时压力测试开始" | tee -a "$LOG_FILE"
echo "开始时间: $(date)" | tee -a "$LOG_FILE"
echo "测试时长: $DURATION 秒 (4小时)" | tee -a "$LOG_FILE"
echo "订阅者数: $NUM_SUBS" | tee -a "$LOG_FILE"
echo "消息大小: $MSG_SIZE 字节" | tee -a "$LOG_FILE"
echo "发送速率: $SEND_RATE msg/s" | tee -a "$LOG_FILE"
echo "日志文件: $LOG_FILE" | tee -a "$LOG_FILE"
echo "统计文件: $STATS_FILE" | tee -a "$LOG_FILE"
echo "========================================" | tee -a "$LOG_FILE"

# 创建CSV表头
echo "时间戳,运行时长(s),发送消息,接收消息,发送错误,接收错误,发送速率(msg/s),接收速率(msg/s),扇出比率" > "$STATS_FILE"

# 启动测试并实时保存输出
./run_devzero_multiproc.sh $DURATION $NUM_SUBS $MSG_SIZE $SEND_RATE 2>&1 | tee -a "$LOG_FILE" | while IFS= read -r line; do
    # 解析运行统计并保存到CSV
    if [[ "$line" =~ "=== 运行统计 ["([0-9]+)"s] ===" ]]; then
        runtime="${BASH_REMATCH[1]}"
        timestamp=$(date +"%Y-%m-%d %H:%M:%S")
        # 读取接下来的几行获取数据
        read -r line2  # 发送: xxx (xxx msg/s)
        read -r line3  # 接收: xxx (xxx msg/s)
        read -r line4  # 发送错误: xxx
        read -r line5  # 接收错误: xxx
        read -r line6  # 扇出比率: xxx
        
        sent=$(echo "$line2" | grep -oP '发送: \K[0-9]+')
        recv=$(echo "$line3" | grep -oP '接收: \K[0-9]+')
        send_err=$(echo "$line4" | grep -oP '发送错误: \K[0-9]+')
        recv_err=$(echo "$line5" | grep -oP '接收错误: \K[0-9]+')
        fanout=$(echo "$line6" | grep -oP '扇出比率: \K[0-9.]+')
        send_rate=$(echo "$line2" | grep -oP '\(\K[0-9]+(?= msg/s\))')
        recv_rate=$(echo "$line3" | grep -oP '\(\K[0-9]+(?= msg/s\))')
        
        echo "$timestamp,$runtime,$sent,$recv,$send_err,$recv_err,$send_rate,$recv_rate,$fanout" >> "$STATS_FILE"
    fi
done

echo "" | tee -a "$LOG_FILE"
echo "========================================" | tee -a "$LOG_FILE"
echo "4小时压力测试完成" | tee -a "$LOG_FILE"
echo "结束时间: $(date)" | tee -a "$LOG_FILE"
echo "日志已保存到: $LOG_FILE" | tee -a "$LOG_FILE"
echo "统计已保存到: $STATS_FILE" | tee -a "$LOG_FILE"
echo "========================================" | tee -a "$LOG_FILE"

# 生成简要报告
echo "" | tee -a "$LOG_FILE"
echo "统计摘要:" | tee -a "$LOG_FILE"
tail -1 "$STATS_FILE" | awk -F',' '{
    printf "  最终发送: %s 消息\n", $3
    printf "  最终接收: %s 消息\n", $4
    printf "  发送错误: %s\n", $5
    printf "  接收错误: %s\n", $6
    printf "  平均发送速率: %s msg/s\n", $7
    printf "  平均接收速率: %s msg/s\n", $8
    printf "  扇出比率: %s\n", $9
}' | tee -a "$LOG_FILE"

echo "" | tee -a "$LOG_FILE"
echo "完整日志: cat $LOG_FILE" | tee -a "$LOG_FILE"
echo "统计数据: cat $STATS_FILE" | tee -a "$LOG_FILE"
