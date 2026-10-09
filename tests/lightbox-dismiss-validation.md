# Lightbox dismissal (#249)

Validated on Windows with the Release build on 2026-10-08.

## Problem

A press outside the zoomed image closed the lightbox, but its release was not
consumed. It fell through to the page's click handling, which opened the
lightbox again whenever an inline image lay under the pointer. Clicks on the
zoomed image or below it seemed fine only because no inline image sat there.
Reproduced on 3.7.4: with the zoomed view open, a click above-left of the image
over the inline image re-opened it, both right away and after the image was
dragged aside.

## Change

- The press that dismisses the lightbox sets `swallowNextMouseUp`, the same
  hand-off the tab strip and signal chips use, so its release ends there.
- A plain click on the zoomed image still closes it on release, and drags and
  the wheel still pan and zoom.

## Fixtures and automated checks

`fixtures/lightbox-dismiss-249.md` is a mixed document with a heading, a
paragraph, an inline SVG swatch, a table, lists, a quote, code and CJK text. It
also lists the expected gestures for manual checks.

Run from the repository root:

```powershell
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

- `image_cache_and_decode` gains a lightbox check on the mixed image fixture
  through the real mouse handlers. A click on an inline image opens the
  lightbox; with the zoomed image dragged aside, a click over the inline image
  closes it and it stays closed; the swallowed release is consumed exactly once;
  the next click opens it again; a click on the zoomed image still closes it.
- With the fix removed, the check fails as expected: "a click beside the zoomed
  image closes it for good".

## Desktop check

The built application ran in portable mode from a scratch folder with the
fixture, in Paper at 1000 x 800 and 96 DPI. Input was posted window messages and
the window was captured with `PrintWindow`.

- The inline swatch sat at (40, 246)-(519, 485) and the zoomed image at
  (252, 296)-(731, 535). A click at (46, 252), over the inline swatch beside the
  zoomed image, re-opened the lightbox on 3.7.4 and closed it with the fix.
- After dragging the zoomed image 300 pixels to the right, a click beside it
  behaved the same: re-opened on 3.7.4, closed with the fix.

## Exports

The fixture rendered to HTML, DOCX, PDF and print pages in Paper and Midnight
with the 3.7.4 and the fixed build. HTML, every print page and every DOCX member
were identical, and all PDFs were valid. One first run of the 3.7.4 build drew
three anti-aliased pixels differently on page 1; two further runs of each build
were identical to the fixed output. Outputs are under the ignored
`out/open-issue-fixes/exports/` directory.

## Limits

- Physical mouse input was not used; the native check and the live run both
  deliver real handler input.
- The Microsoft Store build was not tested; it runs the same input code.
- The request in the same issue for an option to stop keeping the history of
  opened files is a feature request and is not part of this fix.

## Draft reply (not posted)

### #249

Thanks for the precise description, it was a bug. Clicking beside the zoomed
image closed it as the mouse button went down, but the release then reached the
page underneath, and when an inline image was there it opened again. Clicks on
the image or below it worked because nothing was underneath. In the next
release a click anywhere outside the zoomed image closes it, also after zooming
or dragging. Your second suggestion, an option to stop keeping the history of
opened files, is a separate request and is not part of this fix.
