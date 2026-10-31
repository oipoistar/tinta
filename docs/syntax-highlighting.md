# Code syntax highlighting

Add a language after the opening Markdown fence to colour code in Tinta:

````markdown
```sql
SELECT employee_id FROM employees WHERE active = 1;
```
````

Language labels are case-insensitive. Labels that Tinta does not recognise,
and fences without a language, display as plain code.

| Language | Fence labels |
| :--- | :--- |
| C / C++ | `c`, `cpp`, `c++`, `h`, `hpp`, `cxx` |
| C# | `csharp`, `cs`, `c#` |
| Python | `python`, `py` |
| JavaScript / TypeScript | `javascript`, `js`, `jsx`, `typescript`, `ts`, `tsx` |
| JSON | `json` (JavaScript rules) |
| Rust | `rust`, `rs` |
| Go | `go`, `golang` |
| Shell | `bash`, `shell`, `sh`, `zsh` |
| SQL | `sql` |
| PowerShell | `powershell`, `ps`, `pwsh` |
| Java | `java` |
| PHP | `php` |
| HTML | `html`, `htm` |
| XML | `xml` |
| CSS | `css` |
| YAML | `yaml`, `yml` |
| Markdown | `markdown`, `md` |

The nine languages added for [#207](https://github.com/oipoistar/tinta/issues/207)
recognise common keywords, types, calls, numbers, strings and comments where
applicable. Markup adds tags, attributes and entities; CSS adds selectors,
properties and values; YAML adds keys, anchors, aliases and block scalars;
Markdown adds headings, lists, emphasis, code and links.

Multiline support includes SQL comments, quoted strings and dollar-quoted strings;
PowerShell comments and here-strings; Java text blocks; PHP heredocs and nowdocs;
HTML/XML comments, quoted attributes and CDATA; CSS comments; YAML quoted and
block values; and fenced examples inside Markdown. State resets at every outer
Markdown code fence. Copying and searching use the original code text.

Custom themes continue to use `syntaxkeyword`, `syntaxstring`, `syntaxcomment`,
`syntaxnumber`, `syntaxfunction`, `syntaxtype` and `syntaxcontrolflow`. Variables,
attributes and mapping keys use the type colour; plain code and punctuation use
`code`. No additional fonts, packages or runtime are required.

These are lexical colour rules, not a compiler or complete grammar for every
dialect. Calls and type names use heuristics. Interpolation inside strings keeps
the string colour; HTML script/style bodies stay plain, and inner Markdown code
fences use the string colour. Embedded PHP/HTML switching, full XML DTDs and
complete CommonMark parsing inside code blocks are outside this extension.
HTML and DOCX exports retain their existing plain-code formatting; native viewing
and printing use syntax colours.

## Markdown in the source editor

The source editor shows the Markdown itself in colour
([#268](https://github.com/oipoistar/tinta/issues/268)), so a long document
is easier to find your way around and a mark left open shows:

- **Marks go faint**: the `#` of a heading, the asterisks of emphasis, the
  backticks of code, a link's brackets and its address, a table's pipes and
  its delimiter row, a fence's backticks, dividers and setext underlines.
- **Headings** take the theme's heading colour, each level its own where
  the theme sets one, and are bold. **Bold** and *italic* words are bold
  and italic, ~~struck~~ words struck through.
- **The marks that start a block** take the accent: bullets, numbers, task
  boxes, a quote's `>` and a callout's `[!NOTE]`. A finished task's words
  go faint, as on the page.
- **Links**, `[[notes]]`, footnotes, pictures' words and addresses take the
  link colour; inline code and maths the inline code colour; HTML tags go
  faint and HTML comments take the comment colour.
- **Fenced code** takes its language's colours, the same as the preview's,
  and the fence's language the accent; code in a language Tinta does not
  recognise takes the code colour. **The properties** at the top read as
  YAML.

What a line is can depend on the lines above it, so a fence opened further
up keeps the lines below it code, however far down the editor is
scrolled. The source and Live read inline Markdown with the same rules, so
what one shows as code or a link the other does too.

Bold and italic are used only when the editor's font keeps its widths with
them, as monospace fonts do, so the caret, clicks and word wrap measure
exactly what is drawn. Each line is coloured when the editor lays it out to
draw it. On a 290 KB document of 14,800 lines, a key typed at its foot and
the frame after it took 4.2 ms with the colours and 3.4 ms without.

**Settings → Editor → Markdown in colour in the raw editor** turns it off.

Open [the highlighting sample](../tests/fixtures/editor-highlighting-268.md)
in the editor to see every case.

Open [the mixed language sample](../tests/fixtures/syntax-languages.md) or
[the multiline and alias sample](../tests/fixtures/syntax-multiline.md) in Tinta
to try the new rules.
