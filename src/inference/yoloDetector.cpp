#include "yoloDetector.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace {
// These IDs match ml/dataset.yaml and the current best.onnx export.
constexpr std::array<const wchar_t*, YoloDetector::ClassCount> ClassNames = {
    L"enemy_knight", L"friendly_knight", L"enemy_giant",
    L"friendly_giant", L"enemy_archers", L"friendly_archers"
};

void CheckHR(HRESULT hr, const char* operation) {
    if (FAILED(hr)) {
        std::ostringstream message;
        message << operation << " failed (HRESULT 0x" << std::hex << hr << ")";
        throw std::runtime_error(message.str());
    }
}

void CheckThresholds(float confidence, float iou) {
    if (!std::isfinite(confidence) || !std::isfinite(iou) ||
        confidence < 0 || confidence > 1 || iou < 0 || iou > 1) {
        throw std::invalid_argument("Confidence and IoU thresholds must be between 0 and 1.");
    }
}

struct Candidate {
    float left, top, right, bottom, confidence;
    int classId;
};

float IntersectionOverUnion(const Candidate& a, const Candidate& b) {
    const float width = std::max(0.0f, std::min(a.right, b.right) - std::max(a.left, b.left));
    const float height = std::max(0.0f, std::min(a.bottom, b.bottom) - std::max(a.top, b.top));
    const float intersection = width * height;
    const float area = (a.right - a.left) * (a.bottom - a.top) +
                       (b.right - b.left) * (b.bottom - b.top) - intersection;
    return area > 0 ? intersection / area : 0.0f;
}
}

YoloDetector::YoloDetector(const std::wstring& modelPath, float confidenceThreshold, float iouThreshold)
    : confidenceThreshold_(confidenceThreshold), iouThreshold_(iouThreshold) {
    CheckThresholds(confidenceThreshold, iouThreshold);
    Ort::SessionOptions options;
    options.SetIntraOpNumThreads(2);
    options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    session_ = Ort::Session(environment_, modelPath.c_str(), options);

    if (session_.GetInputCount() != 1 || session_.GetOutputCount() != 1) {
        throw std::runtime_error("Expected a detection model with one input and one output.");
    }
    // Keep the owning TypeInfo objects alive while inspecting their tensor views.
    auto inputType = session_.GetInputTypeInfo(0);
    auto inputInfo = inputType.GetTensorTypeAndShapeInfo();
    auto outputType = session_.GetOutputTypeInfo(0);
    auto outputInfo = outputType.GetTensorTypeAndShapeInfo();
    if (inputInfo.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
        inputInfo.GetShape() != std::vector<int64_t>{1, 3, InputSize, InputSize}) {
        throw std::runtime_error("Expected float32 input [1,3,640,640]. Export with imgsz=640 dynamic=False.");
    }
    const auto shape = outputInfo.GetShape();
    if (outputInfo.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
        shape.size() != 3 || shape[0] != 1 || shape[1] != 4 + ClassCount || shape[2] <= 0) {
        throw std::runtime_error("Expected raw float32 output [1,10,N] for six classes. Export with nms=None.");
    }
    candidateCount_ = static_cast<size_t>(shape[2]);
    Ort::AllocatorWithDefaultOptions allocator;
    inputName_ = session_.GetInputNameAllocated(0, allocator).get();
    outputName_ = session_.GetOutputNameAllocated(0, allocator).get();
}

