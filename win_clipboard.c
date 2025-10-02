#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <stdlib.h>
#include <string.h>

/* Undefine conflicting symbols after including windows.h */
#ifdef Rectangle
#undef Rectangle
#endif
#ifdef CloseWindow
#undef CloseWindow
#endif
#ifdef ShowCursor
#undef ShowCursor
#endif
#ifdef LoadImage
#undef LoadImage
#endif
#ifdef DrawText
#undef DrawText
#endif

#include "win_clipboard.h"

static char* WinClip_WideToUtf8(const WCHAR* wideStr) {
    if (wideStr == NULL) {
        return NULL;
    }

    int required = WideCharToMultiByte(CP_UTF8, 0, wideStr, -1, NULL, 0, NULL, NULL);
    if (required <= 0) {
        return NULL;
    }

    char* utf8 = (char*)malloc((size_t)required);
    if (utf8 == NULL) {
        return NULL;
    }

    int written = WideCharToMultiByte(CP_UTF8, 0, wideStr, -1, utf8, required, NULL, NULL);
    if (written <= 0) {
        free(utf8);
        return NULL;
    }

    return utf8;
}

int WinClip_HasText(void) {
    if (!OpenClipboard(NULL)) {
        return 0;
    }

    int hasText = IsClipboardFormatAvailable(CF_TEXT) ||
                  IsClipboardFormatAvailable(CF_UNICODETEXT);

    CloseClipboard();
    return hasText;
}

int WinClip_HasImage(void) {
    if (!OpenClipboard(NULL)) {
        return 0;
    }

    int hasImage = IsClipboardFormatAvailable(CF_BITMAP) ||
                   IsClipboardFormatAvailable(CF_DIB) ||
                   IsClipboardFormatAvailable(CF_DIBV5);

    CloseClipboard();
    return hasImage;
}

int WinClip_HasFileDrop(void) {
    if (!OpenClipboard(NULL)) {
        return 0;
    }

    int hasDrop = IsClipboardFormatAvailable(CF_HDROP);

    CloseClipboard();
    return hasDrop;
}

char* WinClip_GetText(void) {
    if (!WinClip_HasText()) {
        return NULL;
    }

    if (!OpenClipboard(NULL)) {
        return NULL;
    }

    HANDLE hData = GetClipboardData(CF_TEXT);
    if (hData == NULL) {
        CloseClipboard();
        return NULL;
    }

    char* clipText = (char*)GlobalLock(hData);
    if (clipText == NULL) {
        CloseClipboard();
        return NULL;
    }

    /* Create a copy of the text */
    char* result = malloc(strlen(clipText) + 1);
    if (result != NULL) {
        strcpy(result, clipText);
    }

    GlobalUnlock(hData);
    CloseClipboard();

    return result;
}

void* WinClip_GetImageData(int* width, int* height, int* channels) {
    if (!WinClip_HasImage()) {
        return NULL;
    }

    if (!OpenClipboard(NULL)) {
        return NULL;
    }

    HANDLE hData = GetClipboardData(CF_DIB);
    if (hData == NULL) {
        CloseClipboard();
        return NULL;
    }

    BITMAPINFO* bmi = (BITMAPINFO*)GlobalLock(hData);
    if (bmi == NULL) {
        CloseClipboard();
        return NULL;
    }

    /* Get image dimensions */
    *width = bmi->bmiHeader.biWidth;
    *height = abs(bmi->bmiHeader.biHeight);
    *channels = 4; /* RGBA */

    /* Get the actual bitmap data */
    unsigned char* dibData = (unsigned char*)bmi + bmi->bmiHeader.biSize;

    /* Handle color table if present (for <= 8 bpp) */
    if (bmi->bmiHeader.biBitCount <= 8) {
        int numColors = bmi->bmiHeader.biClrUsed;
        if (numColors == 0) {
            numColors = 1 << bmi->bmiHeader.biBitCount;
        }
        dibData += numColors * sizeof(RGBQUAD);
    }

    /* Calculate the size of the RGBA output data */
    int imageSize = (*width) * (*height) * (*channels);

    /* Allocate memory for RGBA data */
    unsigned char* imageData = (unsigned char*)malloc(imageSize);
    if (imageData == NULL) {
        GlobalUnlock(hData);
        CloseClipboard();
        return NULL;
    }

    /* Convert DIB to RGBA format based on bit depth */
    int bpp = bmi->bmiHeader.biBitCount;
    int bytesPerRow = ((*width) * bpp + 31) / 32 * 4; /* DIB rows are padded to 32-bit boundaries */

    if (bpp == 32) {
        /* 32-bit BGRA to RGBA conversion */
        for (int y = 0; y < *height; y++) {
            int srcRow = (*height - 1 - y); /* DIB is bottom-up */
            unsigned char* src = dibData + srcRow * bytesPerRow;
            unsigned char* dst = imageData + y * (*width) * 4;

            for (int x = 0; x < *width; x++) {
                dst[x*4 + 0] = src[x*4 + 2]; /* R = B */
                dst[x*4 + 1] = src[x*4 + 1]; /* G = G */
                dst[x*4 + 2] = src[x*4 + 0]; /* B = R */
                dst[x*4 + 3] = src[x*4 + 3]; /* A = A */
            }
        }
    } else if (bpp == 24) {
        /* 24-bit BGR to RGBA conversion */
        for (int y = 0; y < *height; y++) {
            int srcRow = (*height - 1 - y); /* DIB is bottom-up */
            unsigned char* src = dibData + srcRow * bytesPerRow;
            unsigned char* dst = imageData + y * (*width) * 4;

            for (int x = 0; x < *width; x++) {
                dst[x*4 + 0] = src[x*3 + 2]; /* R = B */
                dst[x*4 + 1] = src[x*3 + 1]; /* G = G */
                dst[x*4 + 2] = src[x*3 + 0]; /* B = R */
                dst[x*4 + 3] = 255;          /* A = opaque */
            }
        }
    } else {
        /* For other bit depths, fill with a recognizable pattern */
        /* indicating unsupported format */
        for (int i = 0; i < imageSize; i += 4) {
            imageData[i] = 255;     /* R */
            imageData[i+1] = 0;     /* G */
            imageData[i+2] = 255;   /* B - magenta indicates unsupported format */
            imageData[i+3] = 255;   /* A */
        }
    }

    GlobalUnlock(hData);
    CloseClipboard();

    return imageData;
}

