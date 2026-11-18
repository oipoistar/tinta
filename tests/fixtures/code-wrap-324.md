# Long lines in code blocks

A line of a code block that is longer than the block **wraps** at the block's
edge, so the page never has to be scrolled sideways to read it. *Settings →
Appearance → Wrap long lines in code blocks* turns that off; see the
[notes](#what-to-try) below and the [issue](https://github.com/oipoistar/tinta/issues/324).

> 用法：把下面的代码块整段复制给助手。它只讨论，不修改文稿，也不运行任何任务。

## A prompt kept in a fence

```
你是本项目的独立评审员，请先按下面的顺序只读取证，再做批判性讨论；只讨论，不自动修改文稿或代码，不运行任何生成、模拟或分析任务。讨论时请把每一个判断都落到具体的文件、章节或数据列上，分清哪些是已经看到的事实，哪些是你的解释，哪些还需要验证；遇到互相矛盾的口径，先把两边的出处并排列出来，再说明你倾向哪一边以及理由，不要替作者做决定。

1. 先读 docs/notes/README.md（证据等级、阅读顺序、维护规则）和 docs/notes/state.md（两篇论文的边界、当前主文结构、量化锚点、解释与证据、未决问题），必要时再读 docs/notes/models.md、methods.md、architecture.md、workflow.md。
2. 读 CONTEXT.md、GLOSSARY.md 与 results/data/README.md（当前结果导航、四种统计总体、场强组、速度组、界面、结构）。
3. 列出当前文件清单，并沿 manuscript.tex 实际的 \input 与 \include 语句阅读当前主文与真正被引入的章节；不要只按文件名或修改时间判断“最新”。

完成后先简短给出：a) 实际读取的文件清单；b) 研究主线；c) 你区分出的“已见事实 / 解释 / 待验证”；d) 关键口径冲突与未决问题。
```

The block above has no language, so its lines are plain text. The next one
is highlighted, and its long lines wrap with their colours:

```python
def summarise(records, *, window_ns=(0, 5), speed_ps=(0, 4600), groups=("collector flux", "molecular centre of mass", "last reliable sample"), units={"time": "ns", "speed": "m/s", "field": "V/nm"}, strict=True, on_missing="report", encoding="utf-8"):
    """Return one row per group; a window that is not listed is left out rather than guessed, and a group with no rows is reported as empty."""
    url = "https://example.org/a/very/long/path/that/has/no/spaces/at/all/and/keeps/going/until/it/is/far/wider/than/any/reading/column/would/ever/be/results-2026-11-11.csv"
    return [row for row in records if window_ns[0] <= row.t <= window_ns[1]]
```

Short blocks stay as they are:

```cpp
int answer = 42;  // fits
```

## What to try

1. Narrow the window: the rows of the three blocks follow its width.
2. Select across a wrapped line and copy it: the line is copied as **one** line.
   - A block inside a list wraps at its own, narrower edge:

     ```json
     {"name": "tinta", "description": "A fast, lightweight Markdown reader for Windows", "keywords": ["markdown", "viewer", "direct2d", "directwrite", "native", "mermaid", "latex", "citations", "themes"], "license": "MIT", "platforms": ["windows-x64", "windows-arm64"], "homepage": "https://example.org/tinta"}
     ```

   - `inline code` is not a block and wraps as it always did.
3. Find a word that sits on a wrapped row with ~~the menu~~ `Ctrl+F`.

> A block inside a quote:
>
> ```
> one two three four five six seven eight nine ten eleven twelve thirteen fourteen fifteen sixteen seventeen eighteen nineteen twenty twenty-one twenty-two twenty-three twenty-four twenty-five twenty-six twenty-seven twenty-eight twenty-nine thirty thirty-one thirty-two thirty-three thirty-four thirty-five thirty-six thirty-seven thirty-eight thirty-nine forty
> ```

| Setting          | A long line                          | The page                  |
|------------------|:------------------------------------:|---------------------------|
| on (the default) | wraps at the block's edge            | never scrolls sideways    |
| off              | stays on one row; the block grows    | scrolls sideways, as before |

Indentation is kept on a wrapped line's first row, and the spaces a row ends
on are not drawn:

```text
        eight spaces lead this line, which then runs on and on with ordinary words until it has to go over to a second row and perhaps a third one, and then a little further still so that even a wide window has to wrap it at least once before it ends
AWordWithNoPlaceToBreakAtAllThatIsLongerThanTheBlockIsWideAndSoHasToBeCutInsideItselfWhereTheRowEndsAndCarriedOverToTheNextOneAndTheOneAfterThatAndStillItGoesOnBecauseAWideWindowHoldsAGreatManyLettersInOneRowOfABlockSoThisWordHasToBeVeryLongIndeedToNeedTwoRowsThere
```

- [x] wide tables and diagrams keep their own **Fit** button
- [ ] H~2~O and x^2^ are untouched
