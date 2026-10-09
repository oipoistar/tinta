#include "d2d_init.h"
#include "render.h"
#include "selection.h"
#include "utils.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <wrl/client.h>

namespace {
int failures = 0;
std::string scenario;
void check(bool condition, const char* message) {
    if (!condition) {
        if (failures < 30) std::cerr << "FAIL: " << scenario << ": " << message << '\n';
        ++failures;
    }
}
bool closeEnough(float a, float b) { return std::abs(a - b) < 0.1f; }

// Draw the retained native table layouts, including their code backgrounds,
// for visual inspection. Wide tables include the horizontally scrollable area.
bool renderTables(App& app, const std::filesystem::path& path) {
    using Microsoft::WRL::ComPtr;
    const float top = app.tableRects.front().bounds.top - 12;
    float right = static_cast<float>(app.width);
    for (const auto& table : app.tableRects) right = std::max(right, table.bounds.right + 12);
    const UINT width = static_cast<UINT>(std::ceil(right));
    const UINT height = static_cast<UINT>(std::ceil(app.tableRects.back().bounds.bottom - top + 12));
    ComPtr<IWICImagingFactory> wic;
    ComPtr<ID2D1Factory> d2d;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                               IID_PPV_ARGS(&wic))) ||
        FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d.GetAddressOf()))) return false;
    ComPtr<IWICBitmap> bitmap;
    ComPtr<ID2D1RenderTarget> target;
    ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(wic->CreateBitmap(width, height, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bitmap)) ||
        FAILED(d2d->CreateWicBitmapRenderTarget(bitmap.Get(), D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE), &target)) ||
        FAILED(target->CreateSolidColorBrush(app.theme.text, &brush))) return false;
    target->BeginDraw();
    target->Clear(app.theme.background);
    target->SetTransform(D2D1::Matrix3x2F::Translation(0, -top));
    for (const auto& item : app.layoutRects) {
        brush->SetColor(item.color);
        target->FillRectangle(item.rect, brush.Get());
    }
    for (const auto& item : app.layoutLines) {
        brush->SetColor(item.color);
        target->DrawLine(item.p1, item.p2, brush.Get(), item.stroke);
    }
    for (const auto& item : app.layoutTextRuns) {
        brush->SetColor(item.color);
        target->DrawTextLayout(item.pos, item.layout, brush.Get());
    }
    if (FAILED(target->EndDraw())) return false;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    if (FAILED(wic->CreateStream(&stream)) || FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) ||
        FAILED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) ||
        FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) ||
        FAILED(encoder->CreateNewFrame(&frame, nullptr)) || FAILED(frame->Initialize(nullptr)) ||
        FAILED(frame->SetSize(width, height))) return false;
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppPBGRA;
    return SUCCEEDED(frame->SetPixelFormat(&format)) && SUCCEEDED(frame->WriteSource(bitmap.Get(), nullptr)) &&
           SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
}

