@echo off
REM ============================================================
REM 批量翻译所有 shader (GLSL -> HLSL)
REM 用法：在项目根目录运行 tools\batch_translate.bat
REM       或在 build 目录运行 ..\tools\batch_translate.bat
REM 输出：build\Logs\translate-mode\ 目录
REM 日志：build\Logs\translate_results.txt
REM ============================================================
setlocal

REM 定位到项目根目录（脚本在 tools/ 下）
cd /d "%~dp0.."
set "BUILD_DIR=%CD%\build"
set "SHADER_DIR=%CD%\assets\shaders"
set "EXE=%BUILD_DIR%\ShaderToyDesktop.exe"
set "LOG=%BUILD_DIR%\Logs\translate_results.txt"

if not exist "%EXE%" (
    echo ERROR: %EXE% not found. Please build the project first.
    exit /b 1
)

echo. > "%LOG%"
set TOTAL=0
set PASS=0
set FAIL=0

echo ====== Single Pass (.glsl) ======
echo ====== Single Pass (.glsl) ====== >> "%LOG%"
for %%f in ("%SHADER_DIR%\*.glsl") do (
    set /a TOTAL+=1
    echo. >> "%LOG%"
    echo --- %%~nxf --- >> "%LOG%"
    echo --- %%~nxf ---
    "%EXE%" --translate --shader "%%f" >> "%LOG%" 2>&1
)

echo.
echo ====== Multi Pass (.json) ======
echo. >> "%LOG%"
echo ====== Multi Pass (.json) ====== >> "%LOG%"
for %%f in ("%SHADER_DIR%\*.json") do (
    set /a TOTAL+=1
    echo. >> "%LOG%"
    echo --- %%~nxf --- >> "%LOG%"
    echo --- %%~nxf ---
    "%EXE%" --translate --shader "%%f" >> "%LOG%" 2>&1
)

echo.
echo ====== Multi Pass (directories) ======
echo. >> "%LOG%"
echo ====== Multi Pass (directories) ====== >> "%LOG%"
for /d %%d in ("%SHADER_DIR%\*") do (
    if exist "%%d\image.glsl" (
        set /a TOTAL+=1
        echo. >> "%LOG%"
        echo --- %%~nxd --- >> "%LOG%"
        echo --- %%~nxd ---
        "%EXE%" --translate --shader "%%d" >> "%LOG%" 2>&1
    )
)

echo.
echo ====== DONE ======
echo. >> "%LOG%"
echo ====== DONE ====== >> "%LOG%"

echo.
echo Results saved to: %LOG%
echo HLSL output dir:  %BUILD_DIR%\Logs\translate-mode\
echo.
echo Checking compile errors...
findstr /s /m /C:"Compile Errors" "%BUILD_DIR%\Logs\translate-mode\*.hlsl" 2>nul && (
    echo.
    echo The above files have compile errors.
) || (
    echo All HLSL files compiled successfully!
)
