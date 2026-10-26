/* Volcano: a night-time island volcano. Lava oozes and erupts from the crater, runs down the
   slopes cell by cell and cools into new rock, so the cone slowly grows. Where a flow reaches
   the sea it hisses into steam and turns to stone. Gray smoke, ash, glowing debris and the
   odd flash of ash lightning fill the sky. When the mountain gets tall it is swallowed by a
   huge eruption and a fresh little island is born. */
#include "../engine.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define TICK (1.0f / 30)
#define MAX_PARTS 700
#define MAX_SMOKE 260

enum { AIR, ROCK, WATER, LAVA, RUBBLE };   /* RUBBLE: freshly cooled, still tumbling; sets into ROCK */
enum { P_BOMB, P_CHUNK, P_EMBER, P_STEAM, P_BUBBLE };

typedef struct { float x, y, vx, vy, life, size; int kind, sunk; } Part;
typedef struct { float x, y, vx, vy, age, life, size, dark; } Smoke;

typedef struct {
    int w, h, gw, gh, u, sub;
    float k;                              /* grid cells per old 1/130th of the screen height */
    uint8_t *m, *heat, *stamp, *shade, *age;   /* material, heat (lava + glowing rock), tick stamp, rock tint */
    float *wtop;                          /* where the sea surface really is, per column (cells, y down) */
    float *wh, *wv;                       /* sea surface height and velocity per column (cells) */
    int *top;                             /* per column: y of the highest rock, or gh */
    int seaY, floorY, ventX, peakMin;
    float acc, t, cycleT, phase, nextErupt, eruptLeft, flash, fade, spill, wind;
    int tick, fadeDir, spillDir;
    Part p[MAX_PARTS];
    Smoke sm[MAX_SMOKE];
    int np, ns;
    uint32_t *px;
    SDL_Texture *tex;
} Volcano;

static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static uint32_t hash2(int x, int y) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
#define AT(s, x, y) ((s)->m[(y) * (s)->gw + (x)])
#define SOLID(m) ((m) == ROCK || (m) == RUBBLE)
static int inside(const Volcano *s, int x, int y) { return x >= 0 && x < s->gw && y >= 0 && y < s->gh; }

/* ---------- world ---------- */

static float swell(const Volcano *s, float x);

/* The sea surface at x: swells and splashes included. Everything that cares whether something
   is "in the water" (grid, steam, bubbles, glow) uses this, not the flat sea level. */
static float sea_top(const Volcano *s, float x) {
    int c = x < 0 ? 0 : x >= s->gw ? s->gw - 1 : (int)x;
    return s->wtop[c];
}

/* Moves the water in the grid to follow the surface, so waves wash over the shore, crests
   douse lava and troughs expose the sea floor. */
static void update_sea(Volcano *s) {
    int band = (int)(12 * s->k);
    for (int x = 0; x < s->gw; x++) {
        s->wtop[x] = (float)s->seaY + s->wh[x] - swell(s, (float)x);
        int yi = (int)floorf(s->wtop[x]), y0 = s->seaY - band, y1 = s->seaY + band;
        if (y0 < 0) y0 = 0;
        if (y1 > s->gh) y1 = s->gh;
        for (int y = y0; y < y1; y++) {
            uint8_t *m = &AT(s, x, y);
            if (y >= yi && *m == AIR) *m = WATER;
            else if (y < yi && *m == WATER) *m = AIR;
        }
    }
}

static void recompute_tops(Volcano *s) {
    for (int x = 0; x < s->gw; x++) {
        int y = 0;
        while (y < s->gh && !SOLID(AT(s, x, y))) y++;
        s->top[x] = y;
    }
}

static void build_world(Volcano *s) {
    int gw = s->gw, gh = s->gh;
    memset(s->m, AIR, (size_t)gw * gh);
    memset(s->heat, 0, (size_t)gw * gh);
    memset(s->age, 0, (size_t)gw * gh);
    for (int i = 0; i < gw * gh; i++) s->shade[i] = (uint8_t)irand(256);
    s->np = s->ns = 0;
    float K = s->k;
    s->seaY = gh * 56 / 100;                          /* sea level */
    s->floorY = gh * 92 / 100;                        /* the sea floor the volcano is born on */
    s->ventX = (int)(gw * frand(0.42f, 0.58f));
    s->peakMin = gh * 17 / 100;                       /* the finale comes when the summit gets this high */
    float ph1 = frand(0, 6), ph2 = frand(0, 6);
    float h0 = gh * 0.07f;                            /* a low seamount to start with */
    float halfBase = gw * 0.20f;
    for (int x = 0; x < gw; x++) {
        float d = fabsf((float)(x - s->ventX)) / halfBase;
        float cone = d < 1 ? h0 * powf(1 - d, 1.15f) : 0;
        float seabed = K * (2.0f * sinf(x * 0.05f / K + ph1) + 1.2f * sinf(x * 0.17f / K + ph2));
        int top = (int)((float)s->floorY - cone + seabed);
        for (int y = top; y < gh; y++) AT(s, x, y) = ROCK;
    }
    /* a crater in the summit */
    for (int x = s->ventX - (int)(2 * K); x <= s->ventX + (int)(2 * K); x++) {
        int y = 0;
        while (y < gh && AT(s, x, y) != ROCK) y++;
        for (int k = 0; k < (int)(2 * K) && y + k < gh; k++) AT(s, x, y + k) = AIR;
    }
    for (int x = 0; x < gw; x++)
        for (int y = s->seaY; y < gh; y++)
            if (AT(s, x, y) == AIR) AT(s, x, y) = WATER;
    update_sea(s);
    recompute_tops(s);
    s->phase = 0;
    s->cycleT = 0;
    s->nextErupt = frand(4, 8);
    s->eruptLeft = 0;
    s->wind = frand(-1, 1) < 0 ? -1.0f : 1.0f;
    s->spillDir = -1;
    s->spill = frand(8, 16);
}

