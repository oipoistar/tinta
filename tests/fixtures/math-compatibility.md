# Native math compatibility samples

## Issue 190

$$
\begin{bmatrix}1&2\\5&6\end{bmatrix}\longrightarrow 6,
\qquad
\begin{bmatrix}3&4\\7&8\end{bmatrix}\longrightarrow 8
$$

## Matrix variants and nesting

$$\begin{pmatrix}\frac{1}{x^2}&\sqrt{y}\\z&\begin{bmatrix}1\\2\end{bmatrix}\end{pmatrix}$$

$$\begin{Bmatrix}a&b\\c&d\end{Bmatrix}\quad\begin{vmatrix}a&b\\c&d\end{vmatrix}\quad\begin{Vmatrix}a&b\\c&d\end{Vmatrix}$$

$$\begin{bmatrix*}[r]1&22\\333&4\end{bmatrix*}$$

## Piecewise functions and derivations

$$f(x)=\begin{cases}x^2&\text{if }x>0\\0&\text{if }x=0\\-x&\text{otherwise}\end{cases}$$

$$\begin{aligned}a+b&=c\\x&=y+z\end{aligned}$$

$$\begin{array}{r|l}\hline 1&22\\\hline 333&4\\\hline\end{array}$$

## Calculus and combinatorics

$$\sum_{i=1}^n x_i\quad\int_0^1 f(x)\,dx\quad\lim_{n\to\infty}\frac{1}{n}=0$$

$$\sum_{\substack{i>0\\j>0}}a_{ij}\quad\dfrac{1}{2}+\tfrac{1}{2}\quad\binom{n}{k}\quad\sqrt[3]{\frac{x+1}{y}}$$

## Text and annotations

$$\text{for all }x\in\mathbb{R},\quad\mathcal{L}=\mathfrak{g}+\boldsymbol{\alpha}$$

$$A\xrightarrow[n\to\infty]{f}B\quad\overset{!}{=}\quad\underbrace{a+b+c}_{\text{sum}}$$

$$\widetilde{xyz}+\dot{x}+\ddot{x}+\underline{y}+\boxed{x=2}+\cancel{x}$$

## Custom notation

$$
\newcommand{\norm}[1]{\left\lVert#1\right\rVert}
\DeclareMathOperator*{\argmin}{arg min}
\argmin_x\norm{x-b}^2
$$

## Inline flow

An inline small matrix $\begin{smallmatrix}a&b\\c&d\end{smallmatrix}$ stays aligned with this sentence. Surrounding text should remain readable when this paragraph wraps onto another line in a narrow window.

A taller inline matrix $\begin{bmatrix}1&2\\3&4\\5&6\end{bmatrix}$ reserves extra space above and below the surrounding text. The preceding and following lines should not overlap its entries or brackets.
