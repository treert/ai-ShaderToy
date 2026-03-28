#include "debug_ui.h"

#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_opengl3.h>

DebugUI::DebugUI() = default;

DebugUI::~DebugUI() {
    if (initialized_) Shutdown();
}

bool DebugUI::Init(SDL_Window* window, SDL_GLContext glContext) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // 不保存 ini 布局文件
    io.IniFilename = nullptr;

    // 暗色半透明样式
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding   = 6.0f;
    style.FrameRounding    = 4.0f;
    style.GrabRounding     = 4.0f;
    style.Alpha            = 0.9f;
    style.WindowBorderSize = 1.0f;

    // 初始化后端
    if (!ImGui_ImplSDL2_InitForOpenGL(window, glContext)) {
        ImGui::DestroyContext();
        return false;
    }
    if (!ImGui_ImplOpenGL3_Init("#version 330 core")) {
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
        return false;
    }

    initialized_ = true;
    return true;
}

void DebugUI::Shutdown() {
    if (!initialized_) return;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    initialized_ = false;
}

void DebugUI::ProcessEvent(const SDL_Event& event) {
    if (!initialized_) return;
    ImGui_ImplSDL2_ProcessEvent(&event);
}

void DebugUI::BeginFrame(int windowWidth, int windowHeight) {
    if (!initialized_) return;
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    // 覆盖 DisplaySize（壁纸模式多窗口时，ImGui 绑定的是第一个窗口，
    // 需要手动设置当前渲染窗口的实际尺寸）
    if (windowWidth > 0 && windowHeight > 0) {
        ImGui::GetIO().DisplaySize = ImVec2(
            static_cast<float>(windowWidth), static_cast<float>(windowHeight));
    }
    ImGui::NewFrame();
}

void DebugUI::UpdateSmoothing(const DebugUIState& state) {
    constexpr float kSmooth = 0.05f;
    if (!smoothInited_) {
        smoothFPS_        = state.fps;
        smoothFrameTime_  = state.timeDelta * 1000.0f;
        smoothTimeDelta_  = state.timeDelta;
        smoothRenderTime_ = state.renderTime * 1000.0f;
        smoothInited_     = true;
    } else {
        smoothFPS_        += kSmooth * (state.fps - smoothFPS_);
        smoothFrameTime_  += kSmooth * (state.timeDelta * 1000.0f - smoothFrameTime_);
        smoothTimeDelta_  += kSmooth * (state.timeDelta - smoothTimeDelta_);
        smoothRenderTime_ += kSmooth * (state.renderTime * 1000.0f - smoothRenderTime_);
    }
}

