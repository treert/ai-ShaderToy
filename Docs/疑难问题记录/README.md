# Troubleshooting Records / 疑难问题记录

This directory contains detailed records of difficult issues encountered and resolved during the development of ai-ShaderToy, especially those related to OpenGL, D3D11, Windows platform, DWM, and DXGI.

Each record follows a standard template (`_template.md`) and includes: problem description, environment info, investigation process, root cause analysis, solution, and related knowledge.

## Index / 问题索引

| # | Title | Summary | Tags | File |
|---|-------|---------|------|------|
| 001 | Multi-Process Frame Rate Halving / 多进程帧率降半 | Two D3D11 processes running simultaneously causes wallpaper FPS to drop from 60 to ~32, due to Windows background process Sleep precision degradation | `D3D11` `DXGI` `Windows` `DWM` `Sleep` `Timer` | [001-多进程帧率降半.md](001-多进程帧率降半.md) |
| 002 | Win11 24H2 Wallpaper Black Screen / Win11 24H2 壁纸模式黑屏 | OpenGL wallpaper renders black on Win11 24H2+ because Progman uses WS_EX_NOREDIRECTIONBITMAP, breaking OpenGL swap chain composition | `D3D11` `OpenGL` `Windows` `DWM` `WS_EX_NOREDIRECTIONBITMAP` | [002-Win11-24H2壁纸模式黑屏.md](002-Win11-24H2壁纸模式黑屏.md) |
| 003 | DWM Occlusion Detection False Positives / DWM 遮挡检测误判 | Invisible system windows (DWM cloaked, UWP, system helpers) trigger false occlusion detection, causing wallpaper to stop rendering | `Windows` `DWM` `UWP` `Win32` | [003-DWM遮挡检测误判.md](003-DWM遮挡检测误判.md) |
