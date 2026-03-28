@echo off
cd /d "%~dp0"
build\Release\ShaderToyDesktop.exe --wallpaper --shader assets/shaders/my_noise_lab.glsl
:: build\Release\ShaderToyDesktop.exe --shader assets/shaders/my_noise_lab.glsl
