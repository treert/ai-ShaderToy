#include "file_dialog.h"
#include "shader_project.h"
#include <iostream>
#include <filesystem>

#ifdef _WIN32

#define NOMINMAX
#include <Windows.h>
#include <shobjidl.h>
#include <shlobj.h>

// 宽字符转 UTF-8
static std::string WideToUtf8(const wchar_t* wide) {
    if (!wide || !wide[0]) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string result(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, &result[0], len, nullptr, nullptr);
    return result;
}

// UTF-8 转宽字符
static std::wstring Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (len <= 0) return {};
    std::wstring result(len - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &result[0], len);
    return result;
}

// 内部辅助：设置默认目录为 assets/shaders
static void SetDefaultShaderFolder(IFileOpenDialog* pDialog) {
    std::error_code ec;
    auto absPath = std::filesystem::absolute("assets/shaders", ec);
    if (!ec) {
        std::wstring wpath = absPath.wstring();
        IShellItem* pFolder = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(wpath.c_str(), nullptr, IID_IShellItem,
                                                   reinterpret_cast<void**>(&pFolder)))) {
            pDialog->SetDefaultFolder(pFolder);
            pFolder->Release();
        }
    }
}

// 内部辅助：从 IFileDialog 获取选中项的路径（UTF-8）
static std::string GetDialogResultPath(IFileOpenDialog* pDialog) {
    IShellItem* pItem = nullptr;
    HRESULT hr = pDialog->GetResult(&pItem);
    if (FAILED(hr) || !pItem) return {};
    PWSTR pszPath = nullptr;
    std::string result;
    if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszPath))) {
        result = WideToUtf8(pszPath);
        CoTaskMemFree(pszPath);
    }
    pItem->Release();
    return result;
}

std::string OpenShaderFileDialog() {
    std::string result;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    bool comInitByUs = (hr == S_OK);
    if (FAILED(hr) && hr != S_FALSE) return {};

    IFileOpenDialog* pDialog = nullptr;
    hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL,
                          IID_IFileOpenDialog, reinterpret_cast<void**>(&pDialog));
    if (SUCCEEDED(hr)) {
        COMDLG_FILTERSPEC filters[] = {
            { L"Shader Files (*.glsl, *.json)", L"*.glsl;*.json" },
            { L"GLSL Files (*.glsl)",           L"*.glsl" },
            { L"JSON Files (*.json)",           L"*.json" },
            { L"All Files (*.*)",               L"*.*" },
        };
        pDialog->SetFileTypes(ARRAYSIZE(filters), filters);
        pDialog->SetTitle(L"Select Shader File");
        SetDefaultShaderFolder(pDialog);

        hr = pDialog->Show(nullptr);
        if (SUCCEEDED(hr)) {
            result = GetDialogResultPath(pDialog);
        }
        pDialog->Release();
    }

    if (comInitByUs) CoUninitialize();
    return result;
}

std::string OpenShaderFolderDialog() {
    std::string result;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    bool comInitByUs = (hr == S_OK);
    if (FAILED(hr) && hr != S_FALSE) return {};

    IFileOpenDialog* pDialog = nullptr;
    hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL,
                          IID_IFileOpenDialog, reinterpret_cast<void**>(&pDialog));
    if (SUCCEEDED(hr)) {
        DWORD options = 0;
        pDialog->GetOptions(&options);
        pDialog->SetOptions(options | FOS_PICKFOLDERS);
        pDialog->SetTitle(L"Select Shader Directory (must contain image.glsl)");
        SetDefaultShaderFolder(pDialog);

        hr = pDialog->Show(nullptr);
        if (SUCCEEDED(hr)) {
            result = GetDialogResultPath(pDialog);
        }
        pDialog->Release();
    }

    if (comInitByUs) CoUninitialize();
    return result;
}

std::string BrowseAndValidateShader() {
    // 两步对话框策略（无额外弹窗）：
    //   1. 打开文件选择对话框（过滤 .glsl/.json）
    //   2. 用户取消文件选择后，自动弹出文件夹选择对话框
    // 两个都取消则返回空字符串。

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    bool comInitByUs = (hr == S_OK);
    if (FAILED(hr) && hr != S_FALSE) return {};

    std::string path;

    IFileOpenDialog* pDialog = nullptr;
    hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL,
                          IID_IFileOpenDialog, reinterpret_cast<void**>(&pDialog));
    if (SUCCEEDED(hr)) {
        COMDLG_FILTERSPEC filters[] = {
            { L"Shader Files (*.glsl, *.json)", L"*.glsl;*.json" },
            { L"GLSL Files (*.glsl)",           L"*.glsl" },
            { L"JSON Files (*.json)",           L"*.json" },
            { L"All Files (*.*)",               L"*.*" },
        };
        pDialog->SetFileTypes(ARRAYSIZE(filters), filters);
        pDialog->SetTitle(L"Open Shader - Select file (or Cancel to pick a folder)");
        pDialog->SetOkButtonLabel(L"Open");
        SetDefaultShaderFolder(pDialog);

        hr = pDialog->Show(nullptr);
        if (SUCCEEDED(hr)) {
            path = GetDialogResultPath(pDialog);
        }
        pDialog->Release();
    }

    // 第二步：如果用户取消了文件选择（path 为空），自动弹出文件夹选择
    if (path.empty()) {
        pDialog = nullptr;
        hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL,
                              IID_IFileOpenDialog, reinterpret_cast<void**>(&pDialog));
        if (SUCCEEDED(hr)) {
            DWORD options = 0;
            pDialog->GetOptions(&options);
            pDialog->SetOptions(options | FOS_PICKFOLDERS);
            pDialog->SetTitle(L"Open Shader - Select directory (must contain image.glsl)");
            pDialog->SetOkButtonLabel(L"Open");
            SetDefaultShaderFolder(pDialog);

            hr = pDialog->Show(nullptr);
            if (SUCCEEDED(hr)) {
                path = GetDialogResultPath(pDialog);
            }
            pDialog->Release();
        }
    }

    if (comInitByUs) CoUninitialize();

    if (path.empty()) return {};

    // 验证选中的路径
    ShaderProject testProject;
    if (!testProject.Load(path)) {
        std::string errorMsg = testProject.GetLastError();
        std::wstring wMsg = L"Failed to load shader from selected path:\n\n";
        wMsg += Utf8ToWide(path);
        wMsg += L"\n\nError: ";
        wMsg += Utf8ToWide(errorMsg);
        wMsg += L"\n\nPlease select:\n";
        wMsg += L"  - A single .glsl file (ShaderToy fragment shader)\n";
        wMsg += L"  - A .json file (ShaderToy API export)\n";
        wMsg += L"  - A directory containing image.glsl (multi-file shader project)";

        MessageBoxW(nullptr, wMsg.c_str(), L"Invalid Shader", MB_OK | MB_ICONWARNING);
        return {};
    }

    std::cout << "Browse: selected shader " << path << std::endl;
    return path;
}

#else

// 非 Win32 平台 stub
std::string OpenShaderFileDialog() { return {}; }
std::string OpenShaderFolderDialog() { return {}; }
std::string BrowseAndValidateShader() { return {}; }

#endif
