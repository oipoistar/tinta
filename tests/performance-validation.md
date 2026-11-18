# Large-document performance investigation

Base: `oipoistar/tinta`, commit `8b9dc7a` (master at checkout).
Local branch: `perf/large-document-hotpaths`.
Measured 2026-11-19 on Windows x64, Ryzen 9 9950X, MSVC 19.50,
Release `/O2`, with the static CRT used by the application.

## Measured results

Final sequential runs, with compilation and tests finished:

| Workload | Original | Optimized | Speedup |
|---|---:|---:|---:|
| Type in 3,000 wrapped source lines | 14.204 ms/key | 0.674 ms/key | 21.1x |
| Type in 10,000 wrapped source lines | 47.037 ms/key | 2.097 ms/key | 22.4x |
| Type in 30,000 wrapped source lines | 141.950 ms/key | 6.371 ms/key | 22.3x |
| Fully lay out 10,000 small aligned tables | 2,288.320 ms | 835.036 ms | 2.74x |
| Fully lay out one 10,000-row aligned table | 1,216.057 ms | 757.992 ms | 1.60x |

Control workloads were effectively unchanged: 10,000 mixed sections
735.186 -> 741.962 ms, 10,000 code lines 246.586 -> 244.907 ms, and a
730 KB paragraph 453.712 -> 457.055 ms. These changes target wrapped
source typing and alignment work, not general parsing or startup.

All 25 workload hashes matched between the original and final binaries.
The final benchmark reported 149 cold-layout comparisons and zero failures.
Raw results: `build/perf-baseline/final-results.csv` and
`build/perf-optimized/final-results.csv`. Original and optimized executables
are preserved in those same directories; the optimized portable application
is `build/perf-optimized/tinta.exe`.

## Changes

### Reuse source wrapping measurements

`rebuildLineStarts` used to ask DirectWrite to lay out every potentially
wrapped source line on every character. The document could contain thousands
of unchanged lines, yet typing in one line measured them all again.

The editor now retains the preceding source text, line starts, and visual
row counts. Exact comparisons identify the unchanged prefix and suffix of
lines, including when an edit inserts or removes newlines. Only lines in
between need new measurements. Width, font, font fallback, and character
width changes invalidate reuse. Switching off source wrapping or leaving
the editor releases the cache.

This deliberately caches small row counts rather than thousands of
DirectWrite layout objects. The tradeoff is one extra UTF-16 source snapshot
plus two `size_t` values per logical line. Rebuilding line indices and
comparing/copying source still takes linear time in the document size.

### Limit alignment updates to affected lines

Every centered or right-aligned table cell used to search all line buckets
already laid out, to discover which hit-test extents needed refreshing.
The CSS alignment path had a second copy of the same full-document scan.
Across many cells this work grew quadratically.

Both paths now share a refresh routine. It finds the vertical extent of
the newly added text, binary-searches the sorted line buckets, and refreshes
only affected buckets within that range. It still accounts for earlier
cells sharing a line, wrapped cells inserting lines out of order, and
row-spanning cells. Per-bucket rectangle indices are appended in order,
so their last index tells whether the bucket contains any new text.

## Reproducing the checks

Build Release with MSVC and CMake, then run:

```powershell
cmake --build build --config Release --target tinta input_tests
ctest --test-dir build -C Release -R performance_regressions --output-on-failure
```

For the full benchmark:

```powershell
cmake -DTEST_BINARY="$PWD/build/Release/input_tests.exe" `
      -DTEST_DIR="$PWD/build/performance-benchmark" -DBENCHMARK=ON `
      -P tests/run_performance_tests.cmake
```

The script copies the binary into an ignored build directory and writes
a portable `settings.ini` beside it, keeping test configuration isolated
from the installed application. On the measurement machine, use the CMake
and CTest bundled with Visual Studio 2026: the older system CMake does not
recognize the Visual Studio 18 generator.

The benchmark generates mixed documents, aligned tables, code, long
paragraphs, and wrapped source buffers. Layout and parse times are the
median of three runs after one warm-up. Typing times are the median of
seven real key-handler calls through `tests/keystroke.h`. Typing measurements
include the synchronous edit/caret work; they do not include the later
debounced preview parse, painting, disk IO, or application startup.

The test emits hashes of rendered text, hit-test rectangles, bucket extents,
and document height. Wrapped-edit hashes cover cumulative row positions.
Coordinates are hashed at 0.01-layout-unit precision. These must agree
between the original and optimized binaries.

## Correctness checks

`performance_regressions` compares incremental wrapping against a cold
DirectWrite measurement in 149 cases: typing, splitting/joining lines,
random replacements, Unicode and tabs, width and zoom changes, themes,
preview toggling, Live/Source switching, empty documents, repeated lines,
and retrying a failed DirectWrite layout instead of caching its fallback.
It also checks line bucket extents against their constituent rectangles.

`tests/fixtures/performance-layout.md` mixes aligned pipe tables, wrapped
cells, an HTML table with row/column spans, headings, lists, quotes,
emphasis, links and code. The native rendering harness produces images
at widths 440 and 1100 in Paper and Midnight. All four were visually
inspected, and their PNG bytes match the original build exactly.

Generated binaries, CSV output and images remain under ignored `build/`.

The complete rebuilt Release suite passed all 88 tests (130.35 seconds).
After adding the failed-layout retry guard and its regression case, the
Release targets were rebuilt again and six affected suites passed:
performance regressions, editor features, wrapped cursor navigation,
CSS/theme layout and export, Live editor, and source highlighting
(18.55 seconds). The final benchmark also runs the 149 wrapping comparisons.
No compiler warnings were emitted by these builds, and `git diff --check`
passes. The final application grew by 2,560 bytes (5,335,552 -> 5,338,112);
no new runtime dependency was added.

## Remaining candidates

- Source editing still scans and copies the entire buffer to rebuild line
  indices. Incremental line indexing would be the next step for very large
  files; it needs a shared edit/invalidation contract across typing,
  multi-caret edits, undo/redo, reloads and tab changes.
- Reading-view paint loops in `src/main_d2d.cpp` iterate over every stored
  primitive before rejecting off-screen items. A spatial/vertical index
  could reduce work while scrolling huge documents. A simple binary search
  over text runs is insufficient because table cells can place runs out
  of vertical order.

These follow-up candidates are code-review findings, not measured speedup
claims. No startup or whole-application speedup is claimed by this change.
