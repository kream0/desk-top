# Desktop Canvas App

A GUI canvas application for manipulating media content using raylib.

## Features

- Canvas interface for placing and manipulating content boxes
- Support for text, images, video, audio, and drawings
- Move, resize, and delete content boxes
- Drawing tools: rectangle and circle
- Paste text from clipboard

## Controls

- S: Select tool
- R: Rectangle drawing tool
- C: Circle drawing tool
- Mouse: Select, move, resize boxes; draw shapes
- Delete: Remove selected box
- Ctrl+V: Paste text or image file path
- Double-click canvas: Create a new text box in edit mode
- Double-click text/image boxes: Enter text edit mode
- Ctrl+= / Ctrl+- (text edit): Increase or decrease font size for the active text box; Ctrl+0 resets the size

## Pasting Content

- **Text**: Copy text and press Ctrl+V to create a text box
- **Images**: Copy the file path of an image (.png, .jpg, .jpeg, .bmp) and press Ctrl+V to load and display the image
- **Video/Audio**: Not yet implemented (planned for future versions)

Note: Pasting actual image data from clipboard may cause warnings; use file paths for best results.

## Building

### Prerequisites

- GCC or compatible C compiler
- raylib library installed (version 4.5.0 or compatible)

### Windows

Download raylib from https://www.raylib.com/ and follow installation instructions, or use the provided raylib45 folder.

Then compile with:

```
build.bat
```

Or to build and run:

```
build-and-run.bat
```

Or manually:

```
gcc main.c -o desktop_app -I"raylib45/raylib-4.5.0_win64_mingw-w64/include" -L"raylib45/raylib-4.5.0_win64_mingw-w64/lib" -lraylib -lm -std=c99 -lgdi32 -lwinmm
```

Or use the Makefile:

```
make
```

### Linux

Install raylib via package manager or build from source.

Compile with:

```
gcc main.c -o desktop_app -lraylib -lm -std=c99 -lpthread -ldl -lX11
```

### macOS

Similar to Linux, adjust libraries as needed.

## Running

```
./desktop_app
```

## Notes

- All content is contained within resizable, movable boxes
- Drawing creates new boxes with rendered shapes
- Text paste creates text boxes
- For full media support, extend the paste functionality