#include "raylib.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <stddef.h>

#ifdef _WIN32
#include "win_clipboard.h"
#endif

#define MAX_BOXES 100
#define MAX_PEN_POINTS 4096
#define MAX_HISTORY 64

typedef enum {
    BOX_IMAGE,
    BOX_TEXT,
    BOX_VIDEO,
    BOX_AUDIO,
    BOX_DRAWING
} BoxType;

typedef enum {
    TOOL_SELECT,
    TOOL_PEN,
    TOOL_SEGMENT,
    TOOL_CIRCLE,
    TOOL_RECT
} Tool;

typedef struct {
    int x;
    int y;
    int width;
    int height;
    BoxType type;
    union {
        Texture2D texture;
        char* text;
        Sound sound;
    } content;
    char* filePath;
    int fontSize;
    Color textColor;
    int isSelected;
} Box;

typedef enum {
    RESIZE_NONE,
    RESIZE_LEFT,
    RESIZE_RIGHT,
    RESIZE_TOP,
    RESIZE_BOTTOM,
    RESIZE_TOP_LEFT,
    RESIZE_TOP_RIGHT,
    RESIZE_BOTTOM_LEFT,
    RESIZE_BOTTOM_RIGHT
} ResizeMode;

static const float HANDLE_SIZE = 10.0f;
static const float EDGE_DETECTION_MARGIN = 6.0f;
static const float TEXT_DRAG_BORDER = 14.0f;
static const float TOOLBAR_HEIGHT = 64.0f;
static const float TOOLBAR_PADDING = 10.0f;
static const float STROKE_THICKNESS = 4.0f;
static const int AUDIO_BOX_WIDTH = 260;
static const int AUDIO_BOX_HEIGHT = 96;
static const float BUTTON_ROUNDNESS = 0.25f;
static const float STATUS_BAR_HEIGHT = 32.0f;
static const char* STATUS_DEFAULT_HINT = "Tip: Double-click to edit text • Ctrl+V pastes media";

static const Color TEXT_SELECTION_COLOR = {100, 149, 237, 120};
static const Color TEXT_EDIT_BORDER_COLOR = {72, 168, 255, 255};
static const Color BOX_SELECTION_BORDER_COLOR = {50, 205, 50, 255};

static const Color COLOR_PALETTE[] = {
    BLACK,
    RED,
    GREEN,
    BLUE,
    GOLD,
    PURPLE,
    DARKGRAY
};

static const int COLOR_PALETTE_COUNT = (int)(sizeof(COLOR_PALETTE) / sizeof(COLOR_PALETTE[0]));
static const int DEFAULT_FONT_SIZE = 20;
static const int MIN_FONT_SIZE = 12;
static const int MAX_FONT_SIZE = 72;
static const int FONT_SIZE_STEP = 2;

/* Text editing state */
int editingBoxIndex = -1;
char editingText[1024] = {0};
char editingOriginalText[1024] = {0};
int editingFontSize = DEFAULT_FONT_SIZE;
int editingOriginalFontSize = DEFAULT_FONT_SIZE;
int cursorPosition = 0;
int selectionStart = 0;
int selectionEnd = 0;
int cursorPreferredColumn = -1;
int selectAllOnStart = 0;
int isMouseSelecting = 0;
float cursorBlinkTime = 0.0f;
int lastTextEditChanged = 0;

void UpdateEditingBoxSize(Box* boxes);
ResizeMode GetResizeModeForPoint(const Box* box, Vector2 point);
void ApplyResize(Box* box, ResizeMode mode, Vector2 delta);
int MouseCursorForResizeMode(ResizeMode mode);
Rectangle GetBoxRect(const Box* box);
int FindTopmostBoxAtPoint(Vector2 point, Box* boxes, int boxCount);
int IsPointInTextDragZone(const Box* box, Vector2 point);
void StopTextEditAndRecord(Box* boxes, int boxCount, int selectedBox);
void StopTextEdit(Box* boxes);
void DestroyBox(Box* box);
int BringBoxToFront(Box* boxes, int boxCount, int index);
int SendBoxToBack(Box* boxes, int boxCount, int index);
void ClearAllBoxes(Box* boxes, int* boxCount, int* selectedBox);
void ResetEditingState(void);
int ColorsEqual(Color a, Color b);
int CopyImageToClipboard(const Image* image);
void HandleTextInput(Box* boxes, char* statusMessage, size_t statusMessageSize, float* statusMessageTimer);

typedef struct {
    Box box;
    Image imageCopy;
    char* textCopy;
    char* filePathCopy;
} BoxSnapshot;

typedef struct {
    BoxSnapshot boxes[MAX_BOXES];
    int boxCount;
    int selectedBox;
} CanvasSnapshot;

void FreeSnapshot(CanvasSnapshot* snapshot);
void CaptureSnapshot(CanvasSnapshot* snapshot, Box* boxes, int boxCount, int selectedBox);
void PushHistoryState(Box* boxes, int boxCount, int selectedBox);
void RestoreSnapshotState(Box* boxes, int* boxCount, int* selectedBox, int targetIndex);
int PerformUndo(Box* boxes, int* boxCount, int* selectedBox);
int PerformRedo(Box* boxes, int* boxCount, int* selectedBox);

static CanvasSnapshot historyStates[MAX_HISTORY];
static int historyCount = 0;
static int historyIndex = -1;
static int suppressHistory = 0;
static int audioDeviceReady = 0;

const char* GetClipboardTextSafe(void) {
    #ifdef _WIN32
    /* Use our custom Windows clipboard implementation to avoid GLFW errors */
    if (!WinClip_HasText()) {
        return NULL;
    }
    /* WinClip_GetText returns allocated memory - caller should free it */
    static char* lastClipText = NULL;
    if (lastClipText) {
        free(lastClipText);
        lastClipText = NULL;
    }
    lastClipText = WinClip_GetText();
    return lastClipText;
    #else
    /* On non-Windows platforms, use raylib's function */
    const char* clip = GetClipboardText();
    if (clip == NULL || strlen(clip) == 0) {
        return NULL;
    }
    return clip;
    #endif
}

static int ClampCursorIndex(const char* text, int index);

static int FindPreviousWordBoundary(const char* text, int index) {
    if (text == NULL) {
        return 0;
    }

    int pos = ClampCursorIndex(text, index);
    if (pos <= 0) {
        return 0;
    }

    pos--;

    while (pos > 0 && isspace((unsigned char)text[pos])) {
        pos--;
    }

    while (pos > 0 && !isspace((unsigned char)text[pos - 1])) {
        pos--;
    }

    if (pos < 0) {
        pos = 0;
    }
    return pos;
}

static int FindNextWordBoundary(const char* text, int index) {
    if (text == NULL) {
        return 0;
    }

    int len = (int)strlen(text);
    int pos = ClampCursorIndex(text, index);
    if (pos >= len) {
        return len;
    }

    while (pos < len && isspace((unsigned char)text[pos])) {
        pos++;
    }

    while (pos < len && !isspace((unsigned char)text[pos])) {
        pos++;
    }

    if (pos > len) {
        pos = len;
    }
    return pos;
}

static char* CopySubstring(const char* start, int length) {
    if (start == NULL || length <= 0) {
        return NULL;
    }
    char* buffer = (char*)malloc((size_t)length + 1);
    if (buffer == NULL) {
        return NULL;
    }
    memcpy(buffer, start, (size_t)length);
    buffer[length] = '\0';
    return buffer;
}

static void TrimSurroundingQuotes(char* str) {
    if (str == NULL) {
        return;
    }
    size_t len = strlen(str);
    if (len >= 2 && ((str[0] == '"' && str[len - 1] == '"') || (str[0] == '\'' && str[len - 1] == '\''))) {
        memmove(str, str + 1, len - 2);
        str[len - 2] = '\0';
        len -= 2;
    }
}

static void TrimWhitespace(char* str) {
    if (str == NULL) {
        return;
    }
    size_t len = strlen(str);
    while (len > 0 && isspace((unsigned char)str[len - 1])) {
        str[len - 1] = '\0';
        len--;
    }
    size_t start = 0;
    while (str[start] != '\0' && isspace((unsigned char)str[start])) {
        start++;
    }
    if (start > 0) {
        memmove(str, str + start, strlen(str + start) + 1);
    }
}

static char* DuplicateSanitizedPath(const char* clipText) {
    if (clipText == NULL) {
        return NULL;
    }
    char* copy = strdup(clipText);
    if (copy == NULL) {
        return NULL;
    }
    TrimWhitespace(copy);
    TrimSurroundingQuotes(copy);
    return copy;
}

static const char* ExtractFileName(const char* path) {
    if (path == NULL) {
        return "";
    }
    const char* slash = strrchr(path, '/');
    const char* backslash = strrchr(path, '\\');
    const char* lastSep = slash;
    if (backslash != NULL && (lastSep == NULL || backslash > lastSep)) {
        lastSep = backslash;
    }
    return (lastSep != NULL) ? lastSep + 1 : path;
}

