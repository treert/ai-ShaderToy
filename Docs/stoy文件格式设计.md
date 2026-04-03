# .stoy 文件格式设计文档

## 概述

`.stoy` 是 ai-ShaderToy 项目的自定义 shader 描述格式，用于替代 shadertoy.com 的 JSON 格式。它借鉴了 Unity ShaderLab 的设计理念，在一个文件中自描述纹理资源、渲染设置、内置变量声明和多 Pass HLSL 代码。

**设计原则**：
- 不兼容 shadertoy.com 格式，独立设计
- 应用框架只注入必要的运行时数据（屏幕尺寸、时间、鼠标、帧率、日期）
- 纹理、渲染设置等在 `.stoy` 文件中自描述
- 外部纹理具名声明，代码中直接用名字引用
- 注释使用 Lua 风格 `--`

---

## 文件结构

一个 `.stoy` 文件由以下顶层块组成（顺序不强制，但建议按此顺序书写）：

```
┌─────────────────────────────────┐
│  global_setting { ... }         │  可选，D3D11 全局渲染设置
├─────────────────────────────────┤
│  inner_vars { ... }             │  可选，声明需要的框架内置变量
├─────────────────────────────────┤
│  texture Name = "path" { ... }  │  可选，声明外部纹理资源（可多个）
├─────────────────────────────────┤
│  common [=[ ... ]=]             │  可选，共享 HLSL 代码
├─────────────────────────────────┤
│  pass Name { ... }              │  必须，至少一个 pass
│  pass Name { ... }              │  可多个，按声明顺序执行
│  ...                            │  最后一个 pass 作为最终输出
└─────────────────────────────────┘
```

---

## 1. `global_setting` 块

全局 D3D11 渲染设置，为未来扩展预留。

```lua
global_setting {
    format           = "R8G8B8A8_UNORM"   -- 默认 render target 格式
    msaa             = 1                    -- 多重采样
    vsync            = true                 -- 垂直同步
    resolution_scale = 1.0                  -- 分辨率缩放
}
```

**说明**：
- 所有字段可选，有默认值
- 整个块可省略

---

## 2. `inner_vars` 块

声明需要的框架内置变量。只有声明了的变量才会被注入到 HLSL 代码中。

```lua
inner_vars {
    iTime
    iTimeDelta
    iResolution
    iMouse
    iFrame
    iFrameRate
    iDate
    iSampleRate
}
```

**可用内置变量列表**：

| 变量名 | HLSL 类型 | 说明 |
|--------|-----------|------|
| `iResolution` | `float3` | 视口分辨率 (width, height, 1.0) |
| `iTime` | `float` | 运行时间（秒） |
| `iTimeDelta` | `float` | 帧间隔时间（秒） |
| `iFrame` | `int` | 当前帧号 |
| `iFrameRate` | `float` | 当前帧率 |
| `iMouse` | `float4` | 鼠标状态 (xy=当前位置, zw=点击位置) |
| `iDate` | `float4` | 日期 (year, month, day, seconds) |
| `iSampleRate` | `float` | 音频采样率（固定 44100） |

**说明**：
- 简单列举变量名即可，不需要写类型（解析器内部维护固定映射表）
- 整个块可省略（不使用任何内置变量）
- 最多只能有一个 `inner_vars` 块，重复声明解析器报错

### 注入策略

`inner_vars` 采用**别名控制可见性**策略：

- C++ 侧的 `ShaderToyUniforms` cbuffer 始终是完整的固定布局（208 字节，与 C++ `ShaderToyConstants` 结构体严格对齐），不会按需裁剪
- `inner_vars` 中声明的变量，会在 HLSL 中生成对应的 `static` 别名（如 `static float3 iResolution = _iResolution4.xyz;`），使其在用户代码中可用
- 未声明的变量在 cbuffer 中仍然存在，但用户代码中不可见（没有别名）
- 这样 C++ 侧无需任何改动，只需控制 HLSL 注入的别名即可

---

## 3. `texture` 声明

声明外部纹理资源，每个纹理有唯一名字。

```lua
texture 纹理名 = "文件路径" {
    filter = "linear"    -- 过滤模式: "linear" | "point"
    wrap   = "repeat"    -- 寻址模式: "repeat" | "clamp" | "mirror"
}
```

**示例**：

```lua
texture NoiseTex = "textures/noise.png" {
    filter = "linear"
    wrap   = "repeat"
}

texture LogoTex = "textures/logo.png" {
    filter = "point"
    wrap   = "clamp"
}
```

