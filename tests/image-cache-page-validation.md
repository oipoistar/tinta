# A page with more pictures than the cache holds

Validated on Windows with the Release build on 2026-11-10.

## What was found

Found in a review of the code; nobody reported it. The image cache holds
64 MiB of decoded pictures or 256 of them, and puts the one used longest
ago out to make room. It did not know which pictures the page in view
shows. A page that needs more than the cache holds (three large
screenshots, two photos from a phone, a list with hundreds of badges)
had each picture put the next one out as the page was laid out:

- pictures from files were decoded again, all of them, at every pass: on
  each step of a window resize, a zoom, a theme change;
- pictures from the web were fetched and decoded again without end, a
  layout pass and a repaint each time, with the window idle.

## Change

A layout pass has a number (`imagePass` in `app.h`), and a picture it
asks for carries it. The pictures of the pass in progress and of the last
one finished are the page's: they stay in the cache whatever the budget.
Others go as before. The cache is over its budget for as long as one page
needs more, and comes back under it when another page has been laid out.

## Fixture

`fixtures/image-cache-big.md`: a heading, a paragraph, and three
pictures, one in a paragraph, one in a table cell and one in a quote,
with a list and a code block between. The pictures are too large to keep
in the repository; the test writes them, and the page says where to put
your own.

## Automated checks

**`image_cache_and_decode`** (`image_tests.cpp`), in `pageKeepsItsImages`,
with three pictures of 3000 x 2400 written by the test:

- the page is laid out with its three pictures and its text; together
  they are more than the budget; the cache holds all three;
- after another pass at another width the page shows the same three
  bitmaps, the same objects: none was decoded again;
- with another page laid out, a picture stored over the budget lets the
  first page's three go and keeps the new page's own.

The checks the test already had on eviction pass unchanged: pictures
stored outside a layout pass go by the budget and the count as before.

Without the change 4 checks fail, and the test takes 7.3 s where it
takes 1.8 s: the difference is the decoding.

The full suite, without the three tests that use the clipboard
(`notification_setting`, `copy_file_path`, `code_block_layout`), passes:
64 of 64.

## Size

`tinta.exe` grows by 1,024 bytes with this change and the next, to
4,031,488.

## Limitations

- Pictures from the web were not run: the test's own web part needs a
  local server and was not set up in this session. The loop there had
  the same cause, one picture putting out another the page shows, and
  the same cache keeps both now.
- A page of very many large pictures holds them all decoded. Decoding
  each at the size it is shown would bound that; not done here.
- Not driven in the built window; the screen could not be copied in this
  session.
