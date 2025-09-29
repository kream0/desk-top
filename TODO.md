# TODO List

## ✅ COMPLETED - Major Achievements
- [x] **RESOLVED**: Windows GLFW clipboard errors completely eliminated
- [x] **IMPLEMENTED**: Full native Windows clipboard support with real image pasting
- [x] **TESTED**: Image clipboard functionality working (249x170 image successfully loaded)
- [x] Install raylib library on development machine
- [x] Fix compilation errors and ensure build passes
- [x] Test basic functionality (window opens, mouse interaction works)
- [x] Implement image pasting from clipboard (screenshots, browser images, etc.)

## High Priority - User Experience Improvements
- [ ] **Fix text box sizing**: Auto-resize text boxes to fit content (currently fixed 200x50)
- [ ] **Implement text editing mode**: Double-click on canvas or text box to create/edit text
- [ ] **Add proper mouse cursors**:
  - Resize cursors when hovering over box edges (horizontal, vertical, diagonal)
  - Move cursor when dragging boxes
  - Default cursor for normal canvas interaction
- [ ] **Implement image resizing**: Add resize handles for image boxes
- [ ] **Implement text box resizing**: Add resize handles for text boxes

## Medium Priority - Enhanced Interactions
- [ ] **Double-click interactions**:
  - Double-click on empty canvas → Create text box in edit mode
  - Double-click on existing text box → Enter text edit mode
  - Double-click on image → Create text box on top of image
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