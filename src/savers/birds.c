/* Birds: a sunny summer day. Pixel-art songbirds and a far-off seagull flap and glide across
   a blue sky at different depths, sometimes in a little flock. Clouds drift, flowers sway on
   rolling hills, a butterfly visits the flowers, and now and then a bird lands for a rest. */
#include "../engine.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define KINDS 4                 /* songbirds; the gull is one more */
#define GULL KINDS
#define FLAP 4                  /* wing frames; index FLAP is the sitting pose */
#define BIRD_W 21
#define BIRD_H 20
#define GULL_W 36
#define GULL_H 26
#define FLY_W 11
#define FLY_H 9
#define SUN_D 25
#define RAYS_D 61
#define MAX_BIRDS 32
#define CLOUD_SHAPES 7
#define MAX_CLOUDS 16
#define MAX_PLANTS 120
#define MAX_PERCHES 8
#define MAX_FLIES 2
#define FLOWER_KINDS 5
#define SUNFLOWER FLOWER_KINDS
#define SUN_H 38
#define RGBA(hex, a) (((uint32_t)(hex) << 8) | (uint32_t)(a))

static const float BAYER[4][4] = {
    { 0.0f / 16, 8.0f / 16, 2.0f / 16, 10.0f / 16 }, { 12.0f / 16, 4.0f / 16, 14.0f / 16, 6.0f / 16 },
    { 3.0f / 16, 11.0f / 16, 1.0f / 16, 9.0f / 16 }, { 15.0f / 16, 7.0f / 16, 13.0f / 16, 5.0f / 16 },
};

/* Mixes two 0xRRGGBB colours and posterizes with an ordered dither, for chunky gradients. */
static uint32_t dither_mix(uint32_t a, uint32_t b, float t, int x, int y, float step) {
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    uint32_t out = 0;
    for (int sh = 16; sh >= 0; sh -= 8) {
        float ca = (float)((a >> sh) & 255), cb = (float)((b >> sh) & 255);
        float v = ca + (cb - ca) * t;
        v = floorf(v / step + BAYER[y & 3][x & 3]) * step;
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        out |= (uint32_t)v << sh;
    }
    return rgb(out);
}

static void recolor(Canvas *c, int x0, int y0, int x1, int y1, uint32_t under, uint32_t col) {
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            if (get(c, x, y) == under) put(c, x, y, col);
}

/* ---------- birds ---------- */

typedef struct { uint32_t back, wing, tip, light, breast, belly, beak, line; } Plumage;

static const Plumage PLUMAGE[KINDS] = {
    { 0x3F86E8, 0x2D66C4, 0x1C3C88, 0x7FB4F5, 0xF39A4B, 0xFFF1DE, 0x3A3A44, 0x14214C },   /* bluebird */
    { 0xE3342F, 0xB8221F, 0x7E1414, 0xF0675C, 0xEE4A3E, 0xF26656, 0xFF9F2E, 0x480C0C },   /* cardinal */
    { 0xFFD52E, 0x2E2E2E, 0x141414, 0xFFFFFF, 0xFFDC45, 0xFFF0A0, 0xF2A27E, 0x4A3800 },   /* goldfinch */
    { 0x7A6A62, 0x5E514B, 0x3B322E, 0x9A8B83, 0xEE6A30, 0xFFF1E6, 0xF2B233, 0x271E1A },   /* robin */
};
static const float FLAP_DEG[FLAP] = { 252.0f, 214.0f, 178.0f, 138.0f };

static void feather(Canvas *c, float px, float py, float a, float len, float hw, uint32_t col) {
    float dx = cosf(a), dy = sinf(a), nx = -dy, ny = dx;
    float xs[5] = { px + nx * hw * 0.6f, px + dx * len * 0.6f + nx * hw, px + dx * len,
                    px + dx * len * 0.6f - nx * hw, px - nx * hw * 0.6f };
    float ys[5] = { py + ny * hw * 0.6f, py + dy * len * 0.6f + ny * hw, py + dy * len,
                    py + dy * len * 0.6f - ny * hw, py - ny * hw * 0.6f };
    fill_poly(c, xs, ys, 5, col);
}

/* A wing seen from the side: a fan of three feathers (dark primaries in front, lighter
   ones behind) with a patch of coverts at the shoulder. The fan opens toward the tail. */
static void wing(Canvas *c, float sx, float sy, float deg, float len, float hw,
                 uint32_t tip, uint32_t base, uint32_t light) {
    static const float fan[3] = { 0.0f, 17.0f, 34.0f }, lf[3] = { 1.0f, 0.82f, 0.64f };
    const uint32_t col[3] = { tip, base, light };
    float sgn = deg > 180.0f ? -1.0f : 1.0f;
    for (int k = 2; k >= 0; k--)
        feather(c, sx, sy, (deg + sgn * fan[k]) * PI_F / 180.0f, len * lf[k], hw, rgb(col[k]));
    feather(c, sx, sy, (deg + sgn * 12.0f) * PI_F / 180.0f, len * 0.48f, hw * 1.25f, rgb(base));
}

static void bird_head(Canvas *c, int kind, float hx, float hy) {
    const Plumage *p = &PLUMAGE[kind];
    fill_ellipse(c, hx, hy, 3.3f, 3.1f, rgb(p->back));
    if (kind == 1) {                                         /* cardinal crest and mask */
        fill_poly(c, (float[]){ hx - 2.5f, hx - 2.8f, hx + 1.0f }, (float[]){ hy - 1.0f, hy - 5.2f, hy - 2.0f },
                  3, rgb(p->back));
        fill_rect(c, (int)hx + 1, (int)hy - 1, (int)hx + 2, (int)hy + 1, rgb(0x1E1414));
    }
    if (kind == 2) recolor(c, (int)hx, (int)hy - 3, (int)hx + 3, (int)hy - 2, rgb(p->back), rgb(0x1E1E1E));
    fill_poly(c, (float[]){ hx + 2.4f, hx + 5.6f, hx + 2.4f }, (float[]){ hy - 1.0f, hy + 0.2f, hy + 1.4f },
              3, rgb(p->beak));
    int ex = (int)hx + (kind == 1 ? 0 : 1), ey = (int)hy - 1;
    if (kind == 3) {                                          /* robin's white eye ring */
        put(c, ex, ey - 1, rgb(0xFFFFFF));
        put(c, ex, ey + 1, rgb(0xFFFFFF));
    }
    put(c, ex, ey, rgb(0x101010));
}

