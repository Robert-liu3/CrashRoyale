#pragma once

#include <Windows.h>
#include <dxgi1_2.h>
#include <d3d11.h>

struct FRAME_DATA
{
    ID3D11Texture2D* Frame = nullptr;
    DXGI_OUTDUPL_FRAME_INFO FrameInfo{};
    BYTE* MetaData = nullptr;
    UINT MoveCount = 0;
    UINT DirtyCount = 0;
};

class DUPLICATIONMANAGER
{
public:
    HRESULT Initialize(ID3D11Device* device);
    HRESULT GetFrame(FRAME_DATA* Data);
    HRESULT DoneWithFrame();
    void Cleanup();

private:
    IDXGIOutputDuplication* DeskDupl = nullptr;
    ID3D11Texture2D* AcquiredDesktopImage = nullptr;

    BYTE* MetaDataBuffer = nullptr;
    UINT MetaDataSize = 0;
};
