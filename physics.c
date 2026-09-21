// oe.physics: small impulse-based physics. Circles and axis-aligned rectangles, no rotation.
// ponytail: no rotation/joints; switch to Box2D v3 (also C) if a game needs them.
#include <lua.h>
#include <lauxlib.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum { CIRCLE, RECT };

typedef struct {
    int shape;
    unsigned id;
    float x, y, vx, vy, w, h, r;  // position is the center
    float invMass, restitution, friction;
    int dead;
} Body;

typedef struct { Body *a, *b; unsigned ida, idb; float nx, ny; } Contact;

typedef struct {
    float gx, gy;
    Body **bodies;
    int n, cap;
    Contact *contacts;
    int nc, ccap;
    unsigned nextId;

    // Broadphase, rebuilt once per world:update() call: dynamic bodies go in a
    // uniform grid (bucketed by center cell), static bodies are checked against
    // every dynamic body directly (there are usually few of them, e.g. walls).
    Body **dyn;
    int ndyn, dynCap;
    Body **statc;
    int nstat, statCap;
    int32_t *cellx, *celly;  // per-dynamic-body cell coords, parallel to dyn[]
    int *gridHead, *gridNext;  // gridHead[hash] -> dyn index, chained via gridNext
    int gridCap;
    float cellSize;
} World;

// World uservalue table: [lightuserdata body] = body userdata (keeps bodies alive), .callback = fn

static World *checkworld(lua_State *L) { return luaL_checkudata(L, 1, "oe.World"); }
static Body *checkbody(lua_State *L) { return luaL_checkudata(L, 1, "oe.Body"); }

static int w_new(lua_State *L) {
    World *w = lua_newuserdatauv(L, sizeof *w, 1);
    memset(w, 0, sizeof *w);
    w->gx = (float)luaL_optnumber(L, 1, 0);
    w->gy = (float)luaL_optnumber(L, 2, 0);
    lua_newtable(L);
    lua_setiuservalue(L, -2, 1);
    luaL_setmetatable(L, "oe.World");
    return 1;
}

static int w_gc(lua_State *L) {
    World *w = checkworld(L);
    free(w->bodies);
    free(w->contacts);
    free(w->dyn);
    free(w->statc);
    free(w->cellx);
    free(w->celly);
    free(w->gridHead);
    free(w->gridNext);
    return 0;
}

static Body *addbody(lua_State *L, World *w, int shape, int typeidx) {
    const char *type = luaL_optstring(L, typeidx, "dynamic");
    int dynamic = strcmp(type, "dynamic") == 0;
    if (!dynamic && strcmp(type, "static") != 0)
        luaL_error(L, "body type must be 'dynamic' or 'static', got '%s'", type);
    if (w->n == w->cap) {
        w->cap = w->cap ? w->cap * 2 : 16;
        w->bodies = realloc(w->bodies, sizeof *w->bodies * (size_t)w->cap);
        if (!w->bodies) luaL_error(L, "out of memory");
    }
    Body *b = lua_newuserdatauv(L, sizeof *b, 1);
    memset(b, 0, sizeof *b);
    b->shape = shape;
    b->id = ++w->nextId;
    b->x = (float)luaL_checknumber(L, 2);
    b->y = (float)luaL_checknumber(L, 3);
    b->friction = 0.2f;
    lua_pushvalue(L, 1);
    lua_setiuservalue(L, -2, 1);  // body -> world
    luaL_setmetatable(L, "oe.Body");
    lua_getiuservalue(L, 1, 1);
    lua_pushlightuserdata(L, b);
    lua_pushvalue(L, -3);
    lua_rawset(L, -3);  // world table -> body
    lua_pop(L, 1);
    w->bodies[w->n++] = b;
    b->invMass = dynamic ? 1 : 0;  // set from area by caller
    return b;
}

// world:newCircle(x, y, radius, [type])
static int w_newCircle(lua_State *L) {
    World *w = checkworld(L);
    float r = (float)luaL_checknumber(L, 4);
    luaL_argcheck(L, r > 0, 4, "radius must be positive");
    Body *b = addbody(L, w, CIRCLE, 5);
    b->r = r;
    b->w = b->h = r * 2;
    if (b->invMass) b->invMass = 1 / ((float)M_PI * r * r);
    return 1;
}

// world:newRectangle(x, y, w, h, [type])
static int w_newRectangle(lua_State *L) {
    World *w = checkworld(L);
    float bw = (float)luaL_checknumber(L, 4), bh = (float)luaL_checknumber(L, 5);
    luaL_argcheck(L, bw > 0 && bh > 0, 4, "size must be positive");
    Body *b = addbody(L, w, RECT, 6);
    b->w = bw;
    b->h = bh;
    if (b->invMass) b->invMass = 1 / (bw * bh);
    return 1;
}

