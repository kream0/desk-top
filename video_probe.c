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
    double avgConvertUs = WinVideo_GetConvertCpuAverageMicros(player);
    double peakConvertUs = WinVideo_GetConvertCpuPeakMicros(player);
    double lastConvertUs = WinVideo_GetConvertCpuLastMicros(player);
    unsigned int convertSamples = WinVideo_GetConvertCpuSampleCount(player);
    const char* formatLabel = WinVideo_GetSampleFormatLabel(player);
    const char* lastErr = WinVideo_GetLastError();

    printf("Video probe result\n");
    printf("  Source: %s\n", path);
    printf("  Decoded frames: %d\n", decodedFrames);
    printf("  Fallback frames: %d\n", fallbackFrames);
    printf("  Convert format: %s\n", (formatLabel != NULL) ? formatLabel : "Unknown");
    printf("  Convert samples: %u\n", convertSamples);
    if (convertSamples > 0u) {
        printf("  Convert avg: %.3f ms\n", avgConvertUs / 1000.0);
        printf("  Convert peak: %.3f ms\n", peakConvertUs / 1000.0);
        printf("  Convert last: %.3f ms\n", lastConvertUs / 1000.0);
    }
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
