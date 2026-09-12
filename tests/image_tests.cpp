#include "d2d_init.h"
#include "image_loader.h"
#include "overlays.h"
#include "print.h"
#include "render.h"

#include <filesystem>
#include <climits>
#include <fstream>
#include <iostream>
#include <memory>

namespace {
using Microsoft::WRL::ComPtr;
int failures = 0;
void check(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}

bool writeImage(App& app, const wchar_t* path, UINT width, UINT height,
                bool jpeg = false, double dpiValue = 96.0, bool alpha = false) {
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    if (FAILED(app.wicFactory->CreateStream(stream.GetAddressOf())) ||
        FAILED(stream->InitializeFromFilename(path, GENERIC_WRITE)) ||
        FAILED(app.wicFactory->CreateEncoder(jpeg ? GUID_ContainerFormatJpeg : GUID_ContainerFormatPng,
                                             nullptr, encoder.GetAddressOf())) ||
        FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) ||
        FAILED(encoder->CreateNewFrame(frame.GetAddressOf(), nullptr)) ||
        FAILED(frame->Initialize(nullptr)) || FAILED(frame->SetSize(width, height)) ||
        FAILED(frame->SetResolution(dpiValue, dpiValue))) return false;
    WICPixelFormatGUID format = alpha ? GUID_WICPixelFormat32bppBGRA : GUID_WICPixelFormat24bppBGR;
    const auto requested = format;
    if (FAILED(frame->SetPixelFormat(&format)) || format != requested) return false;
    const UINT channels = alpha ? 4 : 3;
    std::vector<BYTE> row(static_cast<size_t>(width) * channels);
    for (UINT x = 0; x < width; ++x) {
        row[x * channels] = 100;
        row[x * channels + 1] = 50;
        row[x * channels + 2] = 20;
        if (alpha) row[x * channels + 3] = 128;
    }
    for (UINT y = 0; y < height; ++y)
        if (FAILED(frame->WritePixels(1, width * channels, static_cast<UINT>(row.size()), row.data())))
            return false;
    return SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
}

void generateAssets(App& app) {
    std::filesystem::create_directories("image-cache-assets");
    check(writeImage(app, L"image-cache-assets/small.png", 256, 128), "write small PNG");
    check(writeImage(app, L"image-cache-assets/large.jpg", 6000, 4000, true), "write large JPEG");
    std::filesystem::copy_file("image-cache-assets/large.jpg", "image-cache-assets/large-second.jpg",
                              std::filesystem::copy_options::overwrite_existing);
    check(writeImage(app, L"image-cache-assets/alpha.png", 128, 64, false, 96, true), "write transparent PNG");
    check(writeImage(app, L"image-cache-assets/dpi.png", 300, 150, false, 300), "write high-DPI PNG");
    check(writeImage(app, L"image-cache-assets/wide.png", 12000, 12), "write panorama");
    std::ofstream("image-cache-assets/broken.jpg", std::ios::binary) << "not an image";
    // A tiny BMP header advertising 400 MP: reject before any pixel allocation.
    BITMAPFILEHEADER fileHeader{};
    fileHeader.bfType = 0x4d42;
    fileHeader.bfOffBits = sizeof(fileHeader) + sizeof(BITMAPINFOHEADER);
    fileHeader.bfSize = fileHeader.bfOffBits;
    BITMAPINFOHEADER info{};
    info.biSize = sizeof(info); info.biWidth = info.biHeight = 20000;
    info.biPlanes = 1; info.biBitCount = 24;
    std::ofstream oversized("image-cache-assets/oversized.bmp", std::ios::binary);
    oversized.write(reinterpret_cast<const char*>(&fileHeader), sizeof(fileHeader));
    oversized.write(reinterpret_cast<const char*>(&info), sizeof(info));
    std::filesystem::copy_file(TINTA_IMAGE_FIXTURE, "image-cache-large.md",
                              std::filesystem::copy_options::overwrite_existing);
}

