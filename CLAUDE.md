# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

### Cross-platform (Recommended)
- Build: `make` (automatically detects platform and uses appropriate raylib)
- Clean: `make clean`

### Platform-specific
**Windows:**
- Quick build: `build.bat`
- Build and run: `build-and-run.bat`
- Manual: `gcc main.c -o desktop_app.exe -I"raylib45/raylib-4.5.0_win64_mingw-w64/include" -L"raylib45/raylib-4.5.0_win64_mingw-w64/lib" -lraylib -lm -std=c99 -lgdi32 -lwinmm`

**Linux/WSL:**
- Uses `raylib-4.5.0_linux_amd64/` directory
- Links with `-lX11 -lpthread -ldl`

**macOS:**
- Uses same raylib as Linux but with macOS frameworks

### Running
- `./desktop_app` (Linux/macOS/WSL)
- `desktop_app.exe` (Windows)

## Architecture

This is a single-file C application (`main.c`) using raylib for GUI functionality. The application implements a canvas-based interface where all content is contained within rectangular boxes that can be moved, resized, and deleted.

### Core Data Structures
- `Box` struct: Contains position, dimensions, type, and content data (texture, text, or file path)
- `BoxType` enum: Defines content types (IMAGE, TEXT, VIDEO, AUDIO, DRAWING)
- `Tool` enum: Defines interaction tools (SELECT, PEN, SEGMENT, CIRCLE, RECT)

### Key Systems
- **Box Management**: All content is encapsulated in boxes with unified manipulation (move, resize, delete)
- **Tool System**: Different tools for selection, drawing shapes, and content creation
- **Input Handling**: Mouse interactions for box manipulation and keyboard shortcuts for tool switching
- **Clipboard Integration**: Paste text and image file paths using Ctrl+V

### Development Guidelines
- Use C99 standard
- Keep raylib as the only external dependency
- All content must be contained within boxes
- Maintain cross-platform compatibility (Windows-first, then Linux/macOS)
- Follow atomic commit pattern - ensure builds pass after each commit
- Update LAST_SESSION.md and TODO.md after completing features

### Project Constraints
- Maximum 100 boxes (MAX_BOXES constant)
- Single-threaded architecture
- File-based image loading (paste file paths, not binary data)
- Minimal external dependencies beyond raylib