#include <iostream>
#include <string>
#include <ctime>

#include <glad/glad.h>
#include <SDL.h>
#include <SDL_syswm.h>

#include "renderer.h"
#include "shader_manager.h"
#include "wallpaper.h"

// 默认参数
static const int    kDefaultWidth  = 800;
static const int    kDefaultHeight = 600;
static const char*  kDefaultShader = "assets/shaders/default.glsl";
static const char*  kWindowTitle   = "ShaderToy Desktop";

struct AppConfig {
    std::string shaderPath = kDefaultShader;
    bool        wallpaperMode = false;
    int         width  = kDefaultWidth;
    int         height = kDefaultHeight;
    int         targetFPS = 60;
};

static void PrintUsage(const char* programName) {
    std::cout << "Usage: " << programName << " [options]\n"
              << "Options:\n"
              << "  --shader <path>    Path to ShaderToy GLSL file (default: " << kDefaultShader << ")\n"
              << "  --wallpaper        Run as desktop wallpaper\n"
              << "  --width <n>        Window width (default: " << kDefaultWidth << ")\n"
              << "  --height <n>       Window height (default: " << kDefaultHeight << ")\n"
              << "  --fps <n>          Target FPS (default: 60)\n"
              << "  --help             Show this help\n";
}

static AppConfig ParseArgs(int argc, char* argv[]) {
    AppConfig config;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--shader" && i + 1 < argc) {
            config.shaderPath = argv[++i];
        } else if (arg == "--wallpaper") {
            config.wallpaperMode = true;
        } else if (arg == "--width" && i + 1 < argc) {
            config.width = std::atoi(argv[++i]);
        } else if (arg == "--height" && i + 1 < argc) {
            config.height = std::atoi(argv[++i]);
        } else if (arg == "--fps" && i + 1 < argc) {
            config.targetFPS = std::atoi(argv[++i]);
        } else if (arg == "--help") {
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

    // 壁纸模式时使用桌面分辨率
    if (config.wallpaperMode) {
        Wallpaper::GetDesktopResolution(config.width, config.height);
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
    // 初始化渲染器和着色器
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

    ShaderManager shader;
    if (!shader.LoadFromFile(config.shaderPath)) {
        std::cerr << "Failed to load shader: " << shader.GetLastError() << std::endl;
        SDL_GL_DeleteContext(glContext);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    std::cout << "Shader loaded: " << config.shaderPath << std::endl;
    if (config.wallpaperMode) {
        std::cout << "Running in wallpaper mode." << std::endl;
    } else {
        std::cout << "Running in window mode. Press ESC to exit." << std::endl;
    }

    // ============================================================
    // 主循环
    // ============================================================
    bool running = true;
    Uint64 startTime = SDL_GetPerformanceCounter();
    Uint64 freq = SDL_GetPerformanceFrequency();
    float lastFrameTime = 0.0f;
    int frameCount = 0;
    float mouseX = 0.0f, mouseY = 0.0f;
    bool mousePressed = false;

    while (running) {
        // 事件处理
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_QUIT:
                running = false;
                break;
            case SDL_KEYDOWN:
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    running = false;
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
                mouseX = static_cast<float>(event.motion.x);
                // ShaderToy 的 Y 坐标是从底部开始的
                mouseY = static_cast<float>(config.height - event.motion.y);
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    mousePressed = true;
                }
                break;
            case SDL_MOUSEBUTTONUP:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    mousePressed = false;
                }
                break;
            }
        }

        // 时间计算
        Uint64 now = SDL_GetPerformanceCounter();
        float currentTime = static_cast<float>(now - startTime) / static_cast<float>(freq);
        float timeDelta = currentTime - lastFrameTime;
        lastFrameTime = currentTime;

        // 渲染
        renderer.RenderFrame(shader, currentTime, timeDelta, frameCount,
                            mouseX, mouseY, mousePressed);

        SDL_GL_SwapWindow(window);
        frameCount++;

        // 帧率控制（如果不使用 VSync）
        if (config.targetFPS > 0) {
            float targetFrameTime = 1.0f / config.targetFPS;
            if (timeDelta < targetFrameTime) {
                SDL_Delay(static_cast<Uint32>((targetFrameTime - timeDelta) * 1000.0f));
            }
        }
    }

    // ============================================================
    // 清理
    // ============================================================
    if (config.wallpaperMode) {
        Wallpaper::Restore();
    }

    SDL_GL_DeleteContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();

    std::cout << "Goodbye!" << std::endl;
    return 0;
}