static void *create(SDL_Renderer *r, int w, int h) {
    Volcano *s = calloc(1, sizeof *s);
    s->w = w;
    s->h = h;
    s->u = (h + 269) / 270 > 2 ? (h + 269) / 270 : 2;
    s->gw = w / s->u + 1;
    s->gh = h / s->u + 1;
    s->k = s->gh / 130.0f;
    s->sub = (int)(s->k + 0.5f);
    size_t n = (size_t)s->gw * s->gh;
    s->m = calloc(n, 1);
    s->heat = calloc(n, 1);
    s->stamp = calloc(n, 1);
    s->shade = calloc(n, 1);
    s->age = calloc(n, 1);
    s->wh = calloc((size_t)s->gw, sizeof(float));
    s->wtop = calloc((size_t)s->gw, sizeof(float));
    s->wv = calloc((size_t)s->gw, sizeof(float));
    s->top = calloc((size_t)s->gw, sizeof(int));
    s->px = calloc(n, 4);
    s->tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, s->gw, s->gh);
    if (s->tex) SDL_SetTextureScaleMode(s->tex, SDL_ScaleModeNearest);
    build_world(s);
    s->fade = 1;                                    /* fades in from black */
    s->fadeDir = -1;
    return s;
}

/* ---------- particles ---------- */

static void add_smoke(Volcano *s, float x, float y, float vx, float vy, float size, float dark, float life) {
    if (s->ns >= MAX_SMOKE) return;
    s->sm[s->ns++] = (Smoke){ x, y, vx, vy, 0, life, size, dark };
}

static void add_part(Volcano *s, int kind, float x, float y, float vx, float vy, float life, float size) {
    if (s->np >= MAX_PARTS) return;
    s->p[s->np++] = (Part){ x, y, vx, vy, life, size, kind, 0 };
}

static int surface_y(const Volcano *s, int x) {
    return s->top[x < 0 ? 0 : x >= s->gw ? s->gw - 1 : x];
}

/* Kicks the sea surface down at x (a splash); a few columns either side get a share. */
static void splash(Volcano *s, float x, float amount) {
    int cx = (int)x, r = (int)(2 * s->k) + 1;
    for (int dx = -r; dx <= r; dx++)
        if (cx + dx >= 0 && cx + dx < s->gw) s->wv[cx + dx] += amount * (1.0f - fabsf((float)dx) / (r + 1));
}

static void erupt_burst(Volcano *s, float power) {
    if (surface_y(s, s->ventX) > sea_top(s, (float)s->ventX)) {   /* under water: a burst of bubbles instead of bombs */
        float K = s->k;
        for (int i = 0; i < (int)(10 * power) + 3; i++)
            add_part(s, P_BUBBLE, s->ventX + frand(-3, 3) * K, (float)surface_y(s, s->ventX) - 2 * K, frand(-6, 6) * K,
                     -frand(14, 30) * K, 60, frand(0.8f, 1.8f) * K);
        return;
    }
    float K = s->k, vx0 = s->ventX, vy0 = (float)surface_y(s, s->ventX) - 2 * K;
    int n = (int)(5 * power) + 1;
    for (int i = 0; i < n; i++) {
        float a = frand(-0.6f, 0.6f);
        float v = frand(34, 62) * K * (0.6f + 0.5f * power);
        int chunk = irand(3) == 0;
        add_part(s, chunk ? P_CHUNK : P_BOMB, vx0 + frand(-K, K), vy0, sinf(a) * v, -cosf(a) * v,
                 frand(5, 9), K * (chunk ? frand(2, 4.5f) : frand(1.2f, 2.4f)));
    }
}

/* ---------- lava cellular automaton ---------- */

/* Moves the cell at (x, y) to (nx, ny), which must be air; returns its new index. */
static int move_cell(Volcano *s, int x, int y, int nx, int ny) {
    int i = y * s->gw + x, j = ny * s->gw + nx;
    s->m[j] = s->m[i];
    s->heat[j] = s->heat[i];
    s->stamp[j] = (uint8_t)s->tick;
    s->m[i] = AIR;
    s->heat[i] = 0;
    return j;
}

static int touches_water(const Volcano *s, int x, int y) {
    static const int dx[4] = { 1, -1, 0, 0 }, dy[4] = { 0, 0, 1, -1 };
    for (int k = 0; k < 4; k++)
        if (inside(s, x + dx[k], y + dy[k]) && AT(s, x + dx[k], y + dy[k]) == WATER) return 1;
    return 0;
}