/* Songbird facing right. Everything sits 2 rows down so a raised wing fits. */
static Canvas make_bird(int kind, int frame) {
    const Plumage *p = &PLUMAGE[kind];
    const float o = 2.0f;
    Canvas c = canvas_new(BIRD_W, BIRD_H);
    if (frame == FLAP) {                                      /* sitting */
        fill_poly(&c, (float[]){ 1.0f, 7.0f, 8.5f, 2.5f }, (float[]){ 15 + o, 10.5f + o, 13 + o, 17 + o }, 4, rgb(p->wing));
        fill_ellipse(&c, 10.0f, 11.5f + o, 5.2f, 4.0f, rgb(p->back));
        bird_head(&c, kind, 14.0f, 7.6f + o);
        recolor(&c, 11, 11, 19, 18, rgb(p->back), rgb(p->breast));
        recolor(&c, 6, 15, 11, 18, rgb(p->back), rgb(p->belly));
        recolor(&c, 6, 15, 11, 18, rgb(p->breast), rgb(p->belly));
        fill_ellipse(&c, 8.6f, 10.9f + o, 4.3f, 2.4f, rgb(p->wing));
        fill_poly(&c, (float[]){ 6.0f, 1.5f, 7.0f }, (float[]){ 10.5f + o, 14.0f + o, 13.0f + o }, 3, rgb(p->tip));
        fill_rect(&c, 7, 12, 9, 12, rgb(p->light));
        fill_rect(&c, 9, 17, 9, 18, rgb(0x6B4A2E));
        fill_rect(&c, 11, 17, 11, 18, rgb(0x6B4A2E));
        put(&c, 12, 18, rgb(0x6B4A2E));
    } else {
        float deg = FLAP_DEG[frame];
        if (frame < 2) wing(&c, 11.5f, 9.6f + o, deg + 12.0f, 9.5f, 2.2f, p->tip, p->tip, p->wing);
        fill_poly(&c, (float[]){ 0.0f, 7.0f, 7.0f, 1.0f }, (float[]){ 8.5f + o, 10 + o, 12.5f + o, 13.5f + o }, 4, rgb(p->wing));
        fill_ellipse(&c, 9.5f, 11.0f + o, 5.6f, 3.8f, rgb(p->back));
        bird_head(&c, kind, 14.5f, 8.5f + o);
        recolor(&c, 11, 12, 19, 18, rgb(p->back), rgb(p->breast));
        recolor(&c, 5, 14, 11, 18, rgb(p->back), rgb(p->belly));
        recolor(&c, 5, 14, 11, 18, rgb(p->breast), rgb(p->belly));
        wing(&c, 10.5f, 9.5f + o, deg, 11.0f, 2.3f, p->tip, p->wing, p->light);
    }
    outline(&c, rgb(p->line));
    return c;
}

static Canvas make_gull(int frame) {
    Canvas c = canvas_new(GULL_W, GULL_H);
    float deg = FLAP_DEG[frame];
    if (frame < 2) wing(&c, 19.5f, 14.0f, deg + 12.0f, 13.0f, 2.4f, 0x2B2E33, 0x8E99A8, 0xA9B3C0);
    fill_poly(&c, (float[]){ 2.0f, 10.0f, 10.0f, 3.0f }, (float[]){ 13.5f, 14.0f, 16.8f, 16.0f }, 4, rgb(0xEEF1F5));
    fill_ellipse(&c, 17.0f, 15.5f, 8.5f, 3.2f, rgb(0xFAFBFC));
    fill_ellipse(&c, 26.0f, 13.5f, 3.2f, 2.9f, rgb(0xFAFBFC));
    recolor(&c, 0, 0, 24, 13, rgb(0xFAFBFC), rgb(0xE2E7EE));
    fill_poly(&c, (float[]){ 28.4f, 32.6f, 28.4f }, (float[]){ 12.6f, 13.8f, 14.8f }, 3, rgb(0xFFC531));
    put(&c, 31, 13, rgb(0xE5533B));
    put(&c, 27, 12, rgb(0x15181D));
    wing(&c, 18.0f, 14.5f, deg, 15.0f, 2.6f, 0x2B2E33, 0xB7C1CD, 0xD3DAE3);
    outline(&c, rgb(0x3A4250));
    return c;
}

/* ---------- butterflies, sun, clouds ---------- */

static Canvas make_butterfly(int col, int frame) {
    static const uint32_t up[4] = { 0xFF9A1F, 0xFFE45C, 0x7EC8FF, 0xFF8FC8 };
    static const uint32_t lo[4] = { 0xE87A10, 0xF6C93A, 0x5AA8F0, 0xF070B0 };
    static const uint32_t spot[4] = { 0xFFE6B0, 0xFFFFFF, 0xE8F6FF, 0xFFE3F2 };
    static const uint32_t line[4] = { 0x3A1E00, 0x5A4300, 0x123A66, 0x5A1840 };
    static const float span[3] = { 1.0f, 0.55f, 0.2f };
    Canvas c = canvas_new(FLY_W, FLY_H);
    float s = span[frame];
    for (int side = -1; side <= 1; side += 2) {
        float ux = 5.5f + side * (0.6f + 2.1f * s), lx = 5.5f + side * (0.5f + 1.5f * s);
        fill_ellipse(&c, lx, 6.2f, fmaxf(0.7f, 1.7f * s), 1.6f, rgb(lo[col]));
        fill_ellipse(&c, ux, 3.6f, fmaxf(0.8f, 2.5f * s), 2.3f, rgb(up[col]));
        if (s > 0.5f) put(&c, (int)(ux + side * 0.6f), 3, rgb(spot[col]));
    }
    outline(&c, rgb(line[col]));
    fill_rect(&c, 5, 2, 5, 7, rgb(0x2A1C12));
    put(&c, 4, 1, rgb(0x2A1C12)); put(&c, 3, 0, rgb(0x2A1C12));
    put(&c, 6, 1, rgb(0x2A1C12)); put(&c, 7, 0, rgb(0x2A1C12));
    return c;
}

static Canvas make_sun(void) {
    Canvas c = canvas_new(SUN_D, SUN_D);
    fill_ellipse(&c, 12.5f, 12.5f, 12.5f, 12.5f, rgb(0xFFB62E));
    fill_ellipse(&c, 12.5f, 12.5f, 11.3f, 11.3f, rgb(0xFFD23B));
    fill_ellipse(&c, 11.3f, 11.3f, 8.4f, 8.4f, rgb(0xFFE36B));
    fill_ellipse(&c, 9.0f, 9.0f, 3.4f, 3.4f, rgb(0xFFF4B8));
    return c;
}

/* Twelve rays; frame 0 has the even ones long, 2 the odd ones, 1 sits in between. */
static Canvas make_rays(int frame) {
    Canvas c = canvas_new(RAYS_D, RAYS_D);
    float cx = RAYS_D / 2.0f;
    for (int k = 0; k < 12; k++) {
        float a = k * PI_F / 6.0f + PI_F / 12.0f, d = 0.09f;
        int even = k % 2 == 0;
        float len = frame == 1 ? 25.0f : (even == (frame == 0) ? 29.0f : 21.0f);
        float xs[3] = { cx + cosf(a - d) * 14.5f, cx + cosf(a) * len, cx + cosf(a + d) * 14.5f };
        float ys[3] = { cx + sinf(a - d) * 14.5f, cx + sinf(a) * len, cx + sinf(a + d) * 14.5f };
        fill_poly(&c, xs, ys, 3, RGBA(0xFFE680, 0xA0));
    }
    return c;
}

