#include "signals.h"
#include "drafts.h"
#include "editor.h"
#include "i18n.h"
#include "utils.h"

#include <shellapi.h>
#include <algorithm>
#include <chrono>
#include <cwchar>

namespace {

constexpr float kDrainSeconds = 4.0f;
constexpr float kTuckSeconds = 0.18f;
constexpr size_t kTrayCap = 20;

bool pointIn(float x, float y, const D2D1_RECT_F& r) {
    return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
}

double nowSeconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// Severity hue on the theme's luminance row (design 13c: fixed pairs)
D2D1_COLOR_F signalHue(const App& app, int sev) {
    switch (sev) {
        case SIG_SUCCESS:
            return app.theme.isDark ? hexColor(0x7ED9A0) : hexColor(0x4E7A3A);
        case SIG_WARN:
            return app.theme.isDark ? hexColor(0xF0C27A) : hexColor(0xA66A12);
        case SIG_ERROR:
            return app.theme.isDark ? hexColor(0xF2716A) : hexColor(0xB0433A);
        default:
            return app.theme.accent;
    }
}

// Chip surfaces lift off the background like the settings cards
D2D1_COLOR_F chipSurface(const App& app, float extra = 0.0f) {
    float t = (app.theme.isDark ? 0.07f : 0.6f) + extra;
    D2D1_COLOR_F bg = app.theme.background;
    float target = app.theme.isDark ? 1.0f : 1.0f;
    bg.r = bg.r + (target - bg.r) * (app.theme.isDark ? t * 0.16f : t * 0.5f);
    bg.g = bg.g + (target - bg.g) * (app.theme.isDark ? t * 0.16f : t * 0.5f);
    bg.b = bg.b + (target - bg.b) * (app.theme.isDark ? t * 0.16f : t * 0.5f);
    bg.a = 0.97f;
    return bg;
}

D2D1_COLOR_F inkAt(const App& app, float alpha) {
    D2D1_COLOR_F c = app.theme.text;
    c.a = alpha;
    return c;
}

// Monoline icons drawn with primitives so every severity hue works
void drawSignalIcon(App& app, int icon, float cx, float cy, float s,
                    D2D1_COLOR_F color, float alpha) {
    color.a *= alpha;
    app.brush->SetColor(color);
    float w = std::max(1.1f, s * 0.14f);
    switch (icon) {
        case SIGI_CHECK:
            app.renderTarget->DrawLine(
                D2D1::Point2F(cx - s * 0.42f, cy + s * 0.05f),
                D2D1::Point2F(cx - s * 0.10f, cy + s * 0.36f), app.brush, w * 1.2f);
            app.renderTarget->DrawLine(
                D2D1::Point2F(cx - s * 0.10f, cy + s * 0.36f),
                D2D1::Point2F(cx + s * 0.44f, cy - s * 0.34f), app.brush, w * 1.2f);
            break;
        case SIGI_COPY: {
            D2D1_RECT_F back = D2D1::RectF(cx - s * 0.42f, cy - s * 0.42f,
                                           cx + s * 0.14f, cy + 0.14f * s);
            D2D1_RECT_F front = D2D1::RectF(cx - s * 0.14f, cy - s * 0.14f,
                                            cx + s * 0.42f, cy + s * 0.42f);
            app.renderTarget->DrawRectangle(back, app.brush, w);
            app.brush->SetColor(color);
            app.renderTarget->DrawRectangle(front, app.brush, w);
            break;
        }
        case SIGI_WARNING: {
            D2D1_POINT_2F a = D2D1::Point2F(cx, cy - s * 0.42f);
            D2D1_POINT_2F b = D2D1::Point2F(cx + s * 0.46f, cy + s * 0.36f);
            D2D1_POINT_2F c = D2D1::Point2F(cx - s * 0.46f, cy + s * 0.36f);
            app.renderTarget->DrawLine(a, b, app.brush, w);
            app.renderTarget->DrawLine(b, c, app.brush, w);
            app.renderTarget->DrawLine(c, a, app.brush, w);
            app.renderTarget->DrawLine(D2D1::Point2F(cx, cy - s * 0.1f),
                                       D2D1::Point2F(cx, cy + s * 0.12f),
                                       app.brush, w);
            app.renderTarget->DrawLine(D2D1::Point2F(cx, cy + s * 0.24f),
                                       D2D1::Point2F(cx, cy + s * 0.26f),
                                       app.brush, w);
            break;
        }
        case SIGI_FILE: {
            float left = cx - s * 0.3f, right = cx + s * 0.3f;
            float top = cy - s * 0.44f, bot = cy + s * 0.44f;
            float fold = s * 0.22f;
            app.renderTarget->DrawLine(D2D1::Point2F(left, top),
                                       D2D1::Point2F(right - fold, top), app.brush, w);
            app.renderTarget->DrawLine(D2D1::Point2F(right - fold, top),
                                       D2D1::Point2F(right, top + fold), app.brush, w);
            app.renderTarget->DrawLine(D2D1::Point2F(right, top + fold),
                                       D2D1::Point2F(right, bot), app.brush, w);
            app.renderTarget->DrawLine(D2D1::Point2F(right, bot),
                                       D2D1::Point2F(left, bot), app.brush, w);
            app.renderTarget->DrawLine(D2D1::Point2F(left, bot),
                                       D2D1::Point2F(left, top), app.brush, w);
            break;
        }
        case SIGI_UPDATE:
            app.renderTarget->DrawLine(D2D1::Point2F(cx, cy - s * 0.42f),
                                       D2D1::Point2F(cx, cy + s * 0.18f), app.brush, w);
            app.renderTarget->DrawLine(D2D1::Point2F(cx - s * 0.26f, cy - s * 0.08f),
                                       D2D1::Point2F(cx, cy + s * 0.18f), app.brush, w);
            app.renderTarget->DrawLine(D2D1::Point2F(cx + s * 0.26f, cy - s * 0.08f),
                                       D2D1::Point2F(cx, cy + s * 0.18f), app.brush, w);
            app.renderTarget->DrawLine(D2D1::Point2F(cx - s * 0.4f, cy + s * 0.4f),
                                       D2D1::Point2F(cx + s * 0.4f, cy + s * 0.4f),
                                       app.brush, w);
            break;
        default: {  // info circle
            D2D1_ELLIPSE e = D2D1::Ellipse(D2D1::Point2F(cx, cy), s * 0.44f, s * 0.44f);
            app.renderTarget->DrawEllipse(e, app.brush, w);
            app.renderTarget->DrawLine(D2D1::Point2F(cx, cy - s * 0.02f),
                                       D2D1::Point2F(cx, cy + s * 0.24f), app.brush, w);
            app.renderTarget->DrawLine(D2D1::Point2F(cx, cy - s * 0.22f),
                                       D2D1::Point2F(cx, cy - s * 0.20f), app.brush, w);
            break;
        }
    }
}

std::wstring parkTimestamp() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t buf[8];
    swprintf_s(buf, L"%02d:%02d", st.wHour, st.wMinute);
    return buf;
}