void decoding(App& app) {
    DecodedImage decoded;
    check(SUCCEEDED(decodeImageFile(app.wicFactory, L"image-cache-assets/large.jpg", 8192, decoded)),
          "large JPEG decodes");
    check(decoded.width < 6000 && decoded.height < 4000 &&
          decoded.pixels.size() <= IMAGE_DECODE_MAX_PIXELS * 4,
          "large JPEG uses a bounded display buffer");
    check(std::abs(static_cast<float>(decoded.width) * 96.0f / decoded.dpiX - 6000.0f) < 1.0f &&
          std::abs(static_cast<float>(decoded.height) * 96.0f / decoded.dpiY - 4000.0f) < 1.0f,
          "downsampling preserves the intrinsic size");
    check(SUCCEEDED(decodeImageFile(app.wicFactory, L"image-cache-assets/wide.png", 1024, decoded)) &&
          decoded.width == 1024 && decoded.height == 1, "panorama respects device texture limit");
    check(SUCCEEDED(decodeImageFile(app.wicFactory, L"image-cache-assets/alpha.png", 8192, decoded)) &&
          decoded.pixels[0] == 50 && decoded.pixels[1] == 25 && decoded.pixels[2] == 10 &&
          decoded.pixels[3] == 128, "PNG alpha is premultiplied correctly");
    for (const auto* path : {L"image-cache-assets/broken.jpg", L"image-cache-assets/missing.png",
                              L"image-cache-assets/oversized.bmp"}) {
        check(FAILED(decodeImageFile(app.wicFactory, path, 8192, decoded)) &&
              decoded.pixels.empty() && decoded.width == 0, "bad images leave no partial result");
    }
    check(boundedImageSize(0, 10, 8192).width == 0, "zero dimensions rejected");
    const auto extreme = boundedImageSize(UINT_MAX, UINT_MAX, 8192);
    check(static_cast<uint64_t>(extreme.width) * extreme.height <= IMAGE_DECODE_MAX_PIXELS,
          "size calculation cannot overflow for extreme dimensions");
}

App::ImageEntry bitmapEntry(App& app, UINT width, UINT height, float dpiValue = 96.0f) {
    App::ImageEntry entry;
    const auto props = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        dpiValue, dpiValue);
    check(SUCCEEDED(app.renderTarget->CreateBitmap(D2D1::SizeU(width, height), nullptr, 0,
                    props, entry.bitmap.GetAddressOf())), "native test bitmap created");
    entry.width = static_cast<int>(static_cast<float>(width) * 96.0f / dpiValue);
    entry.height = static_cast<int>(static_cast<float>(height) * 96.0f / dpiValue);
    return entry;
}

void drawImages(App& app) {
    app.renderTarget->BeginDraw();
    for (const auto& bitmap : app.layoutBitmaps) {
        check(bitmap.bitmap && bitmap.bitmap->GetPixelSize().width > 0, "layout bitmap is alive");
        app.renderTarget->DrawBitmap(bitmap.bitmap.Get(), bitmap.destRect);
    }
    check(SUCCEEDED(app.renderTarget->EndDraw()), "native image drawing succeeds after eviction");
}

void ownership(App& app) {
    app.clearLayoutCache();
    app.releaseImageCache();
    app.storeImageCacheEntry("first", bitmapEntry(app, 300, 150, 300));
    check(app.imageCacheBytes == 300 * 150 * 4, "cache counts pixels rather than DPI-adjusted size");
    app.layoutBitmaps.push_back({app.imageCache.at("first").bitmap, D2D1::RectF(0, 0, 96, 48)});
    // 64 MiB plus the earlier image must evict the earlier cache entry.
    app.storeImageCacheEntry("budget", bitmapEntry(app, 4096, 4096));
    check(!app.imageCache.count("first"), "byte budget eviction actually occurs");
    drawImages(app);
    app.releaseImageCache();
    drawImages(app);

    app.storeImageCacheEntry("first", bitmapEntry(app, 8, 8));
    app.layoutBitmaps.push_back({app.imageCache.at("first").bitmap, D2D1::RectF(0, 50, 8, 58)});
    auto filler = bitmapEntry(app, 1, 1);
    for (size_t i = 0; i < App::IMAGE_CACHE_MAX_ENTRIES; ++i)
        app.storeImageCacheEntry(std::to_string(i), filler);
    check(!app.imageCache.count("first"), "entry-count eviction actually occurs");
    drawImages(app);

    app.storeImageCacheEntry("replacement", bitmapEntry(app, 12, 12));
    app.layoutBitmaps.push_back({app.imageCache.at("replacement").bitmap, D2D1::RectF(0, 60, 12, 72)});
    app.storeImageCacheEntry("replacement", bitmapEntry(app, 16, 16));
    auto savedLayout = app.layoutBitmaps;
    app.layoutBitmaps.resize(1);  // table measurement rollback
    app.releaseImageCache();
    app.layoutBitmaps = std::move(savedLayout);
    drawImages(app);
    openLightbox(app, app.layoutBitmaps.back().bitmap.Get());
    app.clearLayoutCache();
    check(app.lightboxBitmap && app.lightboxBitmap->GetPixelSize().width == 12,
          "lightbox owns its image after cache and layout reset");
    closeLightbox(app);
    check(createRenderTarget(app) && app.layoutBitmaps.empty() && app.imageCache.empty() &&
          !app.showLightbox && app.layoutDirty, "target recreation invalidates old image drawings");
}

