#ifndef WIN_VIDEO_H
#define WIN_VIDEO_H

#include "raylib.h"

#ifdef _WIN32

typedef struct WinVideoPlayer WinVideoPlayer;

int WinVideo_GlobalInit(void);
void WinVideo_GlobalShutdown(void);
WinVideoPlayer* WinVideo_Load(const char* filePath);
void WinVideo_Unload(WinVideoPlayer* player);
void WinVideo_Update(WinVideoPlayer* player, float deltaSeconds);
Texture2D* WinVideo_GetTexture(WinVideoPlayer* player);
int WinVideo_IsReady(const WinVideoPlayer* player);
void WinVideo_SetPaused(WinVideoPlayer* player, int paused);
int WinVideo_IsPaused(const WinVideoPlayer* player);
void WinVideo_Rewind(WinVideoPlayer* player);
const char* WinVideo_GetLastError(void);
int WinVideo_GetDecodedFrameCount(const WinVideoPlayer* player);
int WinVideo_GetFallbackFrameCount(const WinVideoPlayer* player);

#else

typedef struct WinVideoPlayer {
    int dummy;
} WinVideoPlayer;

static inline int WinVideo_GlobalInit(void) { return 0; }
static inline void WinVideo_GlobalShutdown(void) { }
static inline WinVideoPlayer* WinVideo_Load(const char* filePath) { (void)filePath; return NULL; }
static inline void WinVideo_Unload(WinVideoPlayer* player) { (void)player; }
static inline void WinVideo_Update(WinVideoPlayer* player, float deltaSeconds) { (void)player; (void)deltaSeconds; }
static inline Texture2D* WinVideo_GetTexture(WinVideoPlayer* player) { (void)player; return NULL; }
static inline int WinVideo_IsReady(const WinVideoPlayer* player) { (void)player; return 0; }
static inline void WinVideo_SetPaused(WinVideoPlayer* player, int paused) { (void)player; (void)paused; }
static inline int WinVideo_IsPaused(const WinVideoPlayer* player) { (void)player; return 1; }
static inline void WinVideo_Rewind(WinVideoPlayer* player) { (void)player; }
static inline const char* WinVideo_GetLastError(void) { return "Video playback not supported"; }
static inline int WinVideo_GetDecodedFrameCount(const WinVideoPlayer* player) { (void)player; return 0; }
static inline int WinVideo_GetFallbackFrameCount(const WinVideoPlayer* player) { (void)player; return 0; }

#endif

#endif /* WIN_VIDEO_H */
