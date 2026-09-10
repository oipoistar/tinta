# Syntax highlighting: nine new languages

Issue [#207](https://github.com/oipoistar/tinta/issues/207), reported by **@hochun836**.
Each example should show distinct syntax colours in Paper and Midnight. Resize the
window and open Contents or the file browser; long lines should remain scrollable.

| Family | Fence labels | Check |
| :--- | :--- | :--- |
| Data | `sql`, `yaml`, `yml` | Keys, numbers, comments |
| Programs | `powershell`, `ps`, `pwsh`, `java`, `php` | Calls, types, control flow |
| Documents | `html`, `htm`, `xml`, `css`, `markdown`, `md` | Tags, attributes, styles, markup |

> A quote with `inline code`, **bold text**, *emphasis* and $a^2+b^2=c^2$.

## SQL

```sql
-- Case-insensitive keywords and doubled quotes
SELECT employee_id, COUNT(*) AS total FROM employees
WHERE active = 1 AND name = 'O''Brien' GROUP BY employee_id;
CREATE TABLE sample (id INTEGER, price DECIMAL(10, 2));
```

## PowerShell

```powershell
# Cmdlets, parameters, variables and backtick escapes
[string] $message = "Hello `$reader"
Get-ChildItem -Path . | Where-Object { $_.Extension -eq '.md' }
if ($true) { Write-Output $message }
```

## Java

```java
@Override
public String greet(String name) {
    // Calls and control flow use existing theme keys
    if (name == null) return "Hello";
    return name.trim() + 42;
}
```

## PHP

```php
<?php
function greet(string $name): string {
    # Variables, types, strings, numbers and calls
    return strtoupper($name) . " visitor " . 42;
}
?>
```

## HTML

```html
<!-- Tags and attributes, with an entity in text -->
<article class="card" data-count="2">
  <a href="https://example.com/#intro">Hello &amp; welcome</a>
</article>
```

## XML

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!-- Namespace-qualified names -->
<doc:item xmlns:doc="urn:tinta" id="7">
  <![CDATA[<literal> & untouched]]>
</doc:item>
```

## CSS

```css
/* Selectors, properties, dimensions, colours and functions */
@media (min-width: 40rem) {
  .card:hover { color: #c06040; padding: calc(1rem + 2px); }
  #hero { background: url(https://example.com/img.svg#icon); }
}
```

## YAML

```yaml
# Keys, booleans, numbers and a URL with a fragment
defaults: &defaults {enabled: true, retries: 3}
site: https://example.com/#intro
message: |-
  # This is text, not a comment.
  Keep these lines together.
copy: *defaults
```

## Markdown inside Markdown

````markdown
# A heading in a code block
**Bold** and *emphasis*, with `inline code`.
- A [link](https://example.com/path_(one))
> A quoted line
```sql
SELECT 1; -- An inner fenced example
```
<!-- A Markdown comment -->
````

### Content after the examples

1. Headings, tables, quotes and lists retain their spacing.
2. Copy a code block and check the original indentation and newlines.
3. Search for `employee_id` and `Keep these lines together`.

$$
\sum_{k=1}^{n} k = \frac{n(n+1)}{2}
$$

Final syntax fixture paragraph: **all surrounding content remains visible**.
