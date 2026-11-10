// A block of hundreds or thousands of lines in Live (live_text.h, Laid). The
// block the caret is in is laid out as Markdown; a table, a list or a fence
// is one block however long. Its lines are laid out in parts, and a key lays
// out the part it changed. Checked here: parts of any size place every
// position, every point and every background where one layout of the whole
// text places them; a layout carried through edits stays what a new one of
// the same text is; and a key or a caret's move in a block of thousands of
// lines lays out a few lines of it, counted through the real handlers.
#include "d2d_init.h"
#include "editor.h"
#include "editrail.h"
#include "frame_preview.h"
#include "i18n.h"
#include "input.h"
#include "keystroke.h"
#include "live_editor.h"
#include "live_text.h"
#include "overlays.h"
#include "render.h"
#include "settings.h"
#include "tabs.h"
#include "utils.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;

int failures = 0;
void check(bool ok, const std::string& message) {
    if (!ok) {
        if (failures < 60) std::cerr << "FAIL live parts: " << message << '\n';
        ++failures;
    }
}

const size_t kWhole = (size_t)-1;  // parts of any number of lines: one layout, as Live had

// What `draw` leaves on the window's target, as bytes
std::vector<BYTE> pixelsAfter(App& app, const std::function<void()>& draw) {
    ComPtr<ID2D1DeviceContext> dc;
    ComPtr<ID2D1Bitmap1> target, readable;
    const auto format = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
    const D2D1_SIZE_U size = D2D1::SizeU((UINT)app.width, (UINT)app.height);
    if (FAILED(app.renderTarget->QueryInterface(IID_PPV_ARGS(&dc))) ||
        FAILED(dc->CreateBitmap(size, nullptr, 0, D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET, format), &target)) ||
        FAILED(dc->CreateBitmap(size, nullptr, 0,
                                D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW, format),
                                &readable))) {
        return {};
    }
    ComPtr<ID2D1Image> previous;
    dc->GetTarget(&previous);
    dc->SetTarget(target.Get());
    dc->BeginDraw();
    draw();
    dc->EndDraw();
    dc->SetTarget(previous.Get());
    D2D1_MAPPED_RECT mapped{};
    std::vector<BYTE> pixels;
    if (FAILED(readable->CopyFromBitmap(nullptr, target.Get(), nullptr)) || FAILED(readable->Map(D2D1_MAP_OPTIONS_READ, &mapped)))
        return pixels;
    for (UINT y = 0; y < size.height; ++y) pixels.insert(pixels.end(), mapped.bits + y * mapped.pitch, mapped.bits + y * mapped.pitch + size.width * 4);
    readable->Unmap();
    return pixels;
}

// The pixels that differ between two frames. With `tall`, only those in a
// column of at least so many that differ: the edge of a line or a box drawn a
// few ten-thousandths of a pixel off is shaded differently in one row or two
size_t differing(const App& app, const std::vector<BYTE>& a, const std::vector<BYTE>& b, int tall = 1) {
    const size_t width = (size_t)app.width, height = (size_t)app.height;
    if (a.size() != b.size() || a.size() != width * height * 4) return (size_t)-1;
    size_t n = 0;
    for (size_t x = 0; x < width; ++x) {
        int run = 0;
        for (size_t y = 0; y <= height; ++y) {
            if (y < height && std::memcmp(&a[(y * width + x) * 4], &b[(y * width + x) * 4], 4) != 0) {
                ++run;
                continue;
            }
            if (run >= tall) n += (size_t)run;
            run = 0;
        }
    }
    return n;
}

// A stretch drawn as Live draws it, scrolled to `offset`: with the window's
// height told to it, or (`all`) every part drawn
std::vector<BYTE> drawn(App& app, const LiveText& e, const live::Laid* laid, size_t from, const live::Look& look, float offset,
                        bool all = false) {
    return pixelsAfter(app, [&] {
        app.renderTarget->Clear(app.theme.background);
        D2D1_MATRIX_3X2_F saved;
        app.renderTarget->GetTransform(&saved);
        app.renderTarget->SetTransform(D2D1::Matrix3x2F::Translation(40.0f, 20.0f - offset) * saved);
        if (all) live::draw(app, e, laid, from, look, true, 1.0f);
        else live::draw(app, e, laid, from, look, true, 1.0f, offset - 20.0f, offset - 20.0f + (float)app.height);
        app.renderTarget->SetTransform(saved);
    });
}

