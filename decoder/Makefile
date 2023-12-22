FFMPEG_INCLUDE ?= /usr/local/ffmpeg
FFMPEG_LIB ?= /usr/local/ffmpeg
CFLAGS = -Wall -Wextra -Werror -I${FFMPEG_INCLUDE} -pthread -g
LDFLAGS = -pthread
ADDITIONAL_LIBS = -L${FFMPEG_LIB} -Wl,--rpath=${FFMPEG_LIB} -lavformat -lavcodec -lavutil -lavfilter -lswresample -lswscale -lm -lz -ljansson
TARGET = nicm

all: $(TARGET)

$(TARGET): main.o detect.o index.o serve.o decode.o check.o lib/framecache.o lib/helper.o lib/scene_detect.o
	$(CC) $(LDFLAGS) -o $@  $^ $(ADDITIONAL_LIBS)

%.o: %.c
	$(CC) -c $(CFLAGS) -o $@ $<

clean:
	$(RM) $(TARGET) *.o lib/*.o

.PHONY: all clean
