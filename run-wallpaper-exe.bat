@echo off
cd /d "%~dp0"
@REM start "" build\ShaderToyDesktop.exe --shader assets/shaders/my_noise_lab.glsl
@REM start "" build\ShaderToyDesktop.exe --wallpaper --debug --shader assets/shaders/my_noise_lab.glsl --fps 60 --renderscale 1
start "" build\ShaderToyDesktop.exe --wallpaper --debug --shader assets/shaders/my_noise_lab.glsl --fps 60 --renderscale 1 --monitor 0
@REM start "" build\ShaderToyDesktop.exe --wallpaper --debug --shader assets/shaders/my_noise_lab.glsl
