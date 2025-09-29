#include "raylib.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>

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
    int resizeMode = 0;
    Tool currentTool = TOOL_SELECT;
    int isDrawing = 0;
    int startX, startY;

    SetTargetFPS(60);

    while (!WindowShouldClose())
    {
        mousePos = GetMousePosition();

        /* Handle input */
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            /* check if clicked on a box */
            selectedBox = -1;
            int i;
            for (i = 0; i < boxCount; i++) {
                if (CheckCollisionPointRec(mousePos, (Rectangle){boxes[i].x, boxes[i].y, boxes[i].width, boxes[i].height})) {
                    selectedBox = i;
                    boxes[i].isSelected = 1;
                    /* check for resize */
                    Box* box = &boxes[selectedBox];
                    if (mousePos.x > box->x + box->width - 10) {
                        resizeMode = 1; /* right */
                    } else if (mousePos.y > box->y + box->height - 10) {
                        resizeMode = 2; /* bottom */
                    } else {
                        resizeMode = 0;
                    }
                    isDragging = 1;
                    break;
                }
            }
            /* if not on box, deselect all */
            if (selectedBox == -1) {
                for (i = 0; i < boxCount; i++) boxes[i].isSelected = 0;
                resizeMode = 0;
                /* start drawing if tool active */
                if ((currentTool == TOOL_RECT || currentTool == TOOL_CIRCLE) && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                    startX = (int)mousePos.x;
                    startY = (int)mousePos.y;
                    isDrawing = 1;
                }
            } else {
                /* check for resize */
                Box* box = &boxes[selectedBox];
                int nearRight = mousePos.x > box->x + box->width - 10;
                int nearBottom = mousePos.y > box->y + box->height - 10;
                if (nearRight && nearBottom) {
                    resizeMode = 3; /* corner */
                } else if (nearRight) {
                    resizeMode = 1; /* right */
                } else if (nearBottom) {
                    resizeMode = 2; /* bottom */
                } else {
                    resizeMode = 0;
                }
                isDragging = 1;
            }
        }

        if (IsMouseButtonDown(MOUSE_LEFT_BUTTON) && isDragging && selectedBox != -1) {
            if (resizeMode == 0) {
                /* move */
                Vector2 delta = {mousePos.x - prevMousePos.x, mousePos.y - prevMousePos.y};
                boxes[selectedBox].x += (int)delta.x;
                boxes[selectedBox].y += (int)delta.y;
            } else if (resizeMode == 1) {
                boxes[selectedBox].width += (int)(mousePos.x - prevMousePos.x);
            } else if (resizeMode == 2) {
                boxes[selectedBox].height += (int)(mousePos.y - prevMousePos.y);
            } else if (resizeMode == 3) {
                boxes[selectedBox].width += (int)(mousePos.x - prevMousePos.x);
                boxes[selectedBox].height += (int)(mousePos.y - prevMousePos.y);
            }
        }

        if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) {
            isDragging = 0;
            resizeMode = 0;
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

        prevMousePos = mousePos;

        /* Tool selection */
        if (IsKeyPressed(KEY_S)) currentTool = TOOL_SELECT;
        if (IsKeyPressed(KEY_R)) currentTool = TOOL_RECT;
        if (IsKeyPressed(KEY_C)) currentTool = TOOL_CIRCLE;

        /* Paste */
        if (IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_V)) {
            const char* clip = GetClipboardText();
            if (clip && strlen(clip) > 0 && boxCount < MAX_BOXES) {
                /* Check if it's an image file path */
                int isImageFile = 0;
                const char* ext = strrchr(clip, '.');
                if (ext && (strcmp(ext, ".png") == 0 || strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0 || strcmp(ext, ".bmp") == 0)) {
                    Image img = LoadImage(clip);
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
                    /* Treat as text */
                    boxes[boxCount].x = (int)mousePos.x;
                    boxes[boxCount].y = (int)mousePos.y;
                    boxes[boxCount].width = 200;
                    boxes[boxCount].height = 50;
                    boxes[boxCount].type = BOX_TEXT;
                    boxes[boxCount].content.text = strdup(clip);
                    boxes[boxCount].isSelected = 0;
                    boxCount++;
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
                        DrawTexture(box->content.texture, box->x, box->y, WHITE);
                        break;
                    case BOX_TEXT:
                        DrawText(box->content.text, box->x + 5, box->y + 5, 20, BLACK);
                        break;
                    /* TODO: handle video, audio, drawing */
                    default:
                        break;
                }
                /* draw border if selected */
                if (box->isSelected) {
                    DrawRectangleLines(box->x, box->y, box->width, box->height, RED);
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