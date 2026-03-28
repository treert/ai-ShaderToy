#pragma once

#include <SDL.h>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#endif

/// 显示器信息
struct MonitorInfo {
    int x, y;           // 屏幕坐标（物理像素）
    int width, height;  // 分辨率（物理像素）
};

/// Wallpaper 模块负责将渲染窗口嵌入到 Windows 桌面背景层。
/// 支持多显示器：每个显示器创建独立子窗口。
class Wallpaper {
public:
    /// 枚举所有显示器信息
    static std::vector<MonitorInfo> EnumMonitors();

    /// 单显示器模式：将 SDL 窗口嵌入指定显示器的壁纸层
    static bool EmbedAsWallpaper(SDL_Window* window, const MonitorInfo& monitor);

    /// 恢复桌面（取消嵌入）
    static void Restore();

    /// 获取主显示器分辨率
    static void GetDesktopResolution(int& width, int& height);

    /// 获取虚拟桌面分辨率
    static void GetVirtualDesktopResolution(int& width, int& height);

    /// 获取虚拟桌面偏移
    static void GetVirtualDesktopOffset(int& x, int& y);

#ifdef _WIN32
    /// 获取 WorkerW 句柄（供外部使用）
    static HWND GetWorkerW();
#endif

private:
#ifdef _WIN32
    static HWND FindWorkerW();
    static HWND workerW_;
#endif
};
