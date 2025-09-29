#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
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

#endif /* _WIN32 */