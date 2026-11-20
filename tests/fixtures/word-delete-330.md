# Deleting a word with the keys

Ctrl+Backspace takes the word before the caret and Ctrl+Delete the word after it, in the **source** as in *Live* and in the find bar. Open this page in the source (`E`, then `Ctrl+/` if it opens in Live), put the caret behind a word and press Ctrl+Backspace: the word goes, and the count of characters in the status bar goes down, not up.

## Plain words

These words are separated by spaces, by commas, and by a full stop. A snake_case_name is one word; a hyphenated-pair is two.

> A quoted line ends here.
> Its second line begins at the mark.

## Lists and tasks

- first item of the list
    - nested item with [a link](https://example.com/path/to/page) inside
- [ ] an open task
- [x] a finished task

1. numbered entry

## A table

| Key            | Takes                     |
|----------------|---------------------------|
| Ctrl+Backspace | the word before the caret |
| Ctrl+Delete    | the word after the caret  |

## Code

```cpp
int total_count = first_value + second_value;  // trailing words
    return total_count;
```

## Other scripts

Türkçe çalışma şöyle görünür: ığdır, İstanbul, öğrenci.

中文句子没有空格，一次删掉一段。Mixed 中文 and English words.

Emoji stay whole: thumbs 👍👍 up, and a flag 🇭🇷 too.

The last line has no line break after it