# shader-language-server 内嵌方案

> 记录时间：2026-04-04  
> 状态：**待实施**（技术可行性已验证，暂不实现）

## 背景

[shader-validator](https://github.com/antaalt/shader-validator) 是一个高质量的 VSCode HLSL/GLSL/WGSL 扩展，后端使用 Rust 编写的 [shader-language-server](https://github.com/antaalt/shader-sense/tree/main/shader-language-server)，基于 tree-sitter AST + DXC 编译验证，提供完整的 LSP 功能。

该插件在虚拟文档转发模式下不响应，根本原因是客户端和服务端均硬编码了 `scheme: 'file'` 限制。

## 项目分析

### shader-language-server 架构

| 组件 | 技术 | 说明 |
|------|------|------|
| 语言 | Rust | Cargo workspace，多 crate |
| LSP 库 | lsp-server 0.7.8 + lsp-types 0.95 | 标准 LSP 实现 |
| HLSL lint | hassle-rs (DXC 绑定) | SM 6.x 支持，桌面端 |
| GLSL lint | glslang-rs | C 绑定 |
| 符号分析 | tree-sitter | AST 查询，非正则 |
| 传输 | stdio（默认） | tcp/memory 未实现 |

### 提供的 LSP 功能

- Diagnostics（DXC 实时编译验证）
- Hover / Completion / Signature Help
- Go-to-definition
- Document Symbols / Workspace Symbols
- Semantic Tokens（MACRO, PARAMETER, ENUM_MEMBER, ENUM）
- Inactive Regions（预处理器灰显）
- Code Formatting（clang-format，需要外部二进制）
- Shader Variants（多入口点/宏变体切换）

### 不响应虚拟文档的原因

**客户端** (`client.ts`)：

```typescript
documentSelector.push({ scheme: 'file', language: langId });
```

**服务端** (`server.rs`)：

```rust
// DidOpenTextDocument 处理中
if uri.scheme() != "file" {
    return Err(ServerLanguageError::InvalidParams(...));
}
```

```rust
// clean_url 函数中
fn clean_url(url: &Url) -> Url {
    Url::from_file_path(url.to_file_path().expect(...)).unwrap()
    // 对非 file:// URI 直接 panic
}
```

### 内容传递机制

关键发现：`watch_main_file` **不读取磁盘文件**，完全使用 `didOpen` 传入的 `text` 参数：

```rust
let shader_module = Rc::new(RefCell::new(
    shader_module_parser.create_module(&file_path, &text)  // text 来自客户端
));
```

`file_path` 仅用于：
- tree-sitter 的文件标识
- `#include` 相对路径基准（我们的 HLSL 无 `#include`，不影响）

依赖文件（`#include`）才会读磁盘（`watch_dependency` 中调用 `read_string_lossy`）。

## 方案设计

### 推荐方案：Fork + 最小改动

Fork `antaalt/shader-sense`，修改 ~13 行 Rust 代码，自行编译 `shader-language-server.exe`。

#### 改动点 1：`clean_url`（server.rs）

```rust
fn clean_url(url: &Url) -> Url {
    // 非 file:// scheme 直接返回原样
    if url.scheme() != "file" {
        return url.clone();
    }
    Url::from_file_path(
        url.to_file_path().expect("Failed to convert to a valid path.")
    ).unwrap()
}
```

#### 改动点 2：`DidOpenTextDocument` scheme 检查（server.rs）

```rust
// 移除或放宽 scheme 限制
// if uri.scheme() != "file" { return Err(...); }
```

#### 改动点 3：`watch_main_file` 中的 `to_file_path`（server_file_cache.rs）

```rust
let file_path = if uri.scheme() == "file" {
    uri.to_file_path().unwrap()
} else {
    // 虚拟文档：用 URI path 部分构造假路径
    PathBuf::from(uri.path())
};
```

#### 编译

```bash
rustup install stable
cd shader-sense
cargo build --release -p shader-language-server
# 产物: target/release/shader-language-server.exe
```

注意：hassle-rs 依赖 DXC（dxcompiler.dll），编译环境需要 Windows SDK 或手动下载 DXC。

### 备选方案：假 file:// 路径（不需要 Fork）

直接使用官方预编译二进制，通过假 `file://` URI + `didOpen` 传内容绕过 scheme 检查：

```typescript
const fakeUri = `file:///C:/stoy-virtual/${passName}.hlsl`;
client.sendNotification('textDocument/didOpen', {
    textDocument: { uri: fakeUri, languageId: 'hlsl', version: 1, text: hlslContent }
});
```

- 优点：不需要 Rust 环境，直接使用官方预编译二进制
- 缺点：hack 性质，假路径可能影响 `#include` 解析、文件监控等，长期不可靠

## 插件集成架构

```
extension.ts
├── LanguageClient #1 → 我们的 Language Server（DSL + 内置 HLSL 提示）
│     documentSelector: [{ scheme: 'file', language: 'stoy' }]
│
└── LanguageClient #2 → shader-language-server（DXC 诊断 + 高级 HLSL 功能）
      documentSelector: [{ scheme: 'stoy-hlsl', language: 'hlsl' }]
      ↑ 需要 middleware 做行号映射
```

### 数据流

```
用户编辑 .stoy
    ↓
stoyParser 解析 → 提取 HLSL 代码块
    ↓
hlslGenerator 生成完整 HLSL（8 层拼接：cbuffer + 别名 + 纹理 + common + code + main 包装）
    ↓
didOpen/didChange → shader-language-server（虚拟 URI + 完整 HLSL 内容）
    ↓
shader-language-server 返回：diagnostics / hover / completion / ...
    ↓
middleware 拦截 → URI 映射（虚拟 → .stoy 物理）+ 行号映射（虚拟行 → 物理行）
    ↓
用户在 .stoy 文件中看到结果
```

### 行号映射

复用已有的 `positionMapper.ts` 逻辑：

```
physicalLine → virtualLine:  prefixLineCount + (physicalLine - blockRange.startLine)
virtualLine → physicalLine:  blockRange.startLine + (virtualLine - prefixLineCount)
```

### 需要映射的 LSP 消息

| 消息 | 方向 | 映射内容 |
|------|------|---------|
| `textDocument/didOpen` | Client → Server | URI 转换，内容生成 |
| `textDocument/didChange` | Client → Server | 内容全量替换 |
| `textDocument/publishDiagnostics` | Server → Client | URI + 行号 |
| `textDocument/hover` | 双向 | 请求: 行号；响应: 无需 |
| `textDocument/completion` | 双向 | 请求: 行号；响应: textEdit 行号 |
| `textDocument/definition` | 双向 | 请求: 行号；响应: location 行号 |
| `textDocument/semanticTokens` | Server → Client | 全文档 token 行号偏移 |

## 工作量估算

| 步骤 | 工作量 |
|------|--------|
| Fork + Rust 改动（~13 行）+ 编译 | 半天 |
| extension.ts 启动第二个 LanguageClient | 2 小时 |
| .stoy 编辑 → HLSL 生成 → didOpen/didChange 同步 | 半天 |
| middleware: diagnostics URI/行号映射 | 1 天 |
| middleware: hover/completion/goto 行号映射 | 半天 |
| middleware: semantic tokens 行号映射 | 半天 |
| 两个 LS 结果冲突处理（优先级/去重） | 半天 |
| 测试 + 边界情况 | 半天 |
| **总计** | **约 4 天** |

## 优先级建议

建议分步实施：

1. **Phase 1**：只接 Diagnostics（最高价值，DXC 实时 lint）
2. **Phase 2**：接 Hover / Completion（替换或增强内置 HLSL 提示）
3. **Phase 3**：接 Semantic Tokens / Inactive Regions

## 与现有内置 HLSL 提示的关系

### 核心认知：HLSL 层是替代关系，不是互补

由于 `hlslGenerator.ts` 已经生成了**包含完整上下文的虚拟 HLSL 文档**（cbuffer + 别名 + 纹理声明 + common + pass 代码 + mainImage 包装），shader-language-server 拿到的 HLSL 内容中**已经包含了 `iResolution`、`iTime` 等框架变量的定义**。因此 shader-language-server 天然就能对这些变量提供 hover/completion/go-to-definition，不需要我们的内置正则方案再重复处理。

两个 Server 的职责划分：

```
┌─────────────────────────────────────────────┐
│  .stoy 文件                                  │
│                                              │
│  DSL 区域（pass、texture 等关键字）            │
│    → 我们自己的 Language Server（保留）         │
│                                              │
│  HLSL 代码区域                                │
│    → shader-language-server（全面接管）         │
│    → 内置正则方案可以完全移除                    │
│                                              │
└─────────────────────────────────────────────┘
```

### 功能对比

| 功能 | 内置正则方案 | shader-language-server | 集成后 |
|------|-------------|----------------------|--------|
| Hover | 60+ 内置函数 + 框架变量 + 用户符号 | tree-sitter AST，更精确 | **由 SLS 替代** |
| Completion | 同上 + 关键字 | 同上 | **由 SLS 替代** |
| Go-to-definition | 正则扫描符号 | tree-sitter，支持作用域 | **由 SLS 替代** |
| Diagnostics | ❌ 无 | ✅ DXC 实时编译验证 | **SLS 新增** |
| Semantic Tokens | ❌ 无 | ✅ 宏/参数/枚举语义高亮 | **SLS 新增** |
| 框架变量提示 | ✅ iResolution 等 | ✅ 虚拟 HLSL 中已包含定义 | **由 SLS 替代** |
| DSL 层提示 | ✅ pass/texture 等 | ❌ 只处理 HLSL | **保留内置** |

### 集成后各模块去留

| 模块 | 去留 | 原因 |
|------|------|------|
| `hlslBuiltins.ts`（60+ 内置函数签名） | **可删除** | shader-language-server 自带完整 HLSL 标准库 |
| `hlslSymbolScanner.ts`（正则扫描用户符号） | **可删除** | tree-sitter AST 更精确 |
| `hlslHoverProvider.ts` | **可删除** | SLS hover 全面覆盖 |
| `hlslCompletionProvider.ts` | **可删除** | SLS completion 全面覆盖 |
| `hlslDefinitionProvider.ts` | **可删除** | SLS go-to-definition 支持作用域 |
| `hlslGenerator.ts`（虚拟文档生成） | **保留** | 作为喂给 shader-language-server 的输入源 |
| DSL providers（`dsl*.ts`） | **保留** | 处理 DSL 区域，SLS 不管这部分 |

结论：**HLSL 层由 shader-language-server 完全替代现有内置正则方案**，我们自己的 Language Server 只需要负责 DSL 层提示。两个 Server 各管一层（DSL 层 vs HLSL 层），互不交叉。

## shader-validator 插件额外功能

除了启动 LanguageClient，shader-validator 还做了以下客户端侧工作：

| 功能 | 说明 | 我们是否需要 |
|------|------|-------------|
| Shader Variant 侧边栏 | TreeView 管理多入口点/宏变体 | ❌ 我们有自己的 pass 管理 |
| 状态栏 | 显示 server 运行状态 | 可选 |
| Dump AST / Dump Dependency 命令 | 调试用 | ❌ |
| clang-format 格式化 | 需要 clang-format 二进制 | 可选 |
| WASI/Web 支持 | 浏览器版 VSCode | ❌ |
| 多语言 documentSelector | HLSL/GLSL/WGSL | 只要 HLSL |

## 参考链接

- shader-validator 插件: https://github.com/antaalt/shader-validator
- shader-language-server: https://github.com/antaalt/shader-sense/tree/main/shader-language-server
- shader-sense 核心库: https://github.com/antaalt/shader-sense
- 虚拟文档转发方案记录: [Docs/虚拟文档转发方案记录.md](虚拟文档转发方案记录.md)
