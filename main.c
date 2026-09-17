// openengine: a tiny 2D game engine. C + SDL3, games written in Lua.
#include <SDL3/SDL.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <ctype.h>
#include <math.h>
#include <string.h>
#include <unistd.h>

static SDL_Window *win;
static SDL_Renderer *ren;
static bool running = true;
static int status = 0;

// ---------- helpers ----------

static int traceback(lua_State *L) {
    luaL_traceback(L, L, lua_tostring(L, 1), 1);
    return 1;
}

// Call oe.<name>(args...) if defined; nargs values must already be on the stack.
static void callback(lua_State *L, const char *name, int nargs) {
    lua_getglobal(L, "oe");
    lua_getfield(L, -1, name);
    lua_remove(L, -2);
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, nargs + 1);
        return;
    }
    lua_insert(L, -(nargs + 1));
    int base = lua_gettop(L) - nargs;
    lua_pushcfunction(L, traceback);
    lua_insert(L, base);
    if (lua_pcall(L, nargs, 0, base) != LUA_OK) {
        SDL_Log("%s", lua_tostring(L, -1));
        lua_pop(L, 1);
        running = false;
        status = 1;
    }
    lua_remove(L, base);
}

static const char *keyname(SDL_Keycode k) {
    static char buf[64];
    SDL_strlcpy(buf, SDL_GetKeyName(k), sizeof buf);
    for (char *p = buf; *p; p++) *p = (char)tolower((unsigned char)*p);
    return buf;
}

static bool fillmode(lua_State *L, int idx) {
    const char *m = luaL_checkstring(L, idx);
    if (strcmp(m, "fill") == 0) return true;
    if (strcmp(m, "line") == 0) return false;
    return luaL_error(L, "mode must be 'fill' or 'line', got '%s'", m);
}

// ---------- oe.graphics ----------

typedef struct { SDL_Texture *tex; } Image;

static int g_clear(lua_State *L) {
    Uint8 r, g, b, a;
    SDL_GetRenderDrawColor(ren, &r, &g, &b, &a);
    SDL_SetRenderDrawColorFloat(ren, (float)luaL_optnumber(L, 1, 0), (float)luaL_optnumber(L, 2, 0),
                                (float)luaL_optnumber(L, 3, 0), 1);
    SDL_RenderClear(ren);
    SDL_SetRenderDrawColor(ren, r, g, b, a);
    return 0;
}

static int g_setColor(lua_State *L) {
    SDL_SetRenderDrawColorFloat(ren, (float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2),
                                (float)luaL_checknumber(L, 3), (float)luaL_optnumber(L, 4, 1));
    return 0;
}

static int g_rectangle(lua_State *L) {
    bool fill = fillmode(L, 1);
    SDL_FRect r = {(float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3),
                   (float)luaL_checknumber(L, 4), (float)luaL_checknumber(L, 5)};
    if (fill) SDL_RenderFillRect(ren, &r); else SDL_RenderRect(ren, &r);
    return 0;
}

static int g_circle(lua_State *L) {
    bool fill = fillmode(L, 1);
    float cx = (float)luaL_checknumber(L, 2), cy = (float)luaL_checknumber(L, 3);
    float rad = (float)luaL_checknumber(L, 4);
    if (fill) {
        for (float dy = -rad; dy <= rad; dy++) {
            float dx = SDL_sqrtf(rad * rad - dy * dy);
            SDL_RenderLine(ren, cx - dx, cy + dy, cx + dx, cy + dy);
        }
    } else {
        SDL_FPoint pts[65];
        for (int i = 0; i <= 64; i++) {
            float t = (float)i / 64 * 2 * SDL_PI_F;
            pts[i] = (SDL_FPoint){cx + SDL_cosf(t) * rad, cy + SDL_sinf(t) * rad};
        }
        SDL_RenderLines(ren, pts, 65);
    }
    return 0;
}

static int g_line(lua_State *L) {
    SDL_RenderLine(ren, (float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2),
                   (float)luaL_checknumber(L, 3), (float)luaL_checknumber(L, 4));
    return 0;
}

