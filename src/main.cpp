#define NOMINMAX
#include <iostream>
#include <string>
#include <ctime>
#include <vector>
#include <atomic>
#include <mutex>
#include <algorithm>

#include <glad/glad.h>
#include <SDL.h>

#include "renderer.h"
#include "shader_manager.h"
#include "wallpaper.h"
#include "texture_manager.h"
#include "multi_pass.h"
#include "file_watcher.h"
#include "tray_icon.h"

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
    bool        wallpaperMode = false;
    bool        hotReload = true;
    int         width  = kDefaultWidth;
    int         height = kDefaultHeight;
    int         targetFPS = 60;
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
              << "  --wallpaper          Run as desktop wallpaper\n"
              << "  --no-hotreload       Disable shader hot reload\n"
              << "  --width <n>          Window width (default: " << kDefaultWidth << ")\n"
              << "  --height <n>         Window height (default: " << kDefaultHeight << ")\n"
              << "  --fps <n>            Target FPS (default: 60)\n"
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
        } else if (arg == "--help" || arg == "-h") {
            PrintUsage(argv[0]);
            exit(0);
        }
    }
    return config;
}

int main(int argc, char* argv[]) {
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

    // 壁纸模式时使用虚拟桌面分辨率（覆盖所有显示器）
    if (config.wallpaperMode) {
        Wallpaper::GetVirtualDesktopResolution(config.width, config.height);
    }

    // 创建窗口
    Uint32 windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN;
    if (config.wallpaperMode) {
        windowFlags |= SDL_WINDOW_BORDERLESS;
    } else {
        windowFlags |= SDL_WINDOW_RESIZABLE;
    }

    SDL_Window* window = SDL_CreateWindow(
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

    // 创建 OpenGL 上下文
    SDL_GLContext glContext = SDL_GL_CreateContext(window);
    if (!glContext) {
        std::cerr << "SDL_GL_CreateContext failed: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // 初始化 GLAD
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        std::cerr << "gladLoadGLLoader failed." << std::endl;
        SDL_GL_DeleteContext(glContext);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    std::cout << "OpenGL " << glGetString(GL_VERSION) << std::endl;
    std::cout << "Renderer: " << glGetString(GL_RENDERER) << std::endl;

    // VSync
    SDL_GL_SetSwapInterval(1);

    // ============================================================
    // 壁纸模式：嵌入桌面
    // ============================================================
    if (config.wallpaperMode) {
        if (!Wallpaper::EmbedAsWallpaper(window)) {
            std::cerr << "Warning: Failed to embed as wallpaper, running in window mode." << std::endl;
            config.wallpaperMode = false;
        }
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
    if (!config.channel0.empty()) textures.LoadTexture(0, config.channel0);
    if (!config.channel1.empty()) textures.LoadTexture(1, config.channel1);
    if (!config.channel2.empty()) textures.LoadTexture(2, config.channel2);
    if (!config.channel3.empty()) textures.LoadTexture(3, config.channel3);

    // ============================================================
    // 加载着色器
    // ============================================================
    ShaderManager shader;
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
        std::cout << "Running in window mode. Press ESC to exit, F5 to reload shader." << std::endl;
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

    // 帧率自适应
    float adaptiveFPS = static_cast<float>(config.targetFPS);
    float frameTimeAccum = 0.0f;
    int frameTimeCount = 0;

    // iMouse: xy=当前鼠标位置, zw=按下瞬间的位置（松开后取负值）
    float mouse[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    bool mousePressed = false;
    float clickTime = -10.0f;  // 最近一次点击的 iTime，初始设为远过去

    while (running) {
        // 事件处理
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            tray.HandleEvent(event);

            switch (event.type) {
            case SDL_QUIT:
                running = false;
                break;
            case SDL_KEYDOWN:
                if (event.key.keysym.sym == SDLK_ESCAPE) {
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
                }
                break;
            case SDL_MOUSEMOTION:
                mouse[0] = static_cast<float>(event.motion.x);
                mouse[1] = static_cast<float>(config.height - event.motion.y);
                break;
            case SDL_MOUSEBUTTONDOWN:
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
                if (event.button.button == SDL_BUTTON_LEFT) {
                    mousePressed = false;
                    mouse[2] = -mouse[2];
                    mouse[3] = -mouse[3];
                }
                break;
            }
        }

        // 壁纸模式：SDL 收不到鼠标事件，改用 Win32 全局鼠标状态
#ifdef _WIN32
        if (config.wallpaperMode) {
            POINT pt;
            GetCursorPos(&pt);
            mouse[0] = static_cast<float>(pt.x);
            mouse[1] = static_cast<float>(config.height - pt.y);

            bool leftDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
            if (leftDown && !mousePressed) {
                // 刚按下
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

        // 热加载：重新编译 shader
        if (shaderNeedsReload.exchange(false)) {
            ShaderManager newShader;
            if (newShader.LoadFromFile(config.shaderPath)) {
                shader = std::move(newShader);
                std::cout << "Shader reloaded successfully." << std::endl;
            } else {
                std::cerr << "Shader reload failed: " << newShader.GetLastError() << std::endl;
            }
        }

        // 暂停时跳过渲染
        if (paused) {
            SDL_Delay(100);
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
        renderer.RenderFrame(shader, currentTime, timeDelta, frameCount,
                            mouse, date);

        SDL_GL_SwapWindow(window);
        frameCount++;

        // 帧率自适应 + 帧率控制
        if (config.targetFPS > 0) {
            // 统计平均帧时间（每60帧调整一次）
            frameTimeAccum += timeDelta;
            frameTimeCount++;
            if (frameTimeCount >= 60) {
                float avgFrameTime = frameTimeAccum / static_cast<float>(frameTimeCount);
                float maxFrameTime = 1.0f / static_cast<float>(config.targetFPS);

                if (avgFrameTime > maxFrameTime * 1.5f && adaptiveFPS > 15.0f) {
                    // GPU 负载过高，降低帧率
                    adaptiveFPS = std::max(15.0f, adaptiveFPS * 0.8f);
                    std::cout << "Adaptive FPS: lowered to " << static_cast<int>(adaptiveFPS) << std::endl;
                } else if (avgFrameTime < maxFrameTime * 0.7f && adaptiveFPS < static_cast<float>(config.targetFPS)) {
                    // GPU 负载轻松，恢复帧率
                    adaptiveFPS = std::min(static_cast<float>(config.targetFPS), adaptiveFPS * 1.2f);
                }
                frameTimeAccum = 0.0f;
                frameTimeCount = 0;
            }

            float targetFrameTime = 1.0f / adaptiveFPS;
            if (timeDelta < targetFrameTime) {
                SDL_Delay(static_cast<Uint32>((targetFrameTime - timeDelta) * 1000.0f));
            }
        }
    }

    // ============================================================
    // 清理
    // ============================================================
    watcher.Stop();
    tray.Destroy();

    if (config.wallpaperMode) {
        Wallpaper::Restore();
    }

    SDL_GL_DeleteContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();

    std::cout << "Goodbye!" << std::endl;
    return 0;
}
