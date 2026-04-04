#include "tray_icon.h"
#include "resource.h"
#include <iostream>
#include <SDL_syswm.h>

#ifdef _WIN32

#include <cwchar>
#include <algorithm>

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

    case WM_COMMAND: {
        UINT cmdId = LOWORD(wParam);
        switch (cmdId) {
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
        case ID_TRAY_DEBUG:
            if (instance_->callbacks_.onToggleDebug) instance_->callbacks_.onToggleDebug();
            break;
        case ID_TRAY_BROWSE:
            if (instance_->callbacks_.onBrowseShader) instance_->callbacks_.onBrowseShader();
            break;
        default:
            if (cmdId >= ID_TRAY_SHADER_BASE && cmdId < ID_TRAY_SHADER_MAX) {
                size_t idx = cmdId - ID_TRAY_SHADER_BASE;
                if (idx < instance_->shaderPaths_.size() && instance_->callbacks_.onSwitchShader) {
                    instance_->callbacks_.onSwitchShader(instance_->shaderPaths_[idx]);
                }
            }
            break;
        }
        return 0;
    }
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool TrayIcon::Create(SDL_Window* window, const MenuCallbacks& callbacks) {
    callbacks_ = callbacks;

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

    HICON hIcon = (HICON)LoadImageW(GetModuleHandle(nullptr),
                                    MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
                                    GetSystemMetrics(SM_CXSMICON),
                                    GetSystemMetrics(SM_CYSMICON), 0);
    if (!hIcon) {
        hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
    }

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

    RebuildMenu();

    created_ = true;
    std::cout << "Tray icon created." << std::endl;
    return true;
}

bool TrayIcon::HandleEvent(const SDL_Event& /*event*/) {
    if (!created_ || !hwnd_) return false;

    MSG msg;
    while (PeekMessage(&msg, hwnd_, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return false;
}

void TrayIcon::ShowContextMenu() {
    RebuildMenu();

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd_);
    TrackPopupMenu(menu_, TPM_RIGHTALIGN | TPM_BOTTOMALIGN,
                   pt.x, pt.y, 0, hwnd_, nullptr);
    PostMessage(hwnd_, WM_NULL, 0, 0);
}

void TrayIcon::RebuildMenu() {
    if (menu_) {
        DestroyMenu(menu_);
        menu_ = nullptr;
        shaderSubMenu_ = nullptr;
    }

    menu_ = CreatePopupMenu();
    AppendMenuW(menu_, MF_STRING, ID_TRAY_PAUSE,  L"Pause");
    AppendMenuW(menu_, MF_STRING, ID_TRAY_RESUME, L"Resume");
    AppendMenuW(menu_, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu_, MF_STRING, ID_TRAY_RELOAD, L"Reload Shader");
    AppendMenuW(menu_, MF_STRING | (showDebug_ ? MF_CHECKED : MF_UNCHECKED),
                ID_TRAY_DEBUG, L"Debug Overlay");
    AppendMenuW(menu_, MF_STRING, ID_TRAY_BROWSE, L"Open Shader...");

    // Shader 切换子菜单
    if (!shaderPaths_.empty()) {
        shaderSubMenu_ = CreatePopupMenu();

        UINT menuId = ID_TRAY_SHADER_BASE;

        auto addItems = [&](const std::vector<std::string>& files) {
            for (const auto& file : files) {
                if (menuId >= ID_TRAY_SHADER_MAX) break;

                // 从路径中提取显示名
                std::string displayName = file;
                auto slashPos = file.find_last_of("/\\");
                if (slashPos != std::string::npos) {
                    displayName = file.substr(slashPos + 1);
                }

                // 转宽字符
                int wlen = MultiByteToWideChar(CP_UTF8, 0, displayName.c_str(), -1, nullptr, 0);
                std::wstring wname(wlen, L'\0');
                MultiByteToWideChar(CP_UTF8, 0, displayName.c_str(), -1, &wname[0], wlen);

                UINT flags = MF_STRING;
                if (file == currentShader_) {
                    flags |= MF_CHECKED;
                }
                AppendMenuW(shaderSubMenu_, flags, menuId, wname.c_str());
                menuId++;
            }
        };

        if (isStoyMode_) {
            addItems(stoyFiles_);
        } else {
            if (!glslFiles_.empty()) {
                addItems(glslFiles_);
            }
            if (!jsonFiles_.empty()) {
                if (menuId > ID_TRAY_SHADER_BASE) {
                    AppendMenuW(shaderSubMenu_, MF_SEPARATOR, 0, nullptr);
                }
                addItems(jsonFiles_);
            }
            if (!dirFiles_.empty()) {
                if (menuId > ID_TRAY_SHADER_BASE) {
                    AppendMenuW(shaderSubMenu_, MF_SEPARATOR, 0, nullptr);
                }
                addItems(dirFiles_);
            }
        }

        AppendMenuW(menu_, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(shaderSubMenu_), L"Switch Shader");
    }

    AppendMenuW(menu_, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu_, MF_STRING, ID_TRAY_QUIT, L"Quit");
}

void TrayIcon::UpdateTooltip(float fps, float renderTimeMs,
                              const std::string& shaderName, int monitorIndex) {
    if (!created_) return;

    wchar_t tip[128];
    // 格式: ShaderToy Desktop [Mon:0]\nFPS: 30.0 | RT: 2.1ms\nshader_name
    std::string monStr = (monitorIndex < 0) ? "All" : std::to_string(monitorIndex);
    // 截断 shaderName 避免超出 szTip 128 字符限制
    std::string truncName = shaderName;
    if (truncName.size() > 60) {
        truncName = truncName.substr(0, 57) + "...";
    }
    int written = swprintf_s(tip, 128,
        L"ShaderToy Desktop [Mon:%hs]\nFPS: %.1f | RT: %.1fms\n%hs",
        monStr.c_str(), fps, renderTimeMs, truncName.c_str());
    if (written < 0) {
        wcscpy_s(tip, L"ShaderToy Desktop");
    }

    wcscpy_s(nid_.szTip, tip);
    nid_.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &nid_);
}

