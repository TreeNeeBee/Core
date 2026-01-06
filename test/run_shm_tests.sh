#!/bin/bash
# ==============================================================================
# run_shm_tests.sh - Comprehensive test suite for SharedMemoryAllocator
# ==============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/../build"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Test result tracking
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0

print_header() {
    echo -e "${BLUE}========================================${NC}"
    echo -e "${BLUE}$1${NC}"
    echo -e "${BLUE}========================================${NC}"
}

print_success() {
    echo -e "${GREEN}✅ $1${NC}"
}

print_error() {
    echo -e "${RED}❌ $1${NC}"
}

print_info() {
    echo -e "${YELLOW}ℹ️  $1${NC}"
}

run_test() {
    local test_name=$1
    local test_binary=$2
    local description=$3
    
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    
    print_header "Test $TOTAL_TESTS: $description"
    echo "Running: $test_binary"
    echo ""
    
    if [ -f "$test_binary" ]; then
        if "$test_binary"; then
            PASSED_TESTS=$((PASSED_TESTS + 1))
            print_success "$test_name PASSED"
            echo ""
            return 0
        else
            FAILED_TESTS=$((FAILED_TESTS + 1))
            print_error "$test_name FAILED"
            echo ""
            return 1
        fi
    else
        print_error "Test binary not found: $test_binary"
        FAILED_TESTS=$((FAILED_TESTS + 1))
        echo ""
        return 1
    fi
}

# ==============================================================================
# Main Test Execution
# ==============================================================================

print_header "SharedMemoryAllocator Test Suite"
echo "Build directory: $BUILD_DIR"
echo ""

# Change to build directory
cd "$BUILD_DIR"

# Run all SharedMemoryAllocator tests
echo -e "${YELLOW}===========================================\n${NC}"
echo -e "${YELLOW}UNIT TESTS\n${NC}"
echo -e "${YELLOW}===========================================\n${NC}"

run_test "gtest_shm" \
    "./core_test" \
    "GTest Unit Tests (test_shm_allocator.cpp)" || true

echo -e "${YELLOW}===========================================\n${NC}"
echo -e "${YELLOW}FUNCTIONAL TESTS\n${NC}"
echo -e "${YELLOW}===========================================\n${NC}"

run_test "iceoryx2_api" \
    "./test_iceoryx2_api" \
    "iceoryx2 Publisher/Subscriber API Tests" || true

run_test "message_queue" \
    "./test_message_queue" \
    "MessageQueue FIFO and Round-Robin Tests" || true

run_test "integration" \
    "./test_shm_integration" \
    "Integration Tests (SOA, Request-Response, Multi-topic)" || true

echo -e "${YELLOW}===========================================\n${NC}"
echo -e "${YELLOW}BENCHMARKS\n${NC}"
echo -e "${YELLOW}===========================================\n${NC}"

run_test "benchmark" \
    "./benchmark_allocators" \
    "Performance Benchmark (malloc vs SHM)" || true

# ==============================================================================
# Test Summary
# ==============================================================================

echo ""
print_header "TEST SUMMARY"
echo "Total Tests:  $TOTAL_TESTS"
echo -e "Passed:       ${GREEN}$PASSED_TESTS${NC}"
echo -e "Failed:       ${RED}$FAILED_TESTS${NC}"
echo ""

if [ $FAILED_TESTS -eq 0 ]; then
    print_success "ALL TESTS PASSED! 🎉"
    echo ""
    exit 0
else
    print_error "SOME TESTS FAILED!"
    echo ""
    exit 1
fi
