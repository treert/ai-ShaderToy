# 内置 HLSL 智能提示实现方案

> 记录时间：2026-04-04  
> 状态：**已实现**（L0 + L1）

## 前因

### 初始方案：虚拟文档 + 外部扩展转发

最初采用 VS Code 的 Request Forwarding 机制，将 `.stoy` 文件中的 HLSL 代码块通过虚拟文档（`stoy-hlsl://` scheme）映射为独立的 `.hlsl` 文档，再通过 `vscode.commands.executeCommand` 将 hover/completion/definition 请求转发给外部 HLSL 扩展。

**失败原因：**

1. `slevesque.shader`（用户安装的 HLSL 扩展）只提供 TextMate 语法高亮，没有 Language Server，无法响应 `executeHoverProvider` 等命令
2. `TimGJones.hlsltools` 有真正的 HLSL Language Server，部分可用，但已停止维护且有 bug
3. 方案强依赖外部扩展质量，用户需要额外安装特定扩展，增加使用门槛

详见 `Docs/虚拟文档转发方案记录.md`。

### 决策：改为插件内置实现

放弃依赖外部扩展，在 Language Server 内部直接实现 HLSL 区域的智能提示。

## HLSL 语法复杂度分析与分层

| 层级 | 能力 | 实现难度 | 收益 | 决策 |
|------|------|---------|------|------|
| **L0** | 内置函数/类型 hover + completion | 很低 | 高（覆盖 80% 日常场景） | ✅ 实现 |
| **L1** | 用户代码中的函数/struct/变量/宏 | 中等 | 高（跨块跳转） | ✅ 实现 |
| **L2** | 类型推导、成员访问（`.xyz`、`.Sample()`） | 很高 | 中（接近半个编译器前端） | ❌ 不做 |
| **L3** | 语法错误、类型不匹配诊断 | 极高 | 中（编译时也能发现） | ❌ 不做 |

L2/L3 不做的原因：HLSL 语法极其复杂（模板、swizzle、隐式类型转换、重载解析），实现等于写半个编译器前端，工作量巨大且 shader 编译时 DXC/FXC 会报完整错误。

## 架构设计

### 之前（虚拟文档转发）

```
Client (extension.ts)
├── Middleware 拦截 HLSL 请求
├── VirtualDocProvider 生成虚拟 .hlsl 文档
├── RequestForwarder 通过 executeCommand 转发
└── PositionMapper 双向行号映射
Server (server.ts)
└── 只处理 DSL 区域
```

客户端很重（middleware + 虚拟文档 + 转发 + 映射），服务端很轻。

### 现在（内置实现）

```
Client (extension.ts)
└── 纯 Language Client，无 middleware

Server (server.ts)
├── isInHlslBlock() 路由判断
├── DSL 区域 → dslHoverProvider / dslCompletionProvider / dslDefinitionProvider
└── HLSL 区域 → hlslHoverProvider / hlslCompletionProvider / hlslDefinitionProvider
```

客户端极简，所有智能逻辑统一在服务端。路由逻辑只需判断光标行号是否在 `codeRange` 范围内。

## 技术实现

### L0：HLSL 内置知识库（`hlslBuiltins.ts`）

纯数据文件，包含：
- **60+ 内置函数签名**：数学（abs/cos/sin/pow/lerp/smoothstep...）、向量（dot/cross/normalize/reflect...）、矩阵、导数、纹理采样方法等
- **30+ 内置类型**：标量、向量（float2/3/4）、矩阵（float4x4）、纹理/采样器类型
- 快速查找 Map（`findHlslFunction`、`findHlslType`）

### L1：HLSL 符号扫描器（`hlslSymbolScanner.ts`）

**设计决策：正则模式匹配，不做完整语法分析。**

用正则从用户 HLSL 代码中提取 4 类符号：

| 符号类型 | 匹配模式 | 示例 |
|---------|---------|------|
| 函数 | `returnType funcName(params) {` | `float3 myFunc(float2 uv, float t) {` |
| Struct | `struct Name {` + 逐行解析成员 | `struct MyData { float3 pos; };` |
| 变量 | `[static] [const] type name =\|;` | `static const float PI = 3.14159;` |
| 宏 | `#define NAME [value]` | `#define MAX_STEPS 64` |

