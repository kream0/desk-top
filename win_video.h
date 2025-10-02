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
double WinVideo_GetConvertCpuAverageMicros(const WinVideoPlayer* player);
double WinVideo_GetConvertCpuPeakMicros(const WinVideoPlayer* player);
double WinVideo_GetConvertCpuLastMicros(const WinVideoPlayer* player);
unsigned int WinVideo_GetConvertCpuSampleCount(const WinVideoPlayer* player);
const char* WinVideo_GetSampleFormatLabel(const WinVideoPlayer* player);
double WinVideo_GetDurationSeconds(const WinVideoPlayer* player);
double WinVideo_GetPositionSeconds(const WinVideoPlayer* player);
void WinVideo_SetPositionSeconds(WinVideoPlayer* player, double seconds);
void WinVideo_SetLooping(WinVideoPlayer* player, int loop);
int WinVideo_IsLooping(const WinVideoPlayer* player);

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
static inline double WinVideo_GetConvertCpuAverageMicros(const WinVideoPlayer* player) { (void)player; return 0.0; }
static inline double WinVideo_GetConvertCpuPeakMicros(const WinVideoPlayer* player) { (void)player; return 0.0; }
static inline double WinVideo_GetConvertCpuLastMicros(const WinVideoPlayer* player) { (void)player; return 0.0; }
static inline unsigned int WinVideo_GetConvertCpuSampleCount(const WinVideoPlayer* player) { (void)player; return 0u; }
static inline const char* WinVideo_GetSampleFormatLabel(const WinVideoPlayer* player) { (void)player; return "Unknown"; }
static inline double WinVideo_GetDurationSeconds(const WinVideoPlayer* player) { (void)player; return 0.0; }
static inline double WinVideo_GetPositionSeconds(const WinVideoPlayer* player) { (void)player; return 0.0; }
static inline void WinVideo_SetPositionSeconds(WinVideoPlayer* player, double seconds) { (void)player; (void)seconds; }
static inline void WinVideo_SetLooping(WinVideoPlayer* player, int loop) { (void)player; (void)loop; }
static inline int WinVideo_IsLooping(const WinVideoPlayer* player) { (void)player; return 0; }

#endif

#endif /* WIN_VIDEO_H */