static int w_setCallback(lua_State *L) {
    checkworld(L);
    if (!lua_isnil(L, 2)) luaL_checktype(L, 2, LUA_TFUNCTION);
    lua_getiuservalue(L, 1, 1);
    lua_pushvalue(L, 2);
    lua_setfield(L, -2, "callback");
    return 0;
}

// Normal points from a to b. Returns 1 on overlap.
static int collide(Body *a, Body *b, float *nx, float *ny, float *depth) {
    float dx = b->x - a->x, dy = b->y - a->y;
    if (a->shape == CIRCLE && b->shape == CIRCLE) {
        float d = sqrtf(dx * dx + dy * dy), rs = a->r + b->r;
        if (d >= rs) return 0;
        if (d > 0) { *nx = dx / d; *ny = dy / d; } else { *nx = 0; *ny = 1; }
        *depth = rs - d;
        return 1;
    }
    if (a->shape == RECT && b->shape == CIRCLE) {
        if (!collide(b, a, nx, ny, depth)) return 0;
        *nx = -*nx; *ny = -*ny;
        return 1;
    }
    if (a->shape == CIRCLE && b->shape == RECT) {
        // Closest point on the rect to the circle center.
        float px = fmaxf(b->x - b->w / 2, fminf(a->x, b->x + b->w / 2));
        float py = fmaxf(b->y - b->h / 2, fminf(a->y, b->y + b->h / 2));
        float ex = px - a->x, ey = py - a->y, d2 = ex * ex + ey * ey;
        if (d2 > 0) {
            if (d2 >= a->r * a->r) return 0;
            float d = sqrtf(d2);
            *nx = ex / d; *ny = ey / d;
            *depth = a->r - d;
            return 1;
        }
        // Center is inside the rect: fall through and treat the circle as its bounding box.
    }
    float ox = (a->w + b->w) / 2 - fabsf(dx), oy = (a->h + b->h) / 2 - fabsf(dy);
    if (ox <= 0 || oy <= 0) return 0;
    if (ox < oy) { *nx = dx < 0 ? -1 : 1; *ny = 0; *depth = ox; }
    else { *nx = 0; *ny = dy < 0 ? -1 : 1; *depth = oy; }
    return 1;
}

static void addcontact(lua_State *L, World *w, Body *a, Body *b, float nx, float ny) {
    for (int i = 0; i < w->nc; i++)  // one report per pair per update
        if (w->contacts[i].a == a && w->contacts[i].b == b) return;
    if (w->nc == w->ccap) {
        w->ccap = w->ccap ? w->ccap * 2 : 16;
        w->contacts = realloc(w->contacts, sizeof *w->contacts * (size_t)w->ccap);
        if (!w->contacts) luaL_error(L, "out of memory");
    }
    w->contacts[w->nc++] = (Contact){a, b, a->id, b->id, nx, ny};
}

// Resolves one candidate pair. Order of a/b only matters for the contact
// normal's direction, so callers pick it however's convenient for dedup.
static void process_pair(lua_State *L, World *w, Body *a, Body *b) {
    float im = a->invMass + b->invMass, nx, ny, depth;
    if (im == 0 || !collide(a, b, &nx, &ny, &depth)) return;
    addcontact(L, w, a, b, nx, ny);

    // Push apart, split by inverse mass.
    float corr = fmaxf(depth - 0.01f, 0) / im * 0.8f;
    a->x -= nx * corr * a->invMass; a->y -= ny * corr * a->invMass;
    b->x += nx * corr * b->invMass; b->y += ny * corr * b->invMass;

    float rvx = b->vx - a->vx, rvy = b->vy - a->vy, vn = rvx * nx + rvy * ny;
    if (vn > 0) return;  // already separating
    float e = fmaxf(a->restitution, b->restitution);
    float jn = -(1 + e) * vn / im;
    a->vx -= jn * nx * a->invMass; a->vy -= jn * ny * a->invMass;
    b->vx += jn * nx * b->invMass; b->vy += jn * ny * b->invMass;

    // Coulomb friction along the tangent.
    float tx = rvx - vn * nx, ty = rvy - vn * ny, tl = sqrtf(tx * tx + ty * ty);
    if (tl < 1e-6f) return;
    tx /= tl; ty /= tl;
    float mu = sqrtf(a->friction * b->friction);
    float jt = fmaxf(-jn * mu, fminf(jn * mu, -(rvx * tx + rvy * ty) / im));
    a->vx -= jt * tx * a->invMass; a->vy -= jt * ty * a->invMass;
    b->vx += jt * tx * b->invMass; b->vy += jt * ty * b->invMass;
}

static int next_pow2(int n) {
    int p = 16;
    while (p < n) p *= 2;
    return p;
}