LetterboxTransform YoloDetector::PrepareInput(const uint8_t* pixels, int width, int height,
                                             size_t rowPitch, std::vector<float>& input) {
    if (!pixels || width <= 0 || height <= 0 || rowPitch < static_cast<size_t>(width) * 4 ||
        static_cast<size_t>(height) > std::numeric_limits<size_t>::max() / rowPitch) {
        throw std::invalid_argument("Invalid BGRA image dimensions or row pitch.");
    }
    const double scale = std::min(double(InputSize) / width, double(InputSize) / height);
    const int resizedWidth = std::clamp(static_cast<int>(std::nearbyint(width * scale)), 1, InputSize);
    const int resizedHeight = std::clamp(static_cast<int>(std::nearbyint(height * scale)), 1, InputSize);
    LetterboxTransform transform{width, height, static_cast<float>(scale),
                                 (InputSize - resizedWidth) / 2, (InputSize - resizedHeight) / 2};
    constexpr size_t plane = InputSize * InputSize;
    input.assign(3 * plane, 114.0f / 255.0f);

    // Bilinear interpolation uses half-pixel centers, then quantizes to RGB bytes
    // before normalization, as the Python image pipeline does.
    for (int y = 0; y < resizedHeight; ++y) {
        const double sourceY = std::clamp((y + 0.5) * height / resizedHeight - 0.5, 0.0, double(height - 1));
        const int y0 = static_cast<int>(sourceY);
        const int y1 = std::min(y0 + 1, height - 1);
        const double fy = sourceY - y0;
        for (int x = 0; x < resizedWidth; ++x) {
            const double sourceX = std::clamp((x + 0.5) * width / resizedWidth - 0.5, 0.0, double(width - 1));
            const int x0 = static_cast<int>(sourceX);
            const int x1 = std::min(x0 + 1, width - 1);
            const double fx = sourceX - x0;
            const size_t destination = size_t(y + transform.padTop) * InputSize + x + transform.padLeft;
            for (int channel = 0; channel < 3; ++channel) {
                const int bgraChannel = 2 - channel;
                const double top = pixels[size_t(y0) * rowPitch + size_t(x0) * 4 + bgraChannel] * (1 - fx) +
                                   pixels[size_t(y0) * rowPitch + size_t(x1) * 4 + bgraChannel] * fx;
                const double bottom = pixels[size_t(y1) * rowPitch + size_t(x0) * 4 + bgraChannel] * (1 - fx) +
                                      pixels[size_t(y1) * rowPitch + size_t(x1) * 4 + bgraChannel] * fx;
                const float value = static_cast<float>(std::floor(top * (1 - fy) + bottom * fy + 0.5));
                input[size_t(channel) * plane + destination] = value / 255.0f;
            }
        }
    }
    return transform;
}

std::vector<DetectionBox> YoloDetector::DecodeOutput(const float* output, size_t count,
                                                    const LetterboxTransform& transform,
                                                    float confidenceThreshold, float iouThreshold) {
    CheckThresholds(confidenceThreshold, iouThreshold);
    if (!output || !std::isfinite(transform.scale) || transform.scale <= 0 ||
        transform.sourceWidth <= 0 || transform.sourceHeight <= 0) {
        throw std::invalid_argument("Invalid detection output or letterbox transform.");
    }
    std::vector<Candidate> candidates;
    for (size_t i = 0; i < count; ++i) {
        int classId = -1;
        float confidence = confidenceThreshold;
        for (int c = 0; c < ClassCount; ++c) {
            const float score = output[size_t(4 + c) * count + i];
            if (std::isfinite(score) && score > confidence && score <= 1.0f) {
                confidence = score;
                classId = c;
            }
        }
        if (classId < 0) continue;
        const float centerX = output[i], centerY = output[count + i];
        const float width = output[2 * count + i], height = output[3 * count + i];
        if (!std::isfinite(centerX) || !std::isfinite(centerY) || !std::isfinite(width) ||
            !std::isfinite(height) || width <= 0 || height <= 0) continue;
        candidates.push_back({centerX - width / 2, centerY - height / 2,
                              centerX + width / 2, centerY + height / 2, confidence, classId});
    }
    std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        return a.confidence > b.confidence;
    });

    // NMS happens in model coordinates, separately for each class, before clipping.
    std::vector<Candidate> kept;
    std::vector<DetectionBox> boxes;
    for (const auto& candidate : candidates) {
        const bool duplicate = std::any_of(kept.begin(), kept.end(), [&](const Candidate& previous) {
            return candidate.classId == previous.classId && IntersectionOverUnion(candidate, previous) > iouThreshold;
        });
        if (duplicate) continue;
        kept.push_back(candidate);
        const float left = std::clamp((candidate.left - transform.padLeft) / transform.scale,
                                      0.0f, float(transform.sourceWidth));
        const float top = std::clamp((candidate.top - transform.padTop) / transform.scale,
                                     0.0f, float(transform.sourceHeight));
        const float right = std::clamp((candidate.right - transform.padLeft) / transform.scale,
                                       0.0f, float(transform.sourceWidth));
        const float bottom = std::clamp((candidate.bottom - transform.padTop) / transform.scale,
                                        0.0f, float(transform.sourceHeight));
        if (right > left && bottom > top) {
            boxes.push_back({left, top, right - left, bottom - top, ClassNames[candidate.classId],
                             candidate.confidence, candidate.classId % 2 == 0 ? RGB(235, 65, 65) : RGB(40, 190, 240)});
        }
        if (kept.size() == 300) break;
    }
    return boxes;
}