static int EqualsIgnoreCase(const char* a, const char* b) {
    if (a == NULL || b == NULL) {
        return 0;
    }
    while (*a != '\0' && *b != '\0') {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static void StopAudioPlayback(Box* box) {
    if (box == NULL || box->type != BOX_AUDIO) {
        return;
    }
    if (audioDeviceReady && IsSoundReady(box->content.sound) && IsSoundPlaying(box->content.sound)) {
        StopSound(box->content.sound);
    }
}

static void ToggleAudioPlayback(Box* box, char* statusMessage, size_t statusMessageSize, float* statusMessageTimer) {
    if (box == NULL || box->type != BOX_AUDIO) {
        return;
    }
    if (!audioDeviceReady) {
        if (statusMessage && statusMessageSize > 0 && statusMessageTimer) {
            snprintf(statusMessage, statusMessageSize, "Audio device unavailable");
            *statusMessageTimer = 1.8f;
        }
        return;
    }
    if (!IsSoundReady(box->content.sound)) {
        if (statusMessage && statusMessageSize > 0 && statusMessageTimer) {
            snprintf(statusMessage, statusMessageSize, "Audio not ready");
            *statusMessageTimer = 1.8f;
        }
        return;
    }

    if (IsSoundPlaying(box->content.sound)) {
        StopSound(box->content.sound);
        if (statusMessage && statusMessageSize > 0 && statusMessageTimer) {
            snprintf(statusMessage, statusMessageSize, "Paused %s", ExtractFileName(box->filePath));
            *statusMessageTimer = 1.4f;
        }
    } else {
        StopSound(box->content.sound);
        PlaySound(box->content.sound);
        if (statusMessage && statusMessageSize > 0 && statusMessageTimer) {
            snprintf(statusMessage, statusMessageSize, "Playing %s", ExtractFileName(box->filePath));
            *statusMessageTimer = 1.4f;
        }
    }
}

static int MeasureTextSegmentWidth(const char* start, int length, int fontSize) {
    if (start == NULL || length <= 0) {
        return 0;
    }
    char* segment = CopySubstring(start, length);
    if (segment == NULL) {
        return 0;
    }
    int width = MeasureText(segment, fontSize);
    free(segment);
    return width;
}

static int ClampCursorIndex(const char* text, int index) {
    if (text == NULL) {
        return 0;
    }
    int len = (int)strlen(text);
    if (index < 0) index = 0;
    if (index > len) index = len;
    return index;
}

static int SelectionHasRange(void) {
    return selectionStart != selectionEnd;
}

static int SelectionMin(void) {
    return (selectionStart < selectionEnd) ? selectionStart : selectionEnd;
}

static int SelectionMax(void) {
    return (selectionStart > selectionEnd) ? selectionStart : selectionEnd;
}

static void MoveCursorTo(int position, int extendSelection) {
    int clamped = ClampCursorIndex(editingText, position);
    if (extendSelection) {
        selectionEnd = clamped;
    } else {
        selectionStart = clamped;
        selectionEnd = clamped;
    }
    cursorPosition = selectionEnd;
    if (!extendSelection) {
        cursorPreferredColumn = -1;
    }
    cursorBlinkTime = 0.0f;
}

static int DeleteSelectionRange(void) {
    if (!SelectionHasRange()) {
        return 0;
    }
    int start = SelectionMin();
    int end = SelectionMax();
    int len = (int)strlen(editingText);
    if (start < 0) start = 0;
    if (end > len) end = len;
    memmove(editingText + start, editingText + end, (size_t)(len - end + 1));
    selectionStart = selectionEnd = start;
    cursorPosition = start;
    cursorPreferredColumn = -1;
    cursorBlinkTime = 0.0f;
    return 1;
}

static int GetLineStartIndex(const char* text, int index) {
    if (text == NULL) return 0;
    int clamped = ClampCursorIndex(text, index);
    while (clamped > 0 && text[clamped - 1] != '\n') {
        clamped--;
    }
    return clamped;
}

static int GetLineEndIndex(const char* text, int index) {
    if (text == NULL) return 0;
    int len = (int)strlen(text);
    int clamped = ClampCursorIndex(text, index);
    while (clamped < len && text[clamped] != '\n') {
        clamped++;
    }
    return clamped;
}

static void MoveCursorVertical(int direction, int extendSelection) {
    if (direction == 0) {
        return;
    }

    int len = (int)strlen(editingText);
    if (len == 0) {
        MoveCursorTo(0, extendSelection);
        return;
    }

    int lineStart = GetLineStartIndex(editingText, cursorPosition);
    int currentColumn = cursorPosition - lineStart;
    int preferredColumn = cursorPreferredColumn;
    if (preferredColumn < 0) {
        preferredColumn = currentColumn;
    }

    if (direction < 0) {
        if (lineStart == 0) {
            MoveCursorTo(0, extendSelection);
            cursorPreferredColumn = preferredColumn;
            return;
        }
        int prevLineEnd = lineStart - 1;
        int prevLineStart = GetLineStartIndex(editingText, prevLineEnd);
        int prevLineLength = prevLineEnd - prevLineStart;
        int targetColumn = preferredColumn;
        if (targetColumn > prevLineLength) targetColumn = prevLineLength;
        MoveCursorTo(prevLineStart + targetColumn, extendSelection);
    } else {
        int lineEnd = GetLineEndIndex(editingText, cursorPosition);
        if (lineEnd >= len) {
            MoveCursorTo(len, extendSelection);
            cursorPreferredColumn = preferredColumn;
            return;
        }
        int nextLineStart = lineEnd + 1;
        if (nextLineStart > len) {
            MoveCursorTo(len, extendSelection);
            cursorPreferredColumn = preferredColumn;
            return;
        }
        int nextLineEnd = GetLineEndIndex(editingText, nextLineStart);
        int nextLineLength = nextLineEnd - nextLineStart;
        int targetColumn = preferredColumn;
        if (targetColumn > nextLineLength) targetColumn = nextLineLength;
        MoveCursorTo(nextLineStart + targetColumn, extendSelection);
    }

    cursorPreferredColumn = preferredColumn;
}

static void GetCursorCoordinates(const char* text, int fontSize, int index, int* outX, int* outY) {
    if (outX) *outX = 0;
    if (outY) *outY = 0;
    if (text == NULL) {
        return;
    }

    int len = (int)strlen(text);
    int clamped = ClampCursorIndex(text, index);
    int currentIndex = 0;
    int lineNumber = 0;

    while (currentIndex <= len) {
        const char* linePtr = text + currentIndex;
        const char* newline = strchr(linePtr, '\n');
        int lineLength = newline ? (int)(newline - linePtr) : (int)strlen(linePtr);
        int lineEnd = currentIndex + lineLength;

        if (clamped <= lineEnd) {
            int offset = clamped - currentIndex;
            if (outX) {
                *outX = MeasureTextSegmentWidth(linePtr, offset, fontSize);
            }
            if (outY) {
                *outY = lineNumber * fontSize;
            }
            return;
        }

        currentIndex = lineEnd;
        if (newline) {
            if (clamped == currentIndex) {
                if (outX) {
                    *outX = MeasureTextSegmentWidth(linePtr, lineLength, fontSize);
                }
                if (outY) {
                    *outY = lineNumber * fontSize;
                }
                return;
            }
            currentIndex++;
            lineNumber++;
        } else {
            break;
        }
    }

    if (outX) {
        const char* linePtr = text + currentIndex;
        *outX = MeasureTextSegmentWidth(linePtr, (int)strlen(linePtr), fontSize);
    }
    if (outY) {
        *outY = lineNumber * fontSize;
    }
}

static int GetTextIndexFromPoint(const char* text, int fontSize, Vector2 local) {
    if (text == NULL) {
        return 0;
    }

    int len = (int)strlen(text);
    int x = (int)local.x - 10;
    int y = (int)local.y - 10;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    int lineIndex = y / fontSize;

    int currentIndex = 0;
    int currentLine = 0;

    while (currentIndex <= len) {
        const char* linePtr = text + currentIndex;
        const char* newline = strchr(linePtr, '\n');
        int lineLength = newline ? (int)(newline - linePtr) : (int)strlen(linePtr);
        int lineEnd = currentIndex + lineLength;

        if (currentLine == lineIndex || !newline) {
            int offset = 0;
            for (int i = 0; i <= lineLength; i++) {
                int width = MeasureTextSegmentWidth(linePtr, i, fontSize);
                if (x < width) {
                    offset = i;
                    break;
                }
                offset = i;
            }
            int target = currentIndex + offset;
            if (target > len) target = len;
            return target;
        }

        if (!newline) {
            break;
        }

        currentIndex = lineEnd + 1;
        currentLine++;
    }

    return len;
}

void CalculateTextBoxSize(const char* text, int fontSize, int* width, int* height) {
    if (width == NULL || height == NULL) {
        return;
    }

    int maxWidth = 0;
    int lineCount = 0;

    if (text && text[0] != '\0') {
        const char* ptr = text;
        const char* lineStart = text;

        while (*ptr != '\0') {
            if (*ptr == '\n') {
                int lineWidth = MeasureTextSegmentWidth(lineStart, (int)(ptr - lineStart), fontSize);
                if (lineWidth > maxWidth) {
                    maxWidth = lineWidth;
                }
                lineCount++;
                lineStart = ptr + 1;
            }
            ptr++;
        }

        int lineWidth = MeasureTextSegmentWidth(lineStart, (int)(ptr - lineStart), fontSize);
        if (lineWidth > maxWidth) {
            maxWidth = lineWidth;
        }
        lineCount++;
    } else {
        lineCount = 1;
    }

    if (lineCount <= 0) {
        lineCount = 1;
    }

    int paddedWidth = maxWidth + 20;   /* 10px padding on each side */
    int paddedHeight = (lineCount * fontSize) + 20;

    int minWidth = fontSize * 5;
    if (minWidth < 80) minWidth = 80;
    if (paddedWidth < minWidth) paddedWidth = minWidth;

    int minHeight = fontSize + 20;
    if (minHeight < 30) minHeight = 30;
    if (paddedHeight < minHeight) paddedHeight = minHeight;

    *width = paddedWidth;
    *height = paddedHeight;
}

static void DrawMultilineTextWithSelection(const char* text, int x, int y, int fontSize, Color color, int selStart, int selEnd, Color highlight) {
    if (text == NULL) {
        return;
    }

    int totalLength = (int)strlen(text);
    int hasSelection = (selStart != selEnd);
    if (selStart > selEnd) {
        int tmp = selStart;
        selStart = selEnd;
        selEnd = tmp;
    }

    int currentIndex = 0;
    int currentY = y;

    while (1) {
        const char* linePtr = text + currentIndex;
        const char* newline = strchr(linePtr, '\n');
        int lineLength = newline ? (int)(newline - linePtr) : (int)strlen(linePtr);
        int lineStartIndex = currentIndex;
        int lineEndIndex = lineStartIndex + lineLength;

        if (hasSelection) {
            int highlightStart = selStart;
            if (highlightStart < lineStartIndex) highlightStart = lineStartIndex;
            if (highlightStart > lineEndIndex) highlightStart = lineEndIndex;

            int highlightEnd = selEnd;
            if (highlightEnd < lineStartIndex) highlightEnd = lineStartIndex;
            if (highlightEnd > lineEndIndex) highlightEnd = lineEndIndex;

            int highlightLength = highlightEnd - highlightStart;
            if (highlightLength > 0) {
                int preLength = highlightStart - lineStartIndex;
                int preWidth = MeasureTextSegmentWidth(linePtr, preLength, fontSize);
                int highlightWidth = MeasureTextSegmentWidth(linePtr + preLength, highlightLength, fontSize);
                if (highlightWidth <= 0) highlightWidth = fontSize / 2;
                DrawRectangle(x + preWidth, currentY, (float)highlightWidth, (float)fontSize, highlight);
            } else if (lineLength == 0 && selStart <= lineStartIndex && selEnd > lineStartIndex) {
                DrawRectangle(x, currentY, (float)(fontSize / 2), (float)fontSize, highlight);
            }

            if (selStart <= lineEndIndex && selEnd > lineEndIndex && newline) {
                int endWidth = MeasureTextSegmentWidth(linePtr, lineLength, fontSize);
                DrawRectangle(x + endWidth, currentY, (float)(fontSize / 2), (float)fontSize, highlight);
            }
        }

        if (lineLength > 0) {
            char* line = CopySubstring(linePtr, lineLength);
            if (line != NULL) {
                DrawText(line, x, currentY, fontSize, color);
                free(line);
            }
        }

        if (!newline) {
            break;
        }

        currentIndex = lineEndIndex + 1;
        currentY += fontSize;

        if (currentIndex > totalLength) {
            break;
        }

        if (currentIndex == totalLength) {
            if (hasSelection && selStart <= currentIndex && selEnd > currentIndex) {
                DrawRectangle(x, currentY, (float)(fontSize / 2), (float)fontSize, highlight);
            }
            break;
        }
    }
}

void StartTextEdit(int boxIndex, Box* boxes) {
    if (boxIndex >= 0 && boxes[boxIndex].type == BOX_TEXT) {
        if (boxes[boxIndex].textColor.a == 0) {
            boxes[boxIndex].textColor = BLACK;
        }
        editingBoxIndex = boxIndex;
        strncpy(editingText, boxes[boxIndex].content.text, sizeof(editingText) - 1);
        editingText[sizeof(editingText) - 1] = '\0';
        strncpy(editingOriginalText, editingText, sizeof(editingOriginalText) - 1);
        editingOriginalText[sizeof(editingOriginalText) - 1] = '\0';
        editingFontSize = boxes[boxIndex].fontSize > 0 ? boxes[boxIndex].fontSize : DEFAULT_FONT_SIZE;
        editingOriginalFontSize = editingFontSize;
        cursorPosition = strlen(editingText);
        if (selectAllOnStart) {
            selectionStart = 0;
            selectionEnd = cursorPosition;
            selectAllOnStart = 0;
        } else {
            selectionStart = cursorPosition;
            selectionEnd = cursorPosition;
        }
        cursorPreferredColumn = -1;
        isMouseSelecting = 0;
        cursorBlinkTime = 0.0f;
        UpdateEditingBoxSize(boxes);
    }
}

void StopTextEdit(Box* boxes) {
    if (editingBoxIndex >= 0) {
        /* Update the box text with edited content */
        if (boxes[editingBoxIndex].content.text) {
            free(boxes[editingBoxIndex].content.text);
        }
        boxes[editingBoxIndex].content.text = strdup(editingText);

        /* Resize box to fit new text */
        int textWidth, textHeight;
        CalculateTextBoxSize(editingText, editingFontSize, &textWidth, &textHeight);
        boxes[editingBoxIndex].width = textWidth;
        boxes[editingBoxIndex].height = textHeight;
        boxes[editingBoxIndex].fontSize = editingFontSize;

        lastTextEditChanged = (strcmp(editingOriginalText, editingText) != 0) || (editingOriginalFontSize != editingFontSize);

        editingBoxIndex = -1;
        memset(editingText, 0, sizeof(editingText));
        memset(editingOriginalText, 0, sizeof(editingOriginalText));
        editingFontSize = DEFAULT_FONT_SIZE;
        editingOriginalFontSize = DEFAULT_FONT_SIZE;
        cursorPosition = 0;
        selectionStart = 0;
        selectionEnd = 0;
        cursorPreferredColumn = -1;
        isMouseSelecting = 0;
    }
}

void StopTextEditAndRecord(Box* boxes, int boxCount, int selectedBox) {
    StopTextEdit(boxes);
    if (lastTextEditChanged) {
        PushHistoryState(boxes, boxCount, selectedBox);
        lastTextEditChanged = 0;
    }
}

void UpdateEditingBoxSize(Box* boxes) {
    if (editingBoxIndex < 0) return;

    int textWidth, textHeight;
    CalculateTextBoxSize(editingText, editingFontSize, &textWidth, &textHeight);
    boxes[editingBoxIndex].width = textWidth;
    boxes[editingBoxIndex].height = textHeight;
    boxes[editingBoxIndex].fontSize = editingFontSize;
}

void HandleTextInput(Box* boxes, char* statusMessage, size_t statusMessageSize, float* statusMessageTimer) {
    if (editingBoxIndex < 0) return;

    int textChanged = 0;
    int fontSizeChanged = 0;
    int ctrlDown = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    int shiftDown = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);

    if (ctrlDown && (IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD))) {
        int newSize = editingFontSize + FONT_SIZE_STEP;
        if (newSize > MAX_FONT_SIZE) newSize = MAX_FONT_SIZE;
        if (newSize != editingFontSize) {
            editingFontSize = newSize;
            fontSizeChanged = 1;
            cursorPreferredColumn = -1;
        }
    }

    if (ctrlDown && (IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT))) {
        int newSize = editingFontSize - FONT_SIZE_STEP;
        if (newSize < MIN_FONT_SIZE) newSize = MIN_FONT_SIZE;
        if (newSize != editingFontSize) {
            editingFontSize = newSize;
            fontSizeChanged = 1;
            cursorPreferredColumn = -1;
        }
    }

    if (ctrlDown && IsKeyPressed(KEY_ZERO)) {
        if (editingFontSize != DEFAULT_FONT_SIZE) {
            editingFontSize = DEFAULT_FONT_SIZE;
            fontSizeChanged = 1;
            cursorPreferredColumn = -1;
        }
    }

    if (ctrlDown && IsKeyPressed(KEY_C)) {
        if (SelectionHasRange()) {
            int start = SelectionMin();
            int length = SelectionMax() - start;
            char* sliced = CopySubstring(editingText + start, length);
            if (sliced != NULL) {
                SetClipboardText(sliced);
                free(sliced);
            }
        }
    }

    if (ctrlDown && IsKeyPressed(KEY_X)) {
        if (SelectionHasRange()) {
            int start = SelectionMin();
            int length = SelectionMax() - start;
            char* sliced = CopySubstring(editingText + start, length);
            if (sliced != NULL) {
                SetClipboardText(sliced);
                free(sliced);
            }
            textChanged |= DeleteSelectionRange();
        }
    }

    if (ctrlDown && IsKeyPressed(KEY_V)) {
        const char* clip = GetClipboardTextSafe();
        if (clip && clip[0] != '\0') {
            if (SelectionHasRange()) {
                textChanged |= DeleteSelectionRange();
            }

            int currentLen = (int)strlen(editingText);
            int available = (int)sizeof(editingText) - 1 - currentLen;
            if (available > 0) {
                int clipLen = (int)strlen(clip);
                if (clipLen > available) {
                    clipLen = available;
                }

                memmove(editingText + cursorPosition + clipLen,
                        editingText + cursorPosition,
                        (size_t)(currentLen - cursorPosition + 1));
                memcpy(editingText + cursorPosition, clip, (size_t)clipLen);
                MoveCursorTo(cursorPosition + clipLen, 0);
                textChanged = 1;
            }
        }
    }

    /* Handle printable character input */
    int key = GetCharPressed();
    while (key > 0) {
        if (!ctrlDown && key >= 32 && key <= 126) {
            int currentLength = (int)strlen(editingText);
            if (currentLength < (int)sizeof(editingText) - 1) {
                if (DeleteSelectionRange()) {
                    textChanged = 1;
                }
                /* Insert character */
                for (int i = currentLength; i >= cursorPosition; i--) {
                    editingText[i + 1] = editingText[i];
                }
                editingText[cursorPosition] = (char)key;
                MoveCursorTo(cursorPosition + 1, 0);
                textChanged = 1;
            }
        }
        key = GetCharPressed();
    }

    if (ctrlDown && IsKeyPressed(KEY_A)) {
        selectionStart = 0;
        selectionEnd = (int)strlen(editingText);
        cursorPosition = selectionEnd;
        cursorBlinkTime = 0.0f;
        cursorPreferredColumn = -1;
    }

    if (IsKeyPressed(KEY_ENTER)) {
        int currentLength = (int)strlen(editingText);
        if (currentLength < (int)sizeof(editingText) - 1) {
            if (DeleteSelectionRange()) {
                textChanged = 1;
            }
            for (int i = currentLength; i >= cursorPosition; i--) {
                editingText[i + 1] = editingText[i];
            }
            editingText[cursorPosition] = '\n';
            MoveCursorTo(cursorPosition + 1, 0);
            textChanged = 1;
        }
    }

    if (IsKeyPressed(KEY_BACKSPACE)) {
        if (SelectionHasRange()) {
            textChanged |= DeleteSelectionRange();
        } else if (cursorPosition > 0) {
            int len = (int)strlen(editingText);
            for (int i = cursorPosition - 1; i < len; i++) {
                editingText[i] = editingText[i + 1];
            }
            MoveCursorTo(cursorPosition - 1, 0);
            textChanged = 1;
        }
    }

    if (IsKeyPressed(KEY_DELETE)) {
        if (SelectionHasRange()) {
            textChanged |= DeleteSelectionRange();
        } else {
            int len = (int)strlen(editingText);
            if (cursorPosition < len) {
                for (int i = cursorPosition; i < len; i++) {
                    editingText[i] = editingText[i + 1];
                }
                textChanged = 1;
                cursorBlinkTime = 0.0f;
                cursorPreferredColumn = -1;
            }
        }
    }

    if (IsKeyPressed(KEY_LEFT)) {
        if (!shiftDown && SelectionHasRange()) {
            MoveCursorTo(SelectionMin(), 0);
        } else if (ctrlDown) {
            int newPos = FindPreviousWordBoundary(editingText, cursorPosition);
            MoveCursorTo(newPos, shiftDown);
        } else {
            MoveCursorTo(cursorPosition - 1, shiftDown);
        }
    }

    if (IsKeyPressed(KEY_RIGHT)) {
        if (!shiftDown && SelectionHasRange()) {
            MoveCursorTo(SelectionMax(), 0);
        } else if (ctrlDown) {
            int newPos = FindNextWordBoundary(editingText, cursorPosition);
            MoveCursorTo(newPos, shiftDown);
        } else {
            MoveCursorTo(cursorPosition + 1, shiftDown);
        }
    }

    if (IsKeyPressed(KEY_HOME)) {
        if (ctrlDown) {
            MoveCursorTo(0, shiftDown);
        } else {
            int start = GetLineStartIndex(editingText, cursorPosition);
            MoveCursorTo(start, shiftDown);
        }
    }

    if (IsKeyPressed(KEY_END)) {
        if (ctrlDown) {
            int len = (int)strlen(editingText);
            MoveCursorTo(len, shiftDown);
        } else {
            int end = GetLineEndIndex(editingText, cursorPosition);
            MoveCursorTo(end, shiftDown);
        }
    }

    if (IsKeyPressed(KEY_UP)) {
        MoveCursorVertical(-1, shiftDown);
    }

    if (IsKeyPressed(KEY_DOWN)) {
        MoveCursorVertical(1, shiftDown);
    }

    if (textChanged || fontSizeChanged) {
        UpdateEditingBoxSize(boxes);
        lastTextEditChanged = 1;
    }

    if (fontSizeChanged && statusMessage && statusMessageSize > 0 && statusMessageTimer) {
        snprintf(statusMessage, statusMessageSize, "Text size: %d pt", editingFontSize);
        *statusMessageTimer = 1.4f;
    }
}

