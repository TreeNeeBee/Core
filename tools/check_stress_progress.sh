#!/bin/bash

# 查看4小时压测进度的脚本
echo "========================================"
echo "4小时压力测试进度检查"
echo "当前时间: $(date)"
echo "========================================"

# 检查进程状态
PROC_COUNT=$(ps aux | grep -E "devzero_stress_test" | grep -v grep | wc -l)
echo "运行进程数: $PROC_COUNT"

if [ $PROC_COUNT -gt 0 ]; then
    echo "进程状态: ✓ 运行中"
    
    # 查找最新的统计文件
    LATEST_STATS=$(ls -t stress_test_results/stress_test_*_stats.csv 2>/dev/null | head -1)
    if [ -f "$LATEST_STATS" ]; then
        echo ""
        echo "最新统计数据:"
        echo "----------------------------------------"
        # 显示表头
        head -1 "$LATEST_STATS"
        # 显示最后一行数据
        tail -1 "$LATEST_STATS"
        
        # 计算进度
        RUNTIME=$(tail -1 "$LATEST_STATS" | cut -d',' -f2)
        if [ ! -z "$RUNTIME" ] && [ "$RUNTIME" != "运行时长(s)" ]; then
            PROGRESS=$(echo "scale=2; $RUNTIME / 14400 * 100" | bc)
            REMAINING=$(echo "14400 - $RUNTIME" | bc)
            echo ""
            echo "测试进度: ${PROGRESS}% (已运行 ${RUNTIME}s / 14400s)"
            echo "剩余时间: ${REMAINING}s (~$(echo "$REMAINING / 3600" | bc)小时)"
        fi
    fi
    
    # 显示最近的统计摘要
    echo ""
    echo "最近5次统计记录:"
    echo "----------------------------------------"
    tail -6 "$LATEST_STATS" | head -5 | while IFS=',' read -r ts rt sent recv serr rerr srate rrate fanout; do
        if [ "$ts" != "时间戳" ]; then
            printf "%s | 运行:%ss | 发送:%s | 接收:%s | 扇出:%.2f\n" "$ts" "$rt" "$sent" "$recv" "$fanout"
        fi
    done
else
    echo "进程状态: ✗ 未运行"
fi

echo ""
echo "========================================"
echo "监控命令:"
echo "  实时日志: tail -f stress_test_results/stress_test_*.log"
echo "  查看CSV:  cat stress_test_results/stress_test_*_stats.csv"
echo "  本脚本:   ./check_stress_progress.sh"
echo "========================================"
