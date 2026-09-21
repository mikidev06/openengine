// openengine: a tiny 2D game engine. C + SDL3, games written in Lua.
#include <SDL3/SDL.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <ctype.h>
#include <math.h>
#include <string.h>
#include <unistd.h>

#define STBI_NO_STDIO
#include "vendor/stb_image.h"

#define STB_VORBIS_HEADER_ONLY
#include "vendor/stb_vorbis.c"
#undef STB_VORBIS_HEADER_ONLY

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

// ---------- oe.graphics: camera transform ----------
// A stack of similarity transforms (translate + rotate + uniform scale, no
// shear), applied to every draw call. Reset to identity at the start of each
// frame, like love2d.

typedef struct { float tx, ty, angle, scale; } Transform;
static const Transform IDENTITY_XFORM = {0, 0, 0, 1};
static Transform xform = {0, 0, 0, 1};
static Transform xstack[64];
static int xdepth = 0;

static void xform_point(float x, float y, float *ox, float *oy) {
    float c = SDL_cosf(xform.angle), s = SDL_sinf(xform.angle);
    *ox = xform.tx + xform.scale * (c * x - s * y);
    *oy = xform.ty + xform.scale * (s * x + c * y);
}

static int g_push(lua_State *L) {
    if (xdepth >= (int)(sizeof xstack / sizeof *xstack)) return luaL_error(L, "transform stack overflow");
    xstack[xdepth++] = xform;
    return 0;
}

static int g_pop(lua_State *L) {
    if (xdepth == 0) return luaL_error(L, "transform stack underflow (pop without push)");
    xform = xstack[--xdepth];
    return 0;
}

static int g_origin(lua_State *L) { (void)L; xform = IDENTITY_XFORM; return 0; }

static int g_translate(lua_State *L) {
    float dx = (float)luaL_checknumber(L, 1), dy = (float)luaL_checknumber(L, 2);
    float c = SDL_cosf(xform.angle), s = SDL_sinf(xform.angle);
    xform.tx += xform.scale * (c * dx - s * dy);
    xform.ty += xform.scale * (s * dx + c * dy);
    return 0;
}

static int g_rotate(lua_State *L) { xform.angle += (float)luaL_checknumber(L, 1); return 0; }
static int g_scale(lua_State *L) { xform.scale *= (float)luaL_checknumber(L, 1); return 0; }

// ---------- oe.graphics ----------

typedef struct { SDL_Texture *tex; } Image;
typedef struct { float x, y, w, h; } Quad;  // a pixel-space sub-rectangle of an image

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
    float x = (float)luaL_checknumber(L, 2), y = (float)luaL_checknumber(L, 3);
    float w = (float)luaL_checknumber(L, 4), h = (float)luaL_checknumber(L, 5);
    SDL_FPoint p[4];
    xform_point(x, y, &p[0].x, &p[0].y);
    xform_point(x + w, y, &p[1].x, &p[1].y);
    xform_point(x + w, y + h, &p[2].x, &p[2].y);
    xform_point(x, y + h, &p[3].x, &p[3].y);
    if (fill) {
        SDL_FColor col;
        SDL_GetRenderDrawColorFloat(ren, &col.r, &col.g, &col.b, &col.a);
        SDL_Vertex v[4] = {{p[0], col, {0, 0}}, {p[1], col, {0, 0}}, {p[2], col, {0, 0}}, {p[3], col, {0, 0}}};
        int idx[6] = {0, 1, 2, 0, 2, 3};
        SDL_RenderGeometry(ren, NULL, v, 4, idx, 6);
    } else {
        SDL_FPoint loop[5] = {p[0], p[1], p[2], p[3], p[0]};
        SDL_RenderLines(ren, loop, 5);
    }
    return 0;
}