void TrayIcon::SetShaderList(const std::vector<std::string>& glslFiles,
                              const std::vector<std::string>& jsonFiles,
                              const std::vector<std::string>& dirFiles,
                              const std::vector<std::string>& stoyFiles,
                              const std::string& currentShader,
                              bool isStoyMode) {
    glslFiles_ = glslFiles;
    jsonFiles_ = jsonFiles;
    dirFiles_ = dirFiles;
    stoyFiles_ = stoyFiles;
    currentShader_ = currentShader;
    isStoyMode_ = isStoyMode;

    // 重建 shaderPaths_ 映射表（顺序需与菜单构建一致）
    shaderPaths_.clear();
    if (isStoyMode_) {
        shaderPaths_.insert(shaderPaths_.end(), stoyFiles_.begin(), stoyFiles_.end());
    } else {
        shaderPaths_.insert(shaderPaths_.end(), glslFiles_.begin(), glslFiles_.end());
        shaderPaths_.insert(shaderPaths_.end(), jsonFiles_.begin(), jsonFiles_.end());
        shaderPaths_.insert(shaderPaths_.end(), dirFiles_.begin(), dirFiles_.end());
    }
}

void TrayIcon::SetDebugState(bool showDebug) {
    showDebug_ = showDebug;
}

void TrayIcon::Destroy() {
    if (created_) {
        Shell_NotifyIconW(NIM_DELETE, &nid_);
        if (menu_) {
            DestroyMenu(menu_);
            menu_ = nullptr;
            shaderSubMenu_ = nullptr;
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
void TrayIcon::UpdateTooltip(float, float, const std::string&, int) {}
void TrayIcon::SetShaderList(const std::vector<std::string>&,
                              const std::vector<std::string>&,
                              const std::vector<std::string>&,
                              const std::vector<std::string>&,
                              const std::string&,
                              bool) {}
void TrayIcon::SetDebugState(bool) {}

#endif
