#include "win_video.h"

#ifdef _WIN32

#define COBJMACROS
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0602
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define Rectangle Win32Rectangle
#define CloseWindow Win32CloseWindow
#define ShowCursor Win32ShowCursor
#include <windows.h>
#undef Rectangle
#undef CloseWindow
#undef ShowCursor
#include <objbase.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <propvarutil.h>
#include <shlwapi.h>
#include <stdlib.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#define WINVIDEO_MAX_DECODE_WIDTH 640u
#define WINVIDEO_MAX_DECODE_HEIGHT 480u
#define WINVIDEO_MIN_FRAME_DURATION (1.0f / 120.0f)
#include <mfobjects.h>
#define WINVIDEO_MAX_FRAME_STEPS 4

#include <d3d11.h>
#ifndef SAFE_RELEASE
#define SAFE_RELEASE(x) if ((x) != NULL) { IUnknown_Release((IUnknown*)(x)); (x) = NULL; }
#endif

typedef enum WinVideoSampleFormat {
    WINVIDEO_SAMPLE_FORMAT_UNKNOWN = 0,
    WINVIDEO_SAMPLE_FORMAT_PACKED,
    WINVIDEO_SAMPLE_FORMAT_NV12,
    WINVIDEO_SAMPLE_FORMAT_YUY2
} WinVideoSampleFormat;

struct WinVideoPlayer {
    IMFSourceReader* reader;
    Texture2D texture;
    unsigned char* pixels;
    int width;
    int height;
    int decodeWidth;
    int decodeHeight;
    float sampleStepX;
    float sampleStepY;
    float frameDuration;
    float timeAccumulator;
    int ready;
    int paused;
    int bytesPerPixel;
    LONG stride;
    int endOfStream;
    int decodedFrameCount;
    int fallbackFrameCount;
    GUID sourceSubtype;
    int sourceChannelsAreBgr;
    int sourceHasAlpha;
    int forceOpaqueAlpha;
    WinVideoSampleFormat sampleFormat;
    double convertCpuSecondsAccum;
    double convertCpuSecondsPeak;
    double convertCpuSecondsLast;
    unsigned int convertCpuSampleCount;
    double durationSeconds;
    double positionSeconds;
    int loop;
};

static int gVideoInitialized = 0;
static int gVideoInitResult = 0;
static int gComInitialized = 0;
static HRESULT gComInitHr = S_OK;
static char gVideoLastError[256] = {0};
static HRESULT gVideoLastHr = S_OK;
static double gVideoPerfSecondsPerCount = 0.0;

static void WinVideo_ClearLastError(void) {
    gVideoLastError[0] = '\0';
    gVideoLastHr = S_OK;
}

static void WinVideo_SetLastError(HRESULT hr, const char* context) {
    gVideoLastHr = hr;
    if (SUCCEEDED(hr)) {
        gVideoLastError[0] = '\0';
        return;
    }

    const char* label = (context != NULL && context[0] != '\0') ? context : "Operation";
    char systemMessage[128] = {0};
    DWORD flags = FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    DWORD length = FormatMessageA(flags, NULL, (DWORD)hr, 0, systemMessage, (DWORD)sizeof(systemMessage), NULL);
    if (length > 0) {
        while (length > 0 && (systemMessage[length - 1] == '\n' || systemMessage[length - 1] == '\r')) {
            systemMessage[length - 1] = '\0';
            length--;
        }
        snprintf(gVideoLastError, sizeof(gVideoLastError), "%s failed (0x%08lX): %s", label, (unsigned long)hr, systemMessage);
    } else {
        snprintf(gVideoLastError, sizeof(gVideoLastError), "%s failed (0x%08lX)", label, (unsigned long)hr);
    }
}

static double WinVideo_QueryDurationSeconds(IMFSourceReader* reader);

static void WinVideo_EnsurePerfTimer(void) {
    if (gVideoPerfSecondsPerCount != 0.0) {
        return;
    }

    LARGE_INTEGER freq;
    if (QueryPerformanceFrequency(&freq) && freq.QuadPart > 0) {
        gVideoPerfSecondsPerCount = 1.0 / (double)freq.QuadPart;
    } else {
        gVideoPerfSecondsPerCount = 1.0 / 1000.0;
    }
}

static double WinVideo_GetSeconds(void) {
    WinVideo_EnsurePerfTimer();
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart * gVideoPerfSecondsPerCount;
}

static double WinVideo_ElapsedSeconds(double startSeconds) {
    double now = WinVideo_GetSeconds();
    double elapsed = now - startSeconds;
    if (elapsed < 0.0) {
        elapsed = 0.0;
    }
    return elapsed;
}

static void WinVideo_RecordConvertTime(struct WinVideoPlayer* player, double elapsedSeconds, int counted) {
    if (player == NULL || !counted) {
        return;
    }

    if (elapsedSeconds < 0.0) {
        elapsedSeconds = 0.0;
    }

    player->convertCpuSecondsLast = elapsedSeconds;
    if (elapsedSeconds > player->convertCpuSecondsPeak) {
        player->convertCpuSecondsPeak = elapsedSeconds;
    }
    player->convertCpuSecondsAccum += elapsedSeconds;
    if (player->convertCpuSampleCount < 0xffffffffu) {
        player->convertCpuSampleCount += 1u;
    }
}

static int WinVideo_GuidsEqual(const GUID* a, const GUID* b) {
    if (a == NULL || b == NULL) {
        return 0;
    }
    return IsEqualGUID(a, b) ? 1 : 0;
}

static unsigned char WinVideo_ClampByte(int value) {
    if (value < 0) {
        return 0;
    }
    if (value > 255) {
        return 255;
    }
    return (unsigned char)value;
}

static void WinVideo_YuvToRgba(unsigned char ySample, unsigned char uSample, unsigned char vSample, unsigned char* dst) {
    if (dst == NULL) {
        return;
    }

    int c = (int)ySample - 16;
    if (c < 0) {
        c = 0;
    }
    int d = (int)uSample - 128;
    int e = (int)vSample - 128;

    int r = (298 * c + 409 * e + 128) >> 8;
    int g = (298 * c - 100 * d - 208 * e + 128) >> 8;
    int b = (298 * c + 516 * d + 128) >> 8;

    dst[0] = WinVideo_ClampByte(r);
    dst[1] = WinVideo_ClampByte(g);
    dst[2] = WinVideo_ClampByte(b);
    dst[3] = 255;
}

static int WinVideo_BytesPerPixelForSubtype(const GUID* subtype) {
    if (subtype == NULL) {
        return 0;
    }

    if (WinVideo_GuidsEqual(subtype, &MFVideoFormat_RGB32) ||
        WinVideo_GuidsEqual(subtype, &MFVideoFormat_ARGB32)
#ifdef MFVideoFormat_BGRA32
        || WinVideo_GuidsEqual(subtype, &MFVideoFormat_BGRA32)
#endif
#ifdef MFVideoFormat_BGR32
        || WinVideo_GuidsEqual(subtype, &MFVideoFormat_BGR32)
#endif
#ifdef MFVideoFormat_ABGR32
        || WinVideo_GuidsEqual(subtype, &MFVideoFormat_ABGR32)
#endif
#ifdef MFVideoFormat_RGBA32
        || WinVideo_GuidsEqual(subtype, &MFVideoFormat_RGBA32)
#endif
    ) {
        return 4;
    }

    if (WinVideo_GuidsEqual(subtype, &MFVideoFormat_RGB24)
#ifdef MFVideoFormat_BGR24
        || WinVideo_GuidsEqual(subtype, &MFVideoFormat_BGR24)
#endif
    ) {
        return 3;
    }

#ifdef MFVideoFormat_YUY2
    if (WinVideo_GuidsEqual(subtype, &MFVideoFormat_YUY2)) {
        return 2;
    }
#endif

#ifdef MFVideoFormat_NV12
    if (WinVideo_GuidsEqual(subtype, &MFVideoFormat_NV12)) {
        return 1;
    }
#endif

    return 0;
}