static int g_circle(lua_State *L) {
    bool fill = fillmode(L, 1);
    float cx = (float)luaL_checknumber(L, 2), cy = (float)luaL_checknumber(L, 3);
    float rad = (float)luaL_checknumber(L, 4);
    SDL_FPoint pts[65];
    for (int i = 0; i <= 64; i++) {
        float t = (float)i / 64 * 2 * SDL_PI_F;
        xform_point(cx + SDL_cosf(t) * rad, cy + SDL_sinf(t) * rad, &pts[i].x, &pts[i].y);
    }
    if (fill) {
        SDL_FColor col;
        SDL_GetRenderDrawColorFloat(ren, &col.r, &col.g, &col.b, &col.a);
        SDL_FPoint center;
        xform_point(cx, cy, &center.x, &center.y);
        SDL_Vertex v[66];
        v[0] = (SDL_Vertex){center, col, {0, 0}};
        for (int i = 0; i <= 64; i++) v[i + 1] = (SDL_Vertex){pts[i], col, {0, 0}};
        int idx[64 * 3];
        for (int i = 0; i < 64; i++) { idx[i * 3] = 0; idx[i * 3 + 1] = i + 1; idx[i * 3 + 2] = i + 2; }
        SDL_RenderGeometry(ren, NULL, v, 66, idx, 64 * 3);
    } else {
        SDL_RenderLines(ren, pts, 65);
    }
    return 0;
}

static int g_line(lua_State *L) {
    SDL_FPoint p1, p2;
    xform_point((float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2), &p1.x, &p1.y);
    xform_point((float)luaL_checknumber(L, 3), (float)luaL_checknumber(L, 4), &p2.x, &p2.y);
    SDL_RenderLine(ren, p1.x, p1.y, p2.x, p2.y);
    return 0;
}

// Built-in 8x8 debug font; scale via SDL_SetRenderScale. Camera position and
// zoom apply; camera rotation doesn't (SDL's debug text can't be rotated).
static int g_print(lua_State *L) {
    float x = (float)luaL_optnumber(L, 2, 0), y = (float)luaL_optnumber(L, 3, 0);
    float sc = (float)luaL_optnumber(L, 4, 1) * xform.scale;
    const char *s = luaL_tolstring(L, 1, NULL);  // pushes onto the stack, so read args first
    float ox, oy;
    xform_point(x, y, &ox, &oy);
    SDL_SetRenderScale(ren, sc, sc);
    SDL_RenderDebugText(ren, ox / sc, oy / sc, s);
    SDL_SetRenderScale(ren, 1, 1);
    return 0;
}

static int g_newImage(lua_State *L) {
    const char *path = luaL_checkstring(L, 1);
    size_t n = strlen(path);
    SDL_Surface *s;
    if (n > 4 && SDL_strcasecmp(path + n - 4, ".bmp") == 0) {
        s = SDL_LoadBMP(path);
        if (!s) return luaL_error(L, "can't load image '%s': %s", path, SDL_GetError());
    } else {
        size_t filelen;
        void *filedata = SDL_LoadFile(path, &filelen);
        if (!filedata) return luaL_error(L, "can't load image '%s': %s", path, SDL_GetError());
        int w, h, channels;
        unsigned char *pixels = stbi_load_from_memory(filedata, (int)filelen, &w, &h, &channels, 4);
        SDL_free(filedata);
        if (!pixels) return luaL_error(L, "can't decode image '%s': %s", path, stbi_failure_reason());
        s = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32);
        if (!s) {
            stbi_image_free(pixels);
            return luaL_error(L, "can't create surface: %s", SDL_GetError());
        }
        memcpy(s->pixels, pixels, (size_t)w * (size_t)h * 4);
        stbi_image_free(pixels);
    }
    Image *img = lua_newuserdatauv(L, sizeof *img, 0);
    img->tex = SDL_CreateTextureFromSurface(ren, s);
    SDL_DestroySurface(s);
    if (!img->tex) return luaL_error(L, "can't create texture: %s", SDL_GetError());
    SDL_SetTextureScaleMode(img->tex, SDL_SCALEMODE_NEAREST);  // pixel-art friendly default
    luaL_setmetatable(L, "oe.Image");
    return 1;
}

