#!/bin/bash
################################################################################
# Overnight Stress Test Runner
# Duration: 8 hours (28800 seconds)
# Date: 2025-12-29
################################################################################

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
LOG_DIR="${SCRIPT_DIR}/test_logs"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
LOG_FILE="${LOG_DIR}/overnight_stress_${TIMESTAMP}.log"

# Create log directory
mkdir -p "${LOG_DIR}"

echo "╔═══════════════════════════════════════════════════════════════════════════╗"
echo "║  Overnight Stress Test Runner                                            ║"
echo "║  Duration: 8 hours                                                       ║"
echo "║  Log file: ${LOG_FILE}"
echo "╚═══════════════════════════════════════════════════════════════════════════╝"
echo ""

# Check if binary exists
if [ ! -f "${BUILD_DIR}/overnight_stress_test" ]; then
    echo "[ERROR] overnight_stress_test binary not found!"
    echo "        Please run: cd ${BUILD_DIR} && cmake --build . --target overnight_stress_test"
    exit 1
fi

# Print system info
echo "[INFO] System Information:" | tee -a "${LOG_FILE}"
echo "  Hostname: $(hostname)" | tee -a "${LOG_FILE}"
echo "  CPU cores: $(nproc)" | tee -a "${LOG_FILE}"
echo "  Memory: $(free -h | grep Mem | awk '{print $2}')" | tee -a "${LOG_FILE}"
echo "  Date: $(date)" | tee -a "${LOG_FILE}"
echo "" | tee -a "${LOG_FILE}"

# Monitor memory usage in background
monitor_memory() {
    while true; do
        sleep 300  # Every 5 minutes
        echo "[$(date +'%Y-%m-%d %H:%M:%S')] Memory: $(free -h | grep Mem | awk '{print $3 "/" $2}')" >> "${LOG_FILE}"
    done
}

monitor_memory &
MONITOR_PID=$!

# Trap to cleanup on exit
cleanup() {
    echo "" | tee -a "${LOG_FILE}"
    echo "[INFO] Cleaning up..." | tee -a "${LOG_FILE}"
    kill $MONITOR_PID 2>/dev/null
    echo "[INFO] Test completed. Log saved to: ${LOG_FILE}" | tee -a "${LOG_FILE}"
}

trap cleanup EXIT INT TERM

# Run the test
echo "[INFO] Starting 8-hour stress test..." | tee -a "${LOG_FILE}"
echo "[INFO] You can monitor progress with: tail -f ${LOG_FILE}" | tee -a "${LOG_FILE}"
echo "[INFO] Or view summary reports every 5 minutes" | tee -a "${LOG_FILE}"
echo "" | tee -a "${LOG_FILE}"

cd "${BUILD_DIR}"
./overnight_stress_test 8 2>&1 | tee -a "${LOG_FILE}"

TEST_EXIT_CODE=$?

echo "" | tee -a "${LOG_FILE}"
echo "╔═══════════════════════════════════════════════════════════════════════════╗" | tee -a "${LOG_FILE}"
echo "║  Test Completed                                                          ║" | tee -a "${LOG_FILE}"
echo "╚═══════════════════════════════════════════════════════════════════════════╝" | tee -a "${LOG_FILE}"
echo "Exit code: ${TEST_EXIT_CODE}" | tee -a "${LOG_FILE}"
echo "Log file: ${LOG_FILE}" | tee -a "${LOG_FILE}"
echo "" | tee -a "${LOG_FILE}"

exit ${TEST_EXIT_CODE}
