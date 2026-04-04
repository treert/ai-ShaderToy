# AI 会话入口（必读）

**本文件面向 AI 助手与人类协作者：在新对话或接手本仓库时，请先阅读本文，再按需深入 `Docs/` 目录。**

## 强制规则（给 AI）

1. **在回答与本项目相关的实现、排错、重构或规划前**，应阅读本文件（`ai-readme.md`），并按主题查阅 [`Docs/`](Docs/) 下对应文档。
2. **修改架构、图层、数据路径或依赖时**，同步更新 `ai-readme.md` 和 `Docs/` 中相关文档，避免文档与代码脱节。


## 项目目标
实现一个 Windows 应用，通过 shader 实时渲染图像，类似 shadertoy.com。
应用支持两种运行模式：
1. **窗口模式** — 作为独立窗口打开，可调整大小
2. **壁纸模式** — 通过 Win32 API 嵌入桌面，替代静态壁纸

## 当前进度
- **阶段 1~7 全部完成** ✅（初步运行 → D3D11 壁纸后端 → .stoy 自定义 Shader 格式 & VSCode 插件）
- 项目功能开发已完成，后续视需求进行优化
- 详见 [`Docs/开发阶段.md`](Docs/开发阶段.md)

## 目录说明
- `src/` — 源代码
- `assets/` — 资源文件（着色器等）
  - `assets/shaders/` — ShaderToy 格式 shader（.glsl/.json/.hlsl 和目录模式）
  - `assets/stoys/` — .stoy 自定义格式 shader（独立模式，不与 ShaderToy 格式混合）
- `third_party/` — 第三方依赖（git submodule：SDL2、GLAD、ImGui、nlohmann/json）
- `Docs/` — 项目知识库（技术文档，供 AI 读写，也供用户查阅）
- `tools/` — 辅助工具（json2dir.py 等）

---

## 文档索引

### ⭐ 重要文档
| 文档 | 说明 |
|------|------|
| [`Docs/开发阶段.md`](Docs/开发阶段.md) | **开发阶段与进度 checklist**（阶段 1~7 详细任务列表） |
| [`Docs/使用说明.md`](Docs/使用说明.md) | **用户使用说明**（命令行参数、运行模式、操作方式） |
| [`Docs/技术架构与实现.md`](Docs/技术架构与实现.md) | **整体技术架构**（模块划分、渲染流程、关键设计决策） |

### 技术方案文档
| 文档 | 说明 |
|------|------|
| [`Docs/D3D11壁纸渲染后端.md`](Docs/D3D11壁纸渲染后端.md) | D3D11 壁纸模式渲染后端设计与实现 |
| [`Docs/多Pass实现方案.md`](Docs/多Pass实现方案.md) | 多 Pass 渲染框架设计 |
| [`Docs/GPU性能优化方案.md`](Docs/GPU性能优化方案.md) | GPU 性能优化策略与实现 |
| [`Docs/调试UI实现方案.md`](Docs/调试UI实现方案.md) | Dear ImGui 调试面板实现方案 |
| [`Docs/参考项目与技术方案.md`](Docs/参考项目与技术方案.md) | 参考项目调研与技术选型 |

### .stoy 格式相关
| 文档 | 说明 |
|------|------|
| [`Docs/stoy文件格式设计.md`](Docs/stoy文件格式设计.md) | .stoy 文件格式语法设计 |
| [`Docs/stoy_grammar.bnf`](Docs/stoy_grammar.bnf) | .stoy BNF 语法规范 |
| [`Docs/stoy模式架构说明.md`](Docs/stoy模式架构说明.md) | .stoy 模式整体架构说明 |
| [`Docs/内置HLSL智能提示方案.md`](Docs/内置HLSL智能提示方案.md) | VSCode 插件内置 HLSL 智能提示方案 |
| [`Docs/虚拟文档转发方案记录.md`](Docs/虚拟文档转发方案记录.md) | 虚拟文档 + 外部扩展转发方案记录 |
| [`Docs/fake_file_lsp技术说明.md`](Docs/fake_file_lsp技术说明.md) | fake_file_lsp 模式技术说明 |
| [`Docs/shader-language-server内嵌方案.md`](Docs/shader-language-server内嵌方案.md) | shader-language-server 内嵌方案（待实施） |

### 疑难问题记录（`Docs/疑难问题记录/`）
| 文档 | 说明 |
|------|------|
| [`001-多进程帧率降半.md`](Docs/疑难问题记录/001-多进程帧率降半.md) | DXGI 多进程帧率降半问题排查 |
| [`002-Win11-24H2壁纸模式黑屏.md`](Docs/疑难问题记录/002-Win11-24H2壁纸模式黑屏.md) | Win11 24H2 壁纸模式黑屏根因分析 |
| [`003-DWM遮挡检测误判.md`](Docs/疑难问题记录/003-DWM遮挡检测误判.md) | DWM 遮挡检测误判修复 |

### 有价值的讨论（`Docs/有价值的讨论/`）
| 文档 | 说明 |
|------|------|
| [`Tree-sitter增量解析原理与挑战.md`](Docs/有价值的讨论/Tree-sitter增量解析原理与挑战.md) | Tree-sitter 增量解析原理深度讨论 |
