#!/bin/bash
# Core Module - Memory Management Test Suite
# Run all memory-related tests

set -e

BUILD_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../build" && pwd)"
cd "$BUILD_DIR"

echo "======================================"
echo "Core Memory Management Test Suite"
echo "======================================"
echo

# Build all targets
echo "Building test programs..."
cmake --build . --target core_memory_example global_operator_test memory_stress_test -j
echo "✓ Build completed"
echo

# Test 1: Basic memory example
echo "======================================"
echo "Test 1: Basic Memory Example"
echo "======================================"
./modules/Core/core_memory_example
echo "✓ Test 1 passed"
echo

# Test 2: Global operator test
echo "======================================"
echo "Test 2: Global Operator Test"
echo "======================================"
./modules/Core/global_operator_test
echo "✓ Test 2 passed"
echo

# Test 3: Stress test
echo "======================================"
echo "Test 3: Multi-thread Stress Test"
echo "======================================"
./modules/Core/memory_stress_test
echo "✓ Test 3 passed"
echo

echo "======================================"
echo "All tests completed successfully! ✓"
echo "======================================"
