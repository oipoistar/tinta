# Bundled citation styles

Tinta ships these Citation Style Language files for citations
(docs/specs/scholar.md). They come unchanged from the official CSL
repositories, [styles](https://github.com/citation-style-language/styles) and
[locales](https://github.com/citation-style-language/locales), fetched on
2026-11-03. The build packs each one (`tools/csl_pack.cpp` drops comments,
the `<info>` block and indentation, then compresses it) into `tinta.exe`.

| File | Style or locale | Authors and contributors |
| --- | --- | --- |
| `apa.csl` | APA Style 7th edition | Brenton M. Wiernik, Andrew Dunning; Jay Pfaffman |
| `chicago-author-date.csl` | Chicago Manual of Style 18th edition (author-date) | Andrew Dunning |
| `ieee.csl` | IEEE Reference Guide version 11.29.2023 | Michael Berkowitz; Julian Onions, Rintze Zelle, Stephen Frank, Sebastian Karcher, Giuseppe Silano, Patrick O'Brien, Brenton M. Wiernik, Oliver Couch, Andrew Dunning |
| `nature.csl` | Nature | Michael Berkowitz; Patrick O'Brien |
| `china-national-standard-gb-t-7714-2015-numeric.csl` | China National Standard GB/T 7714-2015 (numeric, 中文) | 牛耕田; Zeping Lee |
| `locales-en-US.xml` | English (US) | Andrew Dunning, Sebastian Karcher, Rintze M. Zelle, Denis Meier, Brenton M. Wiernik |
| `locales-zh-CN.xml` | Chinese (PRC) | rongls, sati-bodhi, Heromyth, Zeping Lee, 韩小土, 韩敏义 |

Each file is licensed under the
[Creative Commons Attribution-ShareAlike 3.0 License](http://creativecommons.org/licenses/by-sa/3.0/),
as its own `<rights>` element states. Tinta's own code stays under its MIT
licence; these files, and any changes to them, stay under CC BY-SA 3.0.
