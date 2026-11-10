#include "folding.h"

#include "render.h"
#include "utils.h"

#include <algorithm>

bool foldingActive(const App& app) {
    return app.foldSections && !app.editMode && !app.printLayout;
}

static bool toggledHas(const App& app, const std::string& id) {
    return std::find(app.foldToggled.begin(), app.foldToggled.end(), id) != app.foldToggled.end();
}

bool headingFolded(const App& app, const std::string& id) {
    return app.foldAll != toggledHas(app, id);
}

void setHeadingFolded(App& app, const std::string& id, bool folded) {
    if (headingFolded(app, id) == folded) return;
    auto it = std::find(app.foldToggled.begin(), app.foldToggled.end(), id);
    if (it != app.foldToggled.end()) {
        app.foldToggled.erase(it);
    } else {
        app.foldToggled.push_back(id);
    }
    app.layoutDirty = true;
}

void toggleFold(App& app, const std::string& id) {
    setHeadingFolded(app, id, !headingFolded(app, id));
}

void foldAllSections(App& app, bool folded) {
    app.foldAll = folded;
    app.foldToggled.clear();
    app.layoutDirty = true;
}

bool anySectionFolded(const App& app) {
    for (const auto& fold : app.foldArrows) {
        if (fold.folded) return true;
    }
    return false;
}

void foldsLeave(App& app) {
    if (!app.currentFile.empty()) {
        app.foldStates[app.currentFile] = {app.foldAll, app.foldToggled};
    }
    app.foldAll = app.foldOnOpen;
    app.foldToggled.clear();
}

void foldsEnter(App& app) {
    const auto known = app.foldStates.find(app.currentFile);
    if (known != app.foldStates.end()) {
        app.foldAll = known->second.all;
        app.foldToggled = known->second.toggled;
        return;
    }
    app.foldAll = app.foldOnOpen;
    app.foldToggled.clear();
    if (app.foldAll && app.foldSections) app.pendingScrollRestore = -1.0f;
}

float headingTargetY(App& app, size_t heading) {
    ensureLayoutComplete(app);
    // The folded heading hiding it opens, then any folded inside that one,
    // as deep as headings go
    for (int depth = 0; depth < 6 && heading < app.headings.size(); ++depth) {
        const int under = app.headings[heading].hiddenUnder;
        if (under < 0 || under >= (int)app.headings.size()) break;
        setHeadingFolded(app, app.headings[under].id, false);
        ensureLayoutComplete(app);
    }
    return heading < app.headings.size() ? app.headings[heading].y : 0.0f;
}

bool unfoldSectionsMatching(App& app, const std::wstring& queryLower) {
    if (queryLower.empty() || !app.root) return false;
    bool opened = false;
    for (int depth = 0; depth < 6; ++depth) {
        const auto& blocks = app.root->children;
        std::vector<std::string> open;
        for (const auto& fold : app.foldArrows) {
            if (!fold.folded || fold.heading >= app.headings.size()) continue;
            std::wstring text;
            for (size_t b = fold.block + 1; b < fold.endBlock && b < blocks.size(); ++b) {
                extractText(blocks[b], text);
                text += L'\n';
            }
            if (toLower(text).find(queryLower) != std::wstring::npos) {
                open.push_back(app.headings[fold.heading].id);
            }
        }
        if (open.empty()) break;
        for (const auto& id : open) setHeadingFolded(app, id, false);
        opened = true;
        // The sections just opened lay out, with their own folded ones
        ensureLayoutComplete(app);
    }
    return opened;
}

int foldArrowAt(const App& app, float docX, float docY) {
    for (int i = 0; i < (int)app.foldArrows.size(); ++i) {
        const D2D1_RECT_F& r = app.foldArrows[i].arrow;
        if (docX >= r.left && docX <= r.right && docY >= r.top && docY <= r.bottom) return i;
    }
    return -1;
}

int foldLineAt(const App& app, float docX, float docY) {
    for (int i = 0; i < (int)app.foldArrows.size(); ++i) {
        const auto& fold = app.foldArrows[i];
        const D2D1_RECT_F& r = fold.line;
        const float top = std::min(r.top, fold.arrow.top), bottom = std::max(r.bottom, fold.arrow.bottom);
        if (docX >= r.left && docX <= r.right && docY >= top && docY <= bottom) return i;
    }
    return -1;
}

void foldArrowClick(App& app, int arrow) {
    if (arrow < 0 || arrow >= (int)app.foldArrows.size()) return;
    const size_t heading = app.foldArrows[arrow].heading;
    if (heading >= app.headings.size()) return;
    toggleFold(app, app.headings[heading].id);
}

void renderFoldArrows(App& app, float viewportTop, float viewportBottom) {
    if (app.foldArrows.empty() || !app.brush || !app.d2dFactory) return;
    const float scale = app.contentScale * app.zoomFactor;
    for (int i = 0; i < (int)app.foldArrows.size(); ++i) {
        const auto& fold = app.foldArrows[i];
        const bool hovered = i == app.hoveredFold;
        // An open section's arrow shows while its heading is under the
        // mouse; a folded one's always, so the hidden blocks are found again
        if (!fold.folded && !hovered) continue;
        if (fold.arrow.bottom < viewportTop || fold.arrow.top > viewportBottom) continue;
        const D2D1_RECT_F box = D2D1::RectF(fold.arrow.left - app.scrollX, fold.arrow.top - app.scrollY,
                                            fold.arrow.right - app.scrollX, fold.arrow.bottom - app.scrollY);
        const float cx = (box.left + box.right) * 0.5f, cy = (box.top + box.bottom) * 0.5f;
        // A small triangle, right for a folded section and down for an open
        // one, sized to its heading
        const float h = std::max(4.0f * scale, (fold.line.bottom - fold.line.top) * 0.15f);
        D2D1_POINT_2F p[3];
        if (fold.folded) {
            p[0] = D2D1::Point2F(cx - h * 0.6f, cy - h);
            p[1] = D2D1::Point2F(cx + h * 0.75f, cy);
            p[2] = D2D1::Point2F(cx - h * 0.6f, cy + h);
        } else {
            p[0] = D2D1::Point2F(cx - h, cy - h * 0.6f);
            p[1] = D2D1::Point2F(cx + h, cy - h * 0.6f);
            p[2] = D2D1::Point2F(cx, cy + h * 0.75f);
        }
        ID2D1PathGeometry* triangle = nullptr;
        if (FAILED(app.d2dFactory->CreatePathGeometry(&triangle)) || !triangle) continue;
        ID2D1GeometrySink* sink = nullptr;
        if (SUCCEEDED(triangle->Open(&sink)) && sink) {
            sink->BeginFigure(p[0], D2D1_FIGURE_BEGIN_FILLED);
            sink->AddLine(p[1]);
            sink->AddLine(p[2]);
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            sink->Close();
            sink->Release();
            D2D1_COLOR_F color = app.theme.text;
            color.a = hovered ? 0.8f : 0.5f;
            app.brush->SetColor(color);
            app.renderTarget->FillGeometry(triangle, app.brush);
            app.drawCalls++;
        }
        triangle->Release();
    }
}
