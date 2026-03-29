@echo off
cd /d "%~dp0"
start "" build\ShaderToyDesktop.exe --shader assets/shaders/my_noise_lab.glsl
@REM start "" build\ShaderToyDesktop.exe --wallpaper --debug --shader assets/shaders/my_noise_lab.glsl
