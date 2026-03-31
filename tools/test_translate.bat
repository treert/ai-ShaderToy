@echo off
REM ============================================================
REM 翻译回归测试：翻译所有 shader，验证编译通过，并与基准文件对比
REM 用法：tools\test_translate.bat [--update-golden]
REM   无参数：翻译 + 编译验证 + 与基准文件对比差异
REM   --update-golden：翻译后将输出保存为新基准
REM ============================================================
setlocal enabledelayedexpansion

cd /d "%~dp0.."
set "BUILD_DIR=%CD%\build"
set "EXE=%BUILD_DIR%\ShaderToyDesktop.exe"
set "TRANSLATE_DIR=%BUILD_DIR%\Logs\translate-mode"
set "GOLDEN_DIR=%CD%\tests\golden_hlsl"
set "UPDATE_GOLDEN=0"

if "%~1"=="--update-golden" set "UPDATE_GOLDEN=1"

if not exist "%EXE%" (
    echo ERROR: %EXE% not found. Please build the project first.
    exit /b 1
)

REM Step 1: 批量翻译
echo [Step 1] Translating all shaders...
call tools\batch_translate.bat >nul 2>&1

REM 检查编译错误
set "COMPILE_ERRORS=0"
findstr /s /m /C:"Compile Errors" "%TRANSLATE_DIR%\*.hlsl" >nul 2>&1
if %errorlevel%==0 (
    echo FAIL: Some HLSL files have compile errors:
    findstr /s /m /C:"Compile Errors" "%TRANSLATE_DIR%\*.hlsl" 2>nul
    exit /b 1
)
echo   All HLSL files compiled successfully.

REM Step 2: 更新基准 or 对比
if "%UPDATE_GOLDEN%"=="1" (
    echo [Step 2] Updating golden files...
    if exist "%GOLDEN_DIR%" rmdir /s /q "%GOLDEN_DIR%"
    xcopy /s /e /i /q "%TRANSLATE_DIR%" "%GOLDEN_DIR%" >nul
    echo   Golden files saved to: %GOLDEN_DIR%
    echo DONE: Golden files updated.
    exit /b 0
)

if not exist "%GOLDEN_DIR%" (
    echo [Step 2] No golden files found. Run with --update-golden first:
    echo   tools\test_translate.bat --update-golden
    exit /b 1
)

echo [Step 2] Comparing with golden files...
set "DIFF_COUNT=0"

REM 使用过滤对比：去掉来源注释行（含绝对路径），再比较
for /r "%TRANSLATE_DIR%" %%f in (*.hlsl) do (
    set "FULL=%%f"
    set "REL=!FULL:%TRANSLATE_DIR%\=!"
    set "GOLDEN=%GOLDEN_DIR%\!REL!"

    if not exist "!GOLDEN!" (
        echo   NEW: !REL!
        set /a DIFF_COUNT+=1
    ) else (
        REM 过滤掉 "// Source:" 和 "// Auto-generated" 行后对比
        findstr /V /B /C:"// Source:" /C:"// Auto-generated" "%%f" >"%TEMP%\_test_out.tmp" 2>nul
        findstr /V /B /C:"// Source:" /C:"// Auto-generated" "!GOLDEN!" >"%TEMP%\_test_gld.tmp" 2>nul
        fc /L "%TEMP%\_test_out.tmp" "%TEMP%\_test_gld.tmp" >nul 2>&1
        if errorlevel 1 (
            echo   DIFF: !REL!
            set /a DIFF_COUNT+=1
        )
    )
)

echo.
if "!DIFF_COUNT!"=="0" (
    echo PASS: All HLSL outputs match golden files.
    exit /b 0
) else (
    echo FAIL: !DIFF_COUNT! file^(s^) differ. Run --update-golden to accept changes.
    exit /b 1
)
