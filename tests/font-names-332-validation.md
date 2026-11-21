# Font names in the interface's language (#332)

Validated on Windows with the Release build on 2026-11-21.

## What was reported

The issue has a title and no text: "中文字体名称显示不对", Chinese font
names are shown wrongly. No picture, no steps and no version.

## What it was taken to mean

Tinta lists installed fonts in one place, the theme editor's font list
(Settings > Appearance > New). It listed every family under its en-US
name: in a Chinese interface, on a Chinese Windows, 微软雅黑 was
"Microsoft YaHei" and 宋体 was "SimSun", where Windows' own font lists,
Word and Notepad show the Chinese names. That is the most likely reading of
the title, and it is what was changed. Whether it is what the reporter saw
is not known: the draft reply asks.

## What changed

- **The list shows a family's name in the interface's language** where the
  font has one: the name for the interface's exact locale, else one for
  the same language (Simplified and Traditional Chinese kept apart), else
  the en-US name, else the font's first. In English nothing changes.
- **A theme still keeps the en-US name.** A click on 微软雅黑 puts
  `Microsoft YaHei` into the theme, so a theme reads the same on every
  Windows and in the HTML and Word exports.
- **A theme written by hand may use either name.** DirectWrite finds a
  family by any of its names, and the list now opens on the face a theme
  names in the interface's language as well.
- **The list is sorted as the interface's language sorts**, not by code
  unit. In Chinese the Chinese names come after the Latin ones, in pinyin
  order. In English this puts `Malgun Gothic` before `MS Gothic`, which
  were the other way round.
- **Changing the language** reads the list again; it was read once.

## Fixture

`fixtures/font-names-332.md`: what to do by hand, a table of the names six
families have in Simplified Chinese, Japanese, Korean and Traditional
Chinese (as read from the fonts on this PC), a `themes.ini` that names one
face both ways, and a line each of Chinese, Japanese, Korean and Turkish
with inline code, strong text, emphasis and a link to look at in the face
chosen.

## Automated checks

In **`inline_code_theme`** (`font_names_tests.cpp`, 69 checks),
with the theme editor's other tests.

- Which name is chosen, for names' locales given by hand: the exact
  locale, Singapore reading the Simplified name, Taiwan not reading it,
  Hong Kong reading the Traditional one, the exact locale before a related
  one, Japanese and Korean names only for their own interfaces, a
  language's other region, the English name for a language the font has no
  name in, a font with no English name, capitals in a locale's name, no
  names at all.
- The list read from this PC's fonts in English, Simplified Chinese,
  Japanese, Korean and Traditional Chinese: the same families under the
  same theme names in every language; in English each under the name a
  theme keeps; 微软雅黑 and 宋体 in Chinese, 游ゴシック in Japanese,
  맑은 고딕 in Korean, 微軟正黑體 in Traditional Chinese (five families
  installed here were found under such a name; SimHei and Meiryo are
  checked where they are installed, and are not here); Segoe UI and
  Consolas unchanged; a Japanese family not renamed in Chinese nor a
  Chinese one in Japanese; each list in its language's order; and back in
  English the list as it was.
- DirectWrite finds Microsoft YaHei by its Chinese name.
- The theme editor in a Chinese interface, opened through Settings' own
  click: its list is the Chinese one; it opens on the face a theme names in
  English and on the one a theme names in Chinese, and that row is out of
  sight with the list at its top; a click on the row puts
  `Microsoft YaHei` into the theme.

Each part taken out in turn fails the test:

| Taken out                                         | Failing checks |
|---------------------------------------------------|---------------:|
| The list shows the name in the interface language |              5 |
| The exact locale first                            |              1 |
| Simplified and Traditional apart                  |              3 |
| A related locale                                  |              3 |
| Else the English name                             |              1 |
| Capitals in a locale                              |              1 |
| Read again when the language changes              |              5 |
| A theme keeps the en-US name                      |              1 |
| Sorted as the language sorts                      |              5 |
| The list opens on a face named in the language    |              3 |

The whole suite passes, 88 of 88, without `notification_setting`,
`copy_file_path` and `code_block_layout`, which overwrite the clipboard.
`notification_setting` writes `settings.ini`, which has a new line: it was
run by itself with the clipboard kept and put back, and passes.

## In the built app

Not done for this one: the list is a picture, and screen copies come back
black here. The test opens the same dialog through the same clicks and
paints it.

## Limitations

- The reading of the report is a guess (see above).
- Only names a font carries are shown. An old font whose name table is in
  the wrong encoding still shows what DirectWrite makes of it.
- Fonts are listed in the interface's language, not Windows'. An English
  Tinta on a Chinese Windows shows English names.
- Names are not looked up anywhere else: `cjkFont=` and `mathFont=` in
  `settings.ini` and a CSS theme's `font-family` are used as typed, and
  work with either name.
- The window was not looked at; nor was a Chinese Windows used. The
  families' names were read from the fonts on this English one.
