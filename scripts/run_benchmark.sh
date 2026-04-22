#!/bin/bash
#
# ACC Benchmark Runner
# Usage: ./run_benchmark.sh <candidate_directory> [build_directory]
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
CANDIDATE_DIR="${1:-}"
BUILD_DIR_INPUT="${2:-}"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

print_header() {
    echo ""
    echo "======================================"
    echo " $1"
    echo "======================================"
}

print_pass() {
    echo -e "${GREEN}✓ PASS:${NC} $1"
}

print_fail() {
    echo -e "${RED}✗ FAIL:${NC} $1"
}

print_warn() {
    echo -e "${YELLOW}⚠ WARN:${NC} $1"
}

usage() {
    echo "Usage: $0 <candidate_directory> [build_directory]"
    echo ""
    echo "The candidate directory should contain:"
    echo "  include/acc_controller.h"
    echo "  src/acc_controller.c"
    echo "  test/test_acc_controller.cpp (optional)"
}

read_cache_value() {
    local key="$1"
    local cache_file="$2"
    sed -n "s#^${key}:[^=]*=##p" "$cache_file" | head -n 1
}

# Check arguments
if [ -z "$CANDIDATE_DIR" ]; then
    usage
    exit 1
fi

if [ ! -d "$CANDIDATE_DIR" ]; then
    echo "Error: Directory '$CANDIDATE_DIR' does not exist"
    exit 1
fi

# Convert to absolute path
CANDIDATE_DIR="$(cd "$CANDIDATE_DIR" && pwd)"

if [ -n "$BUILD_DIR_INPUT" ]; then
    mkdir -p "$BUILD_DIR_INPUT"
    BUILD_DIR="$(cd "$BUILD_DIR_INPUT" && pwd)"
else
    BUILD_DIR="$CANDIDATE_DIR/build"
fi

if [ "$BUILD_DIR" = "$CANDIDATE_DIR" ]; then
    echo "Error: Build directory must be different from candidate directory"
    exit 1
fi

print_header "ACC Benchmark Evaluation"
echo "Candidate: $CANDIDATE_DIR"
echo "Build dir: $BUILD_DIR"
echo "Date: $(date)"

# Check required files
print_header "File Check"

SCORE=0
MAX_SCORE=200

check_file() {
    local file="$1"
    local points="$2"
    if [ -f "$CANDIDATE_DIR/$file" ]; then
        print_pass "$file exists"
        return 0
    else
        print_fail "$file not found (-$points pts)"
        return 1
    fi
}

check_file "include/acc_controller.h" 10 || true
check_file "src/acc_controller.c" 30 || true
check_file "test/test_acc_controller.cpp" 10 || true

# Build candidate code
print_header "Build Check"

rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

# Create temporary CMakeLists.txt for candidate
cat > "$BUILD_DIR/CMakeLists.txt" << 'EOF'
cmake_minimum_required(VERSION 3.16)
project(CandidateACC LANGUAGES C CXX)

enable_testing()

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(CheckLibraryExists)

# Candidate library
add_library(acc_controller STATIC
    ${CANDIDATE_SRC}/src/acc_controller.c
)
target_include_directories(acc_controller PUBLIC
    ${CANDIDATE_SRC}/include
)
check_library_exists(m roundf "" ACC_CONTROLLER_HAVE_LIBM)
if(ACC_CONTROLLER_HAVE_LIBM)
    target_link_libraries(acc_controller PRIVATE m)
endif()

# Google Test
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.15.0.zip
)
if(WIN32)
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
endif()
FetchContent_MakeAvailable(googletest)

# Reference tests (from benchmark project)
if(EXISTS "${REFERENCE_TEST}")
    add_executable(test_reference ${REFERENCE_TEST})
    target_link_libraries(test_reference PRIVATE acc_controller gtest_main)
    target_include_directories(test_reference PRIVATE ${CANDIDATE_SRC}/include)
    include(GoogleTest)
    gtest_discover_tests(test_reference)
endif()

# Candidate tests (if provided)
if(EXISTS "${CANDIDATE_SRC}/test/test_acc_controller.cpp")
    add_executable(test_candidate ${CANDIDATE_SRC}/test/test_acc_controller.cpp)
    target_link_libraries(test_candidate PRIVATE acc_controller gtest_main)
    target_include_directories(test_candidate PRIVATE ${CANDIDATE_SRC}/include)
    include(GoogleTest)
    gtest_discover_tests(test_candidate)
endif()
EOF

# Configure and build
cd "$BUILD_DIR"

BUILD_SUCCESS=true
GENERATOR_ARGS=()
REFERENCE_CACHE="$PROJECT_ROOT/build/CMakeCache.txt"
if [ -f "$REFERENCE_CACHE" ]; then
    REF_GENERATOR="$(read_cache_value CMAKE_GENERATOR "$REFERENCE_CACHE")"
    REF_C_COMPILER="$(read_cache_value CMAKE_C_COMPILER "$REFERENCE_CACHE")"
    REF_CXX_COMPILER="$(read_cache_value CMAKE_CXX_COMPILER "$REFERENCE_CACHE")"
    REF_MAKE_PROGRAM="$(read_cache_value CMAKE_MAKE_PROGRAM "$REFERENCE_CACHE")"
    REF_PLATFORM="$(read_cache_value CMAKE_GENERATOR_PLATFORM "$REFERENCE_CACHE")"
    REF_TOOLSET="$(read_cache_value CMAKE_GENERATOR_TOOLSET "$REFERENCE_CACHE")"

    if [ -n "$REF_GENERATOR" ]; then
        GENERATOR_ARGS+=(-G "$REF_GENERATOR")
    fi
    if [ -n "$REF_PLATFORM" ]; then
        GENERATOR_ARGS+=(-A "$REF_PLATFORM")
    fi
    if [ -n "$REF_TOOLSET" ]; then
        GENERATOR_ARGS+=(-T "$REF_TOOLSET")
    fi
    if [ -n "$REF_MAKE_PROGRAM" ]; then
        GENERATOR_ARGS+=(-DCMAKE_MAKE_PROGRAM="$REF_MAKE_PROGRAM")
    fi
    if [ -n "$REF_C_COMPILER" ]; then
        GENERATOR_ARGS+=(-DCMAKE_C_COMPILER="$REF_C_COMPILER")
    fi
    if [ -n "$REF_CXX_COMPILER" ]; then
        GENERATOR_ARGS+=(-DCMAKE_CXX_COMPILER="$REF_CXX_COMPILER")
    fi
