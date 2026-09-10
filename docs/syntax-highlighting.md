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

Open [the mixed language sample](../tests/fixtures/syntax-languages.md) or
[the multiline and alias sample](../tests/fixtures/syntax-multiline.md) in Tinta
to try the new rules.
