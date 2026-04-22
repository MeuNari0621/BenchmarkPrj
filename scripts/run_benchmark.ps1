param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$CandidateDirectory,

    [Parameter(Position = 1)]
    [string]$BuildDirectory
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Resolve-FullPath {
    param([Parameter(Mandatory = $true)][string]$Path)

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }

    return [System.IO.Path]::GetFullPath((Join-Path (Get-Location) $Path))
}

function Convert-ToCMakePath {
    param([Parameter(Mandatory = $true)][string]$Path)

    return $Path.Replace('\', '/')
}

function Write-Header {
    param([Parameter(Mandatory = $true)][string]$Title)

    Write-Host ""
    Write-Host "======================================"
    Write-Host " $Title"
    Write-Host "======================================"
}

function Write-Pass {
    param([Parameter(Mandatory = $true)][string]$Message)

    Write-Host "[PASS] $Message" -ForegroundColor Green
}

function Write-Fail {
    param([Parameter(Mandatory = $true)][string]$Message)

    Write-Host "[FAIL] $Message" -ForegroundColor Red
}

function Write-Warn {
    param([Parameter(Mandatory = $true)][string]$Message)

    Write-Host "[WARN] $Message" -ForegroundColor Yellow
}

function Invoke-External {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter()][string[]]$Arguments = @()
    )

    & $FilePath @Arguments | Out-Host
    if ($null -eq $LASTEXITCODE) {
        return 0
    }

    return [int]$LASTEXITCODE
}

function Get-CMakeCacheValue {
    param(
        [Parameter(Mandatory = $true)][string]$CachePath,
        [Parameter(Mandatory = $true)][string]$Key
    )

    if (-not (Test-Path -Path $CachePath -PathType Leaf)) {
        return $null
    }

    $pattern = "^{0}:[^=]*=(.*)$" -f [regex]::Escape($Key)
    $match = Select-String -Path $CachePath -Pattern $pattern | Select-Object -First 1
    if (-not $match) {
        return $null
    }

    return ([regex]::Match($match.Line, $pattern)).Groups[1].Value
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = Split-Path -Parent $scriptDir

if (-not (Test-Path -Path $CandidateDirectory -PathType Container)) {
    throw "Directory '$CandidateDirectory' does not exist"
}

$candidateDir = Resolve-FullPath -Path $CandidateDirectory
$candidateDirForCMake = Convert-ToCMakePath -Path $candidateDir

if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    $buildDir = Join-Path $candidateDir "build"
} else {
    $buildDir = Resolve-FullPath -Path $BuildDirectory
}

if ($buildDir -eq $candidateDir) {
    throw "Build directory must be different from candidate directory"
}

Write-Header "ACC Benchmark Evaluation"
Write-Host "Candidate: $candidateDir"
Write-Host "Build dir: $buildDir"
Write-Host "Date: $(Get-Date)"

Write-Header "File Check"

$score = 0
$maxScore = 200

function Test-RequiredFile {
    param(
        [Parameter(Mandatory = $true)][string]$RelativePath,
        [Parameter(Mandatory = $true)][int]$Points
    )

    $fullPath = Join-Path $candidateDir $RelativePath
    if (Test-Path -Path $fullPath -PathType Leaf) {
        Write-Pass "$RelativePath exists"
        return $true
    }

    Write-Fail "$RelativePath not found (-$Points pts)"
    return $false
}

Test-RequiredFile -RelativePath "include/acc_controller.h" -Points 10 | Out-Null
Test-RequiredFile -RelativePath "src/acc_controller.c" -Points 30 | Out-Null
Test-RequiredFile -RelativePath "test/test_acc_controller.cpp" -Points 10 | Out-Null

Write-Header "Build Check"

if (Test-Path -Path $buildDir) {
    Remove-Item -Path $buildDir -Recurse -Force
}
New-Item -Path $buildDir -ItemType Directory | Out-Null

$candidateCMakeLists = @'
cmake_minimum_required(VERSION 3.16)
project(CandidateACC LANGUAGES C CXX)

enable_testing()

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(CheckLibraryExists)

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

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.15.0.zip
)
if(WIN32)
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
endif()
FetchContent_MakeAvailable(googletest)

