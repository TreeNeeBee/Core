# Prometheus 集成指南

## 概述

压力测试脚本现在每5分钟生成一次Prometheus格式的指标文件，可以被Prometheus的`node_exporter`的textfile collector收集。

## 指标文件位置

```bash
/tmp/stress_test_metrics.prom
```

## 可用指标

### 测试信息
- `ipc_stress_test_info` - 测试基本信息（service, test_id, message_size）
- `ipc_stress_test_uptime_seconds` - 测试运行时长（秒）
- `ipc_test_duration_target_seconds` - 目标测试时长
- `ipc_test_completion_ratio` - 测试完成度（0-1）

### 发布者指标
- `ipc_publisher_messages_sent_total` - 总发送消息数（counter）
- `ipc_publisher_errors_total` - 总发送错误数（counter）
- `ipc_publisher_send_rate_msgs_per_sec` - 当前发送速率（msg/s）
- `ipc_publisher_throughput_bytes_per_sec` - 数据吞吐量（bytes/s）

### 订阅者指标
- `ipc_subscribers_active` - 当前活跃订阅者数
- `ipc_subscribers_created_total` - 总创建订阅者数（counter）
- `ipc_subscribers_completed_total` - 成功完成的订阅者数（counter）
- `ipc_subscribers_messages_received_total` - 总接收消息数（counter）
- `ipc_subscribers_messages_lost_total` - 总丢失消息数（counter）

### 质量指标
- `ipc_message_loss_rate_percent` - 消息丢失率（%）
- `ipc_subscriber_fanout_ratio` - 订阅者扇出比（received/sent）

## Prometheus 配置

### 方法1: Node Exporter Textfile Collector

1. **配置 node_exporter**:
```bash
node_exporter --collector.textfile.directory=/tmp
```

2. **Prometheus 配置** (`prometheus.yml`):
```yaml
scrape_configs:
  - job_name: 'ipc_stress_test'
    static_configs:
      - targets: ['localhost:9100']
    scrape_interval: 30s
```

### 方法2: 直接从文件读取（适合测试）

创建一个简单的HTTP服务器暴露指标：

```bash
#!/bin/bash
# serve_metrics.sh
while true; do
  echo -e "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\n$(cat /tmp/stress_test_metrics.prom)" | nc -l -p 9101 -q 1
done
```

然后配置Prometheus：
```yaml
scrape_configs:
  - job_name: 'ipc_stress_test_direct'
    static_configs:
      - targets: ['localhost:9101']
    scrape_interval: 30s
```

## 查看指标

### 命令行查看
```bash
# 查看当前指标
cat /tmp/stress_test_metrics.prom

# 实时监控
watch -n 5 cat /tmp/stress_test_metrics.prom
```

### Prometheus UI
访问 `http://localhost:9090/graph` 并查询：

```promql
# 发送速率
ipc_publisher_send_rate_msgs_per_sec

# 消息丢失率
ipc_message_loss_rate_percent

# 活跃订阅者数
ipc_subscribers_active

# 总吞吐量（所有订阅者）
rate(ipc_subscribers_messages_received_total[5m])
```

## Grafana 仪表板

### 推荐面板

**1. 性能概览**
```promql
# 发送速率
ipc_publisher_send_rate_msgs_per_sec

# 接收速率
rate(ipc_subscribers_messages_received_total[5m])

# 数据吞吐量
ipc_publisher_throughput_bytes_per_sec / 1024 / 1024  # MB/s
```

**2. 可靠性监控**
```promql
# 消息丢失率
ipc_message_loss_rate_percent

# 发送错误
rate(ipc_publisher_errors_total[5m])

# 订阅者扇出
ipc_subscriber_fanout_ratio
```

**3. 资源监控**
```promql
# 活跃订阅者
ipc_subscribers_active

# 订阅者创建速率
rate(ipc_subscribers_created_total[5m])

# 测试进度
ipc_test_completion_ratio * 100  # %
```

### 示例仪表板JSON

```json
{
  "dashboard": {
    "title": "IPC Stress Test",
    "panels": [
      {
        "title": "发送速率",
        "targets": [
          {
            "expr": "ipc_publisher_send_rate_msgs_per_sec"
          }
        ],
        "type": "graph"
      },
      {
        "title": "消息丢失率",
        "targets": [
          {
            "expr": "ipc_message_loss_rate_percent"
          }
        ],
        "type": "singlestat",
        "thresholds": "0.01,0.1"
      },
      {
        "title": "活跃订阅者",
        "targets": [
          {
            "expr": "ipc_subscribers_active"
          }
        ],
        "type": "graph"
      }
    ]
  }
}
```

