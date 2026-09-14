#include <Windows.h>          // Windows types and functions
#include <d3d11.h>            // D3D11 device creation
#include <dxgi1_2.h>          // DXGI duplication types / errors
#include <thread>             // std::thread
#include <atomic>             // std::atomic<bool>
#include <vector>             // std::vector
#include <iostream>           // std::cerr
#include <chrono>             // sleep_for
#include <string>             // std::wstring
#include <memory>
#include <wrl/client.h>

#include "window_overlay/overlayWindow.h"
#include "window_capture/duplicationManager.h"
#include "window_capture/imageUtils.h"
#include "inference/yoloDetector.h"

#pragma comment(lib, "d3d11.lib")

const std::wstring datasetDirectory = std::wstring(CRASHROYALE_SOURCE_DIR) + L"/dataset";
const std::wstring screenshotDirectory = datasetDirectory + L"/raw";

bool EnsureDirectoryExists(const wchar_t* path) {
    if (CreateDirectoryW(path, nullptr)) {
        return true;
    }

    return GetLastError() == ERROR_ALREADY_EXISTS;
}

bool EnsureScreenshotFolderExists() {
    return EnsureDirectoryExists(datasetDirectory.c_str()) &&
           EnsureDirectoryExists(screenshotDirectory.c_str());
}

std::wstring BuildScreenshotPath(int screenshotIndex) {
    return screenshotDirectory + L"/screenshot_" +
           std::to_wstring(screenshotIndex) +
           L".png";
}

bool FileExists(const std::wstring& path) {
    DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

int FindNextScreenshotIndex() {
    int screenshotIndex = 0;
    while (FileExists(BuildScreenshotPath(screenshotIndex))) {
        screenshotIndex++;
    }

    return screenshotIndex;
}

int main() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const std::wstring modelPath = std::wstring(CRASHROYALE_SOURCE_DIR) +
                                  L"/ml/runs/clashroyale/weights/best.onnx";
    std::unique_ptr<YoloDetector> detector;
    try {
        detector = std::make_unique<YoloDetector>(modelPath);
        std::wcout << L"Loaded " << modelPath << L"\n";
    } catch (const std::exception& error) {
        std::cerr << "Could not load detector: " << error.what() << "\n";
        return 1;
    }
    if (!EnsureScreenshotFolderExists()) {
        std::cerr << "Failed to create screenshot folder. Error: " << GetLastError() << "\n";
        return 1;
    }
    // Create the overlay window object.
    OverlayWindow overlay;

    // Initialize the overlay window + Direct2D resources.
    if (!overlay.Initialize()) {
        std::cerr << "Failed to initialize overlay.\n";
        overlay.Cleanup();
        return -1;
    }

    // Show the overlay so it becomes visible.
    overlay.Show();


    std::atomic<bool> running{true};
    std::atomic<int> exitCode{0};
    // The UI stays on the main thread; capture and inference share this worker.
    std::thread captureThread([&]() {
        Microsoft::WRL::ComPtr<ID3D11Device> device;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;

        // Create a D3D11 device for desktop duplication.
        HRESULT hr = D3D11CreateDevice(
            nullptr,                    // use default adapter
            D3D_DRIVER_TYPE_HARDWARE,   // use GPU hardware
            nullptr,                    // no software rasterizer
            0,                          // no special device flags
            nullptr,                    // default feature level list
            0,                          // number of feature levels in list
            D3D11_SDK_VERSION,          // SDK version
            &device,                    // created device comes out here
            nullptr,                    // ignore chosen feature level
            &context                    // created immediate context comes out here
        );

        // Stop if the D3D11 device could not be created.
        if (FAILED(hr)) {
            std::cerr << "Failed to create D3D11 device. HRESULT=0x"
                    << std::hex << hr << std::dec << "\n";
            exitCode = 1;
            overlay.Close();
            return;
        }

        // Create your desktop duplication manager object.
        DUPLICATIONMANAGER dupl;

        // Initialize duplication using the D3D11 device.
        hr = dupl.Initialize(device.Get());
        if (FAILED(hr)) {
            std::cerr << "Failed to initialize duplication manager. HRESULT=0x"
                    << std::hex << hr << std::dec << "\n";

            exitCode = 1;
            overlay.Close();
            return;
        }

        int screenshotCounter = FindNextScreenshotIndex();
        bool wasSKeyDown = false;

        // Capture loop:
        // repeatedly grab a desktop frame, then update overlay detections.
        while (running) {
            // If ESC is pressed, leave the loop.
            if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
                overlay.Close();
                break;
            }

            // FRAME_DATA is assumed to be defined in duplicationManager.h.
            FRAME_DATA frameData = {};

            // Try to acquire the next desktop frame.
            hr = dupl.GetFrame(&frameData);

            // Timeout is normal when no frame arrives within the wait period.
            if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            // If duplication access was lost, stop for now.
            if (hr == DXGI_ERROR_ACCESS_LOST) {
                std::cerr << "Desktop duplication access lost.\n";
                exitCode = 1;
                overlay.Close();
                break;
            }

            // If any other error happened, stop.
            if (FAILED(hr)) {
                std::cerr << "GetFrame failed. HRESULT=0x"
                        << std::hex << hr << std::dec << "\n";
                exitCode = 1;
                overlay.Close();
                break;
            }

            try {
                if (!frameData.Frame) throw std::runtime_error("Capture returned no texture.");

                /*SCREENSHOT BUTTON S TEST*/
                
                // bool isSKeyDown = (GetAsyncKeyState('S') & 0x8000) != 0;
                // if (isSKeyDown && !wasSKeyDown) {
                //     std::wstring filename = BuildScreenshotPath(screenshotCounter++);
                //     HRESULT saveHr = ImageUtils::SaveTextureAsPNG(frameData.Frame, context.Get(), filename.c_str());
                //     if (SUCCEEDED(saveHr)) {
                //         std::wcout << L"Saved " << filename << L"\n";
                //     } else {
                //         std::cerr << "Failed to save screenshot. HRESULT=0x"
                //                   << std::hex << saveHr << std::dec << "\n";
                //     }
                // }
                // wasSKeyDown = isSKeyDown;

                // Prepare pixels, run ONNX, filter boxes, and return screen coordinates.
                const auto boxes = detector->Detect(frameData.Frame, context.Get());
                overlay.UpdateDetections(boxes);
            } catch (const std::exception& error) {
                std::cerr << "Detection failed: " << error.what() << "\n";
                exitCode = 1;
                running = false;
            }

            // Release the acquired duplication frame.
            hr = dupl.DoneWithFrame();
            if (FAILED(hr)) {
                std::cerr << "DoneWithFrame failed. HRESULT=0x"
                        << std::hex << hr << std::dec << "\n";
                exitCode = 1;
                overlay.Close();
                break;
            }

            // Small sleep so this loop does not hammer the CPU too hard.
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }

        // Clean up duplication resources.
        dupl.Cleanup();

        overlay.Close();
    });

    overlay.MessageLoop();
    running = false;
    captureThread.join();
    overlay.Cleanup();
    return exitCode;
}
