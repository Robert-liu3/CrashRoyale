cmake_minimum_required(VERSION 3.15)
project(CrashRoyale LANGUAGES CXX)
add_executable(
    ${PROJECT_NAME}
    src/main.cpp
    src/window_capture/imageUtils.cpp
    src/window_capture/duplicationManager.cpp
    src/window_capture/screenCaptureApp.cpp
    src/window_overlay/overlayWindow.cpp
)
target_compile_definitions(CrashRoyale PRIVATE
    "CRASHROYALE_SOURCE_DIR=L\"${CMAKE_CURRENT_SOURCE_DIR}\""
)
target_link_libraries(CrashRoyale PRIVATE d3d11 dxgi d2d1 dwmapi windowscodecs)