static uint32_t cellhash(int32_t cx, int32_t cy, uint32_t cap) {
    uint32_t h = (uint32_t)cx * 92837111u ^ (uint32_t)cy * 689287499u;
    return h & (cap - 1);
}

// Rebuilds the dynamic/static split and the dynamic-body grid from the
// world's current body list. Done once per world:update(), not per substep:
// bodies move little enough within one update that a slightly stale grid
// doesn't miss collisions in practice, and it's much cheaper this way.
static void build_grid(lua_State *L, World *w) {
    w->ndyn = 0;
    w->nstat = 0;
    if (w->dynCap < w->n) {
        w->dynCap = w->n;
        w->dyn = realloc(w->dyn, sizeof *w->dyn * (size_t)w->dynCap);
        w->cellx = realloc(w->cellx, sizeof *w->cellx * (size_t)w->dynCap);
        w->celly = realloc(w->celly, sizeof *w->celly * (size_t)w->dynCap);
        w->gridNext = realloc(w->gridNext, sizeof *w->gridNext * (size_t)w->dynCap);
        if (!w->dyn || !w->cellx || !w->celly || !w->gridNext) luaL_error(L, "out of memory");
    }
    if (w->statCap < w->n) {
        w->statCap = w->n;
        w->statc = realloc(w->statc, sizeof *w->statc * (size_t)w->statCap);
        if (!w->statc) luaL_error(L, "out of memory");
    }

    float maxSize = 0;
    for (int i = 0; i < w->n; i++) {
        Body *b = w->bodies[i];
        if (b->invMass) {
            w->dyn[w->ndyn++] = b;
            maxSize = fmaxf(maxSize, fmaxf(b->w, b->h));
        } else {
            w->statc[w->nstat++] = b;
        }
    }
    w->cellSize = fmaxf(maxSize * 2, 16.0f);  // cells at least ~2x the largest dynamic body

    int cap = next_pow2(w->ndyn * 2);
    if (cap != w->gridCap) {
        w->gridCap = cap;
        w->gridHead = realloc(w->gridHead, sizeof *w->gridHead * (size_t)cap);
        if (!w->gridHead) luaL_error(L, "out of memory");
    }
    for (int i = 0; i < w->gridCap; i++) w->gridHead[i] = -1;
    for (int i = 0; i < w->ndyn; i++) {
        Body *b = w->dyn[i];
        int32_t cx = (int32_t)floorf(b->x / w->cellSize), cy = (int32_t)floorf(b->y / w->cellSize);
        w->cellx[i] = cx;
        w->celly[i] = cy;
        uint32_t h = cellhash(cx, cy, (uint32_t)w->gridCap);
        w->gridNext[i] = w->gridHead[h];
        w->gridHead[h] = i;
    }
}

// Checks dynamic bodies against nearby dynamic bodies only. Each unordered
// pair is visited exactly once: same-cell pairs are deduped by dyn-index,
// and the 4 offsets (not their mirrors) mean each cell-to-neighbor relation
// is only walked from one side.
static void collide_grid(lua_State *L, World *w) {
    static const int off[5][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}, {-1, 1}};
    for (int oi = 0; oi < 5; oi++) {
        for (int i = 0; i < w->ndyn; i++) {
            int32_t ncx = w->cellx[i] + off[oi][0], ncy = w->celly[i] + off[oi][1];
            uint32_t h = cellhash(ncx, ncy, (uint32_t)w->gridCap);
            for (int j = w->gridHead[h]; j != -1; j = w->gridNext[j]) {
                if (w->cellx[j] != ncx || w->celly[j] != ncy) continue;  // hash collision, different cell
                if (oi == 0 && j <= i) continue;  // same cell: only process each pair once
                Body *A = w->dyn[i], *B = w->dyn[j];
                if (A->id < B->id) process_pair(L, w, A, B); else process_pair(L, w, B, A);
            }
        }
    }
}

// Static bodies (walls, floors, platforms) are usually few, so every dynamic
// body just checks against all of them directly rather than gridding them too.
static void collide_static(lua_State *L, World *w) {
    for (int i = 0; i < w->ndyn; i++) {
        for (int j = 0; j < w->nstat; j++) {
            Body *A = w->dyn[i], *B = w->statc[j];
            if (A->id < B->id) process_pair(L, w, A, B); else process_pair(L, w, B, A);
        }
    }
}

static void step(lua_State *L, World *w, float dt) {
    for (int i = 0; i < w->ndyn; i++) {
        Body *b = w->dyn[i];
        b->vx += w->gx * dt;
        b->vy += w->gy * dt;
        b->x += b->vx * dt;
        b->y += b->vy * dt;
    }
    collide_grid(L, w);
    collide_static(L, w);
}