void DrawTextCursor(int x, int y, int fontSize) {
    if (editingBoxIndex < 0) return;

    /* Update cursor blink timer */
    cursorBlinkTime += GetFrameTime();

    if (SelectionHasRange()) {
        /* Still draw the caret at the selection end for clarity */
        cursorBlinkTime = fmodf(cursorBlinkTime, 1.0f);
    }

    if (fmodf(cursorBlinkTime, 1.0f) < 0.5f || SelectionHasRange()) {
        int relativeX = 0;
        int relativeY = 0;
        GetCursorCoordinates(editingText, fontSize, cursorPosition, &relativeX, &relativeY);
        int drawX = x + 10 + relativeX;
        int drawY = y + 10 + relativeY;
        int caretWidth = (fontSize >= 28) ? 3 : 2;
        DrawRectangle(drawX, drawY, caretWidth, fontSize, TEXT_EDIT_BORDER_COLOR);
    }
}

Rectangle GetBoxRect(const Box* box) {
    Rectangle rect = {(float)box->x, (float)box->y, (float)box->width, (float)box->height};
    return rect;
}

int IsPointInTextDragZone(const Box* box, Vector2 point) {
    Rectangle rect = GetBoxRect(box);
    if (!CheckCollisionPointRec(point, rect)) {
        return 0;
    }

    Rectangle inner = {
        rect.x + TEXT_DRAG_BORDER,
        rect.y + TEXT_DRAG_BORDER,
        rect.width - 2 * TEXT_DRAG_BORDER,
        rect.height - 2 * TEXT_DRAG_BORDER
    };

    if (inner.width <= 0 || inner.height <= 0) {
        return 1;
    }

    return !CheckCollisionPointRec(point, inner);
}

void SelectBox(Box* boxes, int boxCount, int index) {
    for (int i = 0; i < boxCount; i++) {
        boxes[i].isSelected = (i == index) ? 1 : 0;
    }
}

int FindTopmostBoxAtPoint(Vector2 point, Box* boxes, int boxCount) {
    for (int i = boxCount - 1; i >= 0; i--) {
        if (GetResizeModeForPoint(&boxes[i], point) != RESIZE_NONE) {
            return i;
        }
        if (CheckCollisionPointRec(point, GetBoxRect(&boxes[i]))) {
            return i;
        }
    }
    return -1;
}

ResizeMode GetResizeModeForPoint(const Box* box, Vector2 point) {
    Rectangle rect = GetBoxRect(box);
    float x = rect.x;
    float y = rect.y;
    float w = rect.width;
    float h = rect.height;

    Vector2 handleCenters[] = {
        {x, y},
        {x + w / 2.0f, y},
        {x + w, y},
        {x + w, y + h / 2.0f},
        {x + w, y + h},
        {x + w / 2.0f, y + h},
        {x, y + h},
        {x, y + h / 2.0f}
    };

    ResizeMode handleModes[] = {
        RESIZE_TOP_LEFT,
        RESIZE_TOP,
        RESIZE_TOP_RIGHT,
        RESIZE_RIGHT,
        RESIZE_BOTTOM_RIGHT,
        RESIZE_BOTTOM,
        RESIZE_BOTTOM_LEFT,
        RESIZE_LEFT
    };

    for (int i = 0; i < 8; i++) {
        Rectangle handleRect = {
            handleCenters[i].x - HANDLE_SIZE / 2.0f,
            handleCenters[i].y - HANDLE_SIZE / 2.0f,
            HANDLE_SIZE,
            HANDLE_SIZE
        };

        if (CheckCollisionPointRec(point, handleRect)) {
            return handleModes[i];
        }
    }

    const float margin = EDGE_DETECTION_MARGIN;
    if (point.x >= x - margin && point.x <= x + margin && point.y > y + margin && point.y < y + h - margin) {
        return RESIZE_LEFT;
    }
    if (point.x >= x + w - margin && point.x <= x + w + margin && point.y > y + margin && point.y < y + h - margin) {
        return RESIZE_RIGHT;
    }
    if (point.y >= y - margin && point.y <= y + margin && point.x > x + margin && point.x < x + w - margin) {
        return RESIZE_TOP;
    }
    if (point.y >= y + h - margin && point.y <= y + h + margin && point.x > x + margin && point.x < x + w - margin) {
        return RESIZE_BOTTOM;
    }

    return RESIZE_NONE;
}

void DrawResizeHandles(const Box* box) {
    Rectangle rect = GetBoxRect(box);

    Vector2 handleCenters[] = {
        {rect.x, rect.y},
        {rect.x + rect.width / 2.0f, rect.y},
        {rect.x + rect.width, rect.y},
        {rect.x + rect.width, rect.y + rect.height / 2.0f},
        {rect.x + rect.width, rect.y + rect.height},
        {rect.x + rect.width / 2.0f, rect.y + rect.height},
        {rect.x, rect.y + rect.height},
        {rect.x, rect.y + rect.height / 2.0f}
    };

    for (int i = 0; i < 8; i++) {
        Rectangle handleRect = {
            handleCenters[i].x - HANDLE_SIZE / 2.0f,
            handleCenters[i].y - HANDLE_SIZE / 2.0f,
            HANDLE_SIZE,
            HANDLE_SIZE
        };

        DrawRectangleRec(handleRect, LIGHTGRAY);
        DrawRectangleLinesEx(handleRect, 1.0f, DARKGRAY);
    }
}

void ApplyResize(Box* box, ResizeMode mode, Vector2 delta) {
    int dx = (int)delta.x;
    int dy = (int)delta.y;

    int affectsLeft = (mode == RESIZE_LEFT || mode == RESIZE_TOP_LEFT || mode == RESIZE_BOTTOM_LEFT);
    int affectsRight = (mode == RESIZE_RIGHT || mode == RESIZE_TOP_RIGHT || mode == RESIZE_BOTTOM_RIGHT);
    int affectsTop = (mode == RESIZE_TOP || mode == RESIZE_TOP_LEFT || mode == RESIZE_TOP_RIGHT);
    int affectsBottom = (mode == RESIZE_BOTTOM || mode == RESIZE_BOTTOM_LEFT || mode == RESIZE_BOTTOM_RIGHT);

    int newX = box->x;
    int newY = box->y;
    int newWidth = box->width;
    int newHeight = box->height;

    if (affectsLeft) {
        newX += dx;
        newWidth -= dx;
    }
    if (affectsRight) {
        newWidth += dx;
    }
    if (affectsTop) {
        newY += dy;
        newHeight -= dy;
    }
    if (affectsBottom) {
        newHeight += dy;
    }

    const int minWidth = 40;
    const int minHeight = 30;

    if (newWidth < minWidth) {
        if (affectsLeft) {
            newX -= (minWidth - newWidth);
        }
        newWidth = minWidth;
    }
    if (newHeight < minHeight) {
        if (affectsTop) {
            newY -= (minHeight - newHeight);
        }
        newHeight = minHeight;
    }

    box->x = newX;
    box->y = newY;
    box->width = newWidth;
    box->height = newHeight;
}

int MouseCursorForResizeMode(ResizeMode mode) {
    switch (mode) {
        case RESIZE_LEFT:
        case RESIZE_RIGHT:
            return MOUSE_CURSOR_RESIZE_EW;
        case RESIZE_TOP:
        case RESIZE_BOTTOM:
            return MOUSE_CURSOR_RESIZE_NS;
        case RESIZE_TOP_LEFT:
        case RESIZE_BOTTOM_RIGHT:
            return MOUSE_CURSOR_RESIZE_NWSE;
        case RESIZE_TOP_RIGHT:
        case RESIZE_BOTTOM_LEFT:
            return MOUSE_CURSOR_RESIZE_NESW;
        case RESIZE_NONE:
        default:
            return MOUSE_CURSOR_DEFAULT;
    }
}