void checkTables(App& app) {
    check(app.tableRects.size() == 2, "both mixed-content tables render");
    if (app.tableRects.size() != 2) return;
    for (size_t ti = 0; ti < app.tableRects.size(); ++ti) {
        const auto& table = app.tableRects[ti];
        std::vector<float> xs, ys;
        for (const auto& line : app.layoutLines) {
            if (closeEnough(line.p1.x, line.p2.x) && closeEnough(line.p1.y, table.bounds.top) && closeEnough(line.p2.y, table.bounds.bottom))
                xs.push_back(line.p1.x);
            if (closeEnough(line.p1.y, line.p2.y) && closeEnough(line.p1.x, table.bounds.left) && closeEnough(line.p2.x, table.bounds.right) &&
                line.p1.y >= table.bounds.top - 0.1f && line.p1.y <= table.bounds.bottom + 0.1f)
                ys.push_back(line.p1.y);
        }
        std::sort(xs.begin(), xs.end());
        std::sort(ys.begin(), ys.end());
        check(xs.size() == (ti == 0 ? 14 : 4) && ys.size() == 4, "expected columns and rows remain");
        if (xs.size() < 2 || ys.size() < 2) continue;
        auto cellFor = [&](const D2D1_RECT_F& bounds, D2D1_RECT_F& cell) {
            if (bounds.top < table.bounds.top || bounds.top >= table.bounds.bottom) return false;
            auto col = std::upper_bound(xs.begin(), xs.end(), bounds.left);
            auto row = std::upper_bound(ys.begin(), ys.end(), bounds.top);
            if (col == xs.begin() || col == xs.end() || row == ys.begin() || row == ys.end()) return false;
            cell = D2D1::RectF(*(col - 1), *(row - 1), *col, *row);
            return true;
        };
        for (const auto& run : app.layoutTextRuns) {
            D2D1_RECT_F cell;
            if (!cellFor(run.bounds, cell)) continue;
            check(run.bounds.right <= cell.right + 0.1f, "text stays inside its column");
            check(run.bounds.bottom <= cell.bottom + 0.1f, "row height includes every wrapped line");
            DWRITE_TEXT_METRICS metrics{};
            check(SUCCEEDED(run.layout->GetMetrics(&metrics)), "native text measurement succeeds");
            check(run.pos.x + metrics.width <= cell.right + 0.5f, "actual glyph layout stays inside its column");
            if (run.docLength && run.docStart < app.docText.size()) {
                wchar_t first = app.docText[run.docStart];
                check(!(first >= 0xdc00 && first <= 0xdfff) && first != 0x0301,
                      "wrapping preserves surrogate pairs and combining clusters");
                float hitX = 0, hitY = 0;
                DWRITE_HIT_TEST_METRICS hit{};
                if (SUCCEEDED(run.layout->HitTestTextPosition(0, FALSE, &hitX, &hitY, &hit))) {
                    const size_t selected = selectionOffsetAtPoint(app, run.pos.x + hitX + hit.width * 0.25f,
                                                                  run.bounds.top + (run.bounds.bottom - run.bounds.top) * 0.5f);
                    check(selected == run.docStart, "clicking wrapped text selects its actual source offset");
                }
            }
        }
        for (const auto& link : app.linkRects) {
            D2D1_RECT_F cell;
            if (cellFor(link.bounds, cell))
                check(link.bounds.right <= cell.right + 0.1f && link.bounds.bottom <= cell.bottom + 0.1f,
                      "wrapped link hit regions stay in their cell");
        }
        for (const auto& rect : app.layoutRects) {
            // Full-row header/striping backgrounds intentionally span columns.
            if (closeEnough(rect.rect.left, table.bounds.left) && closeEnough(rect.rect.right, table.bounds.right)) continue;
            D2D1_RECT_F cell;
            if (cellFor(rect.rect, cell))
                check(rect.rect.right <= cell.right + 0.1f && rect.rect.bottom <= cell.bottom + 0.1f,
                      "inline code background stays in its cell");
        }
    }
    for (const auto* text : {L"FX-SGTL-GOODS-001", L"FX-SGTL-GOODS-002", L"2026/04/01 00:00", L"2030/03/31 23:59"}) {
        const size_t pos = app.docText.find(text);
        check(pos != std::wstring::npos && app.docText.find(text, pos + 1) == std::wstring::npos,
              "wrapping preserves searchable text exactly once");
        check(app.tableRects[0].tsv.find(text) != std::wstring::npos, "copy table preserves the full cell value");
        if (pos != std::wstring::npos) {
            for (size_t i = pos; i < pos + std::wcslen(text); ++i) {
                if (app.docText[i] == L' ') continue;
                check(std::any_of(app.textRects.begin(), app.textRects.end(), [i](const auto& rect) {
                    return rect.docStart <= i && i < rect.docStart + rect.docLength;
                }), "every non-space character remains selectable after wrapping");
            }
        }
    }
    check(app.docText.find(L"Final heading") != std::wstring::npos && app.codeBlocks.size() == 1,
          "headings and fenced code following tables still render");
    const size_t shortStart = app.docText.find(L"short code");
    check(std::count_if(app.layoutTextRuns.begin(), app.layoutTextRuns.end(), [&](const auto& run) {
        return run.docStart == shortStart && run.docLength == 10;
    }) == 1, "fitting inline code remains one span");
}
}

