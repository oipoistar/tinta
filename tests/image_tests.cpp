#include "d2d_init.h"
#include "editor.h"
#include "export.h"
#include "i18n.h"
#include "image_loader.h"
#include "input.h"
#include "network_path.h"
#include "overlays.h"
#include "print.h"
#include "render.h"

#include <algorithm>
#include <filesystem>
#include <climits>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>

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

// A page whose images are more than the cache holds keeps them all. Each
// one put the next out as the page was laid out, on every pass: three large
// pictures were decoded again at every resize, and three large downloads
// fetched again without end
void pageKeepsItsImages(App& app) {
    for (const wchar_t* path : {L"image-cache-assets/big1.png", L"image-cache-assets/big2.png", L"image-cache-assets/big3.png"}) {
        check(writeImage(app, path, 3000, 2400), "write a large PNG");
    }
    const std::string source = "# Three large pictures\n\nA paragraph with **bold** before them.\n\n"
                               "![one](image-cache-assets/big1.png)\n\n| A | B |\n| --- | --- |\n| ![two](image-cache-assets/big2.png) | a cell |\n\n"
                               "- an item\n\n> ![three](image-cache-assets/big3.png)\n\n```text\ncode after them\n```\n\nThe end.\n";
    std::ofstream("image-cache-big.md", std::ios::binary) << source;
    app.clearLayoutCache();
    app.releaseImageCache();
    app.currentFile = std::filesystem::absolute("image-cache-big.md").u8string();
    app.root = app.parser.parse(source).root;
    app.contentScale = 1.0f;
    app.width = 1050;
    app.height = 900;
    applyTheme(app, 0);
    updateTextFormats(app);
    app.layoutDirty = true;
    ensureLayoutComplete(app);
    check(app.layoutBitmaps.size() == 3 && app.docText.find(L"The end.") != std::wstring::npos, "the page has its three pictures and its text");
    check(app.imageCacheBytes > App::IMAGE_CACHE_BUDGET, "together the pictures are more than the cache's budget");
    check(app.imageCache.size() == 3, "the cache keeps every picture the page shows");
    std::vector<ID2D1Bitmap*> shown;
    for (const auto& bitmap : app.layoutBitmaps) shown.push_back(bitmap.bitmap.Get());
    // Another pass, at another width, as a resize makes one
    app.width = 820;
    app.layoutDirty = true;
    ensureLayoutComplete(app);
    std::vector<ID2D1Bitmap*> again;
    for (const auto& bitmap : app.layoutBitmaps) again.push_back(bitmap.bitmap.Get());
    check(again == shown, "a new pass shows the same bitmaps: no picture is decoded again");
    drawImages(app);
    // Another page laid out: they are let go when the budget asks
    const std::string other = "# Another page\n\n![small](image-cache-assets/small.png)\n";
    app.root = app.parser.parse(other).root;
    app.layoutDirty = true;
    ensureLayoutComplete(app);
    check(app.layoutBitmaps.size() == 1, "another page with one small picture is laid out");
    app.storeImageCacheEntry("budget", bitmapEntry(app, 4096, 4096));
    check(app.imageCacheBytes <= App::IMAGE_CACHE_BUDGET + 256 * 128 * 4 && app.imageCache.size() == 2,
          "the first page's pictures are let go, the page's own stays");
    drawImages(app);
    app.clearLayoutCache();
    app.releaseImageCache();
}