static Canvas make_cloud(int w) {
    int h = w / 3 + 6;
    Canvas c = canvas_new(w, h);
    float base = h - 2.5f;
    fill_ellipse(&c, w * 0.5f, base - 2.0f, w * 0.5f - 0.5f, 3.4f, 1);
    int puffs = 3 + w / 15;
    for (int i = 0; i < puffs; i++) {
        float f = (i + 0.5f) / puffs;
        float r = (h - 4) * 0.5f * (0.62f + 0.38f * sinf(PI_F * f)) + frand(-0.8f, 0.8f);
        float px = 3.0f + r * 0.6f + (w - 6.0f - r * 1.2f) * f + frand(-1.5f, 1.5f);
        fill_ellipse(&c, px, base - r * 0.75f, r, r * 0.9f, 1);
    }
    for (int y = (int)base; y < h; y++)
        for (int x = 0; x < w; x++) put(&c, x, y, 0);
    Canvas o = canvas_new(w, h);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            if (!get(&c, x, y)) continue;
            int down = 0, upd = 0;
            while (down < 4 && get(&c, x, y + down + 1)) down++;
            while (upd < 2 && get(&c, x, y - upd - 1)) upd++;
            uint32_t col = down == 0 ? 0xC3D7EE : down <= 2 ? 0xDCE8F6 : upd == 0 ? 0xFFFFFF : 0xF4F8FE;
            if (upd == 1 && get(&c, x - 1, y - 1) == 0 && down > 2) col = 0xFFFFFF;
            put(&o, x, y, rgb(col));
        }
    free(c.px);
    return o;
}

/* ---------- plants ---------- */

static Canvas make_flower(int kind, int tall, int frame) {
    static const uint32_t petal[FLOWER_KINDS] = { 0xFFFFFF, 0xF2393A, 0xFFD21F, 0xFF7EB6, 0xA77BFF };
    static const uint32_t mid[FLOWER_KINDS] = { 0xFFC21F, 0x3A1616, 0xF08C0B, 0xFFF3A0, 0xFFFFFF };
    static const uint32_t line[FLOWER_KINDS] = { 0x7F93A8, 0x7A1414, 0xA86A00, 0xA8336E, 0x51309A };
    int h = tall ? 15 : 11, dx = frame - 1;
    Canvas c = canvas_new(9, h);
    for (int y = 4; y < h; y++) {
        float k = (float)(h - 1 - y) / (float)(h - 5);
        put(&c, 4 + (int)lroundf(dx * k), y, rgb(0x3C9A36));
    }
    int ly = h - 4, lx = 4 + (int)lroundf(dx * 3.0f / (h - 5));
    put(&c, lx + 1, ly, rgb(0x5CC046)); put(&c, lx + 2, ly - 1, rgb(0x5CC046));
    if (tall) { put(&c, lx - 1, ly - 4, rgb(0x5CC046)); put(&c, lx - 2, ly - 5, rgb(0x5CC046)); }
    float hx = 4.5f + dx, hy = 3.0f;
    if (kind == 0 || kind == 3) {                     /* plus-shaped petals */
        fill_ellipse(&c, hx, hy, 3.4f, 3.4f, rgb(line[kind]));
        fill_ellipse(&c, hx, hy, 2.6f, 2.6f, rgb(petal[kind]));
        put(&c, (int)hx - 2, (int)hy - 2, rgb(line[kind])); put(&c, (int)hx + 2, (int)hy - 2, rgb(line[kind]));
        put(&c, (int)hx - 2, (int)hy + 2, rgb(line[kind])); put(&c, (int)hx + 2, (int)hy + 2, rgb(line[kind]));
    } else if (kind == 4) {                           /* bell */
        fill_ellipse(&c, hx, hy, 2.9f, 3.0f, rgb(line[kind]));
        fill_ellipse(&c, hx, hy - 0.3f, 2.1f, 2.2f, rgb(petal[kind]));
        put(&c, (int)hx - 1, (int)hy + 2, rgb(petal[kind])); put(&c, (int)hx + 1, (int)hy + 2, rgb(petal[kind]));
    } else {                                          /* round cup */
        fill_ellipse(&c, hx, hy, 3.2f, 3.0f, rgb(line[kind]));
        fill_ellipse(&c, hx, hy, 2.4f, 2.2f, rgb(petal[kind]));
        put(&c, (int)hx - 1, (int)hy - 1, rgb(kind == 1 ? 0xFF8A80 : 0xFFF08A));
    }
    put(&c, (int)hx, (int)hy, rgb(mid[kind]));
    return c;
}

static Canvas make_sunflower(int frame) {
    int dx = frame - 1;
    Canvas c = canvas_new(17, SUN_H);
    for (int y = 9; y < SUN_H; y++) {
        float k = (float)(SUN_H - 1 - y) / (float)(SUN_H - 10);
        int x = 8 + (int)lroundf(dx * k);
        put(&c, x, y, rgb(0x3C9A36));
        put(&c, x + 1, y, rgb(0x2E7F2C));
    }
    float lx = 8.5f + dx * 0.4f;
    fill_poly(&c, (float[]){ lx, lx - 7.0f, lx - 4.0f }, (float[]){ 27.0f, 23.5f, 28.5f }, 3, rgb(0x4DB13C));
    fill_poly(&c, (float[]){ lx + 1, lx + 7.5f, lx + 4.5f }, (float[]){ 21.0f, 17.0f, 22.5f }, 3, rgb(0x4DB13C));
    float hx = 8.5f + dx, hy = 8.0f;
    fill_ellipse(&c, hx, hy, 8.0f, 7.6f, rgb(0xB06A00));
    fill_ellipse(&c, hx, hy, 7.1f, 6.8f, rgb(0xFFC61A));
    for (int y = 0; y < 17; y++)
        for (int x = 0; x < 17; x++) {
            float ddx = x + 0.5f - hx, ddy = y + 0.5f - hy;
            int sector = (int)floorf((atan2f(ddy, ddx) + PI_F) / (PI_F / 7.0f));
            if (ddx * ddx + ddy * ddy > 18.0f && sector % 2 && get(&c, x, y) == rgb(0xFFC61A))
                put(&c, x, y, rgb(0xF5A300));
        }
    fill_ellipse(&c, hx, hy, 3.8f, 3.6f, rgb(0x7A4A1E));
    for (int y = (int)hy - 3; y <= (int)hy + 3; y++)
        for (int x = (int)hx - 3; x <= (int)hx + 3; x++)
            if ((x + y) % 2 && get(&c, x, y) == rgb(0x7A4A1E)) put(&c, x, y, rgb(0x5A3412));
    put(&c, (int)hx - 2, (int)hy - 2, rgb(0x9E6A36));
    return c;
}

