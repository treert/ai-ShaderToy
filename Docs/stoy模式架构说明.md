# .stoy 模式架构说明

## 设计目的

ShaderToy 网站的 shader 格式（JSON + GLSL）存在几个痛点：

1. **GLSL → HLSL 翻译不可靠**：项目使用 D3D11 渲染，需要将 GLSL 翻译为 HLSL。尽管已集成 SPIRV-Cross 管线，但 ShaderToy 的非标准扩展（如 `out vec4 fragColor` 签名、`iChannel0` 隐式全局变量）导致翻译层复杂且有边界情况。
2. **纹理绑定不透明**：ShaderToy 使用 `iChannel0~3` 匿名通道绑定纹理，代码中看不出绑定的是什么资源，需要在 JSON 或 UI 中配置。
3. **格式依赖外部生态**：JSON 格式是为 shadertoy.com API 设计的，包含大量冗余字段（`info`、`date`、`likes` 等），不适合本地自定义创作。

`.stoy` 格式的设计目标是：

- **原生 HLSL**：直接编写 HLSL 代码，跳过翻译层，消除翻译错误
- **自描述**：一个文件包含纹理声明、变量需求、多 Pass 代码，不依赖外部 JSON 或目录结构
- **具名引用**：纹理和 Pass 都有名字，代码中直接用名字采样（`MyTex.Sample(MyTex_Sampler, uv)`），可读性强
- **按需注入**：`inner_vars` 只声明用到的内置变量，减少不必要的 cbuffer 占用
- **与 ShaderToy 模式完全独立**：走独立的代码路径，不共享 `ShaderProject`，不存在兼容性问题

---

## 架构概览

### 模式分离

应用有两条完全独立的渲染管线：

```
ShaderToy 模式（.glsl/.json/目录）:
  ShaderProject::Load()
    → ShaderProjectData
    → GLSL-to-HLSL 翻译（SPIRV-Cross）
    → D3D11MultiPass / MultiPassRenderer
    → 渲染

.stoy 模式（.stoy 文件）:
  StoyParser::ParseFile()
    → StoyFileData
    → StoyHlslGenerator::Generate()
    → StoyHlslResult
    → D3D11MultiPass（stoy 模式）
    → 渲染
```

模式在启动时自动检测（`--shader` 参数的文件扩展名为 `.stoy` 即进入 stoy 模式），也可在运行时通过 DebugUI 或托盘菜单切换。stoy 模式强制使用 D3D11 渲染器。

### 数据流

```
┌──────────────┐     ┌───────────────┐     ┌──────────────────┐
│  .stoy 文件  │────→│  StoyParser   │────→│  StoyFileData    │
│  （文本）     │     │  词法+语法分析 │     │  中间表示        │
└──────────────┘     └───────────────┘     └────────┬─────────┘
                                                     │
                                                     ▼
                                           ┌──────────────────┐
                                           │ StoyHlslGenerator│
                                           │ HLSL 代码注入    │
                                           └────────┬─────────┘
                                                     │
                                    ┌────────────────┼────────────────┐
                                    ▼                                  ▼
                          ┌─────────────────┐              ┌──────────────────┐
                          │ StoyHlslResult  │              │ textureBindings  │
                          │ 每个 pass 的     │              │ register 槽位    │
                          │ 完整 HLSL 源码   │              │ → SRV 映射表     │
                          └────────┬────────┘              └────────┬─────────┘
                                   │                                │
                                   ▼                                ▼
                          ┌──────────────────────────────────────────┐
                          │          D3D11MultiPass（stoy 模式）     │
                          │  AddBufferPass / SetImagePass (isHlsl)  │
                          │  SetStoyMode(true)                       │
                          │  SetStoyExternalTextures (register→SRV) │
                          │  SetStoyPassOutputSlots (pass 间引用)    │
                          └────────────────┬─────────────────────────┘
                                           │
                                           ▼
                                    D3D11 渲染循环
                              RenderBufferPasses / RenderImagePass
```

### 核心模块

| 模块 | 文件 | 职责 |
|------|------|------|
| **解析器** | `stoy_parser.h/cpp` | 词法分析 + 递归下降语法分析，输出 `StoyFileData` |
| **HLSL 生成器** | `stoy_hlsl_generator.h/cpp` | 从 `StoyFileData` 为每个 pass 生成完整可编译的 HLSL PS |
| **渲染管线** | `d3d11_multi_pass.h/cpp` | `SetStoyMode` 启用 stoy 纹理绑定（register 槽位映射） |
| **主控** | `main.cpp` | `SetupD3D11Stoy()` / `LoadD3D11StoyTextures()`（独立纹理加载，不走 D3D11TextureManager）/ 热加载分支 |
| **UI** | `debug_ui.h/cpp` | stoy 模式显示独立的 Stoy Shaders 列表 |
| **托盘** | `tray_icon.h/cpp` | 壁纸模式 stoy 文件切换 |

### 关键数据结构

**`StoyFileData`**（解析器输出）：
- `globalSetting` — 渲染设置（format、msaa 等，预留）
- `innerVars` — 需要的内置变量名列表（`iTime`、`iResolution` 等）
- `textures` — 外部纹理声明列表（名称、路径、filter、wrap、声明顺序）
- `commonCode` — 共享 HLSL 代码
- `passes` — pass 列表（名称、HLSL 代码、初始值、声明顺序）

**`StoyHlslResult`**（生成器输出）：
- `passHlsls[]` — 每个 pass 的完整 HLSL 源码（可直接 D3DCompile）
- `textureBindings[]` — 纹理绑定表（名称 → register 槽位 → 是否 pass 输出）
- `totalTextureSlots` — 总纹理槽位数

