---
title: Proxmox Setup
author: Jane Doe
tags: [proxmox, homelab, tailscale, servers, backups, networking, notes]
created: '2026-01-03T09:30:00Z' # Keep this original date
updated: '2026-09-08T18:00:00Z' # Refresh this on an explicit save when shown
draft: false
status: Reviewing
project: Tinta
language: English
priority: 2
aliases:
  - Home server
  - Lab notes
custom field: 'A custom key with spaces'
settings:
  title: Nested text must not replace the top-level title
  updated: This nested value must stay untouched
---

# Frontmatter display and save checks

Open **Settings → Frontmatter**. Show `author`, `created`, and `updated`.
Drag the rows, change their side, toggle labels, and try the list formats.
Limit tags to three and hover over the **+4** chip to see the remainder.

The property strip should fit at narrow widths as well as in a wide window.
Its title is independent of the Markdown heading below it.

## Mixed content remains intact

| Feature | Example |
| :--- | :--- |
| Emphasis | **Bold**, *italic*, ==highlight== |
| Math | $a^2+b^2=c^2$ and $\frac{1}{2}$ |
| Code | `updated: not metadata here` |
| Footnote | A repeated note[^detail] |

> A quote with inline code `created`, **bold text**, and $x_i^2$.

- First list item with [a link](https://example.com).
- Another reference[^detail] and H~2~O / x^2^.

```yaml
# Literal example: these fields are never rewritten.
created: literal
updated: literal
```

$$
\sum_{k=1}^{n} k = \frac{n(n+1)}{2}
$$

Final body paragraph.

[^detail]: A note with **formatting**, ==highlight== and $E=mc^2$.

    A second paragraph verifies footnotes and frontmatter together.
