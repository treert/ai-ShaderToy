#define NOMINMAX
#include <iostream>
#include <string>
#include <ctime>
#include <vector>
#include <atomic>
#include <mutex>
#include <algorithm>
#include <array>
#include <memory>
#include <fstream>
#include <filesystem>

#include <glad/glad.h>
#include <SDL.h>

#ifdef _WIN32
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#endif

#include "renderer.h"
#include "shader_manager.h"
#include "wallpaper.h"
#include "texture_manager.h"
#include "multi_pass.h"
#include "blit_renderer.h"
#include "shader_project.h"
#include "glsl_to_hlsl.h"
#include "file_watcher.h"
#include "tray_icon.h"
#include "debug_ui.h"
#include "file_dialog.h"

#ifdef _WIN32
#include "d3d11_renderer.h"
#include "d3d11_shader_manager.h"
#include "d3d11_texture_manager.h"
#include "d3d11_multi_pass.h"
#include "d3d11_blit_renderer.h"
#include <SDL_syswm.h>
#endif

// 默认参数
static const int    kDefaultWidth  = 800;
static const int    kDefaultHeight = 600;
static const char*  kDefaultShader = "assets/shaders/default.glsl";
static const char*  kWindowTitle   = "ShaderToy Desktop";

struct AppConfig {
    std::string shaderPath = kDefaultShader;
    std::string channel0;  // iChannel0 纹理路径
    std::string channel1;  // iChannel1 纹理路径
    std::string channel2;  // iChannel2 纹理路径
    std::string channel3;  // iChannel3 纹理路径
    std::array<ChannelType, 4> channelTypes = {
        ChannelType::Texture2D, ChannelType::Texture2D,
        ChannelType::Texture2D, ChannelType::Texture2D
    };
    bool        wallpaperMode = false;
    bool        showDebug = false;   // 壁纸模式下显示只读 Debug 信息（--debug）
    bool        hotReload = true;
    bool        translateMode = false; // 翻译模式：GLSL→HLSL 翻译后输出到 Logs 目录
    bool        useD3D11Mode = false; // 窗口模式使用 D3D11 渲染（--d3d11）
    int         width  = kDefaultWidth;
    int         height = kDefaultHeight;
    int         targetFPS = -1;      // -1 表示未指定，壁纸模式默认30，窗口模式默认60
    float       renderScale = 0.0f;  // 渲染分辨率缩放，0=自动（壁纸模式0.5，窗口模式1.0）
    int         monitorIndex = -1;   // 壁纸模式：指定显示器索引，-1=所有显示器
    bool        pauseOnFullscreen = true; // 壁纸模式：全屏应用遮挡时暂停渲染
};

/// Tee streambuf: 同时输出到文件和（可选的）控制台
class TeeStreambuf : public std::streambuf {
public:
    TeeStreambuf(std::streambuf* fileBuf, std::streambuf* consoleBuf)
        : fileBuf_(fileBuf), consoleBuf_(consoleBuf) {}

protected:
    int overflow(int c) override {
        if (c == EOF) return c;
        bool ok = true;
        if (fileBuf_ && fileBuf_->sputc(static_cast<char>(c)) == EOF) ok = false;
        if (consoleBuf_ && consoleBuf_->sputc(static_cast<char>(c)) == EOF) ok = false;
        return ok ? c : EOF;
    }

    std::streamsize xsputn(const char* s, std::streamsize n) override {
        std::streamsize written = n;
        if (fileBuf_) fileBuf_->sputn(s, n);
        if (consoleBuf_) consoleBuf_->sputn(s, n);
        return written;
    }

    int sync() override {
        int r = 0;
        if (fileBuf_ && fileBuf_->pubsync() != 0) r = -1;
        if (consoleBuf_ && consoleBuf_->pubsync() != 0) r = -1;
        return r;
    }

private:
    std::streambuf* fileBuf_;
    std::streambuf* consoleBuf_;
};

// 全局日志资源（生命周期与进程一致）
static std::ofstream g_logFile;
static std::unique_ptr<TeeStreambuf> g_coutTee;
static std::unique_ptr<TeeStreambuf> g_cerrTee;
static bool g_hasConsole = false;

/// 获取 exe 所在目录下的 Logs 子目录路径，不存在则创建
static std::filesystem::path GetLogDir() {
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    auto dir = std::filesystem::path(exePath).parent_path() / "Logs";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

/// 第一步：附加控制台（解析参数前调用，让 --help 等能输出到终端）
static void InitConsole() {
#ifdef _WIN32
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE* fp = nullptr;
        freopen_s(&fp, "CONOUT$", "w", stdout);
        freopen_s(&fp, "CONOUT$", "w", stderr);
        g_hasConsole = true;
    }
#endif
}

/// 第二步：初始化日志文件（解析参数后调用，根据模式选择文件名）
/// wallpaper 模式 → wallpaper.log，窗口模式 → window.log
static void InitLogFile(bool wallpaperMode) {
#ifdef _WIN32
    auto logName = wallpaperMode ? "shadertoy_log_wallpaper.log" : "shadertoy_log_window.log";
    auto logPath = GetLogDir() / logName;
    g_logFile.open(logPath, std::ios::out | std::ios::trunc);

    std::streambuf* consoleBuf = g_hasConsole ? std::cout.rdbuf() : nullptr;

    if (g_logFile.is_open()) {
        g_coutTee = std::make_unique<TeeStreambuf>(g_logFile.rdbuf(), consoleBuf);
        g_cerrTee = std::make_unique<TeeStreambuf>(g_logFile.rdbuf(), consoleBuf);
        std::cout.rdbuf(g_coutTee.get());
        std::cerr.rdbuf(g_cerrTee.get());
    }
#endif
}

static void PrintUsage(const char* programName) {
    InitConsole();
    std::cout << "Usage: " << programName << " [options]\n"
              << "Options:\n"
              << "  --shader <path>      Path to ShaderToy GLSL file (default: " << kDefaultShader << ")\n"
              << "  --channel0 <path>    iChannel0 texture image\n"
              << "  --channel1 <path>    iChannel1 texture image\n"
              << "  --channel2 <path>    iChannel2 texture image\n"
              << "  --channel3 <path>    iChannel3 texture image\n"
              << "  --channeltype0 <t>   iChannel0 type: 2d (default), cube\n"
              << "  --channeltype1 <t>   iChannel1 type: 2d (default), cube\n"
              << "  --channeltype2 <t>   iChannel2 type: 2d (default), cube\n"
              << "  --channeltype3 <t>   iChannel3 type: 2d (default), cube\n"
              << "  --wallpaper          Run as desktop wallpaper\n"
              << "  --monitor <n>        Wallpaper monitor index (0,1,2...; default: all monitors)\n"
              << "  --no-hotreload       Disable shader hot reload\n"
              << "  --width <n>          Window width (default: " << kDefaultWidth << ")\n"
              << "  --height <n>         Window height (default: " << kDefaultHeight << ")\n"
              << "  --fps <n>            Target FPS (wallpaper default: 30, window default: 60)\n"
              << "  --renderscale <f>    Render resolution scale 0.0-1.0 (wallpaper default: 0.5, window default: 1.0)\n"
              << "  --debug              Show debug overlay in wallpaper mode (default: off)\n"
              << "  --no-pause-on-fullscreen  Disable auto-pause when fullscreen app covers desktop\n"
              << "  --translate          Translate shader (GLSL->HLSL) and save to Logs directory, then exit\n"
              << "  --d3d11              Use D3D11 renderer in window mode (default: OpenGL)\n"
              << "  --help, -h           Show this help\n";
}

static AppConfig ParseArgs(int argc, char* argv[]) {
    AppConfig config;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--shader" && i + 1 < argc) {
            config.shaderPath = argv[++i];
        } else if (arg == "--channel0" && i + 1 < argc) {
            config.channel0 = argv[++i];
        } else if (arg == "--channel1" && i + 1 < argc) {
            config.channel1 = argv[++i];
        } else if (arg == "--channel2" && i + 1 < argc) {
            config.channel2 = argv[++i];
        } else if (arg == "--channel3" && i + 1 < argc) {
            config.channel3 = argv[++i];
        } else if (arg.rfind("--channeltype", 0) == 0 && arg.size() == 14 && i + 1 < argc) {
            int idx = arg[13] - '0';
            std::string typeStr = argv[++i];
            if (idx >= 0 && idx < 4) {
                if (typeStr == "cube" || typeStr == "cubemap")
                    config.channelTypes[idx] = ChannelType::CubeMap;
                else if (typeStr == "3d")
                    config.channelTypes[idx] = ChannelType::Texture3D;
                else
                    config.channelTypes[idx] = ChannelType::Texture2D;
            }
        } else if (arg == "--wallpaper") {
            config.wallpaperMode = true;
        } else if (arg == "--monitor" && i + 1 < argc) {
            config.monitorIndex = std::atoi(argv[++i]);
        } else if (arg == "--no-hotreload") {
            config.hotReload = false;
        } else if (arg == "--width" && i + 1 < argc) {
            config.width = std::atoi(argv[++i]);
        } else if (arg == "--height" && i + 1 < argc) {
            config.height = std::atoi(argv[++i]);
        } else if (arg == "--fps" && i + 1 < argc) {
            config.targetFPS = std::atoi(argv[++i]);
        } else if (arg == "--renderscale" && i + 1 < argc) {
            config.renderScale = static_cast<float>(std::atof(argv[++i]));
            if (config.renderScale < 0.1f) config.renderScale = 0.1f;
            if (config.renderScale > 1.0f) config.renderScale = 1.0f;
        } else if (arg == "--debug") {
            config.showDebug = true;
        } else if (arg == "--no-pause-on-fullscreen") {
            config.pauseOnFullscreen = false;
        } else if (arg == "--translate") {
            config.translateMode = true;
        } else if (arg == "--d3d11") {
            config.useD3D11Mode = true;
        } else if (arg == "--help" || arg == "-h") {
            PrintUsage(argv[0]);
            exit(0);
        }
    }
    return config;
}

