#include "raylib.h"
#include <stdio.h>
#include <stdlib.h>
#include "win_video.h"

static float GetDeltaTime(void) {
    return 1.0f / 60.0f;
}

int main(int argc, char** argv) {
    const char* path = (argc > 1) ? argv[1] : "video_example.mp4";

    SetConfigFlags(FLAG_WINDOW_HIDDEN | FLAG_WINDOW_UNFOCUSED | FLAG_MSAA_4X_HINT);
    InitWindow(32, 32, "video_probe");
    SetTargetFPS(60);

    if (!WinVideo_GlobalInit()) {
        const char* err = WinVideo_GetLastError();
        fprintf(stderr, "WinVideo_GlobalInit failed: %s\n", err != NULL ? err : "(unknown)");
        CloseWindow();
        return EXIT_FAILURE;
    }

    WinVideoPlayer* player = WinVideo_Load(path);
    if (player == NULL) {
        const char* err = WinVideo_GetLastError();
        fprintf(stderr, "WinVideo_Load failed: %s\n", err != NULL ? err : "(unknown)");
        WinVideo_GlobalShutdown();
        CloseWindow();
        return EXIT_FAILURE;
    }

    int frames = 0;
    const int maxFrames = 120;
    while (frames < maxFrames) {
        WinVideo_Update(player, GetDeltaTime());
        BeginDrawing();
        ClearBackground(BLACK);
        EndDrawing();
        frames++;
    }

    int decodedFrames = WinVideo_GetDecodedFrameCount(player);
    int fallbackFrames = WinVideo_GetFallbackFrameCount(player);
    const char* lastErr = WinVideo_GetLastError();

    printf("Video probe result\n");
    printf("  Source: %s\n", path);
    printf("  Decoded frames: %d\n", decodedFrames);
    printf("  Fallback frames: %d\n", fallbackFrames);
    if (lastErr != NULL) {
        printf("  Last error: %s\n", lastErr);
    }

    WinVideo_Unload(player);
    WinVideo_GlobalShutdown();
    CloseWindow();

    if (decodedFrames > 0) {
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}
