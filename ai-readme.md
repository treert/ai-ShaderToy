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
- `third_party/` — 第三方依赖（git submodule：SDL2、GLAD、ImGui、nlohmann/json）
- `Docs/` — 项目知识库（技术文档，供 AI 读写，也供用户查阅）
- `tools/` — 辅助工具（json2dir.py 等）

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
- [x] 托盘 Tooltip 动态状态（FPS/RenderTime/shader名称/monitor参数，每秒刷新）
- [x] 托盘菜单 Shader 切换（Switch Shader 子菜单，列出所有 GLSL/JSON/目录 shader，当前 shader 勾选标记）
- [x] iSampleRate 音频采样率接入（固定 44100）
- [x] 多显示器支持（虚拟桌面 SM_CXVIRTUALSCREEN）
- [x] 壁纸模式指定显示器（--monitor 参数，支持只在某个显示器上生效）
- [x] 全屏应用检测与自动暂停（--no-pause-on-fullscreen 可禁用）
- [x] 按显示器粒度桌面遮挡检测（最大化窗口遮挡 ≥90% 面积时跳过渲染）
- [x] 遮挡检测误判修复（DWM cloaked 隐形窗口过滤 + 系统辅助窗口类过滤）
- [x] GPU 性能优化（帧率自适应）
- [x] GPU 性能优化增强（壁纸模式默认30fps + 0.5x降分辨率渲染 + VSync关闭改用精确帧率控制）

### 阶段 3：调试 UI ✅ 已完成
- [x] 集成 Dear ImGui（FetchContent 自动下载，SDL2+OpenGL3 后端）
- [x] 窗口模式调试面板（Tab 键切换显隐）
- [x] 信息展示：FPS/帧率、RenderTime（实际渲染耗时）、shader 路径/状态、iResolution/iTime/iMouse 等 uniform 值
- [x] 编译错误信息展示（红色高亮）
- [x] 控制按钮：重载 shader、暂停/恢复、重置时间
- [x] 滑条调节：目标 FPS（15~120）、renderScale（0.1~1.0）
- [x] Shader 选择器：列出 assets/shaders/*.glsl 文件，点击切换
- [x] 壁纸模式 Debug 叠加（--debug 参数，只读纯文字叠加，默认关闭）

### 阶段 4：多 Pass 渲染 ✅ 已完成
- [x] ShaderProject 统一加载层（自动检测 .glsl / .json / 目录模式）
- [x] ShaderToy API JSON 导入（解析 renderpass 数组，自动识别 image/buffer/common）
- [x] 多文件目录模式（image.glsl + buf_a~d.glsl + common.glsl + channels.json）
- [x] Common 共享代码注入（ShaderManager 支持 commonSource，编译时自动注入到每个 pass）
- [x] MultiPassRenderer 集成到 main.cpp 主渲染循环（统一单 Pass 和多 Pass）
- [x] Buffer 间互引用和自引用（双缓冲读取上一帧输出）
- [x] iChannelResolution 多 Pass 适配（Buffer 输出尺寸正确传入）
- [x] 降分辨率渲染与多 Pass FBO 协调
- [x] 壁纸模式多 Pass 渲染
- [x] 热加载适配多 Pass（监控所有相关文件，全部重编译）
- [x] 调试 UI 展示渲染模式和各 pass 信息
- [x] 多 Pass 示例（assets/shaders/multipass_demo/）
- [x] nlohmann/json 依赖集成（FetchContent）
- [x] ShaderToy 网站导出 JSON 完全兼容（type/filepath 字段、output id 映射、keyboard 类型识别）
- [x] 纹理路径查找增强（支持 /media/a/ 和 /presets/ 到 assets/ 目录映射）
- [x] JSON input 解析重构（抽取 ParseInputBinding 辅助函数消除重复代码）
- [x] json2dir.py 工具（tools/ 目录，JSON 转目录格式）
- [x] Cube A (CubeMap) pass 支持（mainCubemap 包装、6面 FBO 渲染、双缓冲、samplerCube 输出）

### 阶段 5：GPU 性能深度优化 ✅ 已完成
- [x] FBO 纹理格式降级（GL_RGBA32F → GL_RGBA16F），显存带宽减半
- [x] Uniform location 缓存（ShaderManager 编译后自动缓存，消除每帧 glGetUniformLocation 调用）
- [x] 多显示器共享 Buffer pass（壁纸模式 Buffer pass 只渲染一次，Image pass 按显示器渲染）
- [x] 桌面遮挡检测增强（按显示器粒度检测遮挡，被遮挡 ≥80% 面积的显示器跳过渲染）
- [x] BlitRenderer uniform 缓存（uTex location Init 时一次性缓存）
- [x] MultiPassRenderer 渲染流程拆分（RenderBufferPasses + RenderImagePass）
- [x] Vulkan 迁移评估（结论：暂不迁移，ShaderToy 场景 GPU 瓶颈在 fragment shader 而非 API 开销）