elif command -v ninja >/dev/null 2>&1; then
    GENERATOR_ARGS+=(-G Ninja)
elif command -v mingw32-make >/dev/null 2>&1; then
    GENERATOR_ARGS+=(-G "MinGW Makefiles")
fi

if [ "${#GENERATOR_ARGS[@]}" -gt 0 ]; then
    echo "Using CMake configure arguments: ${GENERATOR_ARGS[*]}"
fi

if cmake -S . -B . \
    "${GENERATOR_ARGS[@]}" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCANDIDATE_SRC="$CANDIDATE_DIR" \
    -DREFERENCE_TEST="$PROJECT_ROOT/test/test_acc_controller.cpp" \
    2>/dev/null; then
    print_pass "CMake configuration successful"
else
    print_fail "CMake configuration failed"
    BUILD_SUCCESS=false
fi

BUILD_CONFIG=""
BUILD_ARGS=()
TEST_ARGS=()
if $BUILD_SUCCESS && grep -q '^CMAKE_CONFIGURATION_TYPES:' CMakeCache.txt; then
    BUILD_CONFIG="Debug"
    BUILD_ARGS=(--config "$BUILD_CONFIG")
    TEST_ARGS=(-C "$BUILD_CONFIG")
fi

if $BUILD_SUCCESS && cmake --build . "${BUILD_ARGS[@]}" 2>/dev/null; then
    print_pass "Build successful"
else
    print_fail "Build failed"
    BUILD_SUCCESS=false
fi

# Run tests
if $BUILD_SUCCESS; then
    print_header "Test Execution"
    
    echo ""
    echo "Running reference tests..."
    set +e
    ctest "${TEST_ARGS[@]}" --output-on-failure 2>&1 | tee test_output.txt
    CTEST_EXIT=${PIPESTATUS[0]}
    set -e

    SUMMARY_LINE="$(grep -E '[0-9]+% tests passed, [0-9]+ tests failed out of [0-9]+' test_output.txt | tail -n 1 || true)"
    if [ -n "$SUMMARY_LINE" ]; then
        TESTS_TOTAL="$(echo "$SUMMARY_LINE" | sed -E 's/.* out of ([0-9]+).*/\1/')"
        TESTS_FAILED="$(echo "$SUMMARY_LINE" | sed -E 's/.* ([0-9]+) tests failed out of.*/\1/')"
        TESTS_PASSED=$((TESTS_TOTAL - TESTS_FAILED))
    else
        TESTS_PASSED=0
        TESTS_FAILED=0
        TESTS_TOTAL=0
    fi

    echo ""
    if [ "$CTEST_EXIT" -eq 0 ]; then
        print_pass "Tests completed: $TESTS_PASSED / $TESTS_TOTAL passed"
    else
        print_fail "Test execution failed: $TESTS_PASSED / $TESTS_TOTAL passed"
    fi

    # Calculate test score (40 points max)
    if [ "$TESTS_TOTAL" -gt 0 ]; then
        TEST_SCORE=$((40 * TESTS_PASSED / TESTS_TOTAL))
    else
        TEST_SCORE=0
    fi
else
    print_warn "Skipping tests due to build failure"
    TEST_SCORE=0
fi

# Static analysis
print_header "Static Analysis"

# Check for dynamic memory
if [ -f "$CANDIDATE_DIR/src/acc_controller.c" ]; then
    if grep -E "(malloc|calloc|realloc|free)\s*\(" "$CANDIDATE_DIR/src/acc_controller.c" > /dev/null 2>&1; then
        print_fail "Dynamic memory usage detected (-5 pts)"
    else
        print_pass "No dynamic memory usage"
    fi
    
    # Check for global variables
    if grep -E "^(static\s+)?[a-zA-Z_][a-zA-Z0-9_]*\s+[a-zA-Z_][a-zA-Z0-9_]*\s*=" "$CANDIDATE_DIR/src/acc_controller.c" | \
       grep -v "^static\s*const" | \
       grep -v "^#define" > /dev/null 2>&1; then
        print_warn "Potential global variables found (review recommended)"
    else
        print_pass "No obvious global variables"
    fi
fi

# Summary
print_header "Evaluation Summary"

echo ""
echo "Build:  $(if $BUILD_SUCCESS; then echo "PASS"; else echo "FAIL"; fi)"
echo "Tests:  ${TESTS_PASSED:-0} / ${TESTS_TOTAL:-0} passed"
echo ""
echo "Note: Full scoring requires manual code review."
echo "      See benchmark/scoring_rubric.md for details."

cd "$PROJECT_ROOT"

EXIT_CODE=0
if ! $BUILD_SUCCESS || [ "${TESTS_FAILED:-0}" -gt 0 ]; then
    EXIT_CODE=1
fi

exit "$EXIT_CODE"