if(EXISTS "${REFERENCE_TEST}")
    add_executable(test_reference ${REFERENCE_TEST})
    target_link_libraries(test_reference PRIVATE acc_controller gtest_main)
    target_include_directories(test_reference PRIVATE ${CANDIDATE_SRC}/include)
    include(GoogleTest)
    gtest_discover_tests(test_reference)
endif()

if(EXISTS "${CANDIDATE_SRC}/test/test_acc_controller.cpp")
    add_executable(test_candidate ${CANDIDATE_SRC}/test/test_acc_controller.cpp)
    target_link_libraries(test_candidate PRIVATE acc_controller gtest_main)
    target_include_directories(test_candidate PRIVATE ${CANDIDATE_SRC}/include)
    include(GoogleTest)
    gtest_discover_tests(test_candidate)
endif()
'@

$cmakeListsPath = Join-Path $buildDir "CMakeLists.txt"
[System.IO.File]::WriteAllText($cmakeListsPath, $candidateCMakeLists)

Push-Location $buildDir
$scriptExitCode = 0
try {
    $buildSuccess = $true
    $generatorArgs = @()
    $referenceCachePath = Join-Path $projectRoot "build/CMakeCache.txt"
    $referenceTestPathForCMake = Convert-ToCMakePath -Path (Join-Path $projectRoot 'test/test_acc_controller.cpp')

    if (Test-Path -Path $referenceCachePath -PathType Leaf) {
        $referenceGenerator = Get-CMakeCacheValue -CachePath $referenceCachePath -Key "CMAKE_GENERATOR"
        $referencePlatform = Get-CMakeCacheValue -CachePath $referenceCachePath -Key "CMAKE_GENERATOR_PLATFORM"
        $referenceToolset = Get-CMakeCacheValue -CachePath $referenceCachePath -Key "CMAKE_GENERATOR_TOOLSET"
        $referenceMakeProgram = Get-CMakeCacheValue -CachePath $referenceCachePath -Key "CMAKE_MAKE_PROGRAM"
        $referenceCCompiler = Get-CMakeCacheValue -CachePath $referenceCachePath -Key "CMAKE_C_COMPILER"
        $referenceCxxCompiler = Get-CMakeCacheValue -CachePath $referenceCachePath -Key "CMAKE_CXX_COMPILER"

        if (-not [string]::IsNullOrWhiteSpace($referenceGenerator)) {
            $generatorArgs += @("-G", $referenceGenerator)
        }
        if (-not [string]::IsNullOrWhiteSpace($referencePlatform)) {
            $generatorArgs += @("-A", $referencePlatform)
        }
        if (-not [string]::IsNullOrWhiteSpace($referenceToolset)) {
            $generatorArgs += @("-T", $referenceToolset)
        }
        if (-not [string]::IsNullOrWhiteSpace($referenceMakeProgram)) {
            $generatorArgs += "-DCMAKE_MAKE_PROGRAM=$referenceMakeProgram"
        }
        if (-not [string]::IsNullOrWhiteSpace($referenceCCompiler)) {
            $generatorArgs += "-DCMAKE_C_COMPILER=$referenceCCompiler"
        }
        if (-not [string]::IsNullOrWhiteSpace($referenceCxxCompiler)) {
            $generatorArgs += "-DCMAKE_CXX_COMPILER=$referenceCxxCompiler"
        }
    } elseif (Get-Command ninja -ErrorAction SilentlyContinue) {
        $generatorArgs += @("-G", "Ninja")
    } elseif (Get-Command mingw32-make -ErrorAction SilentlyContinue) {
        $generatorArgs += @("-G", "MinGW Makefiles")
    }

    if ($generatorArgs.Count -gt 0) {
        Write-Host "Using CMake configure arguments: $($generatorArgs -join ' ')"
    }

    $configureArgs = @(
        "-S", ".",
        "-B", ".",
        "-DCMAKE_BUILD_TYPE=Debug",
        "-DCANDIDATE_SRC=$candidateDirForCMake",
        "-DREFERENCE_TEST=$referenceTestPathForCMake"
    )
    $configureArgs = $generatorArgs + $configureArgs

    if ((Invoke-External -FilePath "cmake" -Arguments $configureArgs) -eq 0) {
        Write-Pass "CMake configuration successful"
    } else {
        Write-Fail "CMake configuration failed"
        $buildSuccess = $false
    }

    $buildConfig = $null
    $buildArgs = @("--build", ".")
    $ctestArgs = @("--output-on-failure")
    $cachePath = Join-Path $buildDir "CMakeCache.txt"
    if ($buildSuccess -and (Test-Path -Path $cachePath -PathType Leaf)) {
        $cacheText = Get-Content -Path $cachePath -Raw
        if ($cacheText -match "CMAKE_CONFIGURATION_TYPES:") {
            $buildConfig = "Debug"
            $buildArgs += @("--config", $buildConfig)
            $ctestArgs = @("-C", $buildConfig) + $ctestArgs
        }
    }

    if ($buildSuccess -and (Invoke-External -FilePath "cmake" -Arguments $buildArgs) -eq 0) {
        Write-Pass "Build successful"
    } elseif ($buildSuccess) {
        Write-Fail "Build failed"
        $buildSuccess = $false
    }

    $testsPassed = 0
    $testsFailed = 0
    $testsTotal = 0
    $testScore = 0

    if ($buildSuccess) {
        Write-Header "Test Execution"
        Write-Host ""
        Write-Host "Running reference tests..."

        $testOutputPath = Join-Path $buildDir "test_output.txt"
        & ctest @ctestArgs 2>&1 | Tee-Object -FilePath $testOutputPath
        $ctestExit = $LASTEXITCODE

        if (Test-Path -Path $testOutputPath -PathType Leaf) {
            $summaryLine = Select-String -Path $testOutputPath -Pattern "[0-9]+% tests passed, [0-9]+ tests failed out of [0-9]+" | Select-Object -Last 1
            if ($summaryLine) {
                $summaryText = $summaryLine.Line
                $match = [regex]::Match($summaryText, "[0-9]+% tests passed, ([0-9]+) tests failed out of ([0-9]+)")
                if ($match.Success) {
                    $testsFailed = [int]$match.Groups[1].Value
                    $testsTotal = [int]$match.Groups[2].Value
                    $testsPassed = $testsTotal - $testsFailed
                }
            }
        }

        Write-Host ""
        if ($ctestExit -eq 0) {
            Write-Pass "Tests completed: $testsPassed / $testsTotal passed"
        } else {
            Write-Fail "Test execution failed: $testsPassed / $testsTotal passed"
        }

        if ($testsTotal -gt 0) {
            $testScore = [int](40 * $testsPassed / $testsTotal)
        }
    } else {
        Write-Warn "Skipping tests due to build failure"
    }

    Write-Header "Static Analysis"

    $sourceFile = Join-Path $candidateDir "src/acc_controller.c"
    if (Test-Path -Path $sourceFile -PathType Leaf) {
        if (Select-String -Path $sourceFile -Pattern "(malloc|calloc|realloc|free)\s*\(" -Quiet) {
            Write-Fail "Dynamic memory usage detected (-5 pts)"
        } else {
            Write-Pass "No dynamic memory usage"
        }

        $globalPattern = "^(static\s+)?[a-zA-Z_][a-zA-Z0-9_\s\*]*\s+[a-zA-Z_][a-zA-Z0-9_]*\s*="
        $globalLines = Get-Content -Path $sourceFile | Where-Object {
            $_ -match $globalPattern -and
            $_ -notmatch "^static\s+const\b" -and
            $_ -notmatch "^#define\b"
        }

        if ($globalLines) {
            Write-Warn "Potential global variables found (review recommended)"
        } else {
            Write-Pass "No obvious global variables"
        }
    }

    Write-Header "Evaluation Summary"
    Write-Host ""
    Write-Host "Build:  $(if ($buildSuccess) { 'PASS' } else { 'FAIL' })"
    Write-Host "Tests:  $testsPassed / $testsTotal passed"
    Write-Host ""
    Write-Host "Note: Full scoring requires manual code review."
    Write-Host "      See benchmark/scoring_rubric.md for details."

    if (-not $buildSuccess -or $testsFailed -gt 0) {
        $scriptExitCode = 1
    }
}
finally {
    Pop-Location
}

exit $scriptExitCode