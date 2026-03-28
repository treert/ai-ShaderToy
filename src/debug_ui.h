#pragma once

#include <string>
#include <vector>
#include <SDL.h>

/// 调试 UI 需要读写的应用状态（main.cpp 与 DebugUI 之间的数据桥梁）
struct DebugUIState {
    // === 只读信息展示（main.cpp 每帧写入，DebugUI 读取展示） ===
    float fps           = 0.0f;
    float adaptiveFPS   = 0.0f;
    float currentTime   = 0.0f;
    float timeDelta     = 0.0f;
    int   frameCount    = 0;
    float resolution[2] = {0.0f, 0.0f};
    float mouse[4]      = {0.0f, 0.0f, 0.0f, 0.0f};
    const char* shaderPath  = "";
    const char* shaderError = "";

    // === 可交互控制（DebugUI 修改，main.cpp 读取并应用） ===
    bool  paused      = false;
    int   targetFPS   = 60;
    float renderScale = 1.0f;

    // === 一次性动作标志（DebugUI 置 true，main.cpp 消费后重置） ===
    bool requestReload    = false;
    bool requestResetTime = false;
    std::string requestSwitchShader;  // 非空 = 请求切换到该路径的 shader

    // === Shader 文件列表（main.cpp 填充，DebugUI 读取展示） ===
    std::vector<std::string> shaderFiles;
};

/// DebugUI 封装 Dear ImGui 调试面板的全部逻辑。
/// 仅在窗口模式下使用，壁纸模式不初始化。
class DebugUI {
public:
    DebugUI();
    ~DebugUI();

    /// 初始化 ImGui（创建 Context、初始化 SDL2+GL3 后端、配置样式）
    bool Init(SDL_Window* window, SDL_GLContext glContext);

    /// 销毁 ImGui（后端 + Context）
    void Shutdown();

    /// 转发 SDL 事件给 ImGui
    void ProcessEvent(const SDL_Event& event);

    /// 开始新的一帧（NewFrame）
    void BeginFrame();

    /// 绘制调试面板并提交渲染
    void Render(DebugUIState& state);

    /// 面板是否可见
    bool IsVisible() const { return visible_; }

    /// 切换显示/隐藏
    void Toggle() { visible_ = !visible_; }

    /// 设置可见性
    void SetVisible(bool v) { visible_ = v; }

    /// ImGui 是否想要捕获鼠标（用于事件吞噬判断）
    bool WantCaptureMouse() const;

    /// ImGui 是否想要捕获键盘
    bool WantCaptureKeyboard() const;

private:
    bool initialized_ = false;
    bool visible_     = false;

    // 平滑显示值（EMA）
    float smoothFPS_       = 0.0f;
    float smoothFrameTime_ = 0.0f;
    float smoothTimeDelta_ = 0.0f;
    bool  smoothInited_    = false;
};
