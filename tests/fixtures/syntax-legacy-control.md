# Existing syntax control

**Bold**, *emphasis*, [link](syntax-languages.md), `inline code` and $x^2$.

| Existing feature | Expected |
| :--- | :--- |
| All seven language families | Same colours and spacing as before #207 |

> A quote between the table and the code.

```cpp
/* across
lines */ int sum(int a, int b) { return a + b; }
```

```python
# Existing Python rules
def greet(name):
    return "Hello " + name + str(42)
```

```typescript
const count = 3;
function greet(name: string) { return `Hello ${name}`; }
```

```rust
fn main() { let n: i32 = 42; println!("Hello {}", n); }
```

```go
package main
func main() { fmt.Println("Hello", 42) }
```

```bash
# Existing shell rules
if test -f sample.md; then echo "Hello"; fi
```

```c#
public string Greet(string name) {
    if (name == null) return @"Hello";
    return $"Hello {name}";
}
```

```json
{"enabled": true, "count": 42, "name": "Hello"}
```

```unknown-language
SELECT 42; # Unrecognised fences remain entirely plain.
```

1. First item with **bold**.
2. Second item with *emphasis*.

$$
\frac{a}{b} + c
$$

Final syntax fixture paragraph: existing rendering is unchanged.