// What a layout shows, told by the text's own positions and lines, so that
// layouts in parts of different sizes compare
struct Shape {
    float height = 0;
    std::vector<float> points;                  // x, y and height of each sampled position
    std::vector<size_t> hits;                   // the position and the task box at each probe point
    std::vector<std::array<size_t, 2>> blocks;  // the lines under a block's background: first, last
    std::vector<std::array<size_t, 3>> spans;   // the spans with a background: start, length, colour
};

size_t colourOf(const D2D1_COLOR_F& c) {
    return ((size_t)(c.r * 255) << 24) | ((size_t)(c.g * 255) << 16) | ((size_t)(c.b * 255) << 8) | (size_t)(c.a * 255);
}

// The positions looked at: every `step`th, and each one near `around`
std::vector<size_t> sampleOf(size_t size, size_t step, size_t around = kWhole) {
    std::vector<size_t> sample;
    for (size_t at = 0; at <= size; ++at) {
        const bool beside = around != kWhole && at + 160 >= around && at <= around + 160;
        if (beside || at % step == 0 || at == size) sample.push_back(at);
    }
    return sample;
}

Shape shapeOf(const live::Laid* laid, const LiveText& e, const std::vector<size_t>& sample,
              const std::vector<D2D1_POINT_2F>& probes) {
    Shape shape;
    if (!laid) return shape;
    const std::wstring& t = e.text;
    shape.height = laid->height;
    for (size_t at : sample) {
        float x = 0, y = 0, h = 0;
        const bool placed = live::caretPoint(laid, 0, at, x, y, h);
        shape.points.insert(shape.points.end(), {placed ? x : -1.0f, y, h});
    }
    for (const D2D1_POINT_2F& p : probes) {
        shape.hits.push_back(live::positionAt(laid, 0, t.size(), p.x, p.y));
        shape.hits.push_back(live::taskBoxAt(laid, e, 0, t.size(), p.x, p.y));
    }
    std::vector<size_t> starts{0};
    for (size_t i = 0; i < t.size(); ++i) {
        if (t[i] == L'\n') starts.push_back(i + 1);
    }
    auto lineOf = [&](size_t at) { return (size_t)(std::upper_bound(starts.begin(), starts.end(), at) - starts.begin()) - 1; };
    bool runsOn = false;
    for (const live::Laid::Part& part : laid->parts) {
        for (const live::Laid::Back& back : part.backs) {
            if (!back.block) {
                shape.spans.push_back({part.at + back.at, back.length, colourOf(back.color)});
                continue;
            }
            // As draw() has it: from its first position's line to its last
            // one's, a block that runs on into the next part going on there
            const size_t last = back.length ? back.at + back.length - 1 : back.at;
            const std::array<size_t, 2> lines{lineOf(part.at + back.at), lineOf(part.at + std::min(last, part.length))};
            if (runsOn && back.at == 0 && !shape.blocks.empty()) shape.blocks.back()[1] = lines[1];
            else shape.blocks.push_back(lines);
            runsOn = back.on;
        }
    }
    return shape;
}

// The points to look from: on every fourth sampled position's line at the
// position, left of the line and past its end; above the text and under it
std::vector<D2D1_POINT_2F> probesOf(const Shape& shape, float width) {
    std::vector<D2D1_POINT_2F> probes;
    for (size_t i = 0; i + 2 < shape.points.size(); i += 12) {
        const float x = shape.points[i], middle = shape.points[i + 1] + shape.points[i + 2] * 0.5f;
        probes.push_back(D2D1::Point2F(x + 1.0f, middle));
        probes.push_back(D2D1::Point2F(-6.0f, middle));
        probes.push_back(D2D1::Point2F(width + 40.0f, middle));
    }
    probes.push_back(D2D1::Point2F(10.0f, -8.0f));
    probes.push_back(D2D1::Point2F(10.0f, shape.height + 30.0f));
    return probes;
}