/// 将 ShaderProjectData 中所有 pass 翻译为 HLSL 并保存到指定目录，同时用 D3DCompile 验证。
/// @param data         已加载的 shader 项目数据
/// @param shaderName   shader 名称（用于单 pass 的文件名）
/// @param outDir       输出目录路径（多 pass 时各 pass 文件直接放在此目录下）
/// @return 编译失败的 pass 数量（0 表示全部通过）
static int TranslateAndDumpHlsl(const ShaderProjectData& data,
                                const std::string& shaderName,
                                const std::filesystem::path& outDir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(outDir, ec);

    auto getChannelTypes = [](const PassData& pass) {
        std::array<ChannelType, 4> types = {
            ChannelType::Texture2D, ChannelType::Texture2D,
            ChannelType::Texture2D, ChannelType::Texture2D
        };
        for (int i = 0; i < 4; ++i) {
            if (pass.channels[i].source != ChannelBinding::Source::None) {
                types[i] = pass.channels[i].textureType;
            }
        }
        return types;
    };

    int compileErrors = 0;

    // 翻译并保存一个 pass，同时编译验证
    auto dumpPass = [&](const std::string& passCode,
                        const std::string& fileName,
                        const std::array<ChannelType, 4>& chTypes,
                        bool isCubeMap) {
        std::string translated = TranslateGlslToHlsl(passCode);
        std::string fullHlsl = WrapShaderToyHlsl(translated, chTypes,
                                                  data.commonSource, isCubeMap);

        std::string compileErrorMsg;
        bool compileOk = CompileHlslForValidation(fullHlsl, fileName, compileErrorMsg);

        if (!compileOk) {
            fullHlsl += "\n\n/*\n=== HLSL Compile Errors ===\n";
            fullHlsl += compileErrorMsg;
            fullHlsl += "\n=== End Compile Errors ===\n*/\n";
            compileErrors++;
        }

        fs::path outPath = outDir / fileName;
        std::ofstream ofs(outPath, std::ios::out | std::ios::trunc);
        if (!ofs.is_open()) {
            std::cerr << "Failed to write: " << outPath.string() << std::endl;
            return;
        }
        ofs << fullHlsl;
        ofs.close();

        if (compileOk) {
            std::cout << "  -> " << outPath.string() << "  [OK]" << std::endl;
        } else {
            std::cerr << "  -> " << outPath.string() << "  [COMPILE ERROR]" << std::endl;
            // 详细错误信息已写入 HLSL 文件末尾，控制台不再重复输出
        }
    };

    bool isMulti = data.isMultiPass || data.hasCubeMapPass || !data.commonSource.empty();

    if (!isMulti) {
        auto chTypes = getChannelTypes(data.imagePass);
        dumpPass(data.imagePass.code, shaderName + ".hlsl", chTypes, false);
    } else {
        // Common 段
        if (!data.commonSource.empty()) {
            std::string commonHlsl = TranslateGlslToHlsl(data.commonSource);
            fs::path commonPath = outDir / "common.hlsl";
            std::ofstream ofs(commonPath, std::ios::out | std::ios::trunc);
            if (ofs.is_open()) {
                ofs << "// Common code (translated from GLSL)\n" << commonHlsl;
                ofs.close();
                std::cout << "  -> " << commonPath.string() << std::endl;
            }
        }

        // CubeMap pass
        if (data.hasCubeMapPass) {
            auto chTypes = getChannelTypes(data.cubeMapPass);
            dumpPass(data.cubeMapPass.code, "cube_a.hlsl", chTypes, true);
        }

        // Buffer passes
        for (size_t bi = 0; bi < data.bufferPasses.size(); ++bi) {
            const auto& bp = data.bufferPasses[bi];
            std::string fname = "buf_";
            fname += static_cast<char>('a' + bi);
            fname += ".hlsl";
            auto chTypes = getChannelTypes(bp);
            dumpPass(bp.code, fname, chTypes, false);
        }

        // Image pass
        {
            auto chTypes = getChannelTypes(data.imagePass);
            dumpPass(data.imagePass.code, "image.hlsl", chTypes, false);
        }
    }

    return compileErrors;
}

int main(int argc, char* argv[]) {
    InitConsole();  // 先附加控制台（让 --help 能输出）

    // 声明 DPI 感知，确保多显示器不同 DPI 时获取正确的物理像素尺寸
#ifdef _WIN32
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
#endif

    AppConfig config = ParseArgs(argc, argv);

    // ============================================================
    // 翻译模式：GLSL → HLSL 翻译后输出到 Logs 目录，然后退出
    // 不需要初始化 SDL / OpenGL / D3D11
    // ============================================================
    if (config.translateMode) {
        InitConsole();  // 确保控制台可用

        ShaderProject project;
        if (!project.Load(config.shaderPath)) {
            std::cerr << "Failed to load shader: " << project.GetLastError() << std::endl;
            return 1;
        }
        const auto& data = project.GetData();

        // 从 shaderPath 提取 shader 名称（不带扩展名）
        namespace fs = std::filesystem;
        fs::path srcPath(config.shaderPath);
        std::string shaderName = srcPath.stem().string();
        if (fs::is_directory(srcPath)) {
            shaderName = srcPath.filename().string();
        }

        auto logDir = GetLogDir() / "translate-mode";
        bool isMulti = data.isMultiPass || data.hasCubeMapPass || !data.commonSource.empty();
        fs::path outDir = isMulti ? (logDir / shaderName) : logDir;

        std::cout << "Translating GLSL -> HLSL: " << config.shaderPath << std::endl;
        int compileErrors = TranslateAndDumpHlsl(data, shaderName, outDir);

        std::cout << "Translation complete, output: " << outDir.string() << std::endl;
        if (compileErrors > 0) {
            std::cerr << "WARNING: " << compileErrors << " pass(es) had HLSL compile errors. "
                      << "Check the .hlsl files for error details." << std::endl;
        }
        return 0;
    }

    // 根据模式初始化日志文件（wallpaper.log / window.log）
    InitLogFile(config.wallpaperMode);

    // ============================================================
    // 初始化 SDL
    // ============================================================
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << std::endl;
        return 1;
    }

    // OpenGL 3.3 Core Profile
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    // ============================================================
    // 创建窗口（壁纸模式为每个显示器创建独立窗口）
    // ============================================================
    struct WallpaperWindow {
        SDL_Window* window = nullptr;
        int x = 0, y = 0;          // 显示器屏幕坐标
        int width = 0, height = 0;
        bool occluded = false;      // 是否被窗口遮挡（遮挡时跳过渲染）
        float clickTime = -10.0f;   // 该显示器上最近一次点击的 iTime（per-monitor）
        float clickLocalX = 0.0f;   // 该显示器上最近一次点击的局部坐标 X
        float clickLocalY = 0.0f;   // 该显示器上最近一次点击的局部坐标 Y
        bool clickActive = false;   // 该显示器上是否有活跃的点击（按下状态）
        std::unique_ptr<BlitRenderer> blit; // 每个显示器独立的 BlitRenderer（不同分辨率各自持有 FBO）— 窗口模式 OpenGL 用
        int d3dSwapChainIndex = -1; // D3D11 SwapChain 索引（壁纸模式用）
        std::unique_ptr<D3D11BlitRenderer> d3dBlit; // D3D11 降分辨率渲染器
    };
    std::vector<WallpaperWindow> wallpaperWindows;
    SDL_Window* window = nullptr;       // 主窗口（窗口模式用，壁纸模式指向第一个）
    SDL_GLContext glContext = nullptr;

    // D3D11 壁纸模式渲染资源
#ifdef _WIN32
    std::unique_ptr<D3D11Renderer> d3dRenderer;
    std::unique_ptr<D3D11MultiPass> d3dMultiPass;
    std::unique_ptr<D3D11TextureManager> d3dTextures;
    bool useD3D11 = false;  // 壁纸模式使用 D3D11
#endif

    if (config.wallpaperMode) {
        auto monitors = Wallpaper::EnumMonitors();
        if (monitors.empty()) {
            std::cerr << "No monitors found." << std::endl;
            SDL_Quit();
            return 1;
        }

        // --monitor 参数校验
        if (config.monitorIndex >= static_cast<int>(monitors.size())) {
            std::cerr << "Monitor index " << config.monitorIndex
                      << " out of range (0~" << monitors.size() - 1 << "), using all monitors." << std::endl;
            config.monitorIndex = -1;
        }

#ifdef _WIN32
        // 壁纸模式使用 D3D11：不创建 OpenGL 窗口
        d3dRenderer = std::make_unique<D3D11Renderer>();
        if (!d3dRenderer->Init()) {
            std::cerr << "D3D11 init failed: " << d3dRenderer->GetLastError()
                      << ", falling back to window mode." << std::endl;
            d3dRenderer.reset();
            config.wallpaperMode = false;
        }
#endif

        if (config.wallpaperMode) {
            for (size_t i = 0; i < monitors.size(); ++i) {
                if (config.monitorIndex >= 0 && static_cast<int>(i) != config.monitorIndex) {
                    continue;
                }
                const auto& mon = monitors[i];
                // 不使用 SDL_WINDOW_OPENGL — D3D11 通过 HWND 创建 SwapChain
                Uint32 flags = SDL_WINDOW_SHOWN | SDL_WINDOW_BORDERLESS;

                SDL_Window* win = SDL_CreateWindow(
                    kWindowTitle, mon.x, mon.y, mon.width, mon.height, flags);
                if (!win) {
                    std::cerr << "Failed to create window for monitor " << i << ": "
                              << SDL_GetError() << std::endl;
                    continue;
                }

                // 嵌入到桌面壁纸层
                if (!Wallpaper::EmbedAsWallpaper(win, mon)) {
                    std::cerr << "Failed to embed monitor " << i << std::endl;
                    SDL_DestroyWindow(win);
                    continue;
                }

#ifdef _WIN32
                // 获取 HWND 并创建 D3D11 SwapChain
                SDL_SysWMinfo wmInfo;
                SDL_VERSION(&wmInfo.version);
                int scIdx = -1;
                if (SDL_GetWindowWMInfo(win, &wmInfo)) {
                    scIdx = d3dRenderer->AddSwapChain(wmInfo.info.win.window, mon.width, mon.height);
                }
                if (scIdx < 0) {
                    std::cerr << "Failed to create D3D11 SwapChain for monitor " << i << std::endl;
                    SDL_DestroyWindow(win);
                    continue;
                }
#endif

                WallpaperWindow ww;
                ww.window = win;
                ww.x = mon.x;
                ww.y = mon.y;
                ww.width = mon.width;
                ww.height = mon.height;
#ifdef _WIN32
                ww.d3dSwapChainIndex = scIdx;
#endif
                wallpaperWindows.push_back(std::move(ww));
            }

            if (wallpaperWindows.empty()) {
                std::cerr << "Failed to embed any monitor, falling back to window mode." << std::endl;
                config.wallpaperMode = false;
            } else {
                window = wallpaperWindows[0].window;
                int maxW = 0, maxH = 0;
                for (const auto& ww : wallpaperWindows) {
                    if (ww.width > maxW) maxW = ww.width;
                    if (ww.height > maxH) maxH = ww.height;
                }
                config.width = maxW;
                config.height = maxH;
#ifdef _WIN32
                useD3D11 = true;
#endif
            }
        }
    }

    // 窗口模式
    if (!config.wallpaperMode) {
#ifdef _WIN32
        if (config.useD3D11Mode) {
            // D3D11 窗口模式：不创建 OpenGL 上下文
            Uint32 windowFlags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;
            window = SDL_CreateWindow(
                kWindowTitle,
                SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                config.width, config.height,
                windowFlags
            );
            if (!window) {
                std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << std::endl;
                SDL_Quit();
                return 1;
            }

            // 初始化 D3D11
            d3dRenderer = std::make_unique<D3D11Renderer>();
            if (!d3dRenderer->Init()) {
                std::cerr << "D3D11 init failed: " << d3dRenderer->GetLastError()
                          << ", falling back to OpenGL." << std::endl;
                d3dRenderer.reset();
                config.useD3D11Mode = false;
                // 降级到 OpenGL：重建窗口
                SDL_DestroyWindow(window);
                window = nullptr;
            } else {
                SDL_SysWMinfo wmInfo;
                SDL_VERSION(&wmInfo.version);
                int scIdx = -1;
                if (SDL_GetWindowWMInfo(window, &wmInfo)) {
                    scIdx = d3dRenderer->AddSwapChain(wmInfo.info.win.window, config.width, config.height);
                }
                if (scIdx < 0) {
                    std::cerr << "D3D11 SwapChain creation failed, falling back to OpenGL." << std::endl;
                    d3dRenderer.reset();
                    config.useD3D11Mode = false;
                    SDL_DestroyWindow(window);
                    window = nullptr;
                } else {
                    useD3D11 = true;
                    std::cout << "Window mode: D3D11 renderer" << std::endl;
                }
            }
        }
#endif
        // OpenGL 窗口模式（默认，或 D3D11 降级后）
        if (!window) {
            Uint32 windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;
            window = SDL_CreateWindow(
                kWindowTitle,
                SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                config.width, config.height,
                windowFlags
            );
            if (!window) {
                std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << std::endl;
                SDL_Quit();
                return 1;
            }
            glContext = SDL_GL_CreateContext(window);
            if (!glContext) {
                std::cerr << "SDL_GL_CreateContext failed: " << SDL_GetError() << std::endl;
                SDL_DestroyWindow(window);
                SDL_Quit();
                return 1;
            }
            if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
                std::cerr << "gladLoadGLLoader failed." << std::endl;
                SDL_GL_DeleteContext(glContext);
                SDL_DestroyWindow(window);
                SDL_Quit();
                return 1;
            }
        }
    }

    if (glContext) {
        std::cout << "OpenGL " << glGetString(GL_VERSION) << std::endl;
        std::cout << "Renderer: " << glGetString(GL_RENDERER) << std::endl;
    }

    // 应用模式相关的默认值
    if (config.targetFPS < 0) {
        config.targetFPS = config.wallpaperMode ? 30 : 60;
    }
    if (config.renderScale <= 0.0f) {
        config.renderScale = config.wallpaperMode ? 0.5f : 1.0f;
    }

    if (glContext) {
        // VSync 策略：关闭 VSync，纯靠 SDL_Delay 精确控帧
        SDL_GL_SetSwapInterval(0);
    }

    std::cout << "Target FPS: " << config.targetFPS
              << ", Render scale: " << config.renderScale << std::endl;

    // ============================================================
    // D3D11 渲染资源初始化（壁纸模式 + 窗口 D3D11 模式共用）
    // ============================================================