// Double-click inside a cell takes that cell's word, and a copied row keeps
// its cells apart (#246, spotted by @Lex987)
void checkCellWords(App& app) {
    const auto fixture = std::filesystem::path(TINTA_FRAGMENT_FIXTURE).parent_path().parent_path() /
                         "table-cells-246.md";
    std::ifstream file(fixture);
    check(file.good(), "cell fixture opens");
    std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    auto doc = app.parser.parse(source);
    check(doc.success, "cell fixture parses");
    app.root = doc.root;
    app.fitBlocks.clear();
    for (int theme : {0, 5}) {
        applyTheme(app, theme);
        for (int width : {1050, 650}) {
            app.width = width;
            scenario = "cells-theme" + std::to_string(theme) + "-width" + std::to_string(width);
            layoutDocument(app);
            check(app.tableRects.size() == 1 && app.codeBlocks.size() == 1,
                  "the table and the code block around it render");
            check(app.docText.find(L"Delta\tEcho\tFoxtrot\n") != std::wstring::npos &&
                      app.docText.find(L"Golf\tHotel\tIndia\n") != std::wstring::npos &&
                      app.docText.find(L"code\tbold\tlink\n") != std::wstring::npos,
                  "a row's cells are tab-separated, so a copied row pastes as columns");
            // The double-click path: hit-test inside the cell's own run,
            // then take the word around that offset
            for (const wchar_t* word : {L"Delta", L"Echo", L"Foxtrot", L"Hotel", L"code", L"bold",
                                        L"link", L"\u4E2D\u6587", L"\u65E5\u672C\u8A9E", L"\uD55C\uAD6D\uC5B4"}) {
                const size_t at = app.docText.find(word);
                const App::TextRect* hit = nullptr;
                for (const auto& r : app.textRects) {
                    if (at != std::wstring::npos && at >= r.docStart && at < r.docStart + r.docLength) {
                        hit = &r;
                        break;
                    }
                }
                check(hit != nullptr, "every cell word is laid out");
                if (!hit) continue;
                const size_t off = selectionOffsetAtPoint(app, (hit->rect.left + hit->rect.right) / 2,
                                                          (hit->rect.top + hit->rect.bottom) / 2);
                size_t start = 0, end = 0;
                selectionWordRange(app, off, start, end);
                check(app.docText.substr(start, end - start) == word,
                      "double-click in a cell selects only that cell's word");
            }
            const size_t bravo = app.docText.find(L"Bravo");
            size_t start = 0, end = 0;
            selectionWordRange(app, bravo + 1, start, end);
            check(app.docText.substr(start, end - start) == L"Bravo",
                  "paragraph words still select one at a time");
        }
    }
}

int runTableLayoutTests() {
    {
        auto state = std::make_unique<App>();
        App& app = *state;
        if (!initD2D(app)) return 2;
        const auto fixture = std::filesystem::path(TINTA_FRAGMENT_FIXTURE).parent_path().parent_path() / "wide-table-239.md";
        std::ifstream file(fixture);
        check(file.good(), "fixture opens");
        std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        auto doc = app.parser.parse(source);
        check(doc.success, "mixed fixture parses");
        app.root = doc.root;
        app.height = 900;
        app.readingWidthPct = 100;
        std::vector<std::wstring> originalTsv;
        wchar_t outputDir[32768];
        const DWORD outputLen = GetEnvironmentVariableW(L"TINTA_TABLE_RENDER_DIR", outputDir, static_cast<DWORD>(std::size(outputDir)));
        for (int theme : {0, 5}) {
            applyTheme(app, theme);
            for (float scale : {1.0f, 1.5f}) {
                app.contentScale = scale;
                updateTextFormats(app);
                for (int width : {1875, 1050, 650}) {
                    app.width = width;
                    for (bool fit : {false, true}) {
                        app.fitBlocks = fit ? std::vector<unsigned>{0x40000000u, 0x40000001u} : std::vector<unsigned>{};
                        scenario = "theme" + std::to_string(theme) + "-scale" + std::to_string(static_cast<int>(scale * 100)) +
                                   "-width" + std::to_string(width) + (fit ? "-fit" : "-auto");
                        layoutDocument(app);
                        checkTables(app);
                        if (originalTsv.empty()) for (const auto& table : app.tableRects) originalTsv.push_back(table.tsv);
                        for (size_t i = 0; i < app.tableRects.size() && i < originalTsv.size(); ++i)
                            check(app.tableRects[i].tsv == originalTsv[i], "layout changes leave TSV unchanged");
                        if (outputLen && outputLen < std::size(outputDir) && app.tableRects.size() == 2) {
                            std::filesystem::create_directories(outputDir);
                            check(renderTables(app, std::filesystem::path(outputDir) / (scenario + ".png")), "native table rasterization succeeds");
                        }
                    }
                }
            }
        }
        app.contentScale = 1.0f;
        updateTextFormats(app);
        checkCellWords(app);
    }
    CoUninitialize();
    std::cout << "Wide table layout: " << failures << " failures\n";
    return failures ? 1 : 0;
}