// Built-in 8x8 debug font; scale via SDL_SetRenderScale.
static int g_print(lua_State *L) {
    float x = (float)luaL_optnumber(L, 2, 0), y = (float)luaL_optnumber(L, 3, 0);
    float sc = (float)luaL_optnumber(L, 4, 1);
    const char *s = luaL_tolstring(L, 1, NULL);  // pushes onto the stack, so read args first
    SDL_SetRenderScale(ren, sc, sc);
    SDL_RenderDebugText(ren, x / sc, y / sc, s);
    SDL_SetRenderScale(ren, 1, 1);
    return 0;
}

static int g_newImage(lua_State *L) {
    const char *path = luaL_checkstring(L, 1);
    size_t n = strlen(path);
    SDL_Surface *s = (n > 4 && SDL_strcasecmp(path + n - 4, ".bmp") == 0) ? SDL_LoadBMP(path) : SDL_LoadPNG(path);
    if (!s) return luaL_error(L, "can't load image '%s': %s", path, SDL_GetError());
    Image *img = lua_newuserdatauv(L, sizeof *img, 0);
    img->tex = SDL_CreateTextureFromSurface(ren, s);
    SDL_DestroySurface(s);
    if (!img->tex) return luaL_error(L, "can't create texture: %s", SDL_GetError());
    SDL_SetTextureScaleMode(img->tex, SDL_SCALEMODE_NEAREST);  // pixel-art friendly default
    luaL_setmetatable(L, "oe.Image");
    return 1;
}

// draw(image, x, y, [rotation radians], [sx], [sy]) -- rotates around image center
static int g_draw(lua_State *L) {
    Image *img = luaL_checkudata(L, 1, "oe.Image");
    float sx = (float)luaL_optnumber(L, 5, 1), sy = (float)luaL_optnumber(L, 6, sx);
    SDL_FRect dst = {(float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3),
                     img->tex->w * sx, img->tex->h * sy};
    double deg = luaL_optnumber(L, 4, 0) * 180.0 / M_PI;
    // Tint images with the current draw color.
    Uint8 r, g, b, a;
    SDL_GetRenderDrawColor(ren, &r, &g, &b, &a);
    SDL_SetTextureColorMod(img->tex, r, g, b);
    SDL_SetTextureAlphaMod(img->tex, a);
    SDL_RenderTextureRotated(ren, img->tex, NULL, &dst, deg, NULL, SDL_FLIP_NONE);
    return 0;
}

static int img_gc(lua_State *L) {
    Image *img = luaL_checkudata(L, 1, "oe.Image");
    if (img->tex) SDL_DestroyTexture(img->tex);
    img->tex = NULL;
    return 0;
}

static int img_getWidth(lua_State *L) { lua_pushinteger(L, ((Image *)luaL_checkudata(L, 1, "oe.Image"))->tex->w); return 1; }
static int img_getHeight(lua_State *L) { lua_pushinteger(L, ((Image *)luaL_checkudata(L, 1, "oe.Image"))->tex->h); return 1; }

// ---------- oe.keyboard / oe.mouse / oe.window ----------

static int k_isDown(lua_State *L) {
    SDL_Keycode k = SDL_GetKeyFromName(luaL_checkstring(L, 1));
    if (k == SDLK_UNKNOWN) return luaL_error(L, "unknown key '%s'", lua_tostring(L, 1));
    lua_pushboolean(L, SDL_GetKeyboardState(NULL)[SDL_GetScancodeFromKey(k, NULL)]);
    return 1;
}

static int m_getPosition(lua_State *L) {
    float x, y;
    SDL_GetMouseState(&x, &y);
    lua_pushnumber(L, x);
    lua_pushnumber(L, y);
    return 2;
}

static int m_isDown(lua_State *L) {
    lua_pushboolean(L, SDL_GetMouseState(NULL, NULL) & SDL_BUTTON_MASK(luaL_checkinteger(L, 1)));
    return 1;
}

static int w_setTitle(lua_State *L) { SDL_SetWindowTitle(win, luaL_checkstring(L, 1)); return 0; }

static int w_setSize(lua_State *L) {
    SDL_SetWindowSize(win, (int)luaL_checkinteger(L, 1), (int)luaL_checkinteger(L, 2));
    return 0;
}