// oe.graphics.newQuad(x, y, w, h) -- a pixel-space region of an image, for spritesheets
static int g_newQuad(lua_State *L) {
    Quad *q = lua_newuserdatauv(L, sizeof *q, 0);
    q->x = (float)luaL_checknumber(L, 1);
    q->y = (float)luaL_checknumber(L, 2);
    q->w = (float)luaL_checknumber(L, 3);
    q->h = (float)luaL_checknumber(L, 4);
    luaL_argcheck(L, q->w > 0 && q->h > 0, 3, "quad size must be positive");
    luaL_setmetatable(L, "oe.Quad");
    return 1;
}

// draw(image, x, y, [rotation radians], [sx], [sy], [quad]) -- rotates around center;
// quad selects a sub-rectangle of the image (e.g. one frame of a spritesheet)
static int g_draw(lua_State *L) {
    Image *img = luaL_checkudata(L, 1, "oe.Image");
    float x = (float)luaL_checknumber(L, 2), y = (float)luaL_checknumber(L, 3);
    double rot = luaL_optnumber(L, 4, 0);
    float sx = (float)luaL_optnumber(L, 5, 1), sy = (float)luaL_optnumber(L, 6, sx);
    Quad *q = luaL_testudata(L, 7, "oe.Quad");
    SDL_FRect src, *srcptr = NULL;
    float srcw = (float)img->tex->w, srch = (float)img->tex->h;
    if (q) {
        src = (SDL_FRect){q->x, q->y, q->w, q->h};
        srcptr = &src;
        srcw = q->w;
        srch = q->h;
    }
    float w = srcw * sx * xform.scale, h = srch * sy * xform.scale;
    float cx, cy;
    xform_point(x + srcw * sx / 2, y + srch * sy / 2, &cx, &cy);
    SDL_FRect dst = {cx - w / 2, cy - h / 2, w, h};
    double deg = rot * 180.0 / M_PI + xform.angle * 180.0 / M_PI;
    // Tint images with the current draw color.
    Uint8 r, g, b, a;
    SDL_GetRenderDrawColor(ren, &r, &g, &b, &a);
    SDL_SetTextureColorMod(img->tex, r, g, b);
    SDL_SetTextureAlphaMod(img->tex, a);
    SDL_RenderTextureRotated(ren, img->tex, srcptr, &dst, deg, NULL, SDL_FLIP_NONE);
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

// ---------- oe.gamepad ----------
// Single active gamepad: the first one connected. Fine for local single-player
// games; multi-gamepad support would need a per-player handle from Lua.

static SDL_Gamepad *pad;

static int p_isDown(lua_State *L) {
    const char *name = luaL_checkstring(L, 1);
    SDL_GamepadButton btn = SDL_GetGamepadButtonFromString(name);
    if (btn == SDL_GAMEPAD_BUTTON_INVALID) return luaL_error(L, "unknown gamepad button '%s'", name);
    lua_pushboolean(L, pad && SDL_GetGamepadButton(pad, btn));
    return 1;
}

static int p_getAxis(lua_State *L) {
    const char *name = luaL_checkstring(L, 1);
    SDL_GamepadAxis ax = SDL_GetGamepadAxisFromString(name);
    if (ax == SDL_GAMEPAD_AXIS_INVALID) return luaL_error(L, "unknown gamepad axis '%s'", name);
    float v = pad ? SDL_GetGamepadAxis(pad, ax) / 32767.0f : 0;
    lua_pushnumber(L, v < -1 ? -1 : v);
    return 1;
}

static int p_isConnected(lua_State *L) { lua_pushboolean(L, pad != NULL); return 1; }

// ---------- oe.audio ----------
// ogg via stb_vorbis, everything else via SDL's own WAV loader; one playing
// instance per sound.

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
    size_t n = strlen(path);
    Sound *snd = lua_newuserdatauv(L, sizeof *snd, 0);
    memset(snd, 0, sizeof *snd);
    luaL_setmetatable(L, "oe.Sound");  // set first so __gc cleans up if loading fails
    SDL_AudioSpec spec;
    if (n > 4 && SDL_strcasecmp(path + n - 4, ".ogg") == 0) {
        size_t filelen;
        void *filedata = SDL_LoadFile(path, &filelen);
        if (!filedata) return luaL_error(L, "can't load sound '%s': %s", path, SDL_GetError());
        int channels, rate;
        short *pcm;
        int samples = stb_vorbis_decode_memory(filedata, (int)filelen, &channels, &rate, &pcm);
        SDL_free(filedata);
        if (samples < 0) return luaL_error(L, "can't decode ogg '%s'", path);
        spec.format = SDL_AUDIO_S16;
        spec.channels = channels;
        spec.freq = rate;
        snd->len = (Uint32)samples * (Uint32)channels * sizeof(short);
        snd->buf = SDL_malloc(snd->len);
        if (!snd->buf) { free(pcm); return luaL_error(L, "out of memory"); }
        memcpy(snd->buf, pcm, snd->len);
        free(pcm);  // stb_vorbis allocates with malloc, not SDL_malloc
    } else if (!SDL_LoadWAV(path, &spec, &snd->buf, &snd->len)) {
        return luaL_error(L, "can't load sound '%s': %s", path, SDL_GetError());
    }
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

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) { SDL_Log("SDL_Init: %s", SDL_GetError()); return 1; }
    if (!SDL_CreateWindowAndRenderer("openengine", 800, 600, SDL_WINDOW_RESIZABLE, &win, &ren)) {
        SDL_Log("window: %s", SDL_GetError());
        return 1;
    }
    SDL_SetRenderVSync(ren, 1);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    // Audio is optional: games still run (silently) without a sound device.
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO) || !(audiodev = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, NULL)))
        SDL_Log("audio disabled: %s", SDL_GetError());

    int npads;
    SDL_JoystickID *padids = SDL_GetGamepads(&npads);
    if (padids) {
        if (npads > 0) pad = SDL_OpenGamepad(padids[0]);
        SDL_free(padids);
    }

    lua_State *L = luaL_newstate();
    luaL_openlibs(L);

    luaL_newmetatable(L, "oe.Image");
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    luaL_setfuncs(L, (luaL_Reg[]){{"__gc", img_gc}, {"getWidth", img_getWidth}, {"getHeight", img_getHeight}, {NULL, NULL}}, 0);
    lua_pop(L, 1);

    luaL_newmetatable(L, "oe.Quad");
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
                                       {"newImage", g_newImage}, {"newQuad", g_newQuad}, {"draw", g_draw},
                                       {"push", g_push}, {"pop", g_pop}, {"origin", g_origin},
                                       {"translate", g_translate}, {"rotate", g_rotate}, {"scale", g_scale}, {NULL, NULL}});
    sublib(L, "keyboard", (luaL_Reg[]){{"isDown", k_isDown}, {NULL, NULL}});
    sublib(L, "mouse", (luaL_Reg[]){{"getPosition", m_getPosition}, {"isDown", m_isDown}, {NULL, NULL}});
    sublib(L, "window", (luaL_Reg[]){{"setTitle", w_setTitle}, {"setSize", w_setSize}, {"getSize", w_getSize}, {NULL, NULL}});
    sublib(L, "audio", (luaL_Reg[]){{"newSound", a_newSound}, {NULL, NULL}});
    sublib(L, "gamepad", (luaL_Reg[]){{"isDown", p_isDown}, {"getAxis", p_getAxis}, {"isConnected", p_isConnected}, {NULL, NULL}});
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
            case SDL_EVENT_GAMEPAD_ADDED:
                if (!pad) pad = SDL_OpenGamepad(e.gdevice.which);
                break;
            case SDL_EVENT_GAMEPAD_REMOVED:
                if (pad && SDL_GetGamepadID(pad) == e.gdevice.which) {
                    SDL_CloseGamepad(pad);
                    pad = NULL;
                }
                break;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            case SDL_EVENT_GAMEPAD_BUTTON_UP:
                lua_pushstring(L, SDL_GetGamepadStringForButton((SDL_GamepadButton)e.gbutton.button));
                callback(L, e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ? "gamepadpressed" : "gamepadreleased", 1);
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
        xform = IDENTITY_XFORM;
        xdepth = 0;
        callback(L, "draw", 0);
        SDL_RenderPresent(ren);
    }

    if (pad) SDL_CloseGamepad(pad);
    lua_close(L);  // runs __gc on images and sounds before SDL shuts down
    SDL_Quit();
    return status;
}
