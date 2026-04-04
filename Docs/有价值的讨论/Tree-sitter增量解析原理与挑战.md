# Tree-sitter 增量解析原理与挑战

> 本文整理自项目开发过程中的一次深度技术讨论，涵盖 Tree-sitter 增量解析的实现原理、GLR 错误处理、闭合符号缺失的本质性难题，以及 Lua 语言的特殊困境。

---

## 1. 增量解析的核心思想

增量解析的目标：用户每次编辑只改了几个字符，不应该从头解析整个文件，而是复用未变更部分的 AST 节点。

### 1.1 Tree-sitter 的方案

Tree-sitter 采用 **LR parser state 整数匹配** 实现节点级精确复用：

1. **保留旧 CST**：上一次解析的完整具体语法树（Concrete Syntax Tree）保留在内存中
2. **接收编辑描述**：编辑器告知 `(start_byte, old_end_byte, new_end_byte)` — 变更发生在哪里、删了多少、加了多少
3. **从变更点开始重新解析**：parser 从变更位置重新启动，逐 token 解析
4. **尝试复用旧节点**：每当 parser 遇到一个旧 CST 中的节点，检查：
   - 该节点的 **字节范围** 未被编辑波及
   - 当前 parser 的 **LR state**（一个整数）与旧节点开始时记录的 **LR state** 一致
5. **匹配则跳过**：如果两个条件都满足，直接将整个旧子树"粘贴"到新 CST 中，跳过该范围的所有 token
6. **不匹配则继续解析**：如果 state 不一致，说明上下文变了，必须继续逐 token 解析

关键洞察：LR parser 的状态是一个整数，比较成本是 O(1)。这使得"能否复用"的判断非常高效。

### 1.2 一个具体例子

```c
// 编辑前
void foo() { ... }    // 节点 A
void bar() { ... }    // 节点 B  ← 用户在这里编辑
void baz() { ... }    // 节点 C
```

用户修改了 `bar` 函数体：

1. Parser 从 `bar` 的起始位置重新解析
2. 解析完 `bar` 后，到达 `baz` 的位置
3. 此时 parser 的 LR state 等于旧树中 `baz` 节点开始时记录的 state
4. `baz` 节点的字节范围未被编辑波及
5. → 直接复用 `baz` 整棵子树，解析结束

`foo` 节点在变更点之前，也被直接保留（Tree-sitter 先定位到变更点，之前的节点自动保留）。

### 1.3 与块级启发式的区别

一些简单的增量解析方案使用"块级启发式"：按行差异找到变更区域，然后重新解析整个函数/类。Tree-sitter 的方案更精确：

- 不依赖缩进或空行来划分块
- 复用粒度可以是任意 AST 节点（一个表达式、一条语句、一个函数）
- 判断标准是数学精确的（LR state 相等），不是启发式猜测

---

## 2. LL / 递归下降也能做增量解析吗？

**可以，但更复杂。**

LR parser 的"状态"是一个整数（state table 中的行号），比较是 O(1)。而递归下降 parser 的"状态"是 **调用栈**（当前在哪个函数、递归到哪一层、局部变量是什么值），比较和恢复都困难得多。

### 2.1 JetBrains PSI 树的做法

JetBrains 的 IDE（IntelliJ IDEA、CLion 等）使用递归下降 parser + **Chameleon Nodes**（变色龙节点）实现了类似的增量解析：

- **Chameleon Node** 是 PSI 树中的一种惰性节点：它知道自己对应的源码范围，但内部子树按需解析
- 当编辑发生时，PSI 树找到包含编辑位置的最小 chameleon node，只重新解析该节点的子树
- 其他 chameleon node 保持不变
- 这本质上是一种"块级复用"，但块的粒度由语法结构决定（函数体、类体、代码块等），而非行差异

### 2.2 对比

| 特性 | Tree-sitter (LR) | JetBrains PSI (递归下降) |
|------|------------------|-------------------------|
| 状态表示 | 整数 | 调用栈 / chameleon node |
| 复用粒度 | 任意 AST 节点 | chameleon node 边界 |
| 判断成本 | O(1) 整数比较 | 找到最小包含块 |
| 实现复杂度 | 中等（需修改 LR 引擎） | 高（需设计惰性节点体系） |
| 错误恢复 | GLR 并行分支 | 手写 recovery 逻辑 |

---

## 3. GLR 与错误处理

Tree-sitter 使用 **GLR（Generalized LR）** 解析器，这在错误恢复方面有独特优势。

### 3.1 GLR 的基本思想

标准 LR 遇到冲突（shift/reduce 或 reduce/reduce）时报错。GLR 的做法是：**同时走两条路**（fork 出并行分支），等后续 token 消歧。最终只有一条路能走通，其余分支被剪枝。

### 3.2 ERROR 节点是一等公民

当所有 GLR 分支都失败时，Tree-sitter 不会崩溃，而是：

1. 将无法匹配的 token 序列包裹进一个 **ERROR 节点**
2. 跳过一些 token 后尝试恢复
3. ERROR 节点作为 CST 的一部分被保留

在增量解析中，ERROR 节点和普通节点一样参与复用判断：
- 如果旧树中某个位置是 ERROR 节点，且该位置未被编辑，parser state 也匹配 → 照样复用
- 这意味着：**文件中已有的错误区域不会拖慢增量解析**

### 3.3 错误恢复后的 state 不匹配

一个微妙的场景：