static int w_getSize(lua_State *L) {
    int w, h;
    SDL_GetWindowSize(win, &w, &h);
    lua_pushinteger(L, w);
    lua_pushinteger(L, h);
    return 2;
}

static int oe_getTime(lua_State *L) { lua_pushnumber(L, SDL_GetTicksNS() / 1e9); return 1; }
static int oe_quit(lua_State *L) { (void)L; running = false; return 0; }

// ---------- oe.audio ----------
// ponytail: WAV only (SDL built-in) and one playing instance per sound; add stb_vorbis for OGG.

static SDL_AudioDeviceID audiodev;

typedef struct { SDL_AudioStream *stream; Uint8 *buf; Uint32 len; bool loop; } Sound;

// Runs on the audio thread with the stream locked: requeue the sound while looping.
static void SDLCALL sound_refill(void *ud, SDL_AudioStream *s, int additional, int total) {
    Sound *snd = ud;
    (void)total;
    for (; snd->loop && additional > 0; additional -= (int)snd->len)
        SDL_PutAudioStreamData(s, snd->buf, (int)snd->len);
}

static Sound *checksound(lua_State *L) {
    Sound *snd = luaL_checkudata(L, 1, "oe.Sound");
    if (!snd->stream) luaL_error(L, "sound is not loaded");
    return snd;
}

static int a_newSound(lua_State *L) {
    const char *path = luaL_checkstring(L, 1);
    Sound *snd = lua_newuserdatauv(L, sizeof *snd, 0);
    memset(snd, 0, sizeof *snd);
    luaL_setmetatable(L, "oe.Sound");  // set first so __gc cleans up if loading fails
    SDL_AudioSpec spec;
    if (!SDL_LoadWAV(path, &spec, &snd->buf, &snd->len))
        return luaL_error(L, "can't load sound '%s': %s", path, SDL_GetError());
    snd->stream = SDL_CreateAudioStream(&spec, NULL);
    if (!snd->stream) return luaL_error(L, "can't create audio stream: %s", SDL_GetError());
    SDL_SetAudioStreamGetCallback(snd->stream, sound_refill, snd);
    if (audiodev) SDL_BindAudioStream(audiodev, snd->stream);
    return 1;
}

// Restarts the sound if it is already playing.
static int snd_play(lua_State *L) {
    Sound *snd = checksound(L);
    SDL_LockAudioStream(snd->stream);
    SDL_ClearAudioStream(snd->stream);
    SDL_PutAudioStreamData(snd->stream, snd->buf, (int)snd->len);
    SDL_UnlockAudioStream(snd->stream);
    return 0;
}

static int snd_stop(lua_State *L) {
    Sound *snd = checksound(L);
    SDL_ClearAudioStream(snd->stream);
    return 0;
}

static int snd_isPlaying(lua_State *L) {
    lua_pushboolean(L, SDL_GetAudioStreamAvailable(checksound(L)->stream) > 0);
    return 1;
}

static int snd_setVolume(lua_State *L) {
    SDL_SetAudioStreamGain(checksound(L)->stream, (float)luaL_checknumber(L, 2));
    return 0;
}

static int snd_setLooping(lua_State *L) {
    Sound *snd = checksound(L);
    SDL_LockAudioStream(snd->stream);
    snd->loop = lua_toboolean(L, 2);
    SDL_UnlockAudioStream(snd->stream);
    return 0;
}

static int snd_gc(lua_State *L) {
    Sound *snd = luaL_checkudata(L, 1, "oe.Sound");
    if (snd->stream) SDL_DestroyAudioStream(snd->stream);  // unbinds; waits for the callback
    SDL_free(snd->buf);
    snd->stream = NULL;
    snd->buf = NULL;
    return 0;
}

// ---------- main ----------

int luaopen_oe_physics(lua_State *L);

static void sublib(lua_State *L, const char *name, const luaL_Reg *fns) {
    lua_newtable(L);
    luaL_setfuncs(L, fns, 0);
    lua_setfield(L, -2, name);
}