#ifdef _WIN32
    std::unique_ptr<D3D11BlitRenderer> d3dWindowBlit; // 窗口模式 D3D11 降分辨率渲染器
    if (useD3D11 && d3dRenderer) {
        bool useScaledD3D = (config.renderScale < 1.0f);
        if (config.wallpaperMode) {
            // 壁纸模式：每个显示器独立的 BlitRenderer
            if (useScaledD3D) {
                for (auto& ww : wallpaperWindows) {
                    ww.d3dBlit = std::make_unique<D3D11BlitRenderer>();
                    if (!ww.d3dBlit->Init(d3dRenderer->GetDevice(), d3dRenderer->GetContext())) {
                        std::cerr << "D3D11BlitRenderer init failed." << std::endl;
                        useScaledD3D = false;
                        break;
                    }
                    ww.d3dBlit->CreateRenderTarget(ww.width, ww.height, config.renderScale);
                    std::cout << "D3D11 Monitor (" << ww.x << "," << ww.y << ") scaled: "
                              << ww.d3dBlit->GetRenderWidth() << "x" << ww.d3dBlit->GetRenderHeight() << std::endl;
                }
                if (!useScaledD3D) {
                    for (auto& ww : wallpaperWindows) ww.d3dBlit.reset();
                }
            }
        } else {
            // 窗口模式 D3D11：单个 BlitRenderer
            if (useScaledD3D) {
                d3dWindowBlit = std::make_unique<D3D11BlitRenderer>();
                if (!d3dWindowBlit->Init(d3dRenderer->GetDevice(), d3dRenderer->GetContext())) {
                    std::cerr << "D3D11BlitRenderer init failed, disabling scaled rendering." << std::endl;
                    d3dWindowBlit.reset();
                    useScaledD3D = false;
                } else {
                    d3dWindowBlit->CreateRenderTarget(config.width, config.height, config.renderScale);
                    std::cout << "D3D11 Window scaled: "
                              << d3dWindowBlit->GetRenderWidth() << "x" << d3dWindowBlit->GetRenderHeight() << std::endl;
                }
            }
        }

        d3dMultiPass = std::make_unique<D3D11MultiPass>();
        d3dMultiPass->SetDevice(d3dRenderer->GetDevice(), d3dRenderer->GetContext());
        d3dMultiPass->Init(config.width, config.height);

        d3dTextures = std::make_unique<D3D11TextureManager>();
        d3dTextures->SetDevice(d3dRenderer->GetDevice(), d3dRenderer->GetContext());
    }