/* ---------- scene ---------- */

enum { FLY, LAND, SIT, TAKEOFF };

typedef struct {
    int kind, level, tex, dir, flock, state, perch, frame, glide;
    float x, y, baseY, v, phase, rate, age, wf, wph, wamp, timer, look, hop;
    float x0, y0, tx, ty;
} Bird;

typedef struct { float x, y; int plant, busy; } Perch;               /* plant: sunflower or -1 */
typedef struct { float x, y, ph; int kind, tall, frame; } Plant;
typedef struct { int shape, layer; float x, y; } Cloud;
typedef struct { int on, col, target, visits, sitting; float x, y, timer, flap; } Butterfly;

typedef struct {
    int w, h, u, lw, lh, nbirds, nclouds, nplants, nperch, want;
    float t, spawnT, flockT, flyT, sunX, sunY;
    int *nearTop;
    SDL_Texture *sky, *hill[3], *sun, *rays[3];
    SDL_Texture *bird[KINDS][FLAP + 1], *gull[FLAP], *cloud[CLOUD_SHAPES], *fly[4][3];
    SDL_Texture *flower[FLOWER_KINDS][2][3], *sunflower[3];
    int cloudW[CLOUD_SHAPES], cloudH[CLOUD_SHAPES];
    Bird b[MAX_BIRDS];
    Cloud c[MAX_CLOUDS];
    Plant p[MAX_PLANTS];
    Perch perch[MAX_PERCHES];
    Butterfly f[MAX_FLIES];
} Birds;

static const float LEVEL_SCALE[4] = { 0.6f, 1.0f, 1.5f, 2.0f };
static const float LEVEL_Y[4][2] = { { 0.06f, 0.40f }, { 0.08f, 0.48f }, { 0.08f, 0.50f }, { 0.10f, 0.48f } };
static const float CLOUD_V[3] = { 0.007f, 0.012f, 0.02f };

static int texel(const Birds *s, int level) {
    int t = (int)(s->u * LEVEL_SCALE[level] + 0.5f);
    return t < 1 ? 1 : t;
}
static int cloud_tex(const Birds *s, int layer) { return layer == 0 ? s->u : layer == 1 ? (s->u * 3 + 1) / 2 : s->u * 2; }

static float bird_offset(const Bird *b) {
    return sinf(b->age * b->wf + b->wph) * b->wamp + cosf(b->phase * 2 * PI_F) * b->tex * 1.2f;
}

static int sway_frame(float v) { return v > 0.4f ? 2 : v < -0.4f ? 0 : 1; }

/* Where a perch is right now (sunflowers sway), in bird-centre coordinates. */
static void perch_pos(const Birds *s, const Perch *pc, int tex, float *x, float *y) {
    *x = pc->x;
    *y = pc->y - 6.0f * tex;
    if (pc->plant >= 0) *x += (s->p[pc->plant].frame - 1) * s->u;
}

static float sun_y(const Birds *s, int i) { return s->p[i].y - (SUN_H - 1) * s->u; }

static Bird *new_bird(Birds *s, int kind, int level, int dir) {
    if (s->nbirds >= MAX_BIRDS) return NULL;
    Bird *b = &s->b[s->nbirds++];
    memset(b, 0, sizeof *b);
    b->kind = kind;
    b->level = level;
    b->tex = texel(s, level);
    b->dir = dir;
    b->perch = -1;
    b->v = b->tex * frand(24.0f, 32.0f) * (kind == GULL ? 0.6f : 1.0f);
    b->rate = kind == GULL ? frand(1.0f, 1.3f) : frand(2.6f, 3.5f);
    b->phase = frand(0, 1);
    b->wf = frand(0.4f, 0.9f);
    b->wph = frand(0, 6.28f);
    b->wamp = b->tex * frand(2.0f, 6.0f);
    b->glide = kind == GULL;
    b->timer = kind == GULL ? frand(2, 5) : frand(1, 3.5f);
    return b;
}

static float half_w(const Bird *b) { return (b->kind == GULL ? 18.0f : 11.0f) * b->tex; }

static void pick_perch(Birds *s, Bird *b) {
    int options[MAX_PERCHES], n = 0;
    for (int i = 0; i < s->nperch; i++)
        if (!s->perch[i].busy && (s->perch[i].x - b->x) * b->dir > 0.22f * s->w) options[n++] = i;
    if (!n) return;
    b->perch = options[irand(n)];
    s->perch[b->perch].busy = 1;
}

static void spawn_single(Birds *s, int anywhere) {
    int gulls = 0;
    for (int i = 0; i < s->nbirds; i++) gulls += s->b[i].kind == GULL;
    int dir = irand(2) ? 1 : -1, kind, level, roll = irand(100);
    if (!gulls && irand(100) < 22) { kind = GULL; level = 0; }
    else {
        kind = irand(KINDS);
        level = roll < 28 ? 0 : roll < 68 ? 1 : roll < 88 ? 2 : 3;
    }
    Bird *b = new_bird(s, kind, level, dir);
    if (!b) return;
    b->x = anywhere ? frand(0.05f, 0.95f) * s->w : dir > 0 ? -half_w(b) : s->w + half_w(b);
    b->baseY = frand(LEVEL_Y[level][0], LEVEL_Y[level][1]) * s->h;
    if (kind == GULL) b->baseY = frand(0.08f, 0.3f) * s->h;
    if (level == 1 && irand(100) < 45) pick_perch(s, b);
    b->y = b->baseY + bird_offset(b);
}

static void spawn_flock(Birds *s, int anywhere) {
    int kind = irand(KINDS), level = irand(2), dir = irand(2) ? 1 : -1, n = 3 + irand(3), vee = irand(2);
    Bird *lead = new_bird(s, kind, level, dir);
    if (!lead) return;
    lead->flock = 1;
    lead->x = anywhere ? frand(0.3f, 0.7f) * s->w : dir > 0 ? -half_w(lead) : s->w + half_w(lead);
    lead->baseY = frand(0.12f, 0.36f) * s->h;
    lead->wamp = lead->tex * 4.0f;
    float sp = (vee ? 15.0f : 21.0f) * lead->tex;
    for (int i = 1; i < n; i++) {
        Bird *b = new_bird(s, kind, level, dir);
        if (!b) break;
        float rank = vee ? (float)((i + 1) / 2) : (float)i, side = i % 2 ? -1.0f : 1.0f;
        b->flock = 1;
        b->v = lead->v;
        b->wf = lead->wf;
        b->wamp = lead->wamp;
        b->rate = lead->rate * frand(0.94f, 1.06f);
        b->x = lead->x - dir * rank * sp;
        b->baseY = lead->baseY + (vee ? side * rank * sp * 0.45f : 0.0f);
        b->wph = vee ? lead->wph : lead->wph - i * 0.9f;
        b->glide = lead->glide;
        b->timer = lead->timer + frand(-0.3f, 0.3f);
    }
    for (int i = 0; i < s->nbirds; i++)
        if (s->b[i].flock) s->b[i].y = s->b[i].baseY + bird_offset(&s->b[i]);
}

