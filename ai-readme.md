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
  - `assets/shaders/` — ShaderToy 格式 shader（.glsl/.json/.hlsl 和目录模式）
  - `assets/stoys/` — .stoy 自定义格式 shader（独立模式，不与 ShaderToy 格式混合）
- `third_party/` — 第三方依赖（git submodule：SDL2、GLAD、ImGui、nlohmann/json）
- `Docs/` — 项目知识库（技术文档，供 AI 读写，也供用户查阅）
  - `Docs/疑难问题记录/` — 疑难问题排查记录（OpenGL/D3D11/Windows 平台相关的问题根因分析与解决方案）
  - `Docs/有价值的讨论/` — 技术深度讨论记录（增量解析、parser 理论等有参考价值的讨论整理）
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
- [x] 托盘菜单 Debug Overlay 切换（运行时开关调试信息叠加显示，支持延迟初始化 ImGui）
- [x] 打开文件对话框选择 Shader（壁纸模式托盘 Open Shader... + 窗口模式 Browse Shader... 按钮，支持 .glsl/.json/目录，带格式验证和错误提示）
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

### 阶段 6：D3D11 壁纸后端 ✅ 已完成
**目标**：解决 Win11 24H2+ (Build 26200+) 壁纸模式黑屏问题（Progman 使用 WS_EX_NOREDIRECTIONBITMAP，OpenGL swap chain 不可见）
- [x] D3D11Renderer（Device/Context/多 SwapChain 管理，全屏三角形 VS）
- [x] GLSL-to-HLSL 翻译器（基于正则替换，后续可升级为 glslang + SPIRV-Cross）
- [x] D3D11ShaderManager（HLSL 模板 + D3DCompile + ConstantBuffer + SRV 绑定）
- [x] D3D11TextureManager（stb_image + Texture2D/CubeMap SRV）
- [x] D3D11MultiPass（多 Pass RTV + 双缓冲 + CubeMap 6 面渲染）
- [x] D3D11BlitRenderer（降分辨率 RTV + blit PS）
- [x] main.cpp 集成（壁纸模式使用 D3D11 路径，窗口模式保留 OpenGL）
- [x] CMakeLists.txt 更新（d3d11/dxgi/d3dcompiler 链接）
- [x] Debug 代码清理（移除红色 glClear 测试、drawable 日志）
- [x] 实机测试（Win11 24H2 壁纸模式验证）
- [x] 壁纸模式 DebugUI（ImGui D3D11 后端适配）
- [x] 壁纸模式 D3D11 路径 DebugUI overlay 接入（--debug 参数 + 托盘切换，暂停/正常渲染均支持）
- [x] SPIRV-Cross 升级（glslang + SPIRV-Cross 管线，翻译通过率 30/30 = 100%，旧正则翻译器保留为降级方案）
- [x] --translate 翻译模式（GLSL→HLSL 翻译输出到 Logs 目录，单 pass 输出单文件，多 pass 输出到子目录）
- [x] --translate 翻译模式编译验证（D3DCompile ps_5_0 验证每个 pass，错误信息输出到控制台和 .hlsl 文件末尾注释）
- [x] 壁纸模式运行时 HLSL dump（初始加载和热加载成功后自动翻译+编译验证，输出到 Logs/wallpaper-runtime/）
- [x] WrapShaderToyHlsl 公共函数抽取（从 D3D11ShaderManager 抽取为 glsl_to_hlsl.h/cpp 自由函数，翻译模式无需 D3D11）
- [x] 窗口模式 D3D11 渲染（--d3d11 开关，复用壁纸模式 D3D11 组件，支持 resize/DebugUI/热加载）
- [x] DebugUI 双后端支持（OpenGL + D3D11 ImGui 后端，InitD3D11 方法，按 useD3D11_ 分发渲染调用）
- [x] 统一 HLSL 翻译输出路径（translate-mode / wallpaper-mode / window-mode 三个子目录，D3D11 模式自动 dump）
- [x] HLSL dump 按格式区分目录（@json / @dir 后缀，避免同名 shader 冲突）
- [x] D3D11 窗口模式崩溃修复（跳过无 GL 上下文时的 OpenGL 纹理加载）
- [x] --quiet 静默模式（不输出到控制台，只写日志文件）
- [x] 日志文件开头记录完整命令行（方便复制重跑）
- [x] HLSL 文件来源注释（标注源 shader 路径和 pass 名，提醒不要手改）
- [x] 多进程帧率降半修复（DXGI ALLOW_TEARING + SetMaximumFrameLatency + 高精度可等待定时器替代 SDL_Delay）

