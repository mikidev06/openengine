LUA ?= lua
CFLAGS += -std=c11 -D_DEFAULT_SOURCE -O2 -Wall -Wextra $(shell pkg-config --cflags sdl3 $(LUA))
LDLIBS += $(shell pkg-config --libs sdl3 $(LUA)) -lm

# vendor/*_impl.c wrap the single-header stb_image/stb_vorbis decoders; -w silences
# their internal warnings since they're third-party code we don't edit.
VENDOR_OBJS = vendor/stb_image_impl.o vendor/stb_vorbis_impl.o

openengine: main.c physics.c $(VENDOR_OBJS)
	$(CC) $(CFLAGS) -o $@ main.c physics.c $(VENDOR_OBJS) $(LDLIBS)

vendor/%_impl.o: vendor/%_impl.c
	$(CC) -std=c11 -D_DEFAULT_SOURCE -O2 -w -c -o $@ $<

clean:
	rm -f openengine $(VENDOR_OBJS)

test: openengine
	SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy ./openengine test.lua
