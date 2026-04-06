#include <Windows.h>          // Windows types and functions
#include <d3d11.h>            // D3D11 device creation
#include <dxgi1_2.h>          // DXGI duplication types / errors
#include <thread>             // std::thread
#include <atomic>             // std::atomic<bool>
#include <vector>             // std::vector
#include <iostream>           // std::cerr
#include <chrono>             // sleep_for
#include <cmath>              // std::sin

#include "window_overlay/overlayWindow.h"
#include "window_capture/duplicationManager.h"

#pragma comment(lib, "d3d11.lib")

int main() {
    // Create the overlay window object.
    OverlayWindow overlay;

    // Initialize the overlay window + Direct2D resources.
    if (!overlay.Initialize()) {
        std::cerr << "Failed to initialize overlay.\n";
        return -1;
    }

    // Show the overlay so it becomes visible.
    overlay.Show();


    // Start the overlay message loop on another thread because MessageLoop() blocks.
    std::thread captureThread([&overlay]() {
        ID3D11Device* device = nullptr;
        ID3D11DeviceContext* context = nullptr;

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
            overlay.Close();
            return;
        }

        // Create your desktop duplication manager object.
        DUPLICATIONMANAGER dupl;

        // Initialize duplication using the D3D11 device.
        hr = dupl.Initialize(device);
        if (FAILED(hr)) {
            std::cerr << "Failed to initialize duplication manager. HRESULT=0x"
                    << std::hex << hr << std::dec << "\n";

            if (context) context->Release();
            if (device) device->Release();
            overlay.Close();
            return;
        }

        // This counter is only for moving the demo box around a little.
        int frameCounter = 0;

        // Capture loop:
        // repeatedly grab a desktop frame, then update overlay detections.
        while (true) {
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
                overlay.Close();
                break;
            }

            // If any other error happened, stop.
            if (FAILED(hr)) {
                std::cerr << "GetFrame failed. HRESULT=0x"
                        << std::hex << hr << std::dec << "\n";
                overlay.Close();
                break;
            }

            // At this point frameData.Frame contains the captured desktop texture.
            // You would normally analyze frameData.Frame here and produce detections.

            std::vector<DetectionBox> boxes;

            // Demo box: move it horizontally over time just so you can see updates happen.
            DetectionBox box;
            box.x = 200.0f + 150.0f * std::sin(frameCounter * 0.05f);
            box.y = 200.0f;
            box.width = 250.0f;
            box.height = 150.0f;
            box.label = L"Demo Box";
            box.confidence = 1.0f;
            box.color = RGB(255, 0, 0);
            // box.x = 100.0f;          // Fixed top-left corner
            // box.y = 100.0f;
            // box.width = 400.0f;      // Large box
            // box.height = 300.0f;
            // box.label = L"TEST BOX";
            // box.confidence = 1.0f;
            // box.color = RGB(0, 255, 0);  // Bright green

            boxes.push_back(box);

            // Send the detections to the overlay for drawing.
            overlay.UpdateDetections(boxes);

            // Release the acquired duplication frame.
            hr = dupl.DoneWithFrame();
            if (FAILED(hr)) {
                std::cerr << "DoneWithFrame failed. HRESULT=0x"
                        << std::hex << hr << std::dec << "\n";
                overlay.Close();
                break;
            }

            // Advance our demo animation.
            frameCounter++;

            // Small sleep so this loop does not hammer the CPU too hard.
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }

        // Clean up duplication resources.
        dupl.Cleanup();

        // Release D3D11 objects.
        if (context) {
            context->Release();
            context = nullptr;
        }

        if (device) {
            device->Release();
            device = nullptr;
        }
    });

    overlay.MessageLoop();
    captureThread.join();

    overlay.Close();


    return 0;
}