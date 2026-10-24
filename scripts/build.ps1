param([string]$BuildDirectory = 'build/release')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
Push-Location -LiteralPath $projectRoot
try {
    if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
        throw 'CMake 3.24 or later is required.'
    }
    if (Test-Path -LiteralPath (Join-Path $BuildDirectory 'CMakeCache.txt')) {
        & cmake -S . -B $BuildDirectory -DCMAKE_BUILD_TYPE=Release
    } elseif ((Get-Command ninja -ErrorAction SilentlyContinue) -and ((Get-Command g++ -ErrorAction SilentlyContinue) -or (Get-Command cl -ErrorAction SilentlyContinue))) {
        & cmake -S . -B $BuildDirectory -G Ninja -DCMAKE_BUILD_TYPE=Release
    } else {
        & cmake -S . -B $BuildDirectory -A x64
    }
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
    & cmake --build $BuildDirectory --config Release
    if ($LASTEXITCODE -ne 0) { throw 'Build failed. Close CalendarDeck.exe if it is running from this directory.' }
    & ctest --test-dir $BuildDirectory -C Release --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }
    Write-Host "Build and tests passed: $BuildDirectory"
} finally {
    Pop-Location
}
