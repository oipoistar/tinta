// Start page (design t7): the quiet launcher an empty Tinta shows
// instead of the tutorial sample document. Recents are the hero, the
// sample document and tutorials become three Learn cards, the whole
// window stays a drop target, and everything on the page carries its
// key so the launcher doubles as a shortcut primer.

#include "startpage.h"
#include "annotations.h"
#include "document.h"
#include "i18n.h"
#include "settings.h"
#include "tabs.h"
#include "utils.h"

#include <dwrite_1.h>

#include <algorithm>
#include <string>

// --- Embedded documents -------------------------------------------------

// The old bare-launch tutorial, now behind the "Sample document" card
// (and still the fallback when a requested file cannot be loaded)
static const char* kSampleDocument = R"(# Welcome to Tinta

**Tinta** is a fast, lightweight Markdown and Mermaid viewer for Windows.

## Getting Started

- **Drag & drop** a `.md` or `.mmd` file onto this window
- Press **B** to browse and open files from a folder
- Or run `tinta.exe readme.md` from the command line
- Press **?** for all available keyboard shortcuts

## Features

- 10 beautiful themes — press **T** to choose
- Native Mermaid flowchart rendering for `.mmd` files
- Edit mode with live preview — press **:**
- Search — press **F**
- Table of contents — press **Tab**
- Text selection and copy
- Syntax highlighting in code blocks for C/C++, C#, Python, JavaScript, Rust, Go, and Bash

## Code Example

```cpp
int main() {
    printf("Hello, World!\n");
    return 0;
}
```

## Keyboard Shortcuts

Press **?** at any time to see all shortcuts.

### Navigation

- **J / K** - Scroll down / up
- **Space / PgDn** - Page down
- **PgUp** - Page up
- **Home / End** - Jump to start / end
- **Ctrl+Scroll** - Zoom in / out

### View

- **F** or **Ctrl+F** - Search
- **Enter** - Next search match
- **B** - Toggle folder browser
- **Tab** - Toggle table of contents
- **T** - Theme chooser
- **S** - Toggle stats

### Editing

- **:** - Enter edit mode
- **Ctrl+S** - Save (in edit mode)
- **ESC ESC** - Exit edit mode

### General

- **Ctrl+A** - Select all
- **Ctrl+C** - Copy selection
- **ESC** - Close overlay / Quit
- **Q** - Quit
)";

static const char* kMermaidTour = R"(# Mermaid diagrams

Tinta renders **22 diagram families natively** from plain text. A diagram
is just a fenced code block with the `mermaid` language tag — edit the
text, save, and the picture follows.

## Flowchart

```mermaid
flowchart LR
    A[Write text] --> B{Looks right?}
    B -->|Yes| C[Ship it]
    B -->|No| D[Tweak and save]
    D --> A
```

## Sequence

```mermaid
sequenceDiagram
    participant You
    participant Tinta
    You->>Tinta: Save the file
    Tinta-->>You: Re-renders instantly
    You->>Tinta: Press : to edit
    Tinta-->>You: Live preview beside the text
```

## Pie

```mermaid
pie title Where the bytes go
    "Rendering" : 62
    "Parsing" : 23
    "Everything else" : 15
```

## Gantt

```mermaid
gantt
    title A tiny plan
    dateFormat YYYY-MM-DD
    section Draft
    Write outline   :done, a, 2026-08-20, 2d
    Fill sections   :active, b, after a, 3d
    section Review
    Read-through    :c, after b, 1d
```

## And more

Class, state, ER, gitGraph, mindmap, timeline, journey, quadrant and XY
charts all render the same way. A file ending in `.mmd` opens straight
as a diagram — no fences needed.
)";

static const char* kMarkdownBasics = R"(# Markdown basics

Everything Tinta renders, in two minutes. Press **:** to open this file
in the editor and watch the preview follow your keystrokes.

## Emphasis

**Bold**, *italic*, ~~strikethrough~~, ==highlight==, `inline code`,
x^2^ superscript and H~2~O subscript. Emoji shortcodes work too: :rocket: :tada:

## Lists

- Bullets with `-`, `*` or `+`
- Press Enter and the list continues by itself
    - Indent a level with Tab

1. Ordered lists number themselves
2. The editor increments on Enter

