#include "raylib.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdio.h>

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
    int x, y, width, height;
    BoxType type;
    union {
        Texture2D texture;
        char* text;
        char* filePath;
    } content;
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

/* Text editing state */
int editingBoxIndex = -1;
char editingText[1024] = {0};
char editingOriginalText[1024] = {0};
int cursorPosition = 0;
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
void DestroyBox(Box* box);
int BringBoxToFront(Box* boxes, int boxCount, int index);
int SendBoxToBack(Box* boxes, int boxCount, int index);
void ClearAllBoxes(Box* boxes, int* boxCount, int* selectedBox);
void ResetEditingState(void);
int ColorsEqual(Color a, Color b);
int CopyImageToClipboard(const Image* image);

typedef struct {
    Box box;
    Image imageCopy;
    char* textCopy;
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
void PerformUndo(Box* boxes, int* boxCount, int* selectedBox);
void PerformRedo(Box* boxes, int* boxCount, int* selectedBox);

static CanvasSnapshot historyStates[MAX_HISTORY];
static int historyCount = 0;
static int historyIndex = -1;
static int suppressHistory = 0;

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

void CalculateTextBoxSize(const char* text, int fontSize, int* width, int* height) {
    if (!text || strlen(text) == 0) {
        *width = 100;  /* Minimum width for empty text */
        *height = fontSize + 20;  /* Font size plus padding */
        return;
    }

    /* Handle multi-line text by splitting on newlines and calculating max width and total height */
    char* textCopy = strdup(text);
    char* line = strtok(textCopy, "\n");
    int maxWidth = 0;
    int lineCount = 0;

    while (line != NULL) {
        int lineWidth = MeasureText(line, fontSize);
        if (lineWidth > maxWidth) {
            maxWidth = lineWidth;
        }
        lineCount++;
        line = strtok(NULL, "\n");
    }

    /* If no newlines found, treat as single line */
    if (lineCount == 0) {
        maxWidth = MeasureText(text, fontSize);
        lineCount = 1;
    }

    /* Add padding around text */
    *width = maxWidth + 20;  /* 10px padding on each side */
    *height = (lineCount * fontSize) + 20;  /* Line height * number of lines plus padding */

    /* Ensure minimum dimensions */
    if (*width < 100) *width = 100;
    if (*height < 30) *height = 30;

    free(textCopy);
}

void DrawMultilineText(const char* text, int x, int y, int fontSize, Color color) {
    if (!text || strlen(text) == 0) {
        return;
    }

    char* textCopy = strdup(text);
    char* line = strtok(textCopy, "\n");
    int currentY = y;

    while (line != NULL) {
        DrawText(line, x, currentY, fontSize, color);
        currentY += fontSize;  /* Move to next line */
        line = strtok(NULL, "\n");
    }

    /* If no newlines found, draw as single line */
    if (currentY == y) {
        DrawText(text, x, y, fontSize, color);
    }

    free(textCopy);
}

void StartTextEdit(int boxIndex, Box* boxes) {
    if (boxIndex >= 0 && boxes[boxIndex].type == BOX_TEXT) {
        editingBoxIndex = boxIndex;
        strncpy(editingText, boxes[boxIndex].content.text, sizeof(editingText) - 1);
        editingText[sizeof(editingText) - 1] = '\0';
        strncpy(editingOriginalText, editingText, sizeof(editingOriginalText) - 1);
        editingOriginalText[sizeof(editingOriginalText) - 1] = '\0';
        cursorPosition = strlen(editingText);
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
        CalculateTextBoxSize(editingText, 20, &textWidth, &textHeight);
        boxes[editingBoxIndex].width = textWidth;
        boxes[editingBoxIndex].height = textHeight;

        lastTextEditChanged = (strcmp(editingOriginalText, editingText) != 0);

        editingBoxIndex = -1;
        memset(editingText, 0, sizeof(editingText));
        memset(editingOriginalText, 0, sizeof(editingOriginalText));
        cursorPosition = 0;
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
    CalculateTextBoxSize(editingText, 20, &textWidth, &textHeight);
    boxes[editingBoxIndex].width = textWidth;
    boxes[editingBoxIndex].height = textHeight;
}

void HandleTextInput(Box* boxes) {
    if (editingBoxIndex < 0) return;

    int textChanged = 0;

    /* Handle character input */
    int key = GetCharPressed();
    while (key > 0) {
        if (key >= 32 && key <= 126 && cursorPosition < sizeof(editingText) - 1) {
            /* Insert character at cursor position */
            int textLen = strlen(editingText);
            for (int i = textLen; i >= cursorPosition; i--) {
                editingText[i + 1] = editingText[i];
            }
            editingText[cursorPosition] = (char)key;
            cursorPosition++;
            textChanged = 1;
        }
        key = GetCharPressed();
    }

    /* Handle special keys */
    if (IsKeyPressed(KEY_BACKSPACE) && cursorPosition > 0) {
        /* Delete character before cursor */
        int textLen = strlen(editingText);
        for (int i = cursorPosition - 1; i < textLen; i++) {
            editingText[i] = editingText[i + 1];
        }
        cursorPosition--;
        textChanged = 1;
    }

    if (IsKeyPressed(KEY_DELETE)) {
        /* Delete character at cursor */
        int textLen = strlen(editingText);
        if (cursorPosition < textLen) {
            for (int i = cursorPosition; i < textLen; i++) {
                editingText[i] = editingText[i + 1];
            }
            textChanged = 1;
        }
    }

    if (IsKeyPressed(KEY_LEFT) && cursorPosition > 0) {
        cursorPosition--;
    }

    if (IsKeyPressed(KEY_RIGHT) && cursorPosition < strlen(editingText)) {
        cursorPosition++;
    }

    if (IsKeyPressed(KEY_ENTER)) {
        /* Insert newline at cursor position */
        if (cursorPosition < sizeof(editingText) - 1) {
            int textLen = strlen(editingText);
            for (int i = textLen; i >= cursorPosition; i--) {
                editingText[i + 1] = editingText[i];
            }
            editingText[cursorPosition] = '\n';
            cursorPosition++;
            textChanged = 1;
        }
    }

    /* Update box size if text changed */
    if (textChanged) {
        UpdateEditingBoxSize(boxes);
    }
}

void DrawTextCursor(int x, int y, int fontSize) {
    if (editingBoxIndex < 0) return;

    /* Update cursor blink timer */
    cursorBlinkTime += GetFrameTime();

    /* Draw blinking cursor */
    if (fmod(cursorBlinkTime, 1.0f) < 0.5f) {
        /* Calculate cursor position within text */
        char tempText[1024];
        strncpy(tempText, editingText, cursorPosition);
        tempText[cursorPosition] = '\0';

        /* Handle multi-line cursor positioning */
        char* lastNewline = strrchr(tempText, '\n');
        int cursorX = x + 10; /* Box padding */
        int cursorY = y + 10; /* Box padding */

        if (lastNewline) {
            /* Cursor is not on first line */
            char* lineStart = lastNewline + 1;
            cursorX += MeasureText(lineStart, fontSize);

            /* Count newlines to determine Y position */
            int lineCount = 0;
            for (int i = 0; i < cursorPosition; i++) {
                if (editingText[i] == '\n') lineCount++;
            }
            cursorY += lineCount * fontSize;
        } else {
            /* Cursor is on first line */
            cursorX += MeasureText(tempText, fontSize);
        }

        DrawLine(cursorX, cursorY, cursorX, cursorY + fontSize, RED);
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

        int screenWidthCurrent = GetScreenWidth();
        int screenHeightCurrent = GetScreenHeight();

        Rectangle toolbarRect = {0.0f, 0.0f, (float)screenWidthCurrent, TOOLBAR_HEIGHT};
        float buttonHeight = TOOLBAR_HEIGHT - 2.0f * TOOLBAR_PADDING;
        float toolButtonWidth = 64.0f;
        float xCursor = TOOLBAR_PADDING;

        Rectangle toolButtons[5];
        const char* toolLabels[5] = {"Sel", "Pen", "Line", "Rect", "Circ"};
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
            HandleTextInput(boxes);

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
                            currentTool = (Tool)i;
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
                                currentDrawColor = COLOR_PALETTE[i];
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
                        if (editingBoxIndex >= 0) {
                            Rectangle editingRect = {
                                boxes[editingBoxIndex].x,
                                boxes[editingBoxIndex].y,
                                (float)boxes[editingBoxIndex].width,
                                (float)boxes[editingBoxIndex].height
                            };

                            if (!CheckCollisionPointRec(mousePos, editingRect)) {
                                StopTextEditAndRecord(boxes, boxCount, selectedBox);
                            }
                        }

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
                                } else if (boxCount < MAX_BOXES) {
                                    int textWidth, textHeight;
                                    const char* newText = "New text";
                                    CalculateTextBoxSize(newText, 20, &textWidth, &textHeight);

                                    boxes[boxCount].x = (int)mousePos.x;
                                    boxes[boxCount].y = (int)mousePos.y;
                                    boxes[boxCount].width = textWidth;
                                    boxes[boxCount].height = textHeight;
                                    boxes[boxCount].type = BOX_TEXT;
                                    boxes[boxCount].content.text = strdup(newText);
                                    boxes[boxCount].isSelected = 0;
                                    boxCount++;
                                    selectedBox = boxCount - 1;
                                    SelectBox(boxes, boxCount, selectedBox);
                                    StartTextEdit(selectedBox, boxes);
                                    PushHistoryState(boxes, boxCount, selectedBox);
                                }
                            } else if (boxCount < MAX_BOXES) {
                                int textWidth, textHeight;
                                const char* newText = "New text";
                                CalculateTextBoxSize(newText, 20, &textWidth, &textHeight);

                                boxes[boxCount].x = (int)mousePos.x;
                                boxes[boxCount].y = (int)mousePos.y;
                                boxes[boxCount].width = textWidth;
                                boxes[boxCount].height = textHeight;
                                boxes[boxCount].type = BOX_TEXT;
                                boxes[boxCount].content.text = strdup(newText);
                                boxes[boxCount].isSelected = 0;
                                boxCount++;
                                selectedBox = boxCount - 1;
                                SelectBox(boxes, boxCount, selectedBox);
                                StartTextEdit(selectedBox, boxes);
                                PushHistoryState(boxes, boxCount, selectedBox);
                            }

                            lastClickTime = 0.0;
                            continue;
                        }

                        lastClickTime = currentTime;
                        lastClickPos = mousePos;

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
                            DrawRectangle(0, 0, width, height, currentDrawColor);
                            EndTextureMode();
                            boxes[boxCount].x = x;
                            boxes[boxCount].y = y;
                            boxes[boxCount].width = width;
                            boxes[boxCount].height = height;
                            boxes[boxCount].type = BOX_DRAWING;
                            boxes[boxCount].content.texture = rt.texture;
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
                            DrawCircle(radius, radius, radius, currentDrawColor);
                            EndTextureMode();
                            boxes[boxCount].x = x;
                            boxes[boxCount].y = y;
                            boxes[boxCount].width = width;
                            boxes[boxCount].height = height;
                            boxes[boxCount].type = BOX_DRAWING;
                            boxes[boxCount].content.texture = rt.texture;
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

            if (IsKeyDown(KEY_LEFT_CONTROL)) {
                if (IsKeyPressed(KEY_Z)) {
                    PerformUndo(boxes, &boxCount, &selectedBox);
                }
                if (IsKeyPressed(KEY_Y)) {
                    PerformRedo(boxes, &boxCount, &selectedBox);
                }
            }
        }

        /* Paste */
        if (!showClearConfirm && IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_V)) {
            #ifdef _WIN32
            /* Check for image data first on Windows */
            if (WinClip_HasImage() && boxCount < MAX_BOXES) {
                int imgWidth, imgHeight, imgChannels;
                void* imgData = WinClip_GetImageData(&imgWidth, &imgHeight, &imgChannels);

                if (imgData != NULL) {
                    /* Create an image from the clipboard data */
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
                    boxes[boxCount].isSelected = 0;
                    boxCount++;
                    selectedBox = boxCount - 1;
                    SelectBox(boxes, boxCount, selectedBox);
                    PushHistoryState(boxes, boxCount, selectedBox);

                    WinClip_FreeData(imgData);
                } else {
                    /* Fallback: create a text box indicating image paste failed */
                    const char* errorText = "(Image from clipboard - processing failed)";
                    int textWidth, textHeight;
                    CalculateTextBoxSize(errorText, 20, &textWidth, &textHeight);

                    boxes[boxCount].x = (int)mousePos.x;
                    boxes[boxCount].y = (int)mousePos.y;
                    boxes[boxCount].width = textWidth;
                    boxes[boxCount].height = textHeight;
                    boxes[boxCount].type = BOX_TEXT;
                    boxes[boxCount].content.text = strdup(errorText);
                    boxes[boxCount].isSelected = 0;
                    boxCount++;
                    selectedBox = boxCount - 1;
                    SelectBox(boxes, boxCount, selectedBox);
                    PushHistoryState(boxes, boxCount, selectedBox);
                }
            } else
            #endif
            {
                const char* clip = GetClipboardTextSafe();
                if (clip && strlen(clip) > 0 && boxCount < MAX_BOXES) {
                char* path = strdup(clip);
                /* Trim quotes if present */
                if (path[0] == '"' && path[strlen(path)-1] == '"') {
                    path[strlen(path)-1] = '\0';
                    path++;
                }
                /* Check if it's an image file path */
                int isImageFile = 0;
                const char* ext = strrchr(path, '.');
                if (ext && (strcmp(ext, ".png") == 0 || strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0 || strcmp(ext, ".bmp") == 0)) {
                    Image img = LoadImage(path);
                    if (IsImageReady(img)) {
                        Texture2D tex = LoadTextureFromImage(img);
                        boxes[boxCount].x = (int)mousePos.x;
                        boxes[boxCount].y = (int)mousePos.y;
                        boxes[boxCount].width = img.width;
                        boxes[boxCount].height = img.height;
                        boxes[boxCount].type = BOX_IMAGE;
                        boxes[boxCount].content.texture = tex;
                        boxes[boxCount].isSelected = 0;
                        boxCount++;
                        UnloadImage(img);
                        isImageFile = 1;
                        selectedBox = boxCount - 1;
                        SelectBox(boxes, boxCount, selectedBox);
                        PushHistoryState(boxes, boxCount, selectedBox);
                    }
                }
                if (!isImageFile) {
                    /* Treat as text - calculate proper dimensions */
                    int textWidth, textHeight;
                    CalculateTextBoxSize(path, 20, &textWidth, &textHeight);

                    boxes[boxCount].x = (int)mousePos.x;
                    boxes[boxCount].y = (int)mousePos.y;
                    boxes[boxCount].width = textWidth;
                    boxes[boxCount].height = textHeight;
                    boxes[boxCount].type = BOX_TEXT;
                    boxes[boxCount].content.text = path;
                    boxes[boxCount].isSelected = 0;
                    boxCount++;
                    selectedBox = boxCount - 1;
                    SelectBox(boxes, boxCount, selectedBox);
                    PushHistoryState(boxes, boxCount, selectedBox);
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
                    if (editingBoxIndex == i) {
                        DrawMultilineText(editingText, box->x + 10, box->y + 10, 20, BLACK);
                        DrawTextCursor(box->x, box->y, 20);
                    } else {
                        DrawMultilineText(box->content.text, box->x + 10, box->y + 10, 20, BLACK);
                    }
                    break;
                case BOX_DRAWING:
                    {
                        Rectangle source = {0.0f, 0.0f, (float)box->content.texture.width, (float)box->content.texture.height};
                        Rectangle dest = {(float)box->x, (float)box->y, (float)box->width, (float)box->height};
                        Vector2 origin = {0.0f, 0.0f};
                        DrawTexturePro(box->content.texture, source, dest, origin, 0.0f, WHITE);
                    }
                    break;
                default:
                    break;
            }

            if (box->isSelected) {
                DrawRectangleLines(box->x, box->y, box->width, box->height, RED);

                if (box->type == BOX_TEXT || box->type == BOX_IMAGE) {
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
        DrawLine(0, (int)TOOLBAR_HEIGHT, screenWidthCurrent, (int)TOOLBAR_HEIGHT, LIGHTGRAY);

        for (int i = 0; i < 5; i++) {
            int isActive = (currentTool == (Tool)i);
            Color fill = isActive ? Fade(SKYBLUE, 0.7f) : Fade(LIGHTGRAY, 0.5f);
            DrawRectangleRec(toolButtons[i], fill);
            DrawRectangleLinesEx(toolButtons[i], 1.0f, DARKGRAY);
            int labelWidth = MeasureText(toolLabels[i], 18);
            DrawText(toolLabels[i], (int)(toolButtons[i].x + (toolButtons[i].width - labelWidth) / 2.0f), (int)(toolButtons[i].y + (toolButtons[i].height - 18.0f) / 2.0f), 18, BLACK);
        }

        for (int i = 0; i < COLOR_PALETTE_COUNT; i++) {
            DrawRectangleRec(colorButtons[i], COLOR_PALETTE[i]);
            DrawRectangleLinesEx(colorButtons[i], 1.0f, DARKGRAY);
            if (ColorsEqual(currentDrawColor, COLOR_PALETTE[i])) {
                Rectangle highlight = {colorButtons[i].x - 2.0f, colorButtons[i].y - 2.0f, colorButtons[i].width + 4.0f, colorButtons[i].height + 4.0f};
                DrawRectangleLinesEx(highlight, 2.0f, BLACK);
            }
        }

        int hasSelection = (selectedBox != -1);
        Color bringFill = hasSelection ? Fade(LIGHTGRAY, 0.6f) : Fade(LIGHTGRAY, 0.3f);
        Color sendFill = bringFill;
        Color bringOutline = hasSelection ? DARKGRAY : GRAY;
        Color sendOutline = bringOutline;

        DrawRectangleRec(bringToFrontButton, bringFill);
        DrawRectangleLinesEx(bringToFrontButton, 1.0f, bringOutline);
        int topLabel = MeasureText("Top", 18);
        DrawText("Top", (int)(bringToFrontButton.x + (bringToFrontButton.width - topLabel) / 2.0f), (int)(bringToFrontButton.y + (bringToFrontButton.height - 18.0f) / 2.0f), 18, BLACK);

        DrawRectangleRec(sendToBackButton, sendFill);
        DrawRectangleLinesEx(sendToBackButton, 1.0f, sendOutline);
        int bottomLabel = MeasureText("Bottom", 18);
        DrawText("Bottom", (int)(sendToBackButton.x + (sendToBackButton.width - bottomLabel) / 2.0f), (int)(sendToBackButton.y + (sendToBackButton.height - 18.0f) / 2.0f), 18, BLACK);

        DrawRectangleRec(exportButton, Fade(LIGHTGRAY, 0.6f));
        DrawRectangleLinesEx(exportButton, 1.0f, DARKGRAY);
        int exportLabel = MeasureText("Export", 18);
        DrawText("Export", (int)(exportButton.x + (exportButton.width - exportLabel) / 2.0f), (int)(exportButton.y + (exportButton.height - 18.0f) / 2.0f), 18, BLACK);

        Color clearFill = showClearConfirm ? Fade(ORANGE, 0.7f) : Fade(LIGHTGRAY, 0.6f);
        DrawRectangleRec(clearButton, clearFill);
        DrawRectangleLinesEx(clearButton, 1.0f, DARKGRAY);
        int clearLabel = MeasureText("Clear", 18);
        DrawText("Clear", (int)(clearButton.x + (clearButton.width - clearLabel) / 2.0f), (int)(clearButton.y + (clearButton.height - 18.0f) / 2.0f), 18, BLACK);

        if (statusMessageTimer > 0.0f && statusMessage[0] != '\0') {
            DrawText(statusMessage, 16, screenHeightCurrent - 28, 18, DARKGRAY);
        }

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
                int cropHeight = screenCapture.height - (int)TOOLBAR_HEIGHT;
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
        default:
            break;
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
    cursorPosition = 0;
    cursorBlinkTime = 0.0f;
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

void PerformUndo(Box* boxes, int* boxCount, int* selectedBox) {
    if (historyIndex > 0) {
        historyIndex--;
        RestoreSnapshotState(boxes, boxCount, selectedBox, historyIndex);
    }
}

void PerformRedo(Box* boxes, int* boxCount, int* selectedBox) {
    if (historyIndex >= 0 && historyIndex < historyCount - 1) {
        historyIndex++;
        RestoreSnapshotState(boxes, boxCount, selectedBox, historyIndex);
    }
}