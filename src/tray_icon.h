#pragma once

#include <SDL.h>

#ifdef _WIN32
#include <Windows.h>
#include <shellapi.h>
#endif

#include <functional>

/// TrayIcon 管理系统托盘图标，提供右键菜单控制。
class TrayIcon {
public:
    struct MenuCallbacks {
        std::function<void()> onPause;
        std::function<void()> onResume;
        std::function<void()> onReload;
        std::function<void()> onQuit;
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

private:
#ifdef _WIN32
    static LRESULT CALLBACK TrayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void ShowContextMenu();

    HWND hwnd_ = nullptr;
    NOTIFYICONDATAW nid_ = {};
    HMENU menu_ = nullptr;
    MenuCallbacks callbacks_;
    bool created_ = false;

    static TrayIcon* instance_;
    static constexpr UINT WM_TRAYICON = WM_USER + 1;
    static constexpr UINT ID_TRAY_PAUSE  = 1001;
    static constexpr UINT ID_TRAY_RESUME = 1002;
    static constexpr UINT ID_TRAY_RELOAD = 1003;
    static constexpr UINT ID_TRAY_QUIT   = 1004;
#endif
};
