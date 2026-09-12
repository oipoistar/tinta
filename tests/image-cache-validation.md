# Issue #220: image lifetime and decoding validation

Validated on Windows on 2026-09-12, with MSVC 14.44.35207, against
[issue #220](https://github.com/oipoistar/tinta/issues/220) reported by @glance02.
Validation includes the preceding compiler-warning cleanup.

## Fix

- Cache entries and document layout each own a COM reference to their bitmap.
  Eviction, replacement, table-measurement rollback, layout copies, clearing the
  cache, and opening an evicted image in the lightbox no longer invalidate a
  bitmap still needed by a drawing. Render-target recreation clears the old
  layout and lightbox before loading images on the replacement target.
- The cache counts allocated pixels instead of DPI-adjusted display dimensions.
- Local and downloaded raster images use the same WIC decoder. It scales before
  allocating the premultiplied BGRA buffer, preserves intrinsic display size,
  and respects the render target's bitmap size limit. The display copy is at
  most 16,777,216 pixels (64 MiB), with neither dimension above 8,192 pixels.
- Sources above 100,000,000 pixels or 65,535 pixels on either axis are rejected
  before pixel decoding. Bad files, WIC failures and pixel allocation failures
  return to the existing alt-text placeholder. Worker startup/decode exceptions
  cannot escape the remote-image worker.
- HTML and DOCX still embed the original image files. SVG rasterization retains
  its existing behavior, with the same corrected bitmap ownership/accounting.

The 64 MiB LRU budget bounds the cache, not the whole document: displayed images
retain references until their layout is released. Large raster images lose some
zoom detail in the bounded display copy. WIC codecs can also use internal working
memory beyond the output pixel buffer; the source-dimension guard bounds input
size rather than promising a strict whole-process memory ceiling.

WIC scaling reference: [Microsoft's bitmap-source scaling example](https://learn.microsoft.com/en-us/windows/win32/wic/-wic-bitmapsources-howto-scale).

## Reproduction and automated checks

The published v3.7.0 binary crashed with `0xC0000005` in `d2d1.dll` when a
6000 x 4000 JPEG appeared alongside a small image, in either order. A flat JPEG
at the same dimensions also crashed despite being only 375,629 bytes. The noisy
JPEG is 11,354,106 bytes. Each JPEG expands to 96,000,000 BGRA bytes without
downsampling. Single-image controls succeeded. Original reproduction artifacts
are retained under `out/issue-220/`.

```powershell
cmake --build build --config Release --parallel 4
ctest --test-dir build -C Release --output-on-failure
python tests/validate_large_images.py
```

- All application/test targets built with zero compiler warnings and errors.
- **21/21 CTests passed**, including the new `image_cache_and_decode` test.
- Native tests explicitly force both byte-budget and entry-count eviction,
  replacement, layout rollback/copy, cache release, lightbox ownership and
  render-target recreation. They check oversized/corrupt/missing data, alpha
  premultiplication, DPI accounting, panorama sizing and integer-overflow guards.
- Mixed layout runs at widths 1050 and 650, themes Paper and Midnight, scales 1
  and 1.5, using both incremental and complete layout. Images in a table and
  quote, final headings, code blocks, math and failure placeholders survive.
- Seven generated image-order/failure cases all render to native print pages;
  the previously crashing cases finish in 0.25–0.44 seconds on this machine.
- The mixed document produces three native pages, HTML, DOCX and PDF. HTML and
  DOCX are checked for byte-for-byte preservation of the original JPEG/PNG
  assets; source Markdown and images are unchanged after all exports.
- A localhost HTTP server exercises real asynchronous download, decoding,
  completion, failure placeholders and relayout with the 11 MB JPEG.
- The small-image control page is byte-identical to published v3.7.0 when both
  binaries use the same isolated settings.

Native CTest generates its own assets with WIC and needs no Python packages.
The broader validation script requires Pillow and NumPy and generates all
images under ignored `out/issue-220/fixed/`. It copies the runnable fixture from
`tests/fixtures/image-cache-large.md` beside those assets. No large images need
to be checked into Git. Executables use isolated portable settings and child
crash-dump directories.

## Visual checks and review files

Computer Use verified the real app in Paper at 1050 x 900 and Midnight at
650 x 900: inline images, large-image lightbox and Escape dismissal, scrolling,
the narrow table containing two images, quote transparency, high-DPI sizing,
failure placeholders, highlighted code, math and the final paragraph. Native
print pages were also visually inspected. The file-dialog helper initially
failed to target its filename field, so the fixture was opened through an
isolated saved session instead.

- Ready-to-open fixture: `out/issue-220/fixed/image-cache-large.md`
- Structured results: `out/issue-220/fixed/results.json`
- Build log: `out/issue-220/build-final.log`
- CTest log: `out/issue-220/ctest-final.log`
- Export/download log: `out/issue-220/validation.log`
- Unposted reporter reply: `out/issue-220/reply-draft.md`
- Review executables refreshed: `build-meta/tinta.exe` and
  `build-meta/Release/tinta.exe`, identical to `build/Release/tinta.exe`.

Validated executable: 2,543,616 bytes, SHA-256
`A4FB56784C733E35D09BF3C22E1E860568D30C59F01AEEB5F8E7B1F1606CB5C7`.

The reporter's original JPEG was not attached, so its exact case remains
unverified. The independently reproduced cache lifetime crash is fixed; a
forced whole-process out-of-memory condition was not tested.
