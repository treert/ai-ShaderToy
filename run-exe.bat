@echo off
cd /d "%~dp0"
@REM start "" build\Release\ShaderToyDesktop.exe --wallpaper --shader assets/shaders/my_noise_lab.glsl
@REM start "" build\Release\ShaderToyDesktop.exe --wallpaper --debug --shader assets/shaders/my_noise_lab.glsl
start "" build\Release\ShaderToyDesktop.exe --wallpaper --debug --shader assets/shaders/my_noise_lab.glsl
