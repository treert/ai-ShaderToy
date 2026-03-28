# 多 Pass 渲染实现方案

## 概述

为 ShaderToy Desktop 增加完整的多 Pass 渲染支持，使其能运行 shadertoy.com 上使用 Buffer A/B/C/D 的复杂 shader。

## 加载模式

支持三种 shader 加载模式，由 `ShaderProject` 类统一管理：

### 1. 单文件模式（向后兼容）
```bash
ShaderToyDesktop.exe --shader my_shader.glsl
```
- 自动识别为只有 Image pass 的单 Pass 项目
- `--channel0~3` 和 `--channeltype0~3` 命令行参数仍然有效

### 2. JSON 模式（ShaderToy 导出）
```bash
ShaderToyDesktop.exe --shader exported_shader.json
```
- 兼容两种 JSON 格式：API 格式（有 `"Shader"` 包裹层）和直接导出格式（无包裹层）
- 兼容两套字段名：ShaderToy 网站格式（`"type"` / `"filepath"` / `"id"`）和自定义格式（`"ctype"` / `"src"`）
- 识别 `image`、`buffer`、`common`、`cubemap` 四种 pass 类型
- 通过 output id 映射表精确解析 Buffer 间引用关系（替代旧的字符串匹配）
- 纹理路径自动映射到本地 `assets/` 目录（支持 `/media/a/HASH.ext` 和 `/presets/texNN.ext`）
- `keyboard` 输入类型被识别并静默忽略（不阻塞加载）
- 自动判断是单 Pass 还是多 Pass

### 3. 目录模式
```bash
ShaderToyDesktop.exe --shader assets/shaders/multipass_demo/
```
- 按约定文件名加载：
  - `image.glsl` — Image pass（必须）
  - `buf_a.glsl` ~ `buf_d.glsl` — Buffer A~D（可选）
  - `cube_a.glsl` — Cube A pass（可选，使用 mainCubemap 函数）
  - `common.glsl` — 共享代码（可选）
  - `channels.json` — 通道配置（可选）
- 目录名作为项目名

#### channels.json 格式
```json
{
  "image": {
    "iChannel0": "buf_a",
    "iChannel1": "assets/textures/noise.png"
  },
  "buf_a": {
    "iChannel0": "buf_a"
  }
}
```

## ShaderToy JSON 格式

```json
{
  "Shader": {
    "info": { "id": "xxx", "name": "...", "username": "..." },
    "renderpass": [
      {
        "type": "common",
        "code": "// shared functions..."
      },
      {
        "type": "buffer",
        "name": "Buf A",
        "code": "void mainImage(...) { ... }",
        "inputs": [
          { "ctype": "buffer", "channel": 0, "src": "...A..." },
          { "ctype": "texture", "channel": 1, "src": "/media/xxx.png" }
        ]
      },
      {
        "type": "image",
        "code": "void mainImage(...) { ... }",
        "inputs": [
          { "ctype": "buffer", "channel": 0, "src": "...A..." }
        ]
      }
    ]
  }
}
```

## 架构设计

### 高层策略

引入 `ShaderProject` 抽象层，统一管理三种 shader 加载模式。`ShaderProject` 负责从不同来源解析出多 Pass 配置（各 pass 的 GLSL 源码、Common 代码、iChannel 绑定关系），然后传递给 `MultiPassRenderer` 进行渲染。对于单 Pass shader，也统一走 `MultiPassRenderer`（只有 Image pass，无 Buffer pass），消除渲染循环中的分支判断。

### 关键技术决策

1. **统一渲染路径**：无论单 Pass 还是多 Pass，都使用 `MultiPassRenderer` 渲染
2. **ShaderProject 职责**：纯数据层，不依赖 OpenGL
3. **Common 代码注入**：在 uniform 声明之后、用户代码之前插入
4. **JSON 解析库**：nlohmann/json v3.11.3，header-only，通过 FetchContent 引入
5. **向后兼容**：`--channel0~3` 命令行参数在单文件模式下仍然有效

### 数据流
```
--shader 参数
  → ShaderProject.Load() 自动检测类型
  → ShaderProjectData（各 pass 源码 + 通道映射 + Common 代码）
  → MultiPassRenderer 初始化（编译 shader + 创建 FBO）
  → 主渲染循环: RenderAllPasses()
  → Cube A 6面 FBO 渲染（如有）→ Buffer A~D FBO 渲染 → Image pass 输出
  → DebugUI + SwapWindow
```

### 核心类

| 类 | 文件 | 职责 |
|---|---|---|
| `ShaderProject` | `shader_project.h/.cpp` | 统一加载三种模式，输出 `ShaderProjectData` |
| `MultiPassRenderer` | `multi_pass.h/.cpp` | 管理多 Pass 渲染流程（FBO 双缓冲、pass 间引用） |
| `ShaderManager` | `shader_manager.h/.cpp` | 编译单个 pass 的 shader（支持 Common 代码注入） |
| `TextureManager` | `texture_manager.h/.cpp` | 管理外部纹理资源 |

### 数据结构

