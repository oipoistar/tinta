# Word selection

Double-clicking a word must select exactly that word, in paragraphs and in
table cells alike.

## Paragraph control

Alpha Bravo Charlie.

## Token table

| Alpha | Bravo | Charlie |
| --- | --- | --- |
| Delta | Echo | Foxtrot |
| Golf | Hotel | India |

A single-token cell must never pull its column neighbours into a double-click
selection: each row shares one visual line but the cells are separate words.

## CJK cells

| 甲 | 乙 | 丙 |
| --- | --- | --- |
| 丁 | 戊 | 己 |