static void solidify(Volcano *s, int x, int y, int glow) {
    int i = y * s->gw + x;
    s->m[i] = RUBBLE;
    s->heat[i] = (uint8_t)glow;
    s->age[i] = 0;
    if (y < s->top[x]) s->top[x] = y;
}

static void step_lava_cell(Volcano *s, int x, int y) {
    int i = y * s->gw + x;
    if (touches_water(s, x, y)) {
        solidify(s, x, y, 40);
        if (y > sea_top(s, (float)x) + 2 * s->k) {
            if (irand(100) < 12)
                add_part(s, P_BUBBLE, (float)x, (float)y - 1, frand(-3, 3) * s->k, -frand(14, 26) * s->k, 60, frand(0.7f, 1.5f) * s->k);
        } else if (irand(100) < 70) {
            if (irand(100) < 25) splash(s, (float)x, 0.5f);
            add_part(s, P_STEAM, (x + frand(0, 1)), (float)y - 1, frand(-4, 4) * s->k, frand(-14, -7) * s->k, frand(1.5f, 3), frand(1.5f, 3) * s->k);
        }
        return;
    }
    int below = y + 1 < s->gh ? AT(s, x, y + 1) : ROCK;
    int at = i, moved = 0;
    if (below == AIR) {
        if (irand(100) < 92) { at = move_cell(s, x, y, x, y + 1); moved = 1; }
    } else {
        int d = irand(100) < 85 ? s->spillDir : -s->spillDir;
        for (int k = 0; k < 2 && !moved; k++, d = -d) {
            int nx = x + d;
            if (inside(s, nx, y + 1) && AT(s, nx, y) == AIR && AT(s, nx, y + 1) == AIR && irand(100) < 55) {
                at = move_cell(s, x, y, nx, y + 1);
                moved = 1;
            }
        }
        /* a flat step: crawl sideways toward an edge a few cells away */
        for (int k = 0; k < 2 && !moved; k++, d = -d) {
            for (int dist = 1; dist <= (int)(4 * s->k); dist++) {
                int nx = x + d * dist;
                if (!inside(s, nx, y) || AT(s, nx, y) != AIR) break;
                if (inside(s, nx, y + 1) && AT(s, nx, y + 1) == AIR) {
                    if (irand(100) < 60) { at = move_cell(s, x, y, x + d, y); moved = 1; }
                    break;
                }
            }
        }
    }
    int hv = s->heat[at] - (irand(1000) < (int)(700 / s->k) ? (moved ? (s->tick % 3 == 0) : 1) : 0);
    if (hv <= 80 && !moved) solidify(s, at % s->gw, at / s->gw, 100);
    else s->heat[at] = (uint8_t)(hv < 80 ? 80 : hv);
}

/* Loose rock falls (slowly through water, which it pushes aside), tumbles down to a steep
   pile and, once it has sat still a moment, sets into solid rock. */
static void step_rubble(Volcano *s, int x, int y) {
    int i = y * s->gw + x;
    int below = y + 1 < s->gh ? AT(s, x, y + 1) : ROCK;
    int nx = x, ny = y;
    if (below == AIR || (below == WATER && irand(100) < 80)) {
        ny = y + 1;
    } else if (below != AIR && below != WATER) {
        int d = irand(2) ? 1 : -1;
        for (int k = 0; k < 2 && ny == y; k++, d = -d) {
            int cx = x + d;
            if (!inside(s, cx, y + 1)) continue;
            int side = AT(s, cx, y), diag = AT(s, cx, y + 1);
            if ((side == AIR || side == WATER) && (diag == AIR || (diag == WATER && irand(100) < 75)) && irand(100) < 80) {
                nx = cx;
                ny = y + 1;
            }
        }
    }
    if (nx == x && ny == y) {
        if (s->age[i] >= 24) s->m[i] = ROCK;
        else s->age[i]++;
        return;
    }
    int j = ny * s->gw + nx, displaced = s->m[j];
    s->m[j] = RUBBLE;
    s->heat[j] = s->heat[i];
    s->age[j] = 0;
    s->stamp[j] = (uint8_t)s->tick;
    s->m[i] = displaced == WATER && y >= sea_top(s, (float)x) ? WATER : AIR;   /* pushed-aside water takes its place */
    s->heat[i] = 0;
    s->age[i] = 0;
}

static void emit_lava(Volcano *s, int n) {
    for (int k = 0; k < n; k++) {
        int x = s->ventX + irand((int)(5 * s->k)) - (int)(2 * s->k);
        int y = surface_y(s, x) - 1;
        if (y < 1) continue;
        while (y > 0 && AT(s, x, y) != AIR && AT(s, x, y) != WATER) y--;
        AT(s, x, y) = LAVA;
        s->heat[y * s->gw + x] = 255;
        s->stamp[y * s->gw + x] = (uint8_t)s->tick;
    }
}

/* ---------- step ---------- */