#endif

    // ============================================================
    // OpenGL 降分辨率渲染（renderScale < 1.0 时启用）
    // ============================================================
    BlitRenderer blitRenderer;  // 窗口模式使用
    bool useScaledRender = (config.renderScale < 1.0f);

    if (useScaledRender && !useD3D11) {
        // 窗口模式：使用全局 blitRenderer
        if (!blitRenderer.Init()) {
            std::cerr << "BlitRenderer init failed, disabling scaled rendering." << std::endl;
            useScaledRender = false;
        } else {
            blitRenderer.CreateRenderFBO(config.width, config.height, config.renderScale);
            std::cout << "Scaled rendering: " << blitRenderer.GetRenderWidth() << "x"
                      << blitRenderer.GetRenderHeight()
                      << " (scale=" << config.renderScale << ")" << std::endl;
        }
    }

    // ============================================================
    // 初始化 OpenGL 渲染器（全屏四边形 VAO）— 仅窗口模式
    // ============================================================
    Renderer renderer;
    if (glContext) {
        if (!renderer.Init()) {
            std::cerr << "Renderer init failed." << std::endl;
            SDL_GL_DeleteContext(glContext);
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }
        renderer.SetViewport(config.width, config.height);
    }

    // ============================================================
    // 加载 Shader 项目（支持单文件 .glsl / JSON / 目录三种模式）
    // ============================================================
    ShaderProject project;
    if (!project.Load(config.shaderPath)) {
        std::cerr << "Failed to load shader project: " << project.GetLastError() << std::endl;
        SDL_GL_DeleteContext(glContext);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // ============================================================
    // 加载纹理
    // ============================================================
    TextureManager textures;

    // 辅助函数：根据项目数据加载纹理（初始化和 shader 切换时都调用）
    auto LoadTexturesForProject = [&](const ShaderProjectData& data) {
        textures.Clear();

        // 单文件模式：使用命令行 --channel0~3 参数
        // 仅当 imagePass 没有任何通道绑定时才走命令行参数（真正的单 .glsl 文件）
        bool hasChannelBindings = false;
        for (int i = 0; i < 4; ++i) {
            if (data.imagePass.channels[i].source != ChannelBinding::Source::None) {
                hasChannelBindings = true;
                break;
            }
        }
        if (!hasChannelBindings && !data.isMultiPass && data.commonSource.empty()) {
            std::string channelPaths[4] = {config.channel0, config.channel1, config.channel2, config.channel3};
            for (int i = 0; i < 4; ++i) {
                if (channelPaths[i].empty()) continue;
                if (config.channelTypes[i] == ChannelType::CubeMap) {
                    textures.LoadCubeMap(i, channelPaths[i]);
                } else {
                    textures.LoadTexture(i, channelPaths[i]);
                }
            }
        } else {
            // JSON/目录模式：从项目数据中加载外部纹理
            auto loadPassTextures = [&](const PassData& pass) {
                for (int i = 0; i < 4; ++i) {
                    const auto& ch = pass.channels[i];
                    if (ch.source == ChannelBinding::Source::ExternalTexture && !ch.texturePath.empty()) {
                        if (!textures.HasTexture(i)) {
                            if (ch.textureType == ChannelType::CubeMap) {
                                textures.LoadCubeMap(i, ch.texturePath);
                            } else {
                                textures.LoadTexture(i, ch.texturePath);
                            }
                        }
                    }
                }
            };
            loadPassTextures(data.imagePass);
            for (const auto& bp : data.bufferPasses) {
                loadPassTextures(bp);
            }
            if (data.hasCubeMapPass) {
                loadPassTextures(data.cubeMapPass);
            }
        }

        // 从实际加载结果同步通道类型
        for (int i = 0; i < 4; ++i) {
            ChannelType actual = textures.GetChannelType(i);
            if (actual != ChannelType::None) {
                config.channelTypes[i] = actual;
            }
        }
    };

    const auto& projData = project.GetData();
    LoadTexturesForProject(projData);

    // ============================================================
    // 配置 MultiPassRenderer
    // ============================================================
    MultiPassRenderer multiPass;
    multiPass.Init(config.width, config.height);

    // 辅助函数：根据 ShaderProjectData 配置 MultiPassRenderer
    auto SetupMultiPass = [&](const ShaderProjectData& data) -> bool {
        multiPass.Clear();
        multiPass.Init(config.width, config.height);
        multiPass.SetCommonSource(data.commonSource);

        // 设置外部纹理
        for (int i = 0; i < 4; ++i) {
            if (textures.HasTexture(i)) {
                float tw, th;
                textures.GetResolution(i, tw, th);
                multiPass.SetExternalTexture(i, textures.GetTextureID(i),
                                             static_cast<int>(tw), static_cast<int>(th),
                                             textures.GetChannelType(i));
            }
        }

        // 辅助 lambda：将 ChannelBinding 转换为 inputChannels 和 channelTypes
        auto resolveChannels = [](const PassData& pass,
                                  std::array<int, 4>& inputs,
                                  std::array<ChannelType, 4>& chTypes) {
            inputs = {-1, -1, -1, -1};
            chTypes = {
                ChannelType::Texture2D, ChannelType::Texture2D,
                ChannelType::Texture2D, ChannelType::Texture2D
            };
            for (int i = 0; i < 4; ++i) {
                const auto& ch = pass.channels[i];
                if (ch.source == ChannelBinding::Source::Buffer) {
                    inputs[i] = ch.bufferIndex;
                } else if (ch.source == ChannelBinding::Source::ExternalTexture) {
                    inputs[i] = 100 + i;  // 外部纹理通道
                    chTypes[i] = ch.textureType;
                } else if (ch.source == ChannelBinding::Source::CubeMapPass) {
                    inputs[i] = 200;  // CubeMap pass 输出
                    chTypes[i] = ChannelType::CubeMap;
                }
            }
        };

        // 设置 CubeMap pass（如果有）
        if (data.hasCubeMapPass) {
            std::array<int, 4> inputs;
            std::array<ChannelType, 4> chTypes;
            resolveChannels(data.cubeMapPass, inputs, chTypes);
            if (!multiPass.SetCubeMapPass(data.cubeMapPass.code, inputs, chTypes)) {
                return false;
            }
        }

        // 添加 Buffer passes
        for (const auto& bp : data.bufferPasses) {
            std::array<int, 4> inputs;
            std::array<ChannelType, 4> chTypes;
            resolveChannels(bp, inputs, chTypes);
            if (multiPass.AddBufferPass(bp.name, bp.code, inputs, chTypes) < 0) {
                return false;
            }
        }

        // 设置 Image pass
        {
            std::array<int, 4> inputs;
            std::array<ChannelType, 4> chTypes;
            resolveChannels(data.imagePass, inputs, chTypes);
            if (!multiPass.SetImagePass(data.imagePass.code, inputs, chTypes)) {
                return false;
            }
        }

        // 降分辨率模式：Image pass 渲染到 blitRenderer 的 FBO
        // 壁纸模式下每个显示器有独立 FBO，在渲染循环中动态设置
        if (useScaledRender && !config.wallpaperMode && blitRenderer.GetRenderFBO()) {
            multiPass.SetImageTargetFBO(blitRenderer.GetRenderFBO());
            multiPass.Resize(blitRenderer.GetRenderWidth(), blitRenderer.GetRenderHeight());
        }

        return true;
    };

    if (!useD3D11) {
        if (!SetupMultiPass(projData)) {
            std::cerr << "Failed to setup multi-pass renderer: " << multiPass.GetLastError() << std::endl;
            if (glContext) SDL_GL_DeleteContext(glContext);
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }
    }

    // D3D11 壁纸模式：配置 D3D11MultiPass
#ifdef _WIN32
    auto SetupD3D11MultiPass = [&](const ShaderProjectData& data) -> bool {
        if (!d3dMultiPass) return false;
        d3dMultiPass->Clear();
        d3dMultiPass->Init(config.width, config.height);
        d3dMultiPass->SetCommonSource(data.commonSource);

        // resolveChannels 与 OpenGL 版本相同
        auto resolveChannels = [](const PassData& pass,
                                  std::array<int, 4>& inputs,
                                  std::array<ChannelType, 4>& chTypes) {
            inputs = {-1, -1, -1, -1};
            chTypes = { ChannelType::Texture2D, ChannelType::Texture2D,
                        ChannelType::Texture2D, ChannelType::Texture2D };
            for (int i = 0; i < 4; ++i) {
                const auto& ch = pass.channels[i];
                if (ch.source == ChannelBinding::Source::Buffer) {
                    inputs[i] = ch.bufferIndex;
                } else if (ch.source == ChannelBinding::Source::ExternalTexture) {
                    inputs[i] = 100 + i;
                    chTypes[i] = ch.textureType;
                } else if (ch.source == ChannelBinding::Source::CubeMapPass) {
                    inputs[i] = 200;
                    chTypes[i] = ChannelType::CubeMap;
                }
            }
        };

        // 设置外部纹理 SRV
        if (d3dTextures) {
            for (int i = 0; i < 4; ++i) {
                if (d3dTextures->HasTexture(i)) {
                    float tw, th;
                    d3dTextures->GetResolution(i, tw, th);
                    d3dMultiPass->SetExternalTexture(i, d3dTextures->GetSRV(i),
                                                      static_cast<int>(tw), static_cast<int>(th),
                                                      d3dTextures->GetChannelType(i));
                }
            }
        }

        if (data.hasCubeMapPass) {
            std::array<int, 4> inputs; std::array<ChannelType, 4> chTypes;
            resolveChannels(data.cubeMapPass, inputs, chTypes);
            if (!d3dMultiPass->SetCubeMapPass(data.cubeMapPass.code, inputs, chTypes)) return false;
        }
        for (const auto& bp : data.bufferPasses) {
            std::array<int, 4> inputs; std::array<ChannelType, 4> chTypes;
            resolveChannels(bp, inputs, chTypes);
            if (d3dMultiPass->AddBufferPass(bp.name, bp.code, inputs, chTypes) < 0) return false;
        }
        {
            std::array<int, 4> inputs; std::array<ChannelType, 4> chTypes;
            resolveChannels(data.imagePass, inputs, chTypes);
            if (!d3dMultiPass->SetImagePass(data.imagePass.code, inputs, chTypes)) return false;
        }
        return true;
    };

    if (useD3D11) {
        // D3D11 纹理加载
        if (d3dTextures) {
            const auto& data = projData;
            bool hasChannelBindings = false;
            for (int i = 0; i < 4; ++i) {
                if (data.imagePass.channels[i].source != ChannelBinding::Source::None) {
                    hasChannelBindings = true; break;
                }
            }
            if (!hasChannelBindings && !data.isMultiPass && data.commonSource.empty()) {
                std::string channelPaths[4] = {config.channel0, config.channel1, config.channel2, config.channel3};
                for (int i = 0; i < 4; ++i) {
                    if (channelPaths[i].empty()) continue;
                    if (config.channelTypes[i] == ChannelType::CubeMap)
                        d3dTextures->LoadCubeMap(i, channelPaths[i]);
                    else
                        d3dTextures->LoadTexture(i, channelPaths[i]);
                }
            } else {
                auto loadPassTex = [&](const PassData& pass) {
                    for (int i = 0; i < 4; ++i) {
                        const auto& ch = pass.channels[i];
                        if (ch.source == ChannelBinding::Source::ExternalTexture && !ch.texturePath.empty()) {
                            if (!d3dTextures->HasTexture(i)) {
                                if (ch.textureType == ChannelType::CubeMap)
                                    d3dTextures->LoadCubeMap(i, ch.texturePath);
                                else
                                    d3dTextures->LoadTexture(i, ch.texturePath);
                            }
                        }
                    }
                };
                loadPassTex(data.imagePass);
                for (const auto& bp : data.bufferPasses) loadPassTex(bp);
                if (data.hasCubeMapPass) loadPassTex(data.cubeMapPass);
            }
        }

        // HLSL 翻译输出：先 dump 再 setup，确保编译失败也能输出 HLSL 方便调试
        {
            namespace fs = std::filesystem;
            fs::path srcPath(config.shaderPath);
            std::string sName = srcPath.stem().string();
            if (fs::is_directory(srcPath)) {
                sName = srcPath.filename().string();
            }
            std::string subDir = config.wallpaperMode ? "wallpaper-mode" : "window-mode";
            fs::path outDir = GetLogDir() / subDir;
            std::cout << "HLSL dump (" << subDir << "): " << config.shaderPath << std::endl;
            int errors = TranslateAndDumpHlsl(project.GetData(), sName, outDir);
            if (errors > 0) {
                std::cerr << "WARNING: " << errors << " pass(es) had HLSL compile errors." << std::endl;
            }
        }

        if (!SetupD3D11MultiPass(projData)) {
            std::cerr << "Failed to setup D3D11 multi-pass: "
                      << (d3dMultiPass ? d3dMultiPass->GetLastError() : "null") << std::endl;
            SDL_Quit();
            return 1;
        }
    }
#endif

    std::cout << "Shader loaded: " << config.shaderPath
              << (projData.isMultiPass ? " (multi-pass)" : " (single-pass)") << std::endl;

    // HLSL 翻译输出 lambda（热加载时复用）
    auto dumpHlsl = [&]() {
#ifdef _WIN32
        if (!useD3D11) return;
        namespace fs = std::filesystem;
        fs::path srcPath(config.shaderPath);
        std::string shaderName = srcPath.stem().string();
        if (fs::is_directory(srcPath)) {
            shaderName = srcPath.filename().string();
        }
        std::string subDir = config.wallpaperMode ? "wallpaper-mode" : "window-mode";
        fs::path outDir = GetLogDir() / subDir;
        std::cout << "HLSL dump (" << subDir << "): " << config.shaderPath << std::endl;
        int errors = TranslateAndDumpHlsl(project.GetData(), shaderName, outDir);
        if (errors > 0) {
            std::cerr << "WARNING: " << errors << " pass(es) had HLSL compile errors." << std::endl;
        }
#endif
    };

    // ============================================================
    // 热加载
    // ============================================================
    std::atomic<bool> shaderNeedsReload{false};
    FileWatcher watcher;
    if (config.hotReload) {
        auto files = project.GetAllFiles();
        if (!files.empty()) {
            watcher.Watch(files[0], [&](const std::string&) {
                shaderNeedsReload.store(true);
            });
            for (size_t i = 1; i < files.size(); ++i) {
                watcher.AddFile(files[i]);
            }
        }
        std::cout << "Hot reload enabled (" << files.size() << " file(s) monitored)." << std::endl;
    }

    // ============================================================
    // 调试 UI 和 Shader 文件扫描（需要在托盘初始化之前声明）
    // ============================================================
    DebugUI debugUI;
    DebugUIState debugState;
    std::string lastShaderError;

    // 扫描 assets/shaders/ 目录获取所有 shader 文件，按类型分三组
    auto ScanShaderFiles = [&]() {
        debugState.glslFiles.clear();
        debugState.jsonFiles.clear();
        debugState.dirFiles.clear();
        const std::string shaderDir = "assets/shaders";
        std::error_code ec;
        if (std::filesystem::exists(shaderDir, ec)) {
            for (const auto& entry : std::filesystem::directory_iterator(shaderDir, ec)) {
                if (entry.is_regular_file()) {
                    auto ext = entry.path().extension();
                    std::string path = entry.path().generic_string();
                    if (ext == ".glsl") {
                        debugState.glslFiles.push_back(path);
                    } else if (ext == ".json") {
                        debugState.jsonFiles.push_back(path);
                    }
                } else if (entry.is_directory()) {
                    auto imagePath = entry.path() / "image.glsl";
                    if (std::filesystem::exists(imagePath, ec)) {
                        std::string path = entry.path().generic_string();
                        debugState.dirFiles.push_back(path);
                    }
                }
            }
            std::sort(debugState.glslFiles.begin(), debugState.glslFiles.end());
            std::sort(debugState.jsonFiles.begin(), debugState.jsonFiles.end());
            std::sort(debugState.dirFiles.begin(), debugState.dirFiles.end());
        }
    };

    // FileWatcher 重建辅助函数（shader 切换时复用）
    auto RebuildFileWatcher = [&]() {
        if (!config.hotReload) return;
        watcher.Stop();
        ShaderProject newProject;
        if (newProject.Load(config.shaderPath)) {
            project = std::move(newProject);
            auto files = project.GetAllFiles();
            if (!files.empty()) {
                watcher.Watch(files[0], [&](const std::string&) {
                    shaderNeedsReload.store(true);
                });
                for (size_t fi = 1; fi < files.size(); ++fi) {
                    watcher.AddFile(files[fi]);
                }
            }
        }
    };

    // ============================================================
    // 系统托盘（壁纸模式下启用）
    // ============================================================
    std::atomic<bool> paused{false};
    bool running = true;
    std::string trayShaderSwitchRequest;  // 托盘菜单 shader 切换请求
    bool trayDebugToggleRequest = false;  // 托盘菜单 debug 切换请求
    Uint32 trayTooltipTimer = 0;          // tooltip 更新计时
    float lastRenderElapsed = 0.0f;       // 最近一帧渲染耗时（秒），用于 tooltip
    TrayIcon tray;
    if (config.wallpaperMode) {
        TrayIcon::MenuCallbacks cb;
        cb.onPause  = [&]() { paused = true;  std::cout << "Paused." << std::endl; };
        cb.onResume = [&]() { paused = false; std::cout << "Resumed." << std::endl; };
        cb.onReload = [&]() { shaderNeedsReload = true; std::cout << "Reload requested." << std::endl; };
        cb.onQuit   = [&]() { running = false; };
        cb.onSwitchShader = [&](const std::string& path) {
            trayShaderSwitchRequest = path;
            std::cout << "Tray: switch shader to " << path << std::endl;
        };
        cb.onToggleDebug = [&]() { trayDebugToggleRequest = true; };
        cb.onBrowseShader = [&]() {
            std::string path = BrowseAndValidateShader();
            if (!path.empty()) {
                trayShaderSwitchRequest = path;
            }
        };
        tray.Create(window, cb);
        tray.SetDebugState(config.showDebug);

        // 扫描 shader 文件列表并传给 tray（复用 ScanShaderFiles 扫描到 debugState，再传给 tray）
        ScanShaderFiles();
        tray.SetShaderList(debugState.glslFiles, debugState.jsonFiles,
                           debugState.dirFiles, config.shaderPath);
    }

    // ============================================================
    // 打印运行模式
    // ============================================================
    if (config.wallpaperMode) {
        std::cout << "Running in wallpaper mode." << std::endl;
    } else {
        std::cout << "Running in window mode. Press ESC to exit, F5 to reload shader, Tab to toggle debug panel." << std::endl;
    }

    // ============================================================
    // 调试 UI 初始化（窗口模式始终初始化，壁纸模式仅 --debug 时初始化）
    // ============================================================
    if (!config.wallpaperMode) {
#ifdef _WIN32
        if (useD3D11 && d3dRenderer) {
            if (!debugUI.InitD3D11(window, d3dRenderer->GetDevice(), d3dRenderer->GetContext())) {
                std::cerr << "DebugUI D3D11 init failed, continuing without debug panel." << std::endl;
            }
        } else
#endif
        {
            if (!debugUI.Init(window, glContext)) {
                std::cerr << "DebugUI init failed, continuing without debug panel." << std::endl;
            }
        }
        ScanShaderFiles();
        debugState.targetFPS = config.targetFPS;
        debugState.renderScale = config.renderScale;
    } else if (config.showDebug) {
        // 壁纸模式 + --debug：初始化 ImGui 用于只读叠加显示
        if (!debugUI.Init(wallpaperWindows[0].window, glContext)) {
            std::cerr << "DebugUI init failed (wallpaper mode), continuing without debug overlay." << std::endl;
            config.showDebug = false;
        }
    }

    // ============================================================
    // 桌面遮挡检测辅助函数（壁纸模式用）
    // ============================================================
#ifdef _WIN32
    // 检查指定显示器区域是否被单个窗口几乎完全覆盖（如最大化窗口）
    // 策略：只要有一个非桌面、非隐形窗口覆盖了该显示器 >= 90% 面积，就视为遮挡
    auto IsMonitorOccluded = [](int monX, int monY, int monW, int monH) -> bool {
        RECT monRect = {monX, monY, monX + monW, monY + monH};
        long monArea = (long)monW * monH;
        if (monArea <= 0) return false;

        HWND hwnd = GetTopWindow(nullptr);
        while (hwnd) {
            if (!IsWindowVisible(hwnd)) {
                hwnd = GetNextWindow(hwnd, GW_HWNDNEXT);
                continue;
            }

            // 跳过 DWM "cloaked" 窗口（Win10/11 UWP 隐形窗口，IsWindowVisible 返回 true 但实际不可见）
            DWORD cloaked = 0;
            if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked != 0) {
                hwnd = GetNextWindow(hwnd, GW_HWNDNEXT);
                continue;
            }

            // 跳过桌面相关窗口
            wchar_t className[256] = {};
            GetClassNameW(hwnd, className, 256);
            if (wcscmp(className, L"Progman") == 0 || wcscmp(className, L"WorkerW") == 0 ||
                wcscmp(className, L"Shell_TrayWnd") == 0 || wcscmp(className, L"Shell_SecondaryTrayWnd") == 0) {
                hwnd = GetNextWindow(hwnd, GW_HWNDNEXT);
                continue;
            }

            // 跳过已知的系统级透明/辅助窗口类
            if (wcscmp(className, L"Windows.UI.Core.CoreWindow") == 0 ||
                wcscmp(className, L"ApplicationFrameInputSinkWindow") == 0 ||
                wcscmp(className, L"EdgeUiInputTopWndClass") == 0 ||
                wcscmp(className, L"EdgeUiInputWndClass") == 0 ||
                wcscmp(className, L"ForegroundStaging") == 0 ||
                wcscmp(className, L"Shell_InputSwitchTopLevelWindow") == 0) {
                hwnd = GetNextWindow(hwnd, GW_HWNDNEXT);
                continue;
            }

            // 跳过最小化窗口
            if (IsIconic(hwnd)) {
                hwnd = GetNextWindow(hwnd, GW_HWNDNEXT);
                continue;
            }

            // 跳过无主工具窗口（WS_EX_TOOLWINDOW 且无所有者）——通常是系统辅助窗口
            LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
            if ((exStyle & WS_EX_TOOLWINDOW) && GetWindow(hwnd, GW_OWNER) == nullptr) {
                hwnd = GetNextWindow(hwnd, GW_HWNDNEXT);
                continue;
            }

            // 跳过零面积窗口
            RECT wndRect;
            if (!GetWindowRect(hwnd, &wndRect)) {
                hwnd = GetNextWindow(hwnd, GW_HWNDNEXT);
                continue;
            }
            if (wndRect.right <= wndRect.left || wndRect.bottom <= wndRect.top) {
                hwnd = GetNextWindow(hwnd, GW_HWNDNEXT);
                continue;
            }

            // 计算单个窗口与显示器的交集面积
            RECT inter;
            if (IntersectRect(&inter, &wndRect, &monRect)) {
                long long iw = inter.right - inter.left;
                long long ih = inter.bottom - inter.top;
                if (iw > 0 && ih > 0) {
                    long long singleCover = iw * ih;
                    if (singleCover >= monArea * 90 / 100) {
                        return true;
                    }
                }
            }

            hwnd = GetNextWindow(hwnd, GW_HWNDNEXT);
        }

        return false;
    };

    auto IsFullscreenAppRunning = []() -> bool {
        HWND fg = GetForegroundWindow();
        if (!fg) return false;

        HWND desktop = GetDesktopWindow();
        HWND shell = GetShellWindow();
        if (fg == desktop || fg == shell) return false;

        wchar_t className[64] = {};
        GetClassNameW(fg, className, 64);
        if (wcscmp(className, L"Progman") == 0 || wcscmp(className, L"WorkerW") == 0)
            return false;

        RECT wndRect;
        GetWindowRect(fg, &wndRect);
        int scrW = GetSystemMetrics(SM_CXSCREEN);
        int scrH = GetSystemMetrics(SM_CYSCREEN);
        return (wndRect.left <= 0 && wndRect.top <= 0 &&
                wndRect.right >= scrW && wndRect.bottom >= scrH);
    };
