cmake_minimum_required(VERSION 3.16)
# 兼容第三方库（glad/SDL2）旧版 cmake_minimum_required
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)
project(ShaderToyDesktop VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ============================================================
# 依赖：优先使用本地 third_party/ submodule，不存在则回退 FetchContent
# ============================================================
include(FetchContent)
set(THIRD_PARTY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

# --- SDL2 ---
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON  CACHE BOOL "" FORCE)
set(SDL_TEST   OFF CACHE BOOL "" FORCE)
if(EXISTS ${THIRD_PARTY_DIR}/SDL2/CMakeLists.txt)
    message(STATUS "SDL2: using local submodule")
    add_subdirectory(${THIRD_PARTY_DIR}/SDL2 ${CMAKE_BINARY_DIR}/_deps/sdl2-build)
else()
    message(STATUS "SDL2: FetchContent fallback")
    FetchContent_Declare(SDL2
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG        release-2.30.12
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(SDL2)
endif()

# --- GLAD (OpenGL 3.3 Core) ---
if(EXISTS ${THIRD_PARTY_DIR}/glad/CMakeLists.txt)
    message(STATUS "GLAD: using local submodule")
    add_subdirectory(${THIRD_PARTY_DIR}/glad ${CMAKE_BINARY_DIR}/_deps/glad-build)
else()
    message(STATUS "GLAD: FetchContent fallback")
    FetchContent_Declare(glad
        GIT_REPOSITORY https://github.com/Dav1dde/glad.git
        GIT_TAG        v0.1.36
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(glad)
endif()

# --- Dear ImGui (手动编译，无 CMakeLists.txt) ---
if(EXISTS ${THIRD_PARTY_DIR}/imgui/imgui.cpp)
    message(STATUS "ImGui: using local submodule")
    set(imgui_SOURCE_DIR ${THIRD_PARTY_DIR}/imgui)
else()
    message(STATUS "ImGui: FetchContent fallback")
    FetchContent_Declare(imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        v1.91.8-docking
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(imgui)
endif()

# --- nlohmann/json ---
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
if(EXISTS ${THIRD_PARTY_DIR}/json/CMakeLists.txt)
    message(STATUS "nlohmann/json: using local submodule")
    add_subdirectory(${THIRD_PARTY_DIR}/json ${CMAKE_BINARY_DIR}/_deps/nlohmann_json-build)
else()
    message(STATUS "nlohmann/json: FetchContent fallback")
    FetchContent_Declare(nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG        v3.11.3
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

# --- SPIRV-Cross 翻译管线（glslang + SPIRV-Cross） ---
option(USE_SPIRV_CROSS "Use glslang + SPIRV-Cross for GLSL->HLSL translation (OFF = regex fallback)" ON)
if(USE_SPIRV_CROSS)
    message(STATUS "SPIRV-Cross translation pipeline: ENABLED")

    # glslang — 关闭不需要的组件
    set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
    set(ENABLE_HLSL             OFF CACHE BOOL "" FORCE)  # glslang 自带的 HLSL 前端，我们不需要
    set(ENABLE_CTEST            OFF CACHE BOOL "" FORCE)
    set(ENABLE_OPT              OFF CACHE BOOL "" FORCE)  # SPIRV-Tools optimizer，不需要
    set(BUILD_TESTING           OFF CACHE BOOL "" FORCE)
    set(SKIP_GLSLANG_INSTALL    ON  CACHE BOOL "" FORCE)
    FetchContent_Declare(glslang
        GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
        GIT_TAG        16.2.0
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(glslang)

    # SPIRV-Cross — 只需要静态库，关闭 CLI/测试/共享库
    set(SPIRV_CROSS_CLI         OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_SHARED      OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_STATIC      ON  CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_MSL  OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_CPP  OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_REFLECT OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_C_API OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(spirv_cross
        GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Cross.git
        GIT_TAG        vulkan-sdk-1.4.304.0
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(spirv_cross)
else()
    message(STATUS "SPIRV-Cross translation pipeline: DISABLED (regex fallback)")
endif()

# ============================================================
# 主程序
# ============================================================

# Dear ImGui 源文件（核心 + SDL2/OpenGL3 后端）
set(IMGUI_SOURCES
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
)

set(SOURCES
    src/main.cpp
    src/renderer.cpp
    src/shader_manager.cpp
    src/wallpaper.cpp
    src/texture_manager.cpp
    src/multi_pass.cpp
    src/blit_renderer.cpp
    src/file_watcher.cpp
    src/tray_icon.cpp
    src/debug_ui.cpp
    src/shader_project.cpp
    src/file_dialog.cpp
    src/glsl_to_hlsl.cpp
    src/hlsl_fixup.cpp
    src/shadertoy_normalizer.cpp
    src/d3d11_renderer.cpp
    src/d3d11_shader_manager.cpp
    src/d3d11_texture_manager.cpp
    src/d3d11_multi_pass.cpp
    src/d3d11_blit_renderer.cpp
    src/stoy_parser.cpp
    src/stoy_hlsl_generator.cpp
    src/app.rc
    ${IMGUI_SOURCES}
)

set(HEADERS
    src/renderer.h
    src/shader_manager.h
    src/wallpaper.h
    src/texture_manager.h
    src/multi_pass.h
    src/blit_renderer.h
    src/file_watcher.h
    src/tray_icon.h
    src/debug_ui.h
    src/shader_project.h
    src/file_dialog.h
    src/stb_image.h
    src/glsl_to_hlsl.h
    src/d3d11_renderer.h
    src/d3d11_shader_manager.h
    src/d3d11_texture_manager.h
    src/d3d11_multi_pass.h
    src/d3d11_blit_renderer.h
    src/stoy_parser.h
    src/stoy_hlsl_generator.h
)

add_executable(${PROJECT_NAME} WIN32 ${SOURCES} ${HEADERS})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

# SPIRV-Cross 管线 include 路径
if(USE_SPIRV_CROSS)
    target_include_directories(${PROJECT_NAME} PRIVATE
        ${glslang_SOURCE_DIR}        # glslang/Public/ShaderLang.h, SPIRV/GlslangToSpv.h
        ${spirv_cross_SOURCE_DIR}    # spirv_hlsl.hpp
    )
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE
    SDL2::SDL2-static
    SDL2::SDL2main
    glad
    opengl32
    nlohmann_json::nlohmann_json
    dwmapi
    ole32
    shell32
    uuid
    d3d11
    dxgi
    d3dcompiler
)

# SPIRV-Cross 翻译管线链接
if(USE_SPIRV_CROSS)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        glslang
        SPIRV
        spirv-cross-core
        spirv-cross-hlsl
        glslang-default-resource-limits
    )
    target_compile_definitions(${PROJECT_NAME} PRIVATE USE_SPIRV_CROSS=1)
endif()

# MSVC: 禁用 C4005（APIENTRY 宏重定义，GLAD 与 Windows SDK 冲突）
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /wd4005)
endif()

# 复制 assets 到构建目录
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/assets
    $<TARGET_FILE_DIR:${PROJECT_NAME}>/assets
)
