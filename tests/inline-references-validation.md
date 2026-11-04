# References in front matter, and CSL YAML

Checked on Windows with the Release build on 2026-11-04. The design is in
`docs/specs/scholar.md` (phase 2: inline `references:`); the user's page is
`docs/citations.md`.

## What was asked

pandoc lets a document carry its references in its front matter
(`references:`, CSL items in YAML), and a paper's front matter has
structured authors that Tinta's front matter reader leaves opaque.

## The change

- **`yaml.h`** (`src/yaml.cpp`): a small YAML reader into the JSON values
  `json.h` holds: block mappings and lists by indentation, `[ ]` and
  `{ }`, plain, single- and double-quoted values (escapes, folding over
  lines), `|` and `>` blocks with chomping and indentation indicators,
  comments, `---` and `...`, a byte order mark and CRLF. Null, true, false
  and whole numbers are typed and keep their text. Anchors, aliases, tags,
  directives, a second document, tabs in the indentation and keys set
  twice are refused with their line; what was read before a mistake is
  kept. `yaml::frontMatter` finds the front matter as `fm::parse` does.
- **`bib::readCslYaml`**: pandoc's CSL YAML, a list of items or a mapping
  with `references:`, through the CSL-JSON item reader; its text fields are
  pandoc Markdown (`*italic*`, `**bold**`, `^sup^`, `~sub~`,
  `[x]{.smallcaps}`, `[x]{.nocase}`), identifiers excepted. `.yaml` and
  `.yml` bibliographies are read with it.
- **`bib::readFrontMatterReferences`**: a document's `references:` alone,
  so a mistake elsewhere in its front matter costs no reference; problems
  carry the document's lines.

## Fixtures

`fixtures/scholar/inline-references.md`: a mixed document (headings, a
table, a list, a quote, code, emphasis, a link, an e-mail address) whose
front matter holds three references, pandoc's manual example among them,
and structured authors; each reference is cited in the text.

## What was checked

- `yaml_tests` (85 checks): block and flow styles, folding and chomping,
  escapes and surrogate pairs, typed values, offsets, markers, CRLF and a
  byte order mark, each refused construct with its line, front matter
  edge cases, structured authors, pandoc's manual example, and 6,000
  random texts.
- `bib_tests` (538 checks, 69 new): pandoc's example read as CSL YAML and
  matching its CSL-JSON form, Markdown in fields, dates as text, as
  lists and ranges, numbers, flags, problems with lines, the references
  before a mistake kept, the fixture's three references with the
  document's lines, a mistake elsewhere in the front matter, CRLF, inline
  references taking precedence over a file's, and random text.
- `cite_tests` and `scholar_tests` pass unchanged.

## Size

Linked alone into a probe with `/OPT:REF`, the YAML reader and the CSL
YAML readers add 27 KB to the CSL-JSON reader.

## Limitations

- Not wired into Scholar yet: a document with only `references:` does not
  cite until `scholar::begin` reads them (first, so they win over a
  bibliography file's entries, as pandoc's do). `.yaml` bibliographies do
  work now, through `bib::readByExtension`; `scholar.cpp` still calls an
  unknown extension "not a .bib or .json bibliography".
- Not seen in a window, for the reason above.