#endif

    // ============================================================
    // 主循环
    // ============================================================
    Uint64 startTime = SDL_GetPerformanceCounter();
    Uint64 freq = SDL_GetPerformanceFrequency();
    float lastFrameTime = 0.0f;
    int frameCount = 0;
    Uint32 fullscreenCheckTimer = 0;  // 全屏检测计时
    bool autoPaused = false;          // 因全屏应用而自动暂停

    // FPS 统计（滑动帧计数法：累计 N 帧后更新，低帧率时也稳定）
    float measuredFPS = 0.0f;
    int fpsFrameCount = 0;
    Uint64 fpsLastTime = SDL_GetPerformanceCounter();

    // 帧率自适应
    float adaptiveFPS = static_cast<float>(config.targetFPS);
    float frameTimeAccum = 0.0f;
    int frameTimeCount = 0;

    // iMouse: xy=当前鼠标位置, zw=按下瞬间的位置（松开后取负值）
    float mouse[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    bool mousePressed = false;
    float clickTime = -10.0f;  // 最近一次点击的 iTime，初始设为远过去

    // 统一填充 debugState 的公共字段
    auto fillDebugState = [&](float fps, float time, float td, float rt,
                              float resW, float resH, const float* m) {
        debugState.fps         = fps;
        debugState.adaptiveFPS = adaptiveFPS;
        debugState.currentTime = time;
        debugState.timeDelta   = td;
        debugState.renderTime  = rt;
        debugState.frameCount  = frameCount;
        debugState.resolution[0] = resW;
        debugState.resolution[1] = resH;
        std::copy(m, m + 4, std::begin(debugState.mouse));
        debugState.shaderPath  = config.shaderPath;
        debugState.shaderError = lastShaderError;
        debugState.paused      = paused.load();
#ifdef _WIN32
        if (useD3D11 && d3dMultiPass) {
            debugState.isMultiPass = d3dMultiPass->IsMultiPass();
            debugState.passNames   = d3dMultiPass->GetPassNames();
        } else
#endif
        {
            debugState.isMultiPass = multiPass.IsMultiPass();
            debugState.passNames   = multiPass.GetPassNames();
        }
    };

    while (running) {
        // 事件处理
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            tray.HandleEvent(event);

            // ImGui 事件转发（始终转发，让 ImGui 跟踪状态）
            if (!config.wallpaperMode) {
                debugUI.ProcessEvent(event);
            }

            switch (event.type) {
            case SDL_QUIT:
                running = false;
                break;
            case SDL_KEYDOWN:
                if (event.key.keysym.sym == SDLK_TAB && !config.wallpaperMode) {
                    debugUI.Toggle();
                } else if (event.key.keysym.sym == SDLK_ESCAPE) {
                    running = false;
                } else if (event.key.keysym.sym == SDLK_F5) {
                    shaderNeedsReload = true;
                }
                break;
            case SDL_WINDOWEVENT:
                if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
                    config.width = event.window.data1;
                    config.height = event.window.data2;
#ifdef _WIN32
                    if (useD3D11 && !config.wallpaperMode && d3dRenderer) {
                        // D3D11 窗口模式 resize
                        d3dRenderer->ResizeSwapChain(0, config.width, config.height);
                        if (d3dWindowBlit) {
                            d3dWindowBlit->CreateRenderTarget(config.width, config.height, config.renderScale);
                        }
                        if (d3dMultiPass) {
                            int rW = d3dWindowBlit ? d3dWindowBlit->GetRenderWidth() : config.width;
                            int rH = d3dWindowBlit ? d3dWindowBlit->GetRenderHeight() : config.height;
                            d3dMultiPass->Resize(rW, rH);
                        }
                    } else
#endif
                    {
                        renderer.SetViewport(config.width, config.height);
                        if (useScaledRender) {
                            blitRenderer.CreateRenderFBO(config.width, config.height, config.renderScale);
                            multiPass.Resize(blitRenderer.GetRenderWidth(), blitRenderer.GetRenderHeight());
                            multiPass.SetImageTargetFBO(blitRenderer.GetRenderFBO());
                        } else {
                            multiPass.Resize(config.width, config.height);
                        }
                    }
                }
                break;
            case SDL_MOUSEMOTION:
                // ImGui 想捕获鼠标时，不更新 shader 的 iMouse
                if (!config.wallpaperMode && debugUI.WantCaptureMouse()) break;
                mouse[0] = static_cast<float>(event.motion.x);
                mouse[1] = static_cast<float>(config.height - event.motion.y);
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (!config.wallpaperMode && debugUI.WantCaptureMouse()) break;
                if (event.button.button == SDL_BUTTON_LEFT) {
                    mousePressed = true;
                    mouse[2] = mouse[0];
                    mouse[3] = mouse[1];
                    // 记录点击时间
                    Uint64 clickNow = SDL_GetPerformanceCounter();
                    clickTime = static_cast<float>(clickNow - startTime) / static_cast<float>(freq);
                }
                break;
            case SDL_MOUSEBUTTONUP:
                if (!config.wallpaperMode && debugUI.WantCaptureMouse()) break;
                if (event.button.button == SDL_BUTTON_LEFT) {
                    mousePressed = false;
                    mouse[2] = -mouse[2];
                    mouse[3] = -mouse[3];
                }
                break;
            }
        }

        // 壁纸模式：SDL 收不到鼠标事件，改用 Win32 全局鼠标状态
        // 存储原始屏幕坐标，渲染时再转各窗口局部坐标
