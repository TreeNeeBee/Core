#!/bin/bash
echo "════════════════════════════════════════════════════════════════"
echo "  Overnight Test Quick Status"
echo "════════════════════════════════════════════════════════════════"
echo ""

# Check if running
if ps aux | grep overnight_stress_test | grep -v grep > /dev/null; then
    echo "✅ Test Status: RUNNING"
    echo ""
    ps aux | grep overnight_stress_test | grep -v grep | awk '{print "  PID: "$2", CPU: "$3"%, MEM: "$4"%, Runtime: "$10}'
else
    echo "❌ Test Status: NOT RUNNING"
    exit 1
fi

echo ""
echo "────────────────────────────────────────────────────────────────"
echo "  Latest Statistics"
echo "────────────────────────────────────────────────────────────────"

# Find latest log
LOG=$(ls -t test_logs/overnight_stress_*.log 2>/dev/null | head -1)

if [ -n "$LOG" ]; then
    echo "Log: $LOG"
    echo ""
    # Show last summary
    grep -A 20 "Iteration" "$LOG" | tail -25
else
    echo "No log file found"
fi

echo ""
echo "════════════════════════════════════════════════════════════════"
echo ""
echo "Commands:"
echo "  Monitor live: ./monitor_overnight_test.sh"
echo "  View log:     tail -f $LOG"
echo "  Stop test:    pkill overnight_stress_test"
echo ""