```c
void foo() { ... }
void bar() { x = ;  // 语法错误
void baz() { ... }
```

用户修复了 `bar` 中的错误：

```c
void bar() { x = 1; }  // 修复了
```

此时 parser 重新解析 `bar`，解析完后到达 `baz`。但由于旧树中 `bar` 含 ERROR 节点，**旧树中 `baz` 开始时的 LR state 是 error recovery 后的 state**，而新树中 `baz` 开始时的 LR state 是正常解析后的 state。两者可能不同 → `baz` 无法复用，需要重新解析。

不过，解析完 `baz` 后，如果后续节点的 state 与旧树匹配上了，就可以恢复复用。所以代价通常只是多解析了一两个兄弟节点。

### 3.4 顶层声明是强同步点

在 C/Java/Go/JavaScript 等语言中，**顶层声明**（函数定义、类定义、全局变量）是天然的同步点：

- 每个顶层声明开始时，LR parser 通常处于相同的 state（"等待下一个声明"）
- 这意味着即使中间有错误，跨过下一个顶层声明边界后，state 几乎必然匹配
- 增量解析的"影响范围"很少跨越顶层声明边界

---

## 4. 闭合符号缺失的本质性难题

### 4.1 问题描述

当缺少 `}` 或 `end` 等闭合符号时，parser 无法确定块的结束位置：

```c
void foo() {
    if (x) {
        // 少了一个 }
    void bar() { ... }
    void baz() { ... }
}
```

`bar` 和 `baz` 到底是 `foo` 内部的局部函数声明（不合法但 parser 不知道），还是 `if` 块意外吞噬了它们？

### 4.2 为什么这是本质性的

这是 **上下文无关文法（CFG）的理论限制**：

- CFG 通过嵌套配对来识别结构（`{` 配 `}`，`begin` 配 `end`）
- 当配对断裂时，需要"未来信息"才能确定哪里断裂的
- 但 parser 是从左到右扫描的，读到 `void bar()` 时，它不知道后面还有没有那个缺失的 `}`
- 直到文件结尾，`}` 的数量不对，才确认确实少了一个 — 但此时已经太晚了

### 4.3 缓解策略

虽然无法完美解决，但有多种缓解手段：

#### 4.3.1 缩进启发式

```c
void foo() {
    if (x) {
        doSomething();
                              // ← 缩进回到顶层
void bar() { ... }            // ← 缩进暗示这是新函数
```

如果 `void bar()` 的缩进和 `void foo()` 相同，启发式判断这不太可能是 `foo` 内部的代码。Tree-sitter 的语法文件可以通过 `external scanner`（C 代码）实现缩进感知。

**局限**：对不依赖缩进的语言（C/Java/JS），缩进只是风格约定，不能作为确定性依据。

#### 4.3.2 关键字启发式

某些关键字几乎只出现在顶层：
- C: `#include`, `#define`, `typedef`, 存储类说明符在全局位置
- Java: `import`, `package`, `public class`
- Go: `package`, `import`, `func` (顶层)

遇到这些关键字时，即使 `{` 计数不平衡，也可以强制关闭当前块。

#### 4.3.3 GLR 并行分支

GLR 可以在"可能缺少 `}`"的位置 fork：
- 分支 1：假设没缺，继续在当前块内解析
- 分支 2：假设缺了一个 `}`，跳出到外层继续

两条分支并行推进，最终看哪条路的语法树更合理（错误更少、ERROR 节点更少）。

#### 4.3.4 cost 上限

Tree-sitter 的 error recovery 有 cost 上限：如果跳过了太多 token 仍无法恢复，就强制插入缺失的符号，产生 ERROR 节点但继续前进。这避免了"解析到文件末尾才发现错误"的最坏情况。

#### 4.3.5 编辑器多层协作

现代编辑器不只依赖 parser：
- **括号配对高亮**：编辑器独立维护括号栈，用户能直观看到不配对的括号
- **自动插入闭合符号**：输入 `{` 时自动补 `}`
- **增量缩进**：帮助用户保持结构清晰
- **语义分析（如果有的话）**：类型检查等上层分析可以提供额外线索

---

## 5. Lua 的特殊困境 — 闭合符号缺失的最坏案例

### 5.1 Lua 的语法特点

Lua 使用 `function...end` 块，且 `function` 关键字既是 **语句关键字** 又是 **表达式关键字**：

```lua
-- 语句形式
function foo()
    ...
end

-- 表达式形式
local f = function()
    ...
end

-- 表达式可以出现在任何地方
print(function() return 1 end)
t[function() return key end] = function() return value end
```

### 5.2 为什么 Lua 是最坏情况

当一个 `end` 缺失时：

```lua
function foo()
    -- 本来这里应该有 end
function bar()
    ...
end
function baz()
    ...
end
```

Parser 会将 `bar` 和 `baz` 解析为 `foo` 内部的局部函数表达式语句（Lua 允许这样写）。直到文件末尾 `end` 数量不够，才知道出了问题。

关键困难：

1. **没有同步锚点**：在 C 中，`void` 几乎只出现在顶层，可以作为同步点。但 Lua 的 `function` 无处不在，它不暗示"这一定是顶层声明"
2. **缩进启发式无效**：Lua 不是缩进敏感语言，缩进只是风格
3. **GLR 并行分支无用**：GLR 的并行分支在**歧义点**触发，但缺少 `end` 时 parser 并不觉得歧义 — 它很开心地把后续代码当作外层函数的内容。没有歧义就没有 fork 点
4. **强制 fork 会爆炸**：如果在每个 `function` 后都强制 fork（"这里可能缺了 end"），对于 N 个 function，会产生 2^N 个分支 — 指数爆炸