static void spawn_butterfly(Birds *s, int anywhere) {
    int flowers[MAX_PLANTS], n = 0;
    for (int i = 0; i < s->nplants; i++)
        if (s->p[i].kind != SUNFLOWER && s->p[i].x > 0.05f * s->w && s->p[i].x < 0.95f * s->w) flowers[n++] = i;
    if (!n) return;
    for (int i = 0; i < MAX_FLIES; i++) {
        Butterfly *f = &s->f[i];
        if (f->on) continue;
        memset(f, 0, sizeof *f);
        f->on = 1;
        f->col = irand(4);
        f->visits = 2 + irand(3);
        f->target = flowers[irand(n)];
        int left = irand(2);
        f->x = anywhere ? frand(0.2f, 0.8f) * s->w : left ? -6.0f * s->u : s->w + 6.0f * s->u;
        f->y = s->p[f->target].y - frand(14, 30) * s->u;
        f->flap = frand(0, 5);
        return;
    }
}

static void *create(SDL_Renderer *r, int w, int h) {
    Birds *s = calloc(1, sizeof *s);
    s->w = w;
    s->h = h;
    s->u = (h + 120) / 240 > 1 ? (h + 120) / 240 : 1;
    int u = s->u, lw = (w + u - 1) / u, lh = (h + u - 1) / u;
    s->lw = lw;
    s->lh = lh;
    s->sunX = lw * 0.78f;
    s->sunY = lh * 0.2f;

    /* Sky with the sun's glow baked in, dithered in chunky bands. */
    Canvas sky = canvas_new(lw, lh);
    float glowR = lh * 0.6f;
    for (int y = 0; y < lh; y++)
        for (int x = 0; x < lw; x++) {
            float t = y / (lh * 0.66f);
            uint32_t top = 0x2C84DE, midc = 0x62B6F0, hor = 0xC4EBFC;
            float dx = x - s->sunX, dy = (y - s->sunY) * 1.1f, d = sqrtf(dx * dx + dy * dy) / glowR;
            float g = d < 1 ? (1 - d) * (1 - d) * 0.9f : 0;
            /* Blend the two gradient stops, then the glow, in one dithered pass. */
            uint32_t a = t < 0.55f ? top : midc, b = t < 0.55f ? midc : hor;
            float k = t < 0.55f ? t / 0.55f : (t - 0.55f) / 0.45f;
            float base[3];
            for (int ch = 0; ch < 3; ch++) {
                int sh = 16 - ch * 8;
                float ca = (float)((a >> sh) & 255), cb = (float)((b >> sh) & 255);
                float v = ca + (cb - ca) * (k > 1 ? 1 : k);
                float gc = (float)((0xFFF2C0u >> sh) & 255);
                base[ch] = v + (gc - v) * g;
            }
            uint32_t mixed = ((uint32_t)base[0] << 16) | ((uint32_t)base[1] << 8) | (uint32_t)base[2];
            put(&sky, x, y, dither_mix(mixed, mixed, 0, x, y, 9.0f));
        }
    s->sky = canvas_texture(r, &sky);

    /* Three bands of rolling hills, far to near. */
    static const float hb[3] = { 0.60f, 0.69f, 0.81f }, ha[3] = { 0.05f, 0.045f, 0.03f };
    static const float hf[3] = { 4.1f, 3.2f, 2.3f };
    static const uint32_t rim[3] = { 0xB9E5C6, 0x95D976, 0x8BDE5E };
    static const uint32_t hiC[3] = { 0x9AD3AE, 0x6CC25A, 0x58BC40 };
    static const uint32_t loC[3] = { 0x8CC6A2, 0x56A84A, 0x3A9132 };
    s->nearTop = calloc((size_t)lw, sizeof(int));
    for (int k = 0; k < 3; k++) {
        Canvas c = canvas_new(lw, lh);
        float p1 = frand(0, 6.28f), p2 = frand(0, 6.28f);
        int *top = calloc((size_t)lw, sizeof(int));
        for (int x = 0; x < lw; x++) {
            float xn = (float)x / lh;
            top[x] = (int)(lh * (hb[k] + ha[k] * sinf(xn * hf[k] + p1) + ha[k] * 0.4f * sinf(xn * hf[k] * 2.6f + p2)));
            for (int y = top[x]; y < lh; y++)
                put(&c, x, y, y == top[x] ? rgb(rim[k])
                              : dither_mix(hiC[k], loC[k], (float)(y - top[x]) / (lh * 0.22f), x, y, 1.0f));
        }
        if (k < 2) {                                      /* lollipop trees */
            int trees = lw / (k ? 26 : 34) + 2;
            for (int i = 0; i < trees; i++) {
                int x = irand(lw), base = top[x] + 1 + irand(3);
                float rr = k ? frand(2.6f, 3.8f) : frand(1.6f, 2.4f);
                uint32_t dark = k ? 0x3E8E44 : 0x79B894, lite = k ? 0x5FB85A : 0x93CFA8;
                if (k) fill_rect(&c, x, base - 2, x, base, rgb(0x7A5230));
                fill_ellipse(&c, x + 0.5f, base - rr - (k ? 1.5f : 0.5f), rr, rr * 1.1f, rgb(dark));
                fill_ellipse(&c, x - 0.3f, base - rr * 1.25f - (k ? 1.5f : 0.5f), rr * 0.55f, rr * 0.5f, rgb(lite));
            }
        } else {
            for (int x = 0; x < lw; x++) {                /* grass along the top, specks and tiny flowers */
                s->nearTop[x] = top[x];
                if (irand(3) == 0) put(&c, x, top[x] - 1, rgb(rim[k]));
                if (irand(9) == 0) { put(&c, x, top[x] - 1, rgb(rim[k])); put(&c, x, top[x] - 2, rgb(rim[k])); }
            }
            for (int i = 0; i < lw * (lh / 5) / 14; i++) {
                int x = irand(lw), y = s->nearTop[x] + 2 + irand(lh - s->nearTop[x]);
                put(&c, x, y, rgb(irand(2) ? 0x4AA93A : 0x6FCB4E));
                put(&c, x, y + 1, rgb(0x4AA93A));
            }
            static const uint32_t dots[3] = { 0xFFFFFF, 0xFFE14D, 0xFF9CC4 };
            for (int i = 0; i < lw / 5; i++) {
                int x = irand(lw), y = s->nearTop[x] + 2 + irand((lh - s->nearTop[x]) / 2 + 1);
                put(&c, x, y, rgb(dots[irand(3)]));
            }
        }
        free(top);
        s->hill[k] = canvas_texture(r, &c);
    }

    /* Flowers and sunflowers on the meadow, sorted back to front. */
    int nf = lw / 9;
    if (nf > MAX_PLANTS - 3) nf = MAX_PLANTS - 3;
    for (int i = 0; i < nf; i++) {
        Plant *p = &s->p[s->nplants++];
        int x = 2 + irand(lw - 4), room = lh - s->nearTop[x] - 3;
        p->x = (x + 0.5f) * u;
        p->y = (float)(s->nearTop[x] + 3 + irand(room > 1 ? room * 3 / 4 : 1)) * u;
        p->kind = irand(FLOWER_KINDS);
        p->tall = irand(3) == 0;
        p->ph = frand(0, 6.28f);
    }
    float sunXs[2] = { frand(0.08f, 0.2f), frand(0.55f, 0.68f) };
    for (int i = 0; i < 2; i++) {
        Plant *p = &s->p[s->nplants++];
        int x = (int)(sunXs[i] * lw);
        p->x = (x + 0.5f) * u;
        p->y = (float)(s->nearTop[x] + 6 + irand(4)) * u;
        p->kind = SUNFLOWER;
        p->ph = frand(0, 6.28f);
    }
    for (int i = 1; i < s->nplants; i++)
        for (int j = i; j > 0 && s->p[j].y < s->p[j - 1].y; j--) {
            Plant t = s->p[j]; s->p[j] = s->p[j - 1]; s->p[j - 1] = t;
        }
    for (int i = 0; i < s->nplants; i++) {
        s->p[i].frame = 1;
        if (s->p[i].kind == SUNFLOWER) s->perch[s->nperch++] = (Perch){ s->p[i].x, sun_y(s, i), i, 0 };
    }
    for (int i = 0; i < 4; i++) {
        int x = (int)((0.12f + 0.22f * i + frand(-0.05f, 0.05f)) * lw);
        s->perch[s->nperch++] = (Perch){ (x + 0.5f) * u, (float)(s->nearTop[x] + 2) * u, -1, 0 };
    }

    for (int k = 0; k < KINDS; k++)
        for (int fr = 0; fr <= FLAP; fr++) {
            Canvas c = make_bird(k, fr);
            s->bird[k][fr] = canvas_texture(r, &c);
        }
    for (int fr = 0; fr < FLAP; fr++) {
        Canvas c = make_gull(fr);
        s->gull[fr] = canvas_texture(r, &c);
    }
    for (int col = 0; col < 4; col++)
        for (int fr = 0; fr < 3; fr++) {
            Canvas c = make_butterfly(col, fr);
            s->fly[col][fr] = canvas_texture(r, &c);
        }
    for (int k = 0; k < FLOWER_KINDS; k++)
        for (int t = 0; t < 2; t++)
            for (int fr = 0; fr < 3; fr++) {
                Canvas c = make_flower(k, t, fr);
                s->flower[k][t][fr] = canvas_texture(r, &c);
            }
    for (int fr = 0; fr < 3; fr++) {
        Canvas c = make_sunflower(fr), ry = make_rays(fr);
        s->sunflower[fr] = canvas_texture(r, &c);
        s->rays[fr] = canvas_texture(r, &ry);
    }
    Canvas sun = make_sun();
    s->sun = canvas_texture(r, &sun);
    for (int i = 0; i < CLOUD_SHAPES; i++) {
        Canvas c = make_cloud(22 + i * 4 + irand(4));
        s->cloudW[i] = c.w;
        s->cloudH[i] = c.h;
        s->cloud[i] = canvas_texture(r, &c);
    }

    /* Clouds: more far ones, a couple of big near ones. */
    float aspect = (float)w / h;
    int per[3] = { 2 + (int)(aspect * 1.6f), 1 + (int)(aspect * 1.1f), 1 + (int)(aspect * 0.6f) };
    for (int layer = 0; layer < 3; layer++)
        for (int i = 0; i < per[layer] && s->nclouds < MAX_CLOUDS; i++) {
            Cloud *c = &s->c[s->nclouds++];
            c->layer = layer;
            c->shape = layer == 0 ? irand(4) : 2 + irand(CLOUD_SHAPES - 2);
            c->x = (i + frand(0.1f, 0.9f)) / per[layer] * (w + 100.0f) - 100.0f;
            c->y = frand(layer == 2 ? 0.02f : 0.05f, layer == 0 ? 0.32f : 0.3f) * h;
        }

    s->want = (int)(6.5f * aspect / 1.78f);
    if (s->want < 4) s->want = 4;
    if (s->want > 12) s->want = 12;
    for (int i = 0; i < s->want; i++) spawn_single(s, 1);
    if (irand(2)) spawn_flock(s, 1);
    if (irand(100) < 70) {                               /* someone is already resting */
        int pi = irand(s->nperch);
        Bird *b = s->perch[pi].busy ? NULL : new_bird(s, irand(KINDS), 1, irand(2) ? 1 : -1);
        if (b) {
            b->perch = pi;
            s->perch[pi].busy = 1;
            b->state = SIT;
            b->timer = frand(2.0f, 6.0f);
            b->look = frand(0.5f, 1.2f);
            perch_pos(s, &s->perch[pi], b->tex, &b->tx, &b->ty);
            b->x = b->tx;
            b->y = b->ty;
        }
    }
    spawn_butterfly(s, 1);
    s->spawnT = frand(1, 3);
    s->flockT = frand(8, 16);
    s->flyT = frand(6, 12);
    return s;
}

