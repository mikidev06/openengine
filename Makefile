LUA ?= lua
CFLAGS += -std=c11 -D_DEFAULT_SOURCE -O2 -Wall -Wextra $(shell pkg-config --cflags sdl3 $(LUA))
LDLIBS += $(shell pkg-config --libs sdl3 $(LUA)) -lm

openengine: main.c physics.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

clean:
	rm -f openengine

test: openengine
	SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy ./openengine test.lua
