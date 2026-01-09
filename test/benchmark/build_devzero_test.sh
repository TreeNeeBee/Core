#!/bin/bash
# Build devzero_stress_test

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/../../../../build"

echo "========================================="
echo "Building devzero_stress_test"
echo "========================================="

# Check if build directory exists
if [ ! -d "${BUILD_DIR}" ]; then
    echo "Error: Build directory not found: ${BUILD_DIR}"
    echo "Please run cmake from workspace root first:"
    echo "  cd /workspace/LightAP/build"
    echo "  cmake .."
    exit 1
fi

cd "${BUILD_DIR}"

# Compile the test
echo "Compiling devzero_stress_test.cpp..."

g++ -std=c++17 -O2 \
    -I../modules/Core/source/inc \
    -I../modules/Core/source/inc/ipc \
    -I../modules/Core/source/inc/utils \
    -pthread \
    ../modules/Core/test/benchmark/devzero_stress_test.cpp \
    -L./modules/Core \
    -llap_core \
    -Wl,-rpath,./modules/Core \
    -lrt \
    -o ./modules/Core/devzero_stress_test

if [ $? -eq 0 ]; then
    echo ""
    echo "========================================="
    echo "Build successful!"
    echo "========================================="
    echo "Executable: ${BUILD_DIR}/modules/Core/devzero_stress_test"
    echo ""
    echo "Usage:"
    echo "  ./modules/Core/devzero_stress_test <duration_sec> <num_subs> <msg_size> [rate]"
    echo ""
    echo "Examples:"
    echo "  ./modules/Core/devzero_stress_test 60 10 4096       # 60s, 10 subs, 4KB"
    echo "  ./modules/Core/devzero_stress_test 300 20 8192 1000 # 5min, 20 subs, 8KB, 1000 msg/s"
else
    echo ""
    echo "Build failed!"
    exit 1
fi
