# TODO List

## ✅ COMPLETED - Major Achievements
- [x] **RESOLVED**: Windows GLFW clipboard errors completely eliminated
- [x] **IMPLEMENTED**: Full native Windows clipboard support with real image pasting
- [x] **TESTED**: Image clipboard functionality working (249x170 image successfully loaded)
- [x] Install raylib library on development machine
- [x] Fix compilation errors and ensure build passes
- [x] Test basic functionality (window opens, mouse interaction works)
- [x] Implement image pasting from clipboard (screenshots, browser images, etc.)
- [x] Deliver canvas UX polish (live text sizing, focus handling, resize handles, contextual cursors)

## High Priority - User Experience Improvements
- [x] **Fix text box sizing**: Auto-resize text boxes to fit content (currently fixed 200x50)
- [x] **Implement text editing mode**: Double-click on canvas or text box to create/edit text
- [x] **Add proper mouse cursors**:
  - [x] Resize cursors when hovering over box edges (horizontal, vertical, diagonal)
  - [x] Move cursor when dragging boxes
  - [x] Default cursor for normal canvas interaction
- [x] **Implement image resizing**: Add resize handles for image boxes
- [x] **Implement text box resizing**: Add resize handles for text boxes
- [x] **Fix drawing color inversion**: Drawn shapes and line tool output appear inverted after releasing the mouse.
- [x] **Correct toolbar shape buttons**: Circle and rectangle buttons trigger the wrong tools; ensure they map to the intended shapes.
- [x] **Enable editing-mode mobility**: Keep text boxes draggable during edit mode (dragging between handles) while preserving resize handles and current drag-anywhere behavior when not editing.
- [x] **Add canvas toolbar**:
  - [x] Pen and line drawing tools with a simple color picker
  - [x] Move to top / move to bottom buttons
  - [x] Export entire canvas to clipboard
  - [x] Clear all button with confirmation dialog
- [x] **Implement undo/redo shortcuts**: Support Ctrl+Z (undo) and Ctrl+Y (redo) across canvas actions.

## Medium Priority - Enhanced Interactions
- [x] **Double-click interactions**:
  - [x] Double-click on empty canvas → Create text box in edit mode
  - [x] Double-click on existing text box → Enter text edit mode
  - [x] Double-click on image → Create text box on top of image
- [x] **Text editing system**:
  - [x] Cursor positioning within text
  - [x] Text selection and deletion
  - [x] Real-time text input and display
  - [x] Enter/Escape to confirm/cancel editing
  - [x] Support Home/End navigation, range selection, and Delete key behaviour matching standard editors
- [x] **Smart box wrapping**: Ensure all content boxes properly fit their content
  - Text boxes auto-size to text dimensions
  - Image boxes match image dimensions
  - Drawing boxes maintain drawn content bounds
- [x] Text nodes should default to black and honor the active color picker selection
- [x] Automatically select (highlight) newly created text boxes
- [x] Preserve trailing spaces and blank lines in text boxes after exiting edit mode
- [x] Support text clipboard workflows and navigation shortcuts (Ctrl+C/X/V, Ctrl+A, Ctrl+Arrow, Ctrl+Home/End)
- [x] Add per-text-box font sizing controls (Ctrl +/−/0) with live auto-resize
- [x] Highlight active text edits with dedicated focus outline and caret styling

## Medium Priority - Additional Features
- [ ] Add video content support (load and display video files)
- [ ] Add audio content support (load and play audio files)
- [x] Implement pen drawing tool (freeform drawing)
- [x] Implement segment/line drawing tool

## Low Priority - Polish and Performance
- [x] Add undo/redo functionality
- [ ] Improve UI (tool selection UI, status bar)
- [x] Add keyboard shortcuts for all tools
- [ ] Support multiple selection of boxes
- [x] Add box layering/z-index management
- [ ] Make selection outline lime green for better contrast

## Technical Debt
- [ ] Improve error handling in clipboard functions
- [ ] Add bounds checking for box operations
- [ ] Implement proper cleanup on application exit
- [ ] Add configuration file support