float furthest = 0;  // the most a part's place differed from the whole text's, over the run

// Whether two layouts of one text show the same; `why` says where not
bool alike(const Shape& a, const Shape& b, std::string& why) {
    why.clear();
    // One layout adds its lines' heights up as floats, and is a share of a
    // pixel out after a thousand of them; parts add them up exactly
    auto drift = [](float y) { return 0.05f + std::fabs(y) * 2.0e-5f; };
    if (std::fabs(a.height - b.height) > drift(b.height)) why = "height " + std::to_string(a.height) + " for " + std::to_string(b.height);
    furthest = std::max(furthest, std::fabs(a.height - b.height));
    if (a.points.size() != b.points.size() || a.hits.size() != b.hits.size()) why = "sizes";
    for (size_t i = 0; why.empty() && i < a.points.size(); ++i) {
        const float off = std::fabs(a.points[i] - b.points[i]);
        if (off > (i % 3 == 1 ? drift(b.points[i]) : 0.02f)) {
            why = "sample " + std::to_string(i / 3) + (i % 3 == 0 ? " x " : i % 3 == 1 ? " y " : " height ") +
                  std::to_string(a.points[i]) + " for " + std::to_string(b.points[i]);
        }
        if (i % 3 == 1) furthest = std::max(furthest, off);
    }
    for (size_t i = 0; why.empty() && i < a.hits.size(); ++i) {
        if (a.hits[i] != b.hits[i]) {
            why = std::string(i % 2 ? "task box" : "position") + " at probe " + std::to_string(i / 2) + ": " +
                  std::to_string(a.hits[i]) + " for " + std::to_string(b.hits[i]);
        }
    }
    if (why.empty() && a.blocks != b.blocks) why = "block backgrounds: " + std::to_string(a.blocks.size()) + " for " + std::to_string(b.blocks.size());
    if (why.empty() && a.spans != b.spans) why = "span backgrounds: " + std::to_string(a.spans.size()) + " for " + std::to_string(b.spans.size());
    return why.empty();
}

std::wstring textOf(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::wstring text = toWide(std::string{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()});
    text.erase(std::remove(text.begin(), text.end(), L'\r'), text.end());
    return text;
}

size_t lineOf(const std::wstring& text, size_t at) {
    return (size_t)std::count(text.begin(), text.begin() + (std::ptrdiff_t)std::min(at, text.size()), L'\n');
}

const size_t kUsual = 32;  // the lines a part holds in the app

// A text laid out in parts of `lines` lines
const live::Laid* laidIn(App& app, const LiveText& e, float width, const live::Look& look, live::LayoutCache& cache, size_t lines,
                         size_t caretLine) {
    live::partLines() = lines;
    const live::Laid* laid = live::layoutOf(app, e, 0, e.text.size(), width, look, cache, caretLine);
    live::partLines() = kUsual;
    return laid;
}

