# Inline math wrapping

Before the tall paragraph: this line must remain visible.

Start with ordinary text before $\begin{bmatrix}1&2\\3&4\\5&6\\7&8\\9&0\end{bmatrix}$ and continue with several words, **bold text**, *italic text*, `inline code`, ==highlighted text==, ~~deleted text~~ and [a link](math-compatibility.md). Keep reading this long sentence until it wraps onto several lines in a narrow window, then compare the smaller $\begin{smallmatrix}a&b\\c&d\end{smallmatrix}$ with a nested $\dfrac{1}{1+\dfrac{1}{x^2}}$ and a labelled $A\xrightarrow[n\to\infty]{f}B$ arrow. All baselines and decoration backgrounds should remain aligned.

After the tall paragraph: this line must remain visible.

| Tall cell | Neighboring cell |
| --- | --- |
| $\begin{pmatrix}1\\2\\3\\4\\5\end{pmatrix}$ | This cell contains enough plain text to wrap over multiple lines when the window is narrow. Its text must stay within the row. |
| Following row | The border above this row must not cross the matrix. |

## Following heading $\sqrt[3]{x}$

> A tall inline case $\begin{cases}x^2&x>0\\0&x=0\\-x&x<0\end{cases}$ in a quote with more words to force wrapping near the right edge of the page.

- An item with $\sum\limits_{i=1}^{n}x_i$ and $\underbrace{a+b+c}_{\text{sum}}$ should remain distinct from its neighbor.
- The last item remains readable.

End of the inline stress fixture.