#ifdef _WIN32
        if (config.wallpaperMode) {
            POINT pt;
            GetCursorPos(&pt);
            mouse[0] = static_cast<float>(pt.x);
            mouse[1] = static_cast<float>(pt.y);

            bool leftDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
            if (leftDown && !mousePressed) {
                mousePressed = true;
                mouse[2] = mouse[0];
                mouse[3] = mouse[1];
                Uint64 clickNow = SDL_GetPerformanceCounter();
                float clickTimeNow = static_cast<float>(clickNow - startTime) / static_cast<float>(freq);
                clickTime = clickTimeNow;  // 全局（Buffer pass 用）
                // 按显示器归属更新 per-monitor clickTime 和点击局部坐标
                for (auto& ww : wallpaperWindows) {
                    if (pt.x >= ww.x && pt.x < ww.x + ww.width &&
                        pt.y >= ww.y && pt.y < ww.y + ww.height) {
                        ww.clickTime = clickTimeNow;
                        ww.clickLocalX = static_cast<float>(pt.x - ww.x);
                        ww.clickLocalY = static_cast<float>(ww.height) - static_cast<float>(pt.y - ww.y);
                        ww.clickActive = true;
                    }
                }
            } else if (!leftDown && mousePressed) {
                // 刚松开 — 标记所有活跃显示器的点击为释放状态
                mousePressed = false;
                mouse[2] = -mouse[2];
                mouse[3] = -mouse[3];
                for (auto& ww : wallpaperWindows) {
                    if (ww.clickActive) {
                        ww.clickActive = false;
                    }
                }
            }
        }
#endif

        // 处理 DebugUI 控制请求
        if (!config.wallpaperMode) {
            // 浏览 shader 文件/文件夹请求
            if (debugState.requestBrowseShader) {
                debugState.requestBrowseShader = false;
                std::string path = BrowseAndValidateShader();
                if (!path.empty()) {
                    debugState.requestSwitchShader = path;
                }
            }
            // shader 切换请求
            if (!debugState.requestSwitchShader.empty()) {
                config.shaderPath = debugState.requestSwitchShader;
                debugState.requestSwitchShader.clear();
                shaderNeedsReload = true;
                RebuildFileWatcher();
            }
            // 重载请求
            if (debugState.requestReload) {
                shaderNeedsReload = true;
                debugState.requestReload = false;
            }
            // 时间重置请求
            if (debugState.requestResetTime) {
                startTime = SDL_GetPerformanceCounter();
                frameCount = 0;
                lastFrameTime = 0.0f;
                debugState.requestResetTime = false;
            }
            // 暂停状态同步（DebugUI → paused）
            paused = debugState.paused;
            // FPS 调节
            if (debugState.targetFPS != config.targetFPS) {
                config.targetFPS = debugState.targetFPS;
                adaptiveFPS = static_cast<float>(config.targetFPS);
                frameTimeAccum = 0.0f;
                frameTimeCount = 0;
            }
            // renderScale 调节
            if (debugState.renderScale != config.renderScale) {
                config.renderScale = debugState.renderScale;
                bool newScaled = (config.renderScale < 1.0f);
                if (newScaled && !useScaledRender) {
                    useScaledRender = true;
                    if (config.wallpaperMode) {
                        for (auto& ww : wallpaperWindows) {
                            if (!ww.blit) {
                                ww.blit = std::make_unique<BlitRenderer>();
                                ww.blit->Init();
                            }
                        }
#ifdef _WIN32
                    } else if (useD3D11 && d3dRenderer) {
                        if (!d3dWindowBlit) {
                            d3dWindowBlit = std::make_unique<D3D11BlitRenderer>();
                            d3dWindowBlit->Init(d3dRenderer->GetDevice(), d3dRenderer->GetContext());
                        }
#endif
                    } else if (!blitRenderer.IsInitialized()) {
                        blitRenderer.Init();
                    }
                } else if (!newScaled) {
                    useScaledRender = false;
                    if (config.wallpaperMode) {
                        for (auto& ww : wallpaperWindows) {
                            ww.blit.reset();
                        }
#ifdef _WIN32
                    } else if (useD3D11) {
                        d3dWindowBlit.reset();
#endif
                    }
                }
                if (useScaledRender) {
                    if (config.wallpaperMode) {
                        for (auto& ww : wallpaperWindows) {
                            if (ww.blit) {
                                ww.blit->CreateRenderFBO(ww.width, ww.height, config.renderScale);
                            }
                        }
                        int bufW = static_cast<int>(config.width * config.renderScale);
                        int bufH = static_cast<int>(config.height * config.renderScale);
                        if (bufW < 1) bufW = 1;
                        if (bufH < 1) bufH = 1;
                        multiPass.Resize(bufW, bufH);
#ifdef _WIN32
                    } else if (useD3D11 && d3dWindowBlit && d3dMultiPass) {
                        d3dWindowBlit->CreateRenderTarget(config.width, config.height, config.renderScale);
                        d3dMultiPass->Resize(d3dWindowBlit->GetRenderWidth(), d3dWindowBlit->GetRenderHeight());
#endif
                    } else {
                        blitRenderer.CreateRenderFBO(config.width, config.height, config.renderScale);
                        multiPass.SetImageTargetFBO(blitRenderer.GetRenderFBO());
                        multiPass.Resize(blitRenderer.GetRenderWidth(), blitRenderer.GetRenderHeight());
                    }
                } else {
#ifdef _WIN32
                    if (useD3D11 && d3dMultiPass) {
                        d3dMultiPass->Resize(config.width, config.height);
                    } else
#endif
                    {
                        multiPass.SetImageTargetFBO(0);
                        multiPass.Resize(config.width, config.height);
                    }
                }
            }
        }

        // 壁纸模式：处理托盘菜单 shader 切换请求
        if (config.wallpaperMode && !trayShaderSwitchRequest.empty()) {
            config.shaderPath = trayShaderSwitchRequest;
            trayShaderSwitchRequest.clear();
            shaderNeedsReload = true;
            RebuildFileWatcher();
        }

        // 壁纸模式：处理托盘菜单 debug overlay 切换
        if (config.wallpaperMode && trayDebugToggleRequest) {
            trayDebugToggleRequest = false;
            config.showDebug = !config.showDebug;
            // 首次开启时延迟初始化 ImGui
            if (config.showDebug && !debugUI.IsInitialized()) {
                if (!debugUI.Init(wallpaperWindows[0].window, glContext)) {
                    std::cerr << "DebugUI init failed, disabling debug overlay." << std::endl;
                    config.showDebug = false;
                }
            }
            tray.SetDebugState(config.showDebug);
            std::cout << "Debug overlay: " << (config.showDebug ? "ON" : "OFF") << std::endl;
        }

        // 热加载：重新加载整个 shader 项目并重建渲染器
        if (shaderNeedsReload.exchange(false)) {
            ShaderProject newProject;
            if (newProject.Load(config.shaderPath)) {
                project = std::move(newProject);
#ifdef _WIN32
                if (useD3D11) {
                    // D3D11 路径热加载
                    if (d3dTextures) d3dTextures->Clear();
                    // TODO: 重新加载 D3D11 纹理（类似 LoadTexturesForProject）
                    if (SetupD3D11MultiPass(project.GetData())) {
                        lastShaderError.clear();
                        std::cout << "D3D11 Shader reloaded successfully." << std::endl;
                        dumpHlsl();
                        if (config.wallpaperMode) {
                            tray.SetShaderList(debugState.glslFiles, debugState.jsonFiles,
                                               debugState.dirFiles, config.shaderPath);
                        }
                    } else {
                        lastShaderError = d3dMultiPass ? d3dMultiPass->GetLastError() : "D3D11 setup failed";
                        std::cerr << "D3D11 Shader reload failed: " << lastShaderError << std::endl;
                        dumpHlsl(); // 编译失败也输出 HLSL，方便调试
                    }
                } else
#endif
                {
                    // OpenGL 路径热加载
                    LoadTexturesForProject(project.GetData());
                    if (SetupMultiPass(project.GetData())) {
                        lastShaderError.clear();
                        std::cout << "Shader reloaded successfully." << std::endl;
                        dumpHlsl();
                        if (config.wallpaperMode) {
                            tray.SetShaderList(debugState.glslFiles, debugState.jsonFiles,
                                               debugState.dirFiles, config.shaderPath);
                        }
                    } else {
                        lastShaderError = multiPass.GetLastError();
                        std::cerr << "Shader reload failed: " << lastShaderError << std::endl;
                    }
                }
            } else {
                lastShaderError = newProject.GetLastError();
                std::cerr << "Shader reload failed: " << lastShaderError << std::endl;
            }
        }

        // 暂停时跳过 shader 渲染，但仍渲染 DebugUI
        if (paused) {
            if (!config.wallpaperMode) {
                fillDebugState(0.0f, lastFrameTime, 0.0f, 0.0f,
                               static_cast<float>(config.width),
                               static_cast<float>(config.height), mouse);

#ifdef _WIN32
                if (useD3D11 && d3dRenderer) {
                    d3dRenderer->BeginFrame(0);
                    debugUI.SetD3D11RenderTarget(d3dRenderer->GetBackBufferRTV(0));
                    debugUI.BeginFrame();
                    debugUI.Render(debugState);
                    d3dRenderer->Present(0, 0);
                } else
#endif
                {
                    debugUI.BeginFrame();
                    debugUI.Render(debugState);
                    SDL_GL_SwapWindow(window);
                }
            } else {
                if (config.showDebug) {
                    for (auto& ww : wallpaperWindows) {
                        SDL_GL_MakeCurrent(ww.window, glContext);
                        fillDebugState(0.0f, lastFrameTime, 0.0f, 0.0f,
                                       static_cast<float>(ww.width),
                                       static_cast<float>(ww.height), mouse);

                        debugUI.BeginFrame(ww.width, ww.height);
                        debugUI.RenderOverlay(debugState);
                        SDL_GL_SwapWindow(ww.window);
                    }
                }
                // 暂停时也更新 tooltip（显示 Paused 状态）
                Uint32 tooltipNow = SDL_GetTicks();
                if (tooltipNow - trayTooltipTimer > 1000) {
                    trayTooltipTimer = tooltipNow;
                    std::string shaderDisplayName = config.shaderPath;
                    auto slashPos = shaderDisplayName.find_last_of("/\\");
                    if (slashPos != std::string::npos) {
                        shaderDisplayName = shaderDisplayName.substr(slashPos + 1);
                    }
                    tray.UpdateTooltip(0.0f, 0.0f, shaderDisplayName + " [Paused]",
                                       config.monitorIndex);
                }
                SDL_Delay(100);
            }
            continue;
        }

        // 壁纸模式：每秒检测一次全屏应用和显示器遮挡
