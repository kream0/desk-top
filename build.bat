@echo off
gcc main.c win_clipboard.c -o desktop_app -I"raylib45/raylib-4.5.0_win64_mingw-w64/include" -L"raylib45/raylib-4.5.0_win64_mingw-w64/lib" -lraylib -lm -std=c99 -lgdi32 -lwinmm
echo Build complete.