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

// ============================================================
// BrowseAndValidateShader：单个对话框同时支持选择文件和文件夹
// ============================================================
// 方案：IFileOpenDialog 文件模式 + IFileDialogCustomize 添加 "Select Current Folder" 按钮。
// - 用户选文件（.glsl/.json）→ 正常返回文件路径
// - 用户点 "Select Current Folder" 按钮 → 返回当前浏览的文件夹路径
// 通过 IFileDialogEvents + IFileDialogControlEvents 监听按钮点击。

static constexpr DWORD IDC_SELECT_FOLDER_BTN = 601;

// 事件处理器：监听自定义按钮点击，获取当前文件夹路径并关闭对话框
class BrowseShaderEvents : public IFileDialogEvents, public IFileDialogControlEvents {
public:
    ULONG refCount_ = 1;
    IFileDialog* dialog_ = nullptr;  // 非拥有引用，由外部设置
    std::string folderPath_;
    bool folderSelected_ = false;

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == IID_IFileDialogEvents) {
            *ppv = static_cast<IFileDialogEvents*>(this);
            AddRef();
            return S_OK;
        }
        if (riid == IID_IFileDialogControlEvents) {
            *ppv = static_cast<IFileDialogControlEvents*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refCount_; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = --refCount_;
        if (r == 0) delete this;
        return r;
    }

    // IFileDialogEvents（空实现）
    HRESULT STDMETHODCALLTYPE OnFileOk(IFileDialog*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnFolderChanging(IFileDialog*, IShellItem*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnFolderChange(IFileDialog*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnSelectionChange(IFileDialog*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnShareViolation(IFileDialog*, IShellItem*, FDE_SHAREVIOLATION_RESPONSE*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnTypeChange(IFileDialog*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnOverwrite(IFileDialog*, IShellItem*, FDE_OVERWRITE_RESPONSE*) override { return S_OK; }

    // IFileDialogControlEvents
    HRESULT STDMETHODCALLTYPE OnItemSelected(IFileDialogCustomize*, DWORD, DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnCheckButtonToggled(IFileDialogCustomize*, DWORD, BOOL) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnControlActivating(IFileDialogCustomize*, DWORD) override { return S_OK; }

    HRESULT STDMETHODCALLTYPE OnButtonClicked(IFileDialogCustomize*, DWORD dwIDCtl) override {
        if (dwIDCtl == IDC_SELECT_FOLDER_BTN && dialog_) {
            // 获取当前浏览的文件夹路径
            IShellItem* pFolder = nullptr;
            if (SUCCEEDED(dialog_->GetFolder(&pFolder)) && pFolder) {
                PWSTR pszPath = nullptr;
                if (SUCCEEDED(pFolder->GetDisplayName(SIGDN_FILESYSPATH, &pszPath))) {
                    folderPath_ = WideToUtf8(pszPath);
                    folderSelected_ = true;
                    CoTaskMemFree(pszPath);
                }
                pFolder->Release();
            }
            // 关闭对话框（S_OK 表示确认关闭）
            dialog_->Close(S_OK);
        }
        return S_OK;
    }
};

std::string BrowseAndValidateShader() {
    // 单个对话框：文件选择 + "Select Current Folder" 按钮

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
        pDialog->SetTitle(L"Open Shader");
        pDialog->SetOkButtonLabel(L"Open File");
        SetDefaultShaderFolder(pDialog);

        // 添加 "Select Current Folder" 自定义按钮
        IFileDialogCustomize* pCustomize = nullptr;
        BrowseShaderEvents* pEvents = new BrowseShaderEvents();
        pEvents->dialog_ = pDialog;  // 设置 dialog 引用供按钮回调使用
        DWORD adviseCookie = 0;

        hr = pDialog->QueryInterface(IID_IFileDialogCustomize, reinterpret_cast<void**>(&pCustomize));
        if (SUCCEEDED(hr)) {
            pCustomize->AddPushButton(IDC_SELECT_FOLDER_BTN, L"Select Current Folder");
        }

        pDialog->Advise(pEvents, &adviseCookie);

        hr = pDialog->Show(nullptr);

        if (pEvents->folderSelected_) {
            // 用户点了 "Select Current Folder" 按钮，路径已在事件中获取
            path = pEvents->folderPath_;
        } else if (SUCCEEDED(hr)) {
            // 用户点了 "Open File"，正常获取选中的文件
            path = GetDialogResultPath(pDialog);
        }

        pDialog->Unadvise(adviseCookie);
        if (pCustomize) pCustomize->Release();
        pEvents->Release();
        pDialog->Release();
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
