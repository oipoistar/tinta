# Code blocks across a page break

A fenced block longer than the page is left runs on to the next one. Each row prints
once, *nothing* of it in the **page margins**, and the card stops where the text does.
See `src/print.cpp` and the [validation notes](../code-block-page-break-validation.md).

```text
row 01 of seventy, plain text
row 02 of seventy, plain text
row 03 of seventy, plain text
row 04 of seventy, plain text
row 05 of seventy, plain text
row 06 of seventy, plain text
row 07 of seventy, plain text
row 08 of seventy, plain text
row 09 of seventy, plain text
row 10 of seventy, plain text
row 11 of seventy, plain text
row 12 of seventy, plain text
row 13 of seventy, plain text
row 14 of seventy, plain text
row 15 of seventy, plain text
row 16 of seventy, plain text
row 17 of seventy, plain text
row 18 of seventy, plain text
row 19 of seventy, plain text
row 20 of seventy, plain text
row 21 of seventy, plain text
row 22 of seventy, plain text
row 23 of seventy, plain text
row 24 of seventy, plain text
row 25 of seventy, plain text
row 26 of seventy, plain text
row 27 of seventy, plain text
row 28 of seventy, plain text
row 29 of seventy, plain text
row 30 of seventy, plain text
row 31 of seventy, plain text
row 32 of seventy, plain text
row 33 of seventy, plain text
row 34 of seventy, plain text
row 35 of seventy, plain text
row 36 of seventy, plain text
row 37 of seventy, plain text
row 38 of seventy, plain text
row 39 of seventy, plain text
row 40 of seventy, plain text
row 41 of seventy, plain text
row 42 of seventy, plain text
row 43 of seventy, plain text
row 44 of seventy, plain text
row 45 of seventy, plain text
row 46 of seventy, plain text
row 47 of seventy, plain text
row 48 of seventy, plain text
row 49 of seventy, plain text
row 50 of seventy, plain text
row 51 of seventy, plain text
row 52 of seventy, plain text
row 53 of seventy, plain text
row 54 of seventy, plain text
row 55 of seventy, plain text
row 56 of seventy, plain text
row 57 of seventy, plain text
row 58 of seventy, plain text
row 59 of seventy, plain text
row 60 of seventy, plain text
row 61 of seventy, plain text
row 62 of seventy, plain text
row 63 of seventy, plain text
row 64 of seventy, plain text
row 65 of seventy, plain text
row 66 of seventy, plain text
row 67 of seventy, plain text
row 68 of seventy, plain text
row 69 of seventy, plain text
row 70 of seventy, plain text
```

## Between the blocks

A list, a table and a quote sit between the two long blocks, so the second break
falls somewhere else in its block.

- a first item with `code`
- a second with a [link](https://example.com)
  1. a numbered child
  2. and its sibling
- a third, *emphasised*

| Page | Holds | Cut |
|------|-------|-----|
| 1 | the heading and the first rows | under a whole row |
| 2 | the rest, the list and this table | inside the second block |
| 3 | what is left | none |

> A quote before the second block. Its bar is a box of its own,
> and it prints whole when it fits the page.

```cpp
// A highlighted row is several runs: all stay on one page.
#include <cstdio>

static int step01(int value) {
    return value * 1 + 1;  // step 01
}

static int step02(int value) {
    return value * 2 + 1;  // step 02
}

static int step03(int value) {
    return value * 3 + 1;  // step 03
}

static int step04(int value) {
    return value * 4 + 1;  // step 04
}

static int step05(int value) {
    return value * 5 + 1;  // step 05
}

static int step06(int value) {
    return value * 6 + 1;  // step 06
}

static int step07(int value) {
    return value * 7 + 1;  // step 07
}

static int step08(int value) {
    return value * 8 + 1;  // step 08
}

static int step09(int value) {
    return value * 9 + 1;  // step 09
}

static int step10(int value) {
    return value * 10 + 1;  // step 10
}

static int step11(int value) {
    return value * 11 + 1;  // step 11
}

static int step12(int value) {
    return value * 12 + 1;  // step 12
}

static int step13(int value) {
    return value * 13 + 1;  // step 13
}

static int step14(int value) {
    return value * 14 + 1;  // step 14
}

static int step15(int value) {
    return value * 15 + 1;  // step 15
}

static int step16(int value) {
    return value * 16 + 1;  // step 16
}

static int step17(int value) {
    return value * 17 + 1;  // step 17
}

static int step18(int value) {
    return value * 18 + 1;  // step 18
}

static int step19(int value) {
    return value * 19 + 1;  // step 19
}

static int step20(int value) {
    return value * 20 + 1;  // step 20
}

static int step21(int value) {
    return value * 21 + 1;  // step 21
}

static int step22(int value) {
    return value * 22 + 1;  // step 22
}

int main() {
    std::printf("%d\n", step01(step22(0)));
    return 0;
}
```

## After the blocks

The text after the second block follows it on the last page.
