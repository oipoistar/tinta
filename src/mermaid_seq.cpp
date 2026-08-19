// Native Mermaid sequence diagram renderer (issue #108).
//
// Supported grammar: participant/actor declarations (with `as` aliases and
// <br/> line breaks), implicit participants, every arrow family
// (->, -->, ->>, -->>, -), --), -x, --x), activation shorthand +/- and
// activate/deactivate statements, Note over/left of/right of, autonumber,
// title, and loop/alt/else/opt/par/and/critical/option/break/rect frames.
// Unknown statements fail the parse so the document falls back to showing
// the source instead of silently dropping content.

#include "mermaid_ext.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <string>
#include <vector>

namespace mermaidext {
namespace detail {

namespace {

// Base geometry in unscaled units
constexpr float kHeaderPadX = 14.0f;
constexpr float kHeaderPadY = 9.0f;
constexpr float kHeaderMinWidth = 64.0f;
constexpr float kColumnMinGap = 46.0f;
constexpr float kMessageTextGap = 4.0f;   // text baseline gap above the line
constexpr float kRowGap = 16.0f;          // space after each message row
constexpr float kNotePadX = 10.0f;
constexpr float kNotePadY = 6.0f;
constexpr float kNoteMargin = 12.0f;
constexpr float kFramePad = 10.0f;
constexpr float kFrameLabelPadX = 8.0f;
constexpr float kFrameLabelPadY = 4.0f;
constexpr float kSelfLoopWidth = 36.0f;
constexpr float kActivationWidth = 9.0f;
constexpr float kActivationNudge = 5.0f;  // extra x-offset per nesting level
constexpr float kEdgePad = 6.0f;          // outer margin around the diagram

struct Participant {
    std::string id;
    std::string label;
    Size headerSize;
    float center = 0.0f;  // column x
};

enum class EventType {
    Message,
    Note,
    FrameOpen,     // loop/alt/opt/par/critical/break/rect
    FrameDivider,  // else/and/option
    FrameClose,    // end
    Activate,
    Deactivate,
};

enum class ArrowLine { Solid, Dashed };
enum class ArrowHead { Filled, Open, Cross, NoneHead };
enum class NotePos { Over, LeftOf, RightOf };

struct Event {
    EventType type = EventType::Message;
    size_t from = 0;
    size_t to = 0;
    std::string text;
    ArrowLine line = ArrowLine::Solid;
    ArrowHead head = ArrowHead::Filled;
    bool activateTarget = false;
    bool deactivateSource = false;
    NotePos notePos = NotePos::Over;
    std::string frameKeyword;  // loop/alt/...
    Size textSize;
    int number = 0;  // autonumber (0 = off)
};

struct Frame {
    size_t openEvent = 0;
    float top = 0.0f;
    float labelBottom = 0.0f;
    float bottom = 0.0f;
    size_t minColumn = SIZE_MAX;
    size_t maxColumn = 0;
    float extraLeft = 0.0f;   // note/self-loop overhang past the edge columns
    float extraRight = 0.0f;
    int depth = 0;
    std::vector<std::pair<float, std::string>> dividers;  // y + label
    std::string keyword;
    std::string label;
    Size labelSize;
    Size keywordSize;
};

struct ActivationSpan {
    size_t participant = 0;
    int level = 0;
    float top = 0.0f;
    float bottom = 0.0f;
};

bool parseArrow(std::string_view text, size_t& position, ArrowLine& line,
                ArrowHead& head, bool& activate, bool& deactivate) {
    // Longest match first: -->> --> --) --x ->> -> -) -x --
    struct Pattern {
        std::string_view token;
        ArrowLine line;
        ArrowHead head;
    };
    static const Pattern kPatterns[] = {
        {"-->>", ArrowLine::Dashed, ArrowHead::Filled},
        {"--)", ArrowLine::Dashed, ArrowHead::Open},
        {"--x", ArrowLine::Dashed, ArrowHead::Cross},
        {"-->", ArrowLine::Dashed, ArrowHead::NoneHead},
        {"->>", ArrowLine::Solid, ArrowHead::Filled},
        {"-)", ArrowLine::Solid, ArrowHead::Open},
        {"-x", ArrowLine::Solid, ArrowHead::Cross},
        {"->", ArrowLine::Solid, ArrowHead::NoneHead},
    };
    for (const auto& pattern : kPatterns) {
        if (text.substr(position, pattern.token.size()) == pattern.token) {
            position += pattern.token.size();
            line = pattern.line;
            head = pattern.head;
            activate = false;
            deactivate = false;
            if (position < text.size() && text[position] == '+') {
                activate = true;
                position++;
            } else if (position < text.size() && text[position] == '-') {
                deactivate = true;
                position++;
            }
            return true;
        }
    }
    return false;
}

}  // namespace

Built buildSequence(std::string_view source, const Measure& measure,
                    float scale) {
    Built result;
    auto lines = diagramLines(source);
    if (lines.empty()) {
        result.error = "Empty diagram";
        return result;
    }

    std::vector<Participant> participants;
    std::map<std::string, size_t, std::less<>> participantIds;
    std::vector<Event> events;
    std::string title;
    bool autonumber = false;
    int messageNumber = 0;
    int frameDepth = 0;

    auto ensureParticipant = [&](std::string_view id,
                                 std::string_view label) -> size_t {
        auto found = participantIds.find(id);
        if (found != participantIds.end()) {
            if (!label.empty()) {
                participants[found->second].label = cleanLabel(label);
            }
            return found->second;
        }
        Participant participant;
        participant.id = std::string(id);
        participant.label = cleanLabel(label.empty() ? id : label);
        participants.push_back(std::move(participant));
        participantIds.emplace(std::string(id), participants.size() - 1);
        return participants.size() - 1;
    };

    bool sawHeader = false;
    for (std::string_view line : lines) {
        if (!sawHeader) {
            if (!startsWithWord(line, "sequenceDiagram")) {
                result.error = "Expected sequenceDiagram header";
                return result;
            }
            sawHeader = true;
            continue;
        }

        std::string_view rest;
        if (startsWithWord(line, "participant", &rest) ||
            startsWithWord(line, "actor", &rest)) {
            size_t asPos = std::string_view::npos;
            for (size_t i = 0; i + 4 <= rest.size(); i++) {
                if (rest.substr(i, 4) == " as " ||
                    (rest.substr(i, 4) == " AS ")) {
                    asPos = i;
                    break;
                }
            }
            if (asPos == std::string_view::npos) {
                ensureParticipant(trimView(rest), {});
            } else {
                ensureParticipant(trimView(rest.substr(0, asPos)),
                                  trimView(rest.substr(asPos + 4)));
            }
            continue;
        }
        if (startsWithWord(line, "autonumber", &rest)) {
            autonumber = !startsWithWord(rest, "off");
            continue;
        }
        if (startsWithWord(line, "title", &rest)) {
            title = cleanLabel(rest);
            continue;
        }
        if (startsWithWord(line, "link", &rest) ||
            startsWithWord(line, "links", &rest) ||
            startsWithWord(line, "box", &rest)) {
            // Participant links are hover metadata in mermaid.js; boxes are
            // cosmetic groupings. Ignore both rather than failing.
            continue;
        }
        if (startsWithWord(line, "end") && frameDepth == 0 &&
            line.size() == 3) {
            // stray `end` closing an ignored box
            continue;
        }
        if (startsWithWord(line, "activate", &rest) ||
            startsWithWord(line, "deactivate", &rest)) {
            Event event;
            event.type = startsWithWord(line, "activate", nullptr)
                             ? EventType::Activate
                             : EventType::Deactivate;
            event.from = ensureParticipant(trimView(rest), {});
            events.push_back(std::move(event));
            continue;
        }
        if (startsWithWord(line, "note", &rest)) {
            Event event;
            event.type = EventType::Note;
            std::string_view spec = rest;
            if (startsWithWord(spec, "over", &spec)) {
                event.notePos = NotePos::Over;
            } else if (startsWithWord(spec, "left", &spec)) {
                startsWithWord(spec, "of", &spec);
                event.notePos = NotePos::LeftOf;
            } else if (startsWithWord(spec, "right", &spec)) {
                startsWithWord(spec, "of", &spec);
                event.notePos = NotePos::RightOf;
            } else {
                result.error = "Unsupported note position";
                return result;
            }
            size_t colon = spec.find(':');
            if (colon == std::string_view::npos) {
                result.error = "Note without text";
                return result;
            }
            std::string_view targets = trimView(spec.substr(0, colon));
            event.text = cleanLabel(trimView(spec.substr(colon + 1)));
            size_t comma = targets.find(',');
            if (comma == std::string_view::npos) {
                event.from = ensureParticipant(trimView(targets), {});
                event.to = event.from;
            } else {
                event.from =
                    ensureParticipant(trimView(targets.substr(0, comma)), {});
                event.to = ensureParticipant(
                    trimView(targets.substr(comma + 1)), {});
                if (event.from > event.to) std::swap(event.from, event.to);
            }
            events.push_back(std::move(event));
            continue;
        }
        {
            std::string_view frameRest;
            static const char* kFrames[] = {"loop", "alt", "opt", "par",
                                            "critical", "break", "rect"};
            bool isFrame = false;
            for (const char* keyword : kFrames) {
                if (startsWithWord(line, keyword, &frameRest)) {
                    Event event;
                    event.type = EventType::FrameOpen;
                    event.frameKeyword = keyword;
                    event.text = (event.frameKeyword == "rect")
                                     ? std::string()
                                     : cleanLabel(frameRest);
                    events.push_back(std::move(event));
                    frameDepth++;
                    isFrame = true;
                    break;
                }
            }
            if (isFrame) continue;
            static const char* kDividers[] = {"else", "and", "option"};
            bool isDivider = false;
            for (const char* keyword : kDividers) {
                if (startsWithWord(line, keyword, &frameRest)) {
                    if (frameDepth == 0) {
                        result.error = "Divider outside a frame";
                        return result;
                    }
                    Event event;
                    event.type = EventType::FrameDivider;
                    event.frameKeyword = keyword;
                    event.text = cleanLabel(frameRest);
                    events.push_back(std::move(event));
                    isDivider = true;
                    break;
                }
            }
            if (isDivider) continue;
            if (startsWithWord(line, "end")) {
                if (frameDepth == 0) {
                    result.error = "end without an open frame";
                    return result;
                }
                Event event;
                event.type = EventType::FrameClose;
                events.push_back(std::move(event));
                frameDepth--;
                continue;
            }
        }

        // Message: <id> <arrow> <id> [: text]
        {
            size_t arrowPos = std::string_view::npos;
            size_t probe = 0;
            ArrowLine arrowLine = ArrowLine::Solid;
            ArrowHead arrowHead = ArrowHead::Filled;
            bool activate = false, deactivate = false;
            // Find the first position where an arrow token parses
            for (size_t i = 0; i < line.size(); i++) {
                if (line[i] != '-') continue;
                size_t position = i;
                if (parseArrow(line, position, arrowLine, arrowHead, activate,
                               deactivate)) {
                    arrowPos = i;
                    probe = position;
                    break;
                }
            }
            if (arrowPos == std::string_view::npos || arrowPos == 0) {
                result.error = "Unsupported sequence statement";
                return result;
            }
            std::string_view fromId = trimView(line.substr(0, arrowPos));
            std::string_view afterArrow = line.substr(probe);
            std::string_view toId = afterArrow;
            std::string_view text;
            size_t colon = afterArrow.find(':');
            if (colon != std::string_view::npos) {
                toId = trimView(afterArrow.substr(0, colon));
                text = trimView(afterArrow.substr(colon + 1));
            } else {
                toId = trimView(afterArrow);
            }
            if (fromId.empty() || toId.empty()) {
                result.error = "Message needs two participants";
                return result;
            }
            Event event;
            event.type = EventType::Message;
            event.from = ensureParticipant(fromId, {});
            event.to = ensureParticipant(toId, {});
            event.text = cleanLabel(text);
            event.line = arrowLine;
            event.head = arrowHead;
            event.activateTarget = activate;
            event.deactivateSource = deactivate;
            if (autonumber) event.number = ++messageNumber;
            events.push_back(std::move(event));
        }
    }

    if (participants.empty()) {
        result.error = "No participants";
        return result;
    }

    // --- measure ---
    TextStyle headerStyle;
    headerStyle.bold = true;
    TextStyle textStyle;
    TextStyle frameKeywordStyle;
    frameKeywordStyle.bold = true;
    frameKeywordStyle.scale = 0.9f;
    TextStyle frameLabelStyle;
    frameLabelStyle.scale = 0.9f;
    frameLabelStyle.italic = true;

    for (auto& participant : participants) {
        Size measured = measure(participant.label, headerStyle, 0.0f);
        participant.headerSize.w = std::max(kHeaderMinWidth * scale,
                                            measured.w + kHeaderPadX * 2 * scale);
        participant.headerSize.h = measured.h + kHeaderPadY * 2 * scale;
    }
    float headerHeight = 0.0f;
    for (const auto& participant : participants) {
        headerHeight = std::max(headerHeight, participant.headerSize.h);
    }

    for (auto& event : events) {
        if (event.type == EventType::Message) {
            std::string display = event.text;
            if (event.number > 0) {
                display = std::to_string(event.number) + ". " + display;
            }
            event.text = display;
            if (!display.empty()) {
                event.textSize = measure(display, textStyle, 0.0f);
            }
        } else if (event.type == EventType::Note) {
            Size measured = measure(event.text, textStyle, 220.0f * scale);
            event.textSize.w = measured.w + kNotePadX * 2 * scale;
            event.textSize.h = measured.h + kNotePadY * 2 * scale;
        }
    }

    // --- column spacing ---
    size_t count = participants.size();
    std::vector<float> gaps(count > 0 ? count - 1 : 0,
                            kColumnMinGap * scale);
    float leftExtra = 0.0f;   // notes hanging left of the first column
    float rightExtra = 0.0f;  // self loops / notes right of the last column

    auto requireSpan = [&](size_t a, size_t b, float needed) {
        if (a == b) return;
        if (a > b) std::swap(a, b);
        float current = 0.0f;
        for (size_t i = a; i < b; i++) current += gaps[i];
        // Adjacent columns must clear each other's header halves too
        if (current < needed) {
            float add = (needed - current) / static_cast<float>(b - a);
            for (size_t i = a; i < b; i++) gaps[i] += add;
        }
    };

    // Headers must not overlap
    for (size_t i = 0; i + 1 < count; i++) {
        requireSpan(i, i + 1,
                    participants[i].headerSize.w * 0.5f +
                        participants[i + 1].headerSize.w * 0.5f +
                        kColumnMinGap * scale * 0.5f);
    }

    for (auto& event : events) {
        if (event.type == EventType::Message) {
            if (event.from == event.to) {
                float needed = kSelfLoopWidth * scale + event.textSize.w +
                               10.0f * scale;
                if (event.from + 1 < count) {
                    requireSpan(event.from, event.from + 1, needed);
                } else {
                    rightExtra = std::max(rightExtra, needed);
                }
            } else {
                requireSpan(event.from, event.to,
                            event.textSize.w + 20.0f * scale);
            }
        } else if (event.type == EventType::Note) {
            if (event.notePos == NotePos::Over) {
                if (event.from == event.to) {
                    float half = event.textSize.w * 0.5f;
                    if (event.from > 0) {
                        requireSpan(event.from - 1, event.from, half);
                    } else {
                        leftExtra = std::max(leftExtra, half);
                    }
                    if (event.from + 1 < count) {
                        requireSpan(event.from, event.from + 1, half);
                    } else {
                        rightExtra = std::max(rightExtra, half);
                    }
                } else {
                    requireSpan(event.from, event.to,
                                event.textSize.w - 2.0f * kNoteMargin * scale);
                }
            } else if (event.notePos == NotePos::LeftOf) {
                float needed = event.textSize.w + kNoteMargin * scale;
                if (event.from > 0) {
                    requireSpan(event.from - 1, event.from, needed);
                } else {
                    leftExtra = std::max(leftExtra, needed);
                }
            } else {
                float needed = event.textSize.w + kNoteMargin * scale;
                if (event.from + 1 < count) {
                    requireSpan(event.from, event.from + 1, needed);
                } else {
                    rightExtra = std::max(rightExtra, needed);
                }
            }
        }
    }

    // Frames inset horizontally: reserve room for the deepest nesting
    int maxDepth = 0, depthNow = 0;
    for (const auto& event : events) {
        if (event.type == EventType::FrameOpen) {
            depthNow++;
            maxDepth = std::max(maxDepth, depthNow);
        } else if (event.type == EventType::FrameClose) {
            depthNow--;
        }
    }
    float frameReserve = maxDepth * kFramePad * scale +
                         (maxDepth > 0 ? 4.0f * scale : 0.0f);
    float x = kEdgePad * scale + leftExtra +
              participants[0].headerSize.w * 0.5f + frameReserve;
    for (size_t i = 0; i < count; i++) {
        participants[i].center = x;
        if (i + 1 < count) x += gaps[i];
    }
    float lastHeaderHalf = participants.back().headerSize.w * 0.5f;
    float diagramWidth = participants.back().center + lastHeaderHalf +
                         rightExtra + frameReserve + kEdgePad * scale;

    // --- vertical layout + primitive emission ---
    std::vector<Prim>& prims = result.prims;
    float y = kEdgePad * scale;

    if (!title.empty()) {
        TextStyle titleStyle;
        titleStyle.bold = true;
        titleStyle.scale = 1.1f;
        Size titleSize = measure(title, titleStyle, 0.0f);
        Prim text;
        text.type = PrimType::Text;
        text.text = title;
        text.style = titleStyle;
        text.fill = Role::Text;
        text.x1 = 0;
        text.y1 = y;
        text.x2 = diagramWidth;
        text.y2 = y + titleSize.h;
        prims.push_back(std::move(text));
        y += titleSize.h + 10.0f * scale;
    }

    float headerTop = y;
    float headerBottom = y + headerHeight;

    auto emitHeaders = [&](float top) {
        for (const auto& participant : participants) {
            Prim box;
            box.type = PrimType::RoundRect;
            box.radius = 6.0f * scale;
            box.x1 = participant.center - participant.headerSize.w * 0.5f;
            box.x2 = participant.center + participant.headerSize.w * 0.5f;
            box.y1 = top;
            box.y2 = top + headerHeight;
            box.fill = Role::Fill;
            box.stroke = Role::Stroke;
            box.strokeWidth = 1.5f * scale;
            prims.push_back(box);

            Prim text;
            text.type = PrimType::Text;
            text.text = participant.label;
            text.style = headerStyle;
            text.fill = Role::Text;
            text.x1 = box.x1;
            text.y1 = box.y1;
            text.x2 = box.x2;
            text.y2 = box.y2;
            prims.push_back(std::move(text));
        }
    };
    emitHeaders(headerTop);
    y = headerBottom + 14.0f * scale;

    std::vector<Frame> frameStack;
    std::vector<Frame> closedFrames;
    std::vector<int> activationDepth(count, 0);
    std::vector<ActivationSpan> openActivations;
    std::vector<ActivationSpan> closedActivations;

    auto lifelineX = [&](size_t participant) {
        return participants[participant].center;
    };
    auto activationEdgeX = [&](size_t participant, bool towardRight) {
        int depth = activationDepth[participant];
        if (depth <= 0) return lifelineX(participant);
        float centerShift = (depth - 1) * kActivationNudge * scale;
        float half = kActivationWidth * 0.5f * scale;
        return lifelineX(participant) + centerShift +
               (towardRight ? half : -half);
    };
    auto pushActivation = [&](size_t participant, float top) {
        activationDepth[participant]++;
        ActivationSpan span;
        span.participant = participant;
        span.level = activationDepth[participant];
        span.top = top;
        openActivations.push_back(span);
    };
    auto popActivation = [&](size_t participant, float bottom) {
        for (size_t i = openActivations.size(); i-- > 0;) {
            if (openActivations[i].participant == participant &&
                openActivations[i].level == activationDepth[participant]) {
                openActivations[i].bottom = bottom;
                closedActivations.push_back(openActivations[i]);
                openActivations.erase(openActivations.begin() + i);
                activationDepth[participant]--;
                return;
            }
        }
    };
    auto touchFrame = [&](size_t columnA, size_t columnB,
                          float overhangLeft = 0.0f,
                          float overhangRight = 0.0f) {
        if (frameStack.empty()) return;
        for (auto& frame : frameStack) {
            frame.minColumn = std::min(frame.minColumn, std::min(columnA, columnB));
            frame.maxColumn = std::max(frame.maxColumn, std::max(columnA, columnB));
            frame.extraLeft = std::max(frame.extraLeft, overhangLeft);
            frame.extraRight = std::max(frame.extraRight, overhangRight);
        }
    };

    for (auto& event : events) {
        switch (event.type) {
            case EventType::Activate:
                pushActivation(event.from, y);
                break;
            case EventType::Deactivate:
                popActivation(event.from, y);
                break;
            case EventType::FrameOpen: {
                Frame frame;
                frame.keyword = event.frameKeyword;
                frame.label = event.text;
                frame.top = y;
                frame.depth = static_cast<int>(frameStack.size());
                frame.keywordSize =
                    measure(frame.keyword, frameKeywordStyle, 0.0f);
                if (!frame.label.empty()) {
                    frame.labelSize =
                        measure("[" + frame.label + "]", frameLabelStyle, 0.0f);
                }
                float labelHeight = std::max(frame.keywordSize.h,
                                             frame.labelSize.h) +
                                    kFrameLabelPadY * 2 * scale;
                frame.labelBottom = y + labelHeight;
                frameStack.push_back(std::move(frame));
                y += labelHeight + 8.0f * scale;
                break;
            }
            case EventType::FrameDivider: {
                if (frameStack.empty()) break;
                y += 4.0f * scale;
                std::string label = event.text.empty()
                                        ? event.frameKeyword
                                        : "[" + event.text + "]";
                frameStack.back().dividers.emplace_back(y, label);
                Size labelSize = measure(label, frameLabelStyle, 0.0f);
                y += labelSize.h + 10.0f * scale;
                break;
            }
            case EventType::FrameClose: {
                if (frameStack.empty()) break;
                Frame frame = frameStack.back();
                frameStack.pop_back();
                y += 6.0f * scale;
                if (frame.minColumn == SIZE_MAX) {
                    frame.minColumn = 0;
                    frame.maxColumn = count - 1;
                }
                frame.bottom = y;
                // Inner frames widen their enclosing frames
                if (!frameStack.empty()) {
                    for (auto& outer : frameStack) {
                        outer.minColumn =
                            std::min(outer.minColumn, frame.minColumn);
                        outer.maxColumn =
                            std::max(outer.maxColumn, frame.maxColumn);
                    }
                }
                closedFrames.push_back(std::move(frame));
                y += 10.0f * scale;
                break;
            }
            case EventType::Note: {
                float noteOverhangLeft =
                    event.notePos == NotePos::LeftOf
                        ? event.textSize.w + 8.0f * scale
                        : (event.notePos == NotePos::Over && event.from == event.to
                               ? event.textSize.w * 0.5f
                               : 0.0f);
                float noteOverhangRight =
                    event.notePos == NotePos::RightOf
                        ? event.textSize.w + 8.0f * scale
                        : (event.notePos == NotePos::Over && event.from == event.to
                               ? event.textSize.w * 0.5f
                               : 0.0f);
                touchFrame(event.from, event.to, noteOverhangLeft,
                           noteOverhangRight);
                float left, right;
                if (event.notePos == NotePos::Over) {
                    left = lifelineX(event.from) -
                           (event.from == event.to
                                ? event.textSize.w * 0.5f
                                : kNoteMargin * scale);
                    right = lifelineX(event.to) +
                            (event.from == event.to
                                 ? event.textSize.w * 0.5f
                                 : kNoteMargin * scale);
                    if (right - left < event.textSize.w) {
                        float middle = (left + right) * 0.5f;
                        left = middle - event.textSize.w * 0.5f;
                        right = middle + event.textSize.w * 0.5f;
                    }
                } else if (event.notePos == NotePos::LeftOf) {
                    right = lifelineX(event.from) - 8.0f * scale;
                    left = right - event.textSize.w;
                } else {
                    left = lifelineX(event.from) + 8.0f * scale;
                    right = left + event.textSize.w;
                }
                Prim box;
                box.type = PrimType::Rect;
                box.x1 = left;
                box.y1 = y;
                box.x2 = right;
                box.y2 = y + event.textSize.h;
                box.fill = Role::AccentSoft;
                box.stroke = Role::Stroke;
                box.strokeWidth = 1.0f * scale;
                prims.push_back(box);
                Prim text;
                text.type = PrimType::Text;
                text.text = event.text;
                text.style = textStyle;
                text.fill = Role::Text;
                text.x1 = left + kNotePadX * scale;
                text.y1 = y + kNotePadY * scale;
                text.x2 = right - kNotePadX * scale;
                text.y2 = y + event.textSize.h - kNotePadY * scale;
                prims.push_back(std::move(text));
                y += event.textSize.h + kRowGap * scale;
                break;
            }
            case EventType::Message: {
                float selfOverhang =
                    event.from == event.to
                        ? kSelfLoopWidth * scale + event.textSize.w +
                              16.0f * scale
                        : 0.0f;
                touchFrame(event.from, event.to, 0.0f, selfOverhang);
                float textHeight =
                    event.text.empty() ? 0.0f : event.textSize.h;
                if (event.from == event.to) {
                    // Self message: text right of a small loop
                    float lineX = activationEdgeX(event.from, true);
                    float loopRight = lineX + kSelfLoopWidth * scale;
                    float topY = y + textHeight * 0.5f;
                    float bottomY = topY + 14.0f * scale;
                    if (event.activateTarget) {
                        pushActivation(event.from, bottomY);
                    }
                    if (!event.text.empty()) {
                        Prim text;
                        text.type = PrimType::Text;
                        text.text = event.text;
                        text.style = textStyle;
                        text.fill = Role::Text;
                        text.alignH = -1;
                        text.x1 = loopRight + 6.0f * scale;
                        text.y1 = y;
                        text.x2 = loopRight + 6.0f * scale + event.textSize.w;
                        text.y2 = y + textHeight + 14.0f * scale;
                        prims.push_back(std::move(text));
                    }
                    auto addSegment = [&](float ax, float ay, float bx,
                                          float by, bool arrowEnd) {
                        Prim segment;
                        segment.type = PrimType::Line;
                        segment.x1 = ax;
                        segment.y1 = ay;
                        segment.x2 = bx;
                        segment.y2 = by;
                        segment.stroke = Role::Muted;
                        segment.strokeWidth = 1.4f * scale;
                        segment.dashed = event.line == ArrowLine::Dashed;
                        if (arrowEnd) {
                            segment.arrow = event.head == ArrowHead::Filled;
                            segment.openArrow = event.head != ArrowHead::Filled;
                        }
                        prims.push_back(segment);
                    };
                    addSegment(lineX, topY, loopRight, topY, false);
                    addSegment(loopRight, topY, loopRight, bottomY, false);
                    addSegment(loopRight, bottomY,
                               activationEdgeX(event.from, true), bottomY,
                               true);
                    y = bottomY + kRowGap * scale;
                    if (event.deactivateSource) {
                        popActivation(event.from, y - kRowGap * scale * 0.5f);
                    }
                } else {
                    bool towardRight = lifelineX(event.to) > lifelineX(event.from);
                    float lineY = y + textHeight +
                                  (textHeight > 0 ? kMessageTextGap * scale
                                                  : 4.0f * scale);
                    if (event.activateTarget) {
                        pushActivation(event.to, lineY);
                    }
                    float fromX = activationEdgeX(event.from, towardRight);
                    float toX = activationEdgeX(event.to, !towardRight);
                    if (!event.text.empty()) {
                        Prim text;
                        text.type = PrimType::Text;
                        text.text = event.text;
                        text.style = textStyle;
                        text.fill = Role::Text;
                        text.x1 = std::min(fromX, toX);
                        text.y1 = y;
                        text.x2 = std::max(fromX, toX);
                        text.y2 = y + textHeight;
                        prims.push_back(std::move(text));
                    }
                    Prim lineSegment;
                    lineSegment.type = PrimType::Line;
                    lineSegment.x1 = fromX;
                    lineSegment.y1 = lineY;
                    lineSegment.x2 = toX;
                    lineSegment.y2 = lineY;
                    lineSegment.stroke = Role::Muted;
                    lineSegment.strokeWidth = 1.4f * scale;
                    lineSegment.dashed = event.line == ArrowLine::Dashed;
                    lineSegment.arrow = event.head == ArrowHead::Filled;
                    lineSegment.openArrow = event.head == ArrowHead::Open ||
                                            event.head == ArrowHead::Cross;
                    prims.push_back(lineSegment);
                    if (event.head == ArrowHead::Cross) {
                        float size = 5.0f * scale;
                        float crossX = toX + (towardRight ? -1.0f : 1.0f) *
                                                 10.0f * scale;
                        for (int i = 0; i < 2; i++) {
                            Prim cross;
                            cross.type = PrimType::Line;
                            cross.x1 = crossX - size;
                            cross.y1 = lineY + (i == 0 ? -size : size);
                            cross.x2 = crossX + size;
                            cross.y2 = lineY + (i == 0 ? size : -size);
                            cross.stroke = Role::Muted;
                            cross.strokeWidth = 1.4f * scale;
                            prims.push_back(cross);
                        }
                    }
                    y = lineY + kRowGap * scale;
                    if (event.deactivateSource) {
                        popActivation(event.from, lineY);
                    }
                }
                break;
            }
        }
    }

    // Close any frames left open (missing `end`)
    while (!frameStack.empty()) {
        Frame frame = frameStack.back();
        frameStack.pop_back();
        if (frame.minColumn == SIZE_MAX) {
            frame.minColumn = 0;
            frame.maxColumn = count - 1;
        }
        frame.bottom = y;
        closedFrames.push_back(std::move(frame));
        y += 10.0f * scale;
    }
    while (!openActivations.empty()) {
        openActivations.back().bottom = y;
        closedActivations.push_back(openActivations.back());
        openActivations.pop_back();
    }

    float lifelineBottom = y + 4.0f * scale;

    // Lifelines behind everything: prepend by inserting at the start
    {
        std::vector<Prim> lifelines;
        for (const auto& participant : participants) {
            Prim lifeline;
            lifeline.type = PrimType::Line;
            lifeline.x1 = participant.center;
            lifeline.y1 = headerBottom;
            lifeline.x2 = participant.center;
            lifeline.y2 = lifelineBottom;
            lifeline.stroke = Role::Muted;
            lifeline.strokeWidth = 1.0f * scale;
            lifeline.dashed = true;
            lifelines.push_back(lifeline);
        }
        // Activation bars sit on top of lifelines but under messages
        for (const auto& span : closedActivations) {
            Prim bar;
            bar.type = PrimType::Rect;
            float centerShift = (span.level - 1) * kActivationNudge * scale;
            float half = kActivationWidth * 0.5f * scale;
            bar.x1 = lifelineX(span.participant) + centerShift - half;
            bar.x2 = lifelineX(span.participant) + centerShift + half;
            bar.y1 = span.top;
            bar.y2 = std::max(span.bottom, span.top + 8.0f * scale);
            bar.fill = Role::AccentSoft;
            bar.stroke = Role::Stroke;
            bar.strokeWidth = 1.0f * scale;
            lifelines.push_back(bar);
        }
        prims.insert(prims.begin(), lifelines.begin(), lifelines.end());
    }

    // Frames drawn on top (outlines + labels)
    for (const auto& frame : closedFrames) {
        float bottom = frame.bottom;
        // Outer frames get the larger inset so nested frames sit inside them
        float inset = kFramePad * scale +
                      (maxDepth - 1 - frame.depth) * kFramePad * 0.6f * scale;
        float left = participants[frame.minColumn].center -
                     std::max(participants[frame.minColumn].headerSize.w * 0.5f,
                              frame.extraLeft) -
                     inset;
        float right = participants[frame.maxColumn].center +
                      std::max(
                          participants[frame.maxColumn].headerSize.w * 0.5f,
                          frame.extraRight) +
                      inset;
        Prim box;
        box.type = PrimType::Rect;
        box.x1 = left;
        box.y1 = frame.top;
        box.x2 = right;
        box.y2 = bottom;
        box.fill = Role::None;
        box.stroke = Role::Muted;
        box.strokeWidth = 1.2f * scale;
        prims.push_back(box);

        // Keyword chip
        float chipWidth = frame.keywordSize.w + kFrameLabelPadX * 2 * scale;
        float chipHeight = frame.labelBottom - frame.top;
        Prim chip;
        chip.type = PrimType::Rect;
        chip.x1 = left;
        chip.y1 = frame.top;
        chip.x2 = left + chipWidth;
        chip.y2 = frame.top + chipHeight;
        chip.fill = Role::AccentSoft;
        chip.stroke = Role::Muted;
        chip.strokeWidth = 1.0f * scale;
        prims.push_back(chip);
        Prim keyword;
        keyword.type = PrimType::Text;
        keyword.text = frame.keyword;
        keyword.style = frameKeywordStyle;
        keyword.fill = Role::Text;
        keyword.x1 = chip.x1;
        keyword.y1 = chip.y1;
        keyword.x2 = chip.x2;
        keyword.y2 = chip.y2;
        prims.push_back(std::move(keyword));
        if (!frame.label.empty()) {
            Prim label;
            label.type = PrimType::Text;
            label.text = "[" + frame.label + "]";
            label.style = frameLabelStyle;
            label.fill = Role::Muted;
            label.alignH = -1;
            label.x1 = chip.x2 + 8.0f * scale;
            label.y1 = chip.y1;
            label.x2 = right - 4.0f * scale;
            label.y2 = chip.y2;
            prims.push_back(std::move(label));
        }
        for (const auto& divider : frame.dividers) {
            Prim dividerLine;
            dividerLine.type = PrimType::Line;
            dividerLine.x1 = left;
            dividerLine.y1 = divider.first;
            dividerLine.x2 = right;
            dividerLine.y2 = divider.first;
            dividerLine.stroke = Role::Muted;
            dividerLine.strokeWidth = 1.0f * scale;
            dividerLine.dashed = true;
            prims.push_back(dividerLine);
            Prim dividerLabel;
            dividerLabel.type = PrimType::Text;
            dividerLabel.text = divider.second;
            dividerLabel.style = frameLabelStyle;
            dividerLabel.fill = Role::Muted;
            dividerLabel.alignH = -1;
            dividerLabel.x1 = left + kFrameLabelPadX * scale;
            dividerLabel.y1 = divider.first + 2.0f * scale;
            dividerLabel.x2 = right;
            dividerLabel.y2 = divider.first + 2.0f * scale +
                              measure(divider.second, frameLabelStyle, 0.0f).h;
            prims.push_back(std::move(dividerLabel));
        }
    }

    // Bottom header row mirrors the top one
    emitHeaders(lifelineBottom);

    result.width = diagramWidth;
    result.height = lifelineBottom + headerHeight + kEdgePad * scale;
    result.ok = true;
    return result;
}

}  // namespace detail
}  // namespace mermaidext
