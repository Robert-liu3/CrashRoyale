#include <iostream>
#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include "windows/duplicationManager.h"

int main() {
    DUPLICATIONMANAGER dup;

    // 1. Create D3D11 device
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    D3D_FEATURE_LEVEL featureLevel;

    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        &device,
        &featureLevel,
        &context
    );
    if (FAILED(hr)) {
        std::cerr << "Failed to create D3D11 device\n";
        return -1;
    }

    // 2. Get DXGI output
    IDXGIDevice* dxgiDevice = nullptr;
    device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice);

    IDXGIAdapter* adapter = nullptr;
    dxgiDevice->GetAdapter(&adapter);

    IDXGIOutput* output = nullptr;
    adapter->EnumOutputs(0, &output); // primary monitor

    IDXGIOutput1* output1 = nullptr;
    output->QueryInterface(__uuidof(IDXGIOutput1), (void**)&output1);

    // 3. Create duplication
    hr = dup.Initialize(device);
    if (FAILED(hr)) {
        std::cerr << "Failed to initialize duplication\n";
        return -1;
    };

    // 4. Prepare frame data
    FRAME_DATA frame = {};
    hr = dup.GetFrame(&frame);

    if (FAILED(hr)) {
        std::cerr << "Failed to get frame. HRESULT=0x" << std::hex << hr << std::dec << "\n";
    } else {
        std::cout << "Frame captured successfully!\n";
        std::cout << "Move rects: " << frame.MoveCount << "\n";
        std::cout << "Dirty rects: " << frame.DirtyCount << "\n";

        if (frame.Frame) {
            D3D11_TEXTURE2D_DESC desc;
            frame.Frame->GetDesc(&desc);
            std::cout << "Frame size: " << desc.Width << "x" << desc.Height << "\n";
            std::cout << "Format: " << desc.Format << "\n";
        }
    }

    // TODO: process frame (e.g., read pixels)

    dup.DoneWithFrame();

    // cleanup
    dup.Cleanup();
    if (context) context->Release();
    if (device) device->Release();

    return 0;
}