std::vector<DetectionBox> YoloDetector::DetectBGRA(const uint8_t* pixels, int width, int height, size_t rowPitch) {
    const auto transform = PrepareInput(pixels, width, height, rowPitch, input_);
    const std::array<int64_t, 4> shape{1, 3, InputSize, InputSize};
    auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    auto tensor = Ort::Value::CreateTensor<float>(memory, input_.data(), input_.size(), shape.data(), shape.size());
    const char* inputNames[] = {inputName_.c_str()};
    const char* outputNames[] = {outputName_.c_str()};
    auto outputs = session_.Run(Ort::RunOptions{nullptr}, inputNames, &tensor, 1, outputNames, 1);
    return DecodeOutput(outputs.front().GetTensorData<float>(), candidateCount_, transform,
                        confidenceThreshold_, iouThreshold_);
}

std::vector<DetectionBox> YoloDetector::Detect(ID3D11Texture2D* texture, ID3D11DeviceContext* context) {
    if (!texture || !context) throw std::invalid_argument("Missing captured texture or D3D context.");
    D3D11_TEXTURE2D_DESC description{};
    texture->GetDesc(&description);
    if ((description.Format != DXGI_FORMAT_B8G8R8A8_UNORM && description.Format != DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) ||
        description.SampleDesc.Count != 1 || description.MipLevels != 1 || description.ArraySize != 1) {
        throw std::runtime_error("Expected a single BGRA desktop texture.");
    }
    D3D11_TEXTURE2D_DESC existing{};
    if (staging_) staging_->GetDesc(&existing);
    if (!staging_ || existing.Width != description.Width || existing.Height != description.Height ||
        existing.Format != description.Format) {
        auto stagingDescription = description;
        stagingDescription.Usage = D3D11_USAGE_STAGING;
        stagingDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stagingDescription.BindFlags = 0;
        stagingDescription.MiscFlags = 0;
        Microsoft::WRL::ComPtr<ID3D11Device> device;
        context->GetDevice(&device);
        CheckHR(device->CreateTexture2D(&stagingDescription, nullptr, staging_.ReleaseAndGetAddressOf()), "Create staging texture");
    }
    const size_t pitch = size_t(description.Width) * 4;
    pixels_.resize(pitch * description.Height);
    context->CopyResource(staging_.Get(), texture);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    CheckHR(context->Map(staging_.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Map captured texture");
    if (mapped.RowPitch < pitch) {
        context->Unmap(staging_.Get(), 0);
        throw std::runtime_error("Captured row pitch is smaller than the image width.");
    }
    for (UINT y = 0; y < description.Height; ++y) {
        std::memcpy(pixels_.data() + size_t(y) * pitch,
                    static_cast<const uint8_t*>(mapped.pData) + size_t(y) * mapped.RowPitch, pitch);
    }
    context->Unmap(staging_.Get(), 0);
    return DetectBGRA(pixels_.data(), static_cast<int>(description.Width), static_cast<int>(description.Height), pitch);
}
