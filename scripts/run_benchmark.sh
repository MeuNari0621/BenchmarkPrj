#!/bin/bash
#
# ACC Benchmark Runner
# Usage: ./run_benchmark.sh <candidate_directory>
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
CANDIDATE_DIR="$1"

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

# Check arguments
if [ -z "$CANDIDATE_DIR" ]; then
    echo "Usage: $0 <candidate_directory>"
    echo ""
    echo "The candidate directory should contain:"
    echo "  include/acc_controller.h"
    echo "  src/acc_controller.c"
    echo "  test/test_acc_controller.cpp (optional)"
    exit 1
fi

if [ ! -d "$CANDIDATE_DIR" ]; then
    echo "Error: Directory '$CANDIDATE_DIR' does not exist"
    exit 1
fi

# Convert to absolute path
CANDIDATE_DIR="$(cd "$CANDIDATE_DIR" && pwd)"

print_header "ACC Benchmark Evaluation"
echo "Candidate: $CANDIDATE_DIR"
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

BUILD_DIR="$CANDIDATE_DIR/build"
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

# Candidate library
add_library(acc_controller STATIC
    ${CANDIDATE_SRC}/src/acc_controller.c
)
target_include_directories(acc_controller PUBLIC
    ${CANDIDATE_SRC}/include
)
target_link_libraries(acc_controller PRIVATE m)

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
if cmake -S . -B . \
    -DCANDIDATE_SRC="$CANDIDATE_DIR" \
    -DREFERENCE_TEST="$PROJECT_ROOT/test/test_acc_controller.cpp" \
    2>/dev/null; then
    print_pass "CMake configuration successful"
else
    print_fail "CMake configuration failed"
    BUILD_SUCCESS=false
fi

if $BUILD_SUCCESS && cmake --build . 2>/dev/null; then
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
    if ctest --output-on-failure 2>&1 | tee test_output.txt; then
        TESTS_PASSED=$(grep -c "Passed" test_output.txt 2>/dev/null || echo "0")
        TESTS_FAILED=$(grep -c "FAILED" test_output.txt 2>/dev/null || echo "0")
        TESTS_TOTAL=$((TESTS_PASSED + TESTS_FAILED))
        
        echo ""
        print_pass "Tests completed: $TESTS_PASSED / $TESTS_TOTAL passed"
        
        # Calculate test score (40 points max)
        if [ "$TESTS_TOTAL" -gt 0 ]; then
            TEST_SCORE=$((40 * TESTS_PASSED / TESTS_TOTAL))
        else
            TEST_SCORE=0
        fi
    else
        print_fail "Test execution failed"
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