static void remove_bird(Birds *s, int i) {
    if (s->b[i].perch >= 0) s->perch[s->b[i].perch].busy = 0;
    s->b[i] = s->b[--s->nbirds];
}

static void advance_wings(Bird *b, float rate, float dt) {
    const float hold = 1.0f / 6;                          /* phase where the wings are half up */
    float np = b->phase + rate * dt;
    if (b->glide && b->phase <= hold && np >= hold) np = hold;
    if (b->glide && b->phase == hold) np = hold;
    b->phase = fmodf(np, 1.0f);
}

static void step_bird(Birds *s, Bird *b, float dt) {
    static const int seq[6] = { 0, 1, 2, 3, 2, 1 };
    float lo = LEVEL_Y[b->level][0] * s->h, hi = LEVEL_Y[b->level][1] * s->h;
    b->age += dt;
    switch (b->state) {
    case FLY:
        b->timer -= dt;
        if (b->timer <= 0) {
            b->glide = !b->glide;
            if (b->kind == GULL) b->timer = b->glide ? frand(3, 7) : frand(0.8f, 1.6f);
            else b->timer = b->glide ? frand(0.6f, 1.6f) : frand(1.5f, 4.0f);
        }
        advance_wings(b, b->rate, dt);
        b->baseY += (b->glide ? 2.5f : -0.8f) * b->tex * dt;
        if (b->baseY < lo) b->baseY += 6.0f * b->tex * dt;
        if (b->baseY > hi) b->baseY -= 6.0f * b->tex * dt;
        b->x += b->dir * b->v * dt;
        b->y = b->baseY + bird_offset(b);
        if (b->perch >= 0) {
            float px, py;
            perch_pos(s, &s->perch[b->perch], b->tex, &px, &py);
            float ahead = (px - b->x) * b->dir;
            if (ahead < 0) { s->perch[b->perch].busy = 0; b->perch = -1; }
            else if (ahead < 0.34f * s->w) {
                b->state = LAND;
                b->x0 = b->x;
                b->y0 = b->y;
                b->tx = px;
                b->ty = py;
            }
        }
        break;
    case LAND: {
        float span = b->tx - b->x0, pr = fabsf(span) < 1 ? 1 : (b->x - b->x0) / span;
        if (pr < 0) pr = 0;
        b->glide = pr < 0.72f;
        advance_wings(b, b->rate * (b->glide ? 1.0f : 1.8f), dt);
        b->x += b->dir * b->v * (1 - 0.65f * pr) * dt;
        pr = fabsf(span) < 1 ? 1 : (b->x - b->x0) / span;
        if (pr >= 1) {
            b->state = SIT;
            b->timer = frand(2.5f, 5.5f);
            b->look = frand(0.6f, 1.4f);
            b->hop = 0;
            pr = 1;
        }
        b->x = b->x0 + span * pr;
        b->y = b->y0 + (b->ty - b->y0) * pr * pr * (3 - 2 * pr);
        break;
    }
    case SIT: {
        perch_pos(s, &s->perch[b->perch], b->tex, &b->x, &b->y);
        b->timer -= dt;
        b->look -= dt;
        if (b->look <= 0) {
            int roll = irand(100);
            if (roll < 40) b->dir = -b->dir;
            else if (roll < 70) b->hop = 0.3f;
            b->look = frand(0.6f, 1.5f);
        }
        if (b->hop > 0) {
            b->hop -= dt;
            if (b->hop < 0) b->hop = 0;
            b->y -= sinf(PI_F * b->hop / 0.3f) * b->tex * 2.5f;
        }
        if (b->timer <= 0) {
            s->perch[b->perch].busy = 0;
            b->perch = -1;
            b->state = TAKEOFF;
            b->glide = 0;
            b->age = 0;
            b->timer = 0;
            b->phase = 0.5f;
        }
        break;
    }
    case TAKEOFF:
        b->timer += dt;
        advance_wings(b, b->rate * 1.7f, dt);
        b->x += b->dir * b->v * (0.3f + 0.7f * fminf(1.0f, b->timer / 1.0f)) * dt;
        b->y -= b->tex * 55.0f * fmaxf(0.0f, 1.0f - b->timer / 1.8f) * dt;
        if (b->timer >= 1.8f) {
            b->state = FLY;
            b->timer = frand(1.5f, 3.0f);
            b->baseY = b->y - bird_offset(b);
        }
        break;
    }
    b->frame = b->state == SIT ? FLAP : seq[(int)(b->phase * 6) % 6];
}

