#define NOMINMAX
#include <iostream>
#include <string>
#include <ctime>
#include <vector>
#include <atomic>
#include <mutex>
#include <algorithm>
#include <array>
#include <filesystem>

#include <glad/glad.h>
#include <SDL.h>

#include "renderer.h"
#include "shader_manager.h"
#include "wallpaper.h"
#include "texture_manager.h"
#include "multi_pass.h"
#include "file_watcher.h"
#include "tray_icon.h"
#include "debug_ui.h"

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
    int         width  = kDefaultWidth;
    int         height = kDefaultHeight;
    int         targetFPS = -1;      // -1 表示未指定，壁纸模式默认30，窗口模式默认60
    float       renderScale = 0.0f;  // 渲染分辨率缩放，0=自动（壁纸模式0.5，窗口模式1.0）
};

/// 确保有控制台可以输出（WIN32 子系统默认没有控制台）
static void EnsureConsole() {
#ifdef _WIN32
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
        AllocConsole();
    }
    FILE* fp = nullptr;
    freopen_s(&fp, "CONOUT$", "w", stdout);
    freopen_s(&fp, "CONOUT$", "w", stderr);
#endif
}

static void PrintUsage(const char* programName) {
    EnsureConsole();
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
              << "  --no-hotreload       Disable shader hot reload\n"
              << "  --width <n>          Window width (default: " << kDefaultWidth << ")\n"
              << "  --height <n>         Window height (default: " << kDefaultHeight << ")\n"
              << "  --fps <n>            Target FPS (wallpaper default: 30, window default: 60)\n"
              << "  --renderscale <f>    Render resolution scale 0.0-1.0 (wallpaper default: 0.5, window default: 1.0)\n"
              << "  --debug              Show debug overlay in wallpaper mode (default: off)\n"
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
        } else if (arg == "--help" || arg == "-h") {
            PrintUsage(argv[0]);
            exit(0);
        }
    }
    return config;
}

