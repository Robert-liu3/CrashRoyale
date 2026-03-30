#pragma once
#include <d3d11.h>

class ImageUtils {
    public:
        static HRESULT SaveTextureAsPNG(ID3D11Texture2D* texture,
                                        ID3D11DeviceContext* context,
                                        const wchar_t* filename);
};