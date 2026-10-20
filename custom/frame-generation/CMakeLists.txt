cmake_minimum_required(VERSION 3.24)
project(openmw_fg LANGUAGES C CXX)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "openmw-fg must be built for x64 (OpenMW is 64-bit)")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
    # Static CRT so the DLL has no extra runtime deps beyond what OpenMW ships.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(FetchContent)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.8
)
if(WIN32)
    FetchContent_Declare(
        minhook
        GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
        GIT_TAG        v1.3.4
    )
    FetchContent_MakeAvailable(minhook imgui)
else()
    FetchContent_MakeAvailable(imgui)
endif()

set(FG_SHARED_SOURCES
    src/log.cpp
    src/config.cpp
    src/gl_min.cpp
    src/fg/framegen.cpp
)
set(FG_IMGUI_CORE
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

if(WIN32)
    add_library(openmw_fg SHARED
        ${FG_SHARED_SOURCES}
        src/dllmain.cpp
        src/version_proxy.cpp
        src/hooks.cpp
        src/win/platform_win.cpp
        src/fg/overlay.cpp
        src/version.def
        ${FG_IMGUI_CORE}
        ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    )
    target_compile_definitions(openmw_fg PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX IMGUI_IMPL_WIN32_DISABLE_GAMEPAD)
    target_link_libraries(openmw_fg PRIVATE minhook opengl32 gdi32 user32 winmm dwmapi shell32 ole32)
    if(MINGW)
        # Keep the cross-built DLL standalone, like the MSVC static-CRT build.
        target_link_options(openmw_fg PRIVATE -static)
    endif()
    # Ships as version.dll next to openmw.exe (SDL2.dll imports VERSION.dll).
    set_target_properties(openmw_fg PROPERTIES OUTPUT_NAME "version" PREFIX "")
else()
    add_library(openmw_fg SHARED
        ${FG_SHARED_SOURCES}
        src/linux/so_init.cpp
        src/linux/hooks_linux.cpp
        src/linux/platform_linux.cpp
        src/fg/overlay_linux.cpp
        ${FG_IMGUI_CORE}
    )
    # Only the interposer symbols (marked visibility("default")) may be
    # visible — anything else could shadow the game's own libraries.
    target_compile_options(openmw_fg PRIVATE -fvisibility=hidden -fvisibility-inlines-hidden)
    # Static libstdc++/libgcc for portability across distros (parallel to the
    # static CRT on Windows); exclude-libs keeps their symbols private.
    target_link_options(openmw_fg PRIVATE -static-libstdc++ -static-libgcc -Wl,--exclude-libs,ALL)
    # SDL2 is deliberately NOT linked: the game provides it, all SDL calls go
    # through dlsym. Only the SDL2 headers are needed at build time.
    target_link_libraries(openmw_fg PRIVATE GL X11 dl pthread)
    # Ships as libopenmw_fg.so, loaded via LD_PRELOAD (see packaging/openmw-fg.sh).
    set_target_properties(openmw_fg PROPERTIES OUTPUT_NAME "openmw_fg")
endif()

target_include_directories(openmw_fg PRIVATE src ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
