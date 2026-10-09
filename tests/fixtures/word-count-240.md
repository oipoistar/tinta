---
title: Word count fixture
tags: [count, test]
---

# Word count (#240)

Plain words here: one two three.

**Bold** and *italic* and ~~gone~~ and ==marked== words count once.

A [link text](https://example.com/should-not-count) and `inline code` count;
the URL does not.

![alt text does not count](missing.png)

| Cell one | Cell two |
| --- | --- |
| 中文 | 日本語テキスト |

- 列表项
- item, with punctuation — and a dash

> Quoted words count too.

```js
code block words do not count
```

$$
x^2 + y^2
$$

Inline math $a + b$ is skipped. 你好，世界。

Last line.[^1]

[^1]: Footnote text counts.