- [x] Task lists render as real checkboxes
- [ ] Click one in the viewer to flip it on disk

## Table

| Syntax | Renders as |
|---|---|
| `**bold**` | **bold** |
| `*italic*` | *italic* |
| `==mark==` | ==mark== |

## Code

```python
def greet(name):
    return f"Hello, {name}!"
```

## Quotes and alerts

> A plain blockquote.

> [!TIP]
> GitHub-style alerts render with their icon and tint.

## Links

Plain URLs autolink, `[text](url)` works, and so do [[wiki links]] and
bare file references like notes.md — live ones open in a tab, missing
ones offer to create the file.

## Math

Inline $E = mc^2$ and block math render natively.
)";

const char* startPageSampleDoc() { return kSampleDocument; }

// --- Small drawing helpers ----------------------------------------------

static D2D1_COLOR_F mixToward(D2D1_COLOR_F base, D2D1_COLOR_F to, float t) {
    D2D1_COLOR_F c;
    c.r = base.r + (to.r - base.r) * t;
    c.g = base.g + (to.g - base.g) * t;
    c.b = base.b + (to.b - base.b) * t;
    c.a = 1.0f;
    return c;
}

static D2D1_COLOR_F withA(D2D1_COLOR_F c, float a) {
    c.a = a;
    return c;
}

// One-shot layout: exact size/weight, optional tracking (design uses
// letter-spacing on the wordmark and section labels) and ellipsis
// trimming for paths. Caller releases.
static IDWriteTextLayout* spLayout(App& app, const std::wstring& text,
                                   float sizeDip, DWRITE_FONT_WEIGHT weight,
                                   float maxW, bool wrap = false,
                                   bool ellipsis = false,
                                   float trackingDip = 0.0f) {
    IDWriteTextFormat* fmt = nullptr;
    app.dwriteFactory->CreateTextFormat(
        L"Segoe UI", nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, dpi(app, sizeDip), L"en-us", &fmt);
    if (!fmt) return nullptr;
    if (!wrap) fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    if (ellipsis) {
        IDWriteInlineObject* sign = nullptr;
        if (SUCCEEDED(app.dwriteFactory->CreateEllipsisTrimmingSign(
                fmt, &sign)) && sign) {
            DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
            fmt->SetTrimming(&trim, sign);
            sign->Release();
        }
    }
    IDWriteTextLayout* layout = nullptr;
    app.dwriteFactory->CreateTextLayout(text.c_str(), (UINT32)text.size(),
                                        fmt, std::max(1.0f, maxW),
                                        dpi(app, 400.0f), &layout);
    fmt->Release();
    if (layout && trackingDip != 0.0f) {
        IDWriteTextLayout1* l1 = nullptr;
        if (SUCCEEDED(layout->QueryInterface(__uuidof(IDWriteTextLayout1),
                                             (void**)&l1)) && l1) {
            DWRITE_TEXT_RANGE all{0, (UINT32)text.size()};
            l1->SetCharacterSpacing(0.0f, dpi(app, trackingDip), 0.0f, all);
            l1->Release();
        }
    }
    return layout;
}

static void spDraw(App& app, IDWriteTextLayout* layout, float x, float y,
                   D2D1_COLOR_F color) {
    if (!layout) return;
    app.brush->SetColor(color);
    app.renderTarget->DrawTextLayout(D2D1::Point2F(x, y), layout, app.brush);
}

static DWRITE_TEXT_METRICS spMetrics(IDWriteTextLayout* layout) {
    DWRITE_TEXT_METRICS m{};
    if (layout) layout->GetMetrics(&m);
    return m;
}

// Keycap chip ("Ctrl+O", "N", "1"). Draws right-aligned when rightEdge
// is set, returns its width.
static float spChip(App& app, const std::wstring& label, float x,
                    float centerY, D2D1_COLOR_F bg, D2D1_COLOR_F fg,
                    bool rightAligned = false,
                    D2D1_RECT_F* outRect = nullptr) {
    IDWriteTextLayout* l = spLayout(app, label, 11.0f,
                                    DWRITE_FONT_WEIGHT_NORMAL, 4096.0f);
    DWRITE_TEXT_METRICS m = spMetrics(l);
    float padX = dpi(app, 7.0f);
    float padY = dpi(app, 2.0f);
    float w = m.width + padX * 2.0f;
    float h = m.height + padY * 2.0f;
    float left = rightAligned ? x - w : x;
    D2D1_RECT_F r = D2D1::RectF(left, centerY - h * 0.5f, left + w,
                                centerY + h * 0.5f);
    app.brush->SetColor(bg);
    app.renderTarget->FillRoundedRectangle(
        D2D1::RoundedRect(r, dpi(app, 4.0f), dpi(app, 4.0f)), app.brush);
    spDraw(app, l, left + padX, r.top + padY, fg);
    if (l) l->Release();
    if (outRect) *outRect = r;
    return w;
}