static void step_butterfly(Birds *s, Butterfly *f, float dt) {
    float u = (float)s->u;
    f->flap += dt;
    float gx, gy;
    if (f->target >= 0) {
        const Plant *p = &s->p[f->target];
        int th = p->tall ? 15 : 11;
        gx = p->x + (p->frame - 1) * u;
        gy = p->y - th * u + 3.0f * u - 2.5f * u;
    } else {
        gx = f->x < s->w / 2 ? -12.0f * u : s->w + 12.0f * u;
        gy = f->y - 6.0f * u;
    }
    if (f->sitting) {
        f->x = gx;
        f->y = gy;
        f->timer -= dt;
        if (f->timer <= 0) {
            f->sitting = 0;
            f->visits--;
            if (f->visits > 0) {
                int tries = 0, next;
                do next = irand(s->nplants);
                while ((s->p[next].kind == SUNFLOWER || next == f->target || s->p[next].x < 0.05f * s->w ||
                        s->p[next].x > 0.95f * s->w) && ++tries < 30);
                f->target = tries < 30 ? next : -1;
            } else {
                f->target = -1;
            }
            f->y -= 2 * u;
        }
        return;
    }
    float dx = gx - f->x, dy = gy - f->y, d = sqrtf(dx * dx + dy * dy);
    float speed = 20.0f * u, wob = fminf(1.0f, d / (10.0f * u));
    if (d > 0.01f) {
        f->x += dx / d * speed * dt;
        f->y += dy / d * speed * dt;
    }
    f->x += sinf(f->flap * 2.1f) * 8.0f * u * wob * dt;
    f->y += cosf(f->flap * 4.3f) * 16.0f * u * wob * dt;
    if (f->target >= 0 && d < 2.5f * u) {
        f->sitting = 1;
        f->timer = frand(1.6f, 3.4f);
    }
    if (f->target < 0 && (f->x < -10.0f * u || f->x > s->w + 10.0f * u)) f->on = 0;
}

static void step(void *p, float dt) {
    Birds *s = p;
    s->t += dt;
    for (int i = 0; i < s->nplants; i++) {
        Plant *pl = &s->p[i];
        float wave = s->t * (pl->kind == SUNFLOWER ? 0.7f : 1.3f) - pl->x / s->w * 4.0f;
        pl->frame = sway_frame(sinf(wave + pl->ph * 0.3f) * 0.8f + sinf(s->t * 2.3f + pl->ph) * 0.25f);
    }
    for (int i = 0; i < s->nclouds; i++) {
        Cloud *c = &s->c[i];
        int t = cloud_tex(s, c->layer);
        c->x += CLOUD_V[c->layer] * s->w * dt;
        if (c->x > s->w) {
            c->shape = c->layer == 0 ? irand(4) : 2 + irand(CLOUD_SHAPES - 2);
            c->x = -(float)s->cloudW[c->shape] * t - frand(0, 0.25f) * s->w;
            c->y = frand(c->layer == 2 ? 0.02f : 0.05f, c->layer == 0 ? 0.32f : 0.3f) * s->h;
        }
    }
    for (int i = 0; i < s->nbirds; i++) step_bird(s, &s->b[i], dt);
    for (int i = s->nbirds - 1; i >= 0; i--) {
        Bird *b = &s->b[i];
        if (b->state == SIT || b->state == LAND) continue;
        if ((b->dir > 0 && b->x - half_w(b) > s->w) || (b->dir < 0 && b->x + half_w(b) < 0)) remove_bird(s, i);
    }
    int singles = 0;
    for (int i = 0; i < s->nbirds; i++) singles += !s->b[i].flock;
    s->spawnT -= dt;
    if (s->spawnT <= 0 && singles < s->want) {
        spawn_single(s, 0);
        s->spawnT = frand(0.8f, 3.5f);
    }
    s->flockT -= dt;
    if (s->flockT <= 0) {
        int flock = 0;
        for (int i = 0; i < s->nbirds; i++) flock += s->b[i].flock;
        if (!flock) spawn_flock(s, 0);
        s->flockT = frand(10, 22);
    }
    for (int i = 0; i < MAX_FLIES; i++)
        if (s->f[i].on) step_butterfly(s, &s->f[i], dt);
    s->flyT -= dt;
    if (s->flyT <= 0) {
        spawn_butterfly(s, 0);
        s->flyT = frand(7, 15);
    }
}

