<div align="center">
  <img src="resources/tinta.ico" width="80">
  <h1>Tinta</h1>
  <p><em>Markdown, distilled.</em></p>
  <p>A fast, lightweight markdown reader for Windows</p>

  <a href="https://github.com/oipoistar/tinta/releases/latest">
    <img src="https://img.shields.io/github/v/release/oipoistar/tinta?label=Download&style=for-the-badge&color=1a1a2e" alt="Download">
  </a>
  <a href="LICENSE">
    <img src="https://img.shields.io/badge/license-MIT-blue?style=for-the-badge" alt="MIT License">
  </a>
</div>

<br>

Built with Direct2D and DirectWrite for hardware-accelerated rendering. ~270KB binary, ~200ms startup.

## Features

- **Lightning-fast startup** - Direct2D rendering, no web engine overhead
- **10 beautiful themes** - 5 light and 5 dark themes to choose from
- **Hardware-accelerated** - Smooth text rendering via DirectWrite
- **Rich tables** - Tables with bold, italic, code, and clickable links in cells
- **Folder browser** - Press B to browse and open markdown files
- **Table of contents** - Press Tab to see document headings, click to jump
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
