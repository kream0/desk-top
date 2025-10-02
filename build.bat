@echo off
setlocal

set "INCLUDE_DIR=raylib45\raylib-4.5.0_win64_mingw-w64\include"
set "LIB_DIR=raylib45\raylib-4.5.0_win64_mingw-w64\lib"
set "COMMON_FLAGS=-std=c99 -Wall"
set "INCLUDE_FLAGS=-I"%INCLUDE_DIR%""
set "LIB_FLAGS=-L"%LIB_DIR%""
set "LIBS=-lraylib -lm -lgdi32 -lwinmm -lole32 -luuid -lmfplat -lmfreadwrite -lmfuuid -lshlwapi"

echo Building desktop_app...
gcc main.c win_clipboard.c win_video.c -o desktop_app %COMMON_FLAGS% %INCLUDE_FLAGS% %LIB_FLAGS% %LIBS%
if errorlevel 1 goto :error

echo Building video_probe...
gcc video_probe.c win_video.c -o video_probe %COMMON_FLAGS% %INCLUDE_FLAGS% %LIB_FLAGS% %LIBS%
if errorlevel 1 goto :error

echo Build complete.
endlocal
exit /b 0

:error
echo Build failed.
endlocal
exit /b 1