```cpp
struct ShaderProjectData {
    std::string projectName;
    std::string commonSource;           // Common 共享 GLSL
    std::vector<PassData> bufferPasses; // Buffer A~D
    PassData imagePass;                 // Image pass
    PassData cubeMapPass;               // Cube A pass（可选）
    bool hasCubeMapPass;
    bool isMultiPass;
};

struct PassData {
    std::string name;                   // "Image", "Buffer A", "Cube A", ...
    std::string code;                   // GLSL 源码
    std::array<ChannelBinding, 4> channels; // iChannel0~3 绑定
};

struct ChannelBinding {
    Source source;          // None / Buffer / ExternalTexture / CubeMapPass
    int bufferIndex;        // Buffer A=0, B=1, C=2, D=3
    std::string texturePath;
    ChannelType textureType;
};
```

## 技术要点

### Common 代码注入
`ShaderManager::WrapShaderToySource()` 在 uniform 声明之后、用户代码之前插入 Common 段代码。编译顺序：
```
#version 330 core
uniform vec3 iResolution; ...
uniform sampler2D iChannel0; ...
// === Common code ===
// 共享函数/结构体/常量
// === Common code end ===
// 用户 mainImage 代码
void main() { mainImage(...); }
```

### 降分辨率渲染协调
- Buffer pass 和 Image pass 都在降分辨率下运行
- `MultiPassRenderer::SetImageTargetFBO(renderFBO)` 让 Image pass 渲染到降分辨率 FBO
- 外部 blit 程序负责将降分辨率 FBO 放大到屏幕

### 热加载原子性
热加载时重新加载整个 `ShaderProject`，然后用 `SetupMultiPass()` 重建 `MultiPassRenderer`（先 Clear 再重新配置）。如果任何 pass 编译失败，渲染器被清空（可接受的降级行为）。

### 壁纸模式多 Pass
壁纸模式每个显示器独立调用 `multiPass.RenderAllPasses()`。Buffer FBO 按最大显示器尺寸预分配，通过 `glViewport` 控制各显示器的实际渲染区域。

---

## 实施记录

### 计划步骤

| # | ID | 内容 | 状态 |
|---|-----|------|------|
| 1 | add-json-dep | 在 CMakeLists.txt 中通过 FetchContent 添加 nlohmann/json 依赖，添加 shader_project.cpp 到 SOURCES | ✅ 已完成 |
| 2 | create-shader-project | 创建 shader_project.h/.cpp，实现 ShaderProject 类：自动检测路径类型（.glsl/.json/目录），分别实现三种加载模式的解析逻辑，输出统一的 ShaderProjectData | ✅ 已完成 |
| 3 | enhance-shader-manager | 修改 ShaderManager 支持 Common 代码注入：添加 SetCommonSource() 方法，WrapShaderToySource() 在 uniform 声明后插入 common 代码段 | ✅ 已完成 |
| 4 | enhance-multi-pass | 增强 MultiPassRenderer：添加 Clear()、SetCommonSource()、SetExternalTexture()，支持 iChannelResolution uniform，Image pass 支持渲染到指定 FBO | ✅ 已完成 |
| 5 | integrate-main | 重构 main.cpp 渲染流程：用 ShaderProject 加载 shader，用 MultiPassRenderer 统一渲染单 Pass 和多 Pass，适配降分辨率 blit 和壁纸模式多显示器 | ✅ 已完成 |
| 6 | adapt-hotreload-debug | 适配热加载和调试UI：FileWatcher 监控所有相关文件，多 Pass 全部重编译；DebugUI 展示渲染模式和各 pass 状态 | ✅ 已完成 |
| 7 | create-demo-docs | 创建多 Pass 示例 shader（assets/shaders/multipass_demo/），更新 ai-readme.md 和 Docs/ 技术文档 | ✅ 已完成 |

### 修改文件清单

**新增文件：**
- `src/shader_project.h` — ShaderProject 类声明 + ShaderProjectData 数据结构
- `src/shader_project.cpp` — ShaderProject 实现（三种加载模式）
- `assets/shaders/multipass_demo/image.glsl` — 多 Pass 示例 Image pass
- `assets/shaders/multipass_demo/buf_a.glsl` — 多 Pass 示例 Buffer A
- `assets/shaders/multipass_demo/channels.json` — 通道配置示例

**修改文件：**
- `CMakeLists.txt` — 添加 nlohmann/json FetchContent 依赖 + shader_project 源文件
- `src/shader_manager.h/.cpp` — 添加 SetCommonSource() 和 Common 代码注入
- `src/multi_pass.h/.cpp` — 完全重写，添加 Clear/SetCommonSource/SetExternalTexture/SetImageTargetFBO 等
- `src/main.cpp` — 重构渲染流程，使用 ShaderProject + MultiPassRenderer
- `src/debug_ui.h/.cpp` — 添加多 Pass 信息展示
- `ai-readme.md` — 添加阶段 4 进度
- `Docs/技术架构与实现.md` — 更新技术栈和源码结构
- `Docs/使用说明.md` — 添加多 Pass 使用方法