// --- Relative timestamps -------------------------------------------------

static std::wstring relativeWhen(App& app, unsigned long long when) {
    FILETIME nowFt;
    GetSystemTimeAsFileTime(&nowFt);
    ULARGE_INTEGER now;
    now.LowPart = nowFt.dwLowDateTime;
    now.HighPart = nowFt.dwHighDateTime;
    if (when == 0 || when >= now.QuadPart) return tr(app, "start.today");

    const unsigned long long kSecond = 10000000ULL;
    unsigned long long diff = now.QuadPart - when;
    if (diff < 90 * kSecond) return tr(app, "start.just_now");
    if (diff < 60 * 60 * kSecond) {
        wchar_t buf[48];
        swprintf_s(buf, _countof(buf), tr(app, "start.min_ago"),
                   (int)(diff / (60 * kSecond)));
        return buf;
    }

    // Calendar-day distance in local time decides today/yesterday/date
    FILETIME whenFt;
    ULARGE_INTEGER w;
    w.QuadPart = when;
    whenFt.dwLowDateTime = w.LowPart;
    whenFt.dwHighDateTime = w.HighPart;
    FILETIME whenLocal, nowLocal;
    FileTimeToLocalFileTime(&whenFt, &whenLocal);
    FileTimeToLocalFileTime(&nowFt, &nowLocal);
    ULARGE_INTEGER wl, nl;
    wl.LowPart = whenLocal.dwLowDateTime;
    wl.HighPart = whenLocal.dwHighDateTime;
    nl.LowPart = nowLocal.dwLowDateTime;
    nl.HighPart = nowLocal.dwHighDateTime;
    const unsigned long long kDay = 24ULL * 60 * 60 * kSecond;
    unsigned long long dayDiff = nl.QuadPart / kDay - wl.QuadPart / kDay;
    if (dayDiff == 0) return tr(app, "start.today");
    if (dayDiff == 1) return tr(app, "start.yesterday");

    SYSTEMTIME st{};
    FileTimeToSystemTime(&whenLocal, &st);
    wchar_t buf[48];
    if (GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &st, L"MMM d", buf,
                        _countof(buf), nullptr) > 0) {
        return buf;
    }
    return tr(app, "start.today");
}

// --- State ---------------------------------------------------------------

bool startPageActive(const App& app) {
    return app.currentFile.empty() && !app.editMode &&
           !app.startPageEmbeddedOpen;
}

void startPageRefreshRecents(App& app) {
    app.startPageRecents.clear();
    Settings stored = loadSettings();
    for (const auto& recent : stored.recentFiles) {
        app.startPageRecents.push_back({recent.path, recent.when});
    }
}

int startPageHitAt(const App& app, float x, float y) {
    for (const auto& hit : app.startPageHits) {
        if (x >= hit.first.left && x <= hit.first.right &&
            y >= hit.first.top && y <= hit.first.bottom) {
            return hit.second;
        }
    }
    return 0;
}

void startPageOpenEmbedded(App& app, HWND hwnd, int card) {
    const char* content = card == 1   ? kMermaidTour
                          : card == 2 ? kMarkdownBasics
                                      : kSampleDocument;
    const char* titleKey = card == 1   ? "start.learn_mermaid"
                           : card == 2 ? "start.learn_md"
                                       : "start.learn_sample";
    auto result = parseDocument(app.parser, content, std::string_view{});
    if (!result.success) return;
    app.root = result.root;
    app.parseTimeUs = result.parseTimeUs;
    app.sourceText = content;
    annotationsParseSource(app);
    app.currentFile.clear();
    app.scrollY = 0;
    app.scrollX = 0;
    app.targetScrollY = 0;
    app.targetScrollX = 0;
    app.contentHeight = 0;
    app.verticalScrollbarVisible = false;
    app.scrollbarContentHeight = 0.0f;
    app.pendingScrollRestore = -1.0f;
    app.fitBlocks.clear();
    app.layoutDirty = true;
    // The launcher hands over to the document; a later switch back to an
    // empty tab brings it right back
    app.startPageEmbeddedOpen = true;
    tabsInit(app);
    app.tabs[app.activeTab].title = tr(app, titleKey);
    updateWindowTitle(app);
    InvalidateRect(hwnd, nullptr, FALSE);
}

