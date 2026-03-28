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
#include <filesystem>

#include <glad/glad.h>
#include <SDL.h>

#include "renderer.h"
#include "shader_manager.h"
#include "wallpaper.h"
#include "texture_manager.h"
#include "multi_pass.h"
#include "blit_renderer.h"
#include "shader_project.h"
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
        bool occluded = false;      // 是否被窗口遮挡（遮挡时跳过渲染）
        std::unique_ptr<BlitRenderer> blit; // 每个显示器独立的 BlitRenderer（不同分辨率各自持有 FBO）
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
            wallpaperWindows.push_back(std::move(ww));
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
    // 降分辨率渲染（renderScale < 1.0 时启用）
    // ============================================================
    BlitRenderer blitRenderer;  // 窗口模式使用
    bool useScaledRender = (config.renderScale < 1.0f);

    if (useScaledRender) {
        if (config.wallpaperMode && !wallpaperWindows.empty()) {
            // 壁纸模式：每个显示器创建独立的 BlitRenderer
            for (auto& ww : wallpaperWindows) {
                ww.blit = std::make_unique<BlitRenderer>();
                if (!ww.blit->Init()) {
                    std::cerr << "BlitRenderer init failed for monitor (" << ww.x << "," << ww.y << ")" << std::endl;
                    useScaledRender = false;
                    break;
                }
                ww.blit->CreateRenderFBO(ww.width, ww.height, config.renderScale);
                std::cout << "Monitor (" << ww.x << "," << ww.y << ") scaled rendering: "
                          << ww.blit->GetRenderWidth() << "x" << ww.blit->GetRenderHeight()
                          << " (scale=" << config.renderScale << ")" << std::endl;
            }
            if (!useScaledRender) {
                for (auto& ww : wallpaperWindows) ww.blit.reset();
            }
        } else {
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
    }

    // ============================================================
    // 初始化渲染器（全屏四边形 VAO，供 MultiPassRenderer 使用）
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
                // 需要获取纹理 ID —— TextureManager 绑定时使用 glActiveTexture + glBindTexture
                // 这里我们通过 Bind 获取当前绑定的纹理
                textures.Bind(i);
                GLint texId = 0;
                GLenum target = (textures.GetChannelType(i) == ChannelType::CubeMap) ?
                                GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
                glGetIntegerv((target == GL_TEXTURE_CUBE_MAP) ?
                              GL_TEXTURE_BINDING_CUBE_MAP : GL_TEXTURE_BINDING_2D, &texId);
                multiPass.SetExternalTexture(i, static_cast<GLuint>(texId),
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

    if (!SetupMultiPass(projData)) {
        std::cerr << "Failed to setup multi-pass renderer: " << multiPass.GetLastError() << std::endl;
        SDL_GL_DeleteContext(glContext);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    std::cout << "Shader loaded: " << config.shaderPath
              << (projData.isMultiPass ? " (multi-pass)" : " (single-pass)") << std::endl;

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
                    // 目录模式：检查是否包含 image.glsl
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
    // 桌面遮挡检测辅助函数（壁纸模式用）
    // ============================================================
#ifdef _WIN32
    // 检查指定显示器区域是否被单个窗口几乎完全覆盖（如最大化窗口）
    // 策略：只要有一个非桌面窗口覆盖了该显示器 >= 90% 面积，就视为遮挡
    // 不累加多窗口面积（避免重叠区域重复计算导致误判）
    auto IsMonitorOccluded = [](int monX, int monY, int monW, int monH) -> bool {
        RECT monRect = {monX, monY, monX + monW, monY + monH};
        long monArea = (long)monW * monH;
        if (monArea <= 0) return false;

        // 枚举从前到后的顶层窗口
        HWND hwnd = GetTopWindow(nullptr);
        while (hwnd) {
            // 跳过不可见窗口
            if (!IsWindowVisible(hwnd)) {
                hwnd = GetNextWindow(hwnd, GW_HWNDNEXT);
                continue;
            }

            // 跳过桌面相关窗口
            wchar_t className[64] = {};
            GetClassNameW(hwnd, className, 64);
            if (wcscmp(className, L"Progman") == 0 || wcscmp(className, L"WorkerW") == 0 ||
                wcscmp(className, L"Shell_TrayWnd") == 0 || wcscmp(className, L"Shell_SecondaryTrayWnd") == 0) {
                hwnd = GetNextWindow(hwnd, GW_HWNDNEXT);
                continue;
            }

            // 跳过最小化窗口
            if (IsIconic(hwnd)) {
                hwnd = GetNextWindow(hwnd, GW_HWNDNEXT);
                continue;
            }

            // 跳过无标题且零面积的隐藏辅助窗口
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
                long iw = inter.right - inter.left;
                long ih = inter.bottom - inter.top;
                if (iw > 0 && ih > 0) {
                    long singleCover = iw * ih;
                    // 单个窗口覆盖 >= 90% 显示器面积，视为遮挡
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
        debugState.shaderPath  = config.shaderPath.c_str();
        debugState.shaderError = lastShaderError.c_str();
        debugState.paused      = paused.load();
        debugState.isMultiPass = multiPass.IsMultiPass();
        debugState.passNames   = multiPass.GetPassNames();
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
                        blitRenderer.CreateRenderFBO(config.width, config.height, config.renderScale);
                        multiPass.Resize(blitRenderer.GetRenderWidth(), blitRenderer.GetRenderHeight());
                        multiPass.SetImageTargetFBO(blitRenderer.GetRenderFBO());
                    } else {
                        multiPass.Resize(config.width, config.height);
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
                    // 重新加载项目并设置监控
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
                    useScaledRender = true;
                    if (config.wallpaperMode) {
                        for (auto& ww : wallpaperWindows) {
                            if (!ww.blit) {
                                ww.blit = std::make_unique<BlitRenderer>();
                                ww.blit->Init();
                            }
                        }
                    } else if (!blitRenderer.IsInitialized()) {
                        blitRenderer.Init();
                    }
                } else if (!newScaled) {
                    useScaledRender = false;
                    // 释放壁纸模式下各显示器的 BlitRenderer（回收显存）
                    if (config.wallpaperMode) {
                        for (auto& ww : wallpaperWindows) {
                            ww.blit.reset();
                        }
                    }
                }
                if (useScaledRender) {
                    if (config.wallpaperMode) {
                        for (auto& ww : wallpaperWindows) {
                            if (ww.blit) {
                                ww.blit->CreateRenderFBO(ww.width, ww.height, config.renderScale);
                            }
                        }
                        // Buffer pass 用最大尺寸
                        int bufW = static_cast<int>(config.width * config.renderScale);
                        int bufH = static_cast<int>(config.height * config.renderScale);
                        if (bufW < 1) bufW = 1;
                        if (bufH < 1) bufH = 1;
                        multiPass.Resize(bufW, bufH);
                    } else {
                        blitRenderer.CreateRenderFBO(config.width, config.height, config.renderScale);
                        multiPass.SetImageTargetFBO(blitRenderer.GetRenderFBO());
                        multiPass.Resize(blitRenderer.GetRenderWidth(), blitRenderer.GetRenderHeight());
                    }
                } else {
                    multiPass.SetImageTargetFBO(0);
                    multiPass.Resize(config.width, config.height);
                }
            }
        }

        // 热加载：重新加载整个 shader 项目并重建渲染器
        if (shaderNeedsReload.exchange(false)) {
            ShaderProject newProject;
            if (newProject.Load(config.shaderPath)) {
                project = std::move(newProject);
                // 重新加载纹理（新 shader 可能需要不同的纹理）
                LoadTexturesForProject(project.GetData());
                if (SetupMultiPass(project.GetData())) {
                    lastShaderError.clear();
                    std::cout << "Shader reloaded successfully." << std::endl;
                } else {
                    lastShaderError = multiPass.GetLastError();
                    std::cerr << "Shader reload failed: " << lastShaderError << std::endl;
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

                        debugUI.BeginFrame(ww.width, ww.height);
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

        // 获取全屏四边形 VAO（从 Renderer 借用）
        GLuint quadVAO = renderer.GetQuadVAO();

        // 渲染
        if (config.wallpaperMode && !wallpaperWindows.empty()) {
            // 壁纸模式优化：Buffer pass 只渲染一次，Image pass 对每个显示器分别渲染
            // Buffer pass 使用最大显示器的降分辨率尺寸（config.width/height 已是 maxW/maxH）
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
                    localMouse[2] = (mouse[2] >= 0) ? clickLocalX : -clickLocalX;
                    localMouse[3] = (mouse[3] >= 0) ? clickLocalY : -clickLocalY;
                } else {
                    localMouse[2] = 0.0f;
                    localMouse[3] = 0.0f;
                }

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
                                             scaledMouse, date, curRenderW, curRenderH, clickTime);

                    ww.blit->BlitToScreen(ww.width, ww.height);
                } else {
                    multiPass.SetImageTargetFBO(0);
                    multiPass.RenderImagePass(quadVAO, currentTime, timeDelta, frameCount,
                                             localMouse, date, ww.width, ww.height, clickTime);
                }

                glFinish();
                Uint64 imageEnd = SDL_GetPerformanceCounter();
                float imageTime = static_cast<float>(imageEnd - imageStart) / static_cast<float>(freq);

                // renderTime = Buffer pass 共享时间 + 当前显示器 Image pass 时间
                float renderElapsed = bufferTime + imageTime;

                if (config.showDebug) {
                    fillDebugState(measuredFPS, currentTime, timeDelta, renderElapsed,
                                   static_cast<float>(ww.width),
                                   static_cast<float>(ww.height), localMouse);

                    debugUI.BeginFrame(ww.width, ww.height);
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
