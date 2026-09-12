# Large images and cache eviction

This regression document combines **bold**, *emphasis*, a [link](https://example.com),
`inline code` and math $a^2+b^2=c^2$ around images.

![Small PNG](image-cache-assets/small.png)

![Large JPEG](image-cache-assets/large.jpg)

## Images inside a table

| Preview | Description |
| --- | --- |
| ![Second large JPEG](image-cache-assets/large-second.jpg) | Table measurement must keep images alive. |
| ![Small PNG repeated](image-cache-assets/small.png) | Reusing a source after eviction must remain safe. |

> A quote with **formatting**, $x+1$, and an image:
>
> ![Transparent PNG](image-cache-assets/alpha.png)

## Resolution and failure cases

![High DPI PNG](image-cache-assets/dpi.png)

![Wide panorama](image-cache-assets/wide.png)

![Broken image placeholder](image-cache-assets/broken.jpg)

![Oversized image placeholder](image-cache-assets/oversized.bmp)

![Missing image placeholder](image-cache-assets/missing.png)

- A list after all images.
- The original files must remain unchanged.

```cpp
// Code highlighting must survive image loading.
const auto pixels = width * height;
```

$$
\int_0^1 x^2\,dx = \frac{1}{3}
$$

## Final heading

All surrounding content remains visible and searchable.