// Move an active chip into the tray (front, capped) and count the badge
void parkChip(App& app, App::SignalChip&& chip) {
    chip.traySub = chip.traySub.empty()
                       ? parkTimestamp()
                       : chip.traySub + L" \u00B7 " + parkTimestamp();
    chip.tuckT = -1.0f;
    chip.pinned = false;
    app.signalTray.insert(app.signalTray.begin(), std::move(chip));
    if (app.signalTray.size() > kTrayCap) app.signalTray.resize(kTrayCap);
    if (!app.signalTrayOpen) app.signalUnseen++;
}

// Run a chip's click action; returns true when the chip is resolved
bool runSignalAction(App& app, HWND hwnd, int action) {
    switch (action) {
        case SIGA_OPEN_RELEASE:
            ShellExecuteW(nullptr, L"open",
                          L"https://github.com/oipoistar/tinta/releases/latest",
                          nullptr, nullptr, SW_SHOWNORMAL);
            app.updateDismissed = true;
            return true;
        case SIGA_RESTORE_DRAFTS:
            draftsRecoverAll(app, hwnd);
            return true;
        case SIGA_RETRY_SAVE:
            if (app.editMode) saveEditorFile(app, hwnd);
            return true;
        default:
            return false;
    }
}

void runCloseAction(App& app, int closeAction) {
    switch (closeAction) {
        case SIGC_DISMISS_UPDATE:
            app.updateDismissed = true;
            app.updateDismissedVersion = app.updateVersion;
            break;
        case SIGC_DISCARD_DRAFTS:
            draftsDiscardAll(app);
            break;
        default:
            break;
    }
}

// One text layout with the emphasis range bolded
IDWriteTextLayout* chipTextLayout(App& app, const App::SignalChip& chip,
                                  IDWriteTextFormat* fmt, float maxW,
                                  float maxH, float* outWidth) {
    std::wstring full = chip.text;
    if (!chip.emph.empty()) full += chip.emph;
    IDWriteTextLayout* layout = nullptr;
    app.dwriteFactory->CreateTextLayout(full.c_str(), (UINT32)full.size(),
                                        fmt, maxW, maxH, &layout);
    if (layout && !chip.emph.empty()) {
        DWRITE_TEXT_RANGE range{(UINT32)chip.text.size(),
                                (UINT32)chip.emph.size()};
        layout->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD, range);
    }
    if (layout) {
        layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        layout->SetTrimming(&trim, nullptr);
        if (outWidth) {
            DWRITE_TEXT_METRICS m{};
            layout->GetMetrics(&m);
            *outWidth = std::min(m.widthIncludingTrailingWhitespace, maxW);
        }
    }
    return layout;
}

