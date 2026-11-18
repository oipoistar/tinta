# Shell and Python slashes

**Bold**, *emphasis*, [link](syntax-legacy-control.md) and `inline code`.

| Fence | Expected |
| :--- | :--- |
| `bash` | URLs, paths and globs keep their colours; only `#` starts a comment |
| `python` | `//` is floor division, not a comment |

> In both fences everything after `//` or `/*` should stay coloured as code, not as a comment.

```bash
# Is the metadata service reachable?
curl -s -m 3 http://169.254.169.254/latest/meta-data/iam/info || echo "no IMDSv1"
ls /opt/* | wc -l
echo "still code, not a comment" # a real comment
```

```python
half = total // 2  # floor division, then a real comment
pattern = "/* not a comment */"
```

```cpp
int x = 1; // C++ still comments with slashes
/* and with blocks */ return x;
```

Final paragraph after the fences.