**说明**：
- 纹理名必须是合法变量名（字母/下划线开头，字母/数字/下划线组成）
- 纹理名全局唯一，不能与 pass 名冲突，不能是保留字（见下文「名称保留字」章节）
- 配置块 `{}` 可选，有默认值（filter=linear, wrap=clamp）
- 同一张图片可以声明多个纹理（不同名字、不同采样设置）
- 纹理类型默认为 `texture2d`（未来扩展 `cubemap`）
- 采样器配置只在 `texture` 声明处设置，pass 不可覆盖。如需不同设置，用同一张图片声明不同纹理
- 纹理文件路径**相对于 `.stoy` 文件所在目录**解析（见下文「文件路径解析」章节）

### 自动注入

每个声明的纹理会自动在所有 pass 中注入以下 HLSL 变量：

```hlsl
Texture2D<float4> NoiseTex : register(tN);       // 纹理对象
SamplerState      NoiseTex_Sampler : register(sN); // 采样器
float4            NoiseTex_TexelSize;              // (1/width, 1/height, width, height)
```

`_TexelSize` 变量存放在独立的 `cbuffer TextureParams : register(b1)` 中，由框架在每帧渲染前填充。

---

## 4. `common` 块

共享 HLSL 代码，会被注入到每个 pass 的代码之前。

```lua
common [=[
    float hash(float p) {
        return frac(sin(p) * 43758.5453);
    }

    float noise(float2 uv) {
        // ...
    }
]=]
```

**说明**：
- 使用 Lua 长字符串语法 `[=[ ... ]=]` 包裹 HLSL 代码
- 可省略
- 最多只能有一个 `common` 块，重复声明解析器报错

---

## 5. `pass` 块

渲染通道，包含配置和 HLSL 代码。

### 基本结构

```lua
pass PassName {
    -- 可选配置项
    init = float4(0, 0, 0, 1)              -- 自引用时的初始值
    init = texture "textures/init.png"      -- 或用纹理初始化

    -- HLSL 代码（必须）
    code [=[
        void mainImage(inout float4 fragColor, float2 fragCoord) {
            // ...
        }
    ]=]
}
```

### 配置项

| 配置项 | 说明 | 默认值 |
|--------|------|--------|
| `init` | 自引用时双缓冲纹理的初始值 | `float4(0, 0, 0, 0)` 全黑透明 |

#### `init` 语法

| 形式 | 语法 | 说明 |
|------|------|------|
| 默认（不写） | 无 | 初始值 = `float4(0, 0, 0, 0)` |
| 指定颜色 | `init = float4(r, g, b, a)` | 两张双缓冲纹理都 Clear 为该颜色 |
| 指定纹理 | `init = texture "path/to/init.png"` | 用外部纹理填充初始内容 |

> 只允许写一个 `init`，颜色和纹理二选一。`init = texture` 中的纹理路径同样**相对于 `.stoy` 文件所在目录**解析。

### `code` 块

- 每个 pass **必须**包含一个 `code [=[ ... ]=]` 块，缺少时解析器报错
- `code` 块内是 HLSL 代码，必须实现 `void mainImage(inout float4 fragColor, float2 fragCoord)` 函数

### Pass 执行规则

1. **按声明顺序执行**，最后一个 pass 作为最终输出显示到屏幕
2. 每个 pass 默认输出一张 2D 纹理
3. 所有 pass 统一使用 `pass Name { ... }` 包裹格式，即使没有配置项也不省略 `{}`
4. pass 名称必须是合法变量名，全局唯一，不能与纹理名冲突，不能是保留字（见下文「名称保留字」章节）

### 最后一个 Pass 的渲染目标

最后一个 pass 的渲染目标取决于它是否被其他 pass 引用：

| 情况 | 渲染目标 | 说明 |
|------|---------|------|
| **未被任何 pass 引用**（最常见） | 直接渲染到 back buffer | 无需双缓冲，节省一张纹理和一次 blit |
| **被其他 pass 引用或自引用** | 双缓冲 RTV + 最终 blit 到 back buffer | 需要双缓冲以供其他 pass 读取 |

> 解析器在加载时静态分析所有 pass 的引用关系，决定最后一个 pass 是否需要双缓冲。

### Pass 间引用

- 代码中直接使用其他 pass 的名字作为纹理变量
- **引用语义**（与 shadertoy.com 一致）：
  - 引用**已执行**的 pass → 读取**当前帧**数据
  - 引用**未执行**的 pass（包括自引用）→ 读取**上一帧**数据

