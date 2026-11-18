# Code ready on the first frame

Repeated code fragments must keep their **syntax colours**, *spacing*, and selection ranges. A [link](https://example.com) and `inline code` remain outside the fenced blocks.

```cpp
/*
int
*/
int
const auto value = compute(1234); // repeated comment
const auto value = compute(1234); // repeated comment
const auto value = compute(1234); // repeated comment
const auto result = compute("a longer line with repeated words and punctuation that wraps in a narrow window", value);
const auto result = compute("a longer line with repeated words and punctuation that wraps in a narrow window", value);
```

> A quote after the code, with **bold text** and `inline code`.

- First item.
- Second item.

## Another language

```python
value = "café 中文 日本語 한글 😀"
value = "café 中文 日本語 한글 😀"
print(value, value, value)
print(value, value, value)
```

| Check | Expected |
|:------|---------:|
| Source copied | Complete |
| First frame | Same text |

The final paragraph stays below both code blocks and the table.
