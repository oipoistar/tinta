# Equations with numbers

Text before the equations, with **bold**, *italic*, `code` and a
[link](#equations-with-numbers).

$$
[\![-p + 2\mu\,\partial_n u_n]\!] + [\![\mathbf n\cdot\mathbf T_E\cdot\mathbf n]\!] = \gamma\kappa,
\qquad
[\![\mathbf n\cdot\mathbf T_E\cdot\mathbf n]\!] = \tfrac12\left[\!\left[\varepsilon\left(E_n^2 - E_t^2\right)\right]\!\right].
\tag{1.8}
\label{eq:jump}
$$

Two in a row, one paragraph:

$$
U_c = \sqrt{\frac{\gamma}{\rho R}},\quad t_c = \sqrt{\frac{\rho R^3}{\gamma}},\quad p_c = \frac{\gamma}{R},\quad
E_c = \sqrt{\frac{\gamma}{\varepsilon_0 R}},\quad \phi_c = E_cR,
$$
$$
\rho_{e,c} = \frac{\varepsilon_0E_c}{R},\quad q_c = \varepsilon_0E_c,\quad j_c = K_lE_c.
\tag{2.1}
\label{eq:scales}
$$

The jump condition $\eqref{eq:jump}$ and the scales $\eqref{eq:scales}$ are
numbered; $\eqref{eq:missing}$ is not.

A paragraph with words
$$x^2 + y^2 = r^2 \tag*{circle}$$
and words after the equation.

Accents sit on their letters: $\tilde\phi = \sqrt{B_E}$, $\hat{x}$, $\bar{x}$,
$\vec{v}$, $\dot{x}$, $\tilde{A}$ and $\widetilde{xy}$.

Signs bind only between two terms: $[-p]$, $(-x)$, $\{-1\}$, $f(x, -y)$,
$a - b$ and $x = -y$.

- A list item with $\tilde{x}$ and $$\unknowncommand{x}$$, which falls back.
- An inline equation keeps its number beside it: $E = mc^2 \tag{3}$

> A quote with $\varepsilon\left(E_n^2 - E_t^2\right)$ inside.

| Equation | Number |
| --- | --- |
| $a^2 + b^2 = c^2$ | $\ref{eq:scales}$ |

```text
A code block keeps $$x \tag{9}$$ as text.
```

The last paragraph, with **bold** and *italic* words.
