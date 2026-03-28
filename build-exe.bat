@echo off
cd /d "%~dp0"

echo [1/2] Configuring...
cmake -B build
if %errorlevel% neq 0 (
    echo Configuration failed!
    exit /b 1
)

echo [2/2] Building...
cmake --build build --config Release
if %errorlevel% neq 0 (
    echo Build failed!
    exit /b 1
)

echo.
echo Build succeeded: build\Release\ShaderToyDesktop.exe
