$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $projectRoot
$juceCommit = '29396c22c93392d6738e021b83196283d6e4d850'
if (!(Test-Path -LiteralPath 'third_party/JUCE/CMakeLists.txt')) {
    git submodule update --init --recursive
    if ($LASTEXITCODE -ne 0) { throw 'JUCE submodule download failed. Clone with --recurse-submodules.' }
}
if (!(Test-Path -LiteralPath 'third_party/JUCE/CMakeLists.txt')) { throw 'Pinned JUCE source is missing.' }
# Corresponding-source ZIPs include JUCE without Git metadata; the pinned source
# is already included there. A Git checkout must resolve to the exact commit.
if (Test-Path -LiteralPath 'third_party/JUCE/.git') {
    $actualCommit = git -C third_party/JUCE rev-parse HEAD
    if ($LASTEXITCODE -ne 0 -or $actualCommit -ne $juceCommit) { throw 'Unexpected JUCE commit; restore the pinned submodule.' }
}
cmake -S . -B build -G 'Visual Studio 17 2022' -A x64
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
cmake --build build --config Release --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
ctest --test-dir build -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }
Write-Host 'Build and tests passed. See docs/BUILDING.md for demo generation and packaging.'
