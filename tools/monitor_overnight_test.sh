#!/bin/bash
################################################################################
# Overnight Test Monitor
# Shows real-time progress of the 8-hour stress test
################################################################################

LOG_DIR="/workspace/LightAP/modules/Core/test_logs"
LATEST_LOG=$(ls -t ${LOG_DIR}/overnight_stress_*.log 2>/dev/null | head -1)

if [ -z "$LATEST_LOG" ]; then
    echo "[ERROR] No overnight test log found in ${LOG_DIR}"
    exit 1
fi

echo "╔═══════════════════════════════════════════════════════════════════════════╗"
echo "║  Overnight Test Monitor                                                  ║"
echo "╚═══════════════════════════════════════════════════════════════════════════╝"
echo ""
echo "Log file: ${LATEST_LOG}"
echo ""
echo "Press Ctrl+C to exit monitor (test will continue running)"
echo ""
echo "═══════════════════════════════════════════════════════════════════════════"
echo ""

# Check if test is running
if ! ps aux | grep overnight_stress_test | grep -v grep > /dev/null; then
    echo "[WARNING] overnight_stress_test process not found!"
    echo "[INFO] Showing last 100 lines of log:"
    echo ""
    tail -100 "${LATEST_LOG}"
    exit 1
fi

# Show process info
echo "[PROCESS INFO]"
ps aux | grep overnight_stress_test | grep -v grep | awk '{print "  PID: "$2", CPU: "$3"%, MEM: "$4"%, Runtime: "$10}'
echo ""
echo "═══════════════════════════════════════════════════════════════════════════"
echo ""

# Follow log with summary extraction
tail -f "${LATEST_LOG}" | grep --line-buffered -E "(PHASE|Iteration|Operations|Errors|Throughput|PASSED|FAILED|Runtime|msg/sec|ops/sec)"