### 5.3 Lua vs 其他语言的对比

| 语言 | 闭合符号 | 顶层锚点 | 缩进敏感 | 缺失恢复难度 |
|------|---------|---------|---------|------------|
| C/C++ | `}` | 类型关键字、`#include` | 否 | 中等（关键字启发式有效） |
| Java | `}` | `class`, `import`, 修饰符 | 否 | 较易（类/方法边界清晰） |
| Go | `}` | `func`, `type`, `var` | 否 | 较易（顶层关键字明确） |
| JavaScript | `}` | `function`, `class`, `export` | 否 | 中等（function 也可嵌套，但有 export/class 锚点） |
| Python | 缩进 | — | **是** | 较易（缩进就是结构） |
| **Lua** | `end` | **无** | 否 | **极难**（function 无处不在，无锚点） |

### 5.4 实际影响

对于 Tree-sitter 的 Lua grammar：
- 正常代码：增量解析工作良好，`function...end` 完整配对时一切正常
- 缺失 `end`：可能产生大面积 ERROR 节点，甚至整个文件变成一个大 ERROR
- 用户体验：编辑器的语法高亮、代码折叠、符号列表可能在缺少 `end` 时大面积崩坏
- 这也是为什么 Lua 编辑器通常强调"括号自动配对"和"end 自动补全"的重要性

---

## 6. 对 .stoy 项目的启示

对于本项目的 `.stoy` DSL：

- **文件很小**（通常几十到几百行），全量解析 <1ms，增量解析是杀鸡用牛刀
- **.stoy 语法简单**：顶层块 `@pass`、`@texture`、`@inner_vars` 等关键字是天然的强同步点
- **块使用 `{ }`**：配合顶层关键字，即使缺了 `}`，遇到下一个 `@pass` 就能恢复
- **结论**：.stoy 的 parser 不需要增量解析，当前的全量重解析策略是最佳选择

---

## 7. 参考资料

