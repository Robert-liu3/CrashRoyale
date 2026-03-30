#include <wincodec.h>
#include "imageUtils.h"
#include <comdef.h>
#include <wrl/client.h>
#pragma comment(lib, "windowscodecs.lib")

HRESULT ImageUtils::SaveTextureAsPNG(ID3D11Texture2D* texture, ID3D11DeviceContext* context, const wchar_t* filename) {
    HRESULT hr = S_OK;
    
    // Initialize COM
    CoInitialize(nullptr);
    
    // Create WIC factory
    IWICImagingFactory* factory = nullptr;
    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                         IID_IWICImagingFactory, (void**)&factory);
    if (FAILED(hr)) return hr;

    // Create file stream
    IWICStream* stream = nullptr;
    hr = factory->CreateStream(&stream);
    if (FAILED(hr)) goto cleanup;

    hr = stream->InitializeFromFilename(filename, GENERIC_WRITE);
    if (FAILED(hr)) goto cleanup;

    // Create PNG encoder
    IWICBitmapEncoder* encoder = nullptr;
    hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (FAILED(hr)) goto cleanup;

    hr = encoder->Initialize(stream, WICBitmapEncoderNoCache);
    if (FAILED(hr)) goto cleanup;

    // Create frame encoder
    IWICBitmapFrameEncode* frameEncode = nullptr;
    IPropertyBag2* propertyBag = nullptr;
    hr = encoder->CreateNewFrame(&frameEncode, &propertyBag);
    if (FAILED(hr)) goto cleanup;

    hr = frameEncode->Initialize(propertyBag);
    if (FAILED(hr)) goto cleanup;

    // Get texture description
    D3D11_TEXTURE2D_DESC desc;
    texture->GetDesc(&desc);

    // Create staging texture for CPU access
    D3D11_TEXTURE2D_DESC stagingDesc = desc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.BindFlags = 0;
    stagingDesc.MiscFlags = 0;

    ID3D11Device* device = nullptr;
    context->GetDevice(&device);

    ID3D11Texture2D* stagingTexture = nullptr;
    hr = device->CreateTexture2D(&stagingDesc, nullptr, &stagingTexture);
    if (FAILED(hr)) goto cleanup;

    // Copy texture to staging
    context->CopyResource(stagingTexture, texture);

    // Map staging texture
    D3D11_MAPPED_SUBRESOURCE mapped;
    hr = context->Map(stagingTexture, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) goto cleanup;

    // Set frame size and pixel format
    hr = frameEncode->SetSize(desc.Width, desc.Height);
    if (FAILED(hr)) goto cleanup;

    WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat32bppBGRA;
    hr = frameEncode->SetPixelFormat(&pixelFormat);
    if (FAILED(hr)) goto cleanup;

    // Write pixels
    UINT stride = desc.Width * 4; // 4 bytes per pixel (BGRA)
    UINT bufferSize = stride * desc.Height;
    hr = frameEncode->WritePixels(desc.Height, stride, bufferSize, (BYTE*)mapped.pData);
    if (FAILED(hr)) goto cleanup;

    // Commit frame and encoder
    hr = frameEncode->Commit();
    if (FAILED(hr)) goto cleanup;

    hr = encoder->Commit();

    // Unmap texture
    context->Unmap(stagingTexture, 0);

cleanup:
    if (stagingTexture) stagingTexture->Release();
    if (device) device->Release();
    if (propertyBag) propertyBag->Release();
    if (frameEncode) frameEncode->Release();
    if (encoder) encoder->Release();
    if (stream) stream->Release();
    if (factory) factory->Release();
    
    CoUninitialize();
    return hr;
}