/* Packs a little heap of rock around a spot that debris came to rest on. */
static void blob(Volcano *s, int x, int y, int rad, int glow) {
    for (int dy = 0; dy <= rad; dy++)
        for (int dx = -rad; dx <= rad; dx++) {
            int nx = x + dx, ny = y - dy;
            if (!inside(s, nx, ny) || dx * dx + dy * dy > rad * rad + 1) continue;
            int m = AT(s, nx, ny), under = ny + 1 < s->gh ? AT(s, nx, ny + 1) : ROCK;
            if ((m == AIR || m == WATER) && under != AIR && under != WATER) solidify(s, nx, ny, glow);
        }
}

static void land(Volcano *s, Part *p) {
    int x = (int)p->x, y = (int)p->y;
    if (!inside(s, x, 0)) return;
    if (y < 0) y = 0;
    while (y < s->gh && AT(s, x, y) == AIR) y++;
    if (y >= s->gh) y = s->gh - 1;
    int rad = (int)(p->size * (p->kind == P_CHUNK ? 0.45f : 0.5f));
    blob(s, x, y - 1, rad < 1 ? 1 : rad, p->kind == P_CHUNK ? 120 : 210);
}

/* One pass of the lava automaton, bottom to top, alternating sweep direction. */
static void ca_pass(Volcano *s) {
    s->tick++;
    for (int y = s->gh - 2; y >= 0; y--) {
        int x0 = (s->tick & 1) ? 0 : s->gw - 1, dx = (s->tick & 1) ? 1 : -1;
        for (int k = 0, x = x0; k < s->gw; k++, x += dx) {
            int i = y * s->gw + x;
            if (s->m[i] == LAVA && s->stamp[i] != (uint8_t)s->tick) step_lava_cell(s, x, y);
            else if (s->m[i] == RUBBLE && s->stamp[i] != (uint8_t)s->tick) step_rubble(s, x, y);
            if (SOLID(s->m[i]) && s->heat[i] && (s->tick & 1) == 0) s->heat[i]--;
        }
    }
    /* any lava that fell off the world floor stops being lava */
    for (int x = 0; x < s->gw; x++)
        if (AT(s, x, s->gh - 1) == LAVA) AT(s, x, s->gh - 1) = ROCK;
}

/* Loose rock slumps down steep spots, so a tall narrow spike spreads into a wider cone. */
static void slump(Volcano *s) {
    for (int n = 0; n < s->gw / 3; n++) {
        int x = irand(s->gw), d = irand(2) ? 1 : -1, nx = x + d;
        if (nx < 0 || nx >= s->gw || s->top[x] >= s->gh - 2) continue;
        int fall = s->top[nx] - s->top[x];
        if (fall < 2 || !SOLID(AT(s, x, s->top[x])) || (AT(s, nx, s->top[nx] - 1) != AIR && AT(s, nx, s->top[nx] - 1) != WATER)) continue;
        int from = s->top[x] * s->gw + x, to = (s->top[nx] - 1) * s->gw + nx;
        s->m[to] = RUBBLE;
        s->age[to] = 0;
        s->heat[to] = s->heat[from];
        s->m[from] = s->top[x] >= sea_top(s, (float)x) ? WATER : AIR;
        s->heat[from] = 0;
        s->top[nx]--;
        int y = s->top[x] + 1;
        while (y < s->gh && !SOLID(AT(s, x, y))) y++;
        s->top[x] = y;
    }
}

