#ifndef WIN_CLIPBOARD_H
#define WIN_CLIPBOARD_H

#ifdef _WIN32

/* Windows clipboard handling that bypasses GLFW entirely */

/* Function declarations */
int WinClip_HasText(void);
int WinClip_HasImage(void);
int WinClip_HasFileDrop(void);
char* WinClip_GetText(void);
void* WinClip_GetImageData(int* width, int* height, int* channels);
void WinClip_FreeData(void* data);
int WinClip_SetImageRGBA(const unsigned char* data, int width, int height);
char** WinClip_GetFileDropList(int* count);
void WinClip_FreeFileDropList(char** list, int count);

#endif /* _WIN32 */

#endif /* WIN_CLIPBOARD_H */