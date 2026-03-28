#pragma once

#include <SDL.h>

#ifdef _WIN32
#include <Windows.h>
#endif

/// Wallpaper 模块负责将渲染窗口嵌入到 Windows 桌面背景层。
/// 使用 WorkerW 方案实现动态壁纸效果。
class Wallpaper {
public:
    /// 将指定的 SDL 窗口嵌入到桌面壁纸层
    /// @return true 如果成功嵌入
    static bool EmbedAsWallpaper(SDL_Window* window);

    /// 恢复桌面（取消嵌入）
    static void Restore();

    /// 获取桌面分辨率
    static void GetDesktopResolution(int& width, int& height);

private:
#ifdef _WIN32
    static HWND FindWorkerW();
    static HWND workerW_;
#endif
};