每个 pass 会自动注入以下 HLSL 变量（供其他 pass 引用）：

```hlsl
Texture2D<float4> PassName : register(tN);        // pass 输出纹理
SamplerState      PassName_Sampler : register(sN); // 采样器
float4            PassName_TexelSize;               // (1/width, 1/height, width, height)
```

`PassName_TexelSize` 同样存放在 `cbuffer TextureParams : register(b1)` 中。

---

## 6. 注入策略

采用**全量注入**策略：

- `.stoy` 文件中声明的**所有** `texture` 和**所有** `pass` 输出纹理，都会注入到**每个** pass 的 HLSL 代码中
- 不做静态分析（不扫描代码中实际引用了哪些纹理）
- HLSL 编译器对未使用的变量不会报错
- D3D11 register 槽位充足（t0-t127），不会成为瓶颈

---

## 7. `_TexelSize` 的 cbuffer 布局

所有纹理和 pass 的 `_TexelSize` 变量统一存放在一个独立的 cbuffer 中：

```hlsl
cbuffer TextureParams : register(b1) {
    // 外部纹理的 TexelSize（按 texture 声明顺序）
    float4 NoiseTex_TexelSize;    // (1/w, 1/h, w, h)
    float4 LogoTex_TexelSize;
    // pass 输出纹理的 TexelSize（按 pass 声明顺序）
    float4 Feedback_TexelSize;
    float4 Blur_TexelSize;
    float4 Final_TexelSize;
};
```

**说明**：
- `register(b0)` 已被 `ShaderToyUniforms` 占用，`TextureParams` 使用 `register(b1)`
- 布局顺序：先外部纹理（按 `texture` 声明顺序），再 pass 输出（按 `pass` 声明顺序）
- C++ 侧在每帧渲染前填充此 cbuffer，纹理尺寸在加载时已知，pass 输出尺寸等于视口尺寸

---

## 8. 名称保留字

pass 名和 texture 名**不能**使用以下保留字，解析器遇到时应报错：

### .stoy 格式关键字
`global_setting`, `inner_vars`, `texture`, `common`, `pass`, `init`, `code`, `float4`, `true`, `false`

### 内置变量名
`iResolution`, `iTime`, `iTimeDelta`, `iFrame`, `iFrameRate`, `iMouse`, `iDate`, `iSampleRate`

### HLSL 关键字和内置类型（部分列举）
`float`, `float2`, `float3`, `float4`, `int`, `int2`, `int3`, `int4`, `uint`, `uint2`, `uint3`, `uint4`, `bool`, `half`, `double`, `void`, `return`, `if`, `else`, `for`, `while`, `do`, `switch`, `case`, `break`, `continue`, `discard`, `struct`, `cbuffer`, `static`, `const`, `inout`, `in`, `out`, `Texture2D`, `Texture3D`, `TextureCube`, `SamplerState`, `SV_Position`, `SV_Target0`, `register`, `main`, `mainImage`

> 完整的 HLSL 保留字列表较长，解析器实现时建议维护一份白名单或黑名单。上述列表覆盖了最常见的冲突场景。

---

## 9. 文件路径解析

所有文件路径（纹理路径、`init = texture` 路径）统一**相对于 `.stoy` 文件所在目录**解析。

```
project/
├── shaders/
│   ├── demo.stoy          ← .stoy 文件
│   └── textures/
│       ├── noise.png       ← texture NoiseTex = "textures/noise.png"
│       └── init.png        ← init = texture "textures/init.png"
```

---

## 10. 块的唯一性约束

| 块 | 最大数量 | 重复时行为 |
|---|---------|----------|
| `global_setting {}` | 1 | 解析器报错 |
| `inner_vars {}` | 1 | 解析器报错 |
| `common [=[ ]=]` | 1 | 解析器报错 |
| `texture Name = "..." {}` | 不限 | 名称不能重复 |
| `pass Name { }` | 不限（至少 1 个） | 名称不能重复 |

---

## 11. 注释

支持 Lua 风格的单行注释：

```lua
-- 这是注释
texture NoiseTex = "textures/noise.png" {  -- 行尾注释
    filter = "linear"
}
```

---

## 最简示例

最小可用的 `.stoy` 文件只需要一个 pass：

```lua
-- minimal.stoy
pass Main {
    code [=[
        void mainImage(inout float4 fragColor, float2 fragCoord) {
            fragColor = float4(1, 0, 0, 1);
        }
    ]=]
}
```

---

## 完整示例

