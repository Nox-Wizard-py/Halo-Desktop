cmake_minimum_required(VERSION 3.20)

project(LumnOverlay LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
    add_compile_options(/W4 /permissive- /utf-8)
endif()

set(SOURCES
    src/main.cpp
    src/platform/Window.hpp
    src/platform/Window.cpp
    src/render/D3D11Renderer.hpp
    src/render/D3D11Renderer.cpp
    src/render/BackdropSampler.hpp
    src/render/BackdropSampler.cpp
    src/render/shaders/plasmaRim.hlsl
    src/render/shaders/liquidGlass.hlsl
    src/render/shaders/glassmorphism.hlsl
    src/audio/BassFilter.hpp
    src/audio/BassFilter.cpp
    src/audio/BeatDetector.hpp
    src/audio/BeatDetector.cpp
    src/audio/MusicSyncClock.hpp
    src/audio/MusicSyncClock.cpp
    src/audio/WasapiLoopback.hpp
    src/audio/WasapiLoopback.cpp
    src/media/ColorExtractor.hpp
    src/media/ColorExtractor.cpp
    src/media/MediaSessionWatcher.hpp
    src/media/MediaSessionWatcher.cpp
    src/ui/MediaCardRenderer.hpp
    src/ui/MediaCardRenderer.cpp
    src/ui/MediaCardWindow.hpp
    src/ui/MediaCardWindow.cpp
    src/settings/LightingSettings.hpp
    src/ui/LightingStudioWindow.hpp
    src/ui/LightingStudioWindow.cpp
    src/ui/AODWindow.hpp
    src/ui/AODWindow.cpp
)

add_executable(LumnOverlay WIN32 ${SOURCES})

target_include_directories(LumnOverlay PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    "C:/Program Files (x86)/Windows Kits/10/Include/10.0.26100.0/cppwinrt"
)

target_link_libraries(LumnOverlay PRIVATE
    d3d11
    dxgi
    dcomp
    d3dcompiler
    d2d1
    dwrite
    user32
    gdi32
    shell32
    advapi32
    avrt
    ole32
    windowsapp
    windowscodecs
    shlwapi
    dwmapi
    comctl32
    comdlg32
)

# Ensure plasmaRim.hlsl and liquidGlass.hlsl are copied alongside the built executable
add_custom_command(TARGET LumnOverlay POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/plasmaRim.hlsl"
        "$<TARGET_FILE_DIR:LumnOverlay>/plasmaRim.hlsl"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/liquidGlass.hlsl"
        "$<TARGET_FILE_DIR:LumnOverlay>/liquidGlass.hlsl"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/glassmorphism.hlsl"
        "$<TARGET_FILE_DIR:LumnOverlay>/glassmorphism.hlsl"
    COMMENT "Copying HLSL shaders to target output directory"
)

# Automated Test Suite Target
enable_testing()

add_executable(LumnVerification
    tests/test_verification.cpp
    src/ui/MediaCardRenderer.cpp
    src/ui/MediaCardWindow.cpp
    src/ui/LightingStudioWindow.cpp
    src/ui/AODWindow.cpp
    src/render/BackdropSampler.cpp
    src/media/MediaSessionWatcher.cpp
    src/media/ColorExtractor.cpp
    src/audio/MusicSyncClock.cpp
)

target_include_directories(LumnVerification PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    "C:/Program Files (x86)/Windows Kits/10/Include/10.0.26100.0/cppwinrt"
)

target_link_libraries(LumnVerification PRIVATE
    d3d11
    dxgi
    dcomp
    d3dcompiler
    d2d1
    dwrite
    user32
    gdi32
    shell32
    advapi32
    avrt
    ole32
    windowsapp
    windowscodecs
    shlwapi
    dwmapi
    comctl32
    comdlg32
)

add_test(NAME VerificationTest COMMAND LumnVerification)

add_custom_command(TARGET LumnVerification POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/liquidGlass.hlsl"
        "$<TARGET_FILE_DIR:LumnVerification>/liquidGlass.hlsl"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/glassmorphism.hlsl"
        "$<TARGET_FILE_DIR:LumnVerification>/glassmorphism.hlsl"
    COMMENT "Copying liquidGlass.hlsl to verification output directory"
)

# Kyant0 Liquid Glass Prototype Target
add_executable(TestKyantLiquidGlass
    tests/test_kyant_liquid_glass.cpp
)

target_link_libraries(TestKyantLiquidGlass PRIVATE
    d3d11
    dxgi
    d3dcompiler
)

add_test(NAME KyantLiquidGlassTest COMMAND TestKyantLiquidGlass)

add_custom_command(TARGET TestKyantLiquidGlass POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/shaders/kyantLiquidGlass.hlsl"
        "$<TARGET_FILE_DIR:TestKyantLiquidGlass>/kyantLiquidGlass.hlsl"
    COMMENT "Copying kyantLiquidGlass.hlsl to test output directory"
)

# Phase 4B.1: Interactive Liquid Glass Preview Target
add_executable(PreviewKyantLiquidGlass
    tests/preview_liquid_glass.cpp
    src/render/BackdropSampler.cpp
)

target_include_directories(PreviewKyantLiquidGlass PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(PreviewKyantLiquidGlass PRIVATE
    d3d11
    dxgi
    d3dcompiler
    d2d1
    dwrite
    user32
    gdi32
    dwmapi
)

add_custom_command(TARGET PreviewKyantLiquidGlass POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/shaders/kyantLiquidGlass.hlsl"
        "$<TARGET_FILE_DIR:PreviewKyantLiquidGlass>/kyantLiquidGlass.hlsl"
    COMMENT "Copying kyantLiquidGlass.hlsl to preview output directory"
)