// Ring shadow shared with the floating panels
void chipShadow(App& app, const D2D1_RECT_F& r, float radius, float alpha) {
    for (int ring = 3; ring >= 1; ring--) {
        float spread = dpi(app, (float)ring * 2.2f);
        D2D1_COLOR_F shadow = D2D1::ColorF(
            0.0f, 0.0f, 0.0f,
            (app.theme.isDark ? 0.16f : 0.07f) * alpha / (float)(ring * ring));
        app.brush->SetColor(shadow);
        app.renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(r.left - spread, r.top - spread * 0.3f,
                                          r.right + spread, r.bottom + spread),
                              radius + spread, radius + spread),
            app.brush);
    }
}

}  // namespace

void signalPush(App& app, int severity, int icon, std::wstring text,
                std::wstring emph, std::wstring context, int action,
                int closeAction, bool drains, const D2D1_RECT_F* docAnchor) {
    App::SignalChip chip;
    chip.id = app.signalIdCounter++;
    chip.severity = severity;
    chip.icon = icon;
    chip.text = std::move(text);
    chip.emph = std::move(emph);
    chip.traySub = std::move(context);
    chip.action = action;
    chip.closeAction = closeAction;
    chip.drains = drains;
    chip.remaining = drains ? kDrainSeconds : 0.0f;
    if (docAnchor) {
        chip.anchored = true;
        chip.docAnchor = *docAnchor;
        chip.remaining = 2.0f;  // anchored Copied pills are brief
    }
    // An identical chip refreshing (repeat Ctrl+C) replaces its old self
    for (size_t i = 0; i < app.signalChips.size(); i++) {
        if (app.signalChips[i].text == chip.text &&
            app.signalChips[i].emph == chip.emph &&
            app.signalChips[i].icon == chip.icon) {
            app.signalChips.erase(app.signalChips.begin() + i);
            break;
        }
    }
    app.signalChips.push_back(std::move(chip));
    app.signalLastTick = nowSeconds();
    startNotificationTimer(app);
    if (app.hwnd) InvalidateRect(app.hwnd, nullptr, FALSE);
}

void signalPushKey(App& app, int severity, int icon, const char* trKey,
                   std::wstring emph, std::wstring context) {
    signalPush(app, severity, icon, tr(app, trKey), std::move(emph),
               std::move(context));
}

D2D1_COLOR_F signalSeverityHue(const App& app, int severity) {
    return signalHue(app, severity);
}

bool signalsNeedTicks(const App& app) {
    for (const auto& c : app.signalChips) {
        if (c.tuckT >= 0.0f) return true;
        if (c.drains && !c.pinned && c.id != app.signalHoverChip) return true;
    }
    return false;
}

void signalTuckPassive(App& app) {
    for (auto& c : app.signalChips) {
        if (c.drains && !c.pinned && c.tuckT < 0.0f) c.tuckT = 0.0f;
    }
    if (!app.signalChips.empty()) startNotificationTimer(app);
}

