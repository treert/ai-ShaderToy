#include "wallpaper.h"
#include <iostream>
#include <SDL_syswm.h>

#ifdef _WIN32

HWND Wallpaper::workerW_ = nullptr;

static BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    HWND* pWorkerW = reinterpret_cast<HWND*>(lParam);
    HWND shellDllDefView = FindWindowExW(hwnd, nullptr, L"SHELLDLL_DefView", nullptr);
    if (shellDllDefView) {
        *pWorkerW = FindWindowExW(nullptr, hwnd, L"WorkerW", nullptr);
    }
    return TRUE;
}

HWND Wallpaper::FindWorkerW() {
    HWND progman = FindWindowW(L"Progman", nullptr);
    if (!progman) {
        std::cerr << "Failed to find Progman window." << std::endl;
        return nullptr;
    }
    LRESULT result = 0;
    SendMessageTimeoutW(progman, 0x052C, 0, 0, SMTO_NORMAL, 1000,
                        reinterpret_cast<PDWORD_PTR>(&result));
    HWND workerW = nullptr;
    EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&workerW));
    return workerW;
}

HWND Wallpaper::GetWorkerW() {
    if (!workerW_) {
        workerW_ = FindWorkerW();
    }
    return workerW_;
}

// EnumDisplayMonitors 回调
static BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC, LPRECT, LPARAM lParam) {
    auto* monitors = reinterpret_cast<std::vector<MonitorInfo>*>(lParam);
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(hMonitor, &mi)) {
        MonitorInfo info;
        info.x = mi.rcMonitor.left;
        info.y = mi.rcMonitor.top;
        info.width = mi.rcMonitor.right - mi.rcMonitor.left;
        info.height = mi.rcMonitor.bottom - mi.rcMonitor.top;
        monitors->push_back(info);
        std::cout << "Monitor: (" << info.x << "," << info.y << ") "
                  << info.width << "x" << info.height << std::endl;
    }
    return TRUE;
}

std::vector<MonitorInfo> Wallpaper::EnumMonitors() {
    std::vector<MonitorInfo> monitors;
    EnumDisplayMonitors(nullptr, nullptr, MonitorEnumProc, reinterpret_cast<LPARAM>(&monitors));
    return monitors;
}

bool Wallpaper::EmbedAsWallpaper(SDL_Window* window, const MonitorInfo& monitor) {
    workerW_ = GetWorkerW();
    if (!workerW_) {
        std::cerr << "Failed to find WorkerW window." << std::endl;
        return false;
    }

    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);
    if (!SDL_GetWindowWMInfo(window, &wmInfo)) {
        std::cerr << "Failed to get window WM info: " << SDL_GetError() << std::endl;
        return false;
    }

    HWND hwnd = wmInfo.info.win.window;

    // 移除窗口边框
    LONG style = GetWindowLong(hwnd, GWL_STYLE);
    style &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX |
               WS_MAXIMIZEBOX | WS_SYSMENU);
    SetWindowLong(hwnd, GWL_STYLE, style);

    // 设置为 WorkerW 的子窗口
    SetParent(hwnd, workerW_);

    // WorkerW 的客户区坐标系与虚拟桌面一致
    // 直接用显示器的屏幕坐标定位
    SetWindowPos(hwnd, nullptr, monitor.x, monitor.y,
                 monitor.width, monitor.height,
                 SWP_NOZORDER | SWP_SHOWWINDOW);

    std::cout << "Embedded on monitor (" << monitor.x << "," << monitor.y << ") "
              << monitor.width << "x" << monitor.height << std::endl;
    return true;
}

void Wallpaper::Restore() {
    HWND progman = FindWindowW(L"Progman", nullptr);
    if (progman) {
        SendMessageTimeoutW(progman, 0x052C, 0, 0, SMTO_NORMAL, 1000, nullptr);
    }
    workerW_ = nullptr;
}

void Wallpaper::GetDesktopResolution(int& width, int& height) {
    width = GetSystemMetrics(SM_CXSCREEN);
    height = GetSystemMetrics(SM_CYSCREEN);
}

void Wallpaper::GetVirtualDesktopResolution(int& width, int& height) {
    width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
}

void Wallpaper::GetVirtualDesktopOffset(int& x, int& y) {
    x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    y = GetSystemMetrics(SM_YVIRTUALSCREEN);
}

#else

std::vector<MonitorInfo> Wallpaper::EnumMonitors() {
    return {{0, 0, 1920, 1080}};
}

bool Wallpaper::EmbedAsWallpaper(SDL_Window*, const MonitorInfo&) {
    std::cerr << "Wallpaper mode is only supported on Windows." << std::endl;
    return false;
}

void Wallpaper::Restore() {}

void Wallpaper::GetDesktopResolution(int& width, int& height) {
    width = 1920; height = 1080;
}

void Wallpaper::GetVirtualDesktopResolution(int& width, int& height) {
    width = 1920; height = 1080;
}

void Wallpaper::GetVirtualDesktopOffset(int& x, int& y) {
    x = 0; y = 0;
}

#endif