- [Tree-sitter 官方文档 - Parsing](https://tree-sitter.github.io/tree-sitter/using-parsers/1-getting-started.html)
- [Tree-sitter 论文 - Incremental parsing](https://tree-sitter.github.io/tree-sitter/)
- [JetBrains PSI 架构](https://plugins.jetbrains.com/docs/intellij/psi.html)
- [GLR Parsing - Wikipedia](https://en.wikipedia.org/wiki/GLR_parser)

---

## 附录：原始对话记录

以下是产生本文档的完整对话过程。

---

### 问题 1：Tree-sitter 增量解析是怎么实现的？

**用户**：增量解析 (Incremental Parsing) Tree-sitter 的方案 — 只重新解析变更部分，保留未变更部分的 AST。对 stoy 来说有点杀鸡用牛刀。只是好奇，这个是怎么实现的？应该也是要往后过滤token，直到遇到块结尾或者块开头吧

**AI**：

你的直觉方向是对的，但 Tree-sitter 的方案比"找到块边界"更精巧。核心机制是 **LR parser state 匹配**，而非简单的块级启发式。

#### 增量解析的完整流程

**输入**：旧的 CST（具体语法树）+ 编辑描述 `(start_byte, old_end_byte, new_end_byte)`

**步骤**：

1. **定位变更点**：在旧 CST 中找到编辑影响的最小子树
2. **从变更点之前的位置开始重新解析**：parser 回退到变更点所在节点的父节点开始位置，用旧 CST 中记录的 **LR parser state** 恢复 parser 状态
3. **逐 token 解析新内容**：对变更区域内的新 token 正常走 LR 解析
4. **尝试与旧树"接轨"**：解析完变更区域后，parser 继续往后看。每遇到一个旧 CST 中的节点，检查：
   - 该节点的字节范围完全在编辑区域之外（未被修改）
   - 当前 parser 的 **LR state** 和旧节点记录的 **LR state** 相同
5. **匹配则复用**：如果 state 匹配，直接把旧子树嫁接到新 CST 上，跳过这段 token，parser 跳到旧节点的末尾继续
6. **不匹配则继续解析**：如果 state 不同，说明上下文变了（比如多了一层嵌套），必须继续逐 token 解析，直到找到下一个能匹配的旧节点

#### 关键洞察：为什么是 LR state 而不是"块边界"

LR parser 在每个位置都有一个明确的整数 state，它编码了"我到目前为止看到了什么语法结构"。两个位置的 state 相同，意味着从这里往后的解析行为完全相同——不管之前发生了什么。

这比"找到 `}` 或块开头"更精确：

```c
// 假设用户在 bar 函数体内编辑
void foo() { ... }    // 节点 A
void bar() { ... }    // 节点 B ← 编辑了这里
void baz() { ... }    // 节点 C
```

- Parser 重新解析 `bar` 的函数体
- 解析完 `bar` 的 `}` 后，parser state 回到"顶层，等待下一个声明"
- 这个 state 和旧 CST 中 `baz` 节点开始时记录的 state 一致
- → 直接复用 `baz` 整棵子树，不需要重新解析

但如果编辑引入了不平衡的 `{`：

```c
void bar() { if (x) {  // 少了一个 }
void baz() { ... }
```

- 解析到 `baz` 位置时，parser state 是"在 bar 的 if 块内"
- 这和旧 CST 中 `baz` 的 state（"顶层"）不匹配
- → 必须继续解析 `baz`，甚至更后面的节点
- 直到某个位置 state 重新匹配上（比如遇到文件末尾的 `}` 平衡回来）

#### 你说的"往后过滤 token 直到遇到块结尾/开头"

这其实是一种简化版的增量解析策略（某些编辑器确实这么做）。Tree-sitter 的方案更通用：

| 方面 | 块级启发式 | Tree-sitter LR state 匹配 |
|------|-----------|--------------------------|
| 判断标准 | 找到 `}` 或特定关键字 | LR state 整数相等 |
| 复用粒度 | 块级（函数/类） | 任意 AST 节点 |
| 对嵌套变更的处理 | 可能过度重解析 | 精确到最小影响范围 |
| 对语言的依赖 | 需要知道什么是"块边界" | 语言无关（由 grammar 自动确定） |

#### 对 .stoy 的意义

.stoy 文件通常很小（几十到几百行），全量解析 < 1ms。增量解析的收益微乎其微，反而增加了 parser 的实现复杂度。所以当前的全量重解析策略是最优选择。

增量解析主要在以下场景有意义：
- 大文件（数千行以上）
- 解析本身较慢（复杂 grammar、语义分析耦合）
- 需要实时响应（每次按键都要出结果，延迟预算 < 16ms）

---

### 问题 2：递归下降 LL 语法解析也可以做到吗？

**用户**：似乎递归下降 LL 语法解析 也可以做到。

**AI**：

可以，但实现起来难度不同。

#### LL/递归下降做增量解析的困难

核心问题：**"状态"不是一个整数，而是调用栈。**

LR parser 的 state 是一个整数（state table 的行号），比较和恢复都是 O(1)。但递归下降 parser 的"状态"是：

```
parseProgram()
  → parseDeclaration()
    → parseFunctionDef()
      → parseBlock()
        → parseStatement()    ← 当前正在这里
```

要"恢复"到某个旧节点的解析位置，意味着要重建整个调用栈——调用哪些函数、局部变量是什么值、回溯点在哪。这比恢复一个整数难得多。

#### 实际中递归下降的增量解析方案

##### 方案 1：Chameleon Nodes（JetBrains PSI 树）

JetBrains 的做法最成熟：

```
PSI Tree:
  File
  ├── Function "foo" [chameleon, parsed]
  │   └── Block { ... }
  ├── Function "bar" [chameleon, LAZY - 未解析内部]
  └── Function "baz" [chameleon, parsed]
```

- 将 AST 中的某些节点标记为 **chameleon**（通常是函数体、类体等大块）
- Chameleon 节点记录自己的源码范围，但**内部子树可以是惰性的**（用到时才解析）
- 编辑时，找到包含编辑点的最小 chameleon 节点，只重新解析它的内容
- 其他 chameleon 节点保持不变（如果它们的源码范围未被编辑波及）

**本质**：把"增量"问题转化为"找到最小受影响的块"，然后块内全量重解析。

**对比 Tree-sitter**：
- 复用粒度较粗（chameleon 节点边界，通常是函数/类级别）
- 不需要 LR state 匹配机制
- 实现相对直觉（找最小包含块 → 重解析）
- 但块的划分需要手动设计（哪些节点是 chameleon）

##### 方案 2：记录 parser "快照"

理论上可以在递归下降 parser 的关键位置保存快照：

```
parseProgram() {
    while (hasMore()) {
        saveSnapshot();  // 记录：当前 token 位置 + 调用栈状态
        parseDeclaration();
    }
}
```

编辑时，找到最近的快照位置，恢复快照继续解析。

**问题**：
- 调用栈快照的存储和恢复成本高
- 快照可能很大（深层嵌套时）
- 实际项目中很少这么做

##### 方案 3：区间树 + 局部重解析

一些编辑器（如 Monaco）的做法：
- 用区间树（interval tree）维护每个 AST 节点的行范围
- 编辑时，找到行范围被影响的节点，只重解析这些节点的父块
- 类似 chameleon 但不需要显式标记

#### 总结对比

| 方案 | Parser 类型 | 状态恢复 | 复用粒度 | 实现复杂度 | 代表项目 |
|------|-----------|---------|---------|-----------|---------|
| LR state 匹配 | LR / GLR | O(1) 整数比较 | 任意节点 | 中 | Tree-sitter |
| Chameleon Nodes | 递归下降 | 不需要（块内全量重解析） | chameleon 边界 | 中高 | JetBrains IDEA |
| Parser 快照 | 递归下降 | 调用栈恢复 | 快照点 | 高 | 极少实用 |
| 区间树 + 局部重解析 | 任意 | 不需要 | 行范围 | 低中 | Monaco Editor |

所以你说的没错——递归下降确实可以做增量解析。只是 LR parser 天然有一个便宜的"state 指纹"（整数），让节点级精确复用变得自然。递归下降需要额外的设计（chameleon nodes 等）来达到类似效果，且粒度通常更粗。

---

### 问题 3：GLR 怎么处理报错？错误发生时怎么和旧的 AST 匹配？

**用户**：GLR 怎么处理报错呢？有点想象不出来，错误发生时怎么和旧的AST匹配。

**AI**：

这是个很好的问题，GLR + error recovery + 增量复用三者的交互确实不直觉。让我分层解释。

#### 第一层：GLR 的 error recovery 是怎么工作的

标准 LR 遇到语法错误时直接报错停止。GLR 的做法更聪明：

**步骤 1：检测错误**
```
当前 state = 42, 下一个 token = "void"
state 42 对 "void" 没有合法的 action → 语法错误
```

**步骤 2：尝试 error recovery（多种策略并行）**

Tree-sitter 同时尝试多种恢复策略，每种策略是一个 GLR 分支：

- **策略 A：跳过当前 token**（"假装这个 token 不存在"）
  - 跳过 "void"，看下一个 token 在 state 42 是否合法
- **策略 B：插入缺失的 token**（"假装用户忘写了什么"）
  - state 42 期望的是 `}`？试试假装收到了 `}`，reduce 后看能否继续
- **策略 C：弹出栈帧**（"回退到更外层的语法规则"）
  - 弹出一层嵌套，看外层 state 能否接受当前 token

**每种策略 fork 出一个独立的 parser 分支**，各自继续解析。

**步骤 3：评分和剪枝**

每个分支有一个 "error cost"：
- 跳过 1 个 token → cost +1
- 插入 1 个假 token → cost +1
- 正常匹配 → cost 不增加

Tree-sitter 持续推进所有分支，**定期剪掉 cost 最高的分支**。最终存活的分支成为最终的解析结果。

**步骤 4：生成 ERROR 节点**

被跳过的 token 或被假插入的部分，包裹成 `ERROR` 节点放进 CST：

```
(function_definition
  name: (identifier)          "foo"
  parameters: (parameter_list) "()"
  body: (block
    (ERROR                     ← 错误区域
      "x" "=" )               ← 被包裹的无法解析的 token
    (expression_statement      ← recovery 后继续正常解析
      (assignment ...))))
```

#### 第二层：ERROR 节点在旧 CST 中是什么样的

假设旧代码有错误：

```c
void foo() {
    x = ;          // 语法错误
    y = 2;         // 正常
}
void bar() { ... } // 正常
```

旧 CST 长这样：
```
(translation_unit
  (function_definition         [state=0 at start]
    name: "foo"
    body: (block
      (ERROR "x" "=" ";")     [state=15 at start]  ← 错误区域
      (expression_statement    [state=23 at start]  ← recovery 后的正常节点
        (assignment "y" "=" "2"))
    ))
  (function_definition         [state=0 at start]  ← bar，顶层 state 回到 0
    name: "bar"
    body: ...))
```

注意：**每个节点（包括 ERROR 节点）都记录了它开始时的 LR state**。

#### 第三层：增量解析时，ERROR 节点如何参与复用

**场景 1：编辑发生在错误区域之外（编辑 bar）**

用户修改了 `bar` 函数体，`foo`（含 ERROR 节点）完全不受影响。

增量解析的流程：
1. `foo` 整棵子树（含 ERROR 节点）未被编辑波及 → **直接复用**
2. 从 `bar` 开始重新解析
3. 解析完 `bar` 后，后续节点 state 匹配 → 停止

ERROR 节点就是普通节点，复用逻辑完全相同。

**场景 2：编辑发生在错误区域内（修复 foo 的错误）**

用户把 `x = ;` 改成 `x = 1;`

增量解析的流程：
1. 定位到变更点在 `foo` 的 body 内
2. 从 `foo` 的 body 开头（或更精确的位置）重新解析
3. 现在 `x = 1;` 正常解析了，不再产生 ERROR 节点
4. 继续解析 `y = 2;` → 但这里有个微妙问题：

**旧 CST 中 `y = 2;` 这个节点记录的 state 是 error recovery 后的 state（假设是 state 23）。而新的 parser 正常解析到 `y = 2;` 时的 state 可能是 state 20（正常路径的 state）。**

state 23 ≠ state 20 → **无法复用 `y = 2;`，必须重新解析它**。

5. 继续解析到 `foo` 的 `}` → 出了 `foo` 后，parser state 回到 0（顶层）
6. 旧 CST 中 `bar` 的 state 也是 0 → **匹配！复用 `bar` 整棵子树**

**关键点**：error recovery 改变了后续节点的 LR state，所以修复错误后，错误区域后面的**兄弟节点**可能无法复用（state 不同了）。但跨过当前块的边界（比如函数体的 `}`）后，state 通常会重新收敛到相同值。

**场景 3：编辑引入了新的错误**

用户在正常代码中引入错误：把 `y = 2;` 改成 `y = ;`

增量解析的流程：
1. 重新解析变更区域
2. Parser 在 `y = ;` 处触发 error recovery
3. Error recovery 产生 ERROR 节点 + 新的 recovery state
4. 继续往后看：旧 CST 中 `}` 之后 `bar` 的 state 是 0
5. 新 parser 经过 error recovery 后，出了 `foo` 的 `}`，state 也回到 0
6. → 匹配！复用 `bar`

#### 总结：核心规则

1. **ERROR 节点和普通节点一样参与复用判断**（字节范围 + LR state 匹配）
2. **错误区域外的节点**：如果未被编辑波及且 state 匹配 → 直接复用
3. **修复错误后**：紧接着的兄弟节点可能因 state 变化无法复用，但跨过块边界后 state 通常收敛 → 后续节点恢复复用
4. **引入新错误后**：类似地，error recovery 后的 state 在块边界处通常收敛
5. **顶层声明是强同步点**：因为每个顶层声明开始时 parser state 几乎总是 state 0（"等待下一个声明"），所以增量解析的影响范围几乎不会跨越顶层声明边界

所以影响范围通常是：**变更点所在的函数/块 + 可能多解析 1-2 个兄弟节点**，然后 state 就匹配上了，后续全部复用。

---

### 问题 4：结尾的 } 或 end 如果丢失，是不是只能认了？

**用户**：但是结尾的 } 或者像 lua 语言的 end 如果丢失，可能要解析到文件结尾才直到出错了。这个是不是只能认了，无法避免？

**AI**：

对，这是一个**本质性的困难**，不是 Tree-sitter 或任何特定 parser 的实现缺陷，而是**上下文无关文法（CFG）的理论限制**。

#### 为什么这是本质性的

```c
void foo() {
    if (x) {
        doSomething();
    // ← 少了一个 }
    y = 2;
}
void bar() { ... }
```

Parser 读到 `y = 2;` 时，它有两种合理解读：
- **解读 A**：`if` 块还没关，`y = 2;` 是 `if` 块内的语句（用户忘了写 `}`）
- **解读 B**：`if` 块已经被某个未知的力量关闭了，`y = 2;` 是 `foo` 函数体的语句

**两种解读在此刻都是合法的**——parser 无法区分。它需要"未来信息"才能判断：如果后面 `}` 的数量刚好平衡，那解读 A 对；如果不平衡，那解读 B 对（但要回头修正）。

这不是实现的问题，而是**前瞻有限的 parser 在面对嵌套配对缺失时的固有困境**。

#### 最坏情况确实存在

```c
void foo() {
    // 少了一个 }
    // 后面是 5000 行其他函数
    // ...
    // 文件末尾才发现 } 数量不对
```

这种情况下，parser 确实可能把后面所有内容都吞进 `foo` 的函数体里，直到文件末尾才知道出错了。增量解析也救不了——因为旧 CST 中这些节点是 `foo` 的子节点，而修复后它们应该是顶层节点，state 完全不同。

#### 但有多种缓解手段（虽然无法完美解决）

##### 1. 缩进启发式（external scanner）

```c
void foo() {
    if (x) {
        doSomething();
                              // ← 缩进突然回到顶层
void bar() { ... }            // ← 即使 { 不平衡，缩进暗示这是新的顶层函数
```

Tree-sitter 的 `external scanner`（用 C 写的自定义 lexer）可以感知缩进：
- 如果一行的缩进突然回到 0 列，且看起来像一个新的声明 → 可以强制产生一个"虚拟的 `}`"来关闭当前块

**局限**：
- 只对"缩进风格良好"的代码有效
- C/Java/JS 不是缩进敏感语言，不能强制要求
- 用户可能故意写不规范的缩进

##### 2. 关键字启发式

```c
void foo() {
    // 少了 }
#include <stdio.h>    // ← #include 不可能出现在函数体内
void bar() { ... }    // ← void 在某些上下文几乎只出现在顶层
```

Tree-sitter 的 grammar 可以定义"emergency recovery rules"：
- 遇到某些关键字（`#include`、`#define`、顶层的类型修饰符等），即使 `{` 不平衡，也强制关闭所有打开的块
- 这在 C/C++ 的 Tree-sitter grammar 中实际有使用

**局限**：
- 需要针对每种语言手动定义哪些关键字是"顶层锚点"
- 某些语言没有好的锚点关键字

##### 3. GLR 并行尝试

```c
void foo() {
    if (x) {
        doSomething();
    y = 2;         // ← GLR 可以在这里 fork
```

GLR 可以：
- **分支 1**：假设 `if` 块还没关，`y = 2;` 在 `if` 内
- **分支 2**：假设 `if` 块缺了 `}`，插入虚拟 `}`，`y = 2;` 在外层

两个分支并行推进，后续谁的 cost 更低谁存活。

**这是 Tree-sitter 对这个问题最有力的武器**，但仍然有限制：
- 分支数量有上限（避免指数爆炸）
- 如果多层嵌套都缺了 `}`，可能的组合太多
- 最终选择的分支可能不是用户意图的那个

##### 4. Error recovery 的 cost 上限

Tree-sitter 在 error recovery 时有一个 "cost budget"：

- 如果跳过了 N 个 token（N 通常是一个不大的常数）仍无法恢复 → 强制停止，把这些 token 打包成 ERROR 节点
- 这避免了"吞噬整个文件"的最坏情况
- 代价是 ERROR 节点可能很大，但至少后续的代码还能被正常解析

##### 5. 编辑器层面的辅助

Parser 不是唯一的防线：
- **括号匹配高亮**：编辑器独立于 parser 维护一个括号栈，不配对的 `{` 会被标红
- **自动插入 `}`**：输入 `{` 时自动补全 `}`，从源头减少问题
- **彩虹括号**：让嵌套层级一目了然
- **代码折叠**：如果某个块异常巨大，视觉上就能提示用户"这里可能有问题"

#### 总结

| 缓解手段 | 效果 | 局限 |
|---------|------|------|
| 缩进启发式 | 对良好格式的代码很有效 | 不适用于缩进不敏感的语言 |
| 关键字启发式 | 在 C/Java 中效果好 | 需要语言特定的规则 |
| GLR 并行分支 | 理论最优，可探索多种恢复路径 | 分支数量有限，多层嵌套失效 |
| Cost 上限 | 防止最坏情况 | ERROR 节点可能很大 |
| 编辑器辅助 | 从用户侧预防 | 不能帮助 parser |

**所以你说的"只能认了"基本是对的**——对于闭合符号缺失的情况，没有完美的解决方案。上面这些都是**缓解**，不是**解决**。理论上，你需要"猜"用户忘了在哪里关闭块，而这需要理解代码的**语义意图**，超出了 CFG 的能力范围。

这也是为什么 Python 选择了用缩进代替 `{}`——缩进是可见的、无法"忘记"的，从根本上消除了这个问题。

---

### 问题 5：Lua 缺少 end 时，Tree-sitter 的并行尝试是不是反而浪费 CPU？

**用户**：lua这个是最误解的了，因为有可能旧就没有错误，就是文件尾有多个 end 在等待中。这个时候 Tree-sitter 的 并行尝试，是不是反而浪费cpu性能了。

**AI**：

你抓到了一个非常精准的点。Lua 在这里确实是 **最糟糕的案例**，而且 GLR 的并行尝试在这个场景下 **不仅没帮助，还可能有害**。

#### Lua 的特殊困境

```lua
function foo()
    -- 假设这里少了一个 end
function bar()
    print("hello")
end
function baz()
    print("world")
end
```

Parser 的视角：

1. 读到 `function foo()` → 开始解析函数体
2. 读到 `function bar()` → **完全合法！** Lua 允许在函数体内定义函数（`local function` 或者函数表达式）
3. 读到 `bar` 的 `end` → 关闭 `bar`，但 `foo` 还没关
4. 读到 `function baz()` → 同样合法，还在 `foo` 的函数体内
5. 读到 `baz` 的 `end` → 关闭 `baz`
6. 文件结束 → `foo` 缺少 `end`！

**关键问题**：**在第 2 步和第 4 步，parser 没有任何歧义感**。它不觉得这里"可能有问题"。`function` 在函数体内出现是完全正常的。

#### 为什么 GLR 并行尝试在这里无用

GLR 的并行分支在 **歧义点** 或 **错误点** 触发。但在 Lua 的这个场景中：

1. **没有歧义**：`function bar()` 在 `foo` 体内是合法的唯一解读（不存在另一种解读需要 fork）
2. **没有错误**：每一步都是合法的 Lua 语法，parser 不会触发 error recovery
3. **错误在未来**：直到文件末尾才知道 `end` 不够

GLR 的并行分支根本不会被触发——因为 parser 一路开心地解析，没有遇到任何需要 fork 的理由。

#### 如果强制 fork 呢？

你可能想："那在每个 `function` 后面强制 fork 一下？"

```
function foo()
    fork → 分支 A: 正常继续
         → 分支 B: 假装 foo 这里有个 end，回到顶层
    function bar()
        fork → 分支 A1: 正常继续
             → 分支 A2: 假装 bar 这里有个 end
        ...
```

对于 N 个 `function`，这会产生 **2^N 个分支**。一个 100 行的 Lua 文件可能有 20 个 function，那就是 2^20 ≈ 100 万个分支。**指数爆炸。**

即使有剪枝，也没有好的剪枝标准——因为在 `end` 到来之前，所有分支的 cost 都是一样的（都没有错误）。

#### 这与 `}` 语言的本质区别

在 C/Java/JavaScript 中，虽然也有闭合符号问题，但有天然的 **同步锚点**：

```c
void foo() {
    // 少了一个 }
void bar() { ... }    // ← "void" 不可能出现在函数体内的这个位置
                      //    parser 可以意识到"这里可能少了 }"
```

`void`/`int`/`class` 等关键字在某些位置是**不合法的**，这给了 parser 一个信号："我可能跑偏了"。

但 Lua：
```lua
function foo()
    -- 少了 end
function bar()   -- ← 完全合法！function 可以出现在这里
    ...
end
```

`function` 关键字在**任何位置都可能合法**（作为表达式的一部分），所以它不是同步锚点。Parser 没有任何理由怀疑自己。

#### 实际影响

对 Tree-sitter 的 Lua grammar 来说：

1. **缺少一个 `end` → 整个文件可能被解析为一个大的嵌套函数**
2. 增量解析在这种情况下几乎退化为全量解析（因为旧 CST 的结构完全不同了）
3. GLR 并行分支不会被触发（没有歧义/错误点）
4. Error recovery 只在文件末尾才触发，此时已经太晚了

**你说的"反而浪费 CPU"**——严格来说，GLR 在这个场景下不会浪费 CPU（因为它根本不会 fork）。但它也**完全帮不上忙**。CPU 浪费发生在另一个层面：增量解析失效，不得不全量重解析。

#### 缓解手段（都不完美）

| 手段 | 对 Lua 的效果 |
|------|-------------|
| 缩进启发式 | 有一定帮助（如果 `function bar` 缩进回到顶层） |
| 关键字启发式 | **无用**（`function` 不是顶层锚点） |
| GLR 并行分支 | **无用**（无歧义点触发） |
| Cost 上限 | **部分有用**（限制 ERROR 节点大小，但不解决结构误解析） |
| 编辑器自动补 `end` | **最有效的预防手段** |

这也解释了为什么 Lua 编辑器（如 ZeroBrane Studio、Lua Language Server）特别强调：
- 自动补全 `end`
- 实时显示 `function`/`end` 配对
- 缩进辅助

因为从 parser 层面，这个问题几乎无解。

---

### 问题 6：与 Lua 的 function 关键字可以出现在几乎任何地方有关

**用户**：也与lua的 function 关键字可以出现在几乎任何地方有关，既是语句关键字又是表达式关键字。

**AI**：

正是如此。这是 Lua 语法设计中一个非常深层的特性，直接导致了 parser 在缺失 `end` 时完全丧失方向感。

#### `function` 的双重身份

在 Lua 中，`function` 同时扮演两个角色：

**1. 语句关键字（声明形式）**
```lua
function foo()          -- 命名函数声明
    ...
end

local function bar()    -- 局部命名函数声明
    ...
end
```

**2. 表达式关键字（匿名函数 / 闭包）**
```lua
local f = function() ... end                    -- 赋值
callback(function(x) return x * 2 end)          -- 参数
t.method = function(self) ... end               -- 表成员
return function() ... end                       -- 返回值
(function() print("IIFE") end)()                -- 立即执行
t[function() return key end] = value            -- 甚至作为 table key
```

`function` 可以出现在**几乎任何"期望一个值"的地方**。

#### 这为什么是灾难性的

在其他语言中，函数定义关键字有 **位置约束**：

| 语言 | 函数关键字 | 能出现在表达式中？ | 顶层锚点强度 |
|------|---------- |-------------------|------------|
| C | `void`/`int`/... (返回类型) | 否 | **强**（函数体内不会出现返回类型开头的声明） |
| Java | `void`/返回类型 | 否 | **强** |
| Go | `func` | 是（闭包） | **中**（但有 `package`, `import`, `type` 等强锚点） |
| JavaScript | `function` | 是 | **中**（但有 `class`, `export`, `import` 等强锚点） |
| Python | `def` | 否（lambda 用不同关键字） | **强**（`def` + 缩进 = 明确的顶层边界） |
| **Lua** | `function` | **是，几乎无限制** | **无**（`function` 在任何位置都合法） |

Lua 的致命组合：
1. `function` **既是语句又是表达式** → 它出现在任何位置都不异常
2. **没有其他强锚点关键字** → Lua 的 `local`, `if`, `for`, `while`, `repeat`, `return` 都可以出现在函数体内
3. **不是缩进敏感** → 缩进不能作为结构依据
4. **使用 `end` 而非 `}`** → 与其他代码在视觉上不突出（`}` 独占一行时很醒目，`end` 看起来像普通标识符）

#### 具体场景：为什么 parser 完全没有线索

```lua
function process()
    local data = getData()
    -- 假设这里少了 end

function helper()
    return 42
end

function main()
    local x = helper()
    print(x)
end
```

Parser 的完整思维过程：

```
1. function process()          → 开始解析 process 的函数体
2.     local data = getData()  → process 体内的语句，合法
3.     function helper()       → 合法！这是 process 体内的一条语句：
                                 functioncall 或 assignment
                                 实际上会被解析为：
                                 process 体内的一个函数声明语句
                                 （Lua 允许 `function helper()` 作为语句）
4.         return 42           → helper 体内的语句，合法
5.     end                     → 关闭 helper，回到 process 体
6.     function main()         → 又一个函数声明语句在 process 体内，合法！
7.         local x = helper()  → main 体内的语句，合法
8.         print(x)            → main 体内的语句，合法
9.     end                     → 关闭 main，回到 process 体
10.    <EOF>                   → process 缺少 end！
```

**在第 3 步和第 6 步，parser 没有任何犹豫**。`function helper()` 和 `function main()` 作为 `process` 体内的语句完全合法。

对比 C 的相同场景：
```c
void process() {
    int data = getData();
    // 少了一个 }

void helper() {    // ← "void" 不能出现在函数体内的这个位置！
                   //    Parser: "这里有问题，可能少了 }"
```

C 的 parser 在看到 `void helper()` 时立刻知道"我跑偏了"。但 Lua 的 parser 看到 `function helper()` 时毫无察觉。

#### 与 JavaScript 的微妙区别

JavaScript 的 `function` 也是表达式，但 JS 有救：

```javascript
function process() {
    // 少了 }

export function helper() {    // ← "export" 只能在顶层！
                              //    Parser: "这里可能少了 }"
```

```javascript
class MyClass {               // ← "class" 在函数体内虽合法但很不常见
                              //    启发式可以利用
```

JS 有 `export`、`import`、`class` 等强锚点关键字，给 parser 提供了"紧急出口"。Lua 没有任何等价物。

#### 对 Tree-sitter Lua Grammar 的实际影响

Tree-sitter 的 Lua grammar (`tree-sitter-lua`) 在处理缺失 `end` 时：

1. **不会触发 GLR fork**：因为 `function` 在任何位置都合法，没有歧义
2. **error recovery 只在 EOF 触发**：此时才发现 `end` 不够
3. **产生的 ERROR 节点可能覆盖大量代码**
4. **增量解析退化**：旧 CST 的结构（所有函数是顶层的）和新 CST 的结构（所有函数嵌套在第一个函数内）完全不同

这是 Lua 语法设计的一个"代价"——简洁优雅的语法（`function` 统一为表达式）换来了 parser 在错误恢复时的困境。

#### 小结

`function` 作为"万能关键字"的特性：
- ✅ 让语法简洁统一（不需要 `lambda`, `=>`, `def` 等多种形式）
- ✅ 让闭包写起来自然
- ❌ 让 parser 在缺失 `end` 时完全丧失参照物
- ❌ 让 GLR 并行分支无法发挥作用
- ❌ 让增量解析在这种错误下退化为全量解析

这也是语言设计中"表达力"和"可分析性"之间的经典权衡。
