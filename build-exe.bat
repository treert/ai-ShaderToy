@echo off
cd /d "%~dp0"

:: 设置 MSVC 编译环境
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1

echo [1/2] Configuring (Ninja + MSVC)...
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
if %errorlevel% neq 0 (
    echo Configuration failed!
    exit /b 1
)

echo [2/2] Building...
cmake --build build
if %errorlevel% neq 0 (
    echo Build failed!
    exit /b 1
)

echo.
echo Build succeeded: build\ShaderToyDesktop.exe