void mixedLayout(App& app) {
    std::ifstream file("image-cache-large.md", std::ios::binary);
    const std::string source((std::istreambuf_iterator<char>(file)), {});
    app.currentFile = std::filesystem::absolute("image-cache-large.md").u8string();
    app.root = app.parser.parse(source).root;
    app.readingWidthPct = 100;
    for (int theme : {0, 5}) {
        applyTheme(app, theme);
        for (int width : {1050, 650}) {
            app.width = width;
            app.height = 900;
            for (float scale : {1.0f, 1.5f}) {
                app.contentScale = scale;
                updateTextFormats(app);
                layoutDocumentViewportFirst(app);
                drawImages(app);
                ensureLayoutComplete(app);
                check(app.layoutBitmaps.size() == 7, "all valid mixed images survive measurement and relayout");
                check(app.tableRects.size() == 1 && app.codeBlocks.size() == 1 &&
                      app.docText.find(L"All surrounding content") != std::wstring::npos,
                      "surrounding headings, table, code and final paragraph survive");
                check(app.docText.find(L"Broken image placeholder") != std::wstring::npos &&
                      app.docText.find(L"Oversized image placeholder") != std::wstring::npos,
                      "invalid images render their alt text");
                drawImages(app);
            }
        }
    }
    check(printDebugPages(app, L"pages") > 0, "mixed document prints after cache eviction");
    openLightbox(app, app.layoutBitmaps.front().bitmap.Get());
    check(createRenderTarget(app) && !app.lightboxBitmap && !app.showLightbox &&
          app.layoutBitmaps.empty(), "device reset clears an open lightbox and live layout");
    ensureLayoutComplete(app);
    check(app.layoutBitmaps.size() == 7, "images reload after target recreation");
    drawImages(app);
}

void remoteImages(App& app, const char* base) {
    app.clearLayoutCache();
    app.releaseImageCache();
    const std::string url = std::string(base) + "/large.jpg";
    const std::string broken = std::string(base) + "/broken.jpg";
    const std::string source = "# Remote images\n\n![Remote](" + url + ")\n\n![Broken](" + broken + ")\n";
    app.root = app.parser.parse(source).root;
    layoutDocument(app);
    check(app.imageCache.at(url).pending, "remote image starts pending");
    const auto deadline = GetTickCount64() + 15000;
    while (GetTickCount64() < deadline && (app.imageCache.at(url).pending || app.imageCache.at(broken).pending)) {
        MSG message{};
        while (PeekMessageW(&message, app.hwnd, WM_APP_IMAGE_READY, WM_APP_IMAGE_READY, PM_REMOVE))
            completeAsyncImage(app, reinterpret_cast<void*>(message.lParam));
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_POSTMESSAGE);
    }
    check(!app.imageCache.at(url).pending && !app.imageCache.at(url).failed &&
          app.imageCache.at(url).bytes <= IMAGE_DECODE_MAX_PIXELS * 4,
          "downloaded large image completes with bounded pixels");
    check(!app.imageCache.at(broken).pending && app.imageCache.at(broken).failed,
          "downloaded corrupt image completes as a failure");
    layoutDocument(app);
    check(app.layoutBitmaps.size() == 1, "remote result relayout displays image");
    app.releaseImageCache();
    drawImages(app);
}
}

int runImageTests(const char* remoteBase) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    auto state = std::make_unique<App>();
    App& app = *state;
    app.hwnd = CreateWindowExW(0, L"STATIC", L"Image regression tests", WS_POPUP,
        0, 0, 1050, 900, nullptr, nullptr, nullptr, nullptr);
    if (!app.hwnd || !initD2D(app) || !createRenderTarget(app)) return 2;
    generateAssets(app);
    decoding(app);
    ownership(app);
    mixedLayout(app);
    if (remoteBase) remoteImages(app, remoteBase);
    DestroyWindow(app.hwnd);
    app.hwnd = nullptr;
    state.reset();
    CoUninitialize();
    std::cout << "Images: " << failures << " failures\n";
    return failures ? 1 : 0;
}
