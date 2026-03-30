// screenCaptureApp.cpp

#include "screenCaptureApp.h"

#include <iostream>
#include <string>
#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>

#include "./imageUtils.h"

bool ScreenCaptureApp::Initialize() {
    if (!InitializeDevice()) {
        return false;
    }

    if (!InitializeDuplication()) {
        return false;
    }

    return true;
}

bool ScreenCaptureApp::InitializeDevice() {
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
        std::cerr << "Failed to create D3D11 device. HRESULT=0x"
                  << std::hex << hr << std::dec << "\n";
        return false;
    }

    hr = device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice);
    if (FAILED(hr)) {
        std::cerr << "Failed to get IDXGIDevice. HRESULT=0x"
                  << std::hex << hr << std::dec << "\n";
        return false;
    }

    hr = dxgiDevice->GetAdapter(&adapter);
    if (FAILED(hr)) {
        std::cerr << "Failed to get IDXGIAdapter. HRESULT=0x"
                  << std::hex << hr << std::dec << "\n";
        return false;
    }

    hr = adapter->EnumOutputs(0, &output); // primary monitor
    if (FAILED(hr)) {
        std::cerr << "Failed to get IDXGIOutput. HRESULT=0x"
                  << std::hex << hr << std::dec << "\n";
        return false;
    }

    hr = output->QueryInterface(__uuidof(IDXGIOutput1), (void**)&output1);
    if (FAILED(hr)) {
        std::cerr << "Failed to get IDXGIOutput1. HRESULT=0x"
                  << std::hex << hr << std::dec << "\n";
        return false;
    }

    return true;
}

bool ScreenCaptureApp::InitializeDuplication() {
    HRESULT hr = dup.Initialize(device);
    if (FAILED(hr)) {
        std::cerr << "Failed to initialize duplication. HRESULT=0x"
                  << std::hex << hr << std::dec << "\n";
        return false;
    }

    return true;
}

void ScreenCaptureApp::Run() {
    for (int i = 0; i < 3; i++) {
        CaptureAndSaveFrame(i);
    }
}

void ScreenCaptureApp::CaptureAndSaveFrame(int i) {
    FRAME_DATA frame = {};
    HRESULT hr = dup.GetFrame(&frame);

    if (FAILED(hr)) {
        std::cerr << "Failed to get frame. HRESULT=0x"
                  << std::hex << hr << std::dec << "\n";
        return;
    }

    if (frame.Frame) {
        D3D11_TEXTURE2D_DESC desc;
        frame.Frame->GetDesc(&desc);

        std::cout << "Frame captured successfully!\n";
        std::cout << "Frame size: " << desc.Width << "x" << desc.Height << "\n";
        std::cout << "Format: " << desc.Format << "\n";

        std::wstring filename = std::wstring(L"screenshot_")
                              + std::to_wstring(i)
                              + L".png";

        hr = ImageUtils::SaveTextureAsPNG(frame.Frame, context, filename.c_str());
        if (SUCCEEDED(hr)) {
            std::wcout << L"Saved " << filename << L"\n";
        } else {
            std::cerr << "Failed to save screenshot. HRESULT=0x"
                      << std::hex << hr << std::dec << "\n";
        }
    } else {
        std::cerr << "Frame was acquired, but frame.Frame is null.\n";
    }

    dup.DoneWithFrame();
}

void ScreenCaptureApp::Cleanup() {
    dup.Cleanup();

    if (output1) {
        output1->Release();
        output1 = nullptr;
    }

    if (output) {
        output->Release();
        output = nullptr;
    }

    if (adapter) {
        adapter->Release();
        adapter = nullptr;
    }

    if (dxgiDevice) {
        dxgiDevice->Release();
        dxgiDevice = nullptr;
    }

    if (context) {
        context->Release();
        context = nullptr;
    }

    if (device) {
        device->Release();
        device = nullptr;
    }
}