### HLSL 注入结构

每个 pass 生成的 HLSL 源码按以下顺序组装：

```hlsl
// 1. cbuffer ShaderToyUniforms : register(b0)
//    固定 208 字节布局，与 C++ 侧 ShaderToyConstants 对齐

// 2. inner_vars 别名
//    static float3 iResolution = _ShaderToyUniforms_iResolution.xyz;
//    只生成 inner_vars 中声明的变量

// 3. cbuffer TextureParams : register(b1)
//    每个纹理的 _TexelSize（float4）

// 4. 外部纹理声明
//    Texture2D MyTex : register(t0);
//    SamplerState MyTex_Sampler : register(s0);

// 5. Pass 输出纹理声明（所有 pass 的输出都注入到每个 pass）
//    Texture2D Feedback : register(t1);
//    SamplerState Feedback_Sampler : register(s1);

// 6. common 代码（原样注入）

// 7. 用户 pass 代码（mainImage 函数）

// 8. main 入口包装
//    float4 main(float4 pos : SV_Position) : SV_Target0 {
//        float4 fragColor = float4(0,0,0,1);
//        mainImage(fragColor, pos.xy);
//        return fragColor;
//    }
```

### 纹理绑定策略

- **全量注入**：所有外部纹理和 pass 输出纹理都注入到每个 pass（简化实现，避免按需分析依赖）
- **register 槽位分配**：外部纹理按声明顺序 `t0, t1, ...`，pass 输出纹理紧随其后
- **Pass 间引用语义**：
  - 已执行的 pass → 读取当前帧输出（`outputSRV`）
  - 未执行或自引用的 pass → 读取上一帧输出（`outputSRVPrev`，双缓冲）
- **C++ 侧绑定**：`StoyTextureBinding` 表驱动，`D3D11MultiPass::BindStoyTextures()` 按 register 槽位绑定 SRV

---

## 与 ShaderToy 模式的差异

| 方面 | ShaderToy 模式 | .stoy 模式 |
|------|---------------|------------|
| 着色语言 | GLSL（翻译为 HLSL） | 原生 HLSL |
| 纹理引用 | `iChannel0~3`（匿名） | 具名（`MyTex.Sample(...)`） |
| 纹理数量 | 每 pass 最多 4 个 | 无数量限制（独立纹理加载，不经过 D3D11TextureManager） |
| Pass 引用 | `iChannel` 绑定 Buffer A~D | 直接用 pass 名采样 |
| 渲染后端 | OpenGL 或 D3D11 | 仅 D3D11 |
| 加载器 | `ShaderProject` 类 | `StoyParser` + `StoyHlslGenerator` |
| 文件目录 | `assets/shaders/` | `assets/stoys/` |
| 代码路径 | `LoadTexturesForProject` / `SetupD3D11MultiPass` | `LoadD3D11StoyTextures` / `SetupD3D11Stoy` |

---

## 已知限制（当前版本）

1. **`global_setting` 未接入渲染**：解析器已支持，但 format/msaa/vsync 等设置尚未传递到 D3D11 渲染管线

---

## 未来扩展规划

### 近期（阶段 7 收尾）

- [ ] **VSCode 语法高亮插件**：基于 `stoy_grammar.bnf` 生成 TextMate 语法，支持 `.stoy` 文件的语法着色
- [x] **TextureParams cbuffer 填充**：每帧更新 `_TexelSize` 到 `register(b1)`，使 shader 可查询纹理尺寸
- [x] **纹理 filter/wrap 采样器创建**：根据 `StoyTextureDecl` 的 filter/wrap 属性创建对应的 `ID3D11SamplerState`
- [x] **Image pass 自引用双缓冲**：`EnableImagePassFBO()` 创建 FBO，`SwapBuffers` 交换 Image pass 输出

### 中期

- [ ] **`global_setting` 接入渲染**：支持 format（RT 纹理格式）、msaa、vsync、resolutionScale 等设置
- [ ] **pass init 接入**：支持 `init = float4(...)` 首帧填充和 `init = texture "path"` 初始纹理
- [ ] **错误恢复增强**：解析器支持多错误收集（当前遇到第一个错误即停止），热加载时在 DebugUI 显示行号级错误
- [ ] **HLSL dump**：stoy 模式也输出生成的 HLSL 到 Logs 目录，方便调试 shader 编译错误

### 远期

- [ ] **Compute Shader pass**：新增 `compute_pass` 块类型，支持通用计算（粒子、物理模拟）
- [ ] **UAV 绑定**：支持 `RWTexture2D` 等可写资源，实现 compute → pixel 的数据传递
- [ ] **音频输入**：新增 `audio` 块，支持麦克风或音频文件输入作为纹理
- [ ] **3D 纹理 / Volume**：扩展 texture 块支持 `type = "3d"` 和 `type = "cube"`
- [ ] **include 指令**：支持 `#include "common.hlsl"` 或 stoy 层面的 `include "utils.stoy"` 代码复用
- [ ] **参数 UI 绑定**：新增 `params` 块，声明可在 DebugUI 中用滑条/颜色选取器调节的 uniform 参数

---

## 相关文档

- [.stoy 文件格式设计](stoy文件格式设计.md) — 语法详细说明、各块的字段和语义
- [stoy_grammar.bnf](stoy_grammar.bnf) — BNF 形式化语法规范
- [使用说明](使用说明.md) — 命令行用法和示例
- [D3D11 壁纸渲染后端](D3D11壁纸渲染后端.md) — D3D11 渲染管线基础