int main(void)
{
    const int screenWidth = 800;
    const int screenHeight = 600;

    SetConfigFlags(FLAG_WINDOW_RESIZABLE);

    InitWindow(screenWidth, screenHeight, "Desktop Canvas App");
    InitAudioDevice();
    audioDeviceReady = IsAudioDeviceReady();
    if (!audioDeviceReady) {
        TraceLog(LOG_WARNING, "Audio device failed to initialize");
    }

    Box boxes[MAX_BOXES];
    int boxCount = 0;
    int selectedBox = -1;
    Vector2 mousePos, prevMousePos;
    int isDragging = 0;
    ResizeMode resizeMode = RESIZE_NONE;
    Tool currentTool = TOOL_SELECT;
    int isDrawing = 0;
    int startX, startY;
    Color currentDrawColor = BLACK;
    Vector2 penPoints[MAX_PEN_POINTS];
    int penPointCount = 0;
    float penMinX = 0.0f, penMinY = 0.0f, penMaxX = 0.0f, penMaxY = 0.0f;
    int requestExportClipboard = 0;
    float statusMessageTimer = 0.0f;
    char statusMessage[128] = {0};
    int showClearConfirm = 0;
    Box dragStartBox = {0};
    int dragBoxValid = 0;
    int dragChanged = 0;

    if (!audioDeviceReady) {
        snprintf(statusMessage, sizeof(statusMessage), "Audio disabled: device unavailable");
        statusMessageTimer = 3.0f;
    }

    /* Double-click detection */
    static double lastClickTime = 0.0;
    static Vector2 lastClickPos = {0, 0};
    const double doubleClickInterval = 0.5;  /* 500ms */
    const float doubleClickDistance = 10.0f;  /* 10 pixel tolerance */

    SetTargetFPS(60);

    int currentCursor = MOUSE_CURSOR_DEFAULT;

    PushHistoryState(boxes, boxCount, selectedBox);

    while (!WindowShouldClose())
    {
        mousePos = GetMousePosition();
        int ctrlDown = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
        int shiftDown = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);

        int screenWidthCurrent = GetScreenWidth();
        int screenHeightCurrent = GetScreenHeight();

        Rectangle toolbarRect = {0.0f, 0.0f, (float)screenWidthCurrent, TOOLBAR_HEIGHT};
        float buttonHeight = TOOLBAR_HEIGHT - 2.0f * TOOLBAR_PADDING;
        float toolButtonWidth = 64.0f;
        float xCursor = TOOLBAR_PADDING;

        Rectangle toolButtons[5];
        Tool toolOrder[5] = {TOOL_SELECT, TOOL_PEN, TOOL_SEGMENT, TOOL_RECT, TOOL_CIRCLE};
        const char* toolLabels[5] = {"Sel", "Pen", "Line", "Rect", "Circ"};
        const char* toolNames[5] = {"Select", "Pen", "Segment", "Rectangle", "Circle"};
        for (int i = 0; i < 5; i++) {
            toolButtons[i] = (Rectangle){xCursor, TOOLBAR_PADDING, toolButtonWidth, buttonHeight};
            xCursor += toolButtonWidth + 6.0f;
        }

        xCursor += 8.0f;

        Rectangle colorButtons[COLOR_PALETTE_COUNT];
        float colorSize = buttonHeight;
        for (int i = 0; i < COLOR_PALETTE_COUNT; i++) {
            colorButtons[i] = (Rectangle){xCursor, TOOLBAR_PADDING, colorSize, colorSize};
            xCursor += colorSize + 6.0f;
        }

        xCursor += 12.0f;
        float layerButtonWidth = 90.0f;
        Rectangle bringToFrontButton = {xCursor, TOOLBAR_PADDING, layerButtonWidth, buttonHeight};
        xCursor += layerButtonWidth + 6.0f;
        Rectangle sendToBackButton = {xCursor, TOOLBAR_PADDING, layerButtonWidth, buttonHeight};
        xCursor += layerButtonWidth + 12.0f;
        float exportButtonWidth = 140.0f;
        Rectangle exportButton = {xCursor, TOOLBAR_PADDING, exportButtonWidth, buttonHeight};
        xCursor += exportButtonWidth + 6.0f;
        float clearButtonWidth = 110.0f;
        Rectangle clearButton = {xCursor, TOOLBAR_PADDING, clearButtonWidth, buttonHeight};

        if (statusMessageTimer > 0.0f) {
            statusMessageTimer -= GetFrameTime();
            if (statusMessageTimer < 0.0f) {
                statusMessageTimer = 0.0f;
            }
        }

        int overToolbar = (mousePos.y <= TOOLBAR_HEIGHT);

        int hoveredToolIndex = -1;
        int hoveredColorIndex = -1;
        int hoveredBringToFront = 0;
        int hoveredSendToBack = 0;
        int hoveredExport = 0;
        int hoveredClear = 0;

        if (overToolbar && !showClearConfirm) {
            for (int i = 0; i < 5; i++) {
                if (CheckCollisionPointRec(mousePos, toolButtons[i])) {
                    hoveredToolIndex = i;
                    break;
                }
            }
            for (int i = 0; i < COLOR_PALETTE_COUNT; i++) {
                if (CheckCollisionPointRec(mousePos, colorButtons[i])) {
                    hoveredColorIndex = i;
                    break;
                }
            }
            hoveredBringToFront = CheckCollisionPointRec(mousePos, bringToFrontButton);
            hoveredSendToBack = CheckCollisionPointRec(mousePos, sendToBackButton);
            hoveredExport = CheckCollisionPointRec(mousePos, exportButton);
            hoveredClear = CheckCollisionPointRec(mousePos, clearButton);
        }

        int hoveredBox = FindTopmostBoxAtPoint(mousePos, boxes, boxCount);
        ResizeMode hoverResizeMode = RESIZE_NONE;
        if (hoveredBox != -1) {
            hoverResizeMode = GetResizeModeForPoint(&boxes[hoveredBox], mousePos);
        }
        if (overToolbar && !isDragging) {
            hoveredBox = -1;
            hoverResizeMode = RESIZE_NONE;
        }

        Rectangle confirmDialogRect = (Rectangle){0};
        Rectangle confirmYesRect = (Rectangle){0};
        Rectangle confirmNoRect = (Rectangle){0};

        if (!showClearConfirm) {
            HandleTextInput(boxes, statusMessage, sizeof(statusMessage), &statusMessageTimer);

            if (IsKeyPressed(KEY_ESCAPE)) {
                if (editingBoxIndex >= 0) {
                    StopTextEditAndRecord(boxes, boxCount, selectedBox);
                } else {
                    currentTool = TOOL_SELECT;
                    SelectBox(boxes, boxCount, -1);
                    selectedBox = -1;
                }
            }

            int handledToolbarClick = 0;

            if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                if (overToolbar) {
                    handledToolbarClick = 1;
                    int actionHandled = 0;

                    for (int i = 0; i < 5; i++) {
                        if (CheckCollisionPointRec(mousePos, toolButtons[i])) {
                            currentTool = toolOrder[i];
                            actionHandled = 1;
                            if (currentTool != TOOL_SELECT && editingBoxIndex >= 0) {
                                StopTextEditAndRecord(boxes, boxCount, selectedBox);
                            }
                            break;
                        }
                    }

                    if (!actionHandled) {
                        for (int i = 0; i < COLOR_PALETTE_COUNT; i++) {
                            if (CheckCollisionPointRec(mousePos, colorButtons[i])) {
                                Color chosenColor = COLOR_PALETTE[i];
                                int targetIndex = (editingBoxIndex >= 0) ? editingBoxIndex : selectedBox;
                                int textColorChanged = 0;
                                currentDrawColor = chosenColor;

                                if (targetIndex != -1 && boxes[targetIndex].type == BOX_TEXT) {
                                    Color previous = boxes[targetIndex].textColor;
                                    boxes[targetIndex].textColor = chosenColor;
                                    if (!ColorsEqual(previous, chosenColor)) {
                                        textColorChanged = 1;
                                    }
                                }

                                if (textColorChanged) {
                                    int historySelection = (targetIndex != -1) ? targetIndex : selectedBox;
                                    PushHistoryState(boxes, boxCount, historySelection);
                                }
                                actionHandled = 1;
                                break;
                            }
                        }
                    }

                    if (!actionHandled && CheckCollisionPointRec(mousePos, bringToFrontButton) && selectedBox != -1) {
                        int previousIndex = selectedBox;
                        int newIndex = BringBoxToFront(boxes, boxCount, selectedBox);
                        SelectBox(boxes, boxCount, newIndex);
                        if (editingBoxIndex == selectedBox) {
                            editingBoxIndex = newIndex;
                        } else if (editingBoxIndex > selectedBox) {
                            editingBoxIndex--;
                        }
                        selectedBox = newIndex;
                        if (newIndex != previousIndex) {
                            PushHistoryState(boxes, boxCount, selectedBox);
                        }
                        actionHandled = 1;
                    }

                    if (!actionHandled && CheckCollisionPointRec(mousePos, sendToBackButton) && selectedBox != -1) {
                        int previousIndex = selectedBox;
                        int newIndex = SendBoxToBack(boxes, boxCount, selectedBox);
                        SelectBox(boxes, boxCount, newIndex);
                        if (editingBoxIndex == selectedBox) {
                            editingBoxIndex = newIndex;
                        } else if (editingBoxIndex >= 0 && editingBoxIndex < selectedBox) {
                            editingBoxIndex++;
                        }
                        selectedBox = newIndex;
                        if (newIndex != previousIndex) {
                            PushHistoryState(boxes, boxCount, selectedBox);
                        }
                        actionHandled = 1;
                    }

                    if (!actionHandled && CheckCollisionPointRec(mousePos, exportButton)) {
                        requestExportClipboard = 1;
                        snprintf(statusMessage, sizeof(statusMessage), "Preparing canvas export...");
                        statusMessageTimer = 2.0f;
                        actionHandled = 1;
                    }

                    if (!actionHandled && CheckCollisionPointRec(mousePos, clearButton)) {
                        if (boxCount > 0) {
                            showClearConfirm = 1;
                            isDragging = 0;
                            isDrawing = 0;
                            resizeMode = RESIZE_NONE;
                            if (editingBoxIndex >= 0) {
                                StopTextEditAndRecord(boxes, boxCount, selectedBox);
                            }
                            SelectBox(boxes, boxCount, -1);
                            selectedBox = -1;
                        } else {
                            snprintf(statusMessage, sizeof(statusMessage), "Canvas already empty");
                            statusMessageTimer = 1.5f;
                        }
                        actionHandled = 1;
                    }

                    lastClickTime = 0.0;
                }

                if (!handledToolbarClick) {
                    if (currentTool == TOOL_SELECT) {
                        int handledTextCaret = 0;
                        if (editingBoxIndex >= 0) {
                            Rectangle editingRect = {
                                boxes[editingBoxIndex].x,
                                boxes[editingBoxIndex].y,
                                (float)boxes[editingBoxIndex].width,
                                (float)boxes[editingBoxIndex].height
                            };

                            if (!CheckCollisionPointRec(mousePos, editingRect)) {
                                StopTextEditAndRecord(boxes, boxCount, selectedBox);
                            } else {
                                ResizeMode editHover = GetResizeModeForPoint(&boxes[editingBoxIndex], mousePos);
                                int onHandle = (editHover != RESIZE_NONE);
                                int onDragBorder = IsPointInTextDragZone(&boxes[editingBoxIndex], mousePos);
                                if (!onHandle && !onDragBorder) {
                                    Vector2 localPoint = {
                                        mousePos.x - (float)boxes[editingBoxIndex].x,
                                        mousePos.y - (float)boxes[editingBoxIndex].y
                                    };
                                    int caretIndex = GetTextIndexFromPoint(editingText, editingFontSize, localPoint);
                                    MoveCursorTo(caretIndex, shiftDown);
                                    isMouseSelecting = 1;
                                    cursorPreferredColumn = -1;
                                    dragBoxValid = 0;
                                    isDragging = 0;
                                    handledTextCaret = 1;
                                }
                            }
                        }

                        if (handledTextCaret) {
                            lastClickTime = 0.0;
                        } else {
                            double currentTime = GetTime();
                            float dx = mousePos.x - lastClickPos.x;
                            float dy = mousePos.y - lastClickPos.y;
                            float clickDistance = sqrtf(dx*dx + dy*dy);
                            int isDoubleClick = (currentTime - lastClickTime < doubleClickInterval) &&
                                                (clickDistance < doubleClickDistance);

                            if (isDoubleClick) {
                                int clickedBox = FindTopmostBoxAtPoint(mousePos, boxes, boxCount);

                                if (clickedBox != -1) {
                                    selectedBox = clickedBox;
                                    SelectBox(boxes, boxCount, selectedBox);

                                    if (boxes[clickedBox].type == BOX_TEXT) {
                                        StartTextEdit(clickedBox, boxes);
                                    } else if (boxes[clickedBox].type == BOX_AUDIO) {
                                        ToggleAudioPlayback(&boxes[clickedBox], statusMessage, sizeof(statusMessage), &statusMessageTimer);
                                    } else if (boxCount < MAX_BOXES) {
                                        int textWidth, textHeight;
                                        const char* newText = "New text";
                                        CalculateTextBoxSize(newText, DEFAULT_FONT_SIZE, &textWidth, &textHeight);

                                        boxes[boxCount].x = (int)mousePos.x;
                                        boxes[boxCount].y = (int)mousePos.y;
                                        boxes[boxCount].width = textWidth;
                                        boxes[boxCount].height = textHeight;
                                        boxes[boxCount].type = BOX_TEXT;
                                        boxes[boxCount].content.text = strdup(newText);
                                        boxes[boxCount].filePath = NULL;
                                        boxes[boxCount].fontSize = DEFAULT_FONT_SIZE;
                                        boxes[boxCount].textColor = currentDrawColor;
                                        boxes[boxCount].isSelected = 0;
                                        boxCount++;
                                        selectedBox = boxCount - 1;
                                        SelectBox(boxes, boxCount, selectedBox);
                                        selectAllOnStart = 1;
                                        StartTextEdit(selectedBox, boxes);
                                        PushHistoryState(boxes, boxCount, selectedBox);
                                    }
                                } else if (boxCount < MAX_BOXES) {
                                    int textWidth, textHeight;
                                    const char* newText = "New text";
                                    CalculateTextBoxSize(newText, DEFAULT_FONT_SIZE, &textWidth, &textHeight);

                                    boxes[boxCount].x = (int)mousePos.x;
                                    boxes[boxCount].y = (int)mousePos.y;
                                    boxes[boxCount].width = textWidth;
                                    boxes[boxCount].height = textHeight;
                                    boxes[boxCount].type = BOX_TEXT;
                                    boxes[boxCount].content.text = strdup(newText);
                                    boxes[boxCount].filePath = NULL;
                                    boxes[boxCount].fontSize = DEFAULT_FONT_SIZE;
                                    boxes[boxCount].textColor = currentDrawColor;
                                    boxes[boxCount].isSelected = 0;
                                    boxCount++;
                                    selectedBox = boxCount - 1;
                                    SelectBox(boxes, boxCount, selectedBox);
                                    selectAllOnStart = 1;
                                    StartTextEdit(selectedBox, boxes);
                                    PushHistoryState(boxes, boxCount, selectedBox);
                                }

                                lastClickTime = 0.0;
                                continue;
                            }

                            lastClickTime = currentTime;
                            lastClickPos = mousePos;
                        }

                        int clickedBox = FindTopmostBoxAtPoint(mousePos, boxes, boxCount);
                        if (clickedBox != -1) {
                            selectedBox = clickedBox;
                            SelectBox(boxes, boxCount, selectedBox);
                            resizeMode = GetResizeModeForPoint(&boxes[selectedBox], mousePos);
                            isDragging = 1;
                            dragStartBox = boxes[selectedBox];
                            dragBoxValid = 1;
                            dragChanged = 0;

                            if (boxes[selectedBox].type == BOX_TEXT && editingBoxIndex == selectedBox) {
                                int onHandle = (resizeMode != RESIZE_NONE);
                                int onDragBorder = IsPointInTextDragZone(&boxes[selectedBox], mousePos);

                                if (!onHandle && !onDragBorder) {
                                    isDragging = 0;
                                    dragBoxValid = 0;
                                }
                            }
                        } else {
                            SelectBox(boxes, boxCount, -1);
                            selectedBox = -1;
                            resizeMode = RESIZE_NONE;
                            isDragging = 0;
                            dragBoxValid = 0;

                            if (editingBoxIndex >= 0) {
                                StopTextEditAndRecord(boxes, boxCount, selectedBox);
                            }

                            if (currentTool == TOOL_RECT || currentTool == TOOL_CIRCLE) {
                                startX = (int)mousePos.x;
                                startY = (int)mousePos.y;
                                isDrawing = 1;
                            }
                        }
                    } else {
                        if (editingBoxIndex >= 0) {
                            StopTextEditAndRecord(boxes, boxCount, selectedBox);
                        }
                        SelectBox(boxes, boxCount, -1);
                        selectedBox = -1;
                        isDragging = 0;
                        resizeMode = RESIZE_NONE;
                        dragBoxValid = 0;

                        if (currentTool == TOOL_PEN) {
                            isDrawing = 1;
                            penPointCount = 0;
                            if (penPointCount < MAX_PEN_POINTS) {
                                penPoints[penPointCount++] = mousePos;
                                penMinX = mousePos.x;
                                penMinY = mousePos.y;
                                penMaxX = mousePos.x;
                                penMaxY = mousePos.y;
                            }
                        } else if (currentTool == TOOL_RECT || currentTool == TOOL_CIRCLE || currentTool == TOOL_SEGMENT) {
                            startX = (int)mousePos.x;
                            startY = (int)mousePos.y;
                            isDrawing = 1;
                        }
                    }
                }
            }

            if (IsMouseButtonDown(MOUSE_LEFT_BUTTON) && isDragging && selectedBox != -1) {
                Vector2 delta = {mousePos.x - prevMousePos.x, mousePos.y - prevMousePos.y};
                if (resizeMode == RESIZE_NONE) {
                    boxes[selectedBox].x += (int)delta.x;
                    boxes[selectedBox].y += (int)delta.y;
                    if ((int)delta.x != 0 || (int)delta.y != 0) {
                        dragChanged = 1;
                    }
                } else {
                    ApplyResize(&boxes[selectedBox], resizeMode, delta);
                    if ((int)delta.x != 0 || (int)delta.y != 0) {
                        dragChanged = 1;
                    }
                }
            }

            if (isMouseSelecting && IsMouseButtonDown(MOUSE_LEFT_BUTTON) && editingBoxIndex >= 0) {
                Rectangle editingRect = {
                    boxes[editingBoxIndex].x,
                    boxes[editingBoxIndex].y,
                    (float)boxes[editingBoxIndex].width,
                    (float)boxes[editingBoxIndex].height
                };
                if (CheckCollisionPointRec(mousePos, editingRect)) {
                    Vector2 localPoint = {
                        mousePos.x - (float)boxes[editingBoxIndex].x,
                        mousePos.y - (float)boxes[editingBoxIndex].y
                    };
                    int caretIndex = GetTextIndexFromPoint(editingText, editingFontSize, localPoint);
                    MoveCursorTo(caretIndex, 1);
                }
            }

            if (IsMouseButtonDown(MOUSE_LEFT_BUTTON) && isDrawing && currentTool == TOOL_PEN && penPointCount > 0) {
                Vector2 lastPoint = penPoints[penPointCount - 1];
                float dx = mousePos.x - lastPoint.x;
                float dy = mousePos.y - lastPoint.y;
                float distSq = dx * dx + dy * dy;
                if (distSq >= 1.0f && penPointCount < MAX_PEN_POINTS) {
                    penPoints[penPointCount++] = mousePos;
                    if (mousePos.x < penMinX) penMinX = mousePos.x;
                    if (mousePos.y < penMinY) penMinY = mousePos.y;
                    if (mousePos.x > penMaxX) penMaxX = mousePos.x;
                    if (mousePos.y > penMaxY) penMaxY = mousePos.y;
                }
            }

            if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) {
                int wasDragging = isDragging;
                if (isDragging) {
                    isDragging = 0;
                    resizeMode = RESIZE_NONE;
                }

                if (isMouseSelecting) {
                    isMouseSelecting = 0;
                    cursorPreferredColumn = -1;
                }

                if (wasDragging && dragBoxValid && dragChanged) {
                    PushHistoryState(boxes, boxCount, selectedBox);
                }
                if (wasDragging) {
                    dragBoxValid = 0;
                    dragChanged = 0;
                }

                if (isDrawing) {
                    int shapeAdded = 0;
                    if (currentTool == TOOL_RECT && boxCount < MAX_BOXES) {
                        int endX = (int)mousePos.x;
                        int endY = (int)mousePos.y;
                        int x = startX < endX ? startX : endX;
                        int y = startY < endY ? startY : endY;
                        int width = abs(endX - startX);
                        int height = abs(endY - startY);
                        if (width > 0 && height > 0) {
                            RenderTexture2D rt = LoadRenderTexture(width, height);
                            BeginTextureMode(rt);
                            ClearBackground(BLANK);
                            DrawRectangleLines(0, 0, width, height, currentDrawColor);
                            EndTextureMode();
                            boxes[boxCount].x = x;
                            boxes[boxCount].y = y;
                            boxes[boxCount].width = width;
                            boxes[boxCount].height = height;
                            boxes[boxCount].type = BOX_DRAWING;
                            boxes[boxCount].content.texture = rt.texture;
                            boxes[boxCount].filePath = NULL;
                            boxes[boxCount].isSelected = 0;
                            boxCount++;
                            selectedBox = boxCount - 1;
                            SelectBox(boxes, boxCount, selectedBox);
                            shapeAdded = 1;
                        }
                    } else if (currentTool == TOOL_CIRCLE && boxCount < MAX_BOXES) {
                        int centerX = startX;
                        int centerY = startY;
                        float dx = mousePos.x - startX;
                        float dy = mousePos.y - startY;
                        int radius = (int)sqrtf(dx * dx + dy * dy);
                        if (radius > 0) {
                            int x = centerX - radius;
                            int y = centerY - radius;
                            int width = radius * 2;
                            int height = radius * 2;
                            RenderTexture2D rt = LoadRenderTexture(width, height);
                            BeginTextureMode(rt);
                            ClearBackground(BLANK);
                            DrawCircleLines(radius, radius, (float)radius, currentDrawColor);
                            EndTextureMode();
                            boxes[boxCount].x = x;
                            boxes[boxCount].y = y;
                            boxes[boxCount].width = width;
                            boxes[boxCount].height = height;
                            boxes[boxCount].type = BOX_DRAWING;
                            boxes[boxCount].content.texture = rt.texture;
                            boxes[boxCount].filePath = NULL;
                            boxes[boxCount].isSelected = 0;
                            boxCount++;
                            selectedBox = boxCount - 1;
                            SelectBox(boxes, boxCount, selectedBox);
                            shapeAdded = 1;
                        }
                    } else if (currentTool == TOOL_SEGMENT && boxCount < MAX_BOXES) {
                        int endX = (int)mousePos.x;
                        int endY = (int)mousePos.y;
                        float minX = (startX < endX ? startX : endX) - STROKE_THICKNESS;
                        float minY = (startY < endY ? startY : endY) - STROKE_THICKNESS;
                        float maxX = (startX > endX ? startX : endX) + STROKE_THICKNESS;
                        float maxY = (startY > endY ? startY : endY) + STROKE_THICKNESS;
                        int width = (int)fmaxf(2.0f, maxX - minX);
                        int height = (int)fmaxf(2.0f, maxY - minY);
                        RenderTexture2D rt = LoadRenderTexture(width, height);
                        BeginTextureMode(rt);
                        ClearBackground(BLANK);
                        Vector2 start = {(float)startX - minX, (float)startY - minY};
                        Vector2 end = {(float)endX - minX, (float)endY - minY};
                        DrawLineEx(start, end, STROKE_THICKNESS, currentDrawColor);
                        EndTextureMode();
                        boxes[boxCount].x = (int)minX;
                        boxes[boxCount].y = (int)minY;
                        boxes[boxCount].width = width;
                        boxes[boxCount].height = height;
                        boxes[boxCount].type = BOX_DRAWING;
                        boxes[boxCount].content.texture = rt.texture;
                            boxes[boxCount].filePath = NULL;
                        boxes[boxCount].isSelected = 0;
                        boxCount++;
                        selectedBox = boxCount - 1;
                        SelectBox(boxes, boxCount, selectedBox);
                        shapeAdded = 1;
                    } else if (currentTool == TOOL_PEN && boxCount < MAX_BOXES && penPointCount > 0) {
                        float minX = penMinX - STROKE_THICKNESS;
                        float minY = penMinY - STROKE_THICKNESS;
                        float widthF = (penMaxX - penMinX) + STROKE_THICKNESS * 2.0f;
                        float heightF = (penMaxY - penMinY) + STROKE_THICKNESS * 2.0f;
                        int width = (int)fmaxf(2.0f, widthF);
                        int height = (int)fmaxf(2.0f, heightF);
                        RenderTexture2D rt = LoadRenderTexture(width, height);
                        BeginTextureMode(rt);
                        ClearBackground(BLANK);
                        if (penPointCount == 1) {
                            DrawCircleV((Vector2){penPoints[0].x - minX, penPoints[0].y - minY}, STROKE_THICKNESS * 0.5f, currentDrawColor);
                        } else {
                            Vector2 prev = {(penPoints[0].x - minX), (penPoints[0].y - minY)};
                            for (int i = 1; i < penPointCount; i++) {
                                Vector2 curr = {(penPoints[i].x - minX), (penPoints[i].y - minY)};
                                DrawLineEx(prev, curr, STROKE_THICKNESS, currentDrawColor);
                                prev = curr;
                            }
                        }
                        EndTextureMode();
                        boxes[boxCount].x = (int)minX;
                        boxes[boxCount].y = (int)minY;
                        boxes[boxCount].width = width;
                        boxes[boxCount].height = height;
                        boxes[boxCount].type = BOX_DRAWING;
                        boxes[boxCount].content.texture = rt.texture;
                        boxes[boxCount].filePath = NULL;
                        boxes[boxCount].isSelected = 0;
                        boxCount++;
                        selectedBox = boxCount - 1;
                        SelectBox(boxes, boxCount, selectedBox);
                        shapeAdded = 1;
                    }

                    isDrawing = 0;
                    penPointCount = 0;
                    if (shapeAdded) {
                        PushHistoryState(boxes, boxCount, selectedBox);
                    }
                }
            }

            if (IsKeyPressed(KEY_DELETE) && selectedBox != -1) {
                DestroyBox(&boxes[selectedBox]);
                if (editingBoxIndex == selectedBox) {
                    ResetEditingState();
                } else if (editingBoxIndex > selectedBox) {
                    editingBoxIndex--;
                }
                for (int i = selectedBox; i < boxCount - 1; i++) {
                    boxes[i] = boxes[i + 1];
                }
                boxes[boxCount - 1] = (Box){0};
                boxCount--;
                selectedBox = -1;
                SelectBox(boxes, boxCount, -1);
                PushHistoryState(boxes, boxCount, selectedBox);
            }
        } else {
            confirmDialogRect = (Rectangle){(screenWidthCurrent - 320.0f) / 2.0f, (screenHeightCurrent - 180.0f) / 2.0f, 320.0f, 180.0f};
            confirmYesRect = (Rectangle){confirmDialogRect.x + 28.0f, confirmDialogRect.y + confirmDialogRect.height - 60.0f, 110.0f, 40.0f};
            confirmNoRect = (Rectangle){confirmDialogRect.x + confirmDialogRect.width - 138.0f, confirmDialogRect.y + confirmDialogRect.height - 60.0f, 110.0f, 40.0f};

            if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                if (CheckCollisionPointRec(mousePos, confirmYesRect)) {
                    ClearAllBoxes(boxes, &boxCount, &selectedBox);
                    PushHistoryState(boxes, boxCount, selectedBox);
                    snprintf(statusMessage, sizeof(statusMessage), "Canvas cleared");
                    statusMessageTimer = 2.0f;
                    showClearConfirm = 0;
                } else if (CheckCollisionPointRec(mousePos, confirmNoRect) || !CheckCollisionPointRec(mousePos, confirmDialogRect)) {
                    showClearConfirm = 0;
                }
            }

            if (IsKeyPressed(KEY_ESCAPE)) {
                showClearConfirm = 0;
            }
        }

        int desiredCursor = MOUSE_CURSOR_DEFAULT;

        if (editingBoxIndex >= 0) {
            Rectangle editingRect = {
                boxes[editingBoxIndex].x,
                boxes[editingBoxIndex].y,
                (float)boxes[editingBoxIndex].width,
                (float)boxes[editingBoxIndex].height
            };

            if (CheckCollisionPointRec(mousePos, editingRect)) {
                desiredCursor = MOUSE_CURSOR_IBEAM;
            }
        }

        if (isDragging && selectedBox != -1) {
            if (resizeMode == RESIZE_NONE) {
                desiredCursor = MOUSE_CURSOR_RESIZE_ALL;
            } else {
                desiredCursor = MouseCursorForResizeMode(resizeMode);
            }
        } else if (hoverResizeMode != RESIZE_NONE && hoveredBox != -1) {
            desiredCursor = MouseCursorForResizeMode(hoverResizeMode);
        } else if (hoveredBox != -1 && boxes[hoveredBox].type == BOX_TEXT && editingBoxIndex == hoveredBox && IsPointInTextDragZone(&boxes[hoveredBox], mousePos)) {
            desiredCursor = MOUSE_CURSOR_RESIZE_ALL;
        } else if (hoveredBox != -1 && boxes[hoveredBox].isSelected) {
            desiredCursor = MOUSE_CURSOR_POINTING_HAND;
        }

        if (!showClearConfirm && currentTool != TOOL_SELECT && desiredCursor == MOUSE_CURSOR_DEFAULT && !overToolbar) {
            desiredCursor = MOUSE_CURSOR_CROSSHAIR;
        }

        if (desiredCursor != currentCursor) {
            SetMouseCursor(desiredCursor);
            currentCursor = desiredCursor;
        }

        prevMousePos = mousePos;

        if (!showClearConfirm) {
            if (IsKeyPressed(KEY_S)) {
                currentTool = TOOL_SELECT;
            }
            if (IsKeyPressed(KEY_P)) {
                if (editingBoxIndex >= 0) {
                            StopTextEditAndRecord(boxes, boxCount, selectedBox);
                }
                currentTool = TOOL_PEN;
            }
            if (IsKeyPressed(KEY_L)) {
                if (editingBoxIndex >= 0) {
                            StopTextEditAndRecord(boxes, boxCount, selectedBox);
                }
                currentTool = TOOL_SEGMENT;
            }
            if (IsKeyPressed(KEY_R)) {
                if (editingBoxIndex >= 0) {
                            StopTextEditAndRecord(boxes, boxCount, selectedBox);
                }
                currentTool = TOOL_RECT;
            }
            if (IsKeyPressed(KEY_C)) {
                if (editingBoxIndex >= 0) {
                            StopTextEditAndRecord(boxes, boxCount, selectedBox);
                }
                currentTool = TOOL_CIRCLE;
            }

            if (ctrlDown) {
                int pressedZ = IsKeyPressed(KEY_Z);
                int pressedY = IsKeyPressed(KEY_Y);
                int pressedW = IsKeyPressed(KEY_W);  /* AZERTY keyboards map Ctrl+Z to this physical key */

                int undoCombo = pressedZ || pressedW;
                int redoCombo = pressedY || (shiftDown && undoCombo);

                if (redoCombo) {
                    if (editingBoxIndex >= 0) {
                        StopTextEditAndRecord(boxes, boxCount, selectedBox);
                    }
                    if (PerformRedo(boxes, &boxCount, &selectedBox)) {
                        snprintf(statusMessage, sizeof(statusMessage), "Redo");
                        statusMessageTimer = 1.2f;
                    } else {
                        snprintf(statusMessage, sizeof(statusMessage), "Nothing to redo");
                        statusMessageTimer = 1.2f;
                    }
                } else if (undoCombo) {
                    if (editingBoxIndex >= 0) {
                        StopTextEditAndRecord(boxes, boxCount, selectedBox);
                    }
                    if (PerformUndo(boxes, &boxCount, &selectedBox)) {
                        snprintf(statusMessage, sizeof(statusMessage), "Undo");
                        statusMessageTimer = 1.2f;
                    } else {
                        snprintf(statusMessage, sizeof(statusMessage), "Nothing to undo");
                        statusMessageTimer = 1.2f;
                    }
                }
            }

            if (selectedBox != -1 && boxes[selectedBox].type == BOX_AUDIO && IsKeyPressed(KEY_SPACE)) {
                ToggleAudioPlayback(&boxes[selectedBox], statusMessage, sizeof(statusMessage), &statusMessageTimer);
            }
        }

        /* Paste */
        if (!showClearConfirm && ctrlDown && IsKeyPressed(KEY_V) && editingBoxIndex < 0) {
            int handledPaste = 0;
#ifdef _WIN32
            if (WinClip_HasFileDrop() && boxCount < MAX_BOXES) {
                int dropCount = 0;
                char** dropList = WinClip_GetFileDropList(&dropCount);
                if (dropList != NULL && dropCount > 0) {
                    int added = 0;
                    int limited = 0;
                    for (int i = 0; i < dropCount && boxCount < MAX_BOXES; ++i) {
                        const char* filePath = dropList[i];
                        if (filePath == NULL || filePath[0] == '\0') {
                            continue;
                        }

                        int baseX = (int)mousePos.x + added * 24;
                        int baseY = (int)mousePos.y + added * 24;
                        int created = 0;
                        const char* ext = strrchr(filePath, '.');

                        if (ext != NULL && (EqualsIgnoreCase(ext, ".png") || EqualsIgnoreCase(ext, ".jpg") ||
                                            EqualsIgnoreCase(ext, ".jpeg") || EqualsIgnoreCase(ext, ".bmp"))) {
                            Image img = LoadImage(filePath);
                            if (IsImageReady(img)) {
                                Texture2D tex = LoadTextureFromImage(img);
                                boxes[boxCount].x = baseX;
                                boxes[boxCount].y = baseY;
                                boxes[boxCount].width = img.width;
                                boxes[boxCount].height = img.height;
                                boxes[boxCount].type = BOX_IMAGE;
                                boxes[boxCount].content.texture = tex;
                                boxes[boxCount].filePath = NULL;
                                boxes[boxCount].isSelected = 0;
                                boxCount++;
                                selectedBox = boxCount - 1;
                                SelectBox(boxes, boxCount, selectedBox);
                                PushHistoryState(boxes, boxCount, selectedBox);
                                created = 1;
                            }
                            UnloadImage(img);
                        } else if (ext != NULL && (EqualsIgnoreCase(ext, ".wav") || EqualsIgnoreCase(ext, ".ogg") ||
                                                     EqualsIgnoreCase(ext, ".mp3") || EqualsIgnoreCase(ext, ".flac"))) {
                            char* storedPath = strdup(filePath);
                            if (storedPath != NULL) {
                                Sound sound = (Sound){0};
                                int soundReady = 0;
                                if (audioDeviceReady) {
                                    Sound loaded = LoadSound(filePath);
                                    if (IsSoundReady(loaded)) {
                                        sound = loaded;
                                        soundReady = 1;
                                    } else {
                                        UnloadSound(loaded);
                                    }
                                }

                                boxes[boxCount].x = baseX;
                                boxes[boxCount].y = baseY;
                                boxes[boxCount].width = AUDIO_BOX_WIDTH;
                                boxes[boxCount].height = AUDIO_BOX_HEIGHT;
                                boxes[boxCount].type = BOX_AUDIO;
                                boxes[boxCount].content.sound = sound;
                                boxes[boxCount].filePath = storedPath;
                                boxes[boxCount].fontSize = 0;
                                boxes[boxCount].textColor = BLACK;
                                boxes[boxCount].isSelected = 0;
                                boxCount++;
                                selectedBox = boxCount - 1;
                                SelectBox(boxes, boxCount, selectedBox);
                                PushHistoryState(boxes, boxCount, selectedBox);
                                created = 1;

                                const char* audioName = ExtractFileName(storedPath);
                                if (soundReady) {
                                    snprintf(statusMessage, sizeof(statusMessage), "Loaded %s", audioName);
                                    statusMessageTimer = 1.6f;
                                } else if (!audioDeviceReady) {
                                    snprintf(statusMessage, sizeof(statusMessage), "Audio placeholder: device unavailable");
                                    statusMessageTimer = 2.0f;
                                } else {
                                    snprintf(statusMessage, sizeof(statusMessage), "Audio placeholder: failed to load %s", audioName);
                                    statusMessageTimer = 2.0f;
                                }
                            }
                        }

                        if (!created) {
                            int textWidth = 0;
                            int textHeight = 0;
                            CalculateTextBoxSize(filePath, DEFAULT_FONT_SIZE, &textWidth, &textHeight);
                            char* textCopy = strdup(filePath);
                            if (textCopy != NULL) {
                                boxes[boxCount].x = baseX;
                                boxes[boxCount].y = baseY;
                                boxes[boxCount].width = textWidth;
                                boxes[boxCount].height = textHeight;
                                boxes[boxCount].type = BOX_TEXT;
                                boxes[boxCount].content.text = textCopy;
                                boxes[boxCount].fontSize = DEFAULT_FONT_SIZE;
                                boxes[boxCount].textColor = currentDrawColor;
                                boxes[boxCount].filePath = NULL;
                                boxes[boxCount].isSelected = 0;
                                boxCount++;
                                selectedBox = boxCount - 1;
                                SelectBox(boxes, boxCount, selectedBox);
                                PushHistoryState(boxes, boxCount, selectedBox);
                                created = 1;
                            }
                        }

                        if (created) {
                            added++;
                        }
                    }

                    if (dropCount > 0 && boxCount >= MAX_BOXES) {
                        limited = 1;
                    }

                    if (added > 0) {
                        handledPaste = 1;
                        if (limited) {
                            snprintf(statusMessage, sizeof(statusMessage), "Imported %d file%s (canvas full)", added, added == 1 ? "" : "s");
                            statusMessageTimer = 2.0f;
                        } else if (statusMessageTimer <= 0.0f) {
                            snprintf(statusMessage, sizeof(statusMessage), "Imported %d file%s", added, added == 1 ? "" : "s");
                            statusMessageTimer = 1.8f;
                        }
                    }

                    WinClip_FreeFileDropList(dropList, dropCount);
                } else if (dropList != NULL) {
                    WinClip_FreeFileDropList(dropList, dropCount);
                }
            }

            if (!handledPaste && WinClip_HasImage() && boxCount < MAX_BOXES) {
                int imgWidth, imgHeight, imgChannels;
                void* imgData = WinClip_GetImageData(&imgWidth, &imgHeight, &imgChannels);

                if (imgData != NULL) {
                    Image img = {
                        .data = imgData,
                        .width = imgWidth,
                        .height = imgHeight,
                        .mipmaps = 1,
                        .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8
                    };

                    Texture2D tex = LoadTextureFromImage(img);

                    boxes[boxCount].x = (int)mousePos.x;
                    boxes[boxCount].y = (int)mousePos.y;
                    boxes[boxCount].width = imgWidth;
                    boxes[boxCount].height = imgHeight;
                    boxes[boxCount].type = BOX_IMAGE;
                    boxes[boxCount].content.texture = tex;
                    boxes[boxCount].filePath = NULL;
                    boxes[boxCount].isSelected = 0;
                    boxCount++;
                    selectedBox = boxCount - 1;
                    SelectBox(boxes, boxCount, selectedBox);
                    PushHistoryState(boxes, boxCount, selectedBox);

                    WinClip_FreeData(imgData);
                    handledPaste = 1;
                } else {
                    const char* errorText = "(Image from clipboard - processing failed)";
                    int textWidth, textHeight;
                    CalculateTextBoxSize(errorText, DEFAULT_FONT_SIZE, &textWidth, &textHeight);

                    boxes[boxCount].x = (int)mousePos.x;
                    boxes[boxCount].y = (int)mousePos.y;
                    boxes[boxCount].width = textWidth;
                    boxes[boxCount].height = textHeight;
                    boxes[boxCount].type = BOX_TEXT;
                    boxes[boxCount].content.text = strdup(errorText);
                    boxes[boxCount].fontSize = DEFAULT_FONT_SIZE;
                    boxes[boxCount].textColor = currentDrawColor;
                    boxes[boxCount].filePath = NULL;
                    boxes[boxCount].isSelected = 0;
                    boxCount++;
                    selectedBox = boxCount - 1;
                    SelectBox(boxes, boxCount, selectedBox);
                    PushHistoryState(boxes, boxCount, selectedBox);
                    handledPaste = 1;
                }
            }
#endif

            if (!handledPaste) {
                const char* clip = GetClipboardTextSafe();
                if (clip && strlen(clip) > 0 && boxCount < MAX_BOXES) {
                    char* path = DuplicateSanitizedPath(clip);
                    int handled = 0;
                    if (path && path[0] != '\0') {
                        const char* ext = strrchr(path, '.');
                        if (ext != NULL) {
                            if (EqualsIgnoreCase(ext, ".png") || EqualsIgnoreCase(ext, ".jpg") ||
                                EqualsIgnoreCase(ext, ".jpeg") || EqualsIgnoreCase(ext, ".bmp")) {
                                Image img = LoadImage(path);
                                if (IsImageReady(img)) {
                                    Texture2D tex = LoadTextureFromImage(img);
                                    boxes[boxCount].x = (int)mousePos.x;
                                    boxes[boxCount].y = (int)mousePos.y;
                                    boxes[boxCount].width = img.width;
                                    boxes[boxCount].height = img.height;
                                    boxes[boxCount].type = BOX_IMAGE;
                                    boxes[boxCount].content.texture = tex;
                                    boxes[boxCount].filePath = NULL;
                                    boxes[boxCount].isSelected = 0;
                                    boxCount++;
                                    UnloadImage(img);
                                    handled = 1;
                                    selectedBox = boxCount - 1;
                                    SelectBox(boxes, boxCount, selectedBox);
                                    PushHistoryState(boxes, boxCount, selectedBox);
                                }
                            } else if (EqualsIgnoreCase(ext, ".wav") || EqualsIgnoreCase(ext, ".ogg") ||
                                       EqualsIgnoreCase(ext, ".mp3") || EqualsIgnoreCase(ext, ".flac")) {
                                Sound sound = {0};
                                int soundReady = 0;
                                if (audioDeviceReady) {
                                    Sound loaded = LoadSound(path);
                                    if (IsSoundReady(loaded)) {
                                        sound = loaded;
                                        soundReady = 1;
                                    } else {
                                        UnloadSound(loaded);
                                    }
                                }

                                boxes[boxCount].x = (int)mousePos.x;
                                boxes[boxCount].y = (int)mousePos.y;
                                boxes[boxCount].width = AUDIO_BOX_WIDTH;
                                boxes[boxCount].height = AUDIO_BOX_HEIGHT;
                                boxes[boxCount].type = BOX_AUDIO;
                                boxes[boxCount].content.sound = sound;
                                boxes[boxCount].filePath = path;
                                boxes[boxCount].fontSize = 0;
                                boxes[boxCount].textColor = BLACK;
                                boxes[boxCount].isSelected = 0;
                                boxCount++;
                                handled = 1;
                                selectedBox = boxCount - 1;
                                SelectBox(boxes, boxCount, selectedBox);
                                PushHistoryState(boxes, boxCount, selectedBox);

                                const char* audioName = ExtractFileName(path);

                                if (soundReady) {
                                    snprintf(statusMessage, sizeof(statusMessage), "Loaded %s", audioName);
                                    statusMessageTimer = 1.6f;
                                } else if (!audioDeviceReady) {
                                    snprintf(statusMessage, sizeof(statusMessage), "Audio placeholder: device unavailable");
                                    statusMessageTimer = 2.0f;
                                } else {
                                    snprintf(statusMessage, sizeof(statusMessage), "Audio placeholder: failed to load %s", audioName);
                                    statusMessageTimer = 2.0f;
                                }
                            }
                        }
                    }

                    if (!handled) {
                        const char* textToUse = path ? path : clip;
                        char* textBuffer = path ? path : strdup(textToUse);
                        if (textBuffer != NULL) {
                            int textWidth, textHeight;
                            CalculateTextBoxSize(textBuffer, DEFAULT_FONT_SIZE, &textWidth, &textHeight);

                            boxes[boxCount].x = (int)mousePos.x;
                            boxes[boxCount].y = (int)mousePos.y;
                            boxes[boxCount].width = textWidth;
                            boxes[boxCount].height = textHeight;
                            boxes[boxCount].type = BOX_TEXT;
                            boxes[boxCount].content.text = textBuffer;
                            boxes[boxCount].fontSize = DEFAULT_FONT_SIZE;
                            boxes[boxCount].textColor = currentDrawColor;
                            boxes[boxCount].filePath = NULL;
                            boxes[boxCount].isSelected = 0;
                            boxCount++;
                            selectedBox = boxCount - 1;
                            SelectBox(boxes, boxCount, selectedBox);
                            PushHistoryState(boxes, boxCount, selectedBox);
                        } else {
                            if (path) {
                                free(path);
                            }
                        }
                    } else if (path && boxes[selectedBox].type != BOX_AUDIO) {
                        free(path);
                    }
                }
            }
        }

        BeginDrawing();

        ClearBackground(RAYWHITE);

        for (int i = 0; i < boxCount; i++) {
            Box* box = &boxes[i];
            if (box->type == BOX_TEXT) {
                DrawRectangle(box->x, box->y, box->width, box->height, WHITE);
            }

            switch (box->type) {
                case BOX_IMAGE:
                    {
                        Rectangle source = {0.0f, 0.0f, (float)box->content.texture.width, (float)box->content.texture.height};
                        Rectangle dest = {(float)box->x, (float)box->y, (float)box->width, (float)box->height};
                        Vector2 origin = {0.0f, 0.0f};
                        DrawTexturePro(box->content.texture, source, dest, origin, 0.0f, WHITE);
                    }
                    break;
                case BOX_TEXT:
                    {
                        Color textColor = box->textColor;
                        if (textColor.a == 0) {
                            textColor = BLACK;
                        }
                        int boxFontSize = box->fontSize > 0 ? box->fontSize : DEFAULT_FONT_SIZE;
                        if (editingBoxIndex == i) {
                            DrawMultilineTextWithSelection(editingText, box->x + 10, box->y + 10, editingFontSize, textColor, selectionStart, selectionEnd, TEXT_SELECTION_COLOR);
                            DrawTextCursor(box->x, box->y, editingFontSize);
                        } else {
                            DrawMultilineTextWithSelection(box->content.text, box->x + 10, box->y + 10, boxFontSize, textColor, 0, 0, TEXT_SELECTION_COLOR);
                        }
                    }
                    break;
                case BOX_AUDIO:
                    {
                        Rectangle backdrop = {(float)box->x, (float)box->y, (float)box->width, (float)box->height};
                        DrawRectangleRec(backdrop, Fade(SKYBLUE, 0.25f));
                        DrawRectangleLines(box->x, box->y, box->width, box->height, Fade(DARKBLUE, 0.4f));
                        const char* fileName = ExtractFileName(box->filePath);
                        int titleFont = 20;
                        int titleWidth = MeasureText(fileName, titleFont);
                        int titleX = box->x + 16;
                        if (titleWidth > box->width - 32) {
                            while (titleFont > 12 && MeasureText(fileName, titleFont) > box->width - 32) {
                                titleFont -= 2;
                            }
                        }
                        DrawText(fileName, titleX, box->y + 16, titleFont, DARKBLUE);

                        int soundReady = audioDeviceReady && IsSoundReady(box->content.sound);
                        int playing = soundReady && IsSoundPlaying(box->content.sound);
                        const char* action = NULL;
                        int actionFont = 18;
                        Color actionColor = DARKGRAY;

                        if (!audioDeviceReady) {
                            action = "Audio disabled (device unavailable)";
                            actionColor = MAROON;
                        } else if (!soundReady) {
                            action = "Audio failed to load";
                            actionColor = MAROON;
                        } else if (playing) {
                            action = "Pause (Space / dbl-click)";
                            actionColor = DARKGREEN;
                        } else {
                            action = "Play (Space / dbl-click)";
                            actionColor = DARKBLUE;
                        }

                        DrawText(action, box->x + 16, box->y + box->height - 34, actionFont, actionColor);

                        int iconX = box->x + box->width - 48;
                        int iconY = box->y + (box->height / 2) - 12;
                        if (soundReady) {
                            if (playing) {
                                DrawRectangle(iconX, iconY, 10, 24, actionColor);
                                DrawRectangle(iconX + 14, iconY, 10, 24, actionColor);
                            } else {
                                Vector2 p1 = {(float)iconX, (float)iconY};
                                Vector2 p2 = {(float)iconX, (float)(iconY + 24)};
                                Vector2 p3 = {(float)(iconX + 22), (float)(iconY + 12)};
                                DrawTriangle(p1, p2, p3, actionColor);
                            }
                        } else {
                            DrawLine(iconX, iconY, iconX + 24, iconY + 24, actionColor);
                            DrawLine(iconX, iconY + 24, iconX + 24, iconY, actionColor);
                        }
                    }
                    break;
                case BOX_DRAWING:
                    {
                        Rectangle source = {0.0f, 0.0f, (float)box->content.texture.width, -(float)box->content.texture.height};
                        Rectangle dest = {(float)box->x, (float)box->y, (float)box->width, (float)box->height};
                        Vector2 origin = {0.0f, 0.0f};
                        DrawTexturePro(box->content.texture, source, dest, origin, 0.0f, WHITE);
                    }
                    break;
                default:
                    break;
            }

            if (box->isSelected) {
                Rectangle selectionRect = {
                    (float)box->x - 1.0f,
                    (float)box->y - 1.0f,
                    (float)box->width + 2.0f,
                    (float)box->height + 2.0f
                };

                Color borderColor = BOX_SELECTION_BORDER_COLOR;

                if (box->type == BOX_TEXT && editingBoxIndex == i) {
                    Rectangle glowRect = {
                        selectionRect.x - 2.0f,
                        selectionRect.y - 2.0f,
                        selectionRect.width + 4.0f,
                        selectionRect.height + 4.0f
                    };
                    DrawRectangleLinesEx(glowRect, 2.0f, Fade(TEXT_EDIT_BORDER_COLOR, 0.35f));
                    borderColor = TEXT_EDIT_BORDER_COLOR;
                }

                DrawRectangleLinesEx(selectionRect, 2.0f, borderColor);

                if (box->type == BOX_TEXT || box->type == BOX_IMAGE || box->type == BOX_AUDIO) {
                    DrawResizeHandles(box);
                }
            }
        }

        if (isDrawing) {
            if (currentTool == TOOL_RECT) {
                int endX = (int)mousePos.x;
                int endY = (int)mousePos.y;
                int x = startX < endX ? startX : endX;
                int y = startY < endY ? startY : endY;
                int width = abs(endX - startX);
                int height = abs(endY - startY);
                DrawRectangleLines(x, y, width, height, Fade(currentDrawColor, 0.8f));
            } else if (currentTool == TOOL_CIRCLE) {
                float dx = mousePos.x - startX;
                float dy = mousePos.y - startY;
                int radius = (int)sqrtf(dx * dx + dy * dy);
                DrawCircleLines(startX, startY, (float)radius, Fade(currentDrawColor, 0.8f));
            } else if (currentTool == TOOL_SEGMENT) {
                Vector2 start = {(float)startX, (float)startY};
                Vector2 end = {mousePos.x, mousePos.y};
                DrawLineEx(start, end, STROKE_THICKNESS, Fade(currentDrawColor, 0.8f));
            } else if (currentTool == TOOL_PEN && penPointCount > 0) {
                Vector2 prev = penPoints[0];
                for (int i = 1; i < penPointCount; i++) {
                    Vector2 curr = penPoints[i];
                    DrawLineEx(prev, curr, STROKE_THICKNESS, Fade(currentDrawColor, 0.8f));
                    prev = curr;
                }
                DrawLineEx(prev, mousePos, STROKE_THICKNESS, Fade(currentDrawColor, 0.5f));
            }
        }

        DrawRectangleRec(toolbarRect, Fade(LIGHTGRAY, 0.6f));
        DrawRectangleGradientV(0, 0, screenWidthCurrent, (int)TOOLBAR_HEIGHT, Fade(WHITE, 0.25f), Fade(LIGHTGRAY, 0.05f));
        DrawRectangle(0, (int)TOOLBAR_HEIGHT, screenWidthCurrent, 1, Fade(DARKGRAY, 0.35f));

        for (int i = 0; i < 5; i++) {
            int isActive = (currentTool == toolOrder[i]);
            int isHovered = (i == hoveredToolIndex);
            Color baseColor = isActive ? SKYBLUE : LIGHTGRAY;
            float alpha = isActive ? (isHovered ? 0.95f : 0.85f) : (isHovered ? 0.78f : 0.55f);
            Color fill = Fade(baseColor, alpha);
            Color outline = isActive ? Fade(DARKBLUE, isHovered ? 0.95f : 0.85f) : (isHovered ? Fade(DARKBLUE, 0.9f) : Fade(DARKGRAY, 0.85f));

            DrawRectangleRounded(toolButtons[i], BUTTON_ROUNDNESS, 6, fill);
            DrawRectangleRoundedLines(toolButtons[i], BUTTON_ROUNDNESS, 6, 2.0f, outline);

            if (isActive || isHovered) {
                Rectangle indicator = {
                    toolButtons[i].x + 10.0f,
                    toolButtons[i].y + toolButtons[i].height - 6.0f,
                    toolButtons[i].width - 20.0f,
                    4.0f
                };
                if (indicator.width < 12.0f) {
                    indicator.width = toolButtons[i].width;
                    indicator.x = toolButtons[i].x;
                }
                Color indicatorColor = isActive ? Fade(DARKBLUE, 0.9f) : Fade(DARKGRAY, 0.7f);
                DrawRectangleRounded(indicator, 0.5f, 4, indicatorColor);
            }

            Color labelColor = isActive ? DARKBLUE : (isHovered ? BLACK : DARKGRAY);
            int labelWidth = MeasureText(toolLabels[i], 18);
            DrawText(toolLabels[i], (int)(toolButtons[i].x + (toolButtons[i].width - labelWidth) / 2.0f), (int)(toolButtons[i].y + (toolButtons[i].height - 18.0f) / 2.0f), 18, labelColor);
        }

        for (int i = 0; i < COLOR_PALETTE_COUNT; i++) {
            Rectangle colorRect = colorButtons[i];
            int isHovered = (i == hoveredColorIndex);
            int isSelected = ColorsEqual(currentDrawColor, COLOR_PALETTE[i]);

            DrawRectangleRounded(colorRect, BUTTON_ROUNDNESS, 6, COLOR_PALETTE[i]);
            if (isHovered) {
                DrawRectangleRounded(colorRect, BUTTON_ROUNDNESS, 6, Fade(WHITE, 0.12f));
            }

            Color outlineColor = isSelected ? BLACK : (isHovered ? Fade(DARKBLUE, 0.85f) : Fade(DARKGRAY, 0.85f));
            float outlineThickness = isSelected ? 2.0f : 1.5f;
            DrawRectangleRoundedLines(colorRect, BUTTON_ROUNDNESS, 6, outlineThickness, outlineColor);
        }

        int hasSelection = (selectedBox != -1);
        int canBring = hasSelection;
        int canSend = hasSelection;

        int bringHovered = hoveredBringToFront && canBring;
        int sendHovered = hoveredSendToBack && canSend;

        Color bringFill = canBring ? Fade(SKYBLUE, bringHovered ? 0.82f : 0.6f) : Fade(LIGHTGRAY, 0.35f);
        Color bringOutline = canBring ? Fade(DARKBLUE, bringHovered ? 0.9f : 0.7f) : Fade(GRAY, 0.8f);
        Color bringText = canBring ? (bringHovered ? DARKBLUE : BLACK) : Fade(DARKGRAY, 0.7f);

        DrawRectangleRounded(bringToFrontButton, BUTTON_ROUNDNESS, 6, bringFill);
        DrawRectangleRoundedLines(bringToFrontButton, BUTTON_ROUNDNESS, 6, 2.0f, bringOutline);
        int topLabel = MeasureText("Top", 18);
        DrawText("Top", (int)(bringToFrontButton.x + (bringToFrontButton.width - topLabel) / 2.0f), (int)(bringToFrontButton.y + (bringToFrontButton.height - 18.0f) / 2.0f), 18, bringText);

        Color sendFill = canSend ? Fade(SKYBLUE, sendHovered ? 0.82f : 0.6f) : Fade(LIGHTGRAY, 0.35f);
        Color sendOutline = canSend ? Fade(DARKBLUE, sendHovered ? 0.9f : 0.7f) : Fade(GRAY, 0.8f);
        Color sendText = canSend ? (sendHovered ? DARKBLUE : BLACK) : Fade(DARKGRAY, 0.7f);

        DrawRectangleRounded(sendToBackButton, BUTTON_ROUNDNESS, 6, sendFill);
        DrawRectangleRoundedLines(sendToBackButton, BUTTON_ROUNDNESS, 6, 2.0f, sendOutline);
        int bottomLabel = MeasureText("Bottom", 18);
        DrawText("Bottom", (int)(sendToBackButton.x + (sendToBackButton.width - bottomLabel) / 2.0f), (int)(sendToBackButton.y + (sendToBackButton.height - 18.0f) / 2.0f), 18, sendText);

        int exportHovered = hoveredExport && !showClearConfirm;
        Color exportFill = Fade(SKYBLUE, exportHovered ? 0.85f : 0.65f);
        Color exportOutline = Fade(DARKBLUE, exportHovered ? 0.95f : 0.8f);
        Color exportText = exportHovered ? DARKBLUE : BLACK;

        DrawRectangleRounded(exportButton, BUTTON_ROUNDNESS, 6, exportFill);
        DrawRectangleRoundedLines(exportButton, BUTTON_ROUNDNESS, 6, 2.0f, exportOutline);
        int exportLabel = MeasureText("Export", 18);
        DrawText("Export", (int)(exportButton.x + (exportButton.width - exportLabel) / 2.0f), (int)(exportButton.y + (exportButton.height - 18.0f) / 2.0f), 18, exportText);

        int clearHovered = hoveredClear && !showClearConfirm;
        Color clearBase = showClearConfirm ? ORANGE : SKYBLUE;
        float clearAlpha = showClearConfirm ? (clearHovered ? 0.95f : 0.8f) : (clearHovered ? 0.9f : 0.65f);
        Color clearFill = Fade(clearBase, clearAlpha);
        Color clearOutline = showClearConfirm ? Fade(MAROON, 0.85f) : Fade(DARKBLUE, clearHovered ? 0.95f : 0.8f);
        Color clearText = showClearConfirm ? MAROON : (clearHovered ? DARKBLUE : BLACK);

        DrawRectangleRounded(clearButton, BUTTON_ROUNDNESS, 6, clearFill);
        DrawRectangleRoundedLines(clearButton, BUTTON_ROUNDNESS, 6, 2.0f, clearOutline);
        int clearLabel = MeasureText("Clear", 18);
        DrawText("Clear", (int)(clearButton.x + (clearButton.width - clearLabel) / 2.0f), (int)(clearButton.y + (clearButton.height - 18.0f) / 2.0f), 18, clearText);

        Rectangle statusBarRect = {0.0f, (float)screenHeightCurrent - STATUS_BAR_HEIGHT, (float)screenWidthCurrent, STATUS_BAR_HEIGHT};
        DrawRectangleRec(statusBarRect, Fade(LIGHTGRAY, 0.45f));
        DrawRectangleGradientV(0, (int)statusBarRect.y, screenWidthCurrent, (int)STATUS_BAR_HEIGHT, Fade(WHITE, 0.2f), Fade(LIGHTGRAY, 0.05f));
        DrawRectangle(0, (int)statusBarRect.y, screenWidthCurrent, 1, Fade(DARKGRAY, 0.3f));

        const char* statusTextPtr = statusMessage;
        char statusFallback[160];
        if (statusMessageTimer <= 0.0f || statusMessage[0] == '\0') {
            const char* toolName = toolNames[currentTool];
            snprintf(statusFallback, sizeof(statusFallback), "Tool: %s • %s", toolName, STATUS_DEFAULT_HINT);
            statusTextPtr = statusFallback;
        }

        int statusFontSize = 18;
        int statusY = (int)(statusBarRect.y + (statusBarRect.height - statusFontSize) / 2.0f);
        DrawText(statusTextPtr, 16, statusY, statusFontSize, DARKGRAY);

        const char* audioStatus = audioDeviceReady ? "Audio ready" : "Audio disabled";
        Color audioColor = audioDeviceReady ? DARKGREEN : MAROON;
        int audioWidth = MeasureText(audioStatus, 16);
        DrawText(audioStatus, screenWidthCurrent - audioWidth - 16, statusY, 16, audioColor);

        if (showClearConfirm) {
            DrawRectangle(0, 0, screenWidthCurrent, screenHeightCurrent, Fade(BLACK, 0.45f));
            DrawRectangleRec(confirmDialogRect, RAYWHITE);
            DrawRectangleLinesEx(confirmDialogRect, 2.0f, DARKGRAY);
            const char* title = "Clear all items?";
            int titleWidth = MeasureText(title, 22);
            DrawText(title, (int)(confirmDialogRect.x + (confirmDialogRect.width - titleWidth) / 2.0f), (int)(confirmDialogRect.y + 28.0f), 22, BLACK);
            const char* subtitle = "This removes every box.";
            int subtitleWidth = MeasureText(subtitle, 18);
            DrawText(subtitle, (int)(confirmDialogRect.x + (confirmDialogRect.width - subtitleWidth) / 2.0f), (int)(confirmDialogRect.y + 62.0f), 18, DARKGRAY);

            DrawRectangleRec(confirmYesRect, Fade(GREEN, 0.7f));
            DrawRectangleLinesEx(confirmYesRect, 1.0f, DARKGREEN);
            int yesWidth = MeasureText("Confirm", 18);
            DrawText("Confirm", (int)(confirmYesRect.x + (confirmYesRect.width - yesWidth) / 2.0f), (int)(confirmYesRect.y + (confirmYesRect.height - 18.0f) / 2.0f), 18, BLACK);

            DrawRectangleRec(confirmNoRect, Fade(LIGHTGRAY, 0.7f));
            DrawRectangleLinesEx(confirmNoRect, 1.0f, DARKGRAY);
            int noWidth = MeasureText("Cancel", 18);
            DrawText("Cancel", (int)(confirmNoRect.x + (confirmNoRect.width - noWidth) / 2.0f), (int)(confirmNoRect.y + (confirmNoRect.height - 18.0f) / 2.0f), 18, BLACK);
        }

        EndDrawing();

        if (requestExportClipboard) {
            requestExportClipboard = 0;
            Image screenCapture = LoadImageFromScreen();
            if (IsImageReady(screenCapture)) {
                int cropHeight = screenCapture.height - (int)TOOLBAR_HEIGHT - (int)STATUS_BAR_HEIGHT;
                if (cropHeight > 0) {
                    Rectangle cropArea = {0.0f, TOOLBAR_HEIGHT, (float)screenCapture.width, (float)cropHeight};
                    ImageCrop(&screenCapture, cropArea);
                    if (screenCapture.width > 0 && screenCapture.height > 0) {
                        int success = CopyImageToClipboard(&screenCapture);
                        if (success) {
                            snprintf(statusMessage, sizeof(statusMessage), "Canvas copied to clipboard");
                        } else {
                            snprintf(statusMessage, sizeof(statusMessage), "Clipboard export unavailable on this platform");
                        }
                        statusMessageTimer = 2.5f;
                    } else {
                        snprintf(statusMessage, sizeof(statusMessage), "Canvas empty, nothing exported");
                        statusMessageTimer = 1.8f;
                    }
                } else {
                    snprintf(statusMessage, sizeof(statusMessage), "Canvas empty, nothing exported");
                    statusMessageTimer = 1.8f;
                }
                UnloadImage(screenCapture);
            } else {
                snprintf(statusMessage, sizeof(statusMessage), "Failed to capture canvas");
                statusMessageTimer = 1.8f;
            }
        }
    }

    for (int i = 0; i < historyCount; i++) {
        FreeSnapshot(&historyStates[i]);
    }

    CloseAudioDevice();
    CloseWindow();

    return 0;
}

