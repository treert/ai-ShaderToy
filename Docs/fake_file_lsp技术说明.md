# fake_file_lsp 模式技术说明

> 记录时间：2026-04-04
> 状态：**已实现**（实验性）

## 概述

`fake_file_lsp` 是 `.stoy` VSCode 插件的第三种 HLSL 智能提示模式，通过假 `file://` URI 启动插件内置的 shader-language-server（来自 [shader-validator](https://github.com/antaalt/shader-validator) 项目的预编译二进制），获得 DXC 实时诊断功能。所有 hover/completion/go-to-definition 均由内置 builtin 方案处理。

## 背景与动机

- **builtin** 模式：内置正则方案，60+ 函数签名 + 正则符号扫描，功能有限
- **externalLsp** 模式：通过 `stoy-hlsl://` 自定义 scheme 虚拟文档转发，因 shader-language-server 硬编码 `scheme: 'file'` 限制而**失败**
- **fake_file_lsp** 模式：绕过 scheme 限制，使用假 `file://` URI + `didOpen` 传内容的方式，直接启动插件内置的预编译二进制，无需安装外部扩展，无需 Fork

### 核心洞察

shader-language-server 的 `watch_main_file` **不读取磁盘文件**，完全使用 `didOpen` 传入的 `text` 参数。`file_path` 仅用于 tree-sitter 文件标识和 `#include` 相对路径基准。

如果假 URI 的目录部分与 `.stoy` 物理文件所在目录相同，`#include` 的相对路径基准就是正确的物理目录，天然支持 `#include`。

## 架构

```
extension.ts
├── LanguageClient #1 → 我们的 Language Server（DSL + 内置 HLSL 提示）
│     documentSelector: [{ scheme: 'file', language: 'stoy' }]
│     无 middleware — hover/completion/definition 全部由 builtin 处理
│
└── LanguageClient #2 → shader-language-server（仅 DXC 诊断）
      通过 FakeFileLspManager 管理
      接收假 file:// URI 的 didOpen/didChange/didClose
      diagnostics 拦截 + 行号映射回 .stoy 物理文件
```

### 数据流

```
用户编辑 .stoy 文件
    ↓
StoyParser 解析 → 提取 HLSL 代码块
    ↓
hlslGenerator 生成完整 HLSL（8 层拼接）
    ↓
didOpen/didChange → shader-language-server（假 file:// URI + 完整 HLSL 内容）
    ↓
shader-language-server 返回 diagnostics
    ↓
FakeFileLspManager 拦截 → URI 映射（假 URI → .stoy 物理 URI）+ 行号映射，过滤注入区域
    ↓
用户在 .stoy 文件中看到编译错误
```

## 假 URI 策略

### 格式

```
file:///<stoyDir>/<stoyBaseName>.<blockName>.stoy-virtual.hlsl
```

### 示例

| .stoy 文件 | 代码块 | 假 URI |
|------------|--------|--------|
| `C:/Projects/demo.stoy` | Image pass | `file:///C:/Projects/demo.Image.stoy-virtual.hlsl` |
| `C:/Projects/demo.stoy` | Feedback pass | `file:///C:/Projects/demo.Feedback.stoy-virtual.hlsl` |
| `C:/Projects/demo.stoy` | common 块 | `file:///C:/Projects/demo._common_.stoy-virtual.hlsl` |

### 冲突避免

使用 `.stoy-virtual.hlsl` 后缀，避免与同目录下真实 `.hlsl` 文件冲突。

### #include 支持

假 URI 与 `.stoy` 文件同目录，shader-language-server 解析 `#include` 时的相对路径基准正确，可以引用同目录下的 `.hlsli` 头文件。

## 实现文件

| 文件 | 职责 |
|------|------|
| `src/fakeUriManager.ts` | 假 URI 生成、解析、活跃 URI 跟踪 |
| `src/fakeFileLsp.ts` | 第二个 LanguageClient 生命周期、文档同步、diagnostics 拦截与行号映射 |
| `src/extension.ts` | fake_file_lsp 分支：事件监听、deactivate 清理（无 middleware） |
| `src/hlslGenerator.ts` | 虚拟 HLSL 文档生成（8 层拼接，复用） |
| `src/positionMapper.ts` | 物理行号 ↔ 虚拟行号双向映射（复用） |

## 行号映射

```
physicalLine → virtualLine:  prefixLineCount + (physicalLine - blockRange.startLine)
virtualLine → physicalLine:  blockRange.startLine + (virtualLine - prefixLineCount)
```

- `prefixLineCount`：注入代码的总行数（cbuffer + 别名 + 纹理声明等）
- `blockRange`：代码块在物理文件中的行号范围

### 映射的 LSP 消息

| 消息 | 方向 | 映射内容 |
|------|------|----------|
| `textDocument/didOpen` | Client → Server | URI 转换，内容生成 |
| `textDocument/didChange` | Client → Server | 内容全量替换 |
| `textDocument/publishDiagnostics` | Server → Client | URI + 行号，过滤注入区域 |
## 前提条件

- 仅支持 Windows 和 Linux 平台（插件内置了对应平台的 shader-language-server 预编译二进制）
- 二进制位于插件目录 `bin/<platform>/shader-language-server[.exe]`

## 降级策略

- 当前平台不支持（如 macOS）→ 输出警告，降级为仅内置 HLSL 提示
- shader-language-server 二进制缺失 → 输出警告，降级为仅内置 HLSL 提示
- shader-language-server 启动失败 → 输出错误，降级为仅内置 HLSL 提示
- middleware 转发失败或返回空 → 回退到我们自己 Language Server 的内置方案

## 简化说明

> 更新于 2026-04-05

经过实际使用验证，SLS 的 hover/completion/definition 存在虚拟文档行号映射偏移问题，且 builtin 方案在以下方面表现更好：
- **行号准确**：直接使用物理文件行号，无需映射
- **跨块跳转**：支持从 pass 跳转到 common 块中的符号定义
- **信息丰富**：内置函数详细文档 + 微软链接，框架变量语义描述

因此简化为：**SLS 只提供 diagnostics（DXC 编译错误），所有 hover/completion/definition 由 builtin 处理**。

已删除的模块：
- `src/symbolClassifier.ts` — 符号分类器（不再需要优先级混合）
- `FakeFileLspManager` 中的 `sendHoverRequest`/`sendCompletionRequest`/`sendDefinitionRequest` 等方法
- `extension.ts` 中的 hover/definition/completion middleware 和相关 helper 函数

## 与内嵌方案的关系

`fake_file_lsp` 是一个**轻量级 hack 方案**，不需要 Fork shader-sense 仓库。未来如果需要更深度的集成（如 semantic tokens、inactive regions），可以考虑 Fork + 最小改动方案（详见 `Docs/shader-language-server内嵌方案.md`）。

## 参考链接

- [shader-validator 插件](https://github.com/antaalt/shader-validator)
- [shader-language-server](https://github.com/antaalt/shader-sense/tree/main/shader-language-server)
- [虚拟文档转发方案记录](虚拟文档转发方案记录.md)
- [shader-language-server 内嵌方案](shader-language-server内嵌方案.md)
