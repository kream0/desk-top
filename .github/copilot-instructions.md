# Copilot Instructions for the Desktop Canvas project

Pair-programming assistant for a raylib canvas application written in C99.

## Overview
- Language: C99 only, with raylib as the video/audio/GUI dependency.
- Platforms: Windows first, while keeping Linux/macOS compatibility in mind.
- Key modules: `main.c` (canvas/box logic), `win_clipboard.*`, `win_video.*`.
- Preserve the "everything is a box" model (position, size, type, content) and the existing tools (SELECT, PEN, SEGMENT, CIRCLE, RECT).

## Commands and checks
- Build: `make` (auto-detects the platform) or `build.bat` on Windows.
- Clean: `make clean`.
- Run: `desktop_app.exe` (Windows) or `./desktop_app` (Unix).
- After every significant change: rebuild to ensure a clean binary with no warnings.

## Coding rules
- Do not introduce new external dependencies (raylib only).
- Respect the 100-box maximum and single-threaded architecture.
- Keep unified box management (creation, move, resize, delete, undo/redo).
- Maintain consistent UI/UX: toolbar, status bar, status toasts, existing keyboard shortcuts.
- Windows Media Foundation additions must report errors through `WinVideo_GetLastError` so toasts surface useful messages.

## Session protocol
- **Before coding:** read [`PRD.md`](../PRD.md) for product vision, [`LAST_SESSION.md`](../LAST_SESSION.md) for recent work, [`TODO.md`](../TODO.md) for priorities, and review [`CLAUDE.md`](../CLAUDE.md) for architectural detail.
- Align with the current priorities (e.g., stabilizing Windows video playback, audio/video transport controls, multi-selection).
- Present a brief plan, work in atomic increments, and keep the bar at production quality.

## Session wrap-up
- Summarize the session in `LAST_SESSION.md` (work completed, outstanding issues, next steps).
- Update `TODO.md` to reflect progress.
- Confirm the build succeeds (`make` or `build.bat`).

## Git strategy
- Use atomic commits: one complete, compiling feature/fix per commit.
- Commit message format: `type(scope): description` (types: feat, fix, refactor, test, docs, chore).
- At the end of a task or session: create a recap commit and record follow-ups in the documentation.

## Quick references
- Architecture and patterns: [`CLAUDE.md`](../CLAUDE.md).
- Product requirements: [`PRD.md`](../PRD.md).
- Session history: [`LAST_SESSION.md`](../LAST_SESSION.md).
- Current priorities: [`TODO.md`](../TODO.md).