### 阶段 7：.stoy 自定义 Shader 格式 🔧 进行中
**目标**：设计并实现 `.stoy` 自定义 shader 描述格式，替代 shadertoy.com JSON 格式，支持具名纹理、多 Pass、内置变量声明等
- [x] .stoy 文件格式设计（Docs/stoy文件格式设计.md）
- [x] BNF 语法规范（Docs/stoy_grammar.bnf，用于 VSCode 语法高亮插件）
- [x] .stoy 解析器（parser）实现（stoy_parser.h/cpp，手写递归下降，完整错误报告）
- [x] HLSL 代码注入（stoy_hlsl_generator.h/cpp，cbuffer/别名/纹理声明/mainImage 包装）
- [x] 渲染管线集成（D3D11MultiPass .stoy 纹理绑定，独立于 ShaderToy 模式）
- [x] .stoy 独立模式分支（不经过 ShaderProject，走 StoyParser → StoyHlslGenerator → D3D11MultiPass 独立路径）
- [x] .stoy 示例文件（assets/stoys/minimal.stoy + assets/stoys/feedback_demo.stoy）
- [x] .stoy 模式架构说明文档（Docs/stoy模式架构说明.md）
- [x] .stoy 模式 HLSL dump（初始加载+热加载后输出到 Logs/stoy-mode/，每个 pass 一个 .hlsl，D3DCompile 编译验证）
- [x] VSCode 语法高亮插件（stoy-vscode-plugin/，纯声明式 TextMate Grammar，含嵌入式 HLSL 子语法）
- [x] VSCode Language Server 智能插件
  - [x] TypeScript 项目初始化（LSP client/server 分离架构，esbuild 打包）
  - [x] TypeScript .stoy 解析器（从 C++ 移植，手写递归下降，精确行号范围）
  - [x] DSL 层智能提示（补全/悬停/跳转定义/诊断 4 个 provider）
  - [x] 内置 HLSL 智能提示（不依赖外部扩展）
    - [x] HLSL 内置知识库（60+ 函数签名 + 30+ 类型，hlslBuiltins.ts）
    - [x] HLSL 符号扫描器（正则提取用户函数/struct/变量/宏/函数参数，hlslSymbolScanner.ts）
    - [x] HLSL hover（内置函数/类型 + 框架变量 + texture/pass 派生变量 + 用户符号 + mainImage 入口函数）
    - [x] HLSL hover 内置函数文档链接（点击跳转微软官方 HLSL Reference）
    - [x] HLSL completion（全部上述 + HLSL 关键字 + struct 成员）
    - [x] HLSL completion 排序优化（框架内置变量优先 + inner_vars 声明状态提示）
    - [x] HLSL go-to-definition（用户符号 + texture/pass/inner_vars 跳转）
    - [x] HLSL hover 内置变量 inner_vars 声明状态提示
    - [x] 文档大纲 Outline（DSL 顶层块 + HLSL 用户符号层级显示）
    - [x] Outline inner_vars 显示所有内置变量（已声明正常 / 未声明灰色+标注）
    - [x] Semantic Token 内置变量高亮（LSP 语义着色覆盖 TextMate，inner_vars + HLSL 代码块中 iTime/iResolution 等精确染色）
    - [x] Semantic Token pass/texture 名称及衍生变量高亮（pass name、texture name 及 _Sampler/_TexelSize 衍生变量在 DSL 声明和 HLSL 代码块中精确染色）
  - [x] Server 端 DSL/HLSL 路由（按光标位置自动切换 provider）
  - [x] 扩展主入口（Language Client + 配置驱动 middleware）
  - [x] HLSL provider 配置切换（`stoy.hlsl.provider` 设置项，builtin/externalLsp 两种模式）
    - [x] package.json 配置声明（枚举 + 描述 + 默认值）
    - [x] server.ts 配置感知（workspace/configuration，externalLsp 模式下 HLSL providers 返回空）
    - [x] extension.ts middleware（externalLsp 模式注册虚拟文档 + 拦截转发 HLSL 请求）
    - [x] 配置变更提示重载窗口
  - 虚拟文档 + 外部扩展转发方案（externalLsp 模式可选启用，详见 Docs/虚拟文档转发方案记录.md）
  - [x] Parser 容错解析（panic mode recovery，错误不中断后续块解析，Outline/补全/hover 不因单个错误丢失信息）
    - [x] fake_file_lsp 模式（实验性，通过假 file:// URI 转发给 shader-language-server，仅提供 DXC 诊断，详见 Docs/fake_file_lsp技术说明.md）
    - [x] `stoy.hlsl.provider` 配置项扩展（新增 `fake_file_lsp` 选项，标注实验性）
    - [x] 假 file:// URI 生成与管理（fakeUriManager.ts，同目录策略支持 #include）
    - [x] 第二个 LanguageClient 生命周期管理（fakeFileLsp.ts，查找 shader-validator 扩展二进制）
    - [x] 假文档 didOpen/didChange/didClose 同步（复用 hlslGenerator.ts 8 层拼接）
    - [x] Diagnostics 拦截与行号映射（假 URI → .stoy 物理文件，过滤注入代码区域）
    - [x] 统一日志系统（logger.ts，info/debug/error 三级，`stoy.debug.log` 配置开关，本地文件日志，路径输出到 outputChannel）
  - 待实施方案：内嵌 shader-language-server（DXC 实时诊断 + tree-sitter AST，HLSL 层全面替代内置正则方案，详见 Docs/shader-language-server内嵌方案.md）