```lua
-- feedback_demo.stoy

global_setting {
    vsync = true
}

inner_vars {
    iTime
    iResolution
    iMouse
    iFrame
}

texture NoiseTex = "textures/noise.png" {
    filter = "linear"
    wrap   = "repeat"
}

texture LogoTex = "textures/logo.png" {
    filter = "point"
    wrap   = "clamp"
}

common [=[
    float hash(float p) {
        return frac(sin(p) * 43758.5453);
    }
]=]

-- Feedback pass: self-referencing, reads its own previous frame output
pass Feedback {
    init = float4(0, 0, 0, 1)

    code [=[
        void mainImage(inout float4 fragColor, float2 fragCoord) {
            float2 uv = fragCoord / iResolution.xy;
            float4 prev = Feedback.Sample(Feedback_Sampler, uv);
            float4 noise = NoiseTex.Sample(NoiseTex_Sampler, uv * 4.0);
            fragColor = prev * 0.98 + noise * 0.02;
        }
    ]=]
}

-- Blur pass: reads Feedback (already executed this frame → current frame data)
pass Blur {
    code [=[
        void mainImage(inout float4 fragColor, float2 fragCoord) {
            float2 uv = fragCoord / iResolution.xy;
            float2 texel = Feedback_TexelSize.xy;
            float4 sum = float4(0, 0, 0, 0);
            for (int x = -2; x <= 2; x++) {
                for (int y = -2; y <= 2; y++) {
                    sum += Feedback.Sample(Feedback_Sampler, uv + float2(x, y) * texel);
                }
            }
            fragColor = sum / 25.0;
        }
    ]=]
}

-- Final pass: last pass = screen output (not referenced by anyone → renders directly to back buffer)
pass Final {
    code [=[
        void mainImage(inout float4 fragColor, float2 fragCoord) {
            float2 uv = fragCoord / iResolution.xy;
            float4 blurred = Blur.Sample(Blur_Sampler, uv);
            float4 logo = LogoTex.Sample(LogoTex_Sampler, uv);
            fragColor = lerp(blurred, logo, logo.a);
        }
    ]=]
}
```

---

## 设计决策汇总

| # | 问题 | 决策 |
|---|------|------|
| 1 | pass 声明与代码块关系 | 方案 B：包裹模式，配置和 `code [=[ ]=]` 都在 `pass {}` 内 |
| 2 | 无 channel 的 pass | 不省略 `{}`，统一包裹格式 |
| 3 | 输入/纹理系统 | 直接用名字引用，自动注入 `_Sampler` / `_TexelSize` |
| 4 | 全局设置 | `global_setting {}`，专注 D3D11 渲染设置，替代原 `properties` |
| 5 | CubeMap | 待定（`cubepass` 关键字，后续细说） |
| 6 | 扩展名 | `.stoy` |
| 7 | pass 名称 | 合法变量名，无引号，全局唯一，可自引用 |
| 8 | 自引用初始值 | `init =` 统一语法：默认全黑，可选颜色或纹理 |
| 9 | 内置变量 | `inner_vars {}` 块，简单列举名字 |
| 10 | 引用语义 | 与 shadertoy.com 一致（已执行=当前帧，未执行=上一帧） |
| 11 | `_TexelSize` 后缀 | `纹理名_TexelSize`，格式 `(1/w, 1/h, w, h)` |
| 12 | 采样器配置 | 只在 `texture` 声明处设置，pass 不可覆盖 |
| 13 | 注入策略 | 全量注入，所有声明的纹理/pass 都注入到每个 pass |
| 14 | `_TexelSize` cbuffer | 独立 `cbuffer TextureParams : register(b1)` |
| 15 | 名称保留字 | pass/texture 名不能是 .stoy 关键字、内置变量名、HLSL 关键字 |
| 16 | `inner_vars` cbuffer 策略 | 固定布局 + 别名控制可见性（C++ 侧不变） |
| 17 | 文件路径解析 | 相对于 `.stoy` 文件所在目录 |
| 18 | 块唯一性 | `global_setting` / `inner_vars` / `common` 最多各一个，重复报错 |
| 19 | `code` 块必须性 | 每个 pass 必须包含 `code` 块，缺少报错 |
| 20 | 最后一个 pass 渲染目标 | 未被引用→直接渲染到 back buffer；被引用→双缓冲+blit |

---

## 待定事项

- **CubeMap 支持**：计划使用 `cubepass` 关键字，具体语法待定
- **`global_setting` 字段扩展**：随 D3D11 功能需求逐步添加
