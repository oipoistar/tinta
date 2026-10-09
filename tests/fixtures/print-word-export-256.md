# 导出与打印 (#256, #257, #258)

Print this document (Ctrl+P) and export it to Word with a custom theme whose
fonts are set in `themes.ini`, for example `print-word-export-themes.ini` from
this folder. The printed pages and the Word file use the theme's body, heading
and code fonts, and the printed text runs between the page margins whatever
the reading width in Settings. The margin chips at the bottom of the preview
(12.7, 19.05 and 25.4 mm) re-lay the pages out and are remembered.

## 正文

这是一段**中文**正文，夹杂 *English words*、`inline code` 和一个
[链接](https://example.com)。打印和导出时，中文、英文和代码都应使用主题字体，与屏幕显示一致。

- 列表第一项：打印预览
- 列表第二项：导出为 PDF
- 列表第三项：导出为 Word

> 引用：页边距之内的正文宽度应与 Word 导出的版面接近。

| 项目 | 屏幕 | 打印 |
| --- | --- | --- |
| 正文字体 | 主题字体 | 主题字体 |
| 代码字体 | 主题代码字体 | 主题代码字体 |

## 代码

A short block:

```python
print("你好")
```

A block wider than the page, which prints shrunk to the margins:

```text
这一行很长很长很长很长很长很长很长很长很长很长很长很长很长很长很长很长很长很长很长很长很长 and it keeps going past the right edge of any page
```

Another short block:

```js
const greeting = "你好";
```

### 结尾

最后一段：所有代码段应与正文左右对齐。
