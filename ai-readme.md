## 重要说明
项目规则见 `.codebuddy/rules/`，技术文档见 `Docs/`。

## 项目目标
实现一个 Windows 应用，通过 shader 实时渲染图像，类似 shadertoy.com。
应用支持两种运行模式：
1. **窗口模式** — 作为独立窗口打开，可调整大小
2. **壁纸模式** — 通过 Win32 API 嵌入桌面，替代静态壁纸

## 目录说明
- `src/` — 源代码
- `assets/` — 资源文件（着色器等）
- `Docs/` — 项目知识库（技术文档，供 AI 读写，也供用户查阅）
- `tests/` — 测试
- `scripts/` — 脚本

## 开发阶段

### 阶段 1：初步运行 ✅ 已完成
- [x] SDL2 + OpenGL 3.3 窗口创建
- [x] ShaderToy 着色器加载与包装
- [x] 全屏四边形渲染 + uniform 传递（iResolution/iTime/iTimeDelta/iFrame/iMouse/iDate）
- [x] 窗口模式（可调大小、ESC 退出）
- [x] 壁纸模式（WorkerW 嵌入桌面）
- [x] 鼠标交互（iMouse 完整语义）
- [x] 命令行参数控制

### 阶段 2：支持 shadertoy.com 的 shader ✅ 已完成
- [x] iChannel 纹理输入（sampler2D，支持图片加载 via stb_image）
- [x] iChannelResolution uniform
- [x] 多 Pass 渲染框架（Buffer A/B/C/D + Image，FBO 双缓冲）
- [x] Shader 热加载（文件监控自动刷新 + F5 手动刷新）
- [x] 系统托盘图标和控制界面（暂停/恢复/重载/退出）
- [x] iSampleRate 音频采样率接入（固定 44100）
- [x] 多显示器支持（虚拟桌面 SM_CXVIRTUALSCREEN）
- [x] 全屏应用检测与自动暂停
- [x] GPU 性能优化（帧率自适应）
- [x] GPU 性能优化增强（壁纸模式默认30fps + 0.5x降分辨率渲染 + VSync关闭改用精确帧率控制）

### 阶段 3：调试 UI ✅ 已完成
- [x] 集成 Dear ImGui（FetchContent 自动下载，SDL2+OpenGL3 后端）
- [x] 窗口模式调试面板（Tab 键切换显隐）
- [x] 信息展示：FPS/帧率、shader 路径/状态、iResolution/iTime/iMouse 等 uniform 值
- [x] 编译错误信息展示（红色高亮）
- [x] 控制按钮：重载 shader、暂停/恢复、重置时间
- [x] 滑条调节：目标 FPS（15~120）、renderScale（0.1~1.0）
- [x] Shader 选择器：列出 assets/shaders/*.glsl 文件，点击切换
