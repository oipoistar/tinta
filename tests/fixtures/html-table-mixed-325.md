# HTML tables among Markdown (#325)

A table written in HTML is drawn as a table: the same grid, header and
bands as a pipe table, with *inline* content in its cells.

## A plain table

<table>
<tr><th>Action</th><th>Key</th></tr>
<tr><td>Open a file</td><td>Ctrl + O</td></tr>
<tr><td>Find in the page</td><td>Ctrl + F</td></tr>
</table>

The paragraph after the plain table. The same table as a pipe table:

| Action           | Key      |
|------------------|----------|
| Open a file      | Ctrl + O |
| Find in the page | Ctrl + F |

## Head, body and foot

<table>
  <caption>Sales by <em>quarter</em></caption>
  <thead>
    <tr><th>Quarter</th><th align="right">Units</th><th align="center">Trend</th></tr>
  </thead>
  <tbody>
    <tr><td>First</td><td align="right">1,204</td><td align="center">up</td></tr>
    <tr><td>Second</td><td align="right">980</td><td align="center">down</td></tr>
  </tbody>
  <tfoot>
    <tr><th>Total</th><td align="right">2,184</td><td></td></tr>
  </tfoot>
</table>

## Spans

Columns and rows joined, a header over two columns and a corner cell over
two rows and two columns:

<table>
  <tr><th rowspan="2">Region</th><th colspan="2">First half</th><th colspan="2">Second half</th></tr>
  <tr><th>Q1</th><th>Q2</th><th>Q3</th><th>Q4</th></tr>
  <tr><td>North</td><td>12</td><td>15</td><td colspan="2" rowspan="2">no data for the second half of the year in the north or the south</td></tr>
  <tr><td>South</td><td>9</td><td>11</td></tr>
  <tr><td>East</td><td colspan="4" align="center">closed</td></tr>
  <tr><td rowspan="3">West</td><td>1</td><td>2</td><td>3</td><td>4</td></tr>
  <tr><td>5</td><td>6</td><td>7</td><td>8</td></tr>
  <tr><td colspan="3">a note under three columns</td><td>12</td></tr>
</table>

## Inline content in cells

<table>
<tr><th>Kind</th><th>Shown as</th></tr>
<tr><td>Bold and italic</td><td><b>bold</b>, <strong>strong</strong>, <i>italic</i> and <em>emphasis</em></td></tr>
<tr><td>Code</td><td><code>tinta.exe --printpages</code> in a cell</td></tr>
<tr><td>A link</td><td><a href="https://github.com/oipoistar/tinta">the repository</a> and text after it</td></tr>
<tr><td>Line breaks</td><td>first line<br>second line<br/>third line</td></tr>
<tr><td>Marks</td><td>H<sub>2</sub>O, x<sup>2</sup>, <del>gone</del>, <ins>added</ins>, <mark>marked</mark></td></tr>
<tr><td>Entities</td><td>5 &lt; 7 &amp;&amp; 7 &gt; 5, a&nbsp;b, &quot;quoted&quot;, &#8364; &#x2713;</td></tr>
<tr><td>Blocks</td><td><p>a paragraph</p><p>another</p><ul><li>one</li><li>two</li></ul></td></tr>
<tr><td>A long cell</td><td>A sentence long enough to wrap inside its cell when the window is narrow, so the row grows with it and the rows after it move down.</td></tr>
</table>

## End tags left out

Rows with no `</tr>`, cells with no `</td>`, and a table with no
`</table>` before the blank line:

<table>
<tr><th>Name<th>Since
<tr><td>Open<td>1.0
<tr><td>Find<td>2.3
<tr><td>Export<td>3.1

The paragraph after the unclosed table.

Rows apart, a blank line between them:

<table>
<tr><th>Step</th><th>What</th></tr>

<tr><td>1</td><td>Open</td></tr>

<tr><td>2</td><td>Read</td></tr>
</table>

## Among other blocks

1. A list item with a table in it:

   <table>
   <tr><th>In</th><th>a list</th></tr>
   <tr><td>one</td><td>two</td></tr>
   </table>

2. The item after it.

> A quote with a table:
>
> <table>
> <tr><td rowspan="2">quoted</td><td>first</td></tr>
> <tr><td>second</td></tr>
> </table>
>
> And the quote's last line.

```html
<table>
  <tr><td>inside a code block: not a table</td></tr>
</table>
```

<div align="center">
  A centered division whose lines
  are indented in the source,
  <b>bold</b> at its end.
</div>

<p>
  A paragraph in HTML
  over three indented
  lines.
</p>

<details>
<summary>More on tables</summary>
The text under the summary stands
on a line of its own.
</details>

<div>First division</div>
<div>Second division</div>

Uneven rows, the second one short and the third one long:

<table>
<tr><td>a</td><td>b</td><td>c</td></tr>
<tr><td>d</td></tr>
<tr><td>e</td><td>f</td><td>g</td><td>h</td></tr>
</table>

A table within a cell is read as lines of that cell:

<table>
<tr><td>outer</td><td><table><tr><td>inner one</td><td>inner two</td></tr><tr><td>inner three</td></tr></table></td></tr>
<tr><td>last</td><td>row</td></tr>
</table>

The last paragraph, after every table.
