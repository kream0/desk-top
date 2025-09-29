# MVP
## Description

Desktop is a GUI "canvas" app allowing the user to manipulate all sorts of media: images, text, video audio.

## Context

This project implements a minimal viable product (MVP) for a desktop canvas application. The app provides a flexible canvas interface where users can paste various media types and manipulate them through a unified box-based system. All content is contained within rectangular boxes that can be moved, resized, and deleted. The application emphasizes simplicity and cross-platform compatibility.

## Features
The app should initialize a "canvas-like" interface with the ability to:
- paste content: image, text, video, audio
- move things around
- resize things
- draw (pen, segment, circle, rect tools)

Rules:
- all content is displayed in a rect box 
- rect boxes are resizable, movable and deletable

## Technical Requirements:

- must work on Windows-first
- should be multi-platform (at least linux and mac)
- use raylib for the needed APIs
- C99 C language (updated from C89 due to raylib compatibility requirements)
- no external libraries apart from raylib

## Project Structure

- `main.c`: Main application code containing the canvas logic, box management, input handling, and rendering
- `Makefile`: Cross-platform build script for compiling the application
- `README.md`: Documentation including build instructions, controls, and usage notes
- `PRD.md`: This product requirements document
- `LAST_SESSION.md`: Summary of the last development session
- `TODO.md`: Current task list and development roadmap
- `raylib45/`: Raylib library files (downloaded separately, not committed to repository)
- `desktop_app.exe`: Compiled executable (generated, not committed)

# Work Requirements

- always iterate with small features
- commit working atomic changes (the build should pass)
- when a feature is finished, update the LAST_SESSION.md and TODO.md files. If not present, create them