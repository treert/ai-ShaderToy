# GPU 性能优化方案

## 用户需求

壁纸模式下 GPU 消耗过大（2 个显示器：2K + 4K），希望探索所有可能的优化手段降低 GPU 占用，包括但不限于 OpenGL 层面优化，甚至可以考虑将 OpenGL 换成 Vulkan。

## 产品概述

在保持壁纸视觉效果不变（或几乎不变）的前提下，系统性地降低壁纸模式的 GPU 消耗，让用户在多显示器高分辨率场景下获得更低的功耗和更稳定的帧率。

## 核心特性

- Buffer FBO 纹理格式从 GL_RGBA32F 降级为 GL_RGBA16F，显存带宽减半
- 多显示器共享 Buffer pass 渲染结果，避免重复计算
- Uniform location 缓存，消除每帧大量的 glGetUniformLocation 调用开销
- 桌面遮挡检测增强，窗口最大化覆盖桌面时也暂停渲染
- Blit shader 的 uniform location 一次性缓存

## 技术栈

- 语言：C++17（保持不变）
- 图形 API：OpenGL 3.3 Core（保持不变）
- 构建：CMake + MSVC（保持不变）

## Vulkan 迁移评估（结论：暂不迁移）

经过分析，当前场景下 Vulkan 迁移投入产出比不划算，原因如下：

1. **ShaderToy shader 本质是 fragment shader 密集型**：GPU 消耗的大头在片段着色器执行本身（per-pixel 的数学计算），而非 API 调用开销。Vulkan 的核心优势（减少 CPU 端 draw call 开销、多线程命令录制）在「每帧只画 1~5 个全屏四边形」的场景下几乎没有收益
2. **WorkerW 嵌入兼容性未知**：Vulkan surface 创建需要 VkSurfaceKHR，SDL2 虽然支持 Vulkan，但嵌入 WorkerW 子窗口后 Vulkan swapchain 行为未经验证，风险较高
3. **GLSL 到 SPIR-V 转换复杂**：ShaderToy shader 用的是 GLSL 330，需要通过 glslang 或 shaderc 编译为 SPIR-V，还需处理 uniform buffer 布局差异（Vulkan 不支持独立 uniform，需要改为 UBO/push constant），改动量巨大
4. **迁移工作量**：整个渲染管线（FBO、纹理、shader 编译、多 pass）需要全部重写，预计 2000+ 行代码改动

**结论**：在 OpenGL 层面进行针对性优化，性价比远高于 Vulkan 迁移。下面的优化预计可减少 30~50% 的 GPU 消耗。

## 实现方案

### 优化 1：Buffer FBO 纹理格式降级 GL_RGBA32F -> GL_RGBA16F（预计减少 20~30% 显存带宽）

**现状**：`multi_pass.cpp` CreateFBO() 和 CreateCubeMapFBO() 使用 `GL_RGBA32F`（每像素 16 字节）。对于 4K 0.5x 缩放后 1920x1080 的 Buffer，双缓冲占用 1920x1080x16x2 = 63MB；改为 RGBA16F 后减半为 31.5MB。

**方案**：将 Buffer pass 和 CubeMap pass 的 FBO 内部格式改为 `GL_RGBA16F`。ShaderToy 官网就是用 half-float，绝大多数 shader 不需要 32-bit 精度。保持 `GL_FLOAT` 作为像素传输格式（OpenGL 会自动转换）。

**兼容性**：GL_RGBA16F 是 OpenGL 3.0 核心功能，所有支持 3.3 的显卡都支持。极少数需要高精度的 shader（如双精度物理模拟）可能出现精度差异，但 ShaderToy.com 本身也是 16F，所以兼容性无问题。

### 优化 2：多显示器共享 Buffer pass 渲染（预计减少 30~40% 渲染量）

**现状**：壁纸模式下，main.cpp 对每个显示器独立调用 `multiPass.RenderAllPasses()`，导致所有 Buffer pass 被重复渲染 N 次（N = 显示器数量）。

**方案**：将 `RenderAllPasses` 拆分为两个阶段：

- `RenderBufferPasses()`：渲染所有 Buffer pass + CubeMap pass + 交换双缓冲。**只执行一次**
- `RenderImagePass()`：渲染 Image pass 到指定 FBO/屏幕。**每个显示器各执行一次**

Buffer pass 的渲染尺寸统一使用最大显示器的降分辨率尺寸（已有 `config.width/height = maxW/maxH` 逻辑），所以 Buffer 结果可以直接被所有显示器的 Image pass 共享。

### 优化 3：Uniform location 缓存（减少 CPU 侧 API 调用开销）

**现状**：`RenderSinglePass()` 每帧每 pass 调用约 15 次 `glGetUniformLocation()`。这是一个字符串哈希查找操作，虽然单次很快（~100ns），但在壁纸模式 2 显示器、多 pass 场景下，每帧累计达 60~90 次调用。

