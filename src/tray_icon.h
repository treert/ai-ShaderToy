#pragma once

#include <SDL.h>

#ifdef _WIN32
#include <Windows.h>
#include <shellapi.h>
#endif

#include <functional>
#include <string>
#include <vector>

/// TrayIcon 管理系统托盘图标，提供右键菜单控制。
class TrayIcon {
public:
    struct MenuCallbacks {
        std::function<void()> onPause;
        std::function<void()> onResume;
        std::function<void()> onReload;
        std::function<void()> onQuit;
        std::function<void(const std::string&)> onSwitchShader;
        std::function<void()> onToggleDebug;
    };

    TrayIcon();
    ~TrayIcon();

    /// 创建系统托盘图标（图标从 exe 内嵌资源加载）
    bool Create(SDL_Window* window, const MenuCallbacks& callbacks);

    /// 处理窗口消息（在事件循环中调用）
    /// @return true 如果消息被处理
    bool HandleEvent(const SDL_Event& event);

    /// 销毁托盘图标
    void Destroy();

    /// 动态更新托盘图标的 tooltip 文本
    /// @param fps 当前 FPS
    /// @param renderTimeMs 渲染耗时（毫秒）
    /// @param shaderName 当前 shader 名称
    /// @param monitorIndex 显示器索引（-1 = 所有显示器）
    void UpdateTooltip(float fps, float renderTimeMs,
                       const std::string& shaderName, int monitorIndex);

    /// 设置 shader 文件列表和当前 shader 路径（用于右键菜单 shader 切换子菜单）
    void SetShaderList(const std::vector<std::string>& glslFiles,
                       const std::vector<std::string>& jsonFiles,
                       const std::vector<std::string>& dirFiles,
                       const std::string& currentShader);

    /// 设置调试信息显示状态（影响菜单勾选标记）
    void SetDebugState(bool showDebug);

private:
#ifdef _WIN32
    static LRESULT CALLBACK TrayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void ShowContextMenu();
    void RebuildMenu();

    HWND hwnd_ = nullptr;
    NOTIFYICONDATAW nid_ = {};
    HMENU menu_ = nullptr;
    HMENU shaderSubMenu_ = nullptr;
    MenuCallbacks callbacks_;
    bool created_ = false;

    // shader 列表（菜单 ID → 路径映射）
    std::vector<std::string> shaderPaths_;
    std::string currentShader_;
    std::vector<std::string> glslFiles_;
    std::vector<std::string> jsonFiles_;
    std::vector<std::string> dirFiles_;
    bool showDebug_ = false;

    static TrayIcon* instance_;
    static constexpr UINT WM_TRAYICON = WM_USER + 1;
    static constexpr UINT ID_TRAY_PAUSE  = 1001;
    static constexpr UINT ID_TRAY_RESUME = 1002;
    static constexpr UINT ID_TRAY_RELOAD = 1003;
    static constexpr UINT ID_TRAY_QUIT   = 1004;
    static constexpr UINT ID_TRAY_DEBUG  = 1005;
    static constexpr UINT ID_TRAY_SHADER_BASE = 2000;
    static constexpr UINT ID_TRAY_SHADER_MAX  = 2500;
#endif
};