void WinClip_FreeData(void* data) {
    if (data != NULL) {
        free(data);
    }
}

int WinClip_SetImageRGBA(const unsigned char* data, int width, int height) {
    if (data == NULL || width <= 0 || height <= 0) {
        return 0;
    }

    if (!OpenClipboard(NULL)) {
        return 0;
    }

    if (!EmptyClipboard()) {
        CloseClipboard();
        return 0;
    }

    int rowStride = width * 4;
    SIZE_T pixelDataSize = (SIZE_T)rowStride * (SIZE_T)height;

    unsigned char* bgra = (unsigned char*)malloc(pixelDataSize);
    if (bgra == NULL) {
        CloseClipboard();
        return 0;
    }

    for (int y = 0; y < height; ++y) {
        const unsigned char* srcRow = data + (SIZE_T)y * (SIZE_T)rowStride;
        unsigned char* dstRow = bgra + (SIZE_T)y * (SIZE_T)rowStride;
        for (int x = 0; x < width; ++x) {
            dstRow[x * 4 + 0] = srcRow[x * 4 + 2]; /* B */
            dstRow[x * 4 + 1] = srcRow[x * 4 + 1]; /* G */
            dstRow[x * 4 + 2] = srcRow[x * 4 + 0]; /* R */
            dstRow[x * 4 + 3] = srcRow[x * 4 + 3]; /* A */
        }
    }

    SIZE_T dibSize = sizeof(BITMAPINFOHEADER) + pixelDataSize;
    HGLOBAL hDib = GlobalAlloc(GHND, dibSize);
    if (hDib == NULL) {
        free(bgra);
        CloseClipboard();
        return 0;
    }

    BITMAPINFOHEADER* dibHeader = (BITMAPINFOHEADER*)GlobalLock(hDib);
    if (dibHeader == NULL) {
        GlobalFree(hDib);
        free(bgra);
        CloseClipboard();
        return 0;
    }

    memset(dibHeader, 0, sizeof(BITMAPINFOHEADER));
    dibHeader->biSize = sizeof(BITMAPINFOHEADER);
    dibHeader->biWidth = width;
    dibHeader->biHeight = -height; /* negative for top-down */
    dibHeader->biPlanes = 1;
    dibHeader->biBitCount = 32;
    dibHeader->biCompression = BI_RGB;
    dibHeader->biSizeImage = (DWORD)pixelDataSize;

    unsigned char* dibPixels = (unsigned char*)(dibHeader + 1);
    memcpy(dibPixels, bgra, pixelDataSize);

    GlobalUnlock(hDib);

    SIZE_T dibv5Size = sizeof(BITMAPV5HEADER) + pixelDataSize;
    HGLOBAL hDibV5 = GlobalAlloc(GHND, dibv5Size);
    if (hDibV5 == NULL) {
        GlobalFree(hDib);
        free(bgra);
        CloseClipboard();
        return 0;
    }

    BITMAPV5HEADER* dibV5Header = (BITMAPV5HEADER*)GlobalLock(hDibV5);
    if (dibV5Header == NULL) {
        GlobalFree(hDibV5);
        GlobalFree(hDib);
        free(bgra);
        CloseClipboard();
        return 0;
    }

    memset(dibV5Header, 0, sizeof(BITMAPV5HEADER));
    dibV5Header->bV5Size = sizeof(BITMAPV5HEADER);
    dibV5Header->bV5Width = width;
    dibV5Header->bV5Height = -height;
    dibV5Header->bV5Planes = 1;
    dibV5Header->bV5BitCount = 32;
    dibV5Header->bV5Compression = BI_BITFIELDS;
    dibV5Header->bV5RedMask = 0x00ff0000;
    dibV5Header->bV5GreenMask = 0x0000ff00;
    dibV5Header->bV5BlueMask = 0x000000ff;
    dibV5Header->bV5AlphaMask = 0xff000000;
    dibV5Header->bV5CSType = 0x73524742u; /* 'sRGB' */
    dibV5Header->bV5Intent = LCS_GM_IMAGES;
    dibV5Header->bV5SizeImage = (DWORD)pixelDataSize;

    unsigned char* dibV5Pixels = (unsigned char*)(dibV5Header + 1);
    memcpy(dibV5Pixels, bgra, pixelDataSize);

    GlobalUnlock(hDibV5);

    HDC hdc = GetDC(NULL);
    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* sectionBits = NULL;
    HBITMAP hBitmap = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &sectionBits, NULL, 0);
    ReleaseDC(NULL, hdc);
    if (hBitmap == NULL || sectionBits == NULL) {
        if (hBitmap != NULL) {
            DeleteObject(hBitmap);
        }
        GlobalFree(hDibV5);
        GlobalFree(hDib);
        free(bgra);
        CloseClipboard();
        return 0;
    }

    memcpy(sectionBits, bgra, pixelDataSize);

    int dibOk = 0;
    int dibV5Ok = 0;
    int bmpOk = 0;

    if (SetClipboardData(CF_DIB, hDib) != NULL) {
        dibOk = 1;
    } else {
        GlobalFree(hDib);
    }

    if (SetClipboardData(CF_DIBV5, hDibV5) != NULL) {
        dibV5Ok = 1;
    } else {
        GlobalFree(hDibV5);
    }

    if (SetClipboardData(CF_BITMAP, hBitmap) != NULL) {
        bmpOk = 1;
    } else {
        DeleteObject(hBitmap);
    }

    CloseClipboard();
    free(bgra);
    return dibOk || dibV5Ok || bmpOk;
}

