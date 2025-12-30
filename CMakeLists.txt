cmake_minimum_required(VERSION 3.15)
project(CrashRoyale LANGUAGES CXX)
add_executable(
    ${PROJECT_NAME}
    src/main.cpp
    src/windows/duplicationManager.cpp
)
target_link_libraries(CrashRoyale PRIVATE d3d11 dxgi)
