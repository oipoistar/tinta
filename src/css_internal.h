#pragma once
// Inside Tinta's CSS engine (css.h): tokens, component values, selectors,
// rules and the value code its parts share. Plain data and text only.
#include "css.h"

#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace css {

// --- Tokens and component values (CSS Syntax 3) --------------------------------

enum class Tok : uint8_t {
    Ident, Function, AtKeyword, Hash, String, BadString, Url, BadUrl, Delim, Number,
    Percentage, Dimension, Space, CDO, CDC, Colon, Semicolon, Comma,
    OpenSquare, CloseSquare, OpenParen, CloseParen, OpenCurly, CloseCurly,
};

struct Token {
    Tok type = Tok::Space;
    bool idHash = false;   // a hash that would also be an identifier
    bool integer = false;  // a number without fraction or exponent
    char delim = 0;        // a Delim's character; 0 when it is not ASCII
    double number = 0;
    std::string text;      // unescaped: ident, function or at-rule name, string, url, hash or unit
    uint32_t begin = 0, end = 0;  // source bytes; a function's or block's end is past its close
};

std::vector<Token> tokenize(std::string_view text);

// A component value: a token, or a function or block with its contents
struct Node {
    Token token;
    std::vector<Node> children;
    bool is(Tok type) const { return token.type == type; }
    bool ident(const char* lower) const;
    bool function(const char* lower) const;
    bool delim(char c) const { return token.type == Tok::Delim && token.delim == c; }
};

// The component values of tokens [at, end), taking the tokens' text; `at`
// ends past them
void readNodes(std::vector<Token>& tokens, size_t& at, size_t end, std::vector<Node>& out);
Node readNode(std::vector<Token>& tokens, size_t& at, size_t end);
size_t skipNode(const std::vector<Token>& tokens, size_t at, size_t end);
std::vector<Node> parseNodes(std::string_view text);
// Back to CSS text, with /**/ where two tokens would run together
void serializeNodes(const std::vector<Node>& nodes, std::string& out);
bool hasVar(const std::vector<Node>& nodes);
void trimSpace(std::vector<Node>& nodes);
// The values of a list split at its top-level commas, each trimmed
std::vector<std::vector<Node>> splitCommas(const std::vector<Node>& nodes);
// Every non-space node, for values of space-separated parts
std::vector<const Node*> parts(const std::vector<Node>& nodes);
// The non-space nodes of each comma-separated value
std::vector<std::vector<const Node*>> splitParts(const std::vector<Node>& nodes);

bool equalsLower(std::string_view text, const char* lower);
std::string toLower(std::string_view text);
bool startsWithLower(std::string_view text, const char* lower);

// Offsets to 1-based lines and columns (columns count characters)
struct Lines {
    std::vector<uint32_t> starts;
    explicit Lines(std::string_view text);
    void at(std::string_view text, uint32_t offset, int& line, int& column) const;
};

// --- Properties ------------------------------------------------------------------

// Property ids: the longhands of Prop, then the shorthands, then custom
enum PropId : uint16_t {
    ShFont = (uint16_t)Prop::Count, ShBackground, ShBorder, ShBorderTop, ShBorderRight,
    ShBorderBottom, ShBorderLeft, ShBorderWidth, ShBorderStyle, ShBorderColor, ShBorderRadius,
    ShMargin, ShPadding, ShTextDecoration, ShFontVariant, ShListStyle, PropCustom,
};

const char* propertyName(int id);
int propertyId(std::string_view lowerName);  // -1 when Tinta does not compute it
bool inherited(Prop prop);
// The longhands a shorthand sets
const std::vector<Prop>& longhands(int shorthand);
// Why Tinta leaves a property it knows alone, or null
const char* ignoredReason(std::string_view lowerName);
// The known property nearest to a misspelt one (at most two edits), or empty
std::string suggestProperty(std::string_view lowerName);
bool cssWideKeyword(const Node& node);
bool cssWideKeyword(const std::vector<Node>& value);

// --- Selectors ---------------------------------------------------------------------

enum class Combinator : uint8_t { Descendant, Child, Next, Subsequent };

struct AttrTest {
    std::string name, value;  // the name lowercase
    char op = 0;              // 0 present, '=', '~', '|', '^', '$', '*'
    bool caseless = false;
};

