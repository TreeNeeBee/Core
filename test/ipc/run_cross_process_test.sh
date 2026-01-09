#!/bin/bash
# Cross-process IPC test runner

set -e

echo "============================================================"
echo "  LightAP IPC Cross-Process Communication Test"
echo "============================================================"
echo ""

# Build executables
echo "[1] Building publisher and subscriber processes..."
make clean > /dev/null 2>&1
make pub sub > /dev/null 2>&1
echo "    ✓ Build complete"
echo ""

# Clean shared memory
echo "[2] Cleaning shared memory..."
rm -f /dev/shm/lightap_ipc_* 2>/dev/null || true
rm -f /dev/shm/lap_ipc_* 2>/dev/null || true
# Use shm_unlink as well
for shm in /dev/shm/lightap_ipc_* /dev/shm/lap_ipc_*; do
    if [ -e "$shm" ]; then
        rm -f "$shm" 2>/dev/null || true
    fi
done
sleep 0.1  # Give time for cleanup
echo "    ✓ Shared memory cleaned"
echo ""

# Test parameters
NUM_MESSAGES=20
INTERVAL_MS=50
NUM_SUBSCRIBERS=2

echo "[3] Test Configuration:"
echo "    Messages to send: $NUM_MESSAGES"
echo "    Interval: ${INTERVAL_MS}ms"
echo "    Subscribers: $NUM_SUBSCRIBERS"
echo ""

# Start publisher first to create shared memory
echo "[4] Starting publisher (to create shared memory)..."
./bin/publisher_process $NUM_MESSAGES $INTERVAL_MS > /tmp/publisher.log 2>&1 &
PUB_PID=$!
echo "    ✓ Publisher started (PID: $PUB_PID)"
echo ""

# Wait for publisher to create shared memory
echo "[5] Waiting for shared memory creation..."
for i in {1..10}; do
    if [ -f /dev/shm/lightap_ipc_sensor_data ]; then
        echo "    ✓ Shared memory created"
        break
    fi
    sleep 0.1
done
echo ""

# Start subscribers
echo "[6] Starting $NUM_SUBSCRIBERS subscribers..."
for i in $(seq 0 $((NUM_SUBSCRIBERS-1))); do
    ./bin/subscriber_process $i $NUM_MESSAGES > /tmp/subscriber_${i}.log 2>&1 &
    SUB_PID[$i]=$!
    echo "    ✓ Subscriber $i started (PID: ${SUB_PID[$i]})"
done
echo ""

# Wait for publisher to finish
echo "[7] Waiting for test completion..."
wait $PUB_PID
echo "    ✓ Publisher finished"

# Wait a bit for subscribers to process remaining messages
sleep 1

# Stop subscribers
echo ""
echo "[8] Stopping subscribers..."
for i in $(seq 0 $((NUM_SUBSCRIBERS-1))); do
    if kill -0 ${SUB_PID[$i]} 2>/dev/null; then
        kill -INT ${SUB_PID[$i]} 2>/dev/null || true
    fi
done

# Wait for subscribers to exit
sleep 1

echo "    ✓ All processes stopped"
echo ""

# Show results
echo "============================================================"
echo "  Test Results"
echo "============================================================"
echo ""

echo "Publisher Output:"
echo "----------------"
cat /tmp/publisher.log
echo ""

for i in $(seq 0 $((NUM_SUBSCRIBERS-1))); do
    echo "Subscriber $i Output:"
    echo "--------------------"
    cat /tmp/subscriber_${i}.log
    echo ""
done

# Analyze results
echo "============================================================"
echo "  Analysis"
echo "============================================================"

# Extract statistics
PUB_SENT=$(grep "Total sent:" /tmp/publisher.log | awk '{print $3}' | cut -d'/' -f1)
PUB_TOTAL=$(grep "Total sent:" /tmp/publisher.log | awk '{print $3}' | cut -d'/' -f2)

echo ""
echo "Publisher:"
echo "  Messages sent: $PUB_SENT / $PUB_TOTAL"

for i in $(seq 0 $((NUM_SUBSCRIBERS-1))); do
    SUB_RCV=$(grep "Total received:" /tmp/subscriber_${i}.log | awk '{print $3}')
    SUB_MISSED=$(grep "Missed messages:" /tmp/subscriber_${i}.log | awk '{print $3}')
    echo ""
    echo "Subscriber $i:"
    echo "  Messages received: $SUB_RCV"
    echo "  Missed messages: $SUB_MISSED"
done

echo ""
echo "============================================================"
echo "  Test Complete"
echo "============================================================"

# Cleanup logs
rm -f /tmp/publisher.log /tmp/subscriber_*.log

exit 0
