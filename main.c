#include "raylib.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#include "win_clipboard.h"
#endif

#define MAX_BOXES 100

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

/* Text editing state */
int editingBoxIndex = -1;
char editingText[1024] = {0};
int cursorPosition = 0;
float cursorBlinkTime = 0.0f;

void UpdateEditingBoxSize(Box* boxes);
ResizeMode GetResizeModeForPoint(const Box* box, Vector2 point);
void ApplyResize(Box* box, ResizeMode mode, Vector2 delta);
int MouseCursorForResizeMode(ResizeMode mode);
Rectangle GetBoxRect(const Box* box);
int FindTopmostBoxAtPoint(Vector2 point, Box* boxes, int boxCount);

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

        editingBoxIndex = -1;
        memset(editingText, 0, sizeof(editingText));
        cursorPosition = 0;
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

    /* Double-click detection */
    static double lastClickTime = 0.0;
    static Vector2 lastClickPos = {0, 0};
    const double doubleClickInterval = 0.5;  /* 500ms */
    const float doubleClickDistance = 10.0f;  /* 10 pixel tolerance */

    SetTargetFPS(60);

    int currentCursor = MOUSE_CURSOR_DEFAULT;

    while (!WindowShouldClose())
    {
        mousePos = GetMousePosition();

        int hoveredBox = FindTopmostBoxAtPoint(mousePos, boxes, boxCount);
        ResizeMode hoverResizeMode = RESIZE_NONE;
        if (hoveredBox != -1) {
            hoverResizeMode = GetResizeModeForPoint(&boxes[hoveredBox], mousePos);
        }

        /* Handle input */

        /* Handle text editing */
        HandleTextInput(boxes);

        /* Exit text editing on Escape */
        if (IsKeyPressed(KEY_ESCAPE) && editingBoxIndex >= 0) {
            StopTextEdit(boxes);
        }

        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            if (editingBoxIndex >= 0) {
                Rectangle editingRect = {
                    boxes[editingBoxIndex].x,
                    boxes[editingBoxIndex].y,
                    (float)boxes[editingBoxIndex].width,
                    (float)boxes[editingBoxIndex].height
                };

                if (!CheckCollisionPointRec(mousePos, editingRect)) {
                    StopTextEdit(boxes);
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

                if (boxes[selectedBox].type == BOX_TEXT && editingBoxIndex == selectedBox) {
                    if (resizeMode != RESIZE_NONE) {
                        resizeMode = RESIZE_NONE;
                    }
                }

                if (boxes[selectedBox].type == BOX_TEXT && editingBoxIndex == selectedBox) {
                    isDragging = 0;
                }
            } else {
                SelectBox(boxes, boxCount, -1);
                selectedBox = -1;
                resizeMode = RESIZE_NONE;
                isDragging = 0;

                if (editingBoxIndex >= 0) {
                    StopTextEdit(boxes);
                }

                if (currentTool == TOOL_RECT || currentTool == TOOL_CIRCLE) {
                    startX = (int)mousePos.x;
                    startY = (int)mousePos.y;
                    isDrawing = 1;
                }
            }
        }

        if (IsMouseButtonDown(MOUSE_LEFT_BUTTON) && isDragging && selectedBox != -1) {
            Vector2 delta = {mousePos.x - prevMousePos.x, mousePos.y - prevMousePos.y};
            if (resizeMode == RESIZE_NONE) {
                boxes[selectedBox].x += (int)delta.x;
                boxes[selectedBox].y += (int)delta.y;
            } else {
                if (!(boxes[selectedBox].type == BOX_TEXT && editingBoxIndex == selectedBox)) {
                    ApplyResize(&boxes[selectedBox], resizeMode, delta);
                }
            }
        }

        if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) {
            isDragging = 0;
            resizeMode = RESIZE_NONE;
            if (isDrawing) {
                if (currentTool == TOOL_RECT && boxCount < MAX_BOXES) {
                    int endX = (int)mousePos.x;
                    int endY = (int)mousePos.y;
                    int x = startX < endX ? startX : endX;
                    int y = startY < endY ? startY : endY;
                    int width = abs(endX - startX);
                    int height = abs(endY - startY);
                    boxes[boxCount].x = x;
                    boxes[boxCount].y = y;
                    boxes[boxCount].width = width;
                    boxes[boxCount].height = height;
                    boxes[boxCount].type = BOX_DRAWING;
                    /* create texture */
                    RenderTexture2D rt = LoadRenderTexture(width, height);
                    BeginTextureMode(rt);
                    ClearBackground(BLANK);
                    DrawRectangle(0, 0, width, height, BLACK);
                    EndTextureMode();
                    boxes[boxCount].content.texture = rt.texture;
                    boxes[boxCount].isSelected = 0;
                    boxCount++;
                } else if (currentTool == TOOL_CIRCLE && boxCount < MAX_BOXES) {
                    int centerX = startX;
                    int centerY = startY;
                    float dx = mousePos.x - startX;
                    float dy = mousePos.y - startY;
                    int radius = (int)sqrt(dx*dx + dy*dy);
                    int x = centerX - radius;
                    int y = centerY - radius;
                    int width = radius * 2;
                    int height = radius * 2;
                    boxes[boxCount].x = x;
                    boxes[boxCount].y = y;
                    boxes[boxCount].width = width;
                    boxes[boxCount].height = height;
                    boxes[boxCount].type = BOX_DRAWING;
                    RenderTexture2D rt = LoadRenderTexture(width, height);
                    BeginTextureMode(rt);
                    ClearBackground(BLANK);
                    DrawCircle(radius, radius, radius, BLACK);
                    EndTextureMode();
                    boxes[boxCount].content.texture = rt.texture;
                    boxes[boxCount].isSelected = 0;
                    boxCount++;
                }
                isDrawing = 0;
            }
        }

        /* Delete selected box */
        if (IsKeyPressed(KEY_DELETE) && selectedBox != -1) {
            int i;
            for (i = selectedBox; i < boxCount - 1; i++) {
                boxes[i] = boxes[i+1];
            }
            boxCount--;
            selectedBox = -1;
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
            int editingSameBox = (boxes[hoveredBox].type == BOX_TEXT && editingBoxIndex == hoveredBox);
            if (!editingSameBox) {
                desiredCursor = MouseCursorForResizeMode(hoverResizeMode);
            }
        } else if (hoveredBox != -1 && boxes[hoveredBox].isSelected) {
            desiredCursor = MOUSE_CURSOR_POINTING_HAND;
        }

        if (desiredCursor != currentCursor) {
            SetMouseCursor(desiredCursor);
            currentCursor = desiredCursor;
        }

        prevMousePos = mousePos;

        /* Tool selection */
        if (IsKeyPressed(KEY_S)) currentTool = TOOL_SELECT;
        if (IsKeyPressed(KEY_R)) currentTool = TOOL_RECT;
        if (IsKeyPressed(KEY_C)) currentTool = TOOL_CIRCLE;

        /* Paste */
        if (IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_V)) {
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
                }
                }
            }
        }

        BeginDrawing();

        ClearBackground(RAYWHITE);

        /* Draw boxes */
        {
            int i;
            for (i = 0; i < boxCount; i++) {
                Box* box = &boxes[i];
                /* draw box background */
                DrawRectangle(box->x, box->y, box->width, box->height, WHITE);
                /* draw content */
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
                        /* Show editing text if this box is being edited, otherwise show normal text */
                        if (editingBoxIndex == i) {
                            DrawMultilineText(editingText, box->x + 10, box->y + 10, 20, BLACK);
                            DrawTextCursor(box->x, box->y, 20);
                        } else {
                            DrawMultilineText(box->content.text, box->x + 10, box->y + 10, 20, BLACK);
                        }
                        break;
                    /* TODO: handle video, audio, drawing */
                    default:
                        break;
                }
                /* draw border if selected */
                if (box->isSelected) {
                    DrawRectangleLines(box->x, box->y, box->width, box->height, RED);

                    if (box->type == BOX_TEXT || box->type == BOX_IMAGE) {
                        DrawResizeHandles(box);
                    }
                }
            }
        }

        /* Draw drawing preview */
        if (isDrawing) {
            if (currentTool == TOOL_RECT) {
                int endX = (int)mousePos.x;
                int endY = (int)mousePos.y;
                int x = startX < endX ? startX : endX;
                int y = startY < endY ? startY : endY;
                int width = abs(endX - startX);
                int height = abs(endY - startY);
                DrawRectangle(x, y, width, height, RED);
            } else if (currentTool == TOOL_CIRCLE) {
                float dx = mousePos.x - startX;
                float dy = mousePos.y - startY;
                int radius = (int)sqrt(dx*dx + dy*dy);
                DrawCircle(startX, startY, radius, RED);
            }
        }

        EndDrawing();
    }

    CloseWindow();

    return 0;
}