void DebugUI::Render(DebugUIState& state) {
    if (!initialized_) return;

    UpdateSmoothing(state);

    if (visible_) {
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(380, 0), ImGuiCond_FirstUseEver);

        ImGui::Begin("Debug Panel", &visible_,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize);

        // ============================================================
        // 信息区
        // ============================================================
        if (ImGui::CollapsingHeader("Info", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("FPS: %.1f  (adaptive: %.1f, target: %d)",
                        smoothFPS_, state.adaptiveFPS, state.targetFPS);
            ImGui::Text("Frame Time: %.3f ms", smoothFrameTime_);
            ImGui::Text("RenderTime: %.3f ms", smoothRenderTime_);
            ImGui::Separator();
            ImGui::Text("Shader: %s", state.shaderPath);
            ImGui::Text("Status: %s",
                        (state.shaderError && state.shaderError[0] != '\0') ? "ERROR" : "OK");
            ImGui::Text("Mode:   %s", state.isMultiPass ? "Multi-Pass" : "Single-Pass");
            if (state.isMultiPass && !state.passNames.empty()) {
                ImGui::Text("Passes: ");
                ImGui::SameLine();
                for (size_t i = 0; i < state.passNames.size(); ++i) {
                    if (i > 0) ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "%s", state.passNames[i].c_str());
                    if (i < state.passNames.size() - 1) {
                        ImGui::SameLine();
                        ImGui::TextUnformatted("->");
                    }
                }
            }
            ImGui::Separator();
            ImGui::Text("iResolution: %.0f x %.0f", state.resolution[0], state.resolution[1]);
            ImGui::Text("iTime:      %.3f", state.currentTime);
            ImGui::Text("iFrame:     %d", state.frameCount);
            ImGui::Text("iMouse:     (%.0f, %.0f, %.0f, %.0f)",
                        state.mouse[0], state.mouse[1], state.mouse[2], state.mouse[3]);
        }

        // ============================================================
        // 控制区
        // ============================================================
        if (ImGui::CollapsingHeader("Controls", ImGuiTreeNodeFlags_DefaultOpen)) {
            // 重载 Shader
            if (ImGui::Button("Reload Shader (F5)")) {
                state.requestReload = true;
            }

            ImGui::SameLine();

            // 暂停/恢复
            if (state.paused) {
                if (ImGui::Button("Resume")) {
                    state.paused = false;
                }
            } else {
                if (ImGui::Button("Pause")) {
                    state.paused = true;
                }
            }

            ImGui::SameLine();

            // 重置时间
            if (ImGui::Button("Reset Time")) {
                state.requestResetTime = true;
            }

            // FPS 滑条
            ImGui::SliderInt("Target FPS", &state.targetFPS, 1, 120);

            // RenderScale 滑条
            ImGui::SliderFloat("Render Scale", &state.renderScale, 0.1f, 1.0f, "%.2f");
        }

        // ============================================================
        // Shader 选择器
        // ============================================================
        if (ImGui::CollapsingHeader("Shader Selector", ImGuiTreeNodeFlags_DefaultOpen)) {
            for (size_t i = 0; i < state.shaderFiles.size(); ++i) {
                const auto& file = state.shaderFiles[i];
                bool isCurrent = (state.shaderPath && file == state.shaderPath);

                if (isCurrent) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 1.0f, 0.4f, 1.0f));
                }

                // 只显示文件名部分（去掉 assets/shaders/ 前缀）
                std::string displayName = file;
                auto slashPos = file.find_last_of("/\\");
                if (slashPos != std::string::npos) {
                    displayName = file.substr(slashPos + 1);
                }

                if (isCurrent) {
                    displayName = "> " + displayName + " (current)";
                }

                if (ImGui::Selectable(displayName.c_str(), isCurrent)) {
                    if (!isCurrent) {
                        state.requestSwitchShader = file;
                    }
                }

                if (isCurrent) {
                    ImGui::PopStyleColor();
                }
            }
        }

        // ============================================================
        // 错误区
        // ============================================================
        if (state.shaderError && state.shaderError[0] != '\0') {
            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
            ImGui::TextWrapped("Shader Error:\n%s", state.shaderError);
            ImGui::PopStyleColor();
        }

        ImGui::End();
    }

    // 提交渲染
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void DebugUI::RenderOverlay(const DebugUIState& state) {
    if (!initialized_) return;

    UpdateSmoothing(state);

    // 固定右上角、无标题栏、不可交互的叠加窗口
    ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(displaySize.x - 10, 10), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.5f);
    ImGui::Begin("##DebugOverlay", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs |
                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);

    ImGui::Text("FPS: %.1f  (adaptive: %.1f, target: %d)",
                smoothFPS_, state.adaptiveFPS, state.targetFPS);
    ImGui::Text("Frame Time: %.3f ms", smoothFrameTime_);
    ImGui::Text("RenderTime: %.3f ms", smoothRenderTime_);
    ImGui::Separator();
    ImGui::Text("Shader: %s", state.shaderPath);
    ImGui::Text("Status: %s",
                (state.shaderError && state.shaderError[0] != '\0') ? "ERROR" : "OK");
    ImGui::Text("Mode:   %s", state.isMultiPass ? "Multi-Pass" : "Single-Pass");
    ImGui::Separator();
    ImGui::Text("iResolution: %.0f x %.0f", state.resolution[0], state.resolution[1]);
    ImGui::Text("iTime:      %.3f", state.currentTime);
    ImGui::Text("iFrame:     %d", state.frameCount);
    ImGui::Text("iMouse:     (%.0f, %.0f, %.0f, %.0f)",
                state.mouse[0], state.mouse[1], state.mouse[2], state.mouse[3]);

    // 错误信息
    if (state.shaderError && state.shaderError[0] != '\0') {
        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
        ImGui::TextWrapped("Shader Error:\n%s", state.shaderError);
        ImGui::PopStyleColor();
    }

    ImGui::End();

    // 提交渲染
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

bool DebugUI::WantCaptureMouse() const {
    if (!initialized_ || !visible_) return false;
    return ImGui::GetIO().WantCaptureMouse;
}

bool DebugUI::WantCaptureKeyboard() const {
    if (!initialized_ || !visible_) return false;
    return ImGui::GetIO().WantCaptureKeyboard;
}
