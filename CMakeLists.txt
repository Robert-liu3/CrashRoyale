cmake_minimum_required(VERSION 3.15)
project(CrashRoyale LANGUAGES CXX)
add_executable(
    ${PROJECT_NAME}
    src/main.cpp
    src/window_capture/imageUtils.cpp
    src/window_capture/duplicationManager.cpp
    src/window_capture/screenCaptureApp.cpp
)
target_link_libraries(CrashRoyale PRIVATE d3d11 dxgi)
