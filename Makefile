CC = gcc
CFLAGS = -Wall -std=c99

TARGET = desktop_app
SRC = main.c win_clipboard.c win_video.c
PROBE = video_probe
PROBE_SRC = video_probe.c win_video.c

ifeq ($(OS),Windows_NT)
LDFLAGS = -lraylib -lm -lgdi32 -lwinmm -lole32 -luuid -lmfplat -lmfreadwrite -lmfuuid -lshlwapi
else
LDFLAGS = -lraylib -lm
endif

# For macOS
# LDFLAGS += -framework CoreVideo -framework IOKit -framework Cocoa -framework GLUT -framework OpenGL

all: $(TARGET) $(PROBE)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)

$(PROBE): $(PROBE_SRC)
	$(CC) $(CFLAGS) -o $(PROBE) $(PROBE_SRC) $(LDFLAGS)

clean:
	rm -f $(TARGET) $(PROBE)

.PHONY: clean all