static void tick_world(Volcano *s) {
    float K = s->k;
    s->t += TICK;
    s->cycleT += TICK;
    for (int x = 0; x < s->gw; x++) {              /* a row of springs: the sea surface */
        float l = s->wh[x > 0 ? x - 1 : x], r2 = s->wh[x + 1 < s->gw ? x + 1 : x];
        s->wv[x] += 0.2f * ((l + r2) * 0.5f - s->wh[x]) - 0.02f * s->wh[x];
        s->wv[x] *= 0.985f;
    }
    for (int x = 0; x < s->gw; x++) s->wh[x] = clampf(s->wh[x] + s->wv[x], -4 * K, 4 * K);
    update_sea(s);
    s->wind += frand(-0.4f, 0.4f) * TICK;
    s->wind = clampf(s->wind, -1.5f, 1.5f);

    /* eruption schedule: calm ooze, then a loud eruption every so often */
    int peak = s->gh;
    for (int x = s->ventX - (int)(6 * K); x <= s->ventX + (int)(6 * K); x++) peak = s->top[x] < peak ? s->top[x] : peak;
    int tall = peak <= s->peakMin;
    s->phase = clampf((float)(s->floorY - peak) / (s->floorY - s->peakMin), 0, 1);   /* progress to the finale */
    if (s->eruptLeft > 0) {
        s->eruptLeft -= TICK;
    } else {
        s->nextErupt -= TICK;
        if (s->nextErupt <= 0 && s->fadeDir == 0) {
            s->eruptLeft = tall ? 7 : frand(5, 9);
            s->nextErupt = frand(16, 30);
        }
    }
    s->spill -= TICK;
    if (s->spill <= 0) {                           /* the main flow switches to another side now and then */
        s->spillDir = irand(100) < 55 ? -1 : 1;
        s->spill = frand(8, 18);
    }
    int erupting = s->eruptLeft > 0;
    if (erupting && surface_y(s, s->ventX) > sea_top(s, (float)s->ventX) && irand(100) < 40) splash(s, s->ventX + frand(-4, 4) * K, -0.6f);
    float power = erupting ? clampf(s->eruptLeft / 2.0f, 0.4f, 1.0f) : 0;

    if (s->fadeDir == 0) {
        emit_lava(s, (int)((erupting ? 3.6f : 2.0f) * (surface_y(s, s->ventX) > sea_top(s, (float)s->ventX) ? 0.5f : 1.0f) * K + frand(0, 1)));
        if (erupting && irand(100) < 30) erupt_burst(s, power);
        if (!erupting && irand(1000) < 6) erupt_burst(s, 0.3f);
    }

    /* the vent clogs with rock now and then: keep it open */
    for (int x = s->ventX - (int)K; x <= s->ventX + (int)K; x++)
        if (SOLID(AT(s, x, s->top[x])) && irand(1000) < 6 && s->top[x] + 1 < s->gh) {
            AT(s, x, s->top[x]) = s->top[x] >= sea_top(s, (float)x) ? WATER : AIR;
            s->top[x]++;
        }

    for (int k = 0; k < s->sub; k++) ca_pass(s);
    slump(s);

    /* keep the sea level: water that rubble pushed aside (or that rock displaced) fills back in */
    for (int n = 0; n < s->gw / 4; n++) {
        int x = irand(s->gw);
        for (int y = s->seaY; y < s->gh; y++) {
            int m = AT(s, x, y);
            if (m == AIR) {
                int fed = (y + 1 < s->gh && AT(s, x, y + 1) == WATER) || (x > 0 && AT(s, x - 1, y) == WATER) ||
                          (x + 1 < s->gw && AT(s, x + 1, y) == WATER);
                if (fed) AT(s, x, y) = WATER;
                break;
            }
            if (m != WATER) break;
        }
    }
    recompute_tops(s);

    /* smoke from the crater, or bubbles and surface steam while the vent is under water */
    if (s->fadeDir == 0) {
        float vx = s->ventX, vy = (float)surface_y(s, s->ventX) - 2 * K;
        if (vy > sea_top(s, vx)) {
            if (irand(100) < (erupting ? 70 : 25))
                add_part(s, P_BUBBLE, vx + frand(-2, 2) * K, vy, frand(-4, 4) * K, -frand(14, 28) * K, 60, frand(0.7f, 1.6f) * K);
            if (vy - sea_top(s, vx) < 40 * K && irand(100) < (erupting ? 70 : 30))
                add_part(s, P_STEAM, vx + frand(-3, 3) * K, sea_top(s, vx) - 1, frand(-5, 5) * K, -frand(8, 16) * K,
                         frand(2, 3.5f), frand(2, 4) * K);
        } else if (irand(100) < (erupting ? 90 : 22)) {
            add_smoke(s, vx + frand(-2, 2) * K, vy, (frand(-3, 3) + s->wind * 4) * K, -frand(erupting ? 22 : 9, erupting ? 40 : 15) * K,
                      K * (erupting ? frand(3, 6) : frand(1.5f, 3)), erupting ? frand(0.6f, 1) : frand(0.2f, 0.5f),
                      erupting ? frand(6, 11) : frand(5, 8));
        }
    }
    /* distant lightning in the ash cloud */
    if (erupting && s->flash <= 0 && irand(1000) < 7) s->flash = 0.5f;
    if (s->flash > 0) s->flash -= TICK;

    /* particles */
    for (int i = s->np - 1; i >= 0; i--) {
        Part *p = &s->p[i];
        p->life -= TICK;
        if (p->kind == P_BUBBLE) {
            p->x += (p->vx + sinf(p->y * 0.4f / K + p->life) * 5 * K) * TICK;
            p->y += p->vy * TICK;
            if (p->y <= sea_top(s, p->x) || p->life <= 0 || p->x < 0 || p->x >= s->gw || AT(s, (int)p->x, (int)p->y) != WATER) {
                if (p->y <= sea_top(s, p->x) + K && p->life > 0 && irand(100) < 40)
                    add_part(s, P_STEAM, p->x, sea_top(s, p->x) - 1, frand(-3, 3) * K, -frand(6, 12) * K, frand(1, 2), frand(1.5f, 2.5f) * K);
                s->p[i] = s->p[--s->np];
            }
            continue;
        }
        if (p->kind == P_STEAM) {
            p->vy -= 4 * K * TICK;
            p->x += p->vx * TICK;
            p->y += p->vy * TICK;
            p->vx += s->wind * 3 * K * TICK;
            if (p->life <= 0) s->p[i] = s->p[--s->np];
            continue;
        }
        p->vy += 55 * K * TICK;
        p->x += p->vx * TICK;
        p->y += p->vy * TICK;
        int gone = p->x < 0 || p->x >= s->gw || p->y >= s->gh;
        int mat = !gone && p->y >= 0 ? AT(s, (int)p->x, (int)p->y) : AIR;
        if (mat == WATER) {                       /* splash, then sink to the sea floor and turn to stone */
            if (!p->sunk) {
                p->sunk = 1;
                p->life = 30;
                splash(s, p->x, p->size * 0.35f);
                for (int k = 0; k < 3; k++)
                    add_part(s, P_STEAM, p->x, sea_top(s, p->x) - 1, frand(-5, 5) * K, frand(-16, -7) * K, frand(1.5f, 3), frand(1.5f, 3) * K);
            }
            p->vx *= 0.92f;
            if (p->vy > 14 * K) p->vy = 14 * K;
            int below = (int)p->y + 1 < s->gh ? AT(s, (int)p->x, (int)p->y + 1) : ROCK;
            if (below != WATER) {
                blob(s, (int)p->x + irand(3) - 1, (int)p->y, (int)(p->size * 0.6f) + 1, 0);
                gone = 1;
            }
        } else if (!p->sunk && irand(100) < 35) {
            add_smoke(s, p->x, p->y, frand(-2, 2) * K, frand(-6, -1) * K, p->size * 0.6f + 0.8f * K, 0.4f, frand(1.5f, 3));
        }
        if (mat != AIR && mat != WATER && p->vy > 0) {
            land(s, p);
            gone = 1;
        }
        if (p->life <= 0) gone = 1;
        if (gone) s->p[i] = s->p[--s->np];
    }
    for (int i = s->ns - 1; i >= 0; i--) {
        Smoke *m = &s->sm[i];
        m->age += TICK;
        m->x += (m->vx + s->wind * 5 * K * (m->age / m->life)) * TICK;
        m->y += m->vy * TICK;
        m->vy *= 1 - 0.55f * TICK;
        if (m->age >= m->life) s->sm[i] = s->sm[--s->ns];
    }

    /* the island is complete: one huge eruption, then a new island */
    if (s->fadeDir == 0 && tall && s->eruptLeft <= 0) {
        s->fadeDir = 1;
        for (int k = 0; k < 40; k++) erupt_burst(s, 1.2f);
    }
    if (s->fadeDir > 0) {
        s->fade += TICK * 0.55f;
        if (irand(100) < 80) erupt_burst(s, 1.0f);
        if (s->fade >= 1) {
            build_world(s);
            s->fadeDir = -1;
        }
    } else if (s->fadeDir < 0) {
        s->fade -= TICK * 0.8f;
        if (s->fade <= 0) {
            s->fade = 0;
            s->fadeDir = 0;
        }
    }
}