void DestroyBox(Box* box) {
    if (box == NULL) {
        return;
    }

    switch (box->type) {
        case BOX_TEXT:
            if (box->content.text != NULL) {
                free(box->content.text);
                box->content.text = NULL;
            }
            break;
        case BOX_IMAGE:
        case BOX_DRAWING:
            if (box->content.texture.id != 0) {
                UnloadTexture(box->content.texture);
                box->content.texture.id = 0;
            }
            break;
        case BOX_AUDIO:
            if (audioDeviceReady && IsSoundReady(box->content.sound)) {
                StopAudioPlayback(box);
                UnloadSound(box->content.sound);
            }
            break;
        default:
            break;
    }

    if (box->filePath != NULL) {
        free(box->filePath);
        box->filePath = NULL;
    }

    box->isSelected = 0;
}

int BringBoxToFront(Box* boxes, int boxCount, int index) {
    if (index < 0 || index >= boxCount) {
        return index;
    }
    if (index == boxCount - 1) {
        return index;
    }

    Box temp = boxes[index];
    for (int i = index; i < boxCount - 1; i++) {
        boxes[i] = boxes[i + 1];
    }
    boxes[boxCount - 1] = temp;
    return boxCount - 1;
}

int SendBoxToBack(Box* boxes, int boxCount, int index) {
    if (index < 0 || index >= boxCount) {
        return index;
    }
    if (index == 0) {
        return index;
    }

    Box temp = boxes[index];
    for (int i = index; i > 0; i--) {
        boxes[i] = boxes[i - 1];
    }
    boxes[0] = temp;
    return 0;
}

