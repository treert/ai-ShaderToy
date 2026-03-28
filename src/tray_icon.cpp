#include "tray_icon.h"
#include "resource.h"
#include <iostream>
#include <SDL_syswm.h>

#ifdef _WIN32

TrayIcon* TrayIcon::instance_ = nullptr;

TrayIcon::TrayIcon() {
    instance_ = this;
}

TrayIcon::~TrayIcon() {
    Destroy();
    if (instance_ == this) instance_ = nullptr;
}

LRESULT CALLBACK TrayIcon::TrayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (!instance_) return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg) {
    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
            instance_->ShowContextMenu();
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_TRAY_PAUSE:
            if (instance_->callbacks_.onPause) instance_->callbacks_.onPause();
            break;
        case ID_TRAY_RESUME:
            if (instance_->callbacks_.onResume) instance_->callbacks_.onResume();
            break;
        case ID_TRAY_RELOAD:
            if (instance_->callbacks_.onReload) instance_->callbacks_.onReload();
            break;
        case ID_TRAY_QUIT:
            if (instance_->callbacks_.onQuit) instance_->callbacks_.onQuit();
            break;
        }
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool TrayIcon::Create(SDL_Window* window, const MenuCallbacks& callbacks) {
    callbacks_ = callbacks;

    // 创建隐藏窗口用于接收托盘消息
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = TrayWndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"ShaderToyTray";
    RegisterClassExW(&wc);

    hwnd_ = CreateWindowExW(0, L"ShaderToyTray", L"", 0,
                            0, 0, 0, 0, HWND_MESSAGE, nullptr,
                            GetModuleHandle(nullptr), nullptr);
    if (!hwnd_) {
        std::cerr << "Failed to create tray window." << std::endl;
        return false;
    }

    // 从 exe 内嵌资源加载图标，失败则用系统默认
    HICON hIcon = (HICON)LoadImageW(GetModuleHandle(nullptr),
                                    MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
                                    GetSystemMetrics(SM_CXSMICON),
                                    GetSystemMetrics(SM_CYSMICON), 0);
    if (!hIcon) {
        hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512)); // IDI_APPLICATION
    }

    // 创建托盘图标
    ZeroMemory(&nid_, sizeof(nid_));
    nid_.cbSize = sizeof(nid_);
    nid_.hWnd = hwnd_;
    nid_.uID = 1;
    nid_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid_.uCallbackMessage = WM_TRAYICON;
    nid_.hIcon = hIcon;
    wcscpy_s(nid_.szTip, L"ShaderToy Desktop");

    if (!Shell_NotifyIconW(NIM_ADD, &nid_)) {
        std::cerr << "Failed to add tray icon." << std::endl;
        return false;
    }

    // 创建右键菜单
    menu_ = CreatePopupMenu();
    AppendMenuW(menu_, MF_STRING, ID_TRAY_PAUSE,  L"Pause");
    AppendMenuW(menu_, MF_STRING, ID_TRAY_RESUME, L"Resume");
    AppendMenuW(menu_, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu_, MF_STRING, ID_TRAY_RELOAD, L"Reload Shader");
    AppendMenuW(menu_, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu_, MF_STRING, ID_TRAY_QUIT,   L"Quit");

    created_ = true;
    std::cout << "Tray icon created." << std::endl;
    return true;
}

bool TrayIcon::HandleEvent(const SDL_Event& /*event*/) {
    if (!created_ || !hwnd_) return false;

    // 处理托盘窗口消息
    MSG msg;
    while (PeekMessage(&msg, hwnd_, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return false;
}

void TrayIcon::ShowContextMenu() {
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd_);
    TrackPopupMenu(menu_, TPM_RIGHTALIGN | TPM_BOTTOMALIGN,
                   pt.x, pt.y, 0, hwnd_, nullptr);
    PostMessage(hwnd_, WM_NULL, 0, 0);
}

void TrayIcon::Destroy() {
    if (created_) {
        Shell_NotifyIconW(NIM_DELETE, &nid_);
        if (menu_) {
            DestroyMenu(menu_);
            menu_ = nullptr;
        }
        if (hwnd_) {
            DestroyWindow(hwnd_);
            hwnd_ = nullptr;
        }
        created_ = false;
    }
}

#else

TrayIcon::TrayIcon() {}
TrayIcon::~TrayIcon() {}
bool TrayIcon::Create(SDL_Window*, const MenuCallbacks&) {
    std::cerr << "Tray icon not supported on this platform." << std::endl;
    return false;
}
bool TrayIcon::HandleEvent(const SDL_Event&) { return false; }
void TrayIcon::Destroy() {}

#endif
