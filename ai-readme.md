## 重要说明
ai在运行时，按需持续更新本文件。

## 项目目标
实现一个windows应用，通过 shader 渲染图片，类似 shadertoy.com 一样，希望最好能支持支持 shadertoy.com 的shader。
然后应用本身可以作为独立的窗口打开，也可以作为桌面壁纸。

## 目录说明
- `src`：源代码
- `assets`：资源
- `Docs`：文档
- `tests`：测试
- `scripts`：脚本

## 技术栈
- **语言**：C++17
- **构建**：CMake 3.16+（FetchContent 自动下载依赖）
- **依赖**：SDL2（窗口/事件）、GLAD（OpenGL 3.3 Core Loader）、Win32 API（桌面壁纸嵌入）
- **编译器**：MSVC（Visual Studio 2022）

## 构建方式
```bash
cmake -B build
cmake --build build --config Release
# 运行（窗口模式）
build/Release/ShaderToyDesktop.exe
# 运行（壁纸模式）
build/Release/ShaderToyDesktop.exe --wallpaper
# 指定自定义 shader
build/Release/ShaderToyDesktop.exe --shader path/to/shader.glsl
```

## 命令行参数
| 参数 | 说明 | 默认值 |
|------|------|--------|
| `--shader <path>` | ShaderToy GLSL 文件路径 | `assets/shaders/default.glsl` |
| `--wallpaper` | 桌面壁纸模式 | 关闭 |
| `--width <n>` | 窗口宽度 | 800 |
| `--height <n>` | 窗口高度 | 600 |
| `--fps <n>` | 目标帧率 | 60 |

## 源码结构
- `src/main.cpp`：主程序入口，事件循环，命令行解析
- `src/shader_manager.h/.cpp`：ShaderToy 着色器加载、包装和编译
- `src/renderer.h/.cpp`：OpenGL 渲染器，全屏四边形，uniform 管理
- `src/wallpaper.h/.cpp`：Win32 WorkerW 桌面壁纸嵌入
- `assets/shaders/default.glsl`：默认示例着色器

## 已支持的 ShaderToy Uniform
| 变量 | 类型 | 说明 |
|------|------|------|
| `iResolution` | `vec3` | 视口分辨率 |
| `iTime` | `float` | 播放时间（秒） |
| `iTimeDelta` | `float` | 帧间隔时间 |
| `iFrame` | `int` | 帧计数 |
| `iMouse` | `vec4` | 鼠标位置 |
| `iDate` | `vec4` | 日期（已声明，待接入） |
| `iSampleRate` | `float` | 音频采样率（已声明，待接入） |

## 阶段计划
### 1. 初步运行 ✅ 已完成
先实现一个应用，可以运行，然后通过 shader 渲染图片，并显示到窗口上。
然后通过 win32 api 将图片渲染到桌面。
初始的shader可以是很简单的，比如就渲染一个三角形。

### 2. 支持 shadertoy.com 的 shader
扩充 shader 的功能，支持 shadertoy.com 的 shader。
待实现功能：
- iChannel 纹理输入（sampler2D）
- 多 Pass 渲染（Buffer A/B/C/D）
- iDate、iSampleRate 实际接入
- Shader 热加载/切换
- 系统托盘图标和控制界面
- 多显示器支持
- 全屏应用自动暂停
- GPU 性能优化