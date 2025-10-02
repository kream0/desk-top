CC = gcc
CFLAGS = -Wall -std=c99

TARGET = desktop_app
SRC = main.c win_clipboard.c win_video.c

ifeq ($(OS),Windows_NT)
LDFLAGS = -lraylib -lm -lgdi32 -lwinmm -lole32 -luuid -lmfplat -lmfreadwrite -lmfuuid -lshlwapi
else
LDFLAGS = -lraylib -lm
endif

# For macOS
# LDFLAGS += -framework CoreVideo -framework IOKit -framework Cocoa -framework GLUT -framework OpenGL

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: clean