static void WinVideo_ConfigureConversionFromSubtype(struct WinVideoPlayer* player, const GUID* subtype) {
    if (player == NULL) {
        return;
    }

    player->sourceSubtype = (subtype != NULL) ? *subtype : GUID_NULL;
    player->sourceChannelsAreBgr = 1;
    player->sourceHasAlpha = 0;
    player->forceOpaqueAlpha = 1;
    player->sampleFormat = WINVIDEO_SAMPLE_FORMAT_PACKED;

    if (subtype == NULL) {
        return;
    }

#ifdef MFVideoFormat_NV12
    if (WinVideo_GuidsEqual(subtype, &MFVideoFormat_NV12)) {
        player->sampleFormat = WINVIDEO_SAMPLE_FORMAT_NV12;
        player->sourceChannelsAreBgr = 0;
        player->sourceHasAlpha = 0;
        player->forceOpaqueAlpha = 1;
        return;
    }
#endif

#ifdef MFVideoFormat_YUY2
    if (WinVideo_GuidsEqual(subtype, &MFVideoFormat_YUY2)) {
        player->sampleFormat = WINVIDEO_SAMPLE_FORMAT_YUY2;
        player->sourceChannelsAreBgr = 0;
        player->sourceHasAlpha = 0;
        player->forceOpaqueAlpha = 1;
        return;
    }
#endif

    if (WinVideo_GuidsEqual(subtype, &MFVideoFormat_RGB24) ||
        WinVideo_GuidsEqual(subtype, &MFVideoFormat_RGB32)) {
        return;
    }

    if (WinVideo_GuidsEqual(subtype, &MFVideoFormat_ARGB32)
#ifdef MFVideoFormat_BGRA32
        || WinVideo_GuidsEqual(subtype, &MFVideoFormat_BGRA32)
#endif
    ) {
        player->sourceHasAlpha = 1;
        return;
    }

#ifdef MFVideoFormat_BGR24
    if (WinVideo_GuidsEqual(subtype, &MFVideoFormat_BGR24)) {
        return;
    }
#endif

#ifdef MFVideoFormat_BGR32
    if (WinVideo_GuidsEqual(subtype, &MFVideoFormat_BGR32)) {
        return;
    }
#endif

#ifdef MFVideoFormat_RGBA32
    if (WinVideo_GuidsEqual(subtype, &MFVideoFormat_RGBA32)) {
        player->sourceChannelsAreBgr = 0;
        player->sourceHasAlpha = 1;
        player->forceOpaqueAlpha = 0;
        return;
    }
#endif

#ifdef MFVideoFormat_ABGR32
    if (WinVideo_GuidsEqual(subtype, &MFVideoFormat_ABGR32)) {
        player->sourceChannelsAreBgr = 0;
        player->sourceHasAlpha = 1;
        player->forceOpaqueAlpha = 1;
        return;
    }
#endif

    player->sourceChannelsAreBgr = 1;
    player->sourceHasAlpha = 0;
    player->forceOpaqueAlpha = 1;
}

