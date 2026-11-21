# Font names in the interface's language

Set the interface to **简体中文** (Settings → General → Language), then open
Settings → Appearance → **New** for the theme editor. Its font list names
each family as Windows' own font lists do in that language:

| The theme keeps | 简体中文 | 日本語 | 한국어 | 繁體中文 |
|-----------------|----------|--------|--------|----------|
| Microsoft YaHei | 微软雅黑 | Microsoft YaHei | Microsoft YaHei | Microsoft YaHei |
| SimSun | 宋体 | SimSun | SimSun | SimSun |
| Microsoft JhengHei | Microsoft JhengHei | Microsoft JhengHei | Microsoft JhengHei | 微軟正黑體 |
| Yu Gothic | Yu Gothic | 游ゴシック | Yu Gothic | Yu Gothic |
| MS Gothic | MS Gothic | ＭＳ ゴシック | MS Gothic | MS Gothic |
| Malgun Gothic | Malgun Gothic | Malgun Gothic | 맑은 고딕 | Malgun Gothic |
| Segoe UI | Segoe UI | Segoe UI | Segoe UI | Segoe UI |

- A family with no name in the interface's language keeps its English one.
- In an English interface every family has its English name, as before.
- The names in the interface's language sort as that language sorts them:
  in Chinese, the Chinese names come after the Latin ones.
- Clicking a row puts the family's **English** name into the theme, so a
  theme reads the same on every Windows.

## A theme written by hand

`themes.ini` and a CSS theme may name a family either way. Both of these
set the page in the same face:

```ini
[theme]
name=YaHei by its English name
font=Microsoft YaHei

[theme]
name=YaHei by its Chinese name
font=微软雅黑
```

> The editor's list opens on the face a theme names, whichever name the
> theme uses.

## Text to look at in the chosen face

中文：天地玄黄，宇宙洪荒。日月盈昃，辰宿列张。

日本語：いろはにほへと ちりぬるを わかよたれそ つねならむ。

한국어: 다람쥐 헌 쳇바퀴에 타고파.

Türkçe: Pijamalı hasta yağız şoföre çabucak güvendi.

`inline code`, **strong**, *emphasis* and a [link](https://example.com).