// The lines around a place drawn: laid out on their own as one layout and in
// parts of three lines, the same pixels, with a selection over some of them.
// One layout adds its lines' heights up as floats, so its lines stand a
// little off where parts have them: a share of a pixel far down a long text,
// which moves the letters' rows, and a few ten-thousandths in these few
// lines, which may shade the edge of a struck line or of a box a little
// differently. So the lines are few, and a pixel counts when it is one of
// three that differ, one above the other
void sameDrawn(App& app, LiveText& e, const live::Look& look, size_t at, const std::string& where, size_t reach = 90) {
    size_t from = live::lineStart(e.text, at), to = live::lineEnd(e.text, at);
    for (int i = 0; i < 12 && from > 0; ++i) from = live::lineStart(e.text, from - 1);
    for (int i = 0; i < 12 && to < e.text.size(); ++i) to = live::lineEnd(e.text, to + 1);
    const size_t caret = e.caret, anchor = e.anchor;
    e.anchor = live::lineStart(e.text, at);
    e.caret = std::min(to, at + reach);
    const size_t typed = live::caretLineOf(e, from, to);
    live::LayoutCache one, three;
    live::partLines() = kWhole;
    const live::Laid* whole = live::layoutOf(app, e, from, to, 700.0f, look, one, typed);
    live::partLines() = 3;
    const live::Laid* parts = live::layoutOf(app, e, from, to, 700.0f, look, three, typed);
    live::partLines() = kUsual;
    check(whole && parts && whole->parts.size() == 1 && parts->parts.size() > 1, "the lines are laid out both ways" + where);
    if (whole && parts) {
        const std::vector<BYTE> want = drawn(app, e, whole, from, look, 0);
        const std::vector<BYTE> blank = pixelsAfter(app, [&] { app.renderTarget->Clear(app.theme.background); });
        const size_t inked = differing(app, want, blank);
        check(inked != (size_t)-1 && inked > 2000, "the lines are drawn" + where);
        const size_t off = differing(app, drawn(app, e, parts, from, look, 0), want, 3);
        check(off == 0, "parts of 3 lines draw the pixels one layout draws" + where + ": " + std::to_string(off) + " differ");
    }
    e.caret = caret;
    e.anchor = anchor;
}

// Parts of any size show what one layout of the whole text shows: every
// position's place, the position and the task box at every point, every
// background, and the same pixels
void partSizes(App& app, const std::wstring& text) {
    check(live::partLines() == kUsual, "a part holds 32 lines");
    LiveText e;
    e.text = text;
    const live::Look look = live::pageLook(app);
    const size_t lines = lineOf(text, text.size()) + 1;
    const size_t row = text.find(L"| 200 |");
    check(row != std::wstring::npos && lines > 1000, "the fixture has its table and over a thousand lines");
    const std::vector<size_t> sample = sampleOf(text.size(), 3);
    for (float width : {900.0f, 330.0f}) {
        for (size_t caretLine : {kWhole, lineOf(text, row), (size_t)0}) {
            const std::string where = " at width " + std::to_string((int)width) + ", typing in line " + std::to_string((long long)caretLine);
            live::LayoutCache whole;
            const live::Laid* reference = laidIn(app, e, width, look, whole, kWhole, caretLine);
            check(reference && reference->parts.size() == 1 && reference->parts[0].lines == lines, "one layout of the whole text" + where);
            if (!reference) return;
            const std::vector<D2D1_POINT_2F> probes = probesOf(shapeOf(reference, e, sample, {}), width);
            const Shape want = shapeOf(reference, e, sample, probes);
            check(want.blocks.size() == 3 && want.spans.size() > 50, "the text has its three code backgrounds and its spans" + where);
            float lowest = 1.0e9f, highest = 0;
            for (size_t size : {(size_t)1, (size_t)2, (size_t)3, (size_t)7, kUsual}) {
                live::LayoutCache cache;
                const live::Laid* laid = laidIn(app, e, width, look, cache, size, caretLine);
                if (laid) lowest = std::min(lowest, laid->height), highest = std::max(highest, laid->height);
                std::string why;
                check(laid && laid->parts.size() == (lines + size - 1) / size, "the lines are in parts of " + std::to_string(size) + where);
                const bool same = laid && alike(shapeOf(laid, e, sample, probes), want, why);
                check(same, "parts of " + std::to_string(size) + " lines" + where + ": " + why);
            }
            // A line stays where it is however the lines above it are split
            // into parts: a key that splits them anew moves nothing under it
            check(highest - lowest < 0.02f, "the text is as tall in parts of every size" + where + ": " + std::to_string(highest - lowest));
        }
    }
    // Drawn. With a selection from the table into the first fence, what
    // draw() leaves when told what is in view is what it leaves drawing every
    // part; and the lines around each place are the pixels one layout draws
    e.anchor = text.find(L"| 190 |");
    e.caret = text.find(L"Block 25");
    const size_t typed = live::caretLineOf(e, 0, text.size());
    live::LayoutCache usual;
    const live::Laid* parts = laidIn(app, e, 900.0f, look, usual, kUsual, typed);
    if (!parts) return;
    const std::vector<BYTE> blank = pixelsAfter(app, [&] { app.renderTarget->Clear(app.theme.background); });
    for (const wchar_t* place : {L"---", L"| 200 |", L"Item 150 ", L"Step 30,", L"Block 25", L"def step_20", L"The end"}) {
        const std::string where = std::string(" around \"") + toUtf8(place) + "\"";
        float x = 0, y = 0, h = 0;
        live::caretPoint(parts, 0, text.find(place), x, y, h);
        const float offset = std::max(0.0f, y - 300.0f);
        const std::vector<BYTE> inView = drawn(app, e, parts, 0, look, offset);
        const size_t inked = differing(app, inView, blank);
        check(inked != (size_t)-1 && inked > 2000, "the text is drawn" + where);
        const size_t off = differing(app, drawn(app, e, parts, 0, look, offset, true), inView);
        check(off == 0, "the parts in view are all that shows" + where + ": " + std::to_string(off) + " pixels differ");
        sameDrawn(app, e, look, text.find(place), where);
    }
}