struct Complex;
struct PseudoClass {
    enum Kind : uint8_t { Root, Empty, Checked, Nth, Lang, Is, Not } kind = Root;
    bool ofType = false, fromEnd = false;
    int a = 0, b = 0;                 // An+B
    std::vector<std::string> langs;   // lowercase ranges
    std::vector<Complex> list;        // :is(), :where(), :not()
};

struct Compound {
    std::string tag, id;  // tag lowercase; empty for any
    std::vector<std::string> classes;
    std::vector<AttrTest> attrs;
    std::vector<PseudoClass> pseudos;
};

struct Complex {
    std::vector<Compound> compounds;      // the subject first, then leftwards
    std::vector<Combinator> combinators;  // [k] joins compounds[k] to compounds[k + 1]
    uint32_t specificity = 0;             // ids << 20 | classes << 10 | types
    uint32_t begin = 0, end = 0;          // where a rule's selector is written in its sheet, for explain()
    int8_t pseudo = -1;                   // a Pseudo, or -1 for the element itself
};

// --- Rules and sheets ----------------------------------------------------------------

struct Declaration {
    uint16_t prop = 0;        // a PropId
    bool important = false;
    bool pending = false;     // holds var(): checked when computed
    uint32_t begin = 0;       // where its name is written in its sheet, for explain()
    std::string name;         // a custom property's --name
    std::string text;         // a custom property's value as written
    std::vector<Node> value;  // trimmed, without !important
    int line = 0, column = 0;
};

struct MediaExpr {
    enum Kind : uint8_t { All, Screen, Print, Never, Dark, Light, MinWidth, MaxWidth, Not, And, Or } kind = All;
    float px = 0;
    std::vector<MediaExpr> args;
};
bool matchMedia(const MediaExpr& expr, const Media& media);

struct Rule {
    std::vector<Complex> selectors;
    std::vector<Declaration> declarations;
    std::vector<std::shared_ptr<const MediaExpr>> media;  // all must match
    bool supported = true;                                 // false inside a failed @supports
    std::shared_ptr<const std::string> file;
};

struct Sheet {
    std::string file, text;
    std::shared_ptr<const std::string> name;  // what its rules' `file` points to, so explain() finds their text
    Origin origin = Origin::Theme;
    std::vector<Rule> rules;  // the imports' rules inlined where they were imported
    std::vector<FontFace> faces;
    struct Import {
        uint32_t begin = 0, end = 0;  // the @import's text
        std::shared_ptr<const Sheet> sheet;  // null drops the text
        std::string media, supports;         // the conditions it was imported under, as written
    };
    std::vector<Import> imports;  // and @charset of imported sheets, to drop
};

// --- Values ----------------------------------------------------------------------------

// Numbers, lengths, percentages and angles, calc() resolved
struct NumEnv {
    float em = 16, rem = 16;
    float percent = NAN;  // what 100% is; NaN keeps percentages as they are
};
struct Dim {
    double value = 0;
    char kind = 'n';  // 'n' number, 'l' px, '%' percent, 'a' degrees
};
bool readDim(const Node& node, const NumEnv& env, Dim& out);
bool readLength(const Node& node, const NumEnv& env, float& px, bool negative = true);
bool readNumber(const Node& node, double& out);
bool readAngle(const Node& node, double& degrees);

struct ColorEnv {
    bool dark = false;
    Color current{0, 0, 0, 1};
};
bool readColor(const Node& node, const ColorEnv& env, Color& out);

// ComputedStyle and the longhands behind its font features, which inherit
struct Computed {
    ComputedStyle s;
    uint8_t caps = 0;
    uint16_t numeric = 0, ligatures = 0;
    std::vector<FontFeature> settings;
};

struct Env {
    const Computed* parent = nullptr;  // null at the root
    float rootFontSize = 16;
    float width = 800;
    bool dark = false;
    std::vector<std::string>* warnings = nullptr;  // while parsing: parts accepted but not drawn
};

// A longhand's value, without var() or a CSS-wide keyword; false when invalid
bool computeLonghand(Prop prop, const std::vector<Node>& value, const Env& env, Computed& out);
// A shorthand's longhands, `initial` for the parts it leaves out; false when invalid
bool expandShorthand(int id, const std::vector<Node>& value, const Env& env,
                     std::vector<std::pair<Prop, std::vector<Node>>>& out);
void copyLonghand(Prop prop, const Computed& from, Computed& to);
// fontFeatures from font-variant-* and font-feature-settings
void buildFeatures(Computed& out);
const Computed& initialStyle();

}
