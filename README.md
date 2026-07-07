<div align="center">
  <img src="resources/tinta.ico" width="80">
  <h1>Tinta</h1>
  <p><em>Markdown, distilled.</em></p>
  <p>A fast, lightweight markdown viewer for Windows</p>

  <a href="https://github.com/oipoistar/tinta/releases/latest">
    <img src="https://img.shields.io/github/v/release/oipoistar/tinta?label=Download&style=for-the-badge&color=1a1a2e" alt="Download">
  </a>
  <a href="https://tinta.cc">
    <img src="https://img.shields.io/badge/website-tinta.cc-8b4513?style=for-the-badge" alt="Website">
  </a>
  <a href="LICENSE">
    <img src="https://img.shields.io/badge/license-MIT-blue?style=for-the-badge" alt="MIT License">
  </a>
</div>

<br>

Tinta is a **fast, lightweight markdown viewer and reader for Windows**, built with Direct2D and DirectWrite for hardware-accelerated rendering. A single native executable under 1 MB that opens instantly — no Electron, no web engine, no installer.

<p align="center">
  <img src="https://tinta.cc/img/screenshots/paper.png" width="49%" alt="Tinta markdown viewer on Windows — Paper light theme">
  <img src="https://tinta.cc/img/screenshots/midnight.png" width="49%" alt="Tinta markdown viewer on Windows — Midnight dark theme">
</p>

## Download

Grab [the latest release](https://github.com/oipoistar/tinta/releases/latest) — a single portable `tinta.exe`, no installation required. Run it, open a markdown file, done.

## Why Tinta?

Most markdown apps ship an entire browser to render text. Tinta uses the GPU-accelerated text stack already built into Windows:

|  | Tinta | Typora | Obsidian | VS Code |
|---|---|---|---|---|
| Startup | **~200 ms** | ~1.5 s | ~3 s | ~2 s |
| Install size | **< 1 MB** | ~90 MB | ~250 MB | ~350 MB |
| Runtime | **Native Direct2D** | Electron | Electron | Electron |

It's a viewer first: perfect as the double-click default for `.md` files, for reading READMEs, notes, and documentation — with an edit mode when you need it.

## Features

- **Lightning-fast startup** - Direct2D rendering, no web engine overhead
- **10 beautiful themes** - 5 light and 5 dark themes to choose from
- **Hardware-accelerated** - Smooth text rendering via DirectWrite
- **Rich tables** - Tables with bold, italic, code, and clickable links in cells
- **Folder browser** - Press B to browse and open markdown files
- **Table of contents** - Press Tab to see document headings, click to jump
- **Edit mode** - Press `:` to edit markdown with live preview, search works in editor too
- **Search** - Find text with F or Ctrl+F, cycle through matches with Enter
- **Persistent settings** - Remembers your theme, zoom level, and window position
- **Text selection & copy** - Select text and copy to clipboard
- **Zoom support** - Ctrl+scroll to zoom in/out
- **Drag & drop** - Drop any markdown file to view it
- **Minimal footprint** - Small binary, minimal dependencies

## Keyboard Shortcuts

| Key | Action |
|-----|--------|
| `B` | Toggle folder browser |
| `Tab` | Toggle table of contents |
| `F` / `Ctrl+F` | Open search |
| `Enter` | Next search match |
| `ESC` | Close overlay / Quit |
| `T` | Open theme chooser |
| `S` | Toggle stats overlay |
| `Ctrl+C` | Copy selected text (or all if none selected) |
| `Ctrl+A` | Select all text |
| `Ctrl+Scroll` | Zoom in/out |
| `Arrow keys` / `J/K` | Scroll |
| `Page Up/Down` | Page scroll |
| `Home/End` | Jump to start/end |
| `:` | Enter edit mode |
| `ESC` `ESC` | Exit edit mode |
| `Ctrl+S` | Save (in edit mode) |
| `Q` | Quit |

## Building

Requires Windows with Visual Studio 2019+ and CMake 3.15+.

```bash
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

The executable will be at `build/Release/tinta.exe`.

## Usage

```bash
# Open a markdown file
tinta.exe document.md

# Open with light theme
tinta.exe -l document.md

# Show stats on startup
tinta.exe -s document.md

# Register as default .md viewer
tinta.exe /register
```

Or simply drag and drop a `.md` file onto the window.

## File Association

On first launch, Tinta will ask if you want to set it as the default viewer for `.md` files. If you choose "No", you won't be asked again.

To register Tinta as the default viewer later, run:
```bash
tinta.exe /register
```

This sets up the file association in the Windows registry so you can double-click any `.md` file to open it in Tinta.

## Themes

Press `T` to open the theme chooser:

**Light Themes:**
- Paper - Warm sepia, literary feel
- Sakura - Soft pink elegance
- Arctic - Cool blue-white
- Meadow - Fresh green
- Dusk - Warm gray twilight

**Dark Themes:**
- Midnight - Deep blue-black
- Dracula - Purple-tinted dark
- Forest - Deep green
- Ember - Warm charcoal
- Abyss - Pure black (OLED-friendly)

## Dependencies

- [MD4C](https://github.com/mity/md4c) - Fast markdown parser (fetched automatically by CMake)
- Windows Direct2D/DirectWrite (system libraries)

## License

MIT