static void draw_bird(const Birds *s, SDL_Renderer *r, const Bird *b) {
    int t = b->tex;
    SDL_Rect d;
    SDL_Texture *tex;
    if (b->kind == GULL) {
        tex = s->gull[b->frame];
        d = (SDL_Rect){ (int)floorf(b->x - 17.0f * t), (int)floorf(b->y - 15.5f * t), GULL_W * t, GULL_H * t };
    } else {
        tex = s->bird[b->kind][b->frame];
        d = (SDL_Rect){ (int)floorf(b->x - 10.5f * t), (int)floorf(b->y - 13.0f * t), BIRD_W * t, BIRD_H * t };
    }
    if (d.x > s->w || d.x + d.w < 0) return;
    SDL_RenderCopyEx(r, tex, NULL, &d, 0, NULL, b->dir < 0 ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
}

static void draw_birds(const Birds *s, SDL_Renderer *r, int level, int grounded) {
    for (int i = 0; i < s->nbirds; i++) {
        const Bird *b = &s->b[i];
        int low = b->state != FLY;
        if (b->level == level && low == grounded) draw_bird(s, r, b);
    }
}

static void draw(void *p, SDL_Renderer *r) {
    static const int raySeq[4] = { 0, 1, 2, 1 };
    static const int flySeq[4] = { 0, 1, 2, 1 };
    static const int restSeq[6] = { 2, 2, 2, 1, 0, 1 };
    Birds *s = p;
    int u = s->u;
    SDL_SetRenderDrawColor(r, 0xC4, 0xEB, 0xFC, 255);
    SDL_RenderFillRect(r, NULL);
    SDL_Rect full = { 0, 0, s->lw * u, s->lh * u };
    SDL_RenderCopy(r, s->sky, NULL, &full);

    int scx = (int)(s->sunX * u), scy = (int)(s->sunY * u);
    SDL_Rect rd = { scx - RAYS_D * u / 2, scy - RAYS_D * u / 2, RAYS_D * u, RAYS_D * u };
    SDL_RenderCopy(r, s->rays[raySeq[(int)(s->t / 1.1f) % 4]], NULL, &rd);
    SDL_Rect sd = { scx - SUN_D * u / 2, scy - SUN_D * u / 2, SUN_D * u, SUN_D * u };
    SDL_RenderCopy(r, s->sun, NULL, &sd);

    for (int layer = 0; layer < 3; layer++)
        for (int i = 0; i < s->nclouds; i++) {
            const Cloud *c = &s->c[i];
            if (c->layer != layer) continue;
            int t = cloud_tex(s, layer);
            SDL_Texture *tex = s->cloud[c->shape];
            SDL_Rect d = { (int)c->x, (int)c->y, s->cloudW[c->shape] * t, s->cloudH[c->shape] * t };
            if (layer == 0) { SDL_SetTextureColorMod(tex, 222, 238, 255); SDL_SetTextureAlphaMod(tex, 215); }
            SDL_RenderCopy(r, tex, NULL, &d);
            if (layer == 0) { SDL_SetTextureColorMod(tex, 255, 255, 255); SDL_SetTextureAlphaMod(tex, 255); }
        }

    SDL_RenderCopy(r, s->hill[0], NULL, &full);
    draw_birds(s, r, 0, 0);
    SDL_RenderCopy(r, s->hill[1], NULL, &full);
    draw_birds(s, r, 1, 0);
    SDL_RenderCopy(r, s->hill[2], NULL, &full);

    for (int i = 0; i < s->nplants; i++) {
        const Plant *pl = &s->p[i];
        SDL_Rect d;
        SDL_Texture *tex;
        if (pl->kind == SUNFLOWER) {
            tex = s->sunflower[pl->frame];
            d = (SDL_Rect){ (int)(pl->x - 8.5f * u), (int)pl->y - SUN_H * u, 17 * u, SUN_H * u };
        } else {
            int th = pl->tall ? 15 : 11;
            tex = s->flower[pl->kind][pl->tall][pl->frame];
            d = (SDL_Rect){ (int)(pl->x - 4.5f * u), (int)pl->y - th * u, 9 * u, th * u };
        }
        SDL_RenderCopy(r, tex, NULL, &d);
    }
    draw_birds(s, r, 1, 1);
    for (int i = 0; i < MAX_FLIES; i++) {
        const Butterfly *f = &s->f[i];
        if (!f->on) continue;
        int fr = f->sitting ? restSeq[(int)(f->flap * 3) % 6] : flySeq[(int)(f->flap * 12) % 4];
        SDL_Rect d = { (int)(f->x - 5.5f * u), (int)(f->y - 4.5f * u), FLY_W * u, FLY_H * u };
        SDL_RenderCopy(r, s->fly[f->col][fr], NULL, &d);
    }
    draw_birds(s, r, 2, 0);
    draw_birds(s, r, 3, 0);
}

static void destroy(void *p) {
    Birds *s = p;
    SDL_DestroyTexture(s->sky);
    SDL_DestroyTexture(s->sun);
    for (int i = 0; i < 3; i++) {
        SDL_DestroyTexture(s->hill[i]);
        SDL_DestroyTexture(s->rays[i]);
        SDL_DestroyTexture(s->sunflower[i]);
    }
    for (int k = 0; k < KINDS; k++)
        for (int fr = 0; fr <= FLAP; fr++) SDL_DestroyTexture(s->bird[k][fr]);
    for (int fr = 0; fr < FLAP; fr++) SDL_DestroyTexture(s->gull[fr]);
    for (int c = 0; c < 4; c++)
        for (int fr = 0; fr < 3; fr++) SDL_DestroyTexture(s->fly[c][fr]);
    for (int k = 0; k < FLOWER_KINDS; k++)
        for (int t = 0; t < 2; t++)
            for (int fr = 0; fr < 3; fr++) SDL_DestroyTexture(s->flower[k][t][fr]);
    for (int i = 0; i < CLOUD_SHAPES; i++) SDL_DestroyTexture(s->cloud[i]);
    free(s->nearTop);
    free(s);
}

const Saver saver_birds = { "birds", "BIRDS", create, step, draw, destroy };