void renderSignalChips(App& app) {
    if (!app.dwriteFactory || !app.folderBrowserFormat) return;
    if (app.showPrintPreview || app.showLightbox) return;

    // Drain integration: hover holds, pins keep, expiry starts the tuck
    double now = nowSeconds();
    float dt = app.signalLastTick > 0.0
                   ? (float)std::min(0.25, now - app.signalLastTick)
                   : 0.0f;
    app.signalLastTick = now;
    for (size_t i = 0; i < app.signalChips.size();) {
        auto& c = app.signalChips[i];
        if (c.tuckT >= 0.0f) {
            c.tuckT += dt;
            if (c.tuckT >= kTuckSeconds) {
                parkChip(app, std::move(c));
                app.signalChips.erase(app.signalChips.begin() + i);
                continue;
            }
        } else if (c.drains && !c.pinned && c.id != app.signalHoverChip) {
            c.remaining -= dt;
            if (c.remaining <= 0.0f) c.tuckT = 0.0f;
        }
        i++;
    }

    IDWriteTextFormat* fmt = app.folderBrowserFormat;
    float margin = dpi(app, 14.0f);
    float bottom = (float)app.height - margin -
                   (app.showStats ? dpi(app, 55.0f) : 0.0f);
    float right = (float)app.width - margin;

    // The unsaved-exit / create-file prompt chip owns the corner slot
    if (!app.confirmExitPending && !app.createRefPending) {
        app.promptChipRect = D2D1_RECT_F{};
    } else if (app.promptChipRect.right > app.promptChipRect.left) {
        bottom = std::min(bottom, app.promptChipRect.top - dpi(app, 8.0f));
    }

    // Bell: bottom-right corner, only once something has parked
    app.signalBellRect = D2D1_RECT_F{};
    bool bellVisible = !app.signalTray.empty() || app.signalTrayOpen;
    if (bellVisible && !app.zenMode) {
        float bs = dpi(app, 27.0f);
        D2D1_RECT_F bell = D2D1::RectF(right - bs, bottom - bs, right, bottom);
        chipShadow(app, bell, dpi(app, 7.0f), 0.8f);
        app.brush->SetColor(chipSurface(app));
        app.renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(bell, dpi(app, 7.0f), dpi(app, 7.0f)), app.brush);
        app.brush->SetColor(inkAt(app, app.signalTrayOpen ? 0.35f : 0.14f));
        app.renderTarget->DrawRoundedRectangle(
            D2D1::RoundedRect(bell, dpi(app, 7.0f), dpi(app, 7.0f)), app.brush,
            1.0f);
        // Monoline bell: dome, brim, clapper
        float cx = (bell.left + bell.right) * 0.5f;
        float cy = (bell.top + bell.bottom) * 0.5f;
        float s = dpi(app, 13.0f);
        D2D1_COLOR_F ink = inkAt(app, 0.75f);
        app.brush->SetColor(ink);
        float w = 1.2f;
        app.renderTarget->DrawLine(D2D1::Point2F(cx - s * 0.38f, cy + s * 0.22f),
                                   D2D1::Point2F(cx + s * 0.38f, cy + s * 0.22f),
                                   app.brush, w);
        app.renderTarget->DrawLine(D2D1::Point2F(cx - s * 0.38f, cy + s * 0.22f),
                                   D2D1::Point2F(cx - s * 0.28f, cy - s * 0.05f),
                                   app.brush, w);
        app.renderTarget->DrawLine(D2D1::Point2F(cx + s * 0.38f, cy + s * 0.22f),
                                   D2D1::Point2F(cx + s * 0.28f, cy - s * 0.05f),
                                   app.brush, w);
        D2D1_ELLIPSE dome = D2D1::Ellipse(
            D2D1::Point2F(cx, cy - s * 0.05f), s * 0.28f, s * 0.34f);
        app.renderTarget->DrawEllipse(dome, app.brush, w);
        // Mask the dome's lower half so it reads as an arch, then clapper
        app.brush->SetColor(chipSurface(app));
        app.renderTarget->FillRectangle(
            D2D1::RectF(cx - s * 0.42f, cy + s * 0.02f, cx + s * 0.42f,
                        cy + s * 0.2f),
            app.brush);
        app.brush->SetColor(ink);
        app.renderTarget->DrawLine(D2D1::Point2F(cx - s * 0.38f, cy + s * 0.22f),
                                   D2D1::Point2F(cx + s * 0.38f, cy + s * 0.22f),
                                   app.brush, w);
        app.renderTarget->DrawLine(D2D1::Point2F(cx - s * 0.08f, cy + s * 0.36f),
                                   D2D1::Point2F(cx + s * 0.08f, cy + s * 0.36f),
                                   app.brush, w * 1.3f);
        if (app.signalUnseen > 0 && app.signalSmallFormat) {
            float bw = dpi(app, 13.0f);
            D2D1_RECT_F badge = D2D1::RectF(bell.right - bw + dpi(app, 3.0f),
                                            bell.top - dpi(app, 4.0f),
                                            bell.right + dpi(app, 3.0f),
                                            bell.top - dpi(app, 4.0f) + bw);
            app.brush->SetColor(app.theme.accent);
            app.renderTarget->FillRoundedRectangle(
                D2D1::RoundedRect(badge, bw * 0.5f, bw * 0.5f), app.brush);
            wchar_t n[4];
            swprintf_s(n, app.signalUnseen > 9 ? L"9+" : L"%d",
                       app.signalUnseen);
            D2D1_COLOR_F bink = app.theme.background;
            app.brush->SetColor(bink);
            IDWriteTextLayout* bl = nullptr;
            app.dwriteFactory->CreateTextLayout(n, (UINT32)wcslen(n),
                                                app.signalSmallFormat, bw,
                                                bw, &bl);
            if (bl) {
                bl->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                bl->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                app.renderTarget->DrawTextLayout(
                    D2D1::Point2F(badge.left, badge.top), bl, app.brush);
                bl->Release();
            }
        }
        app.signalBellRect = bell;
        bottom = bell.top - dpi(app, 8.0f);
    }

    // Active stack: oldest at the top, newest nearest the corner
    float chipH = dpi(app, 36.0f);
    float pad = dpi(app, 10.0f);
    float iconBox = dpi(app, 20.0f);
    float gap = dpi(app, 8.0f);
    float y = bottom;
    for (int idx = (int)app.signalChips.size() - 1; idx >= 0; idx--) {
        auto& c = app.signalChips[idx];
        c.rect = c.pinRect = c.closeRect = D2D1_RECT_F{};

        float tuck = c.tuckT >= 0.0f ? std::min(1.0f, c.tuckT / kTuckSeconds)
                                     : 0.0f;
        float alpha = 1.0f - tuck;

        if (c.anchored) {
            // Mini pill at its source block (document coords -> screen)
            float ax = c.docAnchor.right - app.scrollX + documentViewportX(app);
            float ay = c.docAnchor.top - app.scrollY;
            const wchar_t* label = c.text.c_str();
            float tw = measureText(app, c.text, app.signalSmallFormat
                                                    ? app.signalSmallFormat
                                                    : fmt);
            float ph = dpi(app, 21.0f);
            float pw = tw + dpi(app, 30.0f);
            float px = std::min(ax - pw * 0.5f,
                                (float)app.width - pw - dpi(app, 8.0f));
            float py = std::max(chromeTopHeight(app) + dpi(app, 4.0f),
                                ay - ph * 0.5f);
            D2D1_RECT_F pill = D2D1::RectF(px, py, px + pw, py + ph);
            app.brush->SetColor(chipSurface(app));
            D2D1_COLOR_F srf = chipSurface(app);
            srf.a = 0.97f * alpha;
            app.brush->SetColor(srf);
            app.renderTarget->FillRoundedRectangle(
                D2D1::RoundedRect(pill, ph * 0.5f, ph * 0.5f), app.brush);
            app.brush->SetColor(inkAt(app, 0.14f * alpha));
            app.renderTarget->DrawRoundedRectangle(
                D2D1::RoundedRect(pill, ph * 0.5f, ph * 0.5f), app.brush, 1.0f);
            drawSignalIcon(app, c.icon, pill.left + dpi(app, 12.0f),
                           py + ph * 0.5f, dpi(app, 10.0f),
                           signalHue(app, c.severity), alpha);
            if (app.signalSmallFormat) {
                app.brush->SetColor(inkAt(app, 0.9f * alpha));
                app.renderTarget->DrawText(
                    label, (UINT32)c.text.size(), app.signalSmallFormat,
                    D2D1::RectF(pill.left + dpi(app, 21.0f),
                                py + dpi(app, 4.0f), pill.right, pill.bottom),
                    app.brush);
            }
            c.rect = pill;
            continue;
        }

        bool hovered = c.id == app.signalHoverChip;
        float textMax = dpi(app, 250.0f);
        float textW = 0.0f;
        IDWriteTextLayout* layout =
            chipTextLayout(app, c, fmt, textMax, chipH, &textW);
        float extras = dpi(app, 20.0f) + (hovered || c.pinned ? dpi(app, 18.0f) : 0.0f);
        float chipW = pad + iconBox + dpi(app, 9.0f) + textW + dpi(app, 10.0f) +
                      extras + pad * 0.6f;
        chipW = std::max(chipW, dpi(app, 190.0f));
        float cxRight = right + tuck * dpi(app, 30.0f);
        D2D1_RECT_F r = D2D1::RectF(cxRight - chipW, y - chipH + tuck * dpi(app, 14.0f),
                                    cxRight, y + tuck * dpi(app, 14.0f));

        chipShadow(app, r, dpi(app, 10.0f), alpha * (c.drains ? 0.9f : 1.1f));
        D2D1_COLOR_F srf = chipSurface(app, c.drains ? 0.0f : 0.02f);
        srf.a = 0.97f * alpha;
        app.brush->SetColor(srf);
        app.renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(r, dpi(app, 10.0f), dpi(app, 10.0f)), app.brush);
        D2D1_COLOR_F border = c.drains ? inkAt(app, 0.13f * alpha)
                                       : signalHue(app, c.severity);
        if (!c.drains) border.a = 0.4f * alpha;
        app.brush->SetColor(border);
        app.renderTarget->DrawRoundedRectangle(
            D2D1::RoundedRect(r, dpi(app, 10.0f), dpi(app, 10.0f)), app.brush,
            1.0f);

        // Severity icon in its tinted square
        D2D1_COLOR_F hue = signalHue(app, c.severity);
        D2D1_COLOR_F tint = hue;
        tint.a = 0.14f * alpha;
        D2D1_RECT_F ib = D2D1::RectF(r.left + pad,
                                     r.top + (chipH - iconBox) * 0.5f,
                                     r.left + pad + iconBox,
                                     r.top + (chipH + iconBox) * 0.5f);
        app.brush->SetColor(tint);
        app.renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(ib, dpi(app, 6.0f), dpi(app, 6.0f)), app.brush);
        drawSignalIcon(app, c.icon, (ib.left + ib.right) * 0.5f,
                       (ib.top + ib.bottom) * 0.5f, dpi(app, 12.0f), hue,
                       alpha);

        // Text with the bold tail
        if (layout) {
            app.brush->SetColor(inkAt(app, 0.92f * alpha));
            DWRITE_TEXT_METRICS m{};
            layout->GetMetrics(&m);
            app.renderTarget->DrawTextLayout(
                D2D1::Point2F(ib.right + dpi(app, 9.0f),
                              r.top + (chipH - m.height) * 0.5f),
                layout, app.brush);
        }

        // Pin (hover or pinned) then the cross
        float tx = r.right - pad * 0.6f - dpi(app, 12.0f);
        D2D1_COLOR_F ink = inkAt(app, 0.45f * alpha);
        app.brush->SetColor(ink);
        float cxg = tx + dpi(app, 6.0f), cyg = r.top + chipH * 0.5f;
        float sgl = dpi(app, 3.6f);
        app.renderTarget->DrawLine(D2D1::Point2F(cxg - sgl, cyg - sgl),
                                   D2D1::Point2F(cxg + sgl, cyg + sgl),
                                   app.brush, 1.3f);
        app.renderTarget->DrawLine(D2D1::Point2F(cxg - sgl, cyg + sgl),
                                   D2D1::Point2F(cxg + sgl, cyg - sgl),
                                   app.brush, 1.3f);
        c.closeRect = D2D1::RectF(tx - dpi(app, 4.0f), r.top, r.right, r.bottom);
        if (hovered || c.pinned) {
            float pxg = tx - dpi(app, 18.0f) + dpi(app, 6.0f);
            D2D1_COLOR_F pink = c.pinned ? hue : inkAt(app, 0.5f * alpha);
            if (c.pinned) pink.a = 0.9f * alpha;
            app.brush->SetColor(pink);
            // Map-pin: head circle + needle
            app.renderTarget->DrawEllipse(
                D2D1::Ellipse(D2D1::Point2F(pxg, cyg - dpi(app, 2.0f)),
                              dpi(app, 3.0f), dpi(app, 3.0f)),
                app.brush, 1.3f);
            app.renderTarget->DrawLine(
                D2D1::Point2F(pxg, cyg + dpi(app, 1.0f)),
                D2D1::Point2F(pxg, cyg + dpi(app, 6.0f)), app.brush, 1.3f);
            c.pinRect = D2D1::RectF(tx - dpi(app, 20.0f), r.top,
                                    tx - dpi(app, 2.0f), r.bottom);
        }

        // Drain rail: remaining lifespan; pinned wears the full quiet band
        if (c.drains) {
            D2D1_COLOR_F rail = hue;
            if (c.pinned) {
                rail.a = 0.22f * alpha;
                app.brush->SetColor(rail);
                app.renderTarget->FillRectangle(
                    D2D1::RectF(r.left + dpi(app, 8.0f), r.bottom - 2.0f,
                                r.right - dpi(app, 8.0f), r.bottom),
                    app.brush);
            } else {
                float frac = std::max(0.0f, std::min(1.0f, c.remaining / kDrainSeconds));
                rail.a = 0.5f * alpha;
                app.brush->SetColor(rail);
                float railW = (r.right - r.left - dpi(app, 16.0f)) * frac;
                app.renderTarget->FillRectangle(
                    D2D1::RectF(r.left + dpi(app, 8.0f), r.bottom - 2.0f,
                                r.left + dpi(app, 8.0f) + railW, r.bottom),
                    app.brush);
            }
            // Tiny state label while held or pinned
            if ((c.pinned || hovered) && app.signalSmallFormat) {
                const wchar_t* lbl = tr(app, c.pinned ? "signal.pinned"
                                                      : "signal.held");
                D2D1_COLOR_F lc = c.pinned ? hue : inkAt(app, 0.5f);
                lc.a *= alpha * (c.pinned ? 0.85f : 0.9f);
                app.brush->SetColor(lc);
                app.renderTarget->DrawText(
                    lbl, (UINT32)wcslen(lbl), app.signalSmallFormat,
                    D2D1::RectF(r.left + dpi(app, 10.0f), r.bottom - dpi(app, 13.0f),
                                r.right, r.bottom),
                    app.brush);
            }
        }
        if (layout) layout->Release();
        c.rect = r;
        y = r.top - gap;
    }

    // The tray: history above the bell, actions still live
    app.signalTrayRect = app.signalTrayClearRect = D2D1_RECT_F{};
    app.signalTrayHits.clear();
    if (app.signalTrayOpen && app.signalBellRect.right > 0.0f) {
        float tw = dpi(app, 302.0f);
        float rowH = dpi(app, 40.0f);
        float headH = dpi(app, 32.0f);
        float footH = dpi(app, 24.0f);
        int rows = (int)std::min<size_t>(app.signalTray.size(), 8);
        float th = headH + 1.0f + rows * rowH + footH;
        D2D1_RECT_F t = D2D1::RectF(app.signalBellRect.right - tw,
                                    app.signalBellRect.top - dpi(app, 8.0f) - th,
                                    app.signalBellRect.right,
                                    app.signalBellRect.top - dpi(app, 8.0f));
        chipShadow(app, t, dpi(app, 12.0f), 1.2f);
        app.brush->SetColor(chipSurface(app));
        app.renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(t, dpi(app, 12.0f), dpi(app, 12.0f)), app.brush);
        app.brush->SetColor(inkAt(app, 0.14f));
        app.renderTarget->DrawRoundedRectangle(
            D2D1::RoundedRect(t, dpi(app, 12.0f), dpi(app, 12.0f)), app.brush,
            1.0f);

        // Header: title, count badge, Clear all
        if (app.tocFormatBold) {
            app.brush->SetColor(inkAt(app, 0.95f));
            const wchar_t* title = tr(app, "signal.notifications");
            app.renderTarget->DrawText(
                title, (UINT32)wcslen(title), app.tocFormatBold,
                D2D1::RectF(t.left + dpi(app, 12.0f), t.top + dpi(app, 8.0f),
                            t.right, t.top + headH),
                app.brush);
        }
        if (app.signalSmallFormat) {
            const wchar_t* clear = tr(app, "signal.clear_all");
            float cw = measureText(app, clear, app.signalSmallFormat);
            D2D1_COLOR_F ac = app.theme.accent;
            app.brush->SetColor(ac);
            app.renderTarget->DrawText(
                clear, (UINT32)wcslen(clear), app.signalSmallFormat,
                D2D1::RectF(t.right - dpi(app, 12.0f) - cw,
                            t.top + dpi(app, 10.0f), t.right, t.top + headH),
                app.brush);
            app.signalTrayClearRect =
                D2D1::RectF(t.right - dpi(app, 18.0f) - cw, t.top,
                            t.right, t.top + headH);
        }
        app.brush->SetColor(inkAt(app, 0.1f));
        app.renderTarget->FillRectangle(
            D2D1::RectF(t.left + dpi(app, 12.0f), t.top + headH,
                        t.right - dpi(app, 12.0f), t.top + headH + 1.0f),
            app.brush);

        float ry = t.top + headH + 1.0f;
        for (int i = 0; i < rows; i++) {
            const auto& c = app.signalTray[i];
            D2D1_COLOR_F hue = signalHue(app, c.severity);
            D2D1_RECT_F ib = D2D1::RectF(t.left + dpi(app, 12.0f),
                                         ry + (rowH - dpi(app, 22.0f)) * 0.5f,
                                         t.left + dpi(app, 12.0f) + dpi(app, 22.0f),
                                         ry + (rowH + dpi(app, 22.0f)) * 0.5f);
            app.brush->SetColor(inkAt(app, 0.06f));
            app.renderTarget->FillRoundedRectangle(
                D2D1::RoundedRect(ib, dpi(app, 6.0f), dpi(app, 6.0f)),
                app.brush);
            drawSignalIcon(app, c.icon, (ib.left + ib.right) * 0.5f,
                           (ib.top + ib.bottom) * 0.5f, dpi(app, 12.0f), hue,
                           1.0f);
            // Action button first so the text knows its room
            float textRight = t.right - dpi(app, 12.0f);
            if (c.action != SIGA_NONE && app.signalSmallFormat) {
                const wchar_t* actLbl =
                    tr(app, c.action == SIGA_RETRY_SAVE ? "signal.retry"
                       : c.action == SIGA_RESTORE_DRAFTS ? "signal.restore"
                                                         : "settings.open");
                float aw = measureText(app, actLbl, app.signalSmallFormat) +
                           dpi(app, 18.0f);
                D2D1_RECT_F ab = D2D1::RectF(
                    t.right - dpi(app, 12.0f) - aw,
                    ry + (rowH - dpi(app, 21.0f)) * 0.5f,
                    t.right - dpi(app, 12.0f),
                    ry + (rowH + dpi(app, 21.0f)) * 0.5f);
                D2D1_COLOR_F ac = app.theme.accent;
                ac.a = 0.55f;
                app.brush->SetColor(ac);
                app.renderTarget->DrawRoundedRectangle(
                    D2D1::RoundedRect(ab, dpi(app, 6.0f), dpi(app, 6.0f)),
                    app.brush, 1.0f);
                app.brush->SetColor(app.theme.accent);
                app.renderTarget->DrawText(
                    actLbl, (UINT32)wcslen(actLbl), app.signalSmallFormat,
                    D2D1::RectF(ab.left + dpi(app, 9.0f), ab.top + dpi(app, 4.0f),
                                ab.right, ab.bottom),
                    app.brush);
                app.signalTrayHits.push_back({ab, c.id});
                textRight = ab.left - dpi(app, 8.0f);
            }
            // Title line + parked-context subtitle
            App::SignalChip flat = c;
            flat.traySub.clear();
            IDWriteTextLayout* tl = chipTextLayout(
                app, flat, fmt, textRight - ib.right - dpi(app, 9.0f),
                rowH, nullptr);
            if (tl) {
                app.brush->SetColor(inkAt(app, 0.9f));
                app.renderTarget->DrawTextLayout(
                    D2D1::Point2F(ib.right + dpi(app, 9.0f), ry + dpi(app, 5.0f)),
                    tl, app.brush);
                tl->Release();
            }
            if (!c.traySub.empty() && app.signalSmallFormat) {
                app.brush->SetColor(inkAt(app, 0.45f));
                app.renderTarget->DrawText(
                    c.traySub.c_str(), (UINT32)c.traySub.size(),
                    app.signalSmallFormat,
                    D2D1::RectF(ib.right + dpi(app, 9.0f), ry + dpi(app, 21.0f),
                                textRight, ry + rowH),
                    app.brush);
            }
            ry += rowH;
        }

        app.brush->SetColor(inkAt(app, 0.1f));
        app.renderTarget->FillRectangle(
            D2D1::RectF(t.left + dpi(app, 12.0f), ry, t.right - dpi(app, 12.0f),
                        ry + 1.0f),
            app.brush);
        if (app.signalSmallFormat) {
            const wchar_t* hint = tr(app, "signal.parked");
            app.brush->SetColor(inkAt(app, 0.48f));
            app.renderTarget->DrawText(
                hint, (UINT32)wcslen(hint), app.signalSmallFormat,
                D2D1::RectF(t.left + dpi(app, 12.0f), ry + dpi(app, 6.0f),
                            t.right - dpi(app, 12.0f), t.bottom),
                app.brush);
        }
        app.signalTrayRect = t;
    }
}

