cmake_minimum_required(VERSION 3.15)
project(CrashRoyale LANGUAGES CXX)
add_executable(
    ${PROJECT_NAME}
    src/main.cpp
    src/inference/yoloDetector.cpp
    src/window_capture/imageUtils.cpp
    src/window_capture/duplicationManager.cpp
    src/window_capture/screenCaptureApp.cpp
    src/window_overlay/overlayWindow.cpp
)
set(ORT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime")
target_compile_features(CrashRoyale PRIVATE cxx_std_17)
target_include_directories(CrashRoyale PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${ORT_DIR}/include")
target_link_libraries(CrashRoyale PRIVATE "${ORT_DIR}/lib/onnxruntime.lib")

add_custom_command(TARGET CrashRoyale POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ORT_DIR}/lib/onnxruntime.dll"
        "$<TARGET_FILE_DIR:CrashRoyale>"
    VERBATIM
)
target_compile_definitions(CrashRoyale PRIVATE
    NOMINMAX
    "CRASHROYALE_SOURCE_DIR=L\"${CMAKE_CURRENT_SOURCE_DIR}\""
)
target_link_libraries(CrashRoyale PRIVATE d3d11 dxgi d2d1 dwrite dwmapi windowscodecs)