char** WinClip_GetFileDropList(int* count) {
    if (count != NULL) {
        *count = 0;
    }

    if (!OpenClipboard(NULL)) {
        return NULL;
    }

    if (!IsClipboardFormatAvailable(CF_HDROP)) {
        CloseClipboard();
        return NULL;
    }

    HANDLE hData = GetClipboardData(CF_HDROP);
    if (hData == NULL) {
        CloseClipboard();
        return NULL;
    }

    HDROP hDrop = (HDROP)GlobalLock(hData);
    if (hDrop == NULL) {
        CloseClipboard();
        return NULL;
    }

    UINT fileCount = DragQueryFileW(hDrop, 0xFFFFFFFF, NULL, 0);
    if (fileCount == 0) {
        GlobalUnlock(hData);
        CloseClipboard();
        return NULL;
    }

    char** results = (char**)calloc(fileCount, sizeof(char*));
    if (results == NULL) {
        GlobalUnlock(hData);
        CloseClipboard();
        return NULL;
    }

    int collected = 0;
    for (UINT i = 0; i < fileCount; ++i) {
        UINT pathLen = DragQueryFileW(hDrop, i, NULL, 0);
        if (pathLen == 0) {
            continue;
        }

        WCHAR* widePath = (WCHAR*)malloc(((size_t)pathLen + 1) * sizeof(WCHAR));
        if (widePath == NULL) {
            continue;
        }

        if (DragQueryFileW(hDrop, i, widePath, pathLen + 1) == 0) {
            free(widePath);
            continue;
        }

        char* utf8Path = WinClip_WideToUtf8(widePath);
        free(widePath);
        if (utf8Path == NULL) {
            continue;
        }

        results[collected++] = utf8Path;
    }

    GlobalUnlock(hData);
    CloseClipboard();

    if (collected == 0) {
        free(results);
        return NULL;
    }

    if (count != NULL) {
        *count = collected;
    }

    if (collected < (int)fileCount) {
        char** trimmed = (char**)realloc(results, (size_t)collected * sizeof(char*));
        if (trimmed != NULL) {
            results = trimmed;
        }
    }

    return results;
}

void WinClip_FreeFileDropList(char** list, int count) {
    if (list == NULL || count <= 0) {
        return;
    }

    for (int i = 0; i < count; ++i) {
        free(list[i]);
    }
    free(list);
}

#endif /* _WIN32 */