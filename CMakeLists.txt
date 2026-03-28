cmake_minimum_required(VERSION 3.16)
project(ShaderToyDesktop VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================
# 依赖：使用 FetchContent 自动下载 SDL2 和 GLAD
# ============================================================
include(FetchContent)

# SDL2
FetchContent_Declare(
    SDL2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-2.30.12
    GIT_SHALLOW    TRUE
)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON  CACHE BOOL "" FORCE)
set(SDL_TEST   OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(SDL2)

# GLAD (OpenGL 3.3 Core)
FetchContent_Declare(
    glad
    GIT_REPOSITORY https://github.com/Dav1dde/glad.git
    GIT_TAG        v0.1.36
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(glad)

# Dear ImGui
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.8-docking
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imgui)

# nlohmann/json (header-only JSON 解析库，用于 ShaderToy JSON 导入)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(nlohmann_json)

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
    src/stb_image.h
)

add_executable(${PROJECT_NAME} WIN32 ${SOURCES} ${HEADERS})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    SDL2::SDL2-static
    SDL2::SDL2main
    glad
    opengl32
    nlohmann_json::nlohmann_json
    dwmapi
)

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
