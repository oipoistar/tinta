# A face for CJK letters (#314)

Validated on Windows with the Release build on 2026-11-03.

## What was reported

Bold Chinese words look poor beside Segoe UI: `**内区 Weber 数恰好是经典无量纲流量**`
came out heavy, its strokes running together.

## Cause

Segoe UI draws no Han ideographs; Tinta's fallback sets a document's Chinese
in Microsoft YaHei UI first, and bold in its Bold face. A probe of the same
fallback through a DirectWrite layout finds a real weight-700 face with no
simulated bolding. YaHei UI Bold is simply a heavy design at body sizes, and
there was no way to choose another face for CJK letters without changing the
whole body font.

## Change

`cjkFont=` in settings.ini names an installed face that comes first for a
document's kana, hangul and ideographs, ahead of Tinta's own order; the Word
export names it for CJK as the screen draws it. A face that is not installed
is passed over. It is read at start, like `mathFont=`, and the README lists
both. Interface text keeps its own faces.

## Automated checks

`print_and_word_export` (`wordFonts`): with a Latin body face, `cjkFont=SimSun`
draws Han in SimSun and the Word export's default run properties name it for
East Asian text; a face not installed leaves the faces as they were.
Neutered, the screen check fails without the preferred face in the fallback,
and the Word check without it in the export.

## In the built app

`repro-314.md` in Paper at 1280 px, the reported bold line beside the same
words plain:

- Without the setting the bold Chinese is Microsoft YaHei UI Bold, 77 % more
  ink than the plain words.
- With `cjkFont=Microsoft JhengHei UI` the letters change face and the bold
  is its Bold, 23 % more ink than plain: a lighter bold.

## Size

`tinta.exe` grows by 512 bytes (3,984,384 to 3,984,896).

## Limitations

- No face is bundled: one with lighter bold weights (Source Han Sans,
  DengXian, Noto Sans SC) has to be installed.
- A body font that draws Han itself is used before `cjkFont=`.
