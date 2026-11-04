#pragma once
// Tinta's CSS (docs/specs/theming.md): the part of CSS a theme needs to style
// a Markdown page that Tinta lays out itself. CSS Syntax 3 tokens and rules,
// Selectors 4 over the elements Markdown becomes, the cascade with
// !important, custom properties with var(), calc(), the colour functions,
// @import, @media and @font-face; and computed values for the properties
// Tinta draws. Plain data and text only.
#include <bitset>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace css {

// --- Computed values ---------------------------------------------------------

// sRGB, each channel 0..1, alpha not premultiplied
struct Color {
    float r = 0, g = 0, b = 0, a = 0;
};
bool operator==(const Color& x, const Color& y);
inline bool operator!=(const Color& x, const Color& y) { return !(x == y); }

enum class FontStyle : uint8_t { Normal, Italic, Oblique };
enum class TextAlign : uint8_t { Start, Left, Right, Center, Justify };
enum class TextTransform : uint8_t { None, Uppercase, Lowercase, Capitalize };
enum class LineStyle : uint8_t { None, Solid, Dashed, Dotted, Double, Wavy };
enum class ColorScheme : uint8_t { Normal, Light, Dark, LightDark };
enum class ListStyle : uint8_t {
    Disc, Circle, Square, Decimal, DecimalLeadingZero, LowerAlpha, UpperAlpha,
    LowerRoman, UpperRoman, LowerGreek, CjkDecimal, None, String,
};

// A length that may still depend on the box it is in
struct Length {
    enum Kind : uint8_t { Px, Percent, Auto, None } kind = Px;
    float value = 0;
    float resolve(float containing) const;  // Px as is, Percent of containing, else 0
};

struct Border {
    float width = 0;  // px
    LineStyle style = LineStyle::None;
    Color color;
};

struct GradientStop {
    Color color;
    float position = -1;  // 0..1, or -1 to spread evenly
};
// linear-gradient() only; the angle in degrees, 180 = to bottom
struct Gradient {
    float angle = 180;
    std::vector<GradientStop> stops;
};

struct Shadow {
    float x = 0, y = 0, blur = 0, spread = 0;
    Color color;
    bool inset = false;
};

// An OpenType feature ('liga', 'smcp', 'tnum') or variation axis ('wght')
struct FontFeature {
    uint32_t tag = 0;  // four bytes, first character in the lowest byte as DWRITE_MAKE_OPENTYPE_TAG
    float value = 1;
};

// One piece of a content: value
struct ContentPart {
    enum Kind : uint8_t { Text, Counter, Counters, Attr } kind = Text;
    std::string text;       // the string, the counter's name or the attribute's
    std::string separator;  // counters(name, ".")
    ListStyle style = ListStyle::Decimal;
};

struct Decoration {
    bool underline = false, overline = false, lineThrough = false;
    std::optional<Color> color;  // empty: currentColor
    LineStyle style = LineStyle::Solid;
    float thickness = -1;  // px; -1 auto
    float offset = -1;     // text-underline-offset, px; -1 auto; inherited, as CSS has it
};

// Every property Tinta reads. `ComputedStyle::declared` records which of them
// a rule set on the element itself
enum class Prop : uint8_t {
    Color, BackgroundColor, BackgroundImage,
    FontFamily, FontSize, FontWeight, FontStyle, FontStretch, FontFeatureSettings,
    FontVariationSettings, FontVariantCaps, FontVariantNumeric, FontVariantLigatures,
    LineHeight, LetterSpacing, TextAlign, TextIndent, TextTransform,
    TextDecorationLine, TextDecorationColor, TextDecorationStyle, TextDecorationThickness,
    TextUnderlineOffset,
    BorderTopWidth, BorderTopStyle, BorderTopColor,
    BorderRightWidth, BorderRightStyle, BorderRightColor,
    BorderBottomWidth, BorderBottomStyle, BorderBottomColor,
    BorderLeftWidth, BorderLeftStyle, BorderLeftColor,
    BorderTopLeftRadius, BorderTopRightRadius, BorderBottomRightRadius, BorderBottomLeftRadius,
    MarginTop, MarginRight, MarginBottom, MarginLeft,
    PaddingTop, PaddingRight, PaddingBottom, PaddingLeft,
    MaxWidth, ListStyleType, Content, CounterReset, CounterIncrement,
    Opacity, BoxShadow, AccentColor, CaretColor, ColorScheme,
    Count
};