// A layout carried through edits is what a new layout of the text is. Four
// hundred edits of every kind, at places a fixed sequence picks: letters and
// marks typed and deleted, lines broken, joined, pasted and removed, fences
// and front matter opened and closed over the lines below, the caret moved
void edits(App& app, const std::wstring& text) {
    // A few hundred lines of the fixture: its head, every fifth line, its end
    LiveText e;
    size_t index = 0;
    const size_t all = lineOf(text, text.size());
    for (size_t start = 0; start < text.size(); ++index) {
        const size_t end = std::min(text.find(L'\n', start), text.size() - 1);
        if (index < 60 || index % 5 == 0 || index + 30 > all) e.text.append(text, start, end + 1 - start);
        start = end + 1;
    }
    std::wstring& s = e.text;
    const live::Look look = live::pageLook(app);
    const float width = 700.0f;
    std::wstring keys = L"ab *`|#-$~=[]>1. x_";
    keys += (wchar_t)0x6771;  // a letter of a font the text's own has not
    unsigned long long seed = 20261110;
    auto pick = [&](size_t below) {
        seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
        return (size_t)((seed >> 33) % below);
    };
    live::LayoutCache tight, usual;
    unsigned long long carried = 0, anew = 0;
    int wrong = 0;
    for (int step = 0; step < 400 && wrong < 4; ++step) {
        size_t at = pick(s.size() + 1);
        const int kind = (int)pick(11);
        if (step < 6) {
            // First at the text's end, where its last part has no break after it
            if (step == 0) s.push_back(L'z');
            else if (step == 1) s.push_back(L'\n');
            else if (step == 2) s += L"- a last item";
            else if (step == 3) s.pop_back();
            else if (step == 4) s += L"\n\n";
            else s.erase(s.size() - 2);
            at = s.size();
        } else if (kind <= 1) {
            s.insert(at, 1, keys[pick(keys.size())]);
        } else if (kind == 2 && at < s.size()) {
            s.erase(at, 1);
        } else if (kind == 3) {
            s.insert(at, 1, L'\n');
        } else if (kind == 4 && s.find(L'\n', at) != std::wstring::npos) {
            at = s.find(L'\n', at);
            s.erase(at, 1);
        } else if (kind == 5) {
            at = live::lineStart(s, at);
            s.insert(at, L"```\n");
        } else if (kind == 6) {
            at = live::lineStart(s, at);
            size_t end = at;
            for (size_t i = 0, n = 1 + pick(3); i < n && end < s.size(); ++i) end = std::min(s.find(L'\n', end), s.size() - 1) + 1;
            s.erase(at, end - at);
        } else if (kind == 7) {
            at = live::lineStart(s, at);
            s.insert(at, L"- [ ] a task in **bold**\n> a quote with `code`\n| a | b |\n");
        } else if (kind == 8) {
            // The document's first line opens front matter, or no longer does
            at = 0;
            if (s.compare(0, 4, L"---\n") == 0) s.erase(0, 4);
            else s.insert(0, L"---\n");
        } else if (kind == 9 && s.find(L"```", at) != std::wstring::npos) {
            // A fence's mark goes: the lines under it read another way
            at = live::lineStart(s, s.find(L"```", at));
            s.erase(at, std::min(s.find(L'\n', at), s.size() - 1) + 1 - at);
        }
        at = std::min(at, s.size());
        e.caret = e.anchor = at;
        // The caret's line shows as typed; now and then no line does
        const size_t typed = step % 7 == 3 ? kWhole : lineOf(s, at);
        const std::string where = "edit " + std::to_string(step) + " of kind " + std::to_string(kind) + " at " + std::to_string(at);
        live::LayoutCache fresh;
        const live::Laid* reference = laidIn(app, e, width, look, fresh, kWhole, typed);
        unsigned long long before = live::layoutWork().parts;
        const live::Laid* four = laidIn(app, e, width, look, tight, 4, typed);
        carried += live::layoutWork().parts - before;
        const live::Laid* parts = laidIn(app, e, width, look, usual, kUsual, typed);
        before = live::layoutWork().parts;
        live::LayoutCache again;
        const live::Laid* made = laidIn(app, e, width, look, again, 4, typed);
        anew += live::layoutWork().parts - before;
        if (!reference || !four || !parts || !made) {
            check(false, "the text is laid out after " + where);
            return;
        }
        const std::vector<size_t> sample = sampleOf(s.size(), 11, at);
        const std::vector<D2D1_POINT_2F> probes = probesOf(shapeOf(reference, e, sample, {}), width);
        const Shape want = shapeOf(reference, e, sample, probes);
        std::string why;
        bool same = alike(shapeOf(four, e, sample, probes), want, why);
        check(same, "parts of 4 lines carried through " + where + ": " + why);
        wrong += !same;
        same = alike(shapeOf(parts, e, sample, probes), want, why);
        check(same, "parts of 32 lines carried through " + where + ": " + why);
        wrong += !same;
        size_t held = 0;
        for (const live::Laid::Part& part : four->parts) held += part.lines;
        check(held == lineOf(s, s.size()) + 1 && four->length == s.size(), "the parts hold the text's lines after " + where);
        // Drawn, with a selection that ends anywhere: in a word, in a mark, at a line's end
        if (step % 4 == 0) sameDrawn(app, e, look, at, " after " + where, 1 + pick(240));
    }
    std::cout << "Live parts: 400 edits laid out " << carried << " parts of 4 lines, of " << anew << " for the texts made anew\n";
    check(carried * 2 < anew, "edits lay out the smaller share of the parts: " + std::to_string(carried) + " of " + std::to_string(anew));
}

