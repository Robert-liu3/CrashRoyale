#include "overlayWindow.h"
#include <iostream>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

const wchar_t* CLASS_NAME = L"OverlayWindowClass";

bool OverlayWindow::Initialize() {
    m_hInstance = GetModuleHandle(nullptr);
    
    // Get screen dimensions
    m_screenWidth = GetSystemMetrics(SM_CXSCREEN);
    m_screenHeight = GetSystemMetrics(SM_CYSCREEN);
    
    if (!CreateOverlayWindow()) {
        return false;
    }
    
    if (!InitializeDirect2D()) {
        return false;
    }
    
    return true;
}

bool OverlayWindow::CreateOverlayWindow() {
    // Register window class
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = m_hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = nullptr; // No background
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    
    if (!RegisterClassW(&wc)) {
        DWORD error = GetLastError();
        if (error != ERROR_CLASS_ALREADY_EXISTS) {
            std::cerr << "Failed to register window class. Error: " << error << "\n";
            return false;
        }
    }
    
    // Create layered window
    m_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT, //WS_EX_TRANSPARENT
        CLASS_NAME,
        L"Overlay",
        WS_POPUP,
        0, 0, m_screenWidth, m_screenHeight,
        nullptr, nullptr, m_hInstance, this
    );
    
    if (!m_hwnd) {
        std::cerr << "Failed to create overlay window. Error: " << GetLastError() << "\n";
        return false;
    }
    
    // Make window transparent
    SetLayeredWindowAttributes(m_hwnd, RGB(0, 0, 0), 255, LWA_COLORKEY | LWA_ALPHA);

    //todo
    //semi transparent:

    // SetLayeredWindowAttributes(m_hwnd, 0, 150, LWA_ALPHA); 
    
    // Enable click-through
    SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, m_screenWidth, m_screenHeight, 
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    
    return true;
}

bool OverlayWindow::InitializeDirect2D() {
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_pD2DFactory);
    if (FAILED(hr)) {
        std::cerr << "Failed to create D2D factory. HRESULT=0x" << std::hex << hr << "\n";
        return false;
    }
    
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    
    D2D1_SIZE_U size = D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top);
    
    hr = m_pD2DFactory->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(),
        D2D1::HwndRenderTargetProperties(m_hwnd, size),
        &m_pRenderTarget
    );
    
    if (FAILED(hr)) {
        std::cerr << "Failed to create render target. HRESULT=0x" << std::hex << hr << "\n";
        return false;
    }
    
    // Create brush for drawing
    hr = m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Red), &m_pBrush);
    if (FAILED(hr)) {
        std::cerr << "Failed to create brush. HRESULT=0x" << std::hex << hr << "\n";
        return false;
    }
    
    return true;
}

void OverlayWindow::Show() {
    if (m_hwnd) {
        ShowWindow(m_hwnd, SW_SHOW);
        m_isVisible = true;
    }
}

void OverlayWindow::Hide() {
    if (m_hwnd) {
        ShowWindow(m_hwnd, SW_HIDE);
        m_isVisible = false;
    }
}

void OverlayWindow::UpdateDetections(const std::vector<DetectionBox>& detections) {
    {
        std::lock_guard<std::mutex> lock(m_detectionMutex);
        m_detections = detections;
    }
    m_needsRedraw = true;
    
    if (m_hwnd && m_isVisible) {
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void OverlayWindow::OnPaint() {
    if (!m_pRenderTarget) return;
    
    m_pRenderTarget->BeginDraw();
    // m_pRenderTarget->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f)); // Transparent
    m_pRenderTarget->Clear(D2D1::ColorF(0, 0, 0, 0)); // Transparent
    
    // Draw detection boxes
    {
        std::lock_guard<std::mutex> lock(m_detectionMutex);
        for (const auto& detection : m_detections) {
            DrawDetectionBox(detection);
        }
    }
    
    HRESULT hr = m_pRenderTarget->EndDraw();
    if (FAILED(hr)) {
        std::cerr << "Failed to end draw. HRESULT=0x" << std::hex << hr << "\n";
    }
    
    m_needsRedraw = false;
}

void OverlayWindow::DrawDetectionBox(const DetectionBox& box) {
    // Set brush color based on detection color
    BYTE r = GetRValue(box.color);
    BYTE g = GetGValue(box.color);
    BYTE b = GetBValue(box.color);
    m_pBrush->SetColor(D2D1::ColorF(r/255.0f, g/255.0f, b/255.0f, 0.8f));
    
    // Draw rectangle
    D2D1_RECT_F rect = D2D1::RectF(box.x, box.y, box.x + box.width, box.y + box.height);
    m_pRenderTarget->DrawRectangle(rect, m_pBrush, 2.0f);
    
    // Draw label background
    if (!box.label.empty()) {
        D2D1_RECT_F labelRect = D2D1::RectF(box.x, box.y - 25, box.x + box.width, box.y);
        m_pBrush->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.7f));
        m_pRenderTarget->FillRectangle(labelRect, m_pBrush);
        
        // Note: Text drawing would require DirectWrite, keeping it simple for now
    }
}

void OverlayWindow::MessageLoop() {
    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

LRESULT CALLBACK OverlayWindow::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    OverlayWindow* pThis = nullptr;
    
    if (uMsg == WM_NCCREATE) {
        CREATESTRUCT* pCreate = (CREATESTRUCT*)lParam;
        pThis = (OverlayWindow*)pCreate->lpCreateParams;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)pThis);
    } else {
        pThis = (OverlayWindow*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    }
    
    if (pThis) {
        switch (uMsg) {
        case WM_PAINT:
        {
            //todo
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            pThis->OnPaint();
            EndPaint(hwnd, &ps);
            // ValidateRect(hwnd, nullptr);
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }
    }
    
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void OverlayWindow::Cleanup() {
    if (m_pBrush) {
        m_pBrush->Release();
        m_pBrush = nullptr;
    }
    
    if (m_pRenderTarget) {
        m_pRenderTarget->Release();
        m_pRenderTarget = nullptr;
    }
    
    if (m_pD2DFactory) {
        m_pD2DFactory->Release();
        m_pD2DFactory = nullptr;
    }
    
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

void OverlayWindow::Close() {
    if (m_hwnd) {
        PostMessage(m_hwnd, WM_CLOSE, 0, 0);
    }
}
