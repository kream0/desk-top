# Last Session Summary

## Date: October 2, 2025 (transport controls)

### Completed Work
- Delivered unified transport controls for audio and video boxes, including play/pause buttons, looping toggles, and seekable progress bars with live timecode readouts.
- Migrated audio playback to streamed `Music` assets so progress tracking stays accurate during scrubbing and looping.
- Synced Media Foundation player state (duration, position, loop) into the canvas overlay and history snapshots so restores keep playback settings intact.
- Polished the canvas UI: hover feedback on transport buttons, consistent status messaging, and refined audio loop button drawing to avoid hitbox mismatches.

### Known Issues
- Transport overlays still rely on polling timers and may drift on very long clips; investigate tighter sync against Media Foundation timecodes.
- Repeated status toasts can crowd out transport feedback during heavy video diagnostics—consider prioritisation.

### Next Steps
- Capture longer playback sessions to confirm loop transitions stay seamless across restore/undo flows.
- Explore adding mute/volume controls and keyboard shortcuts for the new transports.

## Date: October 2, 2025 (profiling)

### Completed Work
- Instrumented the Media Foundation conversion paths with high-resolution timers so each `WinVideoPlayer` tracks average, peak, and last-frame CPU cost alongside the negotiated sample format.
- Surfaced the new metrics in the canvas: video boxes now show convert timing overlays and raise a toast the first time real samples arrive, keeping fallback alerts untouched.
- Extended `video_probe` to dump convert statistics (format, sample count, avg/peak/last ms) for quick CLI profiling.
- Rebuilt the desktop app and probe with `build.bat` to confirm the new instrumentation compiles cleanly.

### Known Issues
- Need to exercise the instrumentation against lengthy NV12/YUY2 clips to understand worst-case CPU time; current UI surfaces data but we lack threshold-based alerts.
- No SIMD optimisations yet—watch for hotspots once profiling data comes in.

### Next Steps
- Run the probe and desktop app on large planar/interleaved YUV assets, log the reported timings, and decide whether to pursue SIMD or other micro-optimisations.
- Consider persisting or exporting profiling snapshots for regression tracking if manual sampling proves cumbersome.

## Date: October 2, 2025 (latest)

### Completed Work
- Added Media Foundation fallbacks for NV12 and YUY2 subtypes, including CPU-side conversions to RGBA so videos stay live when RGB outputs are unavailable.
- Extended pixel negotiation and stride handling to detect planar formats, fetch real buffer sizes, and gate conversions on positive strides to avoid misaligned reads.
- Verified the desktop app and `video_probe` continue to build cleanly via `build.bat` after the new conversion paths.

### Known Issues
- NV12/YUY2 conversion currently assumes positive strides; investigate bottom-up buffers or IMF2DBuffer usage if encountered.

### Next Steps
- Profile the new YUV conversion paths on large clips and evaluate SIMD opportunities if CPU headroom dips during playback.

## Date: October 2, 2025 (later)

### Completed Work
- Expanded Media Foundation negotiation to request BGRA32 when available and capture the actual output subtype so each player can configure stride and channel swizzles accurately.
- Reworked the pixel copy loop to honor the recorded channel order and alpha semantics, only forcing opacity when decoders feed zero alpha so real frames replace the diagnostic gradient on BGRA outputs.
- Verified `build.bat` so both the desktop app and `video_probe` continue to compile without warnings.

### Known Issues
- NV12/YUY2 video outputs still fall back to the gradient because we do not yet convert planar or interleaved YUV frames on the CPU.

### Next Steps
- Prototype a lightweight NV12/YUY2 conversion path and revisit toast prioritisation once the new conversions land.

## Date: October 2, 2025 (continued)

### Completed Work
- Forced every decoded frame to render opaque so Media Foundation outputs that leave alpha at zero no longer disappear despite rising frame counters.
- Surfaced Media Foundation decode diagnostics in the UI: video boxes now cache decoded and fallback frame counts, render them inline, and raise status toasts whenever fallback frames appear or the first real frame arrives.
- Wired the new counters through the main loop so the status bar messaging reflects live playback health, making the gradient fallback easy to spot without running `video_probe`.
- Ensured all video box creation/restore paths initialize the counters correctly and validated the build via `build.bat`.