// #293: decode local image destinations once, including UTF-8 bytes, while
// preserving literal plus signs, malformed escapes and encoded NULs.
void encodedImagePaths(App& app) {
    std::ifstream file(TINTA_ENCODED_IMAGE_FIXTURE, std::ios::binary);
    const std::string source((std::istreambuf_iterator<char>(file)), {});
    check(!source.empty(), "encoded image fixture is readable");
    app.clearLayoutCache();
    app.releaseImageCache();
    app.currentFile = TINTA_ENCODED_IMAGE_FIXTURE;
    app.root = app.parser.parse(source).root;
    app.contentScale = 1.0f;
    updateTextFormats(app);
    for (int width : {1050, 650}) {
        app.width = width;
        layoutDocument(app);
        ensureLayoutComplete(app);
        check(app.layoutBitmaps.size() == 9, "all nine local image destinations render");
        check(app.tableRects.size() == 1 && app.codeBlocks.size() == 1 &&
              app.docText.find(L"Surrounding content survives.") != std::wstring::npos,
              "encoded images preserve mixed document content");
        check(app.docText.find(L"[image:") == std::wstring::npos,
              "valid encoded images never fall back to alt text");
        drawImages(app);
    }

    check(exportHtmlFile(app, L"encoded-images.html"), "encoded image HTML export succeeds");
    std::ifstream htmlFile("encoded-images.html", std::ios::binary);
    const std::string html((std::istreambuf_iterator<char>(htmlFile)), {});
    size_t images = 0;
    for (size_t pos = 0; (pos = html.find("<img src=\"data:image/png;base64,", pos)) != std::string::npos; ++pos)
        ++images;
    check(images == 9, "HTML embeds every encoded local PNG, including an encoded extension");

    check(exportDocxFile(app, L"encoded-images.docx"), "encoded image Word export succeeds");
    std::ifstream docxFile("encoded-images.docx", std::ios::binary);
    const std::string docx((std::istreambuf_iterator<char>(docxFile)), {});
    images = 0;
    for (size_t pos = 0; (pos = docx.find("<w:drawing>", pos)) != std::string::npos; ++pos)
        ++images;
    check(images == 9, "Word embeds every encoded local PNG");

    const std::string remote = "https://example.com/folder%20with%20space/image%2520.png";
    app.root = app.parser.parse("![Remote](" + remote + ")\n").root;
    check(exportHtmlFile(app, L"remote-encoded-image.html"), "remote image HTML export succeeds");
    std::ifstream remoteFile("remote-encoded-image.html", std::ios::binary);
    const std::string remoteHtml((std::istreambuf_iterator<char>(remoteFile)), {});
    check(remoteHtml.find("<img src=\"" + remote + "\"") != std::string::npos,
          "remote image URLs retain their original percent encoding");
}

// Two notes in different folders that both show "shot.png" each show
// their own, in either order and as often as they switch: the cache
// holds the file a document names, not the name
void imagesPerDocument(App& app) {
    app.clearLayoutCache();
    app.releaseImageCache();
    app.contentScale = 1.0f;
    updateTextFormats(app);
    for (int pass = 0; pass < 2; ++pass) {
        for (const char* folder : {"first", "second"}) {
            app.currentFile = std::string(TINTA_SAME_IMAGE_FIXTURE) + "/" + folder + "/notes.md";
            std::ifstream file(app.currentFile, std::ios::binary);
            const std::string source((std::istreambuf_iterator<char>(file)), {});
            check(!source.empty(), "same-name image fixture is readable");
            app.root = app.parser.parse(source).root;
            for (int width : {1050, 650}) {
                app.width = width;
                layoutDocument(app);
                ensureLayoutComplete(app);
                const UINT expected = folder[0] == 'f' ? 120 : 240;
                bool own = app.layoutBitmaps.size() == 4;
                for (const auto& bitmap : app.layoutBitmaps)
                    own = own && bitmap.bitmap->GetPixelSize().width == expected;
                check(own, "each note shows the image from its own folder");
                check(app.tableRects.size() == 1 && app.codeBlocks.size() == 1 &&
                      app.docText.find(L"Surrounding content survives.") != std::wstring::npos,
                      "same-name images keep the notes' mixed content");
                drawImages(app);
            }
        }
    }
}

size_t countOf(const std::string& text, const std::string& needle) {
    size_t count = 0;
    for (size_t pos = 0; (pos = text.find(needle, pos)) != std::string::npos; pos += needle.size()) ++count;
    return count;
}

std::string readAll(const wchar_t* path) {
    std::ifstream file(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(file)), {});
}

