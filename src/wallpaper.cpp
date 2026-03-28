#include "wallpaper.h"
#include <iostream>
#include <SDL_syswm.h>

#ifdef _WIN32

HWND Wallpaper::workerW_ = nullptr;

// 回调：遍历所有顶层窗口，找到包含 SHELLDLL_DefView 的 WorkerW
static BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    HWND* pWorkerW = reinterpret_cast<HWND*>(lParam);

    // 查找 SHELLDLL_DefView 子窗口
    HWND shellDllDefView = FindWindowExW(hwnd, nullptr, L"SHELLDLL_DefView", nullptr);
    if (shellDllDefView) {
        // 找到 SHELLDLL_DefView 后，下一个 WorkerW 就是我们需要的
        *pWorkerW = FindWindowExW(nullptr, hwnd, L"WorkerW", nullptr);
    }
    return TRUE;
}

HWND Wallpaper::FindWorkerW() {
    // 1. 找到 Progman 窗口
    HWND progman = FindWindowW(L"Progman", nullptr);
    if (!progman) {
        std::cerr << "Failed to find Progman window." << std::endl;
        return nullptr;
    }

    // 2. 发送未公开消息 0x052C，使 Windows 创建 WorkerW 窗口
    LRESULT result = 0;
    SendMessageTimeoutW(progman, 0x052C, 0, 0, SMTO_NORMAL, 1000,
                        reinterpret_cast<PDWORD_PTR>(&result));

    // 3. 遍历找到正确的 WorkerW
    HWND workerW = nullptr;
    EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&workerW));

    return workerW;
}

bool Wallpaper::EmbedAsWallpaper(SDL_Window* window) {
    workerW_ = FindWorkerW();
    if (!workerW_) {
        std::cerr << "Failed to find WorkerW window." << std::endl;
        return false;
    }

    // 获取 SDL 窗口的原生句柄
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

    // 获取虚拟桌面大小（覆盖所有显示器）并调整窗口
    int x, y, w, h;
    GetVirtualDesktopOffset(x, y);
    GetVirtualDesktopResolution(w, h);
    SetWindowPos(hwnd, nullptr, x, y, w, h, SWP_NOZORDER | SWP_SHOWWINDOW);

    std::cout << "Successfully embedded as wallpaper (" << w << "x" << h
              << " at " << x << "," << y << ")" << std::endl;
    return true;
}

void Wallpaper::Restore() {
    // 恢复桌面 —— 重新发送 0x052C 消息刷新壁纸层
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
// 非 Windows 平台的空实现
bool Wallpaper::EmbedAsWallpaper(SDL_Window*) {
    std::cerr << "Wallpaper mode is only supported on Windows." << std::endl;
    return false;
}

void Wallpaper::Restore() {}

void Wallpaper::GetDesktopResolution(int& width, int& height) {
    width = 1920;
    height = 1080;
}

void Wallpaper::GetVirtualDesktopResolution(int& width, int& height) {
    width = 1920;
    height = 1080;
}

void Wallpaper::GetVirtualDesktopOffset(int& x, int& y) {
    x = 0;
    y = 0;
}
#endif
