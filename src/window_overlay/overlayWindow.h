#pragma once

#include <Windows.h>
#include <d2d1.h>
#include <vector>
#include <string>
#pragma comment(lib, "d2d1.lib")
#include <mutex>

struct DetectionBox {
    float x, y, width, height;
    std::wstring label;
    float confidence;
    COLORREF color;
};

class OverlayWindow {
public:
    bool Initialize();
    void Show();
    void Hide();
    void Close();
    void UpdateDetections(const std::vector<DetectionBox>& detections);
    void Cleanup();
    
    // Message loop - call this in a separate thread
    void MessageLoop();
    
private:
    bool CreateOverlayWindow();
    bool InitializeDirect2D();
    void OnPaint();
    void DrawDetectionBox(const DetectionBox& box);
    
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    
private:
    HWND m_hwnd = nullptr;
    HINSTANCE m_hInstance = nullptr;
    
    // Direct2D resources
    ID2D1Factory* m_pD2DFactory = nullptr;
    ID2D1HwndRenderTarget* m_pRenderTarget = nullptr;
    ID2D1SolidColorBrush* m_pBrush = nullptr;
    
    // Detection data
    std::vector<DetectionBox> m_detections;
    std::mutex m_detectionMutex; 
    bool m_needsRedraw = false;
    
    // Window properties
    int m_screenWidth = 0;
    int m_screenHeight = 0;
    bool m_isVisible = false;
};