int main(int argc, char* argv[]) {
    // 声明 DPI 感知，确保多显示器不同 DPI 时获取正确的物理像素尺寸
#ifdef _WIN32
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
#endif

    AppConfig config = ParseArgs(argc, argv);

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
    };
    std::vector<WallpaperWindow> wallpaperWindows;
    SDL_Window* window = nullptr;       // 主窗口（窗口模式用，壁纸模式指向第一个）
    SDL_GLContext glContext = nullptr;

    if (config.wallpaperMode) {
        auto monitors = Wallpaper::EnumMonitors();
        if (monitors.empty()) {
            std::cerr << "No monitors found." << std::endl;
            SDL_Quit();
            return 1;
        }

        // 允许共享 GL 上下文
        SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);

        for (size_t i = 0; i < monitors.size(); ++i) {
            const auto& mon = monitors[i];
            Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_BORDERLESS;

            SDL_Window* win = SDL_CreateWindow(
                kWindowTitle, mon.x, mon.y, mon.width, mon.height, flags);
            if (!win) {
                std::cerr << "Failed to create window for monitor " << i << ": "
                          << SDL_GetError() << std::endl;
                continue;
            }

            // 第一个窗口创建 GL 上下文
            if (i == 0) {
                glContext = SDL_GL_CreateContext(win);
                if (!glContext) {
                    std::cerr << "SDL_GL_CreateContext failed: " << SDL_GetError() << std::endl;
                    SDL_DestroyWindow(win);
                    SDL_Quit();
                    return 1;
                }
                if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
                    std::cerr << "gladLoadGLLoader failed." << std::endl;
                    SDL_GL_DeleteContext(glContext);
                    SDL_DestroyWindow(win);
                    SDL_Quit();
                    return 1;
                }
            }

            // 嵌入到桌面壁纸层
            if (!Wallpaper::EmbedAsWallpaper(win, mon)) {
                std::cerr << "Failed to embed monitor " << i << std::endl;
                SDL_DestroyWindow(win);
                continue;
            }

            WallpaperWindow ww;
            ww.window = win;
            ww.x = mon.x;
            ww.y = mon.y;
            ww.width = mon.width;
            ww.height = mon.height;
            wallpaperWindows.push_back(ww);
        }

        if (wallpaperWindows.empty()) {
            std::cerr << "Failed to embed any monitor, falling back to window mode." << std::endl;
            config.wallpaperMode = false;
        } else {
            window = wallpaperWindows[0].window;
            // 取所有显示器的最大宽高，用于 FBO 预分配（避免多显示器不同分辨率时反复重建）
            int maxW = 0, maxH = 0;
            for (const auto& ww : wallpaperWindows) {
                if (ww.width > maxW) maxW = ww.width;
                if (ww.height > maxH) maxH = ww.height;
            }
            config.width = maxW;
            config.height = maxH;
        }
    }

    // 窗口模式
    if (!config.wallpaperMode) {
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

    std::cout << "OpenGL " << glGetString(GL_VERSION) << std::endl;
    std::cout << "Renderer: " << glGetString(GL_RENDERER) << std::endl;

    // 应用模式相关的默认值
    if (config.targetFPS < 0) {
        config.targetFPS = config.wallpaperMode ? 30 : 60;
    }
    if (config.renderScale <= 0.0f) {
        config.renderScale = config.wallpaperMode ? 0.5f : 1.0f;
    }

    // VSync 策略：关闭 VSync，纯靠 SDL_Delay 精确控帧
    // （VSync 在 WorkerW 嵌入窗口和部分驱动下行为不可靠）
    SDL_GL_SetSwapInterval(0);

    std::cout << "Target FPS: " << config.targetFPS
              << ", Render scale: " << config.renderScale << std::endl;

    // ============================================================
    // 降分辨率渲染 FBO（renderScale < 1.0 时启用）
    // ============================================================
    GLuint renderFBO = 0, renderTex = 0;
    int renderWidth = config.width, renderHeight = config.height;
    bool useScaledRender = (config.renderScale < 1.0f);

    // 用于 blit FBO 到屏幕的简单着色器
    GLuint blitProgram = 0, blitVAO = 0, blitVBO = 0;

    auto CreateRenderFBO = [&](int w, int h) {
        renderWidth = static_cast<int>(w * config.renderScale);
        renderHeight = static_cast<int>(h * config.renderScale);
        if (renderWidth < 1) renderWidth = 1;
        if (renderHeight < 1) renderHeight = 1;

        if (renderFBO) {
            glDeleteFramebuffers(1, &renderFBO);
            glDeleteTextures(1, &renderTex);
        }
        glGenFramebuffers(1, &renderFBO);
        glGenTextures(1, &renderTex);

        glBindTexture(GL_TEXTURE_2D, renderTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, renderWidth, renderHeight, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glBindFramebuffer(GL_FRAMEBUFFER, renderFBO);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, renderTex, 0);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            std::cerr << "Render FBO incomplete!" << std::endl;
            useScaledRender = false;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    };

    if (useScaledRender) {
        // 创建 blit 着色器
        const char* blitVS =
            "#version 330 core\n"
            "layout(location=0) in vec2 aPos;\n"
            "out vec2 vUV;\n"
            "void main(){\n"
            "  vUV = aPos * 0.5 + 0.5;\n"
            "  gl_Position = vec4(aPos, 0.0, 1.0);\n"
            "}\n";
        const char* blitFS =
            "#version 330 core\n"
            "in vec2 vUV;\n"
            "out vec4 fragColor;\n"
            "uniform sampler2D uTex;\n"
            "void main(){\n"
            "  fragColor = texture(uTex, vUV);\n"
            "}\n";

        GLuint vs = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vs, 1, &blitVS, nullptr);
        glCompileShader(vs);
        GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fs, 1, &blitFS, nullptr);
        glCompileShader(fs);
        blitProgram = glCreateProgram();
        glAttachShader(blitProgram, vs);
        glAttachShader(blitProgram, fs);
        glLinkProgram(blitProgram);
        glDeleteShader(vs);
        glDeleteShader(fs);

        // blit 全屏四边形
        float blitQuad[] = {-1,-1, 1,-1, -1,1, 1,-1, 1,1, -1,1};
        glGenVertexArrays(1, &blitVAO);
        glGenBuffers(1, &blitVBO);
        glBindVertexArray(blitVAO);
        glBindBuffer(GL_ARRAY_BUFFER, blitVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(blitQuad), blitQuad, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
        glEnableVertexAttribArray(0);
        glBindVertexArray(0);

        CreateRenderFBO(config.width, config.height);
        std::cout << "Scaled rendering: " << renderWidth << "x" << renderHeight
                  << " (scale=" << config.renderScale << ")" << std::endl;
    }

    // ============================================================
    // 初始化渲染器
    // ============================================================
    Renderer renderer;
    if (!renderer.Init()) {
        std::cerr << "Renderer init failed." << std::endl;
        SDL_GL_DeleteContext(glContext);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    renderer.SetViewport(config.width, config.height);

    // ============================================================
    // 加载纹理
    // ============================================================
    TextureManager textures;
    std::string channelPaths[4] = {config.channel0, config.channel1, config.channel2, config.channel3};
    for (int i = 0; i < 4; ++i) {
        if (channelPaths[i].empty()) continue;
        if (config.channelTypes[i] == ChannelType::CubeMap) {
            textures.LoadCubeMap(i, channelPaths[i]);
        } else {
            textures.LoadTexture(i, channelPaths[i]);
        }
    }

    // 从实际加载结果同步通道类型（LoadTexture/LoadCubeMap 内部会设置）
    for (int i = 0; i < 4; ++i) {
        ChannelType actual = textures.GetChannelType(i);
        if (actual != ChannelType::None) {
            config.channelTypes[i] = actual;
        }
    }

    // ============================================================
    // 加载着色器
    // ============================================================
    // 根据已加载的纹理推断通道类型（未加载的通道也保持 Texture2D 不会出错）
    // 未来加入 CubeMap/3D 加载时，可通过 --channeltype0 cube 等参数或自动检测
    ShaderManager shader;
    shader.SetChannelTypes(config.channelTypes);
    if (!shader.LoadFromFile(config.shaderPath)) {
        std::cerr << "Failed to load shader: " << shader.GetLastError() << std::endl;
        SDL_GL_DeleteContext(glContext);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    std::cout << "Shader loaded: " << config.shaderPath << std::endl;

    // ============================================================
    // 热加载
    // ============================================================
    std::atomic<bool> shaderNeedsReload{false};
    FileWatcher watcher;
    if (config.hotReload) {
        watcher.Watch(config.shaderPath, [&](const std::string&) {
            shaderNeedsReload.store(true);
        });
        std::cout << "Hot reload enabled." << std::endl;
    }

    // ============================================================
    // 系统托盘（壁纸模式下启用）
    // ============================================================
    std::atomic<bool> paused{false};
    bool running = true;
    TrayIcon tray;
    if (config.wallpaperMode) {
        TrayIcon::MenuCallbacks cb;
        cb.onPause  = [&]() { paused = true;  std::cout << "Paused." << std::endl; };
        cb.onResume = [&]() { paused = false; std::cout << "Resumed." << std::endl; };
        cb.onReload = [&]() { shaderNeedsReload = true; std::cout << "Reload requested." << std::endl; };
        cb.onQuit   = [&]() { running = false; };
        tray.Create(window, cb);
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
    // 调试 UI（窗口模式始终初始化，壁纸模式仅 --debug 时初始化）
    // ============================================================
    DebugUI debugUI;
    DebugUIState debugState;
    std::string lastShaderError;

    // 扫描 assets/shaders/ 目录获取所有 .glsl 文件
    auto ScanShaderFiles = [&]() {
        debugState.shaderFiles.clear();
        const std::string shaderDir = "assets/shaders";
        std::error_code ec;
        if (std::filesystem::exists(shaderDir, ec)) {
            for (const auto& entry : std::filesystem::directory_iterator(shaderDir, ec)) {
                if (entry.is_regular_file() && entry.path().extension() == ".glsl") {
                    // 使用正斜杠路径，与项目惯例一致
                    std::string path = entry.path().generic_string();
                    debugState.shaderFiles.push_back(path);
                }
            }
            std::sort(debugState.shaderFiles.begin(), debugState.shaderFiles.end());
        }
    };

    if (!config.wallpaperMode) {
        if (!debugUI.Init(window, glContext)) {
            std::cerr << "DebugUI init failed, continuing without debug panel." << std::endl;
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
    // 全屏应用检测辅助函数
    // ============================================================
#ifdef _WIN32
    auto IsFullscreenAppRunning = []() -> bool {
        HWND fg = GetForegroundWindow();
        if (!fg) return false;

        // 排除桌面和任务栏
        HWND desktop = GetDesktopWindow();
        HWND shell = GetShellWindow();
        if (fg == desktop || fg == shell) return false;

        // 检查类名排除 Progman/WorkerW（桌面本身）
        wchar_t className[64] = {};
        GetClassNameW(fg, className, 64);
        if (wcscmp(className, L"Progman") == 0 || wcscmp(className, L"WorkerW") == 0)
            return false;

        // 检查窗口是否覆盖了整个屏幕
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
        debugState.shaderPath  = config.shaderPath.c_str();
        debugState.shaderError = lastShaderError.c_str();
        debugState.paused      = paused.load();
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
                    renderer.SetViewport(config.width, config.height);
                    if (useScaledRender) {
                        CreateRenderFBO(config.width, config.height);
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
                clickTime = static_cast<float>(clickNow - startTime) / static_cast<float>(freq);
            } else if (!leftDown && mousePressed) {
                // 刚松开
                mousePressed = false;
                mouse[2] = -mouse[2];
                mouse[3] = -mouse[3];
            }
        }
#endif

        // 处理 DebugUI 控制请求
        if (!config.wallpaperMode) {
            // shader 切换请求
            if (!debugState.requestSwitchShader.empty()) {
                config.shaderPath = debugState.requestSwitchShader;
                debugState.requestSwitchShader.clear();
                shaderNeedsReload = true;
                // 更新 FileWatcher
                if (config.hotReload) {
                    watcher.Stop();
                    watcher.Watch(config.shaderPath, [&](const std::string&) {
                        shaderNeedsReload.store(true);
                    });
                }
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
                    // 需要创建 blit 资源（首次启用降分辨率）
                    // 简化处理：标记 useScaledRender，FBO 在渲染时按需创建
                    useScaledRender = true;
                    if (!blitProgram) {
                        const char* blitVS =
                            "#version 330 core\n"
                            "layout(location=0) in vec2 aPos;\n"
                            "out vec2 vUV;\n"
                            "void main(){\n"
                            "  vUV = aPos * 0.5 + 0.5;\n"
                            "  gl_Position = vec4(aPos, 0.0, 1.0);\n"
                            "}\n";
                        const char* blitFS =
                            "#version 330 core\n"
                            "in vec2 vUV;\n"
                            "out vec4 fragColor;\n"
                            "uniform sampler2D uTex;\n"
                            "void main(){\n"
                            "  fragColor = texture(uTex, vUV);\n"
                            "}\n";
                        GLuint vs = glCreateShader(GL_VERTEX_SHADER);
                        glShaderSource(vs, 1, &blitVS, nullptr);
                        glCompileShader(vs);
                        GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
                        glShaderSource(fs, 1, &blitFS, nullptr);
                        glCompileShader(fs);
                        blitProgram = glCreateProgram();
                        glAttachShader(blitProgram, vs);
                        glAttachShader(blitProgram, fs);
                        glLinkProgram(blitProgram);
                        glDeleteShader(vs);
                        glDeleteShader(fs);

                        float blitQuad[] = {-1,-1, 1,-1, -1,1, 1,-1, 1,1, -1,1};
                        glGenVertexArrays(1, &blitVAO);
                        glGenBuffers(1, &blitVBO);
                        glBindVertexArray(blitVAO);
                        glBindBuffer(GL_ARRAY_BUFFER, blitVBO);
                        glBufferData(GL_ARRAY_BUFFER, sizeof(blitQuad), blitQuad, GL_STATIC_DRAW);
                        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
                        glEnableVertexAttribArray(0);
                        glBindVertexArray(0);
                    }
                } else if (!newScaled) {
                    useScaledRender = false;
                }
                if (useScaledRender) {
                    CreateRenderFBO(config.width, config.height);
                }
            }
        }

        // 热加载：重新编译 shader
        if (shaderNeedsReload.exchange(false)) {
            ShaderManager newShader;
            newShader.SetChannelTypes(config.channelTypes);
            if (newShader.LoadFromFile(config.shaderPath)) {
                shader = std::move(newShader);
                lastShaderError.clear();
                std::cout << "Shader reloaded successfully." << std::endl;
            } else {
                lastShaderError = newShader.GetLastError();
                std::cerr << "Shader reload failed: " << lastShaderError << std::endl;
            }
        }

        // 暂停时跳过 shader 渲染，但仍渲染 DebugUI
        if (paused) {
            if (!config.wallpaperMode) {
                fillDebugState(0.0f, lastFrameTime, 0.0f, 0.0f,
                               static_cast<float>(config.width),
                               static_cast<float>(config.height), mouse);

                debugUI.BeginFrame();
                debugUI.Render(debugState);
                SDL_GL_SwapWindow(window);
            } else {
                if (config.showDebug) {
                    for (auto& ww : wallpaperWindows) {
                        SDL_GL_MakeCurrent(ww.window, glContext);
                        fillDebugState(0.0f, lastFrameTime, 0.0f, 0.0f,
                                       static_cast<float>(ww.width),
                                       static_cast<float>(ww.height), mouse);

                        debugUI.BeginFrame();
                        debugUI.RenderOverlay(debugState);
                        SDL_GL_SwapWindow(ww.window);
                    }
                }
                SDL_Delay(100);
            }
            continue;
        }

        // 壁纸模式：每秒检测一次全屏应用
#ifdef _WIN32
        if (config.wallpaperMode) {
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

        // 绑定纹理并设置 iChannel uniform
        textures.BindAll();
        shader.Use();
        for (int i = 0; i < 4; ++i) {
            char name[16];
            snprintf(name, sizeof(name), "iChannel%d", i);
            glUniform1i(shader.GetUniformLocation(name), i);
        }
        // 设置 iChannelResolution
        float channelRes[4][3];
        textures.GetAllResolutions(channelRes);
        glUniform3fv(shader.GetUniformLocation("iChannelResolution"), 4, &channelRes[0][0]);
        glUniform1f(shader.GetUniformLocation("iClickTime"), clickTime);

        // 渲染
        if (config.wallpaperMode && !wallpaperWindows.empty()) {
            // 壁纸模式：依次渲染每个显示器窗口，鼠标坐标转局部
            for (auto& ww : wallpaperWindows) {
                SDL_GL_MakeCurrent(ww.window, glContext);

                // 将全局屏幕坐标转为当前窗口的局部坐标（ShaderToy Y 从底部开始）
                float localMouse[4];
                float localX = mouse[0] - static_cast<float>(ww.x);
                float localY = static_cast<float>(ww.height) - (mouse[1] - static_cast<float>(ww.y));
                bool inThisMonitor = (localX >= 0 && localX < ww.width &&
                                      localY >= 0 && localY < ww.height);

                localMouse[0] = inThisMonitor ? localX : -1.0f;
                localMouse[1] = inThisMonitor ? localY : -1.0f;

                // zw（点击位置）也转局部
                float clickAbsX = fabsf(mouse[2]);
                float clickAbsY = fabsf(mouse[3]);
                float clickLocalX = clickAbsX - static_cast<float>(ww.x);
                float clickLocalY = static_cast<float>(ww.height) - (clickAbsY - static_cast<float>(ww.y));
                bool clickInThis = (clickLocalX >= 0 && clickLocalX < ww.width &&
                                    clickLocalY >= 0 && clickLocalY < ww.height);

                if (clickInThis) {
                    // 保持正/负号（正=按住，负=已松开）
                    localMouse[2] = (mouse[2] >= 0) ? clickLocalX : -clickLocalX;
                    localMouse[3] = (mouse[3] >= 0) ? clickLocalY : -clickLocalY;
                } else {
                    localMouse[2] = 0.0f;
                    localMouse[3] = 0.0f;
                }

                Uint64 renderStart = SDL_GetPerformanceCounter();

                if (useScaledRender) {
                    // 降分辨率渲染到 FBO（FBO 已按最大显示器尺寸预分配，无需重建）
                    int curRenderW = static_cast<int>(ww.width * config.renderScale);
                    int curRenderH = static_cast<int>(ww.height * config.renderScale);
                    if (curRenderW < 1) curRenderW = 1;
                    if (curRenderH < 1) curRenderH = 1;

                    glBindFramebuffer(GL_FRAMEBUFFER, renderFBO);
                    renderer.SetViewport(curRenderW, curRenderH);

                    // 鼠标坐标也按比例缩放
                    float scaledMouse[4] = {
                        localMouse[0] * config.renderScale,
                        localMouse[1] * config.renderScale,
                        localMouse[2] * config.renderScale,
                        localMouse[3] * config.renderScale
                    };

                    renderer.RenderFrame(shader, currentTime, timeDelta, frameCount,
                                        scaledMouse, date);

                    // Blit FBO 到屏幕
                    glBindFramebuffer(GL_FRAMEBUFFER, 0);
                    renderer.SetViewport(ww.width, ww.height);
                    glUseProgram(blitProgram);
                    glActiveTexture(GL_TEXTURE0);
                    glBindTexture(GL_TEXTURE_2D, renderTex);
                    glUniform1i(glGetUniformLocation(blitProgram, "uTex"), 0);
                    glBindVertexArray(blitVAO);
                    glDrawArrays(GL_TRIANGLES, 0, 6);
                    glBindVertexArray(0);
                } else {
                    renderer.SetViewport(ww.width, ww.height);
                    renderer.RenderFrame(shader, currentTime, timeDelta, frameCount,
                                        localMouse, date);
                }

                // 壁纸模式 Debug 叠加
                if (config.showDebug) {
                    Uint64 preSwap = SDL_GetPerformanceCounter();
                    float renderElapsed = static_cast<float>(preSwap - renderStart) / static_cast<float>(freq);

                    fillDebugState(measuredFPS, currentTime, timeDelta, renderElapsed,
                                   static_cast<float>(ww.width),
                                   static_cast<float>(ww.height), localMouse);

                    debugUI.BeginFrame();
                    debugUI.RenderOverlay(debugState);
                }

                SDL_GL_SwapWindow(ww.window);
            }
        } else {
            // 窗口模式
            Uint64 renderStart = SDL_GetPerformanceCounter();

            if (useScaledRender) {
                int scaledW = static_cast<int>(config.width * config.renderScale);
                int scaledH = static_cast<int>(config.height * config.renderScale);
                if (scaledW != renderWidth || scaledH != renderHeight) {
                    CreateRenderFBO(config.width, config.height);
                }

                glBindFramebuffer(GL_FRAMEBUFFER, renderFBO);
                renderer.SetViewport(renderWidth, renderHeight);
                renderer.RenderFrame(shader, currentTime, timeDelta, frameCount,
                                    mouse, date);

                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                renderer.SetViewport(config.width, config.height);
                glUseProgram(blitProgram);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, renderTex);
                glUniform1i(glGetUniformLocation(blitProgram, "uTex"), 0);
                glBindVertexArray(blitVAO);
                glDrawArrays(GL_TRIANGLES, 0, 6);
                glBindVertexArray(0);
            } else {
                renderer.RenderFrame(shader, currentTime, timeDelta, frameCount,
                                    mouse, date);
            }

            // DebugUI 渲染（在 shader 渲染后、SwapWindow 前）
            {
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

    // 释放降分辨率渲染资源
    if (renderFBO) { glDeleteFramebuffers(1, &renderFBO); glDeleteTextures(1, &renderTex); }
    if (blitProgram) glDeleteProgram(blitProgram);
    if (blitVAO) { glDeleteVertexArrays(1, &blitVAO); glDeleteBuffers(1, &blitVBO); }

    if (config.wallpaperMode) {
        Wallpaper::Restore();
    }

    SDL_GL_DeleteContext(glContext);
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