// What the window draws in Live: the page, the rail, the tab strip
void drawLive(App& app) {
    app.renderTarget->Clear(app.theme.background);
    D2D1_MATRIX_3X2_F saved;
    app.renderTarget->GetTransform(&saved);
    app.renderTarget->SetTransform(D2D1::Matrix3x2F::Translation(documentViewportX(app), 0) * saved);
    renderLivePage(app);
    app.renderTarget->SetTransform(saved);
    renderTabStrip(app);
    app.editRailAnim = 1.0f;
    app.editRailHover = 0;
    renderEditRail(app);
}

void paint(App& app) {
    if (app.layoutDirty) ensureLayoutComplete(app);
    app.renderTarget->BeginDraw();
    drawLive(app);
    app.renderTarget->EndDraw();
}

void placeCaret(App& app, size_t pos) {
    app.editorCursorPos = app.editorSelStart = app.editorSelEnd = pos;
    app.editorHasSelection = false;
    paint(app);
}

void openInLive(App& app, const fs::path& path) {
    app.editorDirty = false;
    exitEditMode(app);
    check(openDocumentInViewer(app, path.wstring()), "opens: " + path.filename().string());
    enterEditMode(app);
    setEditorLive(app, app.hwnd, true);
    paint(app);
    check(app.editMode && editorLiveOn(app), "in Live: " + path.filename().string());
}

