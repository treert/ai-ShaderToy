#pragma once

#include <string>
#include <vector>
#include <SDL.h>

#ifdef _WIN32
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;
#endif

/// 调试 UI 需要读写的应用状态（main.cpp 与 DebugUI 之间的数据桥梁）
struct DebugUIState {
    // === 只读信息展示（main.cpp 每帧写入，DebugUI 读取展示） ===
    float fps           = 0.0f;
    float adaptiveFPS   = 0.0f;
    float currentTime   = 0.0f;
    float timeDelta     = 0.0f;
    float renderTime    = 0.0f;   // 实际渲染耗时（秒）
    int   frameCount    = 0;
    float resolution[2] = {0.0f, 0.0f};
    float mouse[4]      = {0.0f, 0.0f, 0.0f, 0.0f};
    std::string shaderPath;
    std::string shaderError;
    bool isMultiPass   = false;                 // 是否多 Pass 模式
    std::vector<std::string> passNames;         // 各 pass 名称列表（用于展示）

    // === 可交互控制（DebugUI 修改，main.cpp 读取并应用） ===
    bool  paused      = false;
    int   targetFPS   = 60;
    float renderScale = 1.0f;

    // === 一次性动作标志（DebugUI 置 true，main.cpp 消费后重置） ===
    bool requestReload    = false;
    bool requestResetTime = false;
    bool requestBrowseShader = false;  // 请求打开文件对话框选择 shader
    std::string requestSwitchShader;  // 非空 = 请求切换到该路径的 shader

    // === Shader 文件列表（main.cpp 填充，DebugUI 读取展示，按类型分组） ===
    std::vector<std::string> glslFiles;     // 单文件 .glsl shader
    std::vector<std::string> jsonFiles;     // ShaderToy JSON 导入
    std::vector<std::string> dirFiles;      // 目录模式（含 image.glsl 的子目录）
    std::vector<std::string> stoyFiles;     // .stoy 自定义格式 shader
    bool isStoyMode = false;                // 当前是否为 .stoy 模式（影响 shader 列表显示）
};

/// DebugUI 封装 Dear ImGui 调试面板的全部逻辑。
/// 仅在窗口模式下使用，壁纸模式不初始化。
class DebugUI {
public:
    DebugUI();
    ~DebugUI();

    /// 初始化 ImGui（创建 Context、初始化 SDL2+GL3 后端、配置样式）
    bool Init(SDL_Window* window, SDL_GLContext glContext);

#ifdef _WIN32
    /// 初始化 ImGui（D3D11 后端）
    bool InitD3D11(SDL_Window* window, ID3D11Device* device, ID3D11DeviceContext* context);

    /// D3D11 渲染提交前设置 RTV（每帧调用，在 Render/RenderOverlay 之前）
    void SetD3D11RenderTarget(ID3D11RenderTargetView* rtv);
#endif

    /// 销毁 ImGui（后端 + Context）
    void Shutdown();

    /// 转发 SDL 事件给 ImGui
    void ProcessEvent(const SDL_Event& event);

    /// 开始新的一帧（NewFrame），可选传入当前窗口尺寸（壁纸模式多窗口用）
    void BeginFrame(int windowWidth = 0, int windowHeight = 0);

    /// 绘制调试面板并提交渲染
    void Render(DebugUIState& state);

    /// 只读叠加渲染（壁纸模式用），固定位置纯文字显示，不含交互控件
    void RenderOverlay(const DebugUIState& state);

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

    /// 是否已初始化
    bool IsInitialized() const { return initialized_; }

private:
    bool initialized_ = false;
    bool visible_     = false;
    bool useD3D11_    = false;

#ifdef _WIN32
    ID3D11DeviceContext* d3dContext_ = nullptr;
    ID3D11RenderTargetView* d3dRTV_ = nullptr;
#endif

    // EMA 平滑（统一更新）
    void UpdateSmoothing(const DebugUIState& state);

    // 平滑显示值（EMA）
    float smoothFPS_       = 0.0f;
    float smoothFrameTime_ = 0.0f;
    float smoothTimeDelta_  = 0.0f;
    float smoothRenderTime_ = 0.0f;
    bool  smoothInited_     = false;
};