static HRESULT WinVideo_CreateReaderAttempt(const WCHAR* widePath, IMFSourceReader** outReader, int enableAdvanced) {
    if (outReader == NULL) {
        return E_POINTER;
    }

    *outReader = NULL;

    IMFAttributes* attributes = NULL;
    HRESULT hr = MFCreateAttributes(&attributes, 2);
    if (FAILED(hr)) {
        return hr;
    }

    IMFAttributes_SetUINT32(attributes, &MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
    if (enableAdvanced) {
        IMFAttributes_SetUINT32(attributes, &MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);
    }

    hr = MFCreateSourceReaderFromURL(widePath, attributes, outReader);
    SAFE_RELEASE(attributes);
    return hr;
}

static HRESULT WinVideo_CreateReader(const WCHAR* widePath, IMFSourceReader** outReader, const char** outContext) {
    if (outReader == NULL) {
        return E_POINTER;
    }

    *outReader = NULL;
    if (outContext != NULL) {
        *outContext = NULL;
    }

    HRESULT hr = WinVideo_CreateReaderAttempt(widePath, outReader, 1);
    if (SUCCEEDED(hr)) {
        if (outContext != NULL) {
            *outContext = "MFCreateSourceReaderFromURL (advanced)";
        }
        return hr;
    }

    hr = WinVideo_CreateReaderAttempt(widePath, outReader, 0);
    if (SUCCEEDED(hr)) {
        if (outContext != NULL) {
            *outContext = "MFCreateSourceReaderFromURL (fallback processing)";
        }
        return hr;
    }

    hr = MFCreateSourceReaderFromURL(widePath, NULL, outReader);
    if (outContext != NULL) {
        *outContext = "MFCreateSourceReaderFromURL";
    }
    return hr;
}

static HRESULT WinVideo_GetAttributeSize(IMFMediaType* type, const GUID* key, UINT32* width, UINT32* height) {
    UINT64 value = 0;
    HRESULT hr = IMFMediaType_GetUINT64(type, key, &value);
    if (FAILED(hr)) {
        return hr;
    }
    if (width != NULL) {
        *width = (UINT32)(value >> 32);
    }
    if (height != NULL) {
        *height = (UINT32)(value & 0xffffffffu);
    }
    return S_OK;
}

static HRESULT WinVideo_GetAttributeRatio(IMFMediaType* type, const GUID* key, UINT32* numerator, UINT32* denominator) {
    UINT64 value = 0;
    HRESULT hr = IMFMediaType_GetUINT64(type, key, &value);
    if (FAILED(hr)) {
        return hr;
    }
    if (numerator != NULL) {
        *numerator = (UINT32)(value >> 32);
    }
    if (denominator != NULL) {
        *denominator = (UINT32)(value & 0xffffffffu);
    }
    return S_OK;
}

static HRESULT WinVideo_SetAttributeSize(IMFMediaType* type, const GUID* key, UINT32 width, UINT32 height) {
    if (type == NULL || key == NULL) {
        return E_POINTER;
    }
    UINT64 value = ((UINT64)width << 32) | (UINT64)height;
    return IMFMediaType_SetUINT64(type, key, value);
}

static HRESULT WinVideo_SetAttributeRatio(IMFMediaType* type, const GUID* key, UINT32 numerator, UINT32 denominator) {
    if (type == NULL || key == NULL) {
        return E_POINTER;
    }
    UINT64 value = ((UINT64)numerator << 32) | (UINT64)denominator;
    return IMFMediaType_SetUINT64(type, key, value);
}

static double WinVideo_QueryDurationSeconds(IMFSourceReader* reader) {
    if (reader == NULL) {
        return 0.0;
    }

    PROPVARIANT duration;
    PropVariantInit(&duration);
    double seconds = 0.0;
    HRESULT hr = IMFSourceReader_GetPresentationAttribute(reader, MF_SOURCE_READER_MEDIASOURCE, &MF_PD_DURATION, &duration);
    if (SUCCEEDED(hr)) {
        if (duration.vt == VT_UI8) {
            seconds = (double)duration.uhVal.QuadPart / 10000000.0;
        } else if (duration.vt == VT_I8) {
            seconds = (double)duration.hVal.QuadPart / 10000000.0;
        } else if (duration.vt == VT_R8) {
            seconds = duration.dblVal;
        }
    }
    PropVariantClear(&duration);

    if (seconds < 0.0) {
        seconds = 0.0;
    }
    return seconds;
}

static WCHAR* WinVideo_DuplicateWide(const char* utf8) {
    if (utf8 == NULL) {
        return NULL;
    }
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    if (len <= 0) {
        return NULL;
    }
    WCHAR* buffer = (WCHAR*)malloc((size_t)len * sizeof(WCHAR));
    if (buffer == NULL) {
        return NULL;
    }
    if (MultiByteToWideChar(CP_UTF8, 0, utf8, -1, buffer, len) <= 0) {
        free(buffer);
        return NULL;
    }
    return buffer;
}

int WinVideo_GlobalInit(void) {
    if (!gVideoInitialized) {
        HRESULT hr = S_OK;

        WinVideo_EnsurePerfTimer();

        if (!gComInitialized) {
            hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
            if (SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE) {
                gComInitialized = 1;
                gComInitHr = hr;
            } else {
                WinVideo_SetLastError(hr, "CoInitializeEx");
                gComInitHr = hr;
                gVideoInitResult = 0;
                return 0;
            }
        }

        hr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
        if (FAILED(hr)) {
            hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
        }

        if (SUCCEEDED(hr) || hr == MF_E_ALREADY_INITIALIZED) {
            gVideoInitialized = 1;
            gVideoInitResult = 1;
        } else {
            WinVideo_SetLastError(hr, "MFStartup");
            gVideoInitialized = 0;
            gVideoInitResult = 0;
        }
    }
    return gVideoInitResult;
}

void WinVideo_GlobalShutdown(void) {
    if (gVideoInitialized) {
        MFShutdown();
        gVideoInitialized = 0;
        gVideoInitResult = 0;
    }

    if (gComInitialized) {
        if (SUCCEEDED(gComInitHr)) {
            CoUninitialize();
        }
        gComInitialized = 0;
        gComInitHr = S_OK;
    }
}

static int WinVideo_ReadFrame(struct WinVideoPlayer* player);

WinVideoPlayer* WinVideo_Load(const char* filePath) {
    WinVideo_ClearLastError();

    if (filePath == NULL) {
        WinVideo_SetLastError(E_POINTER, "WinVideo_Load path");
        return NULL;
    }

    if (!WinVideo_GlobalInit()) {
        if (FAILED(gComInitHr)) {
            WinVideo_SetLastError(gComInitHr, "WinVideo_GlobalInit (COM)");
        } else {
            WinVideo_SetLastError(MF_E_NOT_INITIALIZED, "WinVideo_GlobalInit");
        }
        return NULL;
    }

    WCHAR* widePath = WinVideo_DuplicateWide(filePath);
    if (widePath == NULL) {
        WinVideo_SetLastError(E_OUTOFMEMORY, "Path conversion");
        return NULL;
    }

    IMFSourceReader* reader = NULL;
    const char* readerContext = NULL;
    HRESULT createHr = WinVideo_CreateReader(widePath, &reader, &readerContext);
    free(widePath);
    if (FAILED(createHr)) {
        WinVideo_SetLastError(createHr, readerContext);
        SAFE_RELEASE(reader);
        return NULL;
    }

    IMFSourceReader_SetStreamSelection(reader, MF_SOURCE_READER_ALL_STREAMS, FALSE);
    IMFSourceReader_SetStreamSelection(reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);

    UINT32 targetWidth = 0;
    UINT32 targetHeight = 0;
    int haveTargetSize = 0;
    int allowScaledAttempt = 0;

    IMFMediaType* sizingType = NULL;
    HRESULT hr = IMFSourceReader_GetNativeMediaType(reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &sizingType);
    if (SUCCEEDED(hr)) {
        UINT32 nativeWidth = 0;
        UINT32 nativeHeight = 0;
        if (SUCCEEDED(WinVideo_GetAttributeSize(sizingType, &MF_MT_FRAME_SIZE, &nativeWidth, &nativeHeight)) &&
            nativeWidth > 0 && nativeHeight > 0) {
            targetWidth = nativeWidth;
            targetHeight = nativeHeight;
            haveTargetSize = 1;
            if (nativeWidth > WINVIDEO_MAX_DECODE_WIDTH || nativeHeight > WINVIDEO_MAX_DECODE_HEIGHT) {
                float scaleW = (float)WINVIDEO_MAX_DECODE_WIDTH / (float)nativeWidth;
                float scaleH = (float)WINVIDEO_MAX_DECODE_HEIGHT / (float)nativeHeight;
                float scale = (scaleW < scaleH) ? scaleW : scaleH;
                if (scale < 1.0f) {
                    UINT32 scaledW = (UINT32)((float)nativeWidth * scale);
                    UINT32 scaledH = (UINT32)((float)nativeHeight * scale);
                    if (scaledW < 2u) scaledW = 2u;
                    if (scaledH < 2u) scaledH = 2u;
                    if ((scaledW & 1u) != 0u) scaledW -= 1u;
                    if ((scaledH & 1u) != 0u) scaledH -= 1u;
                    if (scaledW > 0u && scaledH > 0u && (scaledW != nativeWidth || scaledH != nativeHeight)) {
                        targetWidth = scaledW;
                        targetHeight = scaledH;
                        allowScaledAttempt = 1;
                    }
                }
            }
        }
    }
    SAFE_RELEASE(sizingType);

#if defined(MFVideoFormat_BGRA32)
    const GUID* desiredFormats[] = {
    &MFVideoFormat_RGB32,
    &MFVideoFormat_ARGB32,
    &MFVideoFormat_BGRA32,
#ifdef MFVideoFormat_YUY2
    &MFVideoFormat_YUY2,
#endif
#ifdef MFVideoFormat_NV12
    &MFVideoFormat_NV12,
#endif
    &MFVideoFormat_RGB24
    };
    const int desiredBytesPerPixel[] = {
    4,
    4,
    4,
#ifdef MFVideoFormat_YUY2
    2,
#endif
#ifdef MFVideoFormat_NV12
    1,
#endif
    3
    };
    const char* desiredLabels[] = {
    "RGB32",
    "ARGB32",
    "BGRA32",
#ifdef MFVideoFormat_YUY2
    "YUY2",
#endif
#ifdef MFVideoFormat_NV12
    "NV12",
#endif
    "RGB24"
    };
#else
    const GUID* desiredFormats[] = {
    &MFVideoFormat_RGB32,
    &MFVideoFormat_ARGB32,
#ifdef MFVideoFormat_YUY2
    &MFVideoFormat_YUY2,
#endif
#ifdef MFVideoFormat_NV12
    &MFVideoFormat_NV12,
#endif
    &MFVideoFormat_RGB24
    };
    const int desiredBytesPerPixel[] = {
    4,
    4,
#ifdef MFVideoFormat_YUY2
    2,
#endif
#ifdef MFVideoFormat_NV12
    1,
#endif
    3
    };
    const char* desiredLabels[] = {
    "RGB32",
    "ARGB32",
#ifdef MFVideoFormat_YUY2
    "YUY2",
#endif
#ifdef MFVideoFormat_NV12
    "NV12",
#endif
    "RGB24"
    };
#endif
    const size_t desiredCount = sizeof(desiredFormats) / sizeof(desiredFormats[0]);
    int selectedIndex = -1;
    HRESULT lastSetHr = E_FAIL;
    const char* lastLabel = desiredLabels[0];

    for (size_t i = 0; i < desiredCount; ++i) {
        int attemptCount = (allowScaledAttempt && haveTargetSize) ? 2 : 1;
        for (int attempt = 0; attempt < attemptCount; ++attempt) {
            int useScaling = (attempt == 0 && allowScaledAttempt && haveTargetSize);
            IMFMediaType* mediaType = NULL;
            hr = MFCreateMediaType(&mediaType);
            if (FAILED(hr)) {
                lastSetHr = hr;
                SAFE_RELEASE(mediaType);
                continue;
            }

            hr = IMFMediaType_SetGUID(mediaType, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
            if (SUCCEEDED(hr)) {
                hr = IMFMediaType_SetGUID(mediaType, &MF_MT_SUBTYPE, desiredFormats[i]);
            }

            if (SUCCEEDED(hr) && useScaling) {
                hr = WinVideo_SetAttributeSize(mediaType, &MF_MT_FRAME_SIZE, targetWidth, targetHeight);
                if (SUCCEEDED(hr)) {
                    hr = WinVideo_SetAttributeRatio(mediaType, &MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
                }
            }

            if (SUCCEEDED(hr)) {
                lastLabel = desiredLabels[i];
                hr = IMFSourceReader_SetCurrentMediaType(reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, mediaType);
            }

            SAFE_RELEASE(mediaType);

            if (SUCCEEDED(hr)) {
                selectedIndex = (int)i;
                break;
            }

            lastSetHr = hr;
        }

        if (selectedIndex >= 0) {
            break;
        }
    }

    if (selectedIndex < 0) {
        char context[96];
        snprintf(context, sizeof(context), "IMFSourceReader_SetCurrentMediaType (%s)", lastLabel);
        WinVideo_SetLastError(lastSetHr, context);
        SAFE_RELEASE(reader);
        return NULL;
    }

    GUID selectedSubtype = *desiredFormats[selectedIndex];
    int sourceBytesPerPixel = desiredBytesPerPixel[selectedIndex];

    IMFMediaType* nativeType = NULL;
    hr = IMFSourceReader_GetCurrentMediaType(reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, &nativeType);
    if (FAILED(hr)) {
        WinVideo_SetLastError(hr, "IMFSourceReader_GetCurrentMediaType");
        SAFE_RELEASE(reader);
        return NULL;
    }

    GUID actualSubtype = selectedSubtype;
    if (SUCCEEDED(IMFMediaType_GetGUID(nativeType, &MF_MT_SUBTYPE, &actualSubtype))) {
        selectedSubtype = actualSubtype;
        int actualBytes = WinVideo_BytesPerPixelForSubtype(&selectedSubtype);
        if (actualBytes > 0) {
            sourceBytesPerPixel = actualBytes;
        }
    }

    UINT32 width = 0;
    UINT32 height = 0;
    hr = WinVideo_GetAttributeSize(nativeType, &MF_MT_FRAME_SIZE, &width, &height);
    if (FAILED(hr) || width == 0 || height == 0) {
        WinVideo_SetLastError(hr, "MF_MT_FRAME_SIZE");
        SAFE_RELEASE(nativeType);
        SAFE_RELEASE(reader);
        return NULL;
    }

    UINT32 num = 0;
    UINT32 den = 0;
    if (FAILED(WinVideo_GetAttributeRatio(nativeType, &MF_MT_FRAME_RATE, &num, &den)) || den == 0 || num == 0) {
        num = 30;
        den = 1;
    }

    UINT32 strideValue = 0;
    LONG stride = 0;
    if (FAILED(IMFMediaType_GetUINT32(nativeType, &MF_MT_DEFAULT_STRIDE, &strideValue))) {
        hr = MFGetStrideForBitmapInfoHeader(selectedSubtype.Data1, width, &stride);
        if (FAILED(hr)) {
            stride = (LONG)width * sourceBytesPerPixel;
        }
    } else {
        stride = (LONG)strideValue;
    }

    SAFE_RELEASE(nativeType);

    int decodeWidth = (int)width;
    int decodeHeight = (int)height;
    if (decodeWidth <= 0) decodeWidth = 320;
    if (decodeHeight <= 0) decodeHeight = 180;

    int outputWidth = decodeWidth;
    int outputHeight = decodeHeight;
    if (decodeWidth > (int)WINVIDEO_MAX_DECODE_WIDTH || decodeHeight > (int)WINVIDEO_MAX_DECODE_HEIGHT) {
        float scaleW = (float)WINVIDEO_MAX_DECODE_WIDTH / (float)decodeWidth;
        float scaleH = (float)WINVIDEO_MAX_DECODE_HEIGHT / (float)decodeHeight;
        float scale = (scaleW < scaleH) ? scaleW : scaleH;
        if (scale < 1.0f) {
            outputWidth = (int)floorf((float)decodeWidth * scale);
            outputHeight = (int)floorf((float)decodeHeight * scale);
        }
    }

    if (outputWidth < 1) outputWidth = 1;
    if (outputHeight < 1) outputHeight = 1;

    float sampleStepX = (outputWidth > 0) ? ((float)decodeWidth / (float)outputWidth) : 1.0f;
    float sampleStepY = (outputHeight > 0) ? ((float)decodeHeight / (float)outputHeight) : 1.0f;

    WinVideoPlayer* player = (WinVideoPlayer*)calloc(1, sizeof(WinVideoPlayer));
    if (player == NULL) {
        WinVideo_SetLastError(E_OUTOFMEMORY, "WinVideoPlayer allocation");
        SAFE_RELEASE(reader);
        return NULL;
    }

    player->reader = reader;
    player->decodeWidth = decodeWidth;
    player->decodeHeight = decodeHeight;
    player->width = outputWidth;
    player->height = outputHeight;
    player->sampleStepX = sampleStepX;
    player->sampleStepY = sampleStepY;
    player->frameDuration = ((float)den / (float)num);
    if (player->frameDuration <= 0.0f) {
        player->frameDuration = 1.0f / 30.0f;
    }
    if (player->frameDuration < WINVIDEO_MIN_FRAME_DURATION) {
        player->frameDuration = WINVIDEO_MIN_FRAME_DURATION;
    }
    player->pixels = (unsigned char*)malloc((size_t)player->width * (size_t)player->height * 4u);
    player->stride = stride;
    player->paused = 0;
    player->timeAccumulator = 0.0f;
    player->ready = 0;
    player->endOfStream = 0;
    player->bytesPerPixel = sourceBytesPerPixel;
    player->decodedFrameCount = 0;
    player->fallbackFrameCount = 0;
    if (player->bytesPerPixel <= 0) {
        player->bytesPerPixel = 4;
    }
    WinVideo_ConfigureConversionFromSubtype(player, &selectedSubtype);
    player->durationSeconds = WinVideo_QueryDurationSeconds(reader);
    player->positionSeconds = 0.0;
    player->loop = 0;

    if (player->pixels == NULL) {
        WinVideo_SetLastError(E_OUTOFMEMORY, "Pixel buffer allocation");
        WinVideo_Unload(player);
        return NULL;
    }

    memset(player->pixels, 0, (size_t)player->width * (size_t)player->height * 4u);

    Image img = {
        .data = player->pixels,
        .width = player->width,
        .height = player->height,
        .mipmaps = 1,
        .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8
    };

    player->texture = LoadTextureFromImage(img);
    if (player->texture.id == 0) {
        WinVideo_SetLastError(E_FAIL, "LoadTextureFromImage");
        WinVideo_Unload(player);
        return NULL;
    }

    if (!WinVideo_ReadFrame(player)) {
        /* First frame failed to decode, fill with test pattern to verify texture upload works */
        for (int y = 0; y < player->height; y++) {
            for (int x = 0; x < player->width; x++) {
                size_t offset = ((size_t)y * (size_t)player->width + (size_t)x) * 4u;
                /* Create a blue-to-red gradient to verify texture is working */
                player->pixels[offset + 0] = (unsigned char)((x * 255) / player->width);  /* R */
                player->pixels[offset + 1] = 0;  /* G */
                player->pixels[offset + 2] = (unsigned char)((y * 255) / player->height);  /* B */
                player->pixels[offset + 3] = 255;  /* A */
            }
        }
        UpdateTexture(player->texture, player->pixels);
        player->ready = 1;
        player->fallbackFrameCount += 1;
    }

    return player;
}

void WinVideo_Unload(WinVideoPlayer* player) {
    if (player == NULL) {
        return;
    }

    if (player->texture.id != 0) {
        UnloadTexture(player->texture);
        player->texture = (Texture2D){0};
    }

    if (player->pixels != NULL) {
        free(player->pixels);
        player->pixels = NULL;
    }

    if (player->reader != NULL) {
        SAFE_RELEASE(player->reader);
    }

    free(player);
}

static void WinVideo_ResetToStart(WinVideoPlayer* player) {
    if (player == NULL || player->reader == NULL) {
        return;
    }
    PROPVARIANT pos;
    PropVariantInit(&pos);
    pos.vt = VT_I8;
    pos.hVal.QuadPart = 0;
    if (SUCCEEDED(IMFSourceReader_SetCurrentPosition(player->reader, &GUID_NULL, &pos))) {
        player->endOfStream = 0;
        player->positionSeconds = 0.0;
        player->timeAccumulator = 0.0f;
    }
    PropVariantClear(&pos);
}

static int WinVideo_ReadFrame(WinVideoPlayer* player) {
    if (player == NULL || player->reader == NULL || player->pixels == NULL) {
        return 0;
    }

    DWORD streamIndex = 0;
    DWORD flags = 0;
    LONGLONG timestamp = 0;
    IMFSample* sample = NULL;

    HRESULT hr = IMFSourceReader_ReadSample(player->reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &streamIndex, &flags, &timestamp, &sample);
    if (FAILED(hr)) {
        WinVideo_SetLastError(hr, "IMFSourceReader_ReadSample");
        SAFE_RELEASE(sample);
        return 0;
    }

    if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
        player->endOfStream = 1;
        if (player->durationSeconds > 0.0) {
            player->positionSeconds = player->durationSeconds;
        }
        SAFE_RELEASE(sample);
        return 0;
    }

    if (flags & MF_SOURCE_READERF_STREAMTICK) {
        SAFE_RELEASE(sample);
        return 0;
    }

    if (sample == NULL) {
        SAFE_RELEASE(sample);
        return 0;
    }

    player->endOfStream = 0;

    if (timestamp >= 0) {
        player->positionSeconds = (double)timestamp / 10000000.0;
    }

    IMFMediaBuffer* buffer = NULL;
    IMFDXGIBuffer* dxgiBuffer = NULL;
    ID3D11Texture2D* d3dTexture = NULL;
    ID3D11Device* d3dDevice = NULL;
    ID3D11DeviceContext* d3dContext = NULL;
    ID3D11Texture2D* stagingTexture = NULL;
    D3D11_MAPPED_SUBRESOURCE mapped = {0};
    int useDxgiPath = 0;
    int hadSampleData = 0;

    hr = IMFSample_ConvertToContiguousBuffer(sample, &buffer);
    if (FAILED(hr)) {
        WinVideo_SetLastError(hr, "IMFSample_ConvertToContiguousBuffer");
        SAFE_RELEASE(sample);
        return 0;
    }

    if (SUCCEEDED(IMFMediaBuffer_QueryInterface(buffer, &IID_IMFDXGIBuffer, (void**)&dxgiBuffer))) {
        hr = IMFDXGIBuffer_GetResource(dxgiBuffer, &IID_ID3D11Texture2D, (void**)&d3dTexture);
        if (SUCCEEDED(hr) && d3dTexture != NULL) {
            d3dTexture->lpVtbl->GetDevice(d3dTexture, &d3dDevice);
            if (d3dDevice != NULL) {
                d3dDevice->lpVtbl->GetImmediateContext(d3dDevice, &d3dContext);
            }

            if (d3dDevice != NULL && d3dContext != NULL) {
                D3D11_TEXTURE2D_DESC desc;
                d3dTexture->lpVtbl->GetDesc(d3dTexture, &desc);
                desc.BindFlags = 0;
                desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                desc.Usage = D3D11_USAGE_STAGING;
                desc.MiscFlags = 0;

                hr = d3dDevice->lpVtbl->CreateTexture2D(d3dDevice, &desc, NULL, &stagingTexture);
                if (SUCCEEDED(hr) && stagingTexture != NULL) {
                    d3dContext->lpVtbl->CopyResource(d3dContext, (ID3D11Resource*)stagingTexture, (ID3D11Resource*)d3dTexture);
                    hr = d3dContext->lpVtbl->Map(d3dContext, (ID3D11Resource*)stagingTexture, 0, D3D11_MAP_READ, 0, &mapped);
                    if (SUCCEEDED(hr)) {
                        useDxgiPath = 1;
                    }
                }
            }
        }
    }

    BYTE* data = NULL;
    DWORD maxLength = 0;
    DWORD currentLength = 0;
    if (!useDxgiPath) {
        hr = IMFMediaBuffer_Lock(buffer, &data, &maxLength, &currentLength);
        if (FAILED(hr)) {
            data = NULL;
        }
    } else {
        if (mapped.pData != NULL) {
            maxLength = mapped.RowPitch * (UINT)player->decodeHeight;
            currentLength = maxLength;
            hr = S_OK;
        } else {
            hr = E_POINTER;
        }
    }

    if ((useDxgiPath && mapped.pData != NULL) || (!useDxgiPath && SUCCEEDED(hr) && data != NULL && currentLength > 0)) {
        WinVideoSampleFormat sampleFormat = player->sampleFormat;
        double convertStartSeconds = WinVideo_GetSeconds();
        int destWidth = (player->width > 0) ? player->width : 1;
        int destHeight = (player->height > 0) ? player->height : 1;
        int decodeWidth = (player->decodeWidth > 0) ? player->decodeWidth : destWidth;
        int decodeHeight = (player->decodeHeight > 0) ? player->decodeHeight : destHeight;
        LONG stride = useDxgiPath ? (LONG)mapped.RowPitch : player->stride;
        if (stride == 0) {
            if (sampleFormat == WINVIDEO_SAMPLE_FORMAT_NV12) {
                stride = (LONG)decodeWidth;
            } else if (sampleFormat == WINVIDEO_SAMPLE_FORMAT_YUY2) {
                stride = (LONG)decodeWidth * 2;
            } else {
                int sourceBytesGuess = (player->bytesPerPixel > 0) ? player->bytesPerPixel : 4;
                stride = (LONG)decodeWidth * sourceBytesGuess;
            }
        }

        LONG strideAbs = (stride >= 0) ? stride : -stride;
        if (strideAbs == 0) {
            if (sampleFormat == WINVIDEO_SAMPLE_FORMAT_NV12) {
                strideAbs = (LONG)decodeWidth;
            } else if (sampleFormat == WINVIDEO_SAMPLE_FORMAT_YUY2) {
                strideAbs = (LONG)decodeWidth * 2;
            } else {
                int sourceBytesGuess = (player->bytesPerPixel > 0) ? player->bytesPerPixel : 4;
                strideAbs = (LONG)decodeWidth * sourceBytesGuess;
            }
        }

        size_t bufferSize = 0;
        if (useDxgiPath) {
            bufferSize = (size_t)mapped.RowPitch * (size_t)decodeHeight;
            if (sampleFormat == WINVIDEO_SAMPLE_FORMAT_NV12) {
                bufferSize += (size_t)mapped.RowPitch * (size_t)((decodeHeight + 1) / 2);
            }
        } else {
            bufferSize = (size_t)currentLength;
        }

        const unsigned char* basePtr = useDxgiPath ? (const unsigned char*)mapped.pData : data;
        const unsigned char* topRow = basePtr;
        if (!useDxgiPath && stride < 0 && decodeHeight > 0) {
            size_t offset = (size_t)(decodeHeight - 1) * (size_t)strideAbs;
            if (offset < bufferSize) {
                topRow = basePtr + offset;
            } else if (bufferSize >= (size_t)strideAbs) {
                topRow = basePtr + (bufferSize - (size_t)strideAbs);
            }
        }

        unsigned char* dst = player->pixels;
        float stepX = (player->sampleStepX > 0.0f) ? player->sampleStepX : 1.0f;
        float stepY = (player->sampleStepY > 0.0f) ? player->sampleStepY : 1.0f;
        if (stepX <= 0.0f) stepX = 1.0f;
        if (stepY <= 0.0f) stepY = 1.0f;

        int useDirectCopy = (decodeWidth == destWidth && decodeHeight == destHeight &&
                              fabsf(stepX - 1.0f) < 0.0005f && fabsf(stepY - 1.0f) < 0.0005f);
        int touched = 0;

        if (sampleFormat == WINVIDEO_SAMPLE_FORMAT_PACKED) {
            int sourceBytes = (player->bytesPerPixel > 0) ? player->bytesPerPixel : 4;
            if (sourceBytes < 3) {
                size_t total = (size_t)destWidth * (size_t)destHeight * 4u;
                memset(dst, 0, total);
                touched = 1;
            } else {
                int sourceIsBgr = player->sourceChannelsAreBgr;
                int sourceHasAlpha = (player->sourceHasAlpha && sourceBytes >= 4) ? 1 : 0;
                int forceOpaque = player->forceOpaqueAlpha;
                float srcYPos = 0.0f;

                for (int y = 0; y < destHeight; ++y) {
                    int srcY = useDirectCopy ? y : (int)srcYPos;
                    if (srcY < 0) srcY = 0;
                    if (srcY >= decodeHeight) srcY = decodeHeight - 1;

                    const unsigned char* rowPtr = NULL;
                    if (stride >= 0) {
                        size_t offset = (size_t)srcY * (size_t)stride;
                        if (offset < bufferSize) {
                            rowPtr = topRow + offset;
                        }
                    } else {
                        size_t offset = (size_t)srcY * (size_t)strideAbs;
                        if (topRow >= basePtr + offset) {
                            rowPtr = topRow - offset;
                        }
                    }

                    unsigned char* dstRow = dst + (size_t)y * (size_t)destWidth * 4u;

                    if (rowPtr == NULL) {
                        memset(dstRow, 0, (size_t)destWidth * 4u);
                        touched = 1;
                        if (!useDirectCopy) srcYPos += stepY;
                        continue;
                    }

                    size_t rowOffset = (size_t)(rowPtr - basePtr);
                    if (rowOffset >= bufferSize) {
                        memset(dstRow, 0, (size_t)destWidth * 4u);
                        touched = 1;
                        if (!useDirectCopy) srcYPos += stepY;
                        continue;
                    }

                    size_t available = bufferSize - rowOffset;
                    int maxSrcPixels = (int)(available / (size_t)sourceBytes);
                    if (maxSrcPixels > decodeWidth) {
                        maxSrcPixels = decodeWidth;
                    }
                    if (maxSrcPixels <= 0) {
                        memset(dstRow, 0, (size_t)destWidth * 4u);
                        touched = 1;
                        if (!useDirectCopy) srcYPos += stepY;
                        continue;
                    }

                    hadSampleData = 1;

                    if (useDirectCopy) {
                        int pixelsToCopy = destWidth;
                        if (pixelsToCopy > maxSrcPixels) {
                            pixelsToCopy = maxSrcPixels;
                        }
                        const unsigned char* srcPx = rowPtr;
                        unsigned char* dstPx = dstRow;
                        for (int x = 0; x < pixelsToCopy; ++x) {
                            unsigned char r = sourceIsBgr ? srcPx[2] : srcPx[0];
                            unsigned char g = srcPx[1];
                            unsigned char b = sourceIsBgr ? srcPx[0] : srcPx[2];
                            unsigned char a = 255;
                            if (sourceHasAlpha) {
                                a = srcPx[3];
                            }
                            if (forceOpaque && a == 0) {
                                a = 255;
                            }
                            dstPx[0] = r;
                            dstPx[1] = g;
                            dstPx[2] = b;
                            dstPx[3] = a;
                            srcPx += sourceBytes;
                            dstPx += 4;
                        }
                        if (pixelsToCopy < destWidth) {
                            size_t remaining = (size_t)(destWidth - pixelsToCopy) * 4u;
                            memset(dstRow + (size_t)pixelsToCopy * 4u, 0, remaining);
                        }
                    } else {
                        float srcXPos = 0.0f;
                        for (int x = 0; x < destWidth; ++x) {
                            int srcX = (int)srcXPos;
                            if (srcX < 0) srcX = 0;
                            if (srcX >= maxSrcPixels) srcX = maxSrcPixels - 1;

                            const unsigned char* srcPx = rowPtr + (size_t)srcX * (size_t)sourceBytes;
                            unsigned char* dstPx = dstRow + (size_t)x * 4u;
                            unsigned char r = sourceIsBgr ? srcPx[2] : srcPx[0];
                            unsigned char g = srcPx[1];
                            unsigned char b = sourceIsBgr ? srcPx[0] : srcPx[2];
                            unsigned char a = 255;
                            if (sourceHasAlpha) {
                                a = srcPx[3];
                            }
                            if (forceOpaque && a == 0) {
                                a = 255;
                            }
                            dstPx[0] = r;
                            dstPx[1] = g;
                            dstPx[2] = b;
                            dstPx[3] = a;

                            srcXPos += stepX;
                        }
                    }

                    touched = 1;
                    if (!useDirectCopy) srcYPos += stepY;
                }
            }
        } else if (sampleFormat == WINVIDEO_SAMPLE_FORMAT_NV12) {
            if (stride < 0) {
                size_t total = (size_t)destWidth * (size_t)destHeight * 4u;
                memset(dst, 0, total);
                touched = 1;
            } else {
                size_t yPlaneSize = (size_t)strideAbs * (size_t)decodeHeight;
                size_t uvPlaneHeight = (size_t)((decodeHeight + 1) / 2);
                size_t uvPlaneSize = (size_t)strideAbs * uvPlaneHeight;
                if (bufferSize < yPlaneSize + uvPlaneSize) {
                    size_t total = (size_t)destWidth * (size_t)destHeight * 4u;
                    memset(dst, 0, total);
                    touched = 1;
                } else {
                    const unsigned char* yPlaneTop = topRow;
                    const unsigned char* uvPlaneBase = basePtr + yPlaneSize;
                    size_t expectedUvWidth = (size_t)((decodeWidth + 1) & ~1);
                    if (expectedUvWidth == 0) {
                        expectedUvWidth = 2;
                    }

                    float srcYPos = 0.0f;
                    for (int y = 0; y < destHeight; ++y) {
                        int srcY = useDirectCopy ? y : (int)srcYPos;
                        if (srcY < 0) srcY = 0;
                        if (srcY >= decodeHeight) srcY = decodeHeight - 1;

                        size_t yOffset = (size_t)srcY * (size_t)strideAbs;
                        const unsigned char* yRow = (yOffset < yPlaneSize) ? (yPlaneTop + yOffset) : NULL;
                        unsigned char* dstRow = dst + (size_t)y * (size_t)destWidth * 4u;

                        if (yRow == NULL) {
                            memset(dstRow, 0, (size_t)destWidth * 4u);
                            touched = 1;
                            if (!useDirectCopy) srcYPos += stepY;
                            continue;
                        }

                        int chromaY = srcY / 2;
                        size_t uvOffset = (size_t)chromaY * (size_t)strideAbs;
                        const unsigned char* uvRow = (uvOffset < uvPlaneSize) ? (uvPlaneBase + uvOffset) : NULL;
                        if (uvRow == NULL) {
                            memset(dstRow, 0, (size_t)destWidth * 4u);
                            touched = 1;
                            if (!useDirectCopy) srcYPos += stepY;
                            continue;
                        }

                        size_t yRowLimit = (size_t)strideAbs;
                        if (yRowLimit == 0) {
                            yRowLimit = (size_t)decodeWidth;
                        }

                        size_t uvRowLimit = (size_t)strideAbs;
                        if (uvRowLimit > expectedUvWidth) {
                            uvRowLimit = expectedUvWidth;
                        }
                        if (uvRowLimit == 0) {
                            uvRowLimit = 2;
                        }

                        float srcXPos = 0.0f;
                        for (int x = 0; x < destWidth; ++x) {
                            int srcX = useDirectCopy ? x : (int)srcXPos;
                            if (!useDirectCopy) {
                                srcXPos += stepX;
                            }
                            if (srcX < 0) srcX = 0;
                            if (srcX >= decodeWidth) srcX = decodeWidth - 1;

                            size_t yIndex = (size_t)srcX;
                            if (yIndex >= yRowLimit) {
                                if (yRowLimit > 0) {
                                    yIndex = yRowLimit - 1u;
                                } else {
                                    yIndex = 0;
                                }
                            }
                            unsigned char yValue = yRow[yIndex];

                            size_t uvIndex = (size_t)(srcX / 2) * 2u;
                            if (uvIndex + 1 >= uvRowLimit) {
                                if (uvRowLimit >= 2) {
                                    uvIndex = uvRowLimit - 2;
                                } else {
                                    uvIndex = 0;
                                }
                            }
                            unsigned char uValue = uvRow[uvIndex + 0];
                            unsigned char vValue = uvRow[uvIndex + 1];

                            unsigned char* dstPx = dstRow + (size_t)x * 4u;
                            WinVideo_YuvToRgba(yValue, uValue, vValue, dstPx);
                            if (player->forceOpaqueAlpha && dstPx[3] == 0) {
                                dstPx[3] = 255;
                            }
                        }

                        touched = 1;
                        hadSampleData = 1;
                        if (!useDirectCopy) srcYPos += stepY;
                    }
                }
            }
        } else if (sampleFormat == WINVIDEO_SAMPLE_FORMAT_YUY2) {
            float srcYPos = 0.0f;

            for (int y = 0; y < destHeight; ++y) {
                int srcY = useDirectCopy ? y : (int)srcYPos;
                if (srcY < 0) srcY = 0;
                if (srcY >= decodeHeight) srcY = decodeHeight - 1;

                const unsigned char* rowPtr = NULL;
                if (stride >= 0) {
                    size_t offset = (size_t)srcY * (size_t)stride;
                    if (offset < bufferSize) {
                        rowPtr = topRow + offset;
                    }
                } else {
                    size_t offset = (size_t)srcY * (size_t)strideAbs;
                    if (topRow >= basePtr + offset) {
                        rowPtr = topRow - offset;
                    }
                }

                unsigned char* dstRow = dst + (size_t)y * (size_t)destWidth * 4u;

                if (rowPtr == NULL) {
                    memset(dstRow, 0, (size_t)destWidth * 4u);
                    touched = 1;
                    if (!useDirectCopy) srcYPos += stepY;
                    continue;
                }

                size_t rowOffset = (size_t)(rowPtr - basePtr);
                if (rowOffset >= bufferSize) {
                    memset(dstRow, 0, (size_t)destWidth * 4u);
                    touched = 1;
                    if (!useDirectCopy) srcYPos += stepY;
                    continue;
                }

                size_t rowAvailable = bufferSize - rowOffset;
                size_t rowLimit = (size_t)strideAbs;
                if (rowLimit > rowAvailable) {
                    rowLimit = rowAvailable;
                }
                if (rowLimit < 4) {
                    memset(dstRow, 0, (size_t)destWidth * 4u);
                    touched = 1;
                    if (!useDirectCopy) srcYPos += stepY;
                    continue;
                }

                hadSampleData = 1;
                float srcXPos = 0.0f;

                for (int x = 0; x < destWidth; ++x) {
                    int srcX = useDirectCopy ? x : (int)srcXPos;
                    if (!useDirectCopy) {
                        srcXPos += stepX;
                    }
                    if (srcX < 0) srcX = 0;
                    if (srcX >= decodeWidth) srcX = decodeWidth - 1;

                    size_t pairIndex = (size_t)(srcX >> 1);
                    size_t byteIndex = pairIndex * 4u;
                    if (byteIndex + 3 >= rowLimit) {
                        if (rowLimit >= 4) {
                            byteIndex = rowLimit - 4;
                        } else {
                            byteIndex = 0;
                        }
                    }

                    const unsigned char* pair = rowPtr + byteIndex;
                    unsigned char yValue = (srcX & 1) ? pair[2] : pair[0];
                    unsigned char uValue = pair[1];
                    unsigned char vValue = pair[3];

                    unsigned char* dstPx = dstRow + (size_t)x * 4u;
                    WinVideo_YuvToRgba(yValue, uValue, vValue, dstPx);
                    if (player->forceOpaqueAlpha && dstPx[3] == 0) {
                        dstPx[3] = 255;
                    }
                }

                touched = 1;
                if (!useDirectCopy) srcYPos += stepY;
            }
        } else {
            size_t total = (size_t)destWidth * (size_t)destHeight * 4u;
            memset(dst, 0, total);
            touched = 1;
        }

        if (touched && player->pixels != NULL && player->texture.id != 0) {
            UpdateTexture(player->texture, player->pixels);
            player->ready = 1;
            if (hadSampleData) {
                player->decodedFrameCount += 1;
            } else {
                player->fallbackFrameCount += 1;
            }
        }
        WinVideo_RecordConvertTime(player, WinVideo_ElapsedSeconds(convertStartSeconds), hadSampleData);
        if (!useDxgiPath) {
            IMFMediaBuffer_Unlock(buffer);
        } else if (d3dContext != NULL && stagingTexture != NULL) {
            d3dContext->lpVtbl->Unmap(d3dContext, (ID3D11Resource*)stagingTexture, 0);
        }
    } else {
        if (FAILED(hr)) {
            WinVideo_SetLastError(hr, "IMFMediaBuffer_Lock");
        }
    }

    SAFE_RELEASE(stagingTexture);
    SAFE_RELEASE(d3dContext);
    SAFE_RELEASE(d3dDevice);
    SAFE_RELEASE(d3dTexture);
    SAFE_RELEASE(dxgiBuffer);

    SAFE_RELEASE(buffer);
    SAFE_RELEASE(sample);

    return player->ready;
}

void WinVideo_Update(WinVideoPlayer* player, float deltaSeconds) {
    if (player == NULL || player->paused) {
        return;
    }

    if (player->frameDuration <= 0.0f) {
        player->frameDuration = 1.0f / 30.0f;
    }

    player->timeAccumulator += deltaSeconds;

    float frameDuration = player->frameDuration;
    int steps = 0;
    const int maxSteps = WINVIDEO_MAX_FRAME_STEPS;

    while (player->timeAccumulator >= frameDuration && steps < maxSteps) {
        player->timeAccumulator -= frameDuration;
        if (!WinVideo_ReadFrame(player)) {
            if (player->endOfStream) {
                if (player->loop) {
                    WinVideo_ResetToStart(player);
                    WinVideo_ReadFrame(player);
                } else {
                    player->paused = 1;
                    player->timeAccumulator = 0.0f;
                    if (player->durationSeconds > 0.0) {
                        player->positionSeconds = player->durationSeconds;
                    }
                }
            } else {
                break;
            }
            break;
        }
        steps++;
    }

    if (frameDuration > 0.0f && player->timeAccumulator > frameDuration * (float)maxSteps) {
        player->timeAccumulator = fmodf(player->timeAccumulator, frameDuration);
    }
}

Texture2D* WinVideo_GetTexture(WinVideoPlayer* player) {
    if (player == NULL) {
        return NULL;
    }
    return &player->texture;
}

int WinVideo_IsReady(const WinVideoPlayer* player) {
    return (player != NULL) ? player->ready : 0;
}

const char* WinVideo_GetLastError(void) {
    return (gVideoLastError[0] != '\0') ? gVideoLastError : NULL;
}
void WinVideo_SetPaused(WinVideoPlayer* player, int paused) {
    if (player == NULL) {
        return;
    }
    int newPaused = paused ? 1 : 0;
    int wasPaused = player->paused;
    player->paused = newPaused;
    if (!player->paused && player->endOfStream) {
        WinVideo_ResetToStart(player);
        WinVideo_ReadFrame(player);
        player->timeAccumulator = 0.0f;
    } else if (wasPaused && !player->paused) {
        player->timeAccumulator = 0.0f;
    }
}

int WinVideo_IsPaused(const WinVideoPlayer* player) {
    return (player != NULL) ? player->paused : 1;
}

void WinVideo_Rewind(WinVideoPlayer* player) {
    if (player == NULL) {
        return;
    }
    WinVideo_ResetToStart(player);
    WinVideo_ReadFrame(player);
}

int WinVideo_GetDecodedFrameCount(const WinVideoPlayer* player) {
    return (player != NULL) ? player->decodedFrameCount : 0;
}

int WinVideo_GetFallbackFrameCount(const WinVideoPlayer* player) {
    return (player != NULL) ? player->fallbackFrameCount : 0;
}

double WinVideo_GetConvertCpuAverageMicros(const WinVideoPlayer* player) {
    if (player == NULL || player->convertCpuSampleCount == 0u) {
        return 0.0;
    }

    double averageSeconds = player->convertCpuSecondsAccum / (double)player->convertCpuSampleCount;
    return averageSeconds * 1000000.0;
}

double WinVideo_GetConvertCpuPeakMicros(const WinVideoPlayer* player) {
    if (player == NULL) {
        return 0.0;
    }
    return player->convertCpuSecondsPeak * 1000000.0;
}

double WinVideo_GetConvertCpuLastMicros(const WinVideoPlayer* player) {
    if (player == NULL) {
        return 0.0;
    }
    return player->convertCpuSecondsLast * 1000000.0;
}

unsigned int WinVideo_GetConvertCpuSampleCount(const WinVideoPlayer* player) {
    return (player != NULL) ? player->convertCpuSampleCount : 0u;
}

const char* WinVideo_GetSampleFormatLabel(const WinVideoPlayer* player) {
    if (player == NULL) {
        return "Unknown";
    }

    switch (player->sampleFormat) {
        case WINVIDEO_SAMPLE_FORMAT_PACKED: return "Packed";
        case WINVIDEO_SAMPLE_FORMAT_NV12: return "NV12";
        case WINVIDEO_SAMPLE_FORMAT_YUY2: return "YUY2";
        default: break;
    }
    return "Unknown";
}

double WinVideo_GetDurationSeconds(const WinVideoPlayer* player) {
    if (player == NULL) {
        return 0.0;
    }
    return (player->durationSeconds > 0.0) ? player->durationSeconds : 0.0;
}

double WinVideo_GetPositionSeconds(const WinVideoPlayer* player) {
    if (player == NULL) {
        return 0.0;
    }
    if (player->endOfStream && player->durationSeconds > 0.0 && !player->loop) {
        return player->durationSeconds;
    }
    return (player->positionSeconds >= 0.0) ? player->positionSeconds : 0.0;
}

void WinVideo_SetPositionSeconds(WinVideoPlayer* player, double seconds) {
    if (player == NULL || player->reader == NULL) {
        return;
    }

    if (seconds < 0.0) {
        seconds = 0.0;
    }

    double duration = player->durationSeconds;
    if (duration > 0.0 && seconds > duration) {
        seconds = duration;
    }

    LONGLONG ticks = (LONGLONG)llround(seconds * 10000000.0);

    PROPVARIANT pos;
    PropVariantInit(&pos);
    pos.vt = VT_I8;
    pos.hVal.QuadPart = ticks;
    HRESULT hr = IMFSourceReader_SetCurrentPosition(player->reader, &GUID_NULL, &pos);
    PropVariantClear(&pos);

    if (SUCCEEDED(hr)) {
        player->endOfStream = 0;
        player->timeAccumulator = 0.0f;
        player->positionSeconds = seconds;
        WinVideo_ReadFrame(player);
        if (player->paused) {
            player->timeAccumulator = 0.0f;
        }
    } else {
        WinVideo_SetLastError(hr, "IMFSourceReader_SetCurrentPosition");
    }
}

void WinVideo_SetLooping(WinVideoPlayer* player, int loop) {
    if (player == NULL) {
        return;
    }
    player->loop = loop ? 1 : 0;
}

int WinVideo_IsLooping(const WinVideoPlayer* player) {
    return (player != NULL) ? player->loop : 0;
}

#else

/* Stubs are defined inline in the header for non-Windows platforms. */

#endif /* _WIN32 */
