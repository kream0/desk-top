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
- [ ] **Enable editing-mode mobility**: Keep text boxes draggable during edit mode (dragging between handles) while preserving resize handles and current drag-anywhere behavior when not editing.
- [ ] **Add canvas toolbar**:
  - [ ] Pen and line drawing tools with a simple color picker
  - [ ] Move to top / move to bottom buttons
  - [ ] Export entire canvas to clipboard
  - [ ] Clear all button with confirmation dialog
- [ ] **Implement undo/redo shortcuts**: Support Ctrl+Z (undo) and Ctrl+Y (redo) across canvas actions.

## Medium Priority - Enhanced Interactions
- [x] **Double-click interactions**:
  - [x] Double-click on empty canvas → Create text box in edit mode
  - [x] Double-click on existing text box → Enter text edit mode
  - [x] Double-click on image → Create text box on top of image
- [ ] **Text editing system**:
  - Cursor positioning within text
  - Text selection and deletion
  - Real-time text input and display
  - Enter/Escape to confirm/cancel editing
- [ ] **Smart box wrapping**: Ensure all content boxes properly fit their content
  - Text boxes auto-size to text dimensions
  - Image boxes match image dimensions
  - Drawing boxes maintain drawn content bounds

## Medium Priority - Additional Features
- [ ] Add video content support (load and display video files)
- [ ] Add audio content support (load and play audio files)
- [ ] Implement pen drawing tool (freeform drawing)
- [ ] Implement segment/line drawing tool

## Low Priority - Polish and Performance
- [ ] Add proper memory management (free textures, strings on box deletion)
- [ ] Optimize rendering (only redraw changed areas)
- [ ] Add undo/redo functionality
- [ ] Implement file save/load for canvas state
- [ ] Add more drawing tools (eraser, colors, etc.)
- [ ] Improve UI (tool selection UI, status bar)
- [ ] Add keyboard shortcuts for all tools
- [ ] Support multiple selection of boxes
- [ ] Add box layering/z-index management

## Technical Debt
- [ ] Improve error handling in clipboard functions
- [ ] Add bounds checking for box operations
- [ ] Implement proper cleanup on application exit
- [ ] Add configuration file support