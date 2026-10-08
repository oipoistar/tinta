# Lightbox dismissal (#249)

Click the swatch to view it full size. Then click **beside** the zoomed image:
above it, left or right of it, after zooming with the wheel or dragging it
aside. The view must close and stay closed. Before the fix, the release reached
the page and opened the inline image again whenever the click landed over it.

![Magenta swatch](lightbox-249/swatch.svg)

| Gesture in the zoomed view | Expected |
| --- | --- |
| Click beside the image | Closes and stays closed |
| Click on the image | Closes |
| Drag the image | Pans, stays open |
| Wheel | Zooms, stays open |

- Esc and Enter still close the view
- A *plain* click on the inline swatch opens it again
- Text selection and [links](#lightbox-dismissal-249) on the page keep working

> Surrounding quote: the page underneath is unchanged.

```cpp
// Surrounding code block
int main() { return 0; }
```

Final paragraph with CJK text: 点击放大图片旁边的区域应关闭预览，且不再重新打开。
