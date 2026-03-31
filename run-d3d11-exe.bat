@echo off
cd /d "%~dp0"
@REM start "" build\ShaderToyDesktop.exe --d3d11
@REM start "" build\ShaderToyDesktop.exe --d3d11 --shader assets/shaders/my_noise_lab.glsl
start "" build\ShaderToyDesktop.exe --d3d11 --shader assets/shaders/a_river_goes.json %*