static void step(void *p, float dt) {
    Volcano *s = p;
    s->acc += dt;
    int n = 0;
    while (s->acc >= TICK && n++ < 4) {
        tick_world(s);
        s->acc -= TICK;
    }
    if (s->acc > TICK) s->acc = 0;
}

/* ---------- drawing ---------- */

static uint32_t pack(float r, float g, float b) {
    r = clampf(r, 0, 255);
    g = clampf(g, 0, 255);
    b = clampf(b, 0, 255);
    return ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8) | 255u;
}

static uint32_t lava_color(int heat) {
    float t = clampf((heat - 60) / 195.0f, 0, 1);           /* 0 = dull red, 1 = white-yellow */
    return pack(190 + 65 * t, 40 + 190 * t * t, 10 + 90 * t * t * t);
}

/* Height (in cells, up is positive) of the long ocean swells at column x: two slow wave trains
   rolling in from the left, with crests sharper than troughs, rising and falling in sets, and
   a little chop on top. */
static float swell(const Volcano *s, float x) {
    float K = s->k, L1 = s->gw * 0.30f, L2 = s->gw * 0.52f, t = s->t;
    float th1 = 6.2832f * (x / L1 - t / 7.5f), th2 = 6.2832f * (x / L2 - t / 12.0f) + 1.7f;
    float set = 0.65f + 0.35f * sinf(6.2832f * (x / (s->gw * 1.4f) - t / 40.0f));
    float w1 = sinf(th1) - 0.25f * cosf(2 * th1), w2 = sinf(th2) - 0.2f * cosf(2 * th2);
    float chop = 0.25f * sinf(x * 0.21f / K - t * 2.3f) + 0.15f * sinf(x * 0.47f / K + t * 3.1f);
    return K * (set * (3.4f * w1 + 2.2f * w2) + chop);
}

