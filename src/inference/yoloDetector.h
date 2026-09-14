#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <d3d11.h>
#include <wrl/client.h>
#include <onnxruntime_cxx_api.h>

#include "window_overlay/overlayWindow.h"

struct LetterboxTransform {
    int sourceWidth;
    int sourceHeight;
    float scale;
    int padLeft;
    int padTop;
};

// One instance belongs to one capture thread; the session and buffers are reused.
class YoloDetector {
public:
    static constexpr int InputSize = 640;
    static constexpr int ClassCount = 6;

    explicit YoloDetector(const std::wstring& modelPath,
                          float confidenceThreshold = 0.25f, float iouThreshold = 0.7f);

    std::vector<DetectionBox> Detect(ID3D11Texture2D* texture, ID3D11DeviceContext* context);
    std::vector<DetectionBox> DetectBGRA(const uint8_t* pixels, int width, int height, size_t rowPitch);

    static LetterboxTransform PrepareInput(const uint8_t* pixels, int width, int height,
                                          size_t rowPitch, std::vector<float>& input);
    static std::vector<DetectionBox> DecodeOutput(const float* output, size_t candidates,
                                                const LetterboxTransform& transform,
                                                float confidenceThreshold, float iouThreshold);

private:
    Ort::Env environment_{ORT_LOGGING_LEVEL_WARNING, "CrashRoyale"};
    Ort::Session session_{nullptr};
    std::string inputName_;
    std::string outputName_;
    size_t candidateCount_ = 0;
    float confidenceThreshold_;
    float iouThreshold_;
    std::vector<float> input_;
    std::vector<uint8_t> pixels_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging_;
};
