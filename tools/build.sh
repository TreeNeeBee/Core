#!/bin/bash
# ==============================================================================
# Core Module - Standalone Build Script
# ==============================================================================
# This script helps build the Core module in standalone mode
# ==============================================================================

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Default values
BUILD_TYPE="Release"
BUILD_DIR="build"
ENABLE_TESTS="ON"
ENABLE_EXAMPLES="ON"
ENABLE_BENCHMARKS="ON"
CLEAN_BUILD=false
INSTALL=false
JOBS=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

# Script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# ==============================================================================
# Helper Functions
# ==============================================================================
print_header() {
    echo -e "${BLUE}=============================================================================${NC}"
    echo -e "${BLUE}$1${NC}"
    echo -e "${BLUE}=============================================================================${NC}"
}

print_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# ==============================================================================
# Usage
# ==============================================================================
usage() {
    cat << EOF
Usage: $0 [OPTIONS]

Options:
    -t, --type TYPE         Build type: Debug or Release (default: Release)
    -d, --dir DIR          Build directory (default: build)
    -j, --jobs N           Number of parallel jobs (default: auto-detected)
    -c, --clean            Clean build directory before building
    -i, --install          Install after building
    --no-tests             Disable building tests
    --no-examples          Disable building examples
    --no-benchmarks        Disable building benchmarks
    -h, --help             Show this help message

Examples:
    $0                              # Release build with tests, examples, benchmarks
    $0 -t Debug                     # Debug build
    $0 -c                           # Clean build
    $0 --no-tests --no-examples     # Build only library
    $0 -t Release -i                # Build and install

EOF
    exit 0
}

# ==============================================================================
# Parse Arguments
# ==============================================================================
while [[ $# -gt 0 ]]; do
    case $1 in
        -t|--type)
            BUILD_TYPE="$2"
            shift 2
            ;;
        -d|--dir)
            BUILD_DIR="$2"
            shift 2
            ;;
        -j|--jobs)
            JOBS="$2"
            shift 2
            ;;
        -c|--clean)
            CLEAN_BUILD=true
            shift
            ;;
        -i|--install)
            INSTALL=true
            shift
            ;;
        --no-tests)
            ENABLE_TESTS="OFF"
            shift
            ;;
        --no-examples)
            ENABLE_EXAMPLES="OFF"
            shift
            ;;
        --no-benchmarks)
            ENABLE_BENCHMARKS="OFF"
            shift
            ;;
        -h|--help)
            usage
            ;;
        *)
            print_error "Unknown option: $1"
            usage
            ;;
    esac
done

# ==============================================================================
# Main Build Process
# ==============================================================================
print_header "Core Module - Standalone Build"

# Check if BuildTemplate submodule exists
if [ ! -f "$SCRIPT_DIR/BuildTemplate/Config.cmake.in" ]; then
    print_warning "BuildTemplate submodule not initialized"
    print_info "Initializing submodule..."
    cd "$SCRIPT_DIR"
    git submodule update --init --recursive
    if [ $? -ne 0 ]; then
        print_error "Failed to initialize BuildTemplate submodule"
        exit 1
    fi
fi

# Clean build directory if requested
if [ "$CLEAN_BUILD" = true ]; then
    print_info "Cleaning build directory: $BUILD_DIR"
    rm -rf "$SCRIPT_DIR/$BUILD_DIR"
fi

# Create build directory
print_info "Creating build directory: $BUILD_DIR"
mkdir -p "$SCRIPT_DIR/$BUILD_DIR"
cd "$SCRIPT_DIR/$BUILD_DIR"

# Configure
print_header "Configuring CMake"
print_info "Build type: $BUILD_TYPE"
print_info "Tests: $ENABLE_TESTS"
print_info "Examples: $ENABLE_EXAMPLES"
print_info "Benchmarks: $ENABLE_BENCHMARKS"
print_info "Jobs: $JOBS"

cmake "$SCRIPT_DIR" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DENABLE_BUILD_UNITTEST="$ENABLE_TESTS" \
    -DENABLE_BUILD_EXAMPLES="$ENABLE_EXAMPLES" \
    -DENABLE_BUILD_BENCHMARKS="$ENABLE_BENCHMARKS"

if [ $? -ne 0 ]; then
    print_error "CMake configuration failed"
    exit 1
fi

# Build
print_header "Building Core Module"
cmake --build . -j"$JOBS"

if [ $? -ne 0 ]; then
    print_error "Build failed"
    exit 1
fi

# Run tests if enabled
if [ "$ENABLE_TESTS" = "ON" ]; then
    print_header "Running Tests"
    ctest --output-on-failure
    if [ $? -ne 0 ]; then
        print_warning "Some tests failed"
    else
        print_info "All tests passed!"
    fi
fi

# Install if requested
if [ "$INSTALL" = true ]; then
    print_header "Installing"
    sudo cmake --install .
    if [ $? -eq 0 ]; then
        print_info "Installation completed successfully"
    else
        print_error "Installation failed"
        exit 1
    fi
fi

# Summary
print_header "Build Summary"
echo -e "  Build type:    ${GREEN}$BUILD_TYPE${NC}"
echo -e "  Build dir:     ${GREEN}$BUILD_DIR${NC}"
echo -e "  Library:       ${GREEN}liblap_core.so${NC}"
if [ "$ENABLE_TESTS" = "ON" ]; then
    echo -e "  Tests:         ${GREEN}Enabled${NC}"
fi
if [ "$ENABLE_EXAMPLES" = "ON" ]; then
    echo -e "  Examples:      ${GREEN}Enabled${NC}"
fi
if [ "$ENABLE_BENCHMARKS" = "ON" ]; then
    echo -e "  Benchmarks:    ${GREEN}Enabled${NC}"
fi

print_info "Build completed successfully!"
print_info "Binaries are in: $SCRIPT_DIR/$BUILD_DIR"

if [ "$ENABLE_TESTS" = "ON" ]; then
    echo ""
    print_info "To run tests: cd $BUILD_DIR && ctest"
fi

if [ "$ENABLE_EXAMPLES" = "ON" ]; then
    echo ""
    print_info "To run examples: cd $BUILD_DIR && ./core_memory_example"
fi

echo ""
print_info "To install: $0 -i"