// The fixture folder through this computer's administrative share
// (\\localhost\C$\...), when it answers: a real network path that stays on
// this computer. Empty when the share is off or needs elevation.
std::wstring loopbackShare(const std::wstring& folder) {
    if (folder.size() < 3 || folder[1] != L':') return {};
    const std::wstring share = std::wstring(L"\\\\localhost\\") + folder[0] + L"$" + folder.substr(2);
    if (GetFileAttributesW((share + L"\\plain.png").c_str()) == INVALID_FILE_ATTRIBUTES) return {};
    return share;
}

// A document must not have Tinta open another computer's share by itself:
// showing, exporting or hovering it would sign in to that server with the
// user's Windows credentials. This computer's files, and the server the
// document was opened from, still open.
void networkPaths(App& app) {
    namespace fs = std::filesystem;
    using qmd::mayOpenFromDocument;
    using qmd::pathHost;
    check(pathHost(L"C:\\notes\\a.png").empty() && pathHost(L"notes\\a.png").empty() &&
          pathHost(L"\\notes\\a.png").empty() && pathHost(L"C:a.png").empty() &&
          pathHost(L"\\\\?\\C:\\a.png").empty() && pathHost(L"//./c:/a.png").empty(),
          "paths on this computer name no server");
    check(pathHost(L"\\\\NAS\\docs\\a.png") == L"nas" && pathHost(L"//nas/docs") == L"nas" &&
          pathHost(L"/\\nas\\docs") == L"nas" && pathHost(L"\\/nas/docs") == L"nas" &&
          pathHost(L"\\\\?\\UNC\\nas\\docs") == L"nas" && pathHost(L"//?/unc/nas/docs") == L"nas" &&
          pathHost(L"\\\\.\\UNC\\nas\\docs") == L"nas",
          "every UNC spelling names its server");
    check(pathHost(L"\\\\.\\pipe\\x") == L"?" && pathHost(L"\\\\?\\GLOBALROOT\\Device\\Mup\\nas\\x") == L"?" &&
          pathHost(L"\\\\\\nas\\x") == L"?" && pathHost(L"\\\\?\\Volume{1}\\x") == L"?",
          "other device paths name no server");
    check(mayOpenFromDocument(L"C:\\pics\\a.png", L"C:\\docs") &&
          mayOpenFromDocument(L"C:\\pics\\a.png", L"\\\\nas\\docs") &&
          mayOpenFromDocument(L"\\\\NAS\\photos\\a.png", L"\\\\nas\\docs") &&
          mayOpenFromDocument(L"//nas/docs/../photos/a.png", L"\\\\?\\UNC\\nas\\docs") &&
          !mayOpenFromDocument(L"\\\\other\\photos\\a.png", L"\\\\nas\\docs") &&
          !mayOpenFromDocument(L"//nas/photos/a.png", L"C:\\docs") &&
          !mayOpenFromDocument(L"//nas/photos/a.png", L"") &&
          !mayOpenFromDocument(L"\\\\.\\pipe\\x", L"C:\\docs") &&
          !mayOpenFromDocument(L"\\\\.\\pipe\\x", L"\\\\.\\pipe"),
          "a document opens this computer's files and its own server's only");

    const std::string source = readAll(L"" TINTA_NETWORK_FIXTURE);
    check(!source.empty(), "network path fixture is readable");
    app.clearLayoutCache();
    app.releaseImageCache();
    app.contentScale = 1.0f;
    updateTextFormats(app);
    app.currentFile = TINTA_NETWORK_FIXTURE;
    app.root = app.parser.parse(source).root;
    const std::wstring note = std::wstring(L" \u2014 ") + tr(app, "image.network_blocked") + L"]";
    for (int width : {1050, 650}) {
        app.width = width;
        layoutDocument(app);
        ensureLayoutComplete(app);
        check(app.layoutBitmaps.size() == 1, "only the local image opens");
        size_t notes = 0;
        for (size_t pos = 0; (pos = app.docText.find(note, pos)) != std::wstring::npos; pos += note.size())
            ++notes;
        check(notes == 8, "every image on another computer says why it is missing");
        check(app.docText.find(L"In a table" + note) != std::wstring::npos &&
              app.docText.find(L"In a quote" + note) != std::wstring::npos,
              "images in tables and quotes carry the note too");
        bool opened = false;
        for (const auto& cached : app.imageCache)
            opened = opened || cached.first.find("tinta-blocked") != std::string::npos;
        check(!opened, "nothing on another computer is opened or cached");
        std::set<std::string> remoteRefs;
        bool localLive = false;
        for (const auto& rect : app.linkRects) {
            if (rect.url.rfind("fileref-", 0) == 0 && rect.url.find("tinta-blocked.invalid") != std::string::npos)
                remoteRefs.insert(rect.url);
            localLive = localLive || (rect.url.rfind("fileref-ok:", 0) == 0 &&
                                      rect.url.find("linked.md") != std::string::npos);
        }
        bool allMissing = remoteRefs.size() == 2;
        for (const auto& url : remoteRefs) allMissing = allMissing && url.rfind("fileref-missing:", 0) == 0;
        check(allMissing, "file links to another computer badge as missing");
        check(localLive, "a local file link stays live");
        check(app.tableRects.size() == 1 && app.codeBlocks.size() == 1 &&
              app.docText.find(L"Surrounding content survives.") != std::wstring::npos,
              "network paths keep the mixed content around them");
        drawImages(app);
    }
    app.linkPeekUrl = app.hoveredLink = "wiki://tinta-blocked.invalid/share/wiki";
    handleLinkPeekTimer(app, app.hwnd);
    check(!app.linkPeekActive, "hovering a wiki link to another computer previews nothing");
    app.linkPeekUrl.clear();
    app.hoveredLink.clear();

    check(exportHtmlFile(app, L"network-paths.html"), "network path HTML export succeeds");
    const std::string html = readAll(L"network-paths.html");
    check(countOf(html, "<img src=\"data:image/png;base64,") == 1, "HTML embeds only the local image");
    check(html.find("<img src=\"//tinta-blocked.invalid/share/shot.png\"") != std::string::npos,
          "HTML keeps another computer's image as written");
    check(exportDocxFile(app, L"network-paths.docx"), "network path Word export succeeds");
    check(countOf(readAll(L"network-paths.docx"), "<w:drawing>") == 1, "Word embeds only the local image");

    // The same paths through a share that really answers. A document on
    // this computer must leave it unopened; one opened from the share
    // shows its server's files.
    const std::wstring share = loopbackShare(fs::absolute(fs::path(TINTA_NETWORK_FIXTURE).parent_path()).wstring());
    if (share.empty()) {
        std::cout << "Network paths: \\\\localhost administrative share unavailable, loopback checks skipped\n";
        return;
    }
    std::string url = toUtf8(share);
    std::replace(url.begin(), url.end(), '\\', '/');
    for (size_t space; (space = url.find(' ')) != std::string::npos;) url.replace(space, 1, "%20");
    const std::string loop = "# Loopback\n\n![Loop](" + url + "/plain.png)\n\n[Loop link](/\\" +
                             url.substr(2) + "/linked.md)\n\nAfter the share.\n";
    for (bool onShare : {false, true}) {
        app.clearLayoutCache();
        app.releaseImageCache();
        app.currentFile = onShare ? toUtf8(share + L"\\index.md") : std::string(TINTA_NETWORK_FIXTURE);
        app.root = app.parser.parse(loop).root;
        layoutDocument(app);
        ensureLayoutComplete(app);
        check(app.layoutBitmaps.size() == (onShare ? 1u : 0u),
              onShare ? "a document on a share shows that server's image"
                      : "a local document leaves the share's image unopened");
        bool live = false, missing = false;
        for (const auto& rect : app.linkRects) {
            live = live || rect.url.rfind("fileref-ok:", 0) == 0;
            missing = missing || rect.url.rfind("fileref-missing:", 0) == 0;
        }
        check(onShare ? live && !missing : missing && !live,
              onShare ? "a document on a share finds that server's linked file"
                      : "a local document badges the share's file as missing without a lookup");
        app.linkPeekUrl = app.hoveredLink = "wiki:" + url + "/linked";
        handleLinkPeekTimer(app, app.hwnd);
        check(app.linkPeekActive == onShare,
              onShare ? "a document on a share previews that server's wiki link"
                      : "a local document never previews the share's wiki link");
        app.linkPeekActive = false;
        app.linkPeekUrl.clear();
        // Clicking the wiki link opens the note only from the share
        const std::string before = app.currentFile;
        app.hoveredLink = "wiki:" + url + "/linked";
        app.selecting = app.mouseDown = true;
        app.selectionMode = App::SelectionMode::Normal;
        app.selShiftExtend = false;
        app.lastClickX = app.lastClickY = app.mouseX = app.mouseY = 100;
        handleMouseUp(app, app.hwnd, 0, MAKELPARAM(100, 100));
        check((app.currentFile != before) == onShare,
              onShare ? "a document on a share opens that server's wiki link"
                      : "a local document never looks up the share's wiki link");
        app.hoveredLink.clear();
    }
    // An untitled note's export has no folder and put one separator in
    // front of the path: "/localhost/C$/..." became a share path
    app.currentFile.clear();
    app.root = app.parser.parse("![Loop](" + url.substr(1) + "/plain.png)\n").root;
    check(exportHtmlFile(app, L"network-loop.html") && exportDocxFile(app, L"network-loop.docx"),
          "loopback exports succeed");
    check(countOf(readAll(L"network-loop.html"), "data:image/png;base64,") == 0 &&
          countOf(readAll(L"network-loop.docx"), "<w:drawing>") == 0,
          "exports leave the share's image unopened");
}

