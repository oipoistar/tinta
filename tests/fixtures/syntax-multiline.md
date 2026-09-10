# Syntax state and aliases

A mixed regression document with **bold**, *emphasis*, [a link](syntax-languages.md)
and inline math $x_i^2$. Comments and strings must stop at their real terminators.

| Check | Expected |
| :--- | :--- |
| Separate fences | No leaked string or comment colour |
| URLs | `//` and `#` stay part of values |

> The quote and the table should retain their normal layout.

## SQL strings and nested comments

```SQL
/* outer
   /* nested */ still a comment
*/ SELECT 'first
second ''quoted'' line', $body$literal -- not a comment
/* still a string */$body$, [display name] FROM records;
```

## PowerShell aliases

```ps
<# first <# nested #>
still a comment #> Get-Date
$text = @"
# Not a comment: $env:PATH
"@
Write-Output $text
```

```pwsh
$literal = @'
Don't parse "these" # characters
'@
if ($null -ne $literal) { Write-Host $literal }
```

## Java text blocks

```java
String text = """
    {"message": "Hello", "count": 42}
    // inside the text block
    """;
int count = 0xFF + 1_000 + 2e-3;
```

## PHP heredoc and nowdoc

```php
<?php
$text = <<<TEXT
  # This is a string, including $name.
  TEXT;
$literal = <<<'RAW'
  // These are literal contents.
  RAW;
#[Example]
echo strlen($text);
```

## Markup spanning lines

```htm
<!-- first line
still a comment --> <a
  title="A title
on two lines">Link &amp; text</a>
<script>if (a < b) { run(); }</script><br />
```

```xml
<root><![CDATA[first line
<not-a-tag> & not-an-entity
]]><item count="2" /></root>
```

## CSS comments and strings

```css
/* first line
   still a comment */
.card { --gap: 1.5rem; padding: var(--gap); }
a { background: url("https://example.com/#part"); }
```

## YAML alias, indentation and quoted values

```yml
---
message: >2-
  # block content
  second line
"quoted key": 'one
  two'
next: false # real comment
flow: {url: https://example.com/#part, count: -2.5e+3}
```

## Markdown alias and longer fences

`````md
## Example
````js
``` is shorter than the opener
# This stays code
````
1. Back to Markdown with **emphasis**.
<!-- comment
still a comment --> [link](https://example.com)
`````

## Fresh state in the next fence

```sql
/* deliberately unfinished comment
```

```sql
SELECT 42;
```

```yaml
message: |
  deliberately unfinished scalar
```

```yaml
enabled: true
```

```unknown-language
SELECT 42; /* This whole block stays plain. */
```

- One list item with `inline code`.
- A second with $a+b$.

$$
\int_0^1 x\,dx = \frac{1}{2}
$$

Final syntax fixture paragraph: no syntax state reaches this paragraph.