#ifdef _WIN32
        if (config.wallpaperMode && config.pauseOnFullscreen) {
            Uint32 now_ms = SDL_GetTicks();
            if (now_ms - fullscreenCheckTimer > 1000) {
                fullscreenCheckTimer = now_ms;
                bool fullscreen = IsFullscreenAppRunning();
                if (fullscreen && !autoPaused) {
                    autoPaused = true;
                    std::cout << "Fullscreen app detected, auto-pausing." << std::endl;
                } else if (!fullscreen && autoPaused) {
                    autoPaused = false;
                    std::cout << "Fullscreen app closed, resuming." << std::endl;
                }

                // 按显示器粒度遮挡检测
                for (size_t i = 0; i < wallpaperWindows.size(); ++i) {
                    auto& ww = wallpaperWindows[i];
                    bool wasOccluded = ww.occluded;
                    ww.occluded = IsMonitorOccluded(ww.x, ww.y, ww.width, ww.height);
                    if (ww.occluded != wasOccluded) {
                        std::cout << "Monitor " << i << " (" << ww.width << "x" << ww.height
                                  << ") occluded: " << (ww.occluded ? "yes" : "no") << std::endl;
                    }
                }
            }
            if (autoPaused) {
                SDL_Delay(500);
                continue;
            }
        }
