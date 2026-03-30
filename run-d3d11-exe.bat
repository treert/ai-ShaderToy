@echo off
cd /d "%~dp0"
start "" build\ShaderToyDesktop.exe --d3d11
@REM start "" build\ShaderToyDesktop.exe --d3d11 --shader assets/shaders/my_noise_lab.glsl