static void paint(Volcano *s) {
    int gw = s->gw, gh = s->gh;
    float glow = clampf(0.25f + (s->eruptLeft > 0 ? 0.5f : 0.0f) + (s->flash > 0 ? 0.3f : 0.0f), 0, 1);
    float vxn = (float)s->ventX / gw, K = s->k;
    float *colLit = malloc((size_t)gw * sizeof(float));
    for (int x = 0; x < gw; x++) {
        float dx = (float)x / gw - vxn;
        colLit[x] = 0.5f * glow * expf(-dx * dx * 30);
    }
    int *wsurf = malloc((size_t)gw * sizeof(int));
    float *sinx = malloc((size_t)gw * sizeof(float)), *siny = malloc((size_t)gh * sizeof(float));
    for (int x = 0; x < gw; x++) {
        wsurf[x] = (int)floorf(s->wtop[x]);
        sinx[x] = sinf(x * 0.045f / K + s->t * 0.5f);
    }
    for (int x = 0; x < gw; x++) {
    }
    for (int y = 0; y < gh; y++) siny[y] = sinf(y * 0.08f / K - s->t * 0.35f);
    float *colW = malloc((size_t)gw * sizeof(float)), *rowW = malloc((size_t)gh * sizeof(float));
    float ventSurf = (float)s->top[s->ventX < 0 ? 0 : s->ventX >= gw ? gw - 1 : s->ventX];
    float wglow = (ventSurf > sea_top(s, (float)s->ventX) ? 0.9f : 0.3f) * (0.5f + glow);
    for (int x = 0; x < gw; x++) {
        float dxc = (float)(x - s->ventX) / (28 * K);
        colW[x] = wglow * expf(-dxc * dxc);
    }
    for (int y = 0; y < gh; y++) {
        float dyc = ((float)y - ventSurf) / (26 * K);
        rowW[y] = expf(-dyc * dyc);   /* water pixels only ever read this, so it needs no cut-off at sea level */
    }
    for (int y = 0; y < gh; y++) {
        for (int x = 0; x < gw; x++) {
            int i = y * gw + x;
            uint32_t c;
            int m = s->m[i];
            if (m == ROCK || m == RUBBLE || m == LAVA) {
                if (m == LAVA) {
                    c = lava_color(s->heat[i]);
                } else {
                    int sh = s->shade[i];
                    float v = 40 + (sh & 15) * 1.3f;
                    float depth = y > s->top[x] + (int)(3 * K) ? 0.78f : 1.0f;
                    if (y > s->seaY) depth *= 1 - 0.4f * clampf((float)(y - s->seaY) / (0.34f * gh), 0, 1);
                    /* rock facing the crater glows a little */
                    float lit = colLit[x] * (y < s->top[x] + (int)(3 * K) ? 1.0f : 0.4f);
                    c = pack(v * depth * 1.1f + 70 * lit, v * depth * 0.95f + 24 * lit, v * depth * 0.9f + 8 * lit);
                    if (s->heat[i]) {
                        float h = s->heat[i] / 120.0f;
                        c = pack(v + 190 * h, v * 0.8f + 70 * h, v * 0.7f);
                    }
                }
            } else if (m == WATER) {
                float depth = clampf((float)(y - s->seaY) / (0.34f * gh), 0, 1);
                float wave = sinx[x] * 0.5f + 0.5f, caustic = sinx[x] * siny[y] * 0.5f + 0.5f;
                float shallow = 1 - depth;
                float r = 4 + 10 * shallow, g = 14 + 40 * shallow + 10 * caustic * shallow, b = 40 + 60 * shallow + 18 * caustic * shallow;
                if (y == wsurf[x]) {                         /* bright surface line, whiter where the water is steep */
                    float steep = fabsf((float)(wsurf[x + 1 < gw ? x + 1 : x] - wsurf[x > 0 ? x - 1 : x]));
                    float f = clampf(0.4f + steep * 0.3f, 0, 1);
                    r += 130 * f; g += 150 * f; b += 150 * f;
                } else if (y <= wsurf[x] + (int)K) { r += 25; g += 38; b += 50; }
                /* lava light in the water near the vent, and its glints on the surface */
                float vg = colW[x] * rowW[y];
                float dx = (float)x / gw - vxn;
                float refl = glow * expf(-dx * dx * 12) * wave * clampf(1.0f - (float)(y - wsurf[x]) / (6 * K), 0, 1);
                c = pack(r + 150 * vg + 130 * refl, g + 55 * vg + 40 * refl, b + 10 * vg);
            } else {
                /* night sky: deep navy up top, dusky red-orange glow near the horizon */
                float hor = clampf((float)y / s->seaY, 0, 1);
                float dx = (float)x / gw - vxn;
                float aura = expf(-dx * dx * 6) * glow * (0.3f + 0.7f * hor) * 0.5f;
                float r = 6 + 24 * hor * hor + 120 * aura, g = 6 + 12 * hor * hor + 36 * aura, b = 22 + 30 * hor - 10 * aura;
                if (s->flash > 0) {
                    float f = clampf(s->flash * 1.4f, 0, 1) * (1 - fabsf((float)(x % 11) - 5) * 0.03f);
                    r += 70 * f;
                    g += 60 * f;
                    b += 90 * f;
                }
                /* a few stars, where the smoke doesn't hide them */
                uint32_t h = hash2(x, y);
                if (y < s->seaY * 0.8f && (h % (int)(997 * K * K)) == 0) {
                    float tw = 0.6f + 0.4f * sinf(s->t * 2 + (float)(h % 7));
                    r += 140 * tw; g += 140 * tw; b += 150 * tw;
                }
                c = pack(r, g, b);
            }
            s->px[i] = c;
        }
    }
    free(colLit);
    free(colW);
    free(wsurf);
    free(sinx);
    free(siny);
    free(rowW);
}

static void fillf(SDL_Renderer *r, float x, float y, float w, float h, int u) {
    SDL_Rect d = { (int)floorf(x * u), (int)floorf(y * u), (int)ceilf(w * u), (int)ceilf(h * u) };
    SDL_RenderFillRect(r, &d);
}