bool signalMouseMove(App& app, float mx, float my) {
    int prev = app.signalHoverChip;
    app.signalHoverChip = 0;
    bool interactive = false;
    for (const auto& c : app.signalChips) {
        if (!c.anchored && c.tuckT < 0.0f && pointIn(mx, my, c.rect)) {
            app.signalHoverChip = c.id;
            interactive = true;
            break;
        }
    }
    if (!interactive && app.signalBellRect.right > 0.0f &&
        pointIn(mx, my, app.signalBellRect)) {
        interactive = true;
    }
    if (!interactive && app.signalTrayOpen &&
        pointIn(mx, my, app.signalTrayRect)) {
        interactive = true;
    }
    if (prev != app.signalHoverChip && app.hwnd) {
        startNotificationTimer(app);
        InvalidateRect(app.hwnd, nullptr, FALSE);
    }
    return interactive;
}

bool signalMouseDown(App& app, HWND hwnd, float mx, float my) {
    // Open tray first: rows act, anywhere else inside holds, outside closes
    if (app.signalTrayOpen) {
        if (pointIn(mx, my, app.signalTrayRect)) {
            if (pointIn(mx, my, app.signalTrayClearRect)) {
                app.signalTray.clear();
                app.signalUnseen = 0;
                app.signalTrayOpen = false;
            } else {
                for (const auto& hit : app.signalTrayHits) {
                    if (pointIn(mx, my, hit.first)) {
                        for (size_t i = 0; i < app.signalTray.size(); i++) {
                            if (app.signalTray[i].id == hit.second) {
                                int action = app.signalTray[i].action;
                                app.signalTray.erase(app.signalTray.begin() + i);
                                runSignalAction(app, hwnd, action);
                                break;
                            }
                        }
                        break;
                    }
                }
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return true;
        }
        app.signalTrayOpen = false;
        InvalidateRect(hwnd, nullptr, FALSE);
        // fall through: the closing click may also hit a chip or the bell
    }

    if (app.signalBellRect.right > 0.0f && pointIn(mx, my, app.signalBellRect)) {
        app.signalTrayOpen = !app.signalTrayOpen;
        if (app.signalTrayOpen) app.signalUnseen = 0;
        InvalidateRect(hwnd, nullptr, FALSE);
        return true;
    }

    for (size_t i = 0; i < app.signalChips.size(); i++) {
        auto& c = app.signalChips[i];
        if (c.anchored || c.tuckT >= 0.0f || !pointIn(mx, my, c.rect)) continue;
        if (pointIn(mx, my, c.closeRect)) {
            if (c.closeAction != SIGC_NONE) {
                // The cross resolves state: nothing left for the tray
                runCloseAction(app, c.closeAction);
                app.signalChips.erase(app.signalChips.begin() + i);
            } else {
                c.tuckT = 0.0f;  // park with its action still live
                startNotificationTimer(app);
            }
        } else if (c.pinRect.right > c.pinRect.left &&
                   pointIn(mx, my, c.pinRect)) {
            c.pinned = !c.pinned;
            if (!c.pinned) c.remaining = kDrainSeconds;
        } else if (c.action != SIGA_NONE) {
            int action = c.action;
            app.signalChips.erase(app.signalChips.begin() + i);
            runSignalAction(app, hwnd, action);
        } else {
            c.pinned = true;  // a body click on a passive chip keeps it
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return true;
    }
    return false;
}
