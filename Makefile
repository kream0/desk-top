CC = gcc
CFLAGS = -Wall -std=c99
LDFLAGS = -lraylib -lm

# For Windows with MinGW
# LDFLAGS = -lraylib -lm -lpthread -ldl -lwinmm -lgdi32

# For macOS
# LDFLAGS = -lraylib -lm -framework CoreVideo -framework IOKit -framework Cocoa -framework GLUT -framework OpenGL

TARGET = desktop_app
SRC = main.c

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: clean