**扫描范围：** common 块 + 各 pass 的 code 块。

**符号可见性规则：**
- common 中定义的符号 → 对所有 pass 可见
- pass 中定义的符号 → 仅对当前 pass 可见
- 这与 C++ HLSL 注入器的实际行为一致（common 代码注入到每个 pass 之前）

**行号对齐：** `code`（`ls.value`）可能以 `\n` 开头（`[=[` 后紧跟换行），但 `codeRange.startLine` 已跳过该换行。扫描前需剥离开头的 `\n`/`\r\n`，确保 `lines[0]` 对齐到 `codeRange.startLine`。

### 特殊支持：框架注入变量

除了 HLSL 内置函数和用户符号，还特殊支持框架注入到 HLSL 中的变量：

| 来源 | 注入变量 | hover 内容 |
|------|---------|-----------|
| `inner_vars { iTime }` | `iTime` | `float iTime` — 运行时间 |
| `texture MyTex = "..."` | `MyTex` | `Texture2D MyTex` — 路径 + filter/wrap |
| | `MyTex_Sampler` | `SamplerState` — 采样器 |
| | `MyTex_TexelSize` | `float4` — (1/w, 1/h, w, h) |
| `pass Feedback { ... }` | `Feedback` | `Texture2D Feedback` — Pass buffer |
| | `Feedback_Sampler` | `SamplerState` — 采样器 |
| | `Feedback_TexelSize` | `float4` — (1/w, 1/h, w, h) |

texture 和 pass 的派生变量支持 hover + completion + go-to-definition（跳到 DSL 层的声明位置）。

### 符号缓存

- 每次文档变更时**立即**解析 + 扫描符号（保证 hover/completion 使用最新数据）
- 诊断推送使用 300ms **防抖**（避免高频编辑时反复推送）
- 两个缓存独立：`DocumentManager`（StoyDocument 解析结果）+ `symbolCache`（HlslSymbol 数组）

## 文件清单

### 新增

| 文件 | 职责 |
|------|------|
| `src/hlslBuiltins.ts` | HLSL 内置函数/类型知识库（L0） |
| `src/hlslSymbolScanner.ts` | 用户 HLSL 符号正则扫描器（L1） |
| `src/server/hlslHoverProvider.ts` | HLSL 区域 hover |
| `src/server/hlslCompletionProvider.ts` | HLSL 区域 completion |
| `src/server/hlslDefinitionProvider.ts` | HLSL 区域 go-to-definition |
| `src/server/utils.ts` | 公共工具函数（getWordAtPosition） |

### 修改

| 文件 | 变更 |
|------|------|
| `src/server/server.ts` | 添加 `isInHlslBlock()` 路由 + 符号缓存 |
| `src/extension.ts` | 简化为纯 Language Client（移除 middleware/虚拟文档） |
| `src/server/dslHoverProvider.ts` | 使用公共 `utils.ts` |
| `src/server/dslDefinitionProvider.ts` | 使用公共 `utils.ts` |

### 废弃（保留在 git 历史中，不再被引用）

| 文件 | 原职责 |
|------|-------|
| `src/virtualDocProvider.ts` | 虚拟文档 ContentProvider |
| `src/requestForwarder.ts` | 请求转发器 |
| `src/hlslGenerator.ts` | 虚拟文档内容生成（8 层结构） |
| `src/positionMapper.ts` | 物理行号 ↔ 虚拟行号映射 |

## 已知限制

1. **不支持类型推导**：`.` 触发的成员补全（如 `myStruct.pos`）需要 L2 级别，当前不做
2. **不支持 HLSL 诊断**：语法错误需要编译 shader 时发现
3. **正则扫描的局限**：无法匹配数组声明（`float4 colors[4];`）、跨行块注释中的假匹配等边界情况
4. **补全列表可能有重名**：如果用户定义了和内置函数同名的符号，补全列表会出现两个

## 未来扩展方向

- 补充更多 HLSL 内置函数（`clip`、`asfloat`/`asint`/`asuint` 等）
- struct 成员的 `.` 补全（需要简单的上下文类型推导）
- 如果未来出现高质量的 HLSL Language Server 扩展，可重新启用虚拟文档转发方案