void ClearAllBoxes(Box* boxes, int* boxCount, int* selectedBox) {
    if (boxes == NULL || boxCount == NULL || selectedBox == NULL) {
        return;
    }

    for (int i = 0; i < *boxCount; i++) {
        DestroyBox(&boxes[i]);
        boxes[i] = (Box){0};
    }

    *boxCount = 0;
    *selectedBox = -1;
    ResetEditingState();
}

void ResetEditingState(void) {
    editingBoxIndex = -1;
    memset(editingText, 0, sizeof(editingText));
    memset(editingOriginalText, 0, sizeof(editingOriginalText));
    editingFontSize = DEFAULT_FONT_SIZE;
    editingOriginalFontSize = DEFAULT_FONT_SIZE;
    cursorPosition = 0;
    cursorBlinkTime = 0.0f;
    selectionStart = 0;
    selectionEnd = 0;
    selectAllOnStart = 0;
    isMouseSelecting = 0;
    cursorPreferredColumn = -1;
}

int ColorsEqual(Color a, Color b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

int CopyImageToClipboard(const Image* image) {
    if (image == NULL || image->data == NULL || image->width <= 0 || image->height <= 0) {
        return 0;
    }

    Image copy = ImageCopy(*image);
    if (!IsImageReady(copy)) {
        return 0;
    }

    ImageFormat(&copy, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);

    int success = 0;
#ifdef _WIN32
    success = WinClip_SetImageRGBA((unsigned char*)copy.data, copy.width, copy.height);
#endif

    UnloadImage(copy);
    return success;
}

void FreeSnapshot(CanvasSnapshot* snapshot) {
    if (snapshot == NULL) {
        return;
    }

    for (int i = 0; i < snapshot->boxCount; i++) {
        if (snapshot->boxes[i].textCopy != NULL) {
            free(snapshot->boxes[i].textCopy);
            snapshot->boxes[i].textCopy = NULL;
        }
        if (snapshot->boxes[i].imageCopy.data != NULL) {
            UnloadImage(snapshot->boxes[i].imageCopy);
            snapshot->boxes[i].imageCopy = (Image){0};
        }
        if (snapshot->boxes[i].filePathCopy != NULL) {
            free(snapshot->boxes[i].filePathCopy);
            snapshot->boxes[i].filePathCopy = NULL;
        }
    }

    snapshot->boxCount = 0;
    snapshot->selectedBox = -1;
}

void CaptureSnapshot(CanvasSnapshot* snapshot, Box* boxes, int boxCount, int selectedBox) {
    if (snapshot == NULL) {
        return;
    }

    FreeSnapshot(snapshot);

    snapshot->boxCount = boxCount;
    snapshot->selectedBox = (selectedBox >= 0 && selectedBox < boxCount) ? selectedBox : -1;

    for (int i = 0; i < boxCount; i++) {
        Box* src = &boxes[i];
        BoxSnapshot* dest = &snapshot->boxes[i];

        dest->box = *src;
        dest->textCopy = NULL;
        dest->imageCopy = (Image){0};
        dest->filePathCopy = NULL;
        dest->box.filePath = NULL;

        switch (src->type) {
            case BOX_TEXT:
                if (src->content.text != NULL) {
                    dest->textCopy = strdup(src->content.text);
                } else {
                    dest->textCopy = strdup("");
                }
                dest->box.content.text = NULL;
                break;
            case BOX_IMAGE:
            case BOX_DRAWING:
                if (src->content.texture.id != 0) {
                    dest->imageCopy = LoadImageFromTexture(src->content.texture);
                }
                dest->box.content.texture = (Texture2D){0};
                break;
            case BOX_AUDIO:
                if (src->filePath != NULL) {
                    dest->filePathCopy = strdup(src->filePath);
                }
                dest->box.content.sound = (Sound){0};
                break;
            default:
                break;
        }
    }
}

void PushHistoryState(Box* boxes, int boxCount, int selectedBox) {
    if (suppressHistory) {
        return;
    }

    if (historyIndex < historyCount - 1) {
        for (int i = historyIndex + 1; i < historyCount; i++) {
            FreeSnapshot(&historyStates[i]);
            historyStates[i] = (CanvasSnapshot){0};
        }
        historyCount = historyIndex + 1;
    }

    if (historyCount == MAX_HISTORY) {
        FreeSnapshot(&historyStates[0]);
        for (int i = 1; i < historyCount; i++) {
            historyStates[i - 1] = historyStates[i];
        }
        historyStates[historyCount - 1] = (CanvasSnapshot){0};
        historyCount--;
        if (historyIndex > 0) {
            historyIndex--;
        }
    }

    CaptureSnapshot(&historyStates[historyCount], boxes, boxCount, selectedBox);
    historyCount++;
    historyIndex = historyCount - 1;
}

void RestoreSnapshotState(Box* boxes, int* boxCount, int* selectedBox, int targetIndex) {
    if (targetIndex < 0 || targetIndex >= historyCount || boxes == NULL || boxCount == NULL || selectedBox == NULL) {
        return;
    }

    suppressHistory = 1;

    for (int i = 0; i < *boxCount; i++) {
        DestroyBox(&boxes[i]);
        boxes[i] = (Box){0};
    }

    CanvasSnapshot* snapshot = &historyStates[targetIndex];
    for (int i = 0; i < snapshot->boxCount; i++) {
        BoxSnapshot* src = &snapshot->boxes[i];
        boxes[i] = src->box;
        boxes[i].filePath = NULL;

        switch (src->box.type) {
            case BOX_TEXT:
                boxes[i].content.text = src->textCopy ? strdup(src->textCopy) : strdup("");
                break;
            case BOX_IMAGE:
            case BOX_DRAWING:
                if (src->imageCopy.data != NULL) {
                    boxes[i].content.texture = LoadTextureFromImage(src->imageCopy);
                } else {
                    boxes[i].content.texture = (Texture2D){0};
                }
                break;
            case BOX_AUDIO:
                if (src->filePathCopy != NULL) {
                    boxes[i].filePath = strdup(src->filePathCopy);
                }
                boxes[i].content.sound = (Sound){0};
                if (boxes[i].filePath != NULL && audioDeviceReady) {
                    Sound restored = LoadSound(boxes[i].filePath);
                    if (IsSoundReady(restored)) {
                        boxes[i].content.sound = restored;
                    } else {
                        UnloadSound(restored);
                    }
                }
                if (boxes[i].width <= 0) boxes[i].width = AUDIO_BOX_WIDTH;
                if (boxes[i].height <= 0) boxes[i].height = AUDIO_BOX_HEIGHT;
                break;
            default:
                break;
        }

        boxes[i].isSelected = 0;
    }

    *boxCount = snapshot->boxCount;
    *selectedBox = (snapshot->selectedBox >= 0 && snapshot->selectedBox < *boxCount) ? snapshot->selectedBox : -1;
    SelectBox(boxes, *boxCount, *selectedBox);
    ResetEditingState();

    suppressHistory = 0;
}

int PerformUndo(Box* boxes, int* boxCount, int* selectedBox) {
    if (historyIndex > 0) {
        historyIndex--;
        RestoreSnapshotState(boxes, boxCount, selectedBox, historyIndex);
        return 1;
    }
    return 0;
}

int PerformRedo(Box* boxes, int* boxCount, int* selectedBox) {
    if (historyIndex >= 0 && historyIndex < historyCount - 1) {
        historyIndex++;
        RestoreSnapshotState(boxes, boxCount, selectedBox, historyIndex);
        return 1;
    }
    return 0;
}