// world:update(dt) -- substeps at <= 1/120s to limit tunneling, then fires callbacks
static int w_update(lua_State *L) {
    World *w = checkworld(L);
    float dt = (float)luaL_checknumber(L, 2);
    if (dt > 0.25f) dt = 0.25f;  // don't explode after a long stall
    int steps = (int)ceilf(dt * 120);
    w->nc = 0;
    build_grid(L, w);
    for (int i = 0; i < steps; i++) step(L, w, dt / (float)steps);

    lua_getiuservalue(L, 1, 1);
    int tbl = lua_gettop(L);
    if (lua_getfield(L, tbl, "callback") != LUA_TFUNCTION) return 0;
    int nc = w->nc;
    // Copy: callbacks may call update again. Lua-owned so an erroring callback doesn't leak it.
    Contact *cs = lua_newuserdatauv(L, sizeof *cs * (size_t)nc, 0);
    if (nc) memcpy(cs, w->contacts, sizeof *cs * (size_t)nc);
    for (int i = 0; i < nc; i++) {
        lua_pushvalue(L, tbl + 1);
        lua_pushlightuserdata(L, cs[i].a);
        lua_rawget(L, tbl);
        lua_pushlightuserdata(L, cs[i].b);
        lua_rawget(L, tbl);
        // Skip pairs where a body was destroyed by an earlier callback.
        Body *a = lua_touserdata(L, -2), *b = lua_touserdata(L, -1);
        if (!a || !b || a->id != cs[i].ida || b->id != cs[i].idb) { lua_pop(L, 3); continue; }
        lua_pushnumber(L, cs[i].nx);
        lua_pushnumber(L, cs[i].ny);
        lua_call(L, 4, 0);
    }
    return 0;
}

// ---------- body ----------

static int b_getPosition(lua_State *L) { Body *b = checkbody(L); lua_pushnumber(L, b->x); lua_pushnumber(L, b->y); return 2; }
static int b_getVelocity(lua_State *L) { Body *b = checkbody(L); lua_pushnumber(L, b->vx); lua_pushnumber(L, b->vy); return 2; }

static int b_setPosition(lua_State *L) {
    Body *b = checkbody(L);
    b->x = (float)luaL_checknumber(L, 2);
    b->y = (float)luaL_checknumber(L, 3);
    return 0;
}

static int b_setVelocity(lua_State *L) {
    Body *b = checkbody(L);
    b->vx = (float)luaL_checknumber(L, 2);
    b->vy = (float)luaL_checknumber(L, 3);
    return 0;
}

static int b_setRestitution(lua_State *L) { checkbody(L)->restitution = (float)luaL_checknumber(L, 2); return 0; }
static int b_setFriction(lua_State *L) { checkbody(L)->friction = (float)luaL_checknumber(L, 2); return 0; }

// Returns radius for circles, width and height for rectangles.
static int b_getSize(lua_State *L) {
    Body *b = checkbody(L);
    if (b->shape == CIRCLE) { lua_pushnumber(L, b->r); return 1; }
    lua_pushnumber(L, b->w);
    lua_pushnumber(L, b->h);
    return 2;
}

static int b_getShape(lua_State *L) { lua_pushstring(L, checkbody(L)->shape == CIRCLE ? "circle" : "rectangle"); return 1; }

static int b_destroy(lua_State *L) {
    Body *b = checkbody(L);
    if (b->dead) return 0;
    b->dead = 1;
    lua_getiuservalue(L, 1, 1);
    World *w = luaL_checkudata(L, -1, "oe.World");
    for (int i = 0; i < w->n; i++) {
        if (w->bodies[i] != b) continue;
        w->bodies[i] = w->bodies[--w->n];
        break;
    }
    lua_getiuservalue(L, -1, 1);
    lua_pushlightuserdata(L, b);
    lua_pushnil(L);
    lua_rawset(L, -3);
    return 0;
}

static void metatable(lua_State *L, const char *name, const luaL_Reg *fns) {
    luaL_newmetatable(L, name);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    luaL_setfuncs(L, fns, 0);
    lua_pop(L, 1);
}

int luaopen_oe_physics(lua_State *L) {
    metatable(L, "oe.World", (luaL_Reg[]){{"__gc", w_gc}, {"newCircle", w_newCircle}, {"newRectangle", w_newRectangle},
                                          {"setCallback", w_setCallback}, {"update", w_update}, {NULL, NULL}});
    metatable(L, "oe.Body", (luaL_Reg[]){{"getPosition", b_getPosition}, {"setPosition", b_setPosition},
                                         {"getVelocity", b_getVelocity}, {"setVelocity", b_setVelocity},
                                         {"setRestitution", b_setRestitution}, {"setFriction", b_setFriction},
                                         {"getSize", b_getSize}, {"getShape", b_getShape}, {"destroy", b_destroy},
                                         {NULL, NULL}});
    lua_newtable(L);
    lua_pushcfunction(L, w_new);
    lua_setfield(L, -2, "newWorld");
    return 1;
}