### Known Issues
- Toasts pre-empt other status messages when fallback frames spike; may need prioritisation once more diagnostics land.

### Next Steps
- Keep an eye on pixel-format negotiation when fallback persists so we can triage the Media Foundation conversion path next.

## Date: October 2, 2025

### Completed Work
- Added decode and fallback frame counters to the Windows video pipeline so diagnostics can distinguish real frames from gradient fallbacks.
- Created a headless `video_probe` diagnostic that loads a video via `WinVideo`, advances playback, and reports decoded versus fallback frame counts.
- Extended both the `Makefile` and `build.bat` to compile the new probe alongside the main desktop application.
- Ran the probe against `video_example.mp4`; it decoded 61 frames with zero fallbacks, confirming that the recent Media Foundation fixes render real textures on this sample.

### Known Issues
- GNU Make is not installed in the current environment, so the Windows `build.bat` script is required for local builds until tooling improves.

### Next Steps
- Feed the decoded/fallback counters into status toasts so end users can see when a video falls back to the diagnostic gradient.
- Consider wiring the probe into automated regression checks once a portable testing story is in place.

## Date: October 1, 2025 (continued)

### Completed Work
- **Diagnosed and fixed video texture rendering issue**: Added validation for DXGI buffer size calculation, improved error handling for null samples, and added null-safety checks in texture update path.
- **Enhanced frame decode validation**: Added explicit null check for Media Foundation samples before attempting to decode, preventing silent failures when frames are unavailable.
- **Added fallback test pattern**: When the first frame decode fails, the system now generates a diagnostic gradient pattern to verify the texture upload pipeline is working (helps isolate decoding vs. rendering issues).
- **Improved DXGI buffer handling**: Fixed buffer size calculation for DXGI-backed frames to use `RowPitch * height` instead of relying on `currentLength`, ensuring proper bounds checking for GPU-accelerated decode paths.
- **Strengthened texture update guards**: Added defensive checks (`player->pixels != NULL && player->texture.id != 0`) before calling `UpdateTexture` to prevent crashes when texture resources are invalid.
- Project builds cleanly with no warnings via `build.bat`.

### Known Issues
- Need on-device testing with sample MP4/MOV files to confirm video textures now display properly; the gradient fallback will help diagnose if the issue is in decode or texture upload.

### Next Steps
- Test video playback with real video files and capture behavior (working texture, gradient pattern, or "Loading..." placeholder).
- If gradient appears but real video doesn't, focus on Media Foundation format negotiation and pixel conversion logic.
- If video works, remove or conditionalize the test gradient and proceed to transport controls (scrubbing, looping).

## Date: October 1, 2025

### Completed Work
- Added resilient Media Foundation source reader creation with automatic fallbacks from advanced to basic processing and no-attribute modes.
- Negotiated alternate video output formats (RGB32, ARGB32, RGB24) and convert them to RGBA so videos render even when the preferred format is unavailable.
- Centralized Media Foundation error tracking (`WinVideo_GetLastError`) and surfaced detailed HRESULT messages in status toasts when video ingestion fails.
- Normalized Media Foundation frame copies to handle negative strides and padded rows so MP4/MOV pastes populate video textures reliably.
- Manually downsampled decoded video frames to ≤640×480, throttled per-frame updates, and tightened copy loops to prevent large MP4 loads from freezing the UI or ballooning RAM usage while keeping playback responsive.

### Known Issues
- Still need on-device verification that MP4/MOV pastes now produce video boxes; collect the updated error toast if failures persist.

### Next Steps
- Re-test Windows video ingestion with sample files and update TODO once confirmed.
- Continue designing transport controls and scrubbing UX for audio/video boxes.

## Date: September 30, 2025