int main(int argc, char **argv) {
    const char *game = argc > 1 ? argv[1] : ".";
    const char *script = "main.lua";
    size_t n = strlen(game);
    if (n > 4 && strcmp(game + n - 4, ".lua") == 0) {
        script = game;
    } else if (chdir(game) != 0) {
        SDL_Log("can't open game directory '%s'", game);
        return 1;
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) { SDL_Log("SDL_Init: %s", SDL_GetError()); return 1; }
    if (!SDL_CreateWindowAndRenderer("openengine", 800, 600, SDL_WINDOW_RESIZABLE, &win, &ren)) {
        SDL_Log("window: %s", SDL_GetError());
        return 1;
    }
    SDL_SetRenderVSync(ren, 1);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    // Audio is optional: games still run (silently) without a sound device.
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO) || !(audiodev = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, NULL)))
        SDL_Log("audio disabled: %s", SDL_GetError());

    lua_State *L = luaL_newstate();
    luaL_openlibs(L);

    luaL_newmetatable(L, "oe.Image");
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    luaL_setfuncs(L, (luaL_Reg[]){{"__gc", img_gc}, {"getWidth", img_getWidth}, {"getHeight", img_getHeight}, {NULL, NULL}}, 0);
    lua_pop(L, 1);

    luaL_newmetatable(L, "oe.Sound");
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    luaL_setfuncs(L, (luaL_Reg[]){{"__gc", snd_gc}, {"play", snd_play}, {"stop", snd_stop}, {"isPlaying", snd_isPlaying},
                                  {"setVolume", snd_setVolume}, {"setLooping", snd_setLooping}, {NULL, NULL}}, 0);
    lua_pop(L, 1);

    luaL_newlib(L, ((luaL_Reg[]){{"getTime", oe_getTime}, {"quit", oe_quit}, {NULL, NULL}}));
    sublib(L, "graphics", (luaL_Reg[]){{"clear", g_clear}, {"setColor", g_setColor}, {"rectangle", g_rectangle},
                                       {"circle", g_circle}, {"line", g_line}, {"print", g_print},
                                       {"newImage", g_newImage}, {"draw", g_draw}, {NULL, NULL}});
    sublib(L, "keyboard", (luaL_Reg[]){{"isDown", k_isDown}, {NULL, NULL}});
    sublib(L, "mouse", (luaL_Reg[]){{"getPosition", m_getPosition}, {"isDown", m_isDown}, {NULL, NULL}});
    sublib(L, "window", (luaL_Reg[]){{"setTitle", w_setTitle}, {"setSize", w_setSize}, {"getSize", w_getSize}, {NULL, NULL}});
    sublib(L, "audio", (luaL_Reg[]){{"newSound", a_newSound}, {NULL, NULL}});
    luaopen_oe_physics(L);
    lua_setfield(L, -2, "physics");
    lua_setglobal(L, "oe");

    lua_pushcfunction(L, traceback);
    if (luaL_loadfile(L, script) != LUA_OK || lua_pcall(L, 0, 0, -2) != LUA_OK) {
        SDL_Log("%s", lua_tostring(L, -1));
        return 1;
    }
    lua_pop(L, 1);

    callback(L, "load", 0);
    Uint64 last = SDL_GetPerformanceCounter();
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_EVENT_QUIT: running = false; break;
            case SDL_EVENT_KEY_DOWN:
                if (e.key.repeat) break;
                lua_pushstring(L, keyname(e.key.key));
                callback(L, "keypressed", 1);
                break;
            case SDL_EVENT_KEY_UP:
                lua_pushstring(L, keyname(e.key.key));
                callback(L, "keyreleased", 1);
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
                lua_pushnumber(L, e.button.x);
                lua_pushnumber(L, e.button.y);
                lua_pushinteger(L, e.button.button);
                callback(L, e.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? "mousepressed" : "mousereleased", 3);
                break;
            }
        }
        Uint64 now = SDL_GetPerformanceCounter();
        lua_pushnumber(L, (double)(now - last) / SDL_GetPerformanceFrequency());
        last = now;
        callback(L, "update", 1);

        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
        callback(L, "draw", 0);
        SDL_RenderPresent(ren);
    }

    lua_close(L);  // runs __gc on images and sounds before SDL shuts down
    SDL_Quit();
    return status;
}
