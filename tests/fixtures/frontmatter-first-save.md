---
# No date fields yet: showing created/updated enables insertion on save.
title: First save with frontmatter
author: 'Jane #1' # Preserve quoted hashes and comments
tags:
  - notes
  - "comma, inside one item"
draft: true
description: |
  updated: This is literal content within a block scalar.
  created: This is literal too.
---

# First observation

Opening this document must not modify its bytes. When the date rows are shown,
an explicit save adds the missing dates before the closing frontmatter marker.
Later saves update `updated` while preserving the original `created` value.

| State | Expected behavior |
| --- | --- |
| Shown dates | Maintained on save |
| Hidden dates | Left alone |

> A quote with **bold**, *italic*, ==highlight== and $a+b=c$.

- A list item with `inline code`.
- A footnote[^note].

```markdown
---
updated: This fenced example is not frontmatter.
---
```

[^note]: This note remains after the body, with a text return link.