// --- Rendering -----------------------------------------------------------

void startPageEnsureIcon(App& app) {
    if (app.startPageIconBitmap || !app.wicFactory || !app.renderTarget)
        return;
    HICON icon = (HICON)LoadImageW(GetModuleHandleW(nullptr), L"IDI_ICON1",
                                   IMAGE_ICON, 64, 64, 0);
    if (!icon) return;
    IWICBitmap* wicBitmap = nullptr;
    if (SUCCEEDED(app.wicFactory->CreateBitmapFromHICON(icon, &wicBitmap)) &&
        wicBitmap) {
        IWICFormatConverter* converter = nullptr;
        if (SUCCEEDED(app.wicFactory->CreateFormatConverter(&converter)) &&
            converter) {
            if (SUCCEEDED(converter->Initialize(
                    wicBitmap, GUID_WICPixelFormat32bppPBGRA,
                    WICBitmapDitherTypeNone, nullptr, 0.0,
                    WICBitmapPaletteTypeCustom))) {
                app.renderTarget->CreateBitmapFromWicBitmap(
                    converter, nullptr, &app.startPageIconBitmap);
            }
            converter->Release();
        }
        wicBitmap->Release();
    }
    DestroyIcon(icon);
}

void renderStartPage(App& app) {
    app.startPageHits.clear();
    if (!app.renderTarget || !app.brush || !app.dwriteFactory) return;
    startPageEnsureIcon(app);

    const D2DTheme& th = app.theme;
    D2D1_COLOR_F white = D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f);
    D2D1_COLOR_F text = th.text;
    D2D1_COLOR_F heading = th.heading;
    D2D1_COLOR_F accent = th.accent;

    // Theme-derived surfaces: dark themes lift toward the (light) text,
    // light themes toward paper white, mirroring the settings cards
    D2D1_COLOR_F surface = th.isDark ? mixToward(th.background, text, 0.06f)
                                     : mixToward(th.background, white, 0.60f);
    D2D1_COLOR_F surfaceBorder = withA(text, th.isDark ? 0.12f : 0.14f);
    D2D1_COLOR_F card = th.isDark ? mixToward(th.background, text, 0.05f)
                                  : mixToward(th.background, white, 0.55f);
    D2D1_COLOR_F cardBorder = withA(text, 0.10f);
    D2D1_COLOR_F chipBg = mixToward(th.background, text, 0.07f);
    D2D1_COLOR_F chipFg = withA(text, 0.60f);
    D2D1_COLOR_F hoverBg = mixToward(th.background, text, 0.06f);
    float accentLum =
        0.299f * accent.r + 0.587f * accent.g + 0.114f * accent.b;
    D2D1_COLOR_F onAccent = accentLum > 0.45f
                                ? D2D1::ColorF(0.05f, 0.08f, 0.15f, 1.0f)
                                : D2D1::ColorF(1.0f, 0.97f, 0.95f, 1.0f);

    float viewX = documentViewportX(app);
    float viewW = documentViewportWidth(app);
    float chrome = chromeTopHeight(app);
    float H = (float)app.height;

    float contW = std::min(dpi(app, 820.0f), viewW - dpi(app, 48.0f));
    bool twoCols = contW >= dpi(app, 640.0f);
    float rightW = twoCols ? dpi(app, 280.0f) : 0.0f;
    float colGap = twoCols ? dpi(app, 36.0f) : 0.0f;
    float leftW = contW - rightW - colGap;
    float x0 = (viewW - contW) * 0.5f;
    float y0 = chrome + dpi(app, 64.0f);

    auto addHit = [&](float l, float t, float r, float b, int id) {
        app.startPageHits.push_back(
            {D2D1::RectF(l + viewX, t, r + viewX, b), id});
    };

    // --- Hero: icon, wordmark, version, tagline
    IDWriteTextLayout* wm = spLayout(app, L"Tinta", 38.0f,
                                     DWRITE_FONT_WEIGHT_EXTRA_BOLD, 4096.0f,
                                     false, false, -1.0f);
    DWRITE_TEXT_METRICS wmM = spMetrics(wm);
    float iconSize = dpi(app, 34.0f);
    float heroH = std::max(wmM.height, iconSize);
    if (app.startPageIconBitmap) {
        float iy = y0 + (heroH - iconSize) * 0.5f;
        app.renderTarget->DrawBitmap(
            app.startPageIconBitmap,
            D2D1::RectF(x0, iy, x0 + iconSize, iy + iconSize));
    }
    float wmX = x0 + iconSize + dpi(app, 14.0f);
    spDraw(app, wm, wmX, y0 + (heroH - wmM.height) * 0.5f, heading);
    wchar_t ver[32];
    swprintf_s(ver, _countof(ver), L"v%d.%d.%d", TINTA_VERSION_MAJOR,
               TINTA_VERSION_MINOR, TINTA_VERSION_PATCH);
    IDWriteTextLayout* verL = spLayout(app, ver, 12.0f,
                                       DWRITE_FONT_WEIGHT_NORMAL, 4096.0f);
    DWRITE_TEXT_METRICS verM = spMetrics(verL);
    // Sits on the wordmark baseline, a step to the right
    spDraw(app, verL, wmX + wmM.width + dpi(app, 14.0f),
           y0 + (heroH - wmM.height) * 0.5f + wmM.height - verM.height -
               dpi(app, 6.0f),
           withA(text, 0.42f));
    if (verL) verL->Release();
    if (wm) wm->Release();

    IDWriteTextLayout* tag = spLayout(app, tr(app, "start.tagline"), 14.0f,
                                      DWRITE_FONT_WEIGHT_NORMAL, leftW);
    DWRITE_TEXT_METRICS tagM = spMetrics(tag);
    float tagY = y0 + heroH + dpi(app, 6.0f);
    spDraw(app, tag, x0, tagY, withA(text, 0.56f));
    if (tag) tag->Release();

    // --- Action buttons: Open a file (accent), New document, Browse
    float btnY = tagY + tagM.height + dpi(app, 26.0f);
    float btnH = dpi(app, 38.0f);
    struct Btn {
        const char* label;
        std::wstring key;
        bool primary;
        int id;
    };
    // The chips show the user's actual bindings, not hardcoded defaults
    Btn btns[3] = {
        {"start.open", L"Ctrl+O", true, 1},
        {"start.new", keyLabel(app.keymap[KA_NEWFILE]), false, 2},
        {"start.browse", keyLabel(app.keymap[KA_BROWSE]), false, 3},
    };
    float bx = x0;
    for (const Btn& b : btns) {
        IDWriteTextLayout* label = spLayout(
            app, tr(app, b.label), 13.0f,
            b.primary ? DWRITE_FONT_WEIGHT_SEMI_BOLD
                      : DWRITE_FONT_WEIGHT_NORMAL,
            4096.0f);
        DWRITE_TEXT_METRICS lm = spMetrics(label);
        IDWriteTextLayout* keyL = spLayout(app, b.key, 11.0f,
                                           DWRITE_FONT_WEIGHT_NORMAL,
                                           4096.0f);
        DWRITE_TEXT_METRICS km = spMetrics(keyL);
        if (keyL) keyL->Release();
        float chipW = km.width + dpi(app, 14.0f);
        float w = dpi(app, 14.0f) + lm.width + dpi(app, 9.0f) + chipW +
                  dpi(app, 14.0f);
        D2D1_RECT_F r = D2D1::RectF(bx, btnY, bx + w, btnY + btnH);
        D2D1_ROUNDED_RECT rr =
            D2D1::RoundedRect(r, dpi(app, 8.0f), dpi(app, 8.0f));
        bool hov = app.startPageHover == b.id;
        if (b.primary) {
            // Soft one-pixel drop like the mock's box-shadow
            D2D1_ROUNDED_RECT shadow = D2D1::RoundedRect(
                D2D1::RectF(r.left, r.top + dpi(app, 1.5f), r.right,
                            r.bottom + dpi(app, 1.5f)),
                dpi(app, 8.0f), dpi(app, 8.0f));
            app.brush->SetColor(D2D1::ColorF(0, 0, 0, 0.20f));
            app.renderTarget->FillRoundedRectangle(shadow, app.brush);
            D2D1_COLOR_F fill = accent;
            if (hov) fill = mixToward(accent, white, 0.08f);
            app.brush->SetColor(fill);
            app.renderTarget->FillRoundedRectangle(rr, app.brush);
        } else {
            D2D1_COLOR_F fill = surface;
            if (hov) fill = mixToward(surface, text, 0.04f);
            app.brush->SetColor(fill);
            app.renderTarget->FillRoundedRectangle(rr, app.brush);
            app.brush->SetColor(surfaceBorder);
            app.renderTarget->DrawRoundedRectangle(rr, app.brush, 1.0f);
        }
        float tx = bx + dpi(app, 14.0f);
        spDraw(app, label, tx, btnY + (btnH - lm.height) * 0.5f,
               b.primary ? onAccent : text);
        if (label) label->Release();
        D2D1_COLOR_F kBg = b.primary ? withA(onAccent, 0.14f) : chipBg;
        D2D1_COLOR_F kFg = b.primary ? withA(onAccent, 0.75f) : chipFg;
        spChip(app, b.key, tx + lm.width + dpi(app, 9.0f),
               btnY + btnH * 0.5f, kBg, kFg);
        addHit(r.left, r.top, r.right, r.bottom, b.id);
        bx += w + dpi(app, 8.0f);
    }

    // --- RECENT
    float recY = btnY + btnH + dpi(app, 34.0f);
    IDWriteTextLayout* recH = spLayout(app, tr(app, "start.recent"), 11.0f,
                                       DWRITE_FONT_WEIGHT_SEMI_BOLD, 4096.0f,
                                       false, false, 1.5f);
    DWRITE_TEXT_METRICS recM = spMetrics(recH);
    spDraw(app, recH, x0, recY, withA(text, 0.47f));
    if (recH) recH->Release();
    if (!app.startPageRecents.empty()) {
        IDWriteTextLayout* clearL = spLayout(app, tr(app, "start.clear"),
                                             11.5f, DWRITE_FONT_WEIGHT_NORMAL,
                                             4096.0f);
        DWRITE_TEXT_METRICS cm = spMetrics(clearL);
        float cx = x0 + leftW - cm.width;
        bool hov = app.startPageHover == 4;
        spDraw(app, clearL, cx, recY + (recM.height - cm.height) * 0.5f,
               withA(text, hov ? 0.75f : 0.42f));
        if (clearL) clearL->Release();
        addHit(cx - dpi(app, 6.0f), recY - dpi(app, 4.0f), x0 + leftW,
               recY + cm.height + dpi(app, 4.0f), 4);
    }

    float rowY = recY + recM.height + dpi(app, 8.0f);
    float rowH = dpi(app, 40.0f);
    float inset = dpi(app, 12.0f);
    int shown = std::min<int>(5, (int)app.startPageRecents.size());
    if (shown == 0) {
        IDWriteTextLayout* empty = spLayout(app, tr(app, "start.empty"),
                                            12.5f, DWRITE_FONT_WEIGHT_NORMAL,
                                            leftW);
        spDraw(app, empty, x0, rowY + dpi(app, 10.0f), withA(text, 0.42f));
        if (empty) empty->Release();
    }
    for (int i = 0; i < shown; i++) {
        const App::RecentDoc& rd = app.startPageRecents[i];
        float top = rowY + i * rowH;
        D2D1_RECT_F row =
            D2D1::RectF(x0 - inset, top, x0 + leftW + inset, top + rowH);
        bool hov = app.startPageHover == 10 + i;
        if (hov) {
            app.brush->SetColor(hoverBg);
            app.renderTarget->FillRoundedRectangle(
                D2D1::RoundedRect(row, dpi(app, 7.0f), dpi(app, 7.0f)),
                app.brush);
            app.brush->SetColor(accent);
            app.renderTarget->FillRoundedRectangle(
                D2D1::RoundedRect(
                    D2D1::RectF(row.left, top + dpi(app, 12.0f),
                                row.left + dpi(app, 3.0f),
                                top + dpi(app, 28.0f)),
                    dpi(app, 2.0f), dpi(app, 2.0f)),
                app.brush);
        }
        // Little page glyph
        float gx = x0;
        float gy = top + (rowH - dpi(app, 15.0f)) * 0.5f;
        app.brush->SetColor(withA(text, 0.5f));
        app.renderTarget->DrawRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(gx, gy, gx + dpi(app, 12.0f),
                                          gy + dpi(app, 15.0f)),
                              dpi(app, 2.0f), dpi(app, 2.0f)),
            app.brush, 1.0f);

        std::string pathUtf8 = rd.path;
        size_t slash = pathUtf8.find_last_of("/\\");
        std::wstring name = toWide(
            slash == std::string::npos ? pathUtf8
                                       : pathUtf8.substr(slash + 1));
        std::wstring dir = toWide(
            slash == std::string::npos ? std::string()
                                       : pathUtf8.substr(0, slash));

        // Right side first: the digit chip, then the timestamp
        D2D1_RECT_F chipRect{};
        wchar_t digit[2] = {(wchar_t)(L'1' + i), 0};
        float chipW = spChip(app, digit, x0 + leftW, top + rowH * 0.5f,
                             chipBg, chipFg, true, &chipRect);
        IDWriteTextLayout* whenL =
            spLayout(app, relativeWhen(app, rd.when), 11.5f,
                     DWRITE_FONT_WEIGHT_NORMAL, 4096.0f);
        DWRITE_TEXT_METRICS whenM = spMetrics(whenL);
        float whenX = x0 + leftW - chipW - dpi(app, 12.0f) - whenM.width;
        spDraw(app, whenL, whenX, top + (rowH - whenM.height) * 0.5f,
               withA(text, 0.42f));
        if (whenL) whenL->Release();

        float nameX = gx + dpi(app, 12.0f) + dpi(app, 12.0f);
        IDWriteTextLayout* nameL = spLayout(app, name, 13.5f,
                                            DWRITE_FONT_WEIGHT_NORMAL,
                                            leftW * 0.55f, false, true);
        DWRITE_TEXT_METRICS nameM = spMetrics(nameL);
        spDraw(app, nameL, nameX, top + (rowH - nameM.height) * 0.5f, text);
        if (nameL) nameL->Release();

        float pathX = nameX + nameM.width + dpi(app, 12.0f);
        float pathMax = whenX - dpi(app, 12.0f) - pathX;
        if (pathMax > dpi(app, 30.0f) && !dir.empty()) {
            IDWriteTextLayout* pathL = spLayout(app, dir, 12.0f,
                                                DWRITE_FONT_WEIGHT_NORMAL,
                                                pathMax, false, true);
            DWRITE_TEXT_METRICS pm = spMetrics(pathL);
            spDraw(app, pathL, pathX, top + (rowH - pm.height) * 0.5f,
                   withA(text, 0.38f));
            if (pathL) pathL->Release();
        }
        addHit(row.left, row.top, row.right, row.bottom, 10 + i);
    }

    // --- LEARN column
    if (twoCols) {
        float x1 = x0 + leftW + colGap;
        float ly = y0 + dpi(app, 118.0f);
        IDWriteTextLayout* learnH = spLayout(
            app, tr(app, "start.learn"), 11.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD,
            4096.0f, false, false, 1.5f);
        DWRITE_TEXT_METRICS lhM = spMetrics(learnH);
        spDraw(app, learnH, x1, ly, withA(text, 0.47f));
        if (learnH) learnH->Release();
        ly += lhM.height + dpi(app, 10.0f);

        struct Card {
            const wchar_t* ic;
            const char* title;
            const char* desc;
        };
        Card cards[3] = {
            {L"\u270E", "start.learn_sample", "start.learn_sample_d"},
            {L"\u25C8", "start.learn_mermaid", "start.learn_mermaid_d"},
            {L"\u2318", "start.learn_md", "start.learn_md_d"},
        };
        for (int i = 0; i < 3; i++) {
            float padX = dpi(app, 16.0f);
            float padY = dpi(app, 14.0f);
            IDWriteTextLayout* title = spLayout(app, tr(app, cards[i].title),
                                                13.0f,
                                                DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                                rightW - padX * 2.0f);
            IDWriteTextLayout* desc = spLayout(app, tr(app, cards[i].desc),
                                               12.0f,
                                               DWRITE_FONT_WEIGHT_NORMAL,
                                               rightW - padX * 2.0f, true);
            DWRITE_TEXT_METRICS tm = spMetrics(title);
            DWRITE_TEXT_METRICS dm = spMetrics(desc);
            float cardH = padY + tm.height + dpi(app, 5.0f) + dm.height +
                          padY;
            D2D1_RECT_F r = D2D1::RectF(x1, ly, x1 + rightW, ly + cardH);
            D2D1_ROUNDED_RECT rr =
                D2D1::RoundedRect(r, dpi(app, 9.0f), dpi(app, 9.0f));
            bool hov = app.startPageHover == 20 + i;
            app.brush->SetColor(hov ? mixToward(card, text, 0.03f) : card);
            app.renderTarget->FillRoundedRectangle(rr, app.brush);
            app.brush->SetColor(hov ? withA(text, 0.20f) : cardBorder);
            app.renderTarget->DrawRoundedRectangle(rr, app.brush, 1.0f);

            IDWriteTextLayout* ic = spLayout(app, cards[i].ic, 14.0f,
                                             DWRITE_FONT_WEIGHT_NORMAL,
                                             4096.0f);
            DWRITE_TEXT_METRICS im = spMetrics(ic);
            spDraw(app, ic, x1 + padX,
                   ly + padY + (tm.height - im.height) * 0.5f, accent);
            if (ic) ic->Release();
            spDraw(app, title, x1 + padX + im.width + dpi(app, 9.0f),
                   ly + padY, heading);
            if (title) title->Release();
            spDraw(app, desc, x1 + padX, ly + padY + tm.height + dpi(app, 5.0f),
                   withA(text, 0.52f));
            if (desc) desc->Release();
            addHit(r.left, r.top, r.right, r.bottom, 20 + i);
            ly += cardH + dpi(app, 10.0f);
        }

        // All shortcuts ?
        ly += dpi(app, 4.0f);
        IDWriteTextLayout* sc = spLayout(app, tr(app, "start.shortcuts"),
                                         12.0f, DWRITE_FONT_WEIGHT_NORMAL,
                                         4096.0f);
        DWRITE_TEXT_METRICS scM = spMetrics(sc);
        bool hov = app.startPageHover == 5;
        spDraw(app, sc, x1, ly, withA(text, hov ? 0.75f : 0.45f));
        if (sc) sc->Release();
        spChip(app, keyLabel(app.keymap[KA_HELP]),
               x1 + scM.width + dpi(app, 8.0f), ly + scM.height * 0.5f,
               chipBg, chipFg);
        addHit(x1, ly - dpi(app, 4.0f),
               x1 + scM.width + dpi(app, 8.0f) + dpi(app, 24.0f),
               ly + scM.height + dpi(app, 4.0f), 5);
    }

    // --- Drop hint, centered at the bottom
    IDWriteTextLayout* drop = spLayout(app, tr(app, "start.drop"), 12.0f,
                                       DWRITE_FONT_WEIGHT_NORMAL, 4096.0f);
    DWRITE_TEXT_METRICS dm = spMetrics(drop);
    float dropY = H - dpi(app, 22.0f) - dm.height;
    float dashW = dpi(app, 26.0f);
    float gap = dpi(app, 10.0f);
    float total = dashW + gap + dm.width + gap + dashW;
    float dx = (viewW - total) * 0.5f;
    float lineY = dropY + dm.height * 0.55f;
    app.brush->SetColor(withA(text, 0.28f));
    for (float sx = 0.0f; sx < dashW; sx += dpi(app, 6.5f)) {
        float e1 = std::min(sx + dpi(app, 3.5f), dashW);
        app.renderTarget->DrawLine(D2D1::Point2F(dx + sx, lineY),
                                   D2D1::Point2F(dx + e1, lineY), app.brush,
                                   1.0f);
        float rx = dx + dashW + gap + dm.width + gap;
        app.renderTarget->DrawLine(D2D1::Point2F(rx + sx, lineY),
                                   D2D1::Point2F(rx + e1, lineY), app.brush,
                                   1.0f);
    }
    spDraw(app, drop, dx + dashW + gap, dropY, withA(text, 0.42f));
    if (drop) drop->Release();
}