// What an act lays out: parts, and the lines in them
struct Work {
    unsigned long long parts = 0, lines = 0;
    double ms = 0;
};
Work workOf(const std::function<void()>& act) {
    const live::LayoutWork before = live::layoutWork();
    const auto start = std::chrono::steady_clock::now();
    act();
    Work work;
    work.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    work.parts = live::layoutWork().parts - before.parts;
    work.lines = live::layoutWork().lines - before.lines;
    return work;
}

// A key in a block of thousands of lines lays out the part it is in, not
// the block: counted through the key handlers and a frame after each key
void keys(App& app, const fs::path& dir) {
    const int rows = 3000;
    std::string big = "# Thousands of lines\n\nA paragraph before the table.\n\n| No. | Name | Note |\n|----:|:-----|:-----|\n";
    for (int i = 1; i <= rows; ++i) big += "| " + std::to_string(i) + " | Row **" + std::to_string(i) + "** | a `note` |\n";
    big += "\nA paragraph between.\n\n";
    for (int i = 1; i <= rows; ++i) {
        big += std::string(i % 7 == 3 ? "  - " : "- ") + (i % 5 == 0 ? "[ ] " : "") + "Item " + std::to_string(i) + " of the list\n";
    }
    big += "\nAnother paragraph.\n\n```cpp\n";
    for (int i = 1; i <= rows; ++i) big += "int value" + std::to_string(i) + " = " + std::to_string(i) + ";  /* a note */\n";
    big += "```\n\nThe end.\n";
    const fs::path path = dir / "Thousands.md";
    std::ofstream(path, std::ios::binary) << big;
    openInLive(app, path);
    auto find = [&](const std::wstring& what) { return app.editorText.find(what); };
    struct Place {
        const wchar_t* at;
        const char* name;
    };
    for (const Place& place : {Place{L"| Row **1500**", "the table"}, Place{L"Item 1500 of", "the list"}, Place{L"value1500 =", "the fence"}}) {
        const std::string in = std::string(" in ") + place.name + " of " + std::to_string(rows) + " lines";
        check(find(place.at) != std::wstring::npos, "the place is found" + in);
        // Coming to the block lays its lines out, once each or little more
        const Work entering = workOf([&] { placeCaret(app, find(place.at) + 6); });
        check(entering.lines >= (unsigned long long)rows && entering.lines < (unsigned long long)rows * 2 + 400,
              "the block's lines are laid out as the caret comes to it" + in + ": " + std::to_string(entering.lines));
        const Work still = workOf([&] {
            paint(app);
            paint(app);
        });
        check(still.parts == 0, "a frame with nothing changed lays out nothing" + in);
        const size_t size = app.editorText.size();
        const Work letter = workOf([&] {
            keystroke(app, 'X');
            paint(app);
        });
        check(app.editorText.size() == size + 1, "the letter is typed" + in);
        check(letter.parts >= 1 && letter.parts <= 2 && letter.lines <= 2 * kUsual,
              "a letter lays out its part" + in + ": " + std::to_string(letter.parts) + " parts, " + std::to_string(letter.lines) + " lines");
        const Work down = workOf([&] {
            keystroke(app, VK_DOWN);
            paint(app);
        });
        check(down.parts >= 1 && down.parts <= 4 && down.lines <= 4 * kUsual,
              "the caret's move lays out the parts it leaves and comes to" + in + ": " + std::to_string(down.parts) + " parts");
        const Work enter = workOf([&] {
            keystroke(app, VK_RETURN);
            paint(app);
        });
        check(app.editorText.size() > size + 1, "Enter breaks the line" + in);
        check(enter.parts >= 1 && enter.parts <= 6 && enter.lines <= 6 * kUsual,
              "Enter lays out a few parts" + in + ": " + std::to_string(enter.parts) + " parts, " + std::to_string(enter.lines) + " lines");
        const Work back = workOf([&] {
            keystroke(app, VK_BACK);
            paint(app);
        });
        check(back.parts >= 1 && back.parts <= 6 && back.lines <= 6 * kUsual,
              "Backspace lays out a few parts" + in + ": " + std::to_string(back.parts) + " parts, " + std::to_string(back.lines) + " lines");
        std::cout << "Live parts," << in << ": coming to it " << entering.lines << " lines in " << (int)entering.ms << " ms; a letter "
                  << letter.parts << " parts, " << letter.lines << " lines, " << letter.ms << " ms; Down " << down.parts << " parts, "
                  << down.ms << " ms; Enter " << enter.parts << " parts, " << enter.ms << " ms; Backspace " << back.parts
                  << " parts, " << back.ms << " ms\n";
    }
    app.editorDirty = false;
}