## 告警规则

创建 `alert_rules.yml`:

```yaml
groups:
  - name: ipc_stress_test
    interval: 30s
    rules:
      # 消息丢失告警
      - alert: MessageLoss
        expr: ipc_message_loss_rate_percent > 0.01
        for: 1m
        labels:
          severity: critical
        annotations:
          summary: "IPC消息丢失"
          description: "消息丢失率 {{ $value }}% 超过阈值 0.01%"

      # 发送错误告警
      - alert: SendErrors
        expr: rate(ipc_publisher_errors_total[5m]) > 0
        for: 1m
        labels:
          severity: warning
        annotations:
          summary: "IPC发送错误"
          description: "检测到发送错误，速率 {{ $value }} errors/s"

      # 性能下降告警
      - alert: LowThroughput
        expr: ipc_publisher_send_rate_msgs_per_sec < 400
        for: 5m
        labels:
          severity: warning
        annotations:
          summary: "IPC吞吐量过低"
          description: "发送速率 {{ $value }} msg/s 低于预期 500 msg/s"

      # 订阅者数量异常
      - alert: LowSubscriberCount
        expr: ipc_subscribers_active < 30
        for: 5m
        labels:
          severity: warning
        annotations:
          summary: "订阅者数量过低"
          description: "活跃订阅者 {{ $value }} 个，低于预期平均值"
```

## 数据导出

### 导出到CSV
```bash
# 创建CSV导出脚本
cat > export_metrics.sh << 'EOF'
#!/bin/bash
echo "timestamp,sent,received,lost,loss_rate,active_subs,rate" > /tmp/metrics.csv
while true; do
  if [ -f /tmp/stress_test_metrics.prom ]; then
    ts=$(date +%s)
    sent=$(grep "^ipc_publisher_messages_sent_total" /tmp/stress_test_metrics.prom | awk '{print $2}')
    received=$(grep "^ipc_subscribers_messages_received_total" /tmp/stress_test_metrics.prom | awk '{print $2}')
    lost=$(grep "^ipc_subscribers_messages_lost_total" /tmp/stress_test_metrics.prom | awk '{print $2}')
    loss=$(grep "^ipc_message_loss_rate_percent" /tmp/stress_test_metrics.prom | awk '{print $2}')
    subs=$(grep "^ipc_subscribers_active" /tmp/stress_test_metrics.prom | awk '{print $2}')
    rate=$(grep "^ipc_publisher_send_rate_msgs_per_sec" /tmp/stress_test_metrics.prom | awk '{print $2}')
    echo "${ts},${sent},${received},${lost},${loss},${subs},${rate}" >> /tmp/metrics.csv
  fi
  sleep 60
done
EOF
chmod +x export_metrics.sh
```

## 测试验证

```bash
# 启动压力测试
./overnight_stress_test.sh 3600 100 1000

# 等待第一次统计（5分钟）
sleep 300

# 查看Prometheus指标
cat /tmp/stress_test_metrics.prom

# 验证指标格式
curl -s http://localhost:9090/api/v1/query?query=ipc_publisher_send_rate_msgs_per_sec
```

## 故障排查

### 指标文件不存在
```bash
# 检查测试是否在运行
ps aux | grep overnight_stress_test

# 检查日志
tail -f /tmp/overnight_logs_*/main.log
```

### Prometheus无法采集
```bash
# 检查文件权限
ls -la /tmp/stress_test_metrics.prom

# 检查node_exporter配置
curl localhost:9100/metrics | grep ipc_
```

### 指标值异常
```bash
# 对比统计文件
cat /tmp/overnight_stats_*/publisher.stats

# 检查进程状态
ps aux | grep -E "publisher_process|subscriber_process"
```

## 最佳实践

1. **采集频率**: 建议Prometheus scrape_interval设置为30-60秒
2. **保留时间**: 建议至少保留24小时数据用于分析
3. **告警阈值**: 根据实际业务需求调整告警阈值
4. **可视化**: 使用Grafana创建实时监控仪表板
5. **数据导出**: 定期导出关键指标用于长期分析

## 相关链接

- [Prometheus文档](https://prometheus.io/docs/)
- [Node Exporter](https://github.com/prometheus/node_exporter)
- [Grafana仪表板](https://grafana.com/docs/)
- [压力测试使用指南](README_STRESS_TESTS.md)
