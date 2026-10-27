# Campus Complaint Tracker - Windows PowerShell Build & Test Script

param(
    [string]$Action = "all"
)

$ErrorActionPreference = "Stop"

# Use modern 64-bit MSYS2 UCRT64 GCC if present, otherwise default gcc
$gcc = "C:\msys64\ucrt64\bin\gcc.exe"
if (-not (Test-Path $gcc)) {
    $gcc = "gcc.exe"
}

Write-Host "========================================================" -ForegroundColor Cyan
Write-Host " CAMPUS COMPLAINT TRACKER - BUILD SYSTEM ($Action)     " -ForegroundColor Cyan
Write-Host " Compiler: $gcc" -ForegroundColor Gray
Write-Host "========================================================" -ForegroundColor Cyan

function Build-Mongoose {
    Write-Host "[1/4] Compiling Mongoose C HTTP Server Library..." -ForegroundColor Yellow
    & $gcc -c backend/lib/mongoose.c -o backend/lib/mongoose.o -DMG_ARCH=MG_ARCH_WIN32 -Ibackend/lib
    if ($LASTEXITCODE -ne 0) { throw "Failed to compile Mongoose library" }
}

function Build-Server {
    Write-Host "[2/4] Compiling Campus Server Executable..." -ForegroundColor Yellow
    & $gcc -Wall -Wextra -O2 `
        -I backend/include -I backend/lib `
        backend/src/main.c `
        backend/src/server.c `
        backend/src/database.c `
        backend/src/auth.c `
        backend/src/algorithms.c `
        backend/src/cJSON.c `
        backend/lib/mongoose.o `
        -lsqlite3 -lws2_32 -lbcrypt `
        -o campus_server.exe

    if ($LASTEXITCODE -ne 0) { throw "Failed to compile campus_server.exe" }
    Write-Host "--> Success: campus_server.exe built successfully!" -ForegroundColor Green
}

function Run-Algorithm-Tests {
    Write-Host "[3/4] Compiling & Running DAA Algorithm Unit Tests..." -ForegroundColor Yellow
    & $gcc -I backend/include `
        backend/src/algorithms.c `
        backend/tests/test_algorithms.c `
        -o test_algorithms.exe

    if ($LASTEXITCODE -ne 0) { throw "Failed to compile test_algorithms.exe" }
    & .\test_algorithms.exe
    Remove-Item -Force test_algorithms.exe
    Write-Host "--> DAA Algorithm unit tests PASSED!" -ForegroundColor Green
}

function Run-Api-Tests {
    Write-Host "[4/4] Compiling & Running Backend Integration Tests..." -ForegroundColor Yellow
    & $gcc -I backend/include -I backend/lib `
        backend/tests/test_api.c `
        backend/src/database.c `
        backend/src/auth.c `
        backend/src/cJSON.c `
        -lsqlite3 -lws2_32 -lbcrypt `
        -o test_api.exe

    if ($LASTEXITCODE -ne 0) { throw "Failed to compile test_api.exe" }
    & .\test_api.exe
    Remove-Item -Force test_api.exe
    Write-Host "--> Backend integration tests PASSED!" -ForegroundColor Green
}

if ($Action -eq "all" -or $Action -eq "build") {
    Build-Mongoose
    Build-Server
}

if ($Action -eq "all" -or $Action -eq "test") {
    Run-Algorithm-Tests
    Run-Api-Tests
}

Write-Host "`nAll operations completed successfully!" -ForegroundColor Green
Write-Host "To launch the server: .\campus_server.exe --port 8080" -ForegroundColor Cyan
