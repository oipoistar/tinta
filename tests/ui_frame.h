#pragma once
// What the interface's surface tests share: a surface drawn through the
// window's own context and read back as pixels, and a few measures.
#include "app.h"

#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace uiframe {

inline bool about(float a, float b) { return std::fabs(a - b) < 0.51f; }
inline float width(const D2D1_RECT_F& r) { return r.right - r.left; }
inline float height(const D2D1_RECT_F& r) { return r.bottom - r.top; }
inline bool inside(const D2D1_RECT_F& in, const D2D1_RECT_F& out) {
    return in.left >= out.left - 0.5f && in.right <= out.right + 0.5f && in.top >= out.top - 0.5f &&
           in.bottom <= out.bottom + 0.5f;
}
inline bool overlap(const D2D1_RECT_F& a, const D2D1_RECT_F& b) {
    return a.left < b.right - 0.5f && b.left < a.right - 0.5f && a.top < b.bottom - 0.5f &&
           b.top < a.bottom - 0.5f;
}
// A length that is a step of the scale's base of 4
inline bool onGrid(const App& app, float length) {
    const float units = length / app.contentScale / 4.0f;
    return std::fabs(units - std::round(units)) < 0.02f;
}
inline LPARAM centre(const D2D1_RECT_F& r) {
    return MAKELPARAM((int)((r.left + r.right) / 2), (int)((r.top + r.bottom) / 2));
}

// A drawn frame, in pixels (BGRA)
struct Frame {
    UINT width = 0, height = 0;
    std::vector<BYTE> pixels;
    bool empty() const { return pixels.empty(); }
    D2D1_COLOR_F at(float x, float y) const {
        const UINT px = std::min(width - 1, (UINT)std::max(0.0f, x));
        const UINT py = std::min(height - 1, (UINT)std::max(0.0f, y));
        const BYTE* p = pixels.data() + ((size_t)py * width + px) * 4;
        return D2D1::ColorF(p[2] / 255.0f, p[1] / 255.0f, p[0] / 255.0f);
    }
    // Whether anything in the rectangle differs between two frames
    bool differs(const Frame& other, const D2D1_RECT_F& r) const {
        if (pixels.size() != other.pixels.size()) return true;
        for (int y = std::max(0, (int)r.top); y < std::min((int)height, (int)r.bottom); y++)
            for (int x = std::max(0, (int)r.left); x < std::min((int)width, (int)r.right); x++) {
                const size_t i = ((size_t)y * width + x) * 4;
                if (pixels[i] != other.pixels[i] || pixels[i + 1] != other.pixels[i + 1] ||
                    pixels[i + 2] != other.pixels[i + 2]) {
                    return true;
                }
            }
        return false;
    }
};

// `paint` drawn over the theme's page colour at the app's size
inline Frame draw(App& app, const std::function<void()>& paint) {
    using Microsoft::WRL::ComPtr;
    Frame frame;
    ComPtr<ID2D1DeviceContext> dc;
    ComPtr<ID2D1Bitmap1> target, readable;
    const auto format = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE);
    const D2D1_SIZE_U size = D2D1::SizeU((UINT)app.width, (UINT)app.height);
    if (FAILED(app.renderTarget->QueryInterface(IID_PPV_ARGS(&dc))) ||
        FAILED(dc->CreateBitmap(size, nullptr, 0, D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET, format),
                                &target)) ||
        FAILED(dc->CreateBitmap(size, nullptr, 0,
                                D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ |
                                                            D2D1_BITMAP_OPTIONS_CANNOT_DRAW, format),
                                &readable))) {
        return frame;
    }
    ComPtr<ID2D1Image> previous;
    dc->GetTarget(&previous);
    dc->SetTarget(target.Get());
    dc->BeginDraw();
    dc->Clear(app.theme.background);
    paint();
    const bool drawn = SUCCEEDED(dc->EndDraw());
    dc->SetTarget(previous.Get());
    D2D1_MAPPED_RECT mapped{};
    if (!drawn || FAILED(readable->CopyFromBitmap(nullptr, target.Get(), nullptr)) ||
        FAILED(readable->Map(D2D1_MAP_OPTIONS_READ, &mapped))) {
        return frame;
    }
    frame.width = size.width;
    frame.height = size.height;
    for (UINT y = 0; y < size.height; ++y)
        frame.pixels.insert(frame.pixels.end(), mapped.bits + y * mapped.pitch,
                            mapped.bits + y * mapped.pitch + size.width * 4);
    readable->Unmap();
    return frame;
}

// The frame as a PNG, for looking at
inline bool save(App& app, const Frame& frame, const std::wstring& path) {
    using Microsoft::WRL::ComPtr;
    if (frame.empty() || !app.wicFactory) return false;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> out;
    // PNG takes the pixels with their alpha, which the frame does not carry
    std::vector<BYTE> opaque = frame.pixels;
    for (size_t i = 3; i < opaque.size(); i += 4) opaque[i] = 255;
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    return SUCCEEDED(app.wicFactory->CreateStream(&stream)) &&
           SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) &&
           SUCCEEDED(app.wicFactory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
           SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) &&
           SUCCEEDED(encoder->CreateNewFrame(&out, nullptr)) && SUCCEEDED(out->Initialize(nullptr)) &&
           SUCCEEDED(out->SetSize(frame.width, frame.height)) && SUCCEEDED(out->SetPixelFormat(&format)) &&
           IsEqualGUID(format, GUID_WICPixelFormat32bppBGRA) &&
           SUCCEEDED(out->WritePixels(frame.height, frame.width * 4, (UINT)opaque.size(), opaque.data())) &&
           SUCCEEDED(out->Commit()) && SUCCEEDED(encoder->Commit());
}

inline float luminance(D2D1_COLOR_F c) {
    auto lin = [](float v) { return v <= 0.03928f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); };
    return 0.2126f * lin(c.r) + 0.7152f * lin(c.g) + 0.0722f * lin(c.b);
}
inline float contrast(D2D1_COLOR_F a, D2D1_COLOR_F b) {
    const float la = luminance(a), lb = luminance(b);
    return (std::max(la, lb) + 0.05f) / (std::min(la, lb) + 0.05f);
}

}  // namespace uiframe
