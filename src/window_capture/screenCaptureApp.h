#pragma once

#include <Windows.h>
#include <d3d11.h>
#include "./duplicationManager.h"

class ScreenCaptureApp {
public:
    bool Initialize();
    void Run();
    void Cleanup();

private:
    bool InitializeDevice();
    bool InitializeDuplication();
    void CaptureAndSaveFrame(int i);

private:
    DUPLICATIONMANAGER dup;

    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    D3D_FEATURE_LEVEL featureLevel{};

    IDXGIDevice* dxgiDevice = nullptr;
    IDXGIAdapter* adapter = nullptr;
    IDXGIOutput* output = nullptr;
    IDXGIOutput1* output1 = nullptr;
};