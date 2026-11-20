# Startup to first frame

Measured on 2026-11-19, Windows 11 Pro build 26300, Ryzen 9 9950X,
MSVC 14.50 / Visual Studio 2026, x64 Release.

Control: `948c1a4` (the same source tree as `c71a5e5`, after PR #328).
This isolates the startup changes from the earlier editor/table improvements.

## Results

Median milliseconds from Windows process creation to the end of the first
visible render, 12 fresh processes per case and build, 144 shuffled launches:

| Document | Control | Changed | Reduction |
| --- | ---: | ---: | ---: |
| Start page | 62.1 | 53.7 | 13.5% |
| Small mixed Markdown | 63.6 | 54.3 | 14.6% |
| Repository README, remote images removed | 73.2 | 64.8 | 11.4% |
| 10,000 paragraphs, about 920 KB | 146.6 | 145.9 | 0.5% |
| One aligned table with 4,000 rows | 275.0 | 263.7 | 4.1% |
| One highlighted C++ block with 10,000 lines | 454.7 | 224.3 | 50.7% |

These are warm-filesystem, fresh-process results on one machine, not cold-boot
measurements. Timing includes loader/CRT work before WinMain and stops after
Direct2D EndDraw, before desktop composition or physical screen scanout. No
build or test suite ran concurrently with these measurements. Portable settings
fix a 1000 x 800 window, Paper theme, reading mode, 100% zoom and reading width;
updates, session restoration and first-run association prompts are disabled.

## Changes and evidence

- Overlap software rasterizer initialization with settings, window and font
  setup. A temporary worker owns its own COM apartment, Direct2D factory and
  software DC target, then releases them and joins before the app creates its
  real target. Nothing from the app's single-threaded factory crosses threads.
  On the small-file case, real target creation falls from 16.3 to 2.9 ms;
  worker joining and setup still cost time, so the net first-frame saving is
  smaller. Existing-window handoffs skip the eager worker, thread-creation
  failure falls back to normal initialization, and early returns join safely.
- Delay-load URLMon, WinHTTP, common dialogs and D3D11. None is needed for the
  first software frame in these cases. The measured loader portion falls from
  14.9 to 14.0 ms on the small file. D3D11 is still initialized by the existing
  GPU warm-up after the first paint. `dumpbin /imports` confirms all four
  libraries are in the delay-load table.
- Reuse immutable DirectWrite layouts for repeated code fragments within each
  code block. Keywords, punctuation, indentation and repeated comments otherwise
  receive identical layouts thousands of times. Colour, position and selection
  ranges stay on separate runs/rows. Each run owns a COM reference; the local
  cache holds at most 256 strings of at most 128 UTF-16 code units, and a new
  block/font/theme/zoom/target naturally starts fresh. The 10,000-line example's
  first layout falls from 388.2 to 167.3 ms. It still lays out every line.

The worker follows Microsoft's [Direct2D threading model](https://learn.microsoft.com/en-us/windows/win32/direct2d/multi-threaded-direct2d-apps).
The import settings use MSVC's [delay-load support](https://learn.microsoft.com/en-us/cpp/build/reference/linker-support-for-delay-loaded-dlls?view=msvc-170).
The normal executable grows by 6,656 bytes, from 5,338,112 to 5,344,768 bytes.
Profiling code is injected only into separate build copies, not the application.

## Correctness checks

- All 144 timed launches matched their control case's document/hit-rectangle
  hash (geometry rounded to 0.01 DIP) and text-run count.
- `tests/fixtures/startup-code.md` combines highlighted C++/Python, repeated
  tokens with different colours, long wrapping lines, Unicode, headings, links,
  inline code, a quote, a list and a table. First-frame retained-scene PNGs are
  byte-identical between builds at window widths 440 and 1100, with themes 0
  and 5. All four were visually inspected. These use the native frame-preview
  renderer after timing, not a screenshot of the desktop compositor.
- `code_wrap` checks sharing, separate ownership across blocks, cache/key
  limits, syntax colours, complete source rows, narrow wrapping, theme changes,
  target recreation, native print pages, HTML and DOCX export. Existing tests
  also cover selection, nested code, tabs, zoom and Unicode wrapping.
- Rebuilt all Release targets and ran all 88 CTest cases. After correcting the
  new test to use highlighted runs' line-based source positions, 87 pass.
  `sheet_canvas` fails its clipboard assertion, "the sheet copies as a Mermaid
  flowchart", on both this build and the saved, unchanged `948c1a4` test binary
  in a fresh test directory. This existing failure remains unresolved.
- The real, uninstrumented app opened and closed normally for the start page,
  a document, `--new`, `--edit`, saved source/live modes, and saved zoom with a
  narrow window. HTML/DOCX CLI export early returns succeeded. A second process
  forwarded its file to an existing window and exited successfully; opening a
  separate window while another window existed also succeeded. These checks
  allowed the real message loop and GPU warm-up to run before closing.

Local evidence is under ignored directories `build/startup-comparison`,
`build/startup-frame-{theme}-{width}`, `build/code-wrap-test-Release`,
`build/startup-smoke` and `build/startup` (build/test logs).

## Reproduce

Use Python 3.9+ and a current CMake with the installed Visual Studio generator.
Run from the repository root; choose unused profiling output directories.

```powershell
python tools/instrument_startup.py --revision 948c1a4 --work-dir build/startup-control
python tools/instrument_startup.py --work-dir build/startup-final
foreach ($variant in @('control', 'final')) {
    cmake -S "build/startup-$variant/source" -B "build/startup-$variant/compile" `
        -G 'Visual Studio 18 2026' -A x64 -DBUILD_TESTING=OFF
    cmake --build "build/startup-$variant/compile" --config Release --target tinta
}
python tools/benchmark_startup.py --work-dir build/startup-comparison `
    --binary baseline=build/startup-control/compile/Release/tinta.exe `
    --binary optimized=build/startup-final/compile/Release/tinta.exe --runs 12
```

`--cases fixture --frames --width 440 --theme 5 --runs 1` generates a narrow
dark comparison; use a separate `--work-dir` for each size/theme. The runner
writes per-launch CSVs plus `summary.json`, and rejects different geometry
hashes or run counts across builds. The profiling binaries intentionally exit
after the first frame and must not be distributed as the application.

## Further opportunities

The larger remaining opportunity is layout *within* a single large block.
Viewport-first layout currently stops between blocks: one huge code fence or
table still monopolizes the first layout. The table example spends about
200 ms there even after these changes. Making code/table layout resumable by
row could paint the visible portion much sooner, but must preserve syntax
state, wrapping, table column widths, selection, scrolling and export.

Parsing the entire file also precedes the first paint. On the long-paragraph
case, reading/parsing and related file setup take about 75–83 ms, while the
viewport layout takes about 10 ms. This change does not materially improve
that case; moving parsing off-thread or supporting partial parse results
would need a separate design and validation effort.
