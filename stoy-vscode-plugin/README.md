# Stoy Shader Format — VSCode 语法高亮插件

为 [ai-ShaderToy](https://github.com/user/ai-ShaderToy) 项目的 `.stoy` 自定义 shader 描述格式提供 VSCode 语法高亮支持。

## 功能

### 外层 DSL 语法高亮

- **关键字**：`global_setting`、`inner_vars`、`texture`、`common`、`pass`、`init`、`code`
- **类型**：`float4`
- **布尔常量**：`true`、`false`
- **内置变量**：`iResolution`、`iTime`、`iTimeDelta`、`iFrame`、`iFrameRate`、`iMouse`、`iDate`、`iSampleRate`
- **配置属性**：`filter`、`wrap`、`type`、`format`、`msaa`、`vsync`、`resolution_scale`
- **实体名称**：`pass` / `texture` 后面的标识符
- **字符串**：`"..."` 双引号字符串
- **数字**：整数和浮点数
- **注释**：`--` Lua 风格单行注释

### 嵌入式 HLSL 语法高亮

`code [=[ ... ]=]` 和 `common [=[ ... ]=]` 长字符串内部提供 HLSL 语法高亮：

- **复用外部 HLSL 扩展**（`source.hlsl`）的完整语法规则，安装任意 HLSL 扩展即可获得完整的 HLSL 着色
- `.stoy` 内置变量（`iTime`、`iResolution` 等）**优先匹配**，在 HLSL 代码中也会特殊高亮
- 通过 `embeddedLanguages` 配置，嵌入区域自动适配 HLSL 的括号匹配、注释切换（`//`）和代码片段
- 如果未安装 HLSL 扩展，内置变量仍然高亮，HLSL 部分退化为纯文本颜色

### 语言配置

- 括号匹配：`{}`、`()`、`[]`
- 行注释切换：`--`
- 自动闭合括号和引号
- 基于花括号的代码折叠

## 安装方法

### 方法 1：符号链接（开发推荐）

将插件目录链接到 VSCode 扩展目录：

**Windows（管理员权限 CMD）**：
```cmd
mklink /D "%USERPROFILE%\.vscode\extensions\stoy-syntax" "path\to\stoy-vscode-plugin"
```

**macOS / Linux**：
```bash
ln -s /path/to/stoy-vscode-plugin ~/.vscode/extensions/stoy-syntax
```

然后重启 VSCode 或执行 `Developer: Reload Window`。

### 方法 2：直接复制

将 `stoy-vscode-plugin/` 整个目录复制到 VSCode 扩展目录：

- Windows：`%USERPROFILE%\.vscode\extensions\stoy-syntax\`
- macOS：`~/.vscode/extensions/stoy-syntax/`
- Linux：`~/.vscode/extensions/stoy-syntax/`

### 方法 3：VSIX 打包安装

```bash
cd stoy-vscode-plugin
npx @vscode/vsce package
code --install-extension stoy-syntax-1.0.0.vsix
```

## 文件格式参考

详见项目文档：
- `Docs/stoy文件格式设计.md` — 格式设计文档
- `Docs/stoy_grammar.bnf` — BNF 语法规范
- `assets/stoys/` — 示例 .stoy 文件
