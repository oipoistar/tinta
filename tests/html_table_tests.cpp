#include "d2d_init.h"
#include "document.h"
#include "editor.h"
#include "export.h"
#include "input.h"
#include "keystroke.h"
#include "live_editor.h"
#include "print.h"
#include "render.h"
#include "search.h"
#include "selection.h"
#include "settings.h"
#include "tabs.h"
#include "ui_frame.h"
#include "utils.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

// Tables written in HTML (#325). A <table> in a Markdown file was no table
// at all: its tags were dropped, the cells' text ran together, the lines of
// the block were drawn over each other and the text after it over them. It
// is now read into the same table a pipe table makes, with the end tags
// HTML lets a table leave out, and with cells over several columns or rows.
namespace {
int failures = 0, checks = 0;
void check(bool ok, const std::string& message) {
    ++checks;
    if (!ok) {
        if (failures < 60) std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}
namespace fs = std::filesystem;
using uiframe::Frame;

fs::path g_fixtures, g_work, g_frames;
std::string g_issue, g_mixed;

std::string readAll(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}
// The fixtures read the same whichever line ends the checkout gave them
std::string unixLines(std::string text) {
    text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
    return text;
}
size_t count(const std::string& in, const std::string& what) {
    size_t n = 0;
    for (size_t at = in.find(what); at != std::string::npos; at = in.find(what, at + what.size())) n++;
    return n;
}

struct Window {
    std::unique_ptr<App> state = std::make_unique<App>();
    App& app = *state;
    bool ok = false;
    Window() {
        app.hwnd = CreateWindowExW(0, L"STATIC", L"HTML table tests", WS_POPUP, 0, 0, 1300, 900, nullptr, nullptr,
                                   nullptr, nullptr);
        if (!app.hwnd || !initD2D(app) || !createRenderTarget(app)) return;
        app.width = 1300;
        app.height = 900;
        updateTextFormats(app);
        Settings keys;
        keys.keyProfile = "windows";
        applyKeymap(app, keys);
        applyTheme(app, 0);
        ok = true;
    }
    ~Window() {
        if (app.hwnd) DestroyWindow(app.hwnd);
        app.hwnd = nullptr;
    }
};

// --- The tree -------------------------------------------------------------------

// The tree in short: 'text' for a leaf; a cell as td or th with what it
// covers (*c3 for three columns, *r2 for two rows) and how it is aligned
// (@2 centred, @3 right); a header row as tr!
std::string shape(const ElementPtr& e) {
    if (e->type == ElementType::Text) return "'" + e->text + "'";
    std::string s;
    switch (e->type) {
        case ElementType::Table: s = e->language == "html" ? "table" : "pipes"; break;
        case ElementType::TableRow: s = e->header ? "tr!" : "tr"; break;
        case ElementType::TableCell:
            s = e->header ? "th" : "td";
            if (e->colSpan != 1) s += "*c" + std::to_string(e->colSpan);
            if (e->rowSpan != 1) s += "*r" + std::to_string(e->rowSpan);
            if (e->align) s += "@" + std::to_string(e->align);
            break;
        case ElementType::Caption: s = "caption"; break;
        case ElementType::HardBreak: s = "br"; break;
        default: s = qmd::elementTypeToString(e->type); break;
    }
    if (e->children.empty()) return s;
    s += "[";
    for (size_t i = 0; i < e->children.size(); i++) {
        if (i) s += " ";
        s += shape(e->children[i]);
        check(e->children[i]->parent == e.get(), "a child knows its parent");
    }
    return s + "]";
}
std::string parsed(App& app, const std::string& source) {
    auto result = app.parser.parse(source);
    check(result.success, "parses: " + source);
    if (!result.success || !result.root) return "";
    std::string s;
    for (size_t i = 0; i < result.root->children.size(); i++) {
        if (i) s += " ";
        s += shape(result.root->children[i]);
    }
    return s;
}
void expect(App& app, const std::string& source, const std::string& want) {
    const std::string got = parsed(app, source);
    check(got == want, source + "\n  want " + want + "\n  got  " + got);
}
// The first table under an element
const Element* firstTable(const Element* e) {
    if (e->type == ElementType::Table) return e;
    for (const auto& child : e->children)
        if (const Element* found = firstTable(child.get())) return found;
    return nullptr;
}
std::string textOf(const Element* e) {
    if (e->type == ElementType::Text) return e->text;
    std::string out;
    for (const auto& child : e->children) out += (child->type == ElementType::HardBreak ? " " : "") + textOf(child.get());
    return out;
}

void theTree(App& app) {
    const std::string bullet = "\xE2\x80\xA2 ";
    // A plain table: rows, header cells, cells
    expect(app, "<table><tr><th>Action</th><th>Key</th></tr><tr><td>Open</td><td>Ctrl + O</td></tr></table>\n",
           "HtmlBlock[table[tr[th['Action'] th['Key']] tr[td['Open'] td['Ctrl + O']]]]");
    // Lines and indentation between the tags are nothing
    expect(app, "<table>\n  <tr>\n    <td>a</td>\n    <td>b</td>\n  </tr>\n</table>\n",
           "HtmlBlock[table[tr[td['a'] td['b']]]]");
    expect(app, "<TABLE>\n<TR><TD>a</TD><TH>b</TH></TR>\n</TABLE>\n", "HtmlBlock[table[tr[td['a'] th['b']]]]");
    // A new row ends the open row, a new cell the open cell, </table> all
    // of it; what follows the table is outside it
    expect(app, "<table>\n<tr><td>a<td>b\n<tr><td>c\n</table>\nafter\n",
           "HtmlBlock[table[tr[td['a'] td['b']] tr[td['c']]] 'after']");
    expect(app, "<table><tr><th>Name<th>Since<tr><td>Open<td>1.0\n\nafter\n",
           "HtmlBlock[table[tr[th['Name'] th['Since']] tr[td['Open'] td['1.0']]]] Paragraph['after']");
    expect(app, "<table><td>a</td><td>b</td></table>\n", "HtmlBlock[table[tr[td['a'] td['b']]]]");
    expect(app, "<table><tr><td>a</tr><tr><td>b</table>\n", "HtmlBlock[table[tr[td['a']] tr[td['b']]]]");
    // An end tag with nothing of its own open ends nothing
    expect(app, "<table><tr><td>a</th></caption>b</td></tr></table>\n", "HtmlBlock[table[tr[td['ab']]]]");
    expect(app, "</table></tr></td>\n<p>x</p>\n", "HtmlBlock[Paragraph['x']]");
    // The columns and rows a cell covers, in quotes or bare
    expect(app,
           "<table><tr><td rowspan=\"2\" colspan=3>a</td><TD ROWSPAN='0'>b</TD><td colspan=\"0\">c</td>"
           "<td rowspan=\"x\" colspan=\"\">d</td><td colspan = \"2\" >e</td></tr></table>\n",
           "HtmlBlock[table[tr[td*c3*r2['a'] td*r0['b'] td['c'] td['d'] td*c2['e']]]]");
    expect(app, "<table><tr><th align=\"center\">a</th><td style=\"text-align: right\">b</td><td align=left>c</td>"
                "<td valign=\"top\">d</td></tr></table>\n",
           "HtmlBlock[table[tr[th@2['a'] td@3['b'] td@1['c'] td['d']]]]");
    // The head's rows are marked; the foot's go last wherever they were written
    expect(app,
           "<table><tfoot><tr><td>f</td></tr></tfoot><thead><tr><td>h</td></tr></thead>"
           "<tbody><tr><td>b</td></tr></tbody></table>\n",
           "HtmlBlock[table[tr![td['h']] tr[td['b']] tr[td['f']]]]");
    expect(app, "<table><caption>Sales <em>now</em></caption><tr><td>a</td></tr></table>\n",
           "HtmlBlock[table[caption['Sales ' Emphasis['now']] tr[td['a']]]]");
    // Inline content in a cell
    expect(app, "<table><tr><td>one<br>two <b>bold</b> <code>x</code> <a href=\"u\">link</a></td></tr></table>\n",
           "HtmlBlock[table[tr[td['one' br 'two ' Strong['bold'] ' ' Code['x'] ' ' Link['link']]]]]");
    expect(app, "<table><tr><td>Selected Text <br>in typing area</td></tr></table>\n",
           "HtmlBlock[table[tr[td['Selected Text' br 'in typing area']]]]");
    expect(app, "<table><tr><td>H<sub>2</sub>O <i>it</i></td><td><b>open\n<td>next</table>\n",
           "HtmlBlock[table[tr[td['H' Subscript['2'] 'O ' Emphasis['it']] td[Strong['open']] td['next']]]]");
    expect(app, "<table><tr><td>5 &lt; 7 &amp;&amp; a&nbsp;b &#8364; &#x2713; &quot;q&quot; &apos; &copy; & x</td></tr></table>\n",
           "HtmlBlock[table[tr[td['5 < 7 && a\xC2\xA0" "b \xE2\x82\xAC \xE2\x9C\x93 \"q\" ' &copy; & x']]]]");
    // Blocks inside a cell are lines of it; a list's items are marked
    expect(app, "<table><tr><td><p>a</p><p>b</p><ul><li>one</li><li>two</li></ul><ol><li>x</li></ol></td></tr></table>\n",
           "HtmlBlock[table[tr[td['a' br 'b' br '" + bullet + "' 'one' br '" + bullet + "' 'two' br '1. ' 'x']]]]");
    expect(app, "<table><tr><td>a<div>b</div>c<h3>d</h3>e<pre>f  g</pre></td></tr></table>\n",
           "HtmlBlock[table[tr[td['a' br 'b' br 'c' br Strong['d'] br 'e' br Code['f g']]]]]");
    // A table inside a cell is read as lines of that cell
    expect(app,
           "<table><tr><td>outer</td><td><table><tr><td>i1</td><td>i2</td></tr><tr><td>i3</td></tr></table></td></tr>"
           "<tr><td>last</td></tr></table>\n",
           "HtmlBlock[table[tr[td['outer'] td['i1' ' i2' br 'i3']] tr[td['last']]]]");
    // Text between the cells goes above the table
    expect(app, "<table>before<tr>mid<td>a</td></tr>end</table>\n",
           "HtmlBlock['before' 'mid' 'end' table[tr[td['a']]]]");
    // A tag between the cells opens nothing there
    expect(app, "<table><b><tr><i><td>a</td></i></tr></b></table>\n", "HtmlBlock[table[tr[td['a']]]]");
    // The report: a bold line right above, no blank line between
    expect(app, "**Keyboard shortcuts**\n<table>  \n    <tr>\n      <td rowspan=\"6\">Move</td>\n      <td>Ctrl</td>\n    </tr>\n</table>\n",
           "Paragraph[Strong['Keyboard shortcuts']] HtmlBlock[table[tr[td*r6['Move'] td['Ctrl']]]]");
    // A table open at a blank line goes on in the HTML after it
    expect(app, "<table>\n<tr><td>a</td></tr>\n\n<tr><td>b</td></tr>\n\n</table>\n\nafter\n",
           "HtmlBlock[table[tr[td['a']] tr[td['b']]]] Paragraph['after']");
    expect(app, "<table>\n<tr><td>a</td></tr>\n\n<tr><td>b</td></tr>\n</table>\n<table><tr><td>c</td></tr></table>\n",
           "HtmlBlock[table[tr[td['a']] tr[td['b']]] table[tr[td['c']]]]");
    // Markdown in between ends it: an empty table is not drawn, and rows
    // with no table around them make their own
    expect(app, "<table>\n<tr>\n<td>\n\n**md**\n\n</td>\n</tr>\n</table>\n", "HtmlBlock Paragraph[Strong['md']] HtmlBlock");
    expect(app, "<table>\n<tr><td>a</td></tr>\n\ntext\n\n<tr><td>b</td></tr>\n</table>\n",
           "HtmlBlock[table[tr[td['a']]]] Paragraph['text'] HtmlBlock[table[tr[td['b']]]]");
    expect(app, "<table></table>\n", "HtmlBlock");
    // A table named in a comment is no table left open
    expect(app, "<!-- a note: use a <table> here -->\n\n<div>after</div>\n", "HtmlBlock HtmlBlock['after']");
    expect(app, "<table><tr><td></td><td> </td></tr></table>\n", "HtmlBlock");
    expect(app, "<tr><td>a</td><td>b</td></tr>\n", "HtmlBlock[table[tr[td['a'] td['b']]]]");
    // A table ends the paragraph open before it
    expect(app, "<p>text<table><tr><td>a</td></tr></table>\n", "HtmlBlock[Paragraph['text'] table[tr[td['a']]]]");
    expect(app, "<table><tr><td>a</td></tr><table><tr><td>b</td></tr></table>\n",
           "HtmlBlock[table[tr[td['a']]] table[tr[td['b']]]]");
    // In a list item and in a quote
    expect(app, "- item\n\n  <table>\n  <tr><td>a</td></tr>\n  </table>\n- next\n",
           "List[ListItem[Paragraph['item'] HtmlBlock[table[tr[td['a']]]]] ListItem[Paragraph['next']]]");
    expect(app, "- item\n\n  <table>\n  <tr><td>a</td></tr>\n\n- next\n",
           "List[ListItem[Paragraph['item'] HtmlBlock[table[tr[td['a']]]]] ListItem[Paragraph['next']]]");
    expect(app, "> <table>\n> <tr><td>a</td></tr>\n> </table>\n", "BlockQuote[HtmlBlock[table[tr[td['a']]]]]");
    expect(app, "<ul><li>in<table><tr><td>a</td></tr></table>out</li></ul>\n",
           "HtmlBlock[List[ListItem['in' table[tr[td['a']]] 'out']]]");
    // A pipe table is the table it was
    expect(app, "| a | b |\n|---|:-:|\n| c | d |\n",
           "pipes[tr[td['a'] td@2['b']] tr[td['c'] td@2['d']]]");
    // Not a table: in a fence, and a tag that only starts like one
    expect(app, "```html\n<table><tr><td>a</td></tr></table>\n```\n", "CodeBlock['<table><tr><td>a</td></tr></table>\n']");
    expect(app, "<tablet>a</tablet>\n", "Paragraph['<tablet>a</tablet>']");

    // Other HTML blocks: a line break is a space, as in a browser. Kept,
    // the lines were drawn over each other and over the block after them
    expect(app, "<div>\n  line one\n  line two\n</div>\n\nafter\n", "HtmlBlock['line one line two'] Paragraph['after']");
    expect(app, "<p>\n  A paragraph\n  over  two\tlines.\n</p>\n", "HtmlBlock[Paragraph['A paragraph over two lines.']]");
    expect(app, "<p>a\n<b>b</b>\nc &amp; d</p>\n", "HtmlBlock[Paragraph['a ' Strong['b'] ' c & d']]");
    expect(app, "<pre>\na  b\n  c\n</pre>\n", "HtmlBlock[CodeBlock['a  b\n  c']]");
    // What a <div>, a <summary> and their like hold stands on lines of its
    // own; no empty line where a block or nothing follows
    expect(app, "<details>\n<summary>Click to expand</summary>\nHidden line one.\nLine two.\n</details>\n",
           "HtmlBlock['Click to expand' br 'Hidden line one. Line two.']");
    expect(app, "<div>First</div>\n<div>Second</div>\n\nafter\n", "HtmlBlock['First' br 'Second'] Paragraph['after']");
    expect(app, "<div align=\"center\">\n  <b>Project</b><br>\n  A tagline\n</div>\n",
           "HtmlBlock[Strong['Project'] br 'A tagline']");
    expect(app, "<div><p>a</p></div>\n<div>b</div>\n", "HtmlBlock[Paragraph['a'] 'b']");
    expect(app, "<h2><div>Title</div></h2>\n", "HtmlBlock[Heading['Title']]");
    expect(app, "<dl><dt>Term</dt><dd>Definition</dd></dl>\n", "HtmlBlock['Term' br 'Definition']");

    // The source's line ends make no difference
    const std::string issue = unixLines(g_issue);
    std::string dos;
    for (char c : issue) dos += c == '\n' ? std::string("\r\n") : std::string(1, c);
    check(parsed(app, issue) == parsed(app, dos) && parsed(app, issue).size() > 2000,
          "the report's table reads the same with CRLF line ends");

    // Deep in quotes, cells that cover far too much, and a table of
    // thousands of rows: read, and laid out by the checks further down
    std::string deep;
    for (int i = 0; i < 300; i++) deep += "<blockquote>";
    deep += "<table><tr><td>deep</td><td><table><tr><td>deeper</td></tr></table></td></tr></table>\n";
    check(parsed(app, deep).find("'deep'") != std::string::npos, "a table 300 quotes deep is read, as lines");
    auto wide = app.parser.parse("<table><tr><td colspan=\"100000\">a</td><td rowspan=\"100000\">b</td><td>c</td></tr>"
                                 "<tr><td colspan=\"500\">d</td></tr></table>\n");
    const Element* wideTable = wide.root ? firstTable(wide.root.get()) : nullptr;
    check(wideTable != nullptr, "a table with huge spans is read");
    if (wideTable) {
        const TableGrid grid = tableGrid(*wideTable);
        check(grid.colCount <= 128 && grid.rowCount == 2, "its grid is cut to 128 columns and its own two rows");
        check(grid.cells.size() >= 2 && grid.cells[0].colSpan == 128, "the cell that fills the grid keeps its row");
    }
}

// --- The grid -------------------------------------------------------------------

struct Placed {
    int row = -1, col = -1, rowSpan = 0, colSpan = 0;
    bool header = false;
};
Placed placeOf(const TableGrid& grid, const std::string& text) {
    for (const auto& cell : grid.cells) {
        if (textOf(cell.cell) != text) continue;
        return {cell.row, cell.col, cell.rowSpan, cell.colSpan, cell.header};
    }
    return {};
}
bool is(const Placed& p, int row, int col, int rowSpan = 1, int colSpan = 1) {
    return p.row == row && p.col == col && p.rowSpan == rowSpan && p.colSpan == colSpan;
}

void theGrid(App& app) {
    auto doc = app.parser.parse(g_issue);
    const Element* table = doc.root ? firstTable(doc.root.get()) : nullptr;
    check(table != nullptr, "the report's table is a table");
    if (table) {
        const TableGrid grid = tableGrid(*table);
        check(grid.rowCount == (int)count(g_issue, "<tr>") && grid.rowCount == 50, "a row for every <tr>, 50 of them");
        check(grid.colCount == 3 && grid.headerRows == 0, "three columns and no header row");
        check(grid.cells.size() == count(g_issue, "<td"), "a cell for every <td>");
        check(std::find(grid.slots.begin(), grid.slots.end(), -1) == grid.slots.end(),
              "every slot of the grid is under a cell: the table is whole, as its author wrote it");
        check(is(placeOf(grid, "Move among chats"), 0, 0, 6), "Move among chats covers the first six rows");
        check(is(placeOf(grid, "Next chat"), 0, 1, 3) && is(placeOf(grid, "Previous chat"), 3, 1, 3),
              "Next chat and Previous chat cover three rows each, in the second column");
        check(is(placeOf(grid, "Ctrl + Tab"), 0, 2) && is(placeOf(grid, "Alt + Arrow Up"), 1, 2) &&
                  is(placeOf(grid, "Ctrl + Page Up"), 2, 2) && is(placeOf(grid, "Ctrl + Shift + Tab"), 3, 2) &&
                  is(placeOf(grid, "Ctrl + Page Down"), 5, 2),
              "their keys are in the third column, a row each");
        check(is(placeOf(grid, "Search"), 6, 0, 2) && is(placeOf(grid, "Chat search"), 6, 1) &&
                  is(placeOf(grid, "Ctrl + F"), 7, 2),
              "Search covers two rows");
        check(is(placeOf(grid, "Selected Text in typing area"), 8, 0, 15) && is(placeOf(grid, "Undo"), 8, 1) &&
                  is(placeOf(grid, "Ctrl + Shift + N"), 22, 2),
              "Selected Text in typing area covers fifteen");
        check(is(placeOf(grid, "Folders"), 23, 0, 9) && is(placeOf(grid, "Other shortcuts"), 32, 0, 9),
              "Folders and Other shortcuts cover nine each");
        check(is(placeOf(grid, "Go back"), 33, 1, 2) && is(placeOf(grid, "ESC"), 33, 2) &&
                  is(placeOf(grid, "Alt + Arrow Left"), 34, 2) && is(placeOf(grid, "Redo go back"), 35, 1),
              "Go back covers two rows inside Other shortcuts");
        check(is(placeOf(grid, "Record Notes"), 41, 0, 5) && is(placeOf(grid, "Pause Recording"), 44, 1, 2) &&
                  is(placeOf(grid, "Ctrl + P"), 45, 2),
              "Record Notes covers five, Pause Recording its last two");
        check(is(placeOf(grid, "Calls"), 46, 0, 4) && is(placeOf(grid, "Ctrl + PageDown"), 49, 2),
              "Calls covers the last four");
    }

    auto spans = app.parser.parse(
        "<table>\n"
        "<tr><th rowspan=\"2\">Region</th><th colspan=\"2\">First</th><th colspan=\"2\">Second</th></tr>\n"
        "<tr><th>Q1</th><th>Q2</th><th>Q3</th><th>Q4</th></tr>\n"
        "<tr><td>North</td><td>12</td><td>15</td><td colspan=\"2\" rowspan=\"2\">none</td></tr>\n"
        "<tr><td>South</td><td>9</td><td>11</td></tr>\n"
        "<tr><td>East</td><td colspan=\"4\">closed</td></tr>\n"
        "<tr><th rowspan=\"0\">West</th><td>1</td></tr>\n"
        "<tr><td colspan=\"9\">wide</td></tr>\n"
        "</table>\n");
    table = spans.root ? firstTable(spans.root.get()) : nullptr;
    check(table != nullptr, "the table of spans is a table");
    if (table) {
        const TableGrid grid = tableGrid(*table);
        check(grid.rowCount == 7 && grid.colCount == 10 && grid.headerRows == 2,
              "seven rows, two of them the header: the rows of <th> alone that lead it");
        check(is(placeOf(grid, "Region"), 0, 0, 2) && is(placeOf(grid, "First"), 0, 1, 1, 2) &&
                  is(placeOf(grid, "Second"), 0, 3, 1, 2),
              "the header's cells over two rows and two columns");
        check(is(placeOf(grid, "Q1"), 1, 1) && is(placeOf(grid, "Q4"), 1, 4), "the second header row starts past Region");
        check(is(placeOf(grid, "none"), 2, 3, 2, 2) && is(placeOf(grid, "South"), 3, 0) && is(placeOf(grid, "11"), 3, 2),
              "a cell over two rows and two columns, the row under it short");
        check(is(placeOf(grid, "closed"), 4, 1, 1, 4), "a cell over four columns");
        check(is(placeOf(grid, "West"), 5, 0, 2) && placeOf(grid, "West").header && !placeOf(grid, "1").header,
              "rowspan 0 runs to the last row; a <th> in the body is a header cell");
        check(is(placeOf(grid, "wide"), 6, 1, 1, 9), "the last row's cell starts past West and covers nine columns");
        check(placeOf(grid, "Region").header && placeOf(grid, "Q3").header && !placeOf(grid, "North").header,
              "the header rows' cells are header cells");
    }

    auto pipes = app.parser.parse("| a | b | c |\n|---|---|---|\n| d | e | f |\n| g | h | i |\n");
    table = pipes.root ? firstTable(pipes.root.get()) : nullptr;
    check(table != nullptr, "a pipe table");
    if (table) {
        const TableGrid grid = tableGrid(*table);
        bool plain = grid.rowCount == 3 && grid.colCount == 3 && grid.headerRows == 1 && grid.cells.size() == 9;
        for (size_t i = 0; plain && i < grid.cells.size(); i++) {
            const auto& cell = grid.cells[i];
            plain = cell.row == (int)i / 3 && cell.col == (int)i % 3 && cell.rowSpan == 1 && cell.colSpan == 1 &&
                    cell.header == (i < 3) && grid.slots[i] == (int)i;
        }
        check(plain, "a pipe table's grid is its rows and columns under one header row");
    }
}

// --- The page, laid out ---------------------------------------------------------

bool lay(App& app, const std::string& source, int width, bool edit = false) {
    app.width = width;
    app.height = 900;
    const auto doc = app.parser.parse(source);
    if (!doc.success) return false;
    app.root = doc.root;
    app.layoutDirty = true;
    app.editMode = edit;  // the cells' places are kept in edit mode (in-place table editing)
    layoutDocument(app);
    ensureLayoutComplete(app);
    app.editMode = false;
    return true;
}

// Where the n-th `text` of the page is drawn: the box of its first word
bool boxOf(const App& app, const std::wstring& text, D2D1_RECT_F& box, size_t nth = 0, size_t* docAt = nullptr) {
    size_t at = app.docText.find(text);
    for (size_t i = 0; i < nth && at != std::wstring::npos; i++) at = app.docText.find(text, at + 1);
    if (at == std::wstring::npos) return false;
    if (docAt) *docAt = at;
    for (const auto& rect : app.textRects) {
        if (rect.docLength == 0 || at < rect.docStart || at >= rect.docStart + rect.docLength) continue;
        box = rect.rect;
        return true;
    }
    return false;
}
D2D1_RECT_F at(const App& app, const std::wstring& text, const std::string& where, size_t nth = 0) {
    D2D1_RECT_F box{};
    check(boxOf(app, text, box, nth), where + ": '" + toUtf8(text) + "' is on the page");
    return box;
}
bool sameX(const D2D1_RECT_F& a, const D2D1_RECT_F& b) { return std::fabs(a.left - b.left) < 0.6f; }
bool sameRow(const D2D1_RECT_F& a, const D2D1_RECT_F& b) { return std::fabs(a.top - b.top) < 0.6f; }

// What must hold of any page: no two stretches of text over each other,
// every table's text inside it, what follows a table under it, and the
// page as tall as what is on it
void sound(const App& app, const std::string& where, size_t tables) {
    check(app.tableRects.size() == tables,
          where + ": " + std::to_string(tables) + " tables (" + std::to_string(app.tableRects.size()) + ")");
    size_t over = 0;
    std::string first;
    for (size_t i = 0; i < app.textRects.size(); i++) {
        const auto& a = app.textRects[i];
        if (a.docLength == 0) continue;
        for (size_t k = i + 1; k < app.textRects.size(); k++) {
            const auto& b = app.textRects[k];
            if (b.docLength == 0 || !uiframe::overlap(a.rect, b.rect)) continue;
            // Two boxes of one stretch of text are the same letters
            if (a.docStart < b.docStart + b.docLength && b.docStart < a.docStart + a.docLength) continue;
            if (over++ == 0) {
                first = toUtf8(app.docText.substr(a.docStart, std::min<size_t>(a.docLength, 24))) + " / " +
                        toUtf8(app.docText.substr(b.docStart, std::min<size_t>(b.docLength, 24)));
            }
        }
    }
    check(over == 0, where + ": no text is drawn over other text (" + std::to_string(over) + ": " + first + ")");

    float lowest = 0.0f;
    for (const auto& run : app.layoutTextRuns) lowest = std::max(lowest, run.bounds.bottom);
    check(app.contentHeight >= lowest - 0.5f, where + ": the page is as tall as its text");

    for (size_t t = 0; t < app.tableRects.size(); t++) {
        const D2D1_RECT_F& table = app.tableRects[t].bounds;
        const std::string which = where + ", table " + std::to_string(t + 1);
        check(uiframe::width(table) > 20.0f && uiframe::height(table) > 20.0f, which + ": has a size");
        size_t inside = 0, astray = 0, straddle = 0;
        for (const auto& rect : app.textRects) {
            if (rect.docLength == 0) continue;
            const float middle = (rect.rect.top + rect.rect.bottom) / 2;
            const bool within = middle > table.top && middle < table.bottom && rect.rect.left >= table.left - 0.5f &&
                                rect.rect.left < table.right;
            if (within) {
                inside++;
                if (rect.rect.right > table.right + 0.5f || rect.rect.top < table.top - 0.5f ||
                    rect.rect.bottom > table.bottom + 0.5f) {
                    astray++;
                }
            } else if (rect.rect.top < table.bottom - 0.5f && rect.rect.bottom > table.top + 0.5f &&
                       rect.rect.left < table.right && rect.rect.right > table.left) {
                straddle++;
            }
        }
        check(inside > 0, which + ": has text in it");
        check(astray == 0, which + ": its text is inside it (" + std::to_string(astray) + " boxes are not)");
        check(straddle == 0, which + ": no other text reaches into it (" + std::to_string(straddle) + ")");
        // Its lines and its rows' bands stay inside it too
        bool linesIn = true;
        for (const auto& line : app.layoutLines) {
            const float y = (line.p1.y + line.p2.y) / 2, x = (line.p1.x + line.p2.x) / 2;
            if (y < table.top - 0.5f || y > table.bottom + 0.5f || x < table.left || x > table.right) continue;
            linesIn = linesIn && std::min(line.p1.x, line.p2.x) >= table.left - 0.6f &&
                      std::max(line.p1.x, line.p2.x) <= table.right + 0.6f &&
                      std::min(line.p1.y, line.p2.y) >= table.top - 0.6f &&
                      std::max(line.p1.y, line.p2.y) <= table.bottom + 0.6f;
        }
        check(linesIn, which + ": its lines stay inside it");
    }
}

// A line across the table at `y` that runs through `x`
bool lineThrough(const App& app, float x, float top, float bottom) {
    for (const auto& line : app.layoutLines) {
        if (std::fabs(line.p1.y - line.p2.y) > 0.1f) continue;
        if (line.p1.y <= top + 1.0f || line.p1.y >= bottom - 1.0f) continue;
        if (std::min(line.p1.x, line.p2.x) < x && std::max(line.p1.x, line.p2.x) > x) return true;
    }
    return false;
}

void theIssue(App& app) {
    for (int theme : {0, 5}) {
        applyTheme(app, theme);
        for (int width : {1300, 760, 420}) {
            const std::string where = std::string(theme ? "dark" : "light") + " at " + std::to_string(width);
            check(lay(app, g_issue, width), where + ": the report's page is laid out");
            sound(app, where, 1);
            if (app.tableRects.size() != 1) continue;
            const D2D1_RECT_F table = app.tableRects[0].bounds;
            check(width < 760 || app.contentWidth <= documentViewportWidth(app) + 1.0f,
                  where + ": the page is no wider than the window");

            // The three columns: every cell's text starts where its column does
            const D2D1_RECT_F move = at(app, L"Move among chats", where), next = at(app, L"Next chat", where),
                              tab = at(app, L"Ctrl + Tab", where);
            check(move.left < next.left - 20.0f && next.left < tab.left - 20.0f, where + ": three columns, left to right");
            for (const wchar_t* first : {L"Search", L"Selected Text", L"Folders", L"Other shortcuts", L"Record Notes", L"Calls"})
                check(sameX(at(app, first, where), move), where + ": " + toUtf8(first) + " is in the first column");
            for (const wchar_t* second : {L"Previous chat", L"Chat search", L"Undo", L"Go back", L"Redo go back",
                                          L"Pause Recording", L"Toggle Microphone"})
                check(sameX(at(app, second, where), next), where + ": " + toUtf8(second) + " is in the second column");
            for (const wchar_t* third : {L"Alt + Arrow Up", L"Ctrl + Page Up", L"Ctrl + Shift + Tab", L"Ctrl + Page Down",
                                         L"ESC", L"Alt + Arrow Left", L"Space (when", L"Ctrl + PageDown"})
                check(sameX(at(app, third, where), tab), where + ": " + toUtf8(third) + " is in the third column");

            // The rows: a cell over several rows starts on the first of
            // them, and the rows it covers follow one under the other
            check(sameRow(move, next) && sameRow(next, tab), where + ": the first row's three cells are on one row");
            const D2D1_RECT_F up = at(app, L"Alt + Arrow Up", where), pageUp = at(app, L"Ctrl + Page Up", where),
                              previous = at(app, L"Previous chat", where), shiftTab = at(app, L"Ctrl + Shift + Tab", where),
                              search = at(app, L"Search", where), chatSearch = at(app, L"Chat search", where);
            check(up.top > tab.bottom - 0.5f && pageUp.top > up.bottom - 0.5f && previous.top > pageUp.bottom - 0.5f,
                  where + ": the keys of Next chat follow one under the other, Previous chat under them");
            check(sameRow(previous, shiftTab) && sameRow(search, chatSearch), where + ": a cell starts on its first row");
            check(sameRow(at(app, L"Go back", where), at(app, L"ESC", where)) &&
                      at(app, L"Alt + Arrow Left", where).top > at(app, L"ESC", where).bottom - 0.5f &&
                      at(app, L"Redo go back", where).top > at(app, L"Alt + Arrow Left", where).bottom - 0.5f,
                  where + ": Go back and its two keys, then Redo go back");
            // The <br> in a cell is a line break in it
            const D2D1_RECT_F selected = at(app, L"Selected Text", where), typing = at(app, L"in typing area", where);
            check(sameX(selected, typing) && typing.top > selected.bottom - 0.5f,
                  where + ": the line after a <br> starts under the one before it");

            // A cell over fifteen rows with two lines in it leaves its rows
            // the height they have: its first row is no taller than the next
            const D2D1_RECT_F undo = at(app, L"Undo", where), redo = at(app, L"Redo", where), cut = at(app, L"Cut", where);
            check(std::fabs((redo.top - undo.top) - (cut.top - redo.top)) < 0.6f && redo.top > undo.bottom,
                  where + ": a joined cell's second line does not make its first row taller");

            // No line across a cell that covers several rows, and a line under its last
            check(!lineThrough(app, move.left + 2.0f, move.bottom, at(app, L"Ctrl + Page Down", where).top),
                  where + ": no line runs across Move among chats");
            check(lineThrough(app, tab.left + 2.0f, tab.bottom, up.top + 2.0f) &&
                      lineThrough(app, move.left + 2.0f, at(app, L"Ctrl + Page Down", where).bottom, search.top + 2.0f),
                  where + ": a line parts its keys, and one runs under it");
            check(!lineThrough(app, next.left + 2.0f, next.bottom, pageUp.top),
                  where + ": no line runs across Next chat");

            // What comes before and after
            const D2D1_RECT_F title = at(app, L"Keyboard shortcuts", where), after = at(app, L"The paragraph after", where);
            check(title.bottom <= table.top + 0.5f, where + ": the bold line is above the table");
            check(after.top >= table.bottom - 0.5f, where + ": the paragraph after the table starts below it");
            check(at(app, L"Ctrl + PageDown", where).bottom <= table.bottom + 0.5f, where + ": the last row is inside the table");
            check(at(app, L"A list item after", where).top > after.bottom - 0.5f && at(app, L"A quote closes", where).top > after.bottom,
                  where + ": the list and the quote follow");
        }
    }
    applyTheme(app, 0);
}

void theMixed(App& app) {
    for (int theme : {0, 5}) {
        applyTheme(app, theme);
        for (int width : {1300, 760, 420}) {
            const std::string where = std::string(theme ? "dark" : "light") + " at " + std::to_string(width) + ", mixed";
            check(lay(app, g_mixed, width), where + ": laid out");
            sound(app, where, 11);
            if (app.tableRects.size() != 11) continue;
            auto table = [&](size_t i) { return app.tableRects[i].bounds; };

            // The same table in HTML and with pipes is the same table
            check(uiframe::about(uiframe::width(table(0)), uiframe::width(table(1))) &&
                      uiframe::about(uiframe::height(table(0)), uiframe::height(table(1))) &&
                      uiframe::about(table(0).left, table(1).left),
                  where + ": the plain table is as wide and as tall as the pipe table of the same cells");
            check(sameX(at(app, L"Open a file", where, 0), at(app, L"Open a file", where, 1)) &&
                      sameX(at(app, L"Ctrl + O", where, 0), at(app, L"Ctrl + O", where, 1)),
                  where + ": and its cells start where the pipe table's do");
            check(app.tableRects[0].tsv == app.tableRects[1].tsv, where + ": and it is copied as the same cells");

            // What follows each table is under it
            const struct { size_t table; const wchar_t* text; } follows[] = {
                {0, L"The paragraph after the plain table"}, {1, L"Head, body and foot"}, {2, L"Columns and rows joined"},
                {3, L"Inline content in cells"}, {4, L"End tags left out"}, {5, L"The paragraph after the unclosed"},
                {6, L"Among other blocks"}, {7, L"The item after it"}, {8, L"And the quote's last line"},
                {9, L"A table within a cell"}, {10, L"The last paragraph, after every table"}};
            for (const auto& item : follows) {
                D2D1_RECT_F box{};
                check(boxOf(app, item.text, box) && box.top >= table(item.table).bottom - 0.5f,
                      where + ": '" + toUtf8(item.text) + "' is under table " + std::to_string(item.table + 1));
            }
            // The caption is over its table; the foot's row is its last
            check(at(app, L"Sales by", where).bottom <= table(2).top + 0.5f && at(app, L"Sales by", where).top > table(1).bottom,
                  where + ": the caption stands over its table");
            check(at(app, L"Total", where).top > at(app, L"Second", where).bottom - 0.5f, where + ": the foot's row is the last");
            // align="right" and "center"
            const D2D1_RECT_F units = at(app, L"Units", where), wide = at(app, L"1,204", where), narrow = at(app, L"980", where);
            check(std::fabs(wide.right - narrow.right) < 0.6f && std::fabs(wide.right - units.right) < 0.6f && narrow.left > wide.left + 2.0f,
                  where + ": a column aligned right ends on one edge");

            // The table of spans
            const D2D1_RECT_F region = at(app, L"Region", where), firstHalf = at(app, L"First half", where),
                              secondHalf = at(app, L"Second half", where), q1 = at(app, L"Q1", where), q3 = at(app, L"Q3", where),
                              north = at(app, L"North", where), none = at(app, L"no data", where), south = at(app, L"South", where),
                              eleven = at(app, L"11", where), east = at(app, L"East", where), closed = at(app, L"closed", where),
                              west = at(app, L"West", where), note = at(app, L"a note under", where), five = at(app, L"5\t6\t7\t8", where);
            check(sameRow(region, firstHalf) && sameRow(firstHalf, secondHalf) && q1.top > firstHalf.bottom - 0.5f,
                  where + ": the header's two rows");
            check(sameX(q1, firstHalf) && sameX(q3, secondHalf) && sameX(none, q3) && sameRow(none, north),
                  where + ": a cell over two columns starts where the first of them does");
            // Its two columns are wide enough between them for its words
            check(width < 1300 || (sameRow(firstHalf, at(app, L"half\tSecond", where)) &&
                                   at(app, L"Q2", where).right < secondHalf.left && sameRow(none, at(app, L"the south", where))),
                  where + ": a cell over two narrow columns widens them: its words stay on one line");
            check(sameX(south, north) && south.top > north.bottom - 0.5f && eleven.right < none.left,
                  where + ": the short row under a cell over two rows keeps to its own columns");
            check(sameX(note, q1) && sameX(five, q1) && sameX(west, region) && note.top > five.bottom - 0.5f,
                  where + ": the rows West covers start in the second column");
            check(closed.left > q1.left + 20.0f && sameRow(closed, east), where + ": a centred cell over four columns");
            // Rows: no text of one row reaches into the next
            check(east.top > south.bottom - 0.5f && west.top > east.bottom - 0.5f, where + ": rows follow one under the other");

            // Cells with inline content
            check(sameX(at(app, L"first line", where), at(app, L"second line", where)) &&
                      at(app, L"second line", where).top > at(app, L"first line", where).bottom - 0.5f &&
                      at(app, L"third line", where).top > at(app, L"second line", where).bottom - 0.5f,
                  where + ": three lines parted by <br> in one cell");
            check(app.docText.find(L"5 < 7 && 7 > 5, a\x00A0" L"b, \"quoted\", \x20AC \x2713") != std::wstring::npos,
                  where + ": character references are the characters");
            bool linked = false;
            for (const auto& link : app.linkRects)
                linked = linked || (link.url == "https://github.com/oipoistar/tinta" && uiframe::inside(link.bounds, table(4)));
            check(linked, where + ": a link in a cell is a link, inside the table");
            check(sameX(at(app, L"a paragraph", where), at(app, L"another", where)) &&
                      app.docText.find(L"a paragraph\nanother\n\x2022 one\n\x2022 two") != std::wstring::npos,
                  where + ": blocks in a cell are lines of it, a list's items marked");

            // Other HTML blocks: one line where the source has three
            const D2D1_RECT_F division = at(app, L"A centered division", where), bold = at(app, L"at its end", where),
                              html = at(app, L"A paragraph in HTML", where), uneven = at(app, L"Uneven rows", where);
            check(app.docText.find(L"A centered division whose lines are indented in the source, bold at its end.") !=
                          std::wstring::npos &&
                      app.docText.find(L"A paragraph in HTML over three indented lines.") != std::wstring::npos,
                  where + ": the lines of an HTML block run on as one");
            check(width < 1300 || (sameRow(division, bold) && sameRow(html, at(app, L"lines.", where))),
                  where + ": and are one line in a wide window");
            check(html.top > bold.bottom - 0.5f && uneven.top > at(app, L"lines.", where).bottom - 0.5f,
                  where + ": the block after an HTML block starts under it");
            // A <summary> and the text under it, two <div>s: a line each
            const D2D1_RECT_F summary = at(app, L"More on tables", where), under = at(app, L"The text under", where),
                              one = at(app, L"First division", where), two = at(app, L"Second division", where);
            check(summary.top > at(app, L"lines.", where).bottom - 0.5f && sameX(summary, under) &&
                      under.top > summary.bottom - 0.5f,
                  where + ": the text under a <summary> starts on the line after it");
            check(one.top > at(app, L"of its own.", where).bottom - 0.5f && sameX(one, two) && two.top > one.bottom - 0.5f &&
                      uneven.top > two.bottom - 0.5f,
                  where + ": two <div>s are two lines, and the paragraph after them is under them");
            // A table inside a cell: lines of the cell
            check(at(app, L"inner three", where).top > at(app, L"inner one", where).bottom - 0.5f &&
                      sameRow(at(app, L"inner one", where), at(app, L"outer", where)),
                  where + ": a table within a cell is lines of that cell");
        }
    }
    applyTheme(app, 0);
}

// --- Selecting, finding, copying, editing -----------------------------------------

void theText(App& app) {
    check(lay(app, g_issue, 1300, true), "the report's page, for its text");
    if (app.tableRects.size() != 1) return;
    const D2D1_RECT_F table = app.tableRects[0].bounds;
    // The text: a tab between a row's cells, a line a row, the <br> a line break
    check(app.docText.find(L"Move among chats\tNext chat\tCtrl + Tab\nAlt + Arrow Up\nCtrl + Page Up\n"
                           L"Previous chat\tCtrl + Shift + Tab\n") != std::wstring::npos,
          "the page's text has a row a line and a tab between its cells");
    check(app.docText.find(L"Selected Text\nin typing area\tUndo\tCtrl + Z\n") != std::wstring::npos,
          "a <br> is a line break in the text");
    // The copy of the table: a cell in the column it starts in
    const std::wstring& tsv = app.tableRects[0].tsv;
    check(tsv.rfind(L"Move among chats\tNext chat\tCtrl + Tab\n\t\tAlt + Arrow Up\n\t\tCtrl + Page Up\n"
                    L"\tPrevious chat\tCtrl + Shift + Tab\n",
                    0) == 0,
          "the table is copied with every cell in its column");
    check(tsv.find(L"Selected Text in typing area\tUndo\tCtrl + Z\n\tRedo\tCtrl + Y\n") != std::wstring::npos,
          "and a <br> as a space");
    check((size_t)std::count(tsv.begin(), tsv.end(), L'\n') == 49, "50 rows are copied");

    // A click in a cell is a place in that cell's text; a selection over
    // two cells is their text
    size_t undoAt = 0, nextAt = 0;
    D2D1_RECT_F undo{}, next{};
    check(boxOf(app, L"Undo", undo, 0, &undoAt) && boxOf(app, L"Next chat", next, 0, &nextAt), "Undo and Next chat are drawn");
    const size_t hit = selectionOffsetAtPoint(app, (undo.left + undo.right) / 2, (undo.top + undo.bottom) / 2);
    check(hit > undoAt && hit < undoAt + 4, "a click on Undo lands in its text");
    check(selectionTextForRange(app, nextAt, nextAt + 20) == L"Next chat\tCtrl + Tab", "two cells selected are their text");
    std::vector<D2D1_RECT_F> bands;
    selectionHighlightRects(app, nextAt, nextAt + 20, bands);
    bool bandsIn = !bands.empty();
    for (const auto& band : bands) bandsIn = bandsIn && uiframe::inside(band, table);
    check(bandsIn, "and are marked inside the table");

    // Find
    app.searchQuery = L"Ctrl + Page Down";
    performSearch(app);
    mapSearchMatchesToLayout(app);
    D2D1_RECT_F tab{};
    boxOf(app, L"Ctrl + Tab", tab);
    check(app.searchMatches.size() == 1, "Ctrl + Page Down is found once");
    if (app.searchMatches.size() == 1) {
        const D2D1_RECT_F mark = app.searchMatches[0].highlightRect;
        check(uiframe::inside(mark, table) && std::fabs(mark.left - tab.left) < 3.0f && uiframe::width(mark) > 40.0f,
              "and marked in the third column");
    }
    app.searchQuery = L"folder";
    performSearch(app);
    mapSearchMatchesToLayout(app);
    bool marksIn = app.searchMatches.size() == 9;
    for (const auto& match : app.searchMatches) marksIn = marksIn && uiframe::inside(match.highlightRect, table);
    check(marksIn, "folder is found nine times, each mark inside the table (" + std::to_string(app.searchMatches.size()) + ")");
    app.searchQuery.clear();
    app.searchMatches.clear();

    // A table written in HTML is not edited in place: it has no pipes to
    // write back. The pipe table beside it still is.
    check(app.tableCellRects.empty() && app.tableRects[0].sourceOffset == SIZE_MAX,
          "in edit mode the HTML table offers no cell to edit and no row or column to add");
    check(lay(app, g_mixed, 1300, true), "the mixed page in edit mode");
    size_t editable = 0;
    for (const auto& info : app.tableRects) editable += info.sourceOffset != SIZE_MAX ? 1 : 0;
    bool inPipes = app.tableRects.size() == 11 && app.tableCellRects.size() == 6;
    for (const auto& cell : app.tableCellRects) inPipes = inPipes && uiframe::inside(cell.rect, app.tableRects[1].bounds);
    check(editable == 1 && inPipes, "of the mixed page's eleven tables the pipe table alone is edited in place");
}

// --- Print, export, pictures ----------------------------------------------------

Frame pageFrame(App& app, int width, int height) {
    app.width = width;
    app.height = height;
    app.printShrinkBands.clear();  // the last print's, which the screen does not have
    return uiframe::draw(app, [&] {
        Microsoft::WRL::ComPtr<ID2D1DeviceContext> dc;
        if (SUCCEEDED(app.renderTarget->QueryInterface(IID_PPV_ARGS(&dc)))) {
            drawLayoutRange(app, dc.Get(), app.brush, 0.0f, (float)height, 0.0f, 0.0f);
        }
    });
}

void printAndExport(App& app) {
    check(lay(app, g_issue, 1000), "the report's page for printing");
    check(printDebugPages(app, (g_frames / "print-issue").wstring()) >= 2, "the report's page prints, over pages");
    check(app.printShrinkBands.empty(), "nothing of it has to be shrunk to fit the paper");
    check(lay(app, g_mixed, 1000), "the mixed page for printing");
    check(printDebugPages(app, (g_frames / "print-mixed").wstring()) >= 2, "the mixed page prints");

    check(lay(app, g_issue, 1000), "the report's page for export");
    check(exportHtmlFile(app, (g_work / "issue.html").wstring()), "the report's page exports to HTML");
    std::string html = readAll(g_work / "issue.html");
    check(html.find("<td rowspan=\"6\">Move among chats</td><td rowspan=\"3\">Next chat</td><td>Ctrl + Tab</td></tr>") !=
              std::string::npos,
          "HTML: the first row with its cells over six and three rows");
    check(html.find("<tr><td>Alt + Arrow Up</td></tr>") != std::string::npos, "HTML: a row of one cell");
    check(count(html, "<tr>") == 50 && count(html, " rowspan=\"") == 11 && html.find("<th") == std::string::npos,
          "HTML: 50 rows, eleven cells over several rows, no header cell");
    check(html.find("<td rowspan=\"15\">Selected Text<br") != std::string::npos, "HTML: the <br> in a cell");
    check(exportDocxFile(app, (g_work / "issue.docx").wstring()), "the report's page exports to Word");
    std::string docx = readAll(g_work / "issue.docx");
    check(count(docx, "<w:tr>") == 50 && count(docx, "<w:tc>") == 150 && count(docx, "<w:gridCol/>") == 3,
          "Word: 50 rows of three cells each");
    check(count(docx, "<w:vMerge w:val=\"restart\"/>") == 11 && count(docx, "<w:vMerge/>") == 49,
          "Word: eleven cells over several rows, merged down through the 49 rows under them");
    check(docx.find("<w:shd ") == std::string::npos || count(docx, "<w:tcPr><w:shd") == 0, "Word: no header cell");

    check(lay(app, g_mixed, 1000), "the mixed page for export");
    check(exportHtmlFile(app, (g_work / "mixed.html").wstring()), "the mixed page exports to HTML");
    html = readAll(g_work / "mixed.html");
    check(count(html, "<table>") == 11, "HTML: eleven tables");
    check(count(html, "<tr><th>Action</th><th>Key</th></tr>\n<tr><td>Open a file</td><td>Ctrl + O</td></tr>") == 2,
          "HTML: the plain table and the pipe table export as the same rows");
    check(html.find("<caption>Sales by <em>quarter</em></caption>") != std::string::npos, "HTML: the caption");
    check(html.find("<tr><th>Quarter</th><th align=\"right\">Units</th><th align=\"center\">Trend</th></tr>") !=
              std::string::npos &&
              html.find("<tr><th>Total</th><td align=\"right\">2,184</td><td></td></tr>\n</table>") != std::string::npos,
          "HTML: the head's cells, and the foot's row last with its header cell");
    check(html.find("<tr><th rowspan=\"2\">Region</th><th colspan=\"2\">First half</th><th colspan=\"2\">Second half</th></tr>") !=
              std::string::npos &&
              html.find("<td colspan=\"2\" rowspan=\"2\">no data") != std::string::npos &&
              html.find("<td colspan=\"4\" align=\"center\">closed</td>") != std::string::npos,
          "HTML: cells over columns and rows");
    check(html.find("<tr><th>Name</th><th>Since</th></tr>\n<tr><td>Open</td><td>1.0</td></tr>") != std::string::npos,
          "HTML: rows and cells whose end tags were left out are closed");
    check(html.find("<code>tinta.exe --printpages</code> in a cell") != std::string::npos &&
              html.find("<a href=\"https://github.com/oipoistar/tinta\">the repository</a> and text after it") !=
                  std::string::npos && html.find("5 &lt; 7 &amp;&amp; 7 &gt; 5") != std::string::npos,
          "HTML: code, a link and character references in cells");
    check(exportDocxFile(app, (g_work / "mixed.docx").wstring()), "the mixed page exports to Word");
    docx = readAll(g_work / "mixed.docx");
    check(count(docx, "<w:tbl>") == 11, "Word: eleven tables");
    check(count(docx, "<w:gridSpan w:val=\"2\"/>") == 4 && count(docx, "<w:gridSpan w:val=\"4\"/>") == 1 &&
              count(docx, "<w:gridSpan w:val=\"3\"/>") == 1,
          "Word: cells over two, three and four columns");
    check(docx.find("<w:gridSpan w:val=\"2\"/><w:vMerge w:val=\"restart\"/>") != std::string::npos &&
              docx.find("<w:gridSpan w:val=\"2\"/><w:vMerge/>") != std::string::npos,
          "Word: the cell over two columns and two rows, and its part in the row under it");
    check(docx.find("Sales by ") != std::string::npos && docx.find("w:val=\"Caption\"") != std::string::npos,
          "Word: the caption is a caption");
    check(docx.find(">in typing area<") == std::string::npos && docx.find(">a list<") != std::string::npos,
          "Word: the table in a list item is a table");

    // Pictures of both pages, for the eye
    for (int theme : {0, 5}) {
        applyTheme(app, theme);
        for (int width : {1300, 760, 420}) {
            for (const std::string* page : {&g_issue, &g_mixed}) {
                if (!lay(app, *page, width)) continue;
                const int height = (int)std::min(app.contentHeight + 40.0f, 7000.0f);
                const Frame frame = pageFrame(app, width, height);
                const std::string file = std::string(page == &g_issue ? "issue-" : "mixed-") + std::to_string(width) +
                                         (theme ? "-dark" : "-light");
                check(uiframe::save(app, frame, (g_frames / (file + ".png")).wstring()), "a picture of " + file);
            }
        }
    }
    applyTheme(app, 0);
    app.height = 900;
}

// --- Live and Source ------------------------------------------------------------

// In Live the table is drawn as the table while the caret is elsewhere, and
// is its lines of HTML, to be typed in, while the caret is in it
void liveAndSource(App& app) {
    const fs::path doc = g_work / "live-table.md";
    std::ofstream(doc, std::ios::binary) << g_issue;
    check(openDocumentInViewer(app, doc.wstring()), "the report's page opens from its file");
    app.width = 1050;
    app.height = 800;
    enterEditMode(app);
    check(app.editMode, "the editor opens on it");

    // Source, the preview beside it: the table is the table there too
    setEditorLive(app, app.hwnd, false);
    app.layoutDirty = true;
    ensureLayoutComplete(app);
    check(!editorLiveOn(app) && app.tableRects.size() == 1 && app.tableCellRects.empty(),
          "beside the source the preview draws the table, with no cell to edit in place");
    if (app.tableRects.size() == 1) sound(app, "the preview beside the source", 1);

    setEditorLive(app, app.hwnd, true);
    check(editorLiveOn(app), "Live is on");
    auto paint = [&] {
        if (app.layoutDirty) ensureLayoutComplete(app);
        app.renderTarget->BeginDraw();
        app.renderTarget->Clear(app.theme.background);
        renderLivePage(app);
        app.renderTarget->EndDraw();
    };
    auto caret = [&](size_t pos) {
        app.editorCursorPos = app.editorSelStart = app.editorSelEnd = pos;
        app.editorHasSelection = false;
        paint();
    };
    caret(4);  // in the first heading
    check(app.tableRects.size() == 1, "in Live the table is a table");
    if (app.tableRects.size() != 1) return;
    sound(app, "Live", 1);
    const D2D1_RECT_F table = app.tableRects[0].bounds;
    check(sameX(at(app, L"Previous chat", "Live"), at(app, L"Next chat", "Live")) &&
              sameX(at(app, L"Alt + Arrow Up", "Live"), at(app, L"Ctrl + Tab", "Live")) &&
              at(app, L"The paragraph after", "Live").top >= table.bottom - 0.5f,
          "Live: its cells in their columns, the paragraph after it under it");

    // The caret in the table's source: the stretch shown as typed starts
    // where the table does and is as long as its lines of HTML
    const size_t inTable = app.editorText.find(L"Next chat");
    check(inTable != std::wstring::npos, "the source has the table's text");
    caret(inTable + 9);
    float top = 0, bottom = 0, shift = 0;
    check(livePageGeometry(app, top, bottom, shift), "the caret's stretch of the page is placed");
    check(top < table.top + 60.0f && top > table.top - 120.0f && bottom > table.bottom,
          "the caret in the table's source opens the whole table as its HTML");
    // Typing there is typing in the HTML; back out of it, the table shows it
    keystroke(app, 'S');
    check(app.editorText.find(L"<td rowspan=\"3\">Next chats</td>") != std::wstring::npos, "a letter typed in a cell is in its source");
    caret(4);
    check(app.tableRects.size() == 1 && app.docText.find(L"Move among chats\tNext chats\tCtrl + Tab\n") != std::wstring::npos,
          "and in the table once the caret has left it");
    if (app.tableRects.size() == 1) sound(app, "Live, after typing", 1);
    // A row added in the source is a row of the table
    const size_t lastRow = app.editorText.find(L"</table>");
    check(lastRow != std::wstring::npos, "the source has the table's end");
    caret(lastRow);
    for (wchar_t c : std::wstring(L"<tr><td>new<td>row<td>F1")) {
        const SHORT scan = VkKeyScanW(c);
        KeyMods mods;
        mods.shift = (HIBYTE(scan) & 1) != 0;
        keystroke(app, LOBYTE(scan), mods, c);
    }
    keystroke(app, VK_RETURN);
    caret(4);
    check(app.tableRects.size() == 1 && app.docText.find(L"Ctrl + PageDown\nnew\trow\tF1\n") != std::wstring::npos,
          "a row typed before </table>, its end tags left out, is the table's last row");
    if (app.tableRects.size() == 1) {
        sound(app, "Live, a row added", 1);
        check(sameX(at(app, L"new", "Live", 0), at(app, L"Move among chats", "Live")) &&
                  sameX(at(app, L"F1", "Live"), at(app, L"Ctrl + Tab", "Live")),
              "its three cells in the three columns");
    }
    app.editorDirty = false;
    exitEditMode(app);
    app.width = 1300;
    app.height = 900;
}

// --- How long it takes -----------------------------------------------------------

// 1,500 rows of four cells: as a pipe table, as the same table in HTML, and
// in HTML with a cell over three rows leading every third row
std::string bigTable(int kind) {
    std::string out = kind == 0 ? "| Group | Action | Key | Note |\n|---|---|---|---|\n"
                                : "<table>\n<tr><th>Group</th><th>Action</th><th>Key</th><th>Note</th></tr>\n";
    for (int i = 0; i < 1500; i++) {
        const std::string n = std::to_string(i);
        const std::string cells[] = {"Group " + std::to_string(i / 3), "Action number " + n, "Ctrl + Shift + " + n,
                                     "a note on row " + n};
        if (kind == 0) {
            out += "| " + cells[0] + " | " + cells[1] + " | " + cells[2] + " | " + cells[3] + " |\n";
        } else if (kind == 1) {
            out += "<tr><td>" + cells[0] + "</td><td>" + cells[1] + "</td><td>" + cells[2] + "</td><td>" + cells[3] + "</td></tr>\n";
        } else {
            out += "  <tr>\n";
            if (i % 3 == 0) out += "    <td rowspan=\"3\">" + cells[0] + "</td>\n";
            out += "    <td>" + cells[1] + "</td>\n    <td>" + cells[2] + "</td>\n    <td>" + cells[3] + "</td>\n";
        }
    }
    return out + (kind == 0 ? "" : "</table>\n");
}

// A long document of 1,500 sections, each a heading, a paragraph, a list, a
// table of six rows and a quote: the tables with pipes, or in HTML with
// joined cells
std::string longDocument(bool html) {
    std::string out;
    for (int i = 0; i < 1500; i++) {
        const std::string n = std::to_string(i);
        out += "## Section " + n + "\n\nParagraph " + n + " with **bold**, *italic*, `code` and a [link](https://example.com/" + n +
               "). It runs long enough to wrap over a line or two at the width of a page, so the layout has words to "
               "measure and lines to break.\n\n- item one of " + n + "\n- item two with `code`\n- item three\n\n";
        out += html ? "<table>\n  <tr><th>Action</th><th>Key</th><th>Note</th></tr>\n" : "| Action | Key | Note |\n|---|---|---|\n";
        for (int r = 0; r < 6; r++) {
            const std::string row = std::to_string(r);
            if (html) {
                out += "  <tr>" + (r % 3 == 0 ? "<td rowspan=\"3\">Action " + n + "." + row + "</td>" : std::string()) +
                       "<td>Ctrl + " + row + "</td><td>a note on row " + row + " of table " + n + "</td>\n";
            } else {
                out += "| Action " + n + "." + row + " | Ctrl + " + row + " | a note on row " + row + " of table " + n + " |\n";
            }
        }
        out += (html ? "</table>\n" : "") + std::string("\n> A quote in section ") + n + ".\n\n";
    }
    return out;
}

// The best of a few readings and layouts of a page, in milliseconds
void timed(App& app, const std::string& source, int runs, double& parse, double& layout) {
    parse = layout = 1e9;
    for (int i = 0; i < runs; i++) {
        const auto t0 = std::chrono::steady_clock::now();
        const auto doc = app.parser.parse(source);
        const auto t1 = std::chrono::steady_clock::now();
        app.width = 1000;
        app.height = 900;
        app.root = doc.root;
        app.layoutDirty = true;
        layoutDocument(app);
        ensureLayoutComplete(app);
        const auto t2 = std::chrono::steady_clock::now();
        parse = std::min(parse, std::chrono::duration<double, std::milli>(t1 - t0).count());
        layout = std::min(layout, std::chrono::duration<double, std::milli>(t2 - t1).count());
    }
}

void timing(App& app) {
    double parse[3] = {}, layout[3] = {};
    float height[3] = {};
    for (int kind = 0; kind < 3; kind++) {
        timed(app, bigTable(kind), 5, parse[kind], layout[kind]);
        height[kind] = app.contentHeight;
        check(app.tableRects.size() == 1, "1,500 rows are one table");
    }
    std::cout << "1,500 rows of four cells, read + laid out: pipes " << parse[0] << " + " << layout[0] << " ms, HTML "
              << parse[1] << " + " << layout[1] << " ms, HTML with joined cells " << parse[2] << " + " << layout[2]
              << " ms\n";
    check(uiframe::about(height[0], height[1]) && uiframe::about(height[1], height[2]),
          "the same rows make a table as tall with pipes, in HTML and with joined cells");
    check(parse[1] < parse[0] * 20.0 + 500.0 && layout[1] < layout[0] * 3.0 + 200.0 && layout[2] < layout[0] * 4.0 + 300.0,
          "a table in HTML does not take out of all proportion longer than a pipe table");

    double pipesParse = 0, pipesLayout = 0, htmlParse = 0, htmlLayout = 0;
    timed(app, longDocument(false), 3, pipesParse, pipesLayout);
    const size_t pipeTables = app.tableRects.size();
    timed(app, longDocument(true), 3, htmlParse, htmlLayout);
    std::cout << "1,500 sections with a table each, read + laid out: pipes " << pipesParse << " + " << pipesLayout
              << " ms, HTML with joined cells " << htmlParse << " + " << htmlLayout << " ms\n";
    check(pipeTables == 1500 && app.tableRects.size() == 1500, "a long document's 1,500 tables are tables either way");
    check(htmlParse < pipesParse * 20.0 + 500.0 && htmlLayout < pipesLayout * 3.0 + 500.0,
          "a long document with its tables in HTML does not take out of all proportion longer");
}
}  // namespace

int runHtmlTableTests(const char* fixtureRoot) {
    HRESULT ole = OleInitialize(nullptr);
    if (FAILED(ole)) return 2;
    g_fixtures = fixtureRoot;
    g_work = fs::current_path() / "html-table";
    g_frames = fs::current_path() / "frames";
    std::error_code ignored;
    fs::remove_all(g_work, ignored);
    fs::create_directories(g_work);
    fs::create_directories(g_frames);
    g_issue = unixLines(readAll(g_fixtures / "html-table-325.md"));
    g_mixed = unixLines(readAll(g_fixtures / "html-table-mixed-325.md"));
    check(g_issue.size() > 4000 && g_mixed.size() > 3000, "the fixtures are there");

    {
        Window window;
        if (!window.ok) {
            check(false, "a window for the test");
        } else {
            App& app = window.app;
            tabsInit(app);
            app.readingWidthPct = 100;
            theTree(app);
            theGrid(app);
            theIssue(app);
            theMixed(app);
            theText(app);
            printAndExport(app);
            timing(app);
            liveAndSource(app);
        }
    }

    CoUninitialize();
    OleUninitialize();
    std::cout << "HTML tables: " << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