// The fixture in Live, the caret in each large block: frames to look at, in
// two themes at two widths
void frames(App& app, const fs::path& doc, const fs::path& out) {
    openInLive(app, doc);
    struct Shot {
        const wchar_t* at;
        const char* name;
    };
    for (int theme : {0, 5}) {
        applyTheme(app, theme);
        for (int width : {1050, 560}) {
            app.width = width;
            app.layoutDirty = true;
            for (const Shot& shot : {Shot{L"| 200 |", "table"}, Shot{L"Item 150 ", "list"}, Shot{L"Block 25:", "fence"}}) {
                const size_t at = app.editorText.find(shot.at);
                check(at != std::wstring::npos, std::string("the fixture has its ") + shot.name);
                placeCaret(app, at + 4);
                // A selection up from the caret, over a few lines
                app.editorSelStart = app.editorText.rfind(L'\n', at - 100);
                app.editorSelEnd = at + 4;
                app.editorHasSelection = true;
                if (app.layoutDirty) ensureLayoutComplete(app);
                const fs::path file = out / ("live-parts-" + std::string(shot.name) + "-theme" + std::to_string(theme) + "-" +
                                             std::to_string(width) + ".png");
                check(saveFramePreview(app, file, [&] { drawLive(app); }), "frame saved: " + file.filename().string());
                app.editorHasSelection = false;
            }
        }
    }
    app.width = 1050;
    app.layoutDirty = true;
    applyTheme(app, 0);
    app.editorDirty = false;
}

}  // namespace

int runLivePartsTests(const char* fixture) {
    auto state = std::make_unique<App>();
    App& app = *state;
    app.hwnd = CreateWindowExW(0, L"STATIC", L"Live parts tests", WS_POPUP, 0, 0, 1050, 800, nullptr, nullptr, nullptr, nullptr);
    if (!app.hwnd || !initD2D(app) || !createRenderTarget(app)) return 2;
    app.width = 1050;
    app.height = 800;
    updateTextFormats(app);
    Settings settings;
    settings.keyProfile = "windows";
    applyKeymap(app, settings);

    const fs::path dir = fs::current_path() / "live-parts";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const fs::path doc = dir / "live-large-block.md";
    fs::copy_file(fixture, doc, fs::copy_options::overwrite_existing, ec);
    check(!ec, "the fixture copies");
    const fs::path out = fs::current_path() / "frames";
    fs::create_directories(out, ec);

    tabsInit(app);
    applyTheme(app, 0);
    const std::wstring text = textOf(doc);
    partSizes(app, text);
    edits(app, text);
    keys(app, dir);
    frames(app, doc, out);
    std::cout << "Live parts: a part's place differed from one layout's by " << furthest << " px at most\n";

    app.editorDirty = false;
    DestroyWindow(app.hwnd);
    app.hwnd = nullptr;
    state.reset();
    std::cout << "Live parts: " << failures << " failures\n";
    return failures ? 1 : 0;
}