struct ComputedStyle {
    std::bitset<(size_t)Prop::Count> declared;
    bool has(Prop prop) const { return declared.test((size_t)prop); }

    // Inherited
    Color color{0, 0, 0, 1};
    std::vector<std::string> fontFamily;  // UTF-8, first choice first; generic names as written
    float fontSize = 16;                  // px
    int fontWeight = 400;
    FontStyle fontStyle = FontStyle::Normal;
    float fontStretch = 100;              // percent
    std::vector<FontFeature> fontFeatures;    // font-feature-settings and font-variant-*
    std::vector<FontFeature> fontVariations;  // font-variation-settings
    float lineHeight = 0;                 // px; 0 is normal
    float lineHeightFactor = 0;           // a unitless line-height, which children inherit as the factor; else 0
    float letterSpacing = 0;              // px
    TextAlign textAlign = TextAlign::Start;
    float textIndent = 0;                 // px
    TextTransform textTransform = TextTransform::None;
    ListStyle listStyle = ListStyle::Disc;
    std::string listString;               // list-style-type: "- "
    std::optional<Color> accentColor;     // empty: auto
    std::optional<Color> caretColor;      // empty: auto
    ColorScheme colorScheme = ColorScheme::Normal;
    std::string lang;                     // from lang attributes, for :lang()

    // Not inherited
    Color backgroundColor;                // transparent
    std::optional<Gradient> backgroundImage;
    Border border[4];                     // top, right, bottom, left
    float radius[4] = {};                 // top-left, top-right, bottom-right, bottom-left; px
    float margin[4] = {};                 // top, right, bottom, left; px
    float padding[4] = {};
    Length maxWidth{Length::None, 0};
    Decoration decoration;
    std::vector<Shadow> boxShadow;
    float opacity = 1;
    std::vector<ContentPart> content;     // ::before, ::after, ::marker; empty is none
    std::vector<std::pair<std::string, int>> counterReset, counterIncrement;

    // A custom property (--name), inherited, var() substituted, as CSS text;
    // empty when unset
    std::string custom(std::string_view name) const;
    // A custom property read as a colour (`--ui-accent: #3366cc`)
    std::optional<Color> customColor(std::string_view name) const;

    // Shared, copy-on-write, per context; sorted by name
    std::shared_ptr<const std::vector<std::pair<std::string, std::string>>> customs;
    bool dark = false;  // the media it was computed for, which light-dark() in customColor() follows
};

// --- Sheets --------------------------------------------------------------------

struct Diagnostic {
    enum Level : uint8_t { Error, Warning, Note } level = Warning;
    std::string file;
    int line = 0, column = 0;  // 1-based
    std::string message;       // "`display` has no effect: Tinta lays out blocks itself"
};

// Finds the text of an @import: a bundled theme by name ("Paper") or a file
// relative to the importing one. Fills `file` with the name diagnostics and
// nested imports use. False when there is no such sheet
using ImportSource = std::function<bool(std::string_view name, std::string_view fromFile,
                                        std::string& text, std::string& file)>;

enum class Origin : uint8_t { Base, Theme, Folder };

struct FontFace {
    std::string family;
    std::vector<std::string> sources;  // files resolved against the sheet; local() names as "local:Name"
    int weightMin = 400, weightMax = 400;
    FontStyle style = FontStyle::Normal;
    std::string file;  // where it was declared
    int line = 0;
};

struct Sheet;

// Parses a sheet and the sheets it imports. Never fails: what it cannot use
// goes to `diagnostics`, and the rest applies
std::shared_ptr<const Sheet> parse(std::string_view text, std::string_view file, Origin origin,
                                   const ImportSource& imports,
                                   std::vector<Diagnostic>& diagnostics);