#endif

        // 时间计算
        Uint64 now = SDL_GetPerformanceCounter();
        float currentTime = static_cast<float>(now - startTime) / static_cast<float>(freq);
        float timeDelta = currentTime - lastFrameTime;
        lastFrameTime = currentTime;

        // FPS 统计：累计足够帧数后更新（高帧率约1秒更新，低帧率每3帧更新）
        fpsFrameCount++;
        float fpsElapsed = static_cast<float>(now - fpsLastTime) / static_cast<float>(freq);
        int fpsUpdateInterval = std::max(3, static_cast<int>(adaptiveFPS));
        if (fpsFrameCount >= fpsUpdateInterval) {
            measuredFPS = static_cast<float>(fpsFrameCount) / fpsElapsed;
            fpsFrameCount = 0;
            fpsLastTime = now;
        }

        // iDate: 年/月/日/当天已过秒数
        time_t rawTime = std::time(nullptr);
        struct tm localTm;
        localtime_s(&localTm, &rawTime);
        float date[4] = {
            static_cast<float>(localTm.tm_year + 1900),
            static_cast<float>(localTm.tm_mon),
            static_cast<float>(localTm.tm_mday),
            static_cast<float>(localTm.tm_hour * 3600 + localTm.tm_min * 60 + localTm.tm_sec)
        };

        // 获取全屏四边形 VAO（从 Renderer 借用）
        GLuint quadVAO = renderer.GetQuadVAO();

        // 渲染
        if (config.wallpaperMode && !wallpaperWindows.empty()) {
            // 检查是否所有显示器都被遮挡
            bool allOccluded = config.pauseOnFullscreen &&
                std::all_of(wallpaperWindows.begin(), wallpaperWindows.end(),
                            [](const WallpaperWindow& ww) { return ww.occluded; });
            if (allOccluded) {
                SDL_Delay(100);
                continue;
            }

#ifdef _WIN32
            if (useD3D11 && d3dRenderer && d3dMultiPass) {
                // ============ D3D11 壁纸渲染路径 ============
                Uint64 renderStart = SDL_GetPerformanceCounter();
                auto* ctx = d3dRenderer->GetContext();

                int bufferW = useScaledRender
                    ? std::max(1, static_cast<int>(config.width * config.renderScale))
                    : config.width;
                int bufferH = useScaledRender
                    ? std::max(1, static_cast<int>(config.height * config.renderScale))
                    : config.height;

                float bufferMouse[4] = {mouse[0], mouse[1], mouse[2], mouse[3]};
                if (useScaledRender) {
                    for (int i = 0; i < 4; ++i) bufferMouse[i] *= config.renderScale;
                }

                // 设置全屏三角形 VS（所有 pass 共享）
                ctx->VSSetShader(d3dRenderer->GetFullscreenVS(), nullptr, 0);

                d3dMultiPass->RenderBufferPasses(ctx, currentTime, timeDelta, frameCount,
                                                  bufferMouse, date, bufferW, bufferH, clickTime);

                Uint64 bufferEnd = SDL_GetPerformanceCounter();
                float bufferTime = static_cast<float>(bufferEnd - renderStart) / static_cast<float>(freq);

                for (auto& ww : wallpaperWindows) {
                    if (ww.occluded && config.pauseOnFullscreen) continue;

                    // 鼠标坐标转局部
                    float localMouse[4];
                    float localX = mouse[0] - static_cast<float>(ww.x);
                    float localY = static_cast<float>(ww.height) - (mouse[1] - static_cast<float>(ww.y));
                    bool inMon = (localX >= 0 && localX < ww.width && localY >= 0 && localY < ww.height);
                    localMouse[0] = inMon ? localX : -1.0f;
                    localMouse[1] = inMon ? localY : -1.0f;
                    // 使用 per-monitor 记录的点击局部坐标（不受其他显示器点击影响）
                    localMouse[2] = ww.clickActive ? ww.clickLocalX : -ww.clickLocalX;
                    localMouse[3] = ww.clickActive ? ww.clickLocalY : -ww.clickLocalY;

                    Uint64 imageStart = SDL_GetPerformanceCounter();

                    ctx->VSSetShader(d3dRenderer->GetFullscreenVS(), nullptr, 0);

                    if (useScaledRender && ww.d3dBlit && ww.d3dBlit->IsInitialized()) {
                        int rW = std::max(1, static_cast<int>(ww.width * config.renderScale));
                        int rH = std::max(1, static_cast<int>(ww.height * config.renderScale));
                        float sMouse[4] = { localMouse[0]*config.renderScale, localMouse[1]*config.renderScale,
                                            localMouse[2]*config.renderScale, localMouse[3]*config.renderScale };
                        d3dMultiPass->SetImageTargetRTV(ww.d3dBlit->GetRenderRTV(), rW, rH);
                        d3dMultiPass->RenderImagePass(ctx, currentTime, timeDelta, frameCount,
                                                       sMouse, date, rW, rH, ww.clickTime);
                        d3dRenderer->BeginFrame(ww.d3dSwapChainIndex);
                        ctx->VSSetShader(d3dRenderer->GetFullscreenVS(), nullptr, 0);
                        ww.d3dBlit->BlitToTarget(d3dRenderer->GetBackBufferRTV(ww.d3dSwapChainIndex),
                                                  ww.width, ww.height);
                    } else {
                        d3dMultiPass->SetImageTargetRTV(nullptr);
                        d3dRenderer->BeginFrame(ww.d3dSwapChainIndex);
                        ctx->VSSetShader(d3dRenderer->GetFullscreenVS(), nullptr, 0);
                        d3dMultiPass->RenderImagePass(ctx, currentTime, timeDelta, frameCount,
                                                       localMouse, date, ww.width, ww.height, ww.clickTime);
                    }

                    d3dRenderer->Present(ww.d3dSwapChainIndex, 0);

                    Uint64 imageEnd = SDL_GetPerformanceCounter();
                    float imageTime = static_cast<float>(imageEnd - imageStart) / static_cast<float>(freq);
                    lastRenderElapsed = bufferTime + imageTime;
                }

                // 托盘 tooltip 更新
                Uint32 tooltipNow = SDL_GetTicks();
                if (tooltipNow - trayTooltipTimer > 1000) {
                    trayTooltipTimer = tooltipNow;
                    std::string shaderDisplayName = config.shaderPath;
                    auto slashPos = shaderDisplayName.find_last_of("/\\");
                    if (slashPos != std::string::npos) shaderDisplayName = shaderDisplayName.substr(slashPos + 1);
                    tray.UpdateTooltip(measuredFPS, lastRenderElapsed * 1000.0f, shaderDisplayName, config.monitorIndex);
                }
            } else
#endif
            {
                // ============ OpenGL 壁纸渲染路径（原有逻辑）============
                SDL_GL_MakeCurrent(wallpaperWindows[0].window, glContext);

            Uint64 renderStart = SDL_GetPerformanceCounter();  // 计时包含 Buffer + Image pass

            int bufferW, bufferH;
            if (useScaledRender) {
                bufferW = static_cast<int>(config.width * config.renderScale);
                bufferH = static_cast<int>(config.height * config.renderScale);
                if (bufferW < 1) bufferW = 1;
                if (bufferH < 1) bufferH = 1;
            } else {
                bufferW = config.width;
                bufferH = config.height;
            }

            // 使用第一个显示器的鼠标局部坐标传给 Buffer pass（Buffer 内容与显示器无关，鼠标影响不大）
            float bufferMouse[4] = {mouse[0], mouse[1], mouse[2], mouse[3]};
            if (useScaledRender) {
                bufferMouse[0] *= config.renderScale;
                bufferMouse[1] *= config.renderScale;
                bufferMouse[2] *= config.renderScale;
                bufferMouse[3] *= config.renderScale;
            }

            multiPass.RenderBufferPasses(quadVAO, currentTime, timeDelta, frameCount,
                                         bufferMouse, date, bufferW, bufferH, clickTime);
            glFinish();

            Uint64 bufferEnd = SDL_GetPerformanceCounter();
            float bufferTime = static_cast<float>(bufferEnd - renderStart) / static_cast<float>(freq);

            // 每个显示器各渲染 Image pass + blit + debug + swap
            for (auto& ww : wallpaperWindows) {
                // 被遮挡的显示器跳过渲染
                if (ww.occluded && config.pauseOnFullscreen) {
                    continue;
                }

                SDL_GL_MakeCurrent(ww.window, glContext);

                // 将全局屏幕坐标转为当前窗口的局部坐标（ShaderToy Y 从底部开始）
                float localMouse[4];
                float localX = mouse[0] - static_cast<float>(ww.x);
                float localY = static_cast<float>(ww.height) - (mouse[1] - static_cast<float>(ww.y));
                bool inThisMonitor = (localX >= 0 && localX < ww.width &&
                                      localY >= 0 && localY < ww.height);

                localMouse[0] = inThisMonitor ? localX : -1.0f;
                localMouse[1] = inThisMonitor ? localY : -1.0f;

                // 使用 per-monitor 记录的点击局部坐标（不受其他显示器点击影响）
                localMouse[2] = ww.clickActive ? ww.clickLocalX : -ww.clickLocalX;
                localMouse[3] = ww.clickActive ? ww.clickLocalY : -ww.clickLocalY;

                Uint64 imageStart = SDL_GetPerformanceCounter();

                if (useScaledRender && ww.blit) {
                    int curRenderW = static_cast<int>(ww.width * config.renderScale);
                    int curRenderH = static_cast<int>(ww.height * config.renderScale);
                    if (curRenderW < 1) curRenderW = 1;
                    if (curRenderH < 1) curRenderH = 1;

                    float scaledMouse[4] = {
                        localMouse[0] * config.renderScale,
                        localMouse[1] * config.renderScale,
                        localMouse[2] * config.renderScale,
                        localMouse[3] * config.renderScale
                    };

                    multiPass.SetImageTargetFBO(ww.blit->GetRenderFBO());
                    multiPass.RenderImagePass(quadVAO, currentTime, timeDelta, frameCount,
                                             scaledMouse, date, curRenderW, curRenderH, ww.clickTime);

                    ww.blit->BlitToScreen(ww.width, ww.height);
                } else {
                    multiPass.SetImageTargetFBO(0);
                    multiPass.RenderImagePass(quadVAO, currentTime, timeDelta, frameCount,
                                             localMouse, date, ww.width, ww.height, ww.clickTime);
                }

                glFinish();
                Uint64 imageEnd = SDL_GetPerformanceCounter();
                float imageTime = static_cast<float>(imageEnd - imageStart) / static_cast<float>(freq);

                // renderTime = Buffer pass 共享时间 + 当前显示器 Image pass 时间
                float renderElapsed = bufferTime + imageTime;
                lastRenderElapsed = renderElapsed;

                if (config.showDebug) {
                    fillDebugState(measuredFPS, currentTime, timeDelta, renderElapsed,
                                   static_cast<float>(ww.width),
                                   static_cast<float>(ww.height), localMouse);

                    debugUI.BeginFrame(ww.width, ww.height);
                    debugUI.RenderOverlay(debugState);
                }

                SDL_GL_SwapWindow(ww.window);
            }

            // 每秒更新一次托盘 tooltip
            Uint32 tooltipNow = SDL_GetTicks();
            if (tooltipNow - trayTooltipTimer > 1000) {
                trayTooltipTimer = tooltipNow;
                // 从 shaderPath 提取显示名
                std::string shaderDisplayName = config.shaderPath;
                auto slashPos = shaderDisplayName.find_last_of("/\\");
                if (slashPos != std::string::npos) {
                    shaderDisplayName = shaderDisplayName.substr(slashPos + 1);
                }
                tray.UpdateTooltip(measuredFPS, lastRenderElapsed * 1000.0f,
                                   shaderDisplayName, config.monitorIndex);
            }
            } // end OpenGL wallpaper else block
        } else {
            // 窗口模式
            Uint64 renderStart = SDL_GetPerformanceCounter();

#ifdef _WIN32
            if (useD3D11 && d3dRenderer && d3dMultiPass) {
                // ============ D3D11 窗口模式渲染路径 ============
                auto* ctx = d3dRenderer->GetContext();
                bool useScaledD3D = (d3dWindowBlit && d3dWindowBlit->IsInitialized());

                ctx->VSSetShader(d3dRenderer->GetFullscreenVS(), nullptr, 0);

                if (useScaledD3D) {
                    int rW = d3dWindowBlit->GetRenderWidth();
                    int rH = d3dWindowBlit->GetRenderHeight();
                    float sMouse[4] = { mouse[0]*config.renderScale, mouse[1]*config.renderScale,
                                        mouse[2]*config.renderScale, mouse[3]*config.renderScale };
                    d3dMultiPass->SetImageTargetRTV(d3dWindowBlit->GetRenderRTV(), rW, rH);
                    d3dMultiPass->RenderAllPasses(ctx, currentTime, timeDelta, frameCount,
                                                   sMouse, date, rW, rH, clickTime);
                    d3dRenderer->BeginFrame(0);
                    ctx->VSSetShader(d3dRenderer->GetFullscreenVS(), nullptr, 0);
                    d3dWindowBlit->BlitToTarget(d3dRenderer->GetBackBufferRTV(0),
                                                 config.width, config.height);
                } else {
                    d3dMultiPass->SetImageTargetRTV(nullptr);
                    d3dRenderer->BeginFrame(0);
                    ctx->VSSetShader(d3dRenderer->GetFullscreenVS(), nullptr, 0);
                    d3dMultiPass->RenderAllPasses(ctx, currentTime, timeDelta, frameCount,
                                                   mouse, date, config.width, config.height, clickTime);
                }

                // DebugUI 渲染
                {
                    Uint64 renderEnd = SDL_GetPerformanceCounter();
                    float renderElapsed = static_cast<float>(renderEnd - renderStart) / static_cast<float>(freq);

                    fillDebugState(measuredFPS, currentTime, timeDelta, renderElapsed,
                                   static_cast<float>(config.width),
                                   static_cast<float>(config.height), mouse);

                    debugUI.SetD3D11RenderTarget(d3dRenderer->GetBackBufferRTV(0));
                    debugUI.BeginFrame();
                    debugUI.Render(debugState);
                }

                d3dRenderer->Present(0, 0);
            } else
#endif
            {
                // ============ OpenGL 窗口模式渲染路径 ============
                if (useScaledRender) {
                    int scaledW = static_cast<int>(config.width * config.renderScale);
                    int scaledH = static_cast<int>(config.height * config.renderScale);
                    if (scaledW != blitRenderer.GetRenderWidth() || scaledH != blitRenderer.GetRenderHeight()) {
                        blitRenderer.CreateRenderFBO(config.width, config.height, config.renderScale);
                        multiPass.Resize(blitRenderer.GetRenderWidth(), blitRenderer.GetRenderHeight());
                    }

                    multiPass.SetImageTargetFBO(blitRenderer.GetRenderFBO());
                    multiPass.RenderAllPasses(quadVAO, currentTime, timeDelta, frameCount,
                                             mouse, date, blitRenderer.GetRenderWidth(),
                                             blitRenderer.GetRenderHeight(), clickTime);

                    blitRenderer.BlitToScreen(config.width, config.height);
                } else {
                    multiPass.SetImageTargetFBO(0);
                    multiPass.RenderAllPasses(quadVAO, currentTime, timeDelta, frameCount,
                                             mouse, date, config.width, config.height, clickTime);
                }

                // DebugUI 渲染（在 shader 渲染后、SwapWindow 前）
                {
                    glFinish();
                    Uint64 renderEnd = SDL_GetPerformanceCounter();
                    float renderElapsed = static_cast<float>(renderEnd - renderStart) / static_cast<float>(freq);

                    fillDebugState(measuredFPS, currentTime, timeDelta, renderElapsed,
                                   static_cast<float>(config.width),
                                   static_cast<float>(config.height), mouse);

                    debugUI.BeginFrame();
                    debugUI.Render(debugState);
                }

                SDL_GL_SwapWindow(window);
            }
        }
        frameCount++;

        // 帧率自适应 + 帧率控制
        if (config.targetFPS > 0) {
            Uint64 frameEnd = SDL_GetPerformanceCounter();
            float frameElapsed = static_cast<float>(frameEnd - now) / static_cast<float>(freq);

            // 统计平均帧耗时（每60帧调整一次）
            frameTimeAccum += frameElapsed;
            frameTimeCount++;
            if (frameTimeCount >= 60) {
                float avgFrameTime = frameTimeAccum / static_cast<float>(frameTimeCount);
                float maxFrameTime = 1.0f / static_cast<float>(config.targetFPS);

                if (avgFrameTime > maxFrameTime * 1.5f && adaptiveFPS > 1.0f) {
                    // GPU 负载过高，降低帧率
                    adaptiveFPS = std::max(1.0f, adaptiveFPS * 0.8f);
                    std::cout << "Adaptive FPS: lowered to " << static_cast<int>(adaptiveFPS) << std::endl;
                } else if (avgFrameTime < maxFrameTime * 0.7f && adaptiveFPS < static_cast<float>(config.targetFPS)) {
                    // GPU 负载轻松，恢复帧率
                    adaptiveFPS = std::min(static_cast<float>(config.targetFPS), adaptiveFPS * 1.2f);
                }
                frameTimeAccum = 0.0f;
                frameTimeCount = 0;
            }

            // 帧率控制：用总耗时（含 VSync）来补足剩余等待
            float targetFrameTime = 1.0f / adaptiveFPS;
            if (frameElapsed < targetFrameTime) {
                SDL_Delay(static_cast<Uint32>((targetFrameTime - frameElapsed) * 1000.0f));
            }
        }
    }

    // ============================================================
    // 清理
    // ============================================================
    watcher.Stop();
    tray.Destroy();
    debugUI.Shutdown();
    blitRenderer.Cleanup();

#ifdef _WIN32
    // D3D11 资源释放
    if (useD3D11) {
        d3dWindowBlit.reset();
        for (auto& ww : wallpaperWindows) {
            ww.d3dBlit.reset();
        }
        d3dMultiPass.reset();
        d3dTextures.reset();
        d3dRenderer.reset();
    }
#endif

    // 壁纸模式：在 GL context 销毁前释放各显示器的 BlitRenderer
    if (config.wallpaperMode) {
        for (auto& ww : wallpaperWindows) {
            ww.blit.reset();
        }
    }

    if (config.wallpaperMode) {
        Wallpaper::Restore();
    }

    if (glContext) {
        SDL_GL_DeleteContext(glContext);
    }
    if (config.wallpaperMode) {
        for (auto& ww : wallpaperWindows) {
            SDL_DestroyWindow(ww.window);
        }
    } else {
        SDL_DestroyWindow(window);
    }
    SDL_Quit();

    std::cout << "Goodbye!" << std::endl;
    return 0;
}