static void draw(void *p, SDL_Renderer *r) {
    Volcano *s = p;
    int u = s->u;
    paint(s);
    SDL_UpdateTexture(s->tex, NULL, s->px, s->gw * 4);
    SDL_Rect dst = { 0, 0, s->gw * u, s->gh * u };
    SDL_RenderCopy(r, s->tex, NULL, &dst);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

    /* smoke, oldest (biggest) first underneath; chunky square puffs */
    for (int i = 0; i < s->ns; i++) {
        const Smoke *m = &s->sm[i];
        float t = m->age / m->life;
        float sz = m->size * (0.7f + 1.6f * t);
        float a = (1 - t) * (t < 0.1f ? t * 10 : 1) * 0.7f;
        float g = 60 + 90 * (1 - m->dark) + 30 * (1 - t);
        float warm = (1 - t) * (m->y > s->seaY - 70 * s->k ? 1 : 0) * 0.6f;
        SDL_SetRenderDrawColor(r, (Uint8)clampf(g + 90 * warm, 0, 255), (Uint8)clampf(g + 30 * warm, 0, 255),
                               (Uint8)clampf(g * 1.02f, 0, 255), (Uint8)(a * 190));
        fillf(r, m->x - sz * 0.5f, m->y - sz * 0.5f, sz, sz, u);
    }
    /* steam */
    for (int i = 0; i < s->np; i++) {
        const Part *q = &s->p[i];
        if (q->kind == P_BUBBLE) {
            SDL_SetRenderDrawColor(r, 190, 225, 255, 150);
            fillf(r, q->x - q->size * 0.5f, q->y - q->size * 0.5f, q->size, q->size, u);
            continue;
        }
        if (q->kind != P_STEAM) continue;
        float a = clampf(q->life / 2.0f, 0, 1);
        SDL_SetRenderDrawColor(r, 225, 235, 245, (Uint8)(a * 150));
        fillf(r, q->x - q->size * 0.5f, q->y - q->size * 0.5f, q->size, q->size, u);
    }
    /* flying lava bombs and rock chunks, with a glowing core */
    for (int i = 0; i < s->np; i++) {
        const Part *q = &s->p[i];
        if (q->kind == P_STEAM || q->kind == P_BUBBLE) continue;
        float sz = q->size;
        if (q->sunk) {
            SDL_SetRenderDrawColor(r, 70, 60, 62, 200);
            fillf(r, q->x - sz * 0.4f, q->y - sz * 0.4f, sz * 0.8f, sz * 0.8f, u);
        } else if (q->kind == P_CHUNK) {
            SDL_SetRenderDrawColor(r, 62, 52, 50, 255);
            fillf(r, q->x - sz * 0.5f, q->y - sz * 0.5f, sz, sz, u);
            SDL_SetRenderDrawColor(r, 255, 120, 30, 255);
            fillf(r, q->x - sz * 0.5f, q->y - sz * 0.5f, sz * 0.45f, sz * 0.45f, u);
        } else {
            SDL_SetRenderDrawColor(r, 255, 90, 20, 140);
            fillf(r, q->x - sz, q->y - sz, sz * 2, sz * 2, u);
            SDL_SetRenderDrawColor(r, 255, 220, 120, 255);
            fillf(r, q->x - sz * 0.5f, q->y - sz * 0.5f, sz, sz, u);
        }
    }
    if (g_show_info) {
        char line[96];
        int sc = s->h / 360 > 2 ? s->h / 360 : 2, x0 = 12, y0 = 12, step = 9 * sc;
        SDL_Color white = { 255, 255, 255, 255 };
        int submerged = s->top[s->ventX] > sea_top(s, (float)s->ventX);
        snprintf(line, sizeof line, "%s", submerged ? "UNDERSEA VOLCANO" : "ISLAND VOLCANO");
        draw_text_shadow(r, line, x0, y0, sc, white);
        snprintf(line, sizeof line, "GROWTH %d%%  (FINALE AT 100%%)", (int)(s->phase * 100));
        draw_text_shadow(r, line, x0, y0 + step, sc, white);
        if (s->eruptLeft > 0) snprintf(line, sizeof line, "ERUPTING  %.0fS LEFT", s->eruptLeft);
        else snprintf(line, sizeof line, "NEXT ERUPTION IN %.0fS", s->nextErupt > 0 ? s->nextErupt : 0.0f);
        draw_text_shadow(r, line, x0, y0 + 2 * step, sc, white);
        snprintf(line, sizeof line, "TIME %d:%02d", (int)s->cycleT / 60, (int)s->cycleT % 60);
        draw_text_shadow(r, line, x0, y0 + 3 * step, sc, white);
        draw_text_shadow(r, "ENDS: SUMMIT REACHES 100%, THEN A NEW SEAMOUNT", x0, y0 + 4 * step, sc, white);
    }
    if (s->fade > 0) {
        SDL_SetRenderDrawColor(r, 0, 0, 0, (Uint8)(clampf(s->fade, 0, 1) * 255));
        SDL_RenderFillRect(r, NULL);
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

static void destroy(void *p) {
    Volcano *s = p;
    if (s->tex) SDL_DestroyTexture(s->tex);
    free(s->m);
    free(s->heat);
    free(s->stamp);
    free(s->shade);
    free(s->age);
    free(s->wh);
    free(s->wtop);
    free(s->wv);
    free(s->top);
    free(s->px);
    free(s);
}

const Saver saver_volcano = { "volcano", "VOLCANO", create, step, draw, destroy };