const std::vector<FontFace>& fontFaces(const Sheet& sheet);
// The sheet again as CSS with its imports inlined, for HTML export. Rules and
// declarations Tinta ignores are kept, since a browser may use them
std::string serialize(const Sheet& sheet);

// --- Styling elements ----------------------------------------------------------

struct Media {
    bool dark = false;    // prefers-color-scheme, and light-dark()
    bool print = false;   // print vs screen
    float width = 800;    // px, for min-width/max-width
};

// One element of the page, as the HTML export writes it
struct Element {
    std::string_view tag;  // lowercase: "p", "h1", "li", "code"...
    std::string_view id;
    std::vector<std::string_view> classes;
    std::vector<std::pair<std::string_view, std::string_view>> attributes;  // href, lang, data-callout...
    int index = 0, count = 0;          // 1-based place among element siblings, and their number
    int typeIndex = 0, typeCount = 0;  // the same among siblings with this tag
    bool checked = false;              // a done task's checkbox, or its list item
    bool empty = false;                // no children and no text, for :empty
};

enum class Pseudo : uint8_t { Before, After, Marker, Selection };

// A declaration that reaches an element, as the inspector lists it
// (Engine::explain)
struct Applied {
    enum State : uint8_t {
        Winning,     // it sets the property
        Overridden,  // a declaration listed before it, for the same property, wins
        Inherited,   // it sets the property on an ancestor, which hands it down
    } state = Winning;
    std::string property;     // the longhand or custom property: "border-left-color", "--ink"
    std::string value;        // what it gives that property, as CSS: "#b35900", "var(--ink)"
    std::string declaration;  // as written: "border-left: 3px solid #b35900 !important"
    std::string selector;     // the selector of its rule that matched, as written: "blockquote > p"
    uint32_t specificity = 0; // ids << 20 | classes << 10 | types
    Origin origin = Origin::Theme;
    bool important = false;
    int ancestor = 0;         // where it is declared: 0 the element explained, 1 its parent...
    int8_t pseudo = -1;       // a Pseudo when its rule styles that pseudo-element, else -1
    std::string file;
    int line = 0, column = 0; // 1-based, of the declaration
};

// An element's path from the page down, for the inspector: "body >
// blockquote.alert-note > p > strong". A leading html is left out
std::string selectorPath(const std::vector<Element>& path);

class Engine {
public:
    using Context = uint32_t;

    Engine();
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // The sheets in cascade order, base first. Clears every context
    void load(std::vector<std::shared_ptr<const Sheet>> sheets, const Media& media);
    void setMedia(const Media& media);  // light/dark or print: recomputes lazily
    const Media& media() const;

    Context root();  // the html element, :root
    // An element under `parent`. `previous` are its earlier element siblings
    // in order, for + and ~; passing the same array as it grows reads each
    // sibling once. The same parent and an element no selector can tell
    // apart from an earlier one give the same context: a document has a
    // few hundred, not one per element
    Context child(Context parent, const Element& element, const Element* previous = nullptr,
                  size_t previousCount = 0);
    const ComputedStyle& style(Context context);
    // Null when no rule styles that pseudo-element of this context, or (for
    // before/after) gives it no content
    const ComputedStyle* pseudo(Context context, Pseudo which);

    // Which rules styled a context, for the inspector: for each property its
    // rules declare, the winning declaration and then those it beat, in
    // cascade order; then, for inherited properties it does not declare, the
    // declaration on the nearest ancestor that does, and the custom
    // properties its declarations' var()s read. Works the cascade out again,
    // so it is slow; style() does not pay for it
    std::vector<Applied> explain(Context context);
    std::vector<Applied> explain(Context context, Pseudo which);

    // Changes on every load() and setMedia(); caches keyed on styles compare it
    uint64_t generation() const;
    // Problems found while computing (an invalid var() substitution...)
    const std::vector<Diagnostic>& diagnostics() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

// Colours outside a sheet: theme editors and themes.ini values
std::optional<Color> parseColor(std::string_view text, bool dark = false);
std::string formatColor(const Color& color);  // #rrggbb, or #rrggbbaa when translucent
}