### Completed Work
- Delivered audio boxes: clipboard path detection, waveform loading, undo-ready persistence, and space/double-click playback toggles with visual status cues.
- Added playback UI styling with dynamic icons, filename display, and resilient fallbacks when the audio device is unavailable.
- Ensured audio boxes render reliably even when playback assets can’t load, including placeholder UI, status messaging, and history restore support.
- Brought in Media Foundation–backed video boxes with texture streaming, pause/resume toggles, double-click playback, history persistence, and clipboard/drag-drop ingestion.
- Added CF_HDROP clipboard ingestion so Windows file copies paste directly as image, audio, or path boxes with multi-file support.
- Hardened the Media Foundation video pipeline with COM startup, advanced processing hints, and width/height scaling so mp4 pastes should now materialize as playable video boxes instead of plain text fallbacks (needs on-device verification).
- Enabled per-text-box font sizing with Ctrl +/−/0 shortcuts, auto-resize, undo/redo integration, and live status feedback.
- Refreshed text node visuals with lime selection outlines, blue editing focus rings, and a caret that tracks font scale.
- Implemented full text selection and caret navigation: mouse placement, drag selection, Shift+Arrow, Home/End, and vertical movement with column memory.
- Rendered selection highlights, auto-selected newly created text boxes, and added Ctrl+A and mouse drag behaviours that feed undo history.
- Added per-text-box color metadata so text nodes default to black and adopt the active palette selection.
- Palette clicks now recolor the active text box in real time and produce undoable history entries.
- Synced TODO checklist with implemented features (smart wrapping, pen/segment tools, shortcuts, layering) to reflect current scope.
- Verified the project builds cleanly via `build.bat` after the updates.
- Preserved trailing spaces and blank lines in text boxes by replacing strtok-based measurement/drawing with allocation-safe helpers.
- Added text clipboard shortcuts (Ctrl+C/Ctrl+X/Ctrl+V within edits) and Ctrl+Arrow word navigation so text boxes behave like native editors.
- Cleaned up temporary backup sources and ensured global paste shortcuts no longer interfere while editing text boxes.
- Polished the toolbar with rounded buttons, hover feedback, palette highlights, and active tool indicators.
- Introduced a persistent status bar with contextual hints and audio readiness reporting, and excluded it from exported canvas captures.

### Known Issues
- Need to re-test mp4 pasting/export on-device; prior sessions saw video files fall back to text nodes. Status popups now report when video decoding fails.

### Next Steps
- Surface font size controls in the toolbar (slider or preset buttons) and preview the active size.
- Provide toolbar typography controls (alignment, weight presets) now that font sizing is adjustable.
- Replace text abbreviations with iconography and optional tooltips to further elevate the toolbar.
- Verify mp4/Mov pasting now instantiates video boxes; if issues persist, capture the status toast and consider wiring an FFmpeg-backed decoder.
- Explore inline transport controls and visualizers for audio/video boxes, including scrubbing, looping, and hover previews.
- Implement marquee "box" selection via click-and-drag to select multiple items at once.
- Extend marquee selection to full multi-object workflows (bulk move, layer changes, delete, etc.).

## Previous Session (September 29, 2025)

### Completed Work
- Created basic project structure with `main.c`, `Makefile`, and `README.md`.
- Implemented core canvas interface with raylib, box data structures, rendering, manipulation, and drawing tools.
- Set up the cross-platform build system, switched to the C99 standard, and eliminated compilation issues.
- Delivered a complete native Windows clipboard integration (`win_clipboard.c/h`) supporting text, image, and path formats with no GLFW errors.
- Enhanced canvas UX with inline text editing, resize handles, contextual cursors, toolbar controls (tools, palette, layering, export, clear), and undo/redo shortcuts including AZERTY-friendly bindings.
- Implemented freehand pen and segment drawing with texture-backed storage and corrected render orientation.

### Verified Working
- Image pasting (249x170) validated, undo/redo exercised across all content types, and export-to-clipboard re-import tested successfully.
- Toolbar interactions, drawing tools, and clipboard operations run without console errors or leaks.