// Hover, press and release through the real handlers, as a separate click
// rather than the second half of a double-click
void click(App& app, float x, float y) {
    const LPARAM point = MAKELPARAM(static_cast<int>(x), static_cast<int>(y));
    app.lastClickTime = {};
    handleMouseMove(app, app.hwnd, point);
    handleMouseDown(app, app.hwnd, MK_LBUTTON, point);
    handleMouseUp(app, app.hwnd, 0, point);
}

// A click beside the open lightbox closes it; its release must not reach
// the page and re-open the inline image under the pointer (#249)
void lightboxDismissal(App& app) {
    ensureLayoutComplete(app);
    check(!app.layoutBitmaps.empty(), "lightbox fixture lays out inline images");
    if (app.layoutBitmaps.empty()) return;
    const D2D1_RECT_F image = app.layoutBitmaps.front().destRect;
    app.scrollX = app.targetScrollX = 0.0f;
    app.scrollY = app.targetScrollY = std::max(0.0f, image.top - dpi(app, 120.0f));
    const float x = documentViewportX(app) + (image.left + image.right) * 0.5f;
    const float y = std::min((image.top + image.bottom) * 0.5f, image.top + dpi(app, 40.0f)) -
                    app.scrollY;

    click(app, x, y);
    check(app.showLightbox, "clicking an inline image opens the lightbox");
    // Dragged aside, the zoomed image leaves the inline image uncovered
    app.lightboxPanX = static_cast<float>(app.width) * 4.0f;
    click(app, x, y);
    check(!app.showLightbox, "a click beside the zoomed image closes it for good");
    check(!app.swallowNextMouseUp, "the dismissing release is consumed exactly once");

    click(app, x, y);
    check(app.showLightbox, "the next click on the inline image opens it again");
    const D2D1_RECT_F zoomed = lightboxImageRect(app);
    click(app, (zoomed.left + zoomed.right) * 0.5f, (zoomed.top + zoomed.bottom) * 0.5f);
    check(!app.showLightbox, "a plain click on the zoomed image still closes it");
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
    lightboxDismissal(app);
    encodedImagePaths(app);
    imagesPerDocument(app);
    networkPaths(app);
    if (remoteBase) remoteImages(app, remoteBase);
    pageKeepsItsImages(app);
    DestroyWindow(app.hwnd);
    app.hwnd = nullptr;
    state.reset();
    CoUninitialize();
    std::cout << "Images: " << failures << " failures\n";
    return failures ? 1 : 0;
}