**方案**：在 `ShaderManager` 中添加 `CacheUniformLocations()` 方法，编译链接成功后一次性缓存所有 ShaderToy uniform 的 location 到一个结构体。`RenderSinglePass()` 直接使用缓存值。

### 优化 4：桌面遮挡检测增强（减少无意义渲染）

**现状**：`IsFullscreenAppRunning()` 只检测前台窗口是否覆盖整个主屏幕。普通窗口最大化（如浏览器、IDE）不会触发暂停，但此时桌面壁纸完全不可见。

**方案**：增强检测逻辑，枚举所有顶层可见窗口（`GetTopWindow` + `GetNextWindow`），对每个显示器检查是否有**单个窗口**覆盖 ≥90% 面积（如最大化窗口）。注意不能简单累加多窗口面积，因为窗口之间有重叠，累加会严重高估遮挡比例导致误判。每秒检测一次，被遮挡的显示器跳过 Image pass 渲染。

### 优化 5：BlitRenderer uniform 缓存

**现状**：`BlitToScreen()` 每次调用 `glGetUniformLocation(blitProgram_, "uTex")`。

**方案**：Init 时缓存 location。

## 实现注意事项

- **RGBA16F 降级需要向后兼容**：如果未来有 shader 确实需要 32F 精度，可以通过 channels.json 或命令行参数配置，但默认走 16F
- **多显示器共享 Buffer 需要确保 Buffer pass 的 Resize 逻辑正确**：Buffer FBO 使用 max(显示器宽高) * renderScale 作为尺寸
- **BlitRenderer FBO 需按显示器分辨率动态重建**：壁纸模式多显示器分辨率不同时（如 4K+2K），BlitRenderer 的降分辨率 FBO 必须在每个显示器渲染前检查尺寸是否匹配，不匹配时重建。否则小分辨率显示器的 Image pass 只写入 FBO 左下角，导致画面无法填满屏幕
- **遮挡检测暂未启用**：原方案累加多窗口面积容易因重叠导致误判，已暂时关闭，仅保留全屏应用检测。后续可考虑更精确的算法（如分块检测或 DWM 接口）
- **Uniform 缓存需要在 shader 重编译（热加载）后刷新**

## 架构设计

```
每帧渲染流程（优化后）：

壁纸模式:
    Buffer pass 渲染一次
    → 交换双缓冲
    → 遍历显示器
        → 该显示器被遮挡? → 跳过渲染
        → 否 → Image pass 渲染 → Blit 到屏幕 → SwapWindow

窗口模式:
    完整 RenderAllPasses
    → DebugUI + SwapWindow
```

## 涉及文件

| 文件 | 改动内容 |
|------|----------|
| `src/multi_pass.h` | 新增 RenderBufferPasses/RenderImagePass 拆分方法 |
| `src/multi_pass.cpp` | FBO 格式改 GL_RGBA16F；拆分渲染流程；uniform location 使用缓存 |
| `src/shader_manager.h` | 新增 UniformLocations 结构体和 CacheUniformLocations 方法 |
| `src/shader_manager.cpp` | 实现 CacheUniformLocations，LoadFromSource 成功后自动缓存 |
| `src/blit_renderer.h` | 新增 uTexLocation_ 缓存字段 |
| `src/blit_renderer.cpp` | Init 时缓存 uTex location，BlitToScreen 使用缓存 |
| `src/main.cpp` | 壁纸模式渲染循环改用拆分的 Buffer/Image 流程；桌面遮挡检测增强 |

## 关键数据结构

```cpp
// shader_manager.h 新增
struct UniformLocations {
    GLint iResolution = -1;
    GLint iTime = -1;
    GLint iTimeDelta = -1;
    GLint iFrame = -1;
    GLint iMouse = -1;
    GLint iDate = -1;
    GLint iSampleRate = -1;
    GLint iFrameRate = -1;
    GLint iChannelTime = -1;
    GLint iChannelResolution = -1;
    GLint iClickTime = -1;
    GLint iChannel[4] = {-1, -1, -1, -1};
    // CubeMap pass 专用
    GLint cubeFaceRight = -1;
    GLint cubeFaceUp = -1;
    GLint cubeFaceDir = -1;
};
```

## 优化效果总结

| 优化项 | 预计收益 |
|--------|----------|
| FBO 格式 RGBA32F → RGBA16F | 显存带宽减半 |
| Buffer pass 共享 | N 显示器减少 (N-1)/N 的 Buffer 渲染量 |
| 桌面遮挡检测 | 桌面被挡住时 GPU 占用接近 0 |
| Uniform location 缓存 | 消除每帧 60~90 次 glGetUniformLocation |
| BlitRenderer 缓存 | 微优化 |

**综合预估可降低 30~50% 的 GPU 消耗。**
