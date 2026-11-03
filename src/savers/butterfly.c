/* Butterfly: the life cycle of a butterfly, close up in a sunny garden. A butterfly lands on its
   host plant and lays an egg. The egg wobbles and hatches; the tiny caterpillar eats its shell,
   then chews holes in the leaves and eats them down, growing bigger and shedding its skin as it
   goes. Fully grown, it crawls out onto a twig and hangs in a J (or ties itself on with a silk
   belt) and turns into a chrysalis, which slowly changes colour until the wings show through.
   The butterfly splits it open, pumps up its crumpled wings, dries them, flies around the garden
   and away, and the next butterfly comes to lay the next egg. Each round is a different kind on
   its own plant: a monarch on milkweed, a black swallowtail on parsley, a painted lady on thistle
   and a cabbage white on kale, each with its own caterpillar, chrysalis and wings. A ring in the
   corner shows the four stages and where we are. A bee, a ladybug, clouds and the sun keep it
   company, and nothing ever eats the caterpillar. Windowed keys: B skips to the butterfly coming
   out, F fast-forwards to it, I tells what is happening. */
#include "../engine.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define K 28                 /* points along a caterpillar, tail first */
#define TRAIL 600            /* where the caterpillar's head has been */
#define RIB 24               /* points along a leaf's midrib */
#define STEMN 24
#define MAXLEAF 10
#define MAXWP 200
#define MAXCRUMB 64
#define MAXFLOWER 24
#define NCLOUD 5
#define NTW 48               /* points along the twig: branch, then up the stick */
#define TOPF 4               /* top-view wing frames */
#define SUN_D 21
#define RAYS_D 47
#define RGBA(hex, a) (((uint32_t)(hex) << 8) | (uint32_t)(a))

/* ---------- the kinds of butterfly (content as data) ---------- */

enum { MONARCH, SWALLOWTAIL, PAINTED, WHITE, NSP };
enum { MILKWEED, PARSLEY, THISTLE, KALE };

typedef struct {
    const char *name, *plantName;
    int plant, girdled;      /* girdled: the chrysalis stands on a silk belt instead of hanging */
    float wing;              /* wing size, 1 = a monarch */
    float len, fat;          /* full-grown caterpillar length (logical pixels) and thickness */
    uint32_t egg, body, bodyDot, catLine, chLine;
    float chLen, chW;        /* chrysalis length and half-width */
} Species;

static const Species SPECIES[NSP] = {
    { "MONARCH", "MILKWEED", MILKWEED, 0, 1.00f, 31, 0.105f, 0xF4EEC4, 0x1E1A18, 0xFFFFFF, 0x141010, 0x2A6A48, 19, 6.0f },
    { "BLACK SWALLOWTAIL", "PARSLEY", PARSLEY, 1, 0.98f, 30, 0.11f, 0xF2E07A, 0x1A1820, 0xF2D040, 0x1A2A10, 0x3E6A2A, 20, 4.8f },
    { "PAINTED LADY", "THISTLE", THISTLE, 0, 0.84f, 26, 0.10f, 0xB8E09A, 0x5A4A3A, 0xC8A880, 0x141010, 0x4A3A2A, 17, 5.0f },
    { "CABBAGE WHITE", "KALE", KALE, 1, 0.80f, 24, 0.10f, 0xF2D25A, 0x3A3A42, 0xE8E8E8, 0x2E5018, 0x4A7A30, 17, 4.2f },
};

/* Kid-facing words, all in one place. */
static const char *const STAGE[4] = { "EGG", "CATERPILLAR", "CHRYSALIS", "BUTTERFLY" };

/* How long each instar (one skin) lasts, and how long the caterpillar is in it, as a share of full grown. */
static const float INSTAR_T[5] = { 7, 8, 9, 11, 13 };   /* seconds of eating */
static const float INSTAR_LEN[5] = { 0.24f, 0.36f, 0.52f, 0.72f, 0.92f };

enum { P_ARRIVE, P_LAY, P_EGG, P_HATCH, P_SHELL, P_LARVA, P_SILK, P_HANG, P_PUPATE, P_CHRYS,
       P_EMERGE, P_PUMP, P_DRY, P_FLY, P_FADE };
#define EGG_T 13.0f
#define LAY_T 3.4f
#define HATCH_T 3.6f
#define SILK_T 2.6f
#define HANG_T 6.5f
#define PUPATE_T 5.0f
#define CHRYS_T 36.0f
#define EMERGE_T 4.2f
#define PUMP_T 8.5f
#define DRY_T 6.5f
#define FADE_T 2.6f

enum { T_WALK, T_HOLE, T_TIP, T_REST, T_SHELL, T_DONE, T_IDLE };

/* ---------- small helpers ---------- */

typedef struct { float x, y; } V2;

static V2 v2(float x, float y) { V2 p; p.x = x; p.y = y; return p; }
static V2 vadd(V2 a, V2 b) { return v2(a.x + b.x, a.y + b.y); }
static V2 vsub(V2 a, V2 b) { return v2(a.x - b.x, a.y - b.y); }
static V2 vmul(V2 a, float k) { return v2(a.x * k, a.y * k); }
static float vlen(V2 a) { return sqrtf(a.x * a.x + a.y * a.y); }
static V2 vnorm(V2 a) { float l = vlen(a); return l > 1e-6f ? vmul(a, 1 / l) : v2(1, 0); }
static V2 vang(float a) { return v2(cosf(a), sinf(a)); }
static V2 vperp(V2 a) { return v2(-a.y, a.x); }
static float vdot(V2 a, V2 b) { return a.x * b.x + a.y * b.y; }
static float vcross(V2 a, V2 b) { return a.x * b.y - a.y * b.x; }
static V2 vlerp(V2 a, V2 b, float t) { return v2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t); }
static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static float smooth(float t) { t = clampf(t, 0, 1); return t * t * (3 - 2 * t); }
static float sinp(float a) { return fmaxf(0, sinf(a)); }   /* for powf, which turns a tiny negative into NaN */
static float wrap_angle(float a) { while (a > PI_F) a -= 2 * PI_F; while (a < -PI_F) a += 2 * PI_F; return a; }

static uint32_t mixc(uint32_t a, uint32_t b, float t) {
    t = clampf(t, 0, 1);
    uint32_t out = 0;
    for (int sh = 16; sh >= 0; sh -= 8) {
        float ca = (float)((a >> sh) & 255), cb = (float)((b >> sh) & 255);
        out |= (uint32_t)(ca + (cb - ca) * t + 0.5f) << sh;
    }
    return out;
}

static uint32_t hash3(int a, int b, int c) {
    uint32_t h = (uint32_t)a * 374761393u + (uint32_t)b * 668265263u + (uint32_t)c * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

static const float BAYER[4][4] = {
    { 0.0f / 16, 8.0f / 16, 2.0f / 16, 10.0f / 16 }, { 12.0f / 16, 4.0f / 16, 14.0f / 16, 6.0f / 16 },
    { 3.0f / 16, 11.0f / 16, 1.0f / 16, 9.0f / 16 }, { 15.0f / 16, 7.0f / 16, 13.0f / 16, 5.0f / 16 },
};

/* Mixes two colours with an ordered dither, for chunky gradients like the birds' sky. */
static uint32_t dither(uint32_t a, uint32_t b, float t, int x, int y, float step) {
    t = clampf(t, 0, 1);
    uint32_t out = 0;
    for (int sh = 16; sh >= 0; sh -= 8) {
        float ca = (float)((a >> sh) & 255), cb = (float)((b >> sh) & 255);
        float v = floorf((ca + (cb - ca) * t) / step + BAYER[y & 3][x & 3]) * step;
        out |= (uint32_t)clampf(v, 0, 255) << sh;
    }
    return rgb(out);
}

static int in_poly(const float *xs, const float *ys, int n, float x, float y) {
    int in = 0;
    for (int i = 0, j = n - 1; i < n; j = i++)
        if ((ys[i] > y) != (ys[j] > y) && x < xs[j] + (y - ys[j]) * (xs[i] - xs[j]) / (ys[i] - ys[j])) in = !in;
    return in;
}

static float seg_dist(float px, float py, float ax, float ay, float bx, float by) {
    float dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
    float t = l2 > 0 ? clampf(((px - ax) * dx + (py - ay) * dy) / l2, 0, 1) : 0;
    float ex = ax + dx * t - px, ey = ay + dy * t - py;
    return sqrtf(ex * ex + ey * ey);
}

static float poly_edge(const float *xs, const float *ys, int n, float x, float y) {
    float best = 1e9f;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        float d = seg_dist(x, y, xs[j], ys[j], xs[i], ys[i]);
        if (d < best) best = d;
    }
    return best;
}

/* A one-pixel line on a canvas. */
static void cline(Canvas *c, float x0, float y0, float x1, float y1, uint32_t col) {
    int n = (int)(fmaxf(fabsf(x1 - x0), fabsf(y1 - y0)) * 1.5f) + 1;
    for (int i = 0; i <= n; i++) {
        float t = (float)i / n;
        put(c, (int)floorf(x0 + (x1 - x0) * t), (int)floorf(y0 + (y1 - y0) * t), col);
    }
}

/* Inside pixels that touch the outside get `col`: a rim drawn within the shape. */
static void inner_edge(Canvas *c, uint32_t col) {
    Canvas o = canvas_new(c->w, c->h);
    for (int y = 0; y < c->h; y++)
        for (int x = 0; x < c->w; x++)
            if (get(c, x, y) && (!get(c, x - 1, y) || !get(c, x + 1, y) || !get(c, x, y - 1) || !get(c, x, y + 1)))
                o.px[y * c->w + x] = col;
    for (int i = 0; i < c->w * c->h; i++)
        if (o.px[i]) c->px[i] = o.px[i];
    free(o.px);
}

static SDL_Texture *keep_texture(SDL_Renderer *r, const Canvas *c, int streaming) {
    SDL_Texture *t = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA8888,
                                       streaming ? SDL_TEXTUREACCESS_STREAMING : SDL_TEXTUREACCESS_STATIC, c->w, c->h);
    if (t) {
        SDL_UpdateTexture(t, NULL, c->px, c->w * 4);
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(t, SDL_ScaleModeNearest);
    }
    return t;
}

/* ---------- moving sprites, drawn in code every frame onto a small canvas at world pixels ---------- */

typedef struct { Canvas c; SDL_Texture *t; int ox, oy, used; } Dyn;

static void dyn_make(SDL_Renderer *r, Dyn *d, int w, int h) {
    d->c = canvas_new(w, h);
    d->t = keep_texture(r, &d->c, 1);
}
static void dyn_start(Dyn *d, float cx, float cy) {
    d->ox = (int)floorf(cx) - d->c.w / 2;
    d->oy = (int)floorf(cy) - d->c.h / 2;
    memset(d->c.px, 0, (size_t)d->c.w * d->c.h * 4);
    d->used = 1;
}
static void dset(Dyn *d, int x, int y, uint32_t col) { put(&d->c, x - d->ox, y - d->oy, col); }
static uint32_t dget(const Dyn *d, int x, int y) { return get(&d->c, x - d->ox, y - d->oy); }
static void dline(Dyn *d, V2 a, V2 b, uint32_t col) { cline(&d->c, a.x - d->ox, a.y - d->oy, b.x - d->ox, b.y - d->oy, col); }
static void ddisc(Dyn *d, V2 p, float r, uint32_t col) { fill_ellipse(&d->c, p.x - d->ox, p.y - d->oy, r, r, col); }

/* ---------- the scene ---------- */

typedef struct {
    Canvas c;
    SDL_Texture *t;
    int ox, oy, dirty, side, bridge, full, left;
    float len, stemT;
    V2 rib[RIB];
    float w[RIB];            /* half width along the midrib */
    uint32_t base, chew;
} Leaf;

typedef struct { int on; V2 pts[K]; float R, len, t; int inst, falling; float vy; } Skin;
typedef struct { float x, y, vx, vy, life; uint32_t col; } Crumb;
typedef struct { float x, y, v; int shape; } Cloud;

typedef struct {
    V2 head, trail[TRAIL];
    int nt;
    float heading, len, R, instT, taskT, biteT, bobPh, bob, wave, walked, holeR, holeMax, shiver;
    int inst, task, then, afterRest, leaf, goal, moltPending, holeSide, visible;
    V2 wp[MAXWP];
    int nwp, wpi;
    V2 hole, pts[K];
} Cat;

typedef struct {         /* a butterfly flying, seen from above */
    int on, sitting, npath, pi;
    V2 pos, path[6];
    float flap, sitT, speed;
} Flyer;

typedef struct { int dir, sitting, away; V2 pos, target; float flap, timer; } Bee;
typedef struct { int leaf, flying; V2 pos, from, to; float heading, t, stay, legPh, s, v; } Ladybug;

typedef struct {
    SDL_Renderer *r;            /* kept to swap in the next plant between rounds */
    int w, h, lw, lh, ts, labels;
    float u, gy, t, phaseT, cycleT;
    int phase, sp, lastSp, cycles, stage;
    /* background */
    SDL_Texture *sky, *land, *sun, *rays[3], *cloudTex[4], *beeTex[2];
    int cloudW[4], cloudH[4];
    Cloud cloud[NCLOUD];
    V2 sunPos;
    /* the plant and the twig */
    SDL_Texture *plantTex, *twigTex;
    V2 stem[STEMN];
    float stemX;
    Leaf leaf[MAXLEAF];
    int nleaf, eggLeaf, bridge;
    V2 flower[MAXFLOWER], lawn[MAXFLOWER];
    int nflower, nlawn;
    V2 tw[NTW];
    float twd[NTW], dJ, dH;
    int ntw;
    V2 J, E, H, P0, A;          /* junction, branch end, hang point, chrysalis attach and axis */
    float stickX;
    /* the story */
    V2 eggPos, grip;
    int egg, laid;
    V2 from[K];                 /* the body as it was when it started to hang */
    float eggEaten, crack;
    Cat cat;
    Skin skin;
    Crumb crumb[MAXCRUMB];
    int ncrumb;
    Flyer fly;
    int side;                   /* the laying or hanging butterfly faces this way */
    float silk, girdle;
    Bee bee;
    Ladybug bug;
    /* sprites */
    SDL_Texture *top[TOPF];
    int topW, topH, topCx, topCy;
    Dyn catD, skinD, pupaD, sideD, eggD, bugD;
    /* the ring of stages */
    SDL_Texture *ringBg, *icon[4], *label[4];
    SDL_Rect panel;
    float ringCx, ringCy, ringRx, ringRy, iconHalf;
    V2 labelPos[4];             /* screen pixels */
    float fade;
    /* the zoom bubble that shows the tiny egg and caterpillar up close */
    float lensK;
    V2 lensAt, lensC;
    int drewEgg, drewCat, drewSkin;
} Garden;

/* ---------- sky, sun, clouds, garden ---------- */

static Canvas make_sun(void) {
    Canvas c = canvas_new(SUN_D, SUN_D);
    float m = SUN_D / 2.0f;
    fill_ellipse(&c, m, m, m, m, rgb(0xFFB62E));
    fill_ellipse(&c, m, m, m - 1.1f, m - 1.1f, rgb(0xFFD23B));
    fill_ellipse(&c, m - 1, m - 1, m * 0.68f, m * 0.68f, rgb(0xFFE36B));
    fill_ellipse(&c, m - 2.6f, m - 2.6f, 2.8f, 2.8f, rgb(0xFFF4B8));
    return c;
}

static Canvas make_rays(int frame) {
    Canvas c = canvas_new(RAYS_D, RAYS_D);
    float cx = RAYS_D / 2.0f;
    for (int k = 0; k < 12; k++) {
        float a = k * PI_F / 6 + PI_F / 12, d = 0.1f;
        int even = k % 2 == 0;
        float len = frame == 1 ? 19.5f : (even == (frame == 0) ? 23.0f : 16.5f);
        float xs[3] = { cx + cosf(a - d) * 12, cx + cosf(a) * len, cx + cosf(a + d) * 12 };
        float ys[3] = { cx + sinf(a - d) * 12, cx + sinf(a) * len, cx + sinf(a + d) * 12 };
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
            int down = 0, up = 0;
            while (down < 4 && get(&c, x, y + down + 1)) down++;
            while (up < 2 && get(&c, x, y - up - 1)) up++;
            uint32_t col = down == 0 ? 0xC3D7EE : down <= 2 ? 0xDCE8F6 : up == 0 ? 0xFFFFFF : 0xF4F8FE;
            put(&o, x, y, rgb(col));
        }
    free(c.px);
    return o;
}

static Canvas make_bee(int frame) {
    Canvas c = canvas_new(13, 10);
    float lift = frame ? 1.6f : 3.4f;
    fill_ellipse(&c, 5.6f, lift, 1.6f, 2.0f, rgb(0xDDF2FF));
    fill_ellipse(&c, 7.6f, lift + 0.4f, 1.3f, 1.7f, rgb(0xEAF8FF));
    fill_ellipse(&c, 5.8f, 6.4f, 3.7f, 2.6f, rgb(0xFFCE1F));
    fill_rect(&c, 4, 4, 4, 9, rgb(0x2A2216));
    fill_rect(&c, 6, 4, 6, 9, rgb(0x2A2216));
    fill_ellipse(&c, 9.8f, 6.2f, 1.7f, 1.7f, rgb(0x2A2216));
    put(&c, 1, 7, rgb(0x2A2216));
    put(&c, 10, 5, rgb(0xFFFFFF));
    outline(&c, rgb(0x5A4300));
    return c;
}

static Canvas make_sky(const Garden *s) {
    int lw = s->lw, lh = s->lh;
    Canvas c = canvas_new(lw, lh);
    for (int y = 0; y < lh; y++)
        for (int x = 0; x < lw; x++) {
            float t = y / (lh * 0.58f);
            uint32_t col = t < 0.5f ? mixc(0x3F9DE8, 0x7CC4F4, t / 0.5f) : mixc(0x7CC4F4, 0xD4F0FC, (t - 0.5f) / 0.5f);
            float dx = x - s->sunPos.x, dy = (y - s->sunPos.y) * 1.1f, d = sqrtf(dx * dx + dy * dy) / (lh * 0.55f);
            if (d < 1) col = mixc(col, 0xFFF2C0, (1 - d) * (1 - d) * 0.8f);
            put(&c, x, y, dither(col, col, 0, x, y, 8.0f));
        }
    return c;
}

/* Hills, a picket fence, a lawn with little flowers, and the soil of the garden bed. */
static Canvas make_land(Garden *s) {
    int lw = s->lw, lh = s->lh, gy = (int)s->gy;
    Canvas c = canvas_new(lw, lh);
    float p1 = frand(0, 6), p2 = frand(0, 6);
    for (int x = 0; x < lw; x++) {       /* far hills with lollipop trees */
        float xn = (float)x / lh;
        int top = (int)(lh * (0.40f + 0.035f * sinf(xn * 3.6f + p1) + 0.015f * sinf(xn * 9.0f + p2)));
        for (int y = top; y < lh; y++)
            put(&c, x, y, y == top ? rgb(0xB9E5C6) : dither(0x9AD3AE, 0x82BE98, (float)(y - top) / (lh * 0.15f), x, y, 1));
    }
    for (int i = 0; i < lw / 22 + 2; i++) {
        int x = irand(lw);
        float xn = (float)x / lh, top = lh * (0.40f + 0.035f * sinf(xn * 3.6f + p1) + 0.015f * sinf(xn * 9.0f + p2));
        float rr = frand(2.4f, 3.6f);
        fill_ellipse(&c, x + 0.5f, top - rr + 1.5f, rr, rr * 1.1f, rgb(0x79B894));
        fill_ellipse(&c, x - 0.3f, top - rr * 1.3f + 1.5f, rr * 0.5f, rr * 0.45f, rgb(0x93CFA8));
    }
    int lawnTop = (int)(lh * 0.62f);
    for (int x = 0; x < lw; x++) {       /* the lawn behind the fence */
        int top = lawnTop + (int)(1.5f * sinf(x * 0.05f + p1));
        for (int y = top; y < lh; y++)
            put(&c, x, y, y == top ? rgb(0x95D976) : dither(0x6CC25A, 0x4FA542, (float)(y - top) / (lh * 0.26f), x, y, 1));
    }
    /* a white picket fence */
    int fTop = (int)(lh * 0.47f), fBot = (int)(lh * 0.70f), r1 = (int)(lh * 0.53f), r2 = (int)(lh * 0.64f);
    for (int y = r1; y <= r1 + 2; y++)
        for (int x = 0; x < lw; x++) put(&c, x, y, rgb(y == r1 + 2 ? 0xC8BCA0 : 0xE6DCC4));
    for (int y = r2; y <= r2 + 2; y++)
        for (int x = 0; x < lw; x++) put(&c, x, y, rgb(y == r2 + 2 ? 0xC8BCA0 : 0xE6DCC4));
    for (int px = 1; px < lw; px += 9) {
        for (int y = fTop + 2; y <= fBot; y++)
            for (int x = px; x < px + 6; x++)
                put(&c, x, y, rgb(x == px ? 0xFFFFFF : x == px + 5 ? 0xCFC3A6 : 0xF4EEDE));
        fill_poly(&c, (float[]){ (float)px, px + 3.0f, px + 6.0f }, (float[]){ fTop + 2.5f, (float)fTop - 1, fTop + 2.5f }, 3, rgb(0xF4EEDE));
        for (int y = fTop; y <= fBot; y++) {
            if (get(&c, px - 1, y) != rgb(0xF4EEDE)) put(&c, px - 1, y, get(&c, px - 1, y));
        }
    }
    for (int x = 0; x < lw; x++)          /* grass in front of the fence foot */
        for (int k = 0; k < 2; k++)
            if (irand(3)) put(&c, x, fBot + 1 - k - irand(2), rgb(irand(2) ? 0x5DB84C : 0x7ACF5E));
    /* little flowers in the lawn, some of them for the bee */
    static const uint32_t pet[5] = { 0xFFFFFF, 0xF2393A, 0xFFD21F, 0xFF7EB6, 0xA77BFF };
    int nf = lw / 14;
    for (int i = 0; i < nf; i++) {
        int x = 3 + irand(lw - 6), y = fBot + 3 + irand(gy - fBot - 6 > 1 ? gy - fBot - 6 : 1), k = irand(5);
        for (int yy = y; yy < y + 5; yy++) put(&c, x, yy, rgb(0x3C9A36));
        put(&c, x + 1, y + 3, rgb(0x5CC046));
        fill_ellipse(&c, x + 0.5f, y - 0.5f, 2.2f, 2.2f, rgb(pet[k]));
        put(&c, x, y - 1, rgb(k == 2 ? 0xF08C0B : 0xFFC21F));
        if (s->nlawn < MAXFLOWER && irand(3) == 0 && (x < s->stemX - 18 || x > s->stemX + 18))
            s->lawn[s->nlawn++] = v2(x + 0.5f, y - 2.0f);
    }
    for (int x = 0; x < lw; x++) {       /* the garden bed */
        int top = gy + (int)(1.2f * sinf(x * 0.21f + p2));
        for (int y = top; y < lh; y++)
            put(&c, x, y, dither(0x8A5E38, 0x5E3E22, (float)(y - top) / (lh * 0.1f + 1), x, y, 1));
        if (irand(2)) put(&c, x, top - 1, rgb(0x5DB84C));
        if (irand(4) == 0) { put(&c, x, top - 1, rgb(0x7ACF5E)); put(&c, x, top - 2, rgb(0x7ACF5E)); }
    }
    for (int i = 0; i < lw / 6; i++) {   /* pebbles */
        int x = irand(lw), y = gy + 3 + irand(lh - gy - 3 > 1 ? lh - gy - 3 : 1);
        put(&c, x, y, rgb(irand(2) ? 0xA8927A : 0x705038));
        if (irand(2)) put(&c, x + 1, y, rgb(0xC4AE94));
    }
    return c;
}

/* ---------- the host plant ---------- */

static V2 stem_at(const Garden *s, float t) {
    float f = clampf(t, 0, 1) * (STEMN - 1);
    int i = (int)f;
    if (i >= STEMN - 1) return s->stem[STEMN - 1];
    return vlerp(s->stem[i], s->stem[i + 1], f - i);
}

static V2 rib_at(const Leaf *l, float s) {
    float f = clampf(s, 0, 1) * (RIB - 1);
    int i = (int)f;
    if (i >= RIB - 1) return l->rib[RIB - 1];
    return vlerp(l->rib[i], l->rib[i + 1], f - i);
}

static float w_at(const Leaf *l, float s) {
    float f = clampf(s, 0, 1) * (RIB - 1);
    int i = (int)f;
    if (i >= RIB - 1) return l->w[RIB - 1];
    return lerpf(l->w[i], l->w[i + 1], f - i);
}

static V2 rib_dir(const Leaf *l, float s) {
    return vnorm(vsub(rib_at(l, fminf(1, s + 0.04f)), rib_at(l, fmaxf(0, s - 0.04f))));
}

/* The nearest point on the midrib: returns s, with the signed distance across in *v. */
static float rib_nearest(const Leaf *l, V2 p, float *v, float *dist) {
    float best = 1e9f, bs = 0, bv = 0;
    for (int i = 0; i < RIB - 1; i++) {
        V2 a = l->rib[i], b = l->rib[i + 1], ab = vsub(b, a);
        float l2 = vdot(ab, ab), t = l2 > 0 ? clampf(vdot(vsub(p, a), ab) / l2, 0, 1) : 0;
        V2 q = vadd(a, vmul(ab, t));
        float d = vlen(vsub(p, q));
        if (d < best) { best = d; bs = (i + t) / (RIB - 1); bv = vcross(ab, vsub(p, a)) > 0 ? d : -d; }
    }
    if (v) *v = bv;
    if (dist) *dist = best;
    return bs;
}

static int leaf_has(const Leaf *l, float x, float y) {
    return get(&l->c, (int)floorf(x) - l->ox, (int)floorf(y) - l->oy) != 0;
}

/* Half width of a leaf blade at s (0 base .. 1 tip); *spike is set where a thistle has a spine. */
static float blade_w(int kind, float s, float L, float v, int *spike) {
    *spike = 0;
    if (kind == MILKWEED) {
        if (s < 0.07f) return 0.9f;
        float q = (s - 0.07f) / 0.93f;
        return fmaxf(0.6f, L * 0.2f * powf(sinp(PI_F * fminf(q * 0.96f + 0.02f, 0.999f)), 0.6f));
    }
    if (kind == THISTLE) {
        if (s < 0.05f) return 1.0f;
        float q = (s - 0.05f) / 0.95f, k = s * L / 6.5f, f = k - floorf(k);
        float w = L * 0.18f * powf(sinp(PI_F * fminf(q, 0.999f)), 0.75f) * (0.5f + 0.5f * fabsf(sinf(PI_F * k)));
        if (fabsf(f - 0.5f) < 0.07f && fabsf(v) > w && fabsf(v) < w + 2.0f) *spike = 1;
        return fmaxf(0.7f, w);
    }
    /* kale: broad and wavy */
    if (s < 0.06f) return 1.1f;
    float q = (s - 0.06f) / 0.94f;
    return fmaxf(0.8f, L * 0.27f * powf(sinp(PI_F * fminf(powf(q, 0.85f), 0.999f)), 0.55f) * (0.86f + 0.14f * sinf(s * L * 0.75f)));
}

/* Rasterizes a leaf from its midrib: a blade (or parsley's leaflets), veins and a rim. */
static void make_leaf(Garden *s, SDL_Renderer *r, Leaf *l, int kind) {
    static const uint32_t base[4] = { 0x7DBF5A, 0x4FB043, 0x4E9A48, 0x6FAE8E };
    static const uint32_t dark[4] = { 0x64A64A, 0x3E9637, 0x3E823C, 0x5A9478 };
    static const uint32_t rim[4] = { 0x3F7A34, 0x2D6E28, 0x2C5E2C, 0x3D6E58 };
    static const uint32_t ribc[4] = { 0xD2EAA8, 0x86D060, 0xE6F2DA, 0xD6EDD8 };
    static const uint32_t vein[4] = { 0xA2D47E, 0x6CC650, 0x8CC480, 0xA2CFB4 };
    float L = l->len, maxw = kind == KALE ? L * 0.3f : kind == PARSLEY ? L * 0.26f : L * 0.24f;
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (int i = 0; i < RIB; i++) {
        x0 = fminf(x0, l->rib[i].x); x1 = fmaxf(x1, l->rib[i].x);
        y0 = fminf(y0, l->rib[i].y); y1 = fmaxf(y1, l->rib[i].y);
    }
    l->ox = (int)floorf(x0 - maxw - 4);
    l->oy = (int)floorf(y0 - maxw - 4);
    l->c = canvas_new((int)ceilf(x1 - x0 + 2 * maxw + 9), (int)ceilf(y1 - y0 + 2 * maxw + 9));
    l->base = base[kind];
    l->chew = mixc(base[kind], 0xF0E890, 0.5f);
    /* parsley leaflets: centres, directions and sizes */
    V2 lc[7], ld[7];
    int nl = 0;
    float lsz = L * 0.16f;
    if (kind == PARSLEY) {
        static const float at[3] = { 0.42f, 0.66f, 0.86f };
        for (int i = 0; i < 3; i++)
            for (int sd = -1; sd <= 1; sd += 2) {
                V2 f = rib_dir(l, at[i]), n = vmul(vperp(f), (float)sd);
                ld[nl] = vnorm(vadd(n, vmul(f, 0.8f)));
                lc[nl] = vadd(rib_at(l, at[i]), vmul(ld[nl], lsz * 0.85f));
                nl++;
            }
        ld[nl] = rib_dir(l, 1);
        lc[nl++] = vadd(rib_at(l, 1), vmul(ld[6], lsz * 0.55f));
    }
    for (int i = 0; i < RIB; i++) {
        float sv = (float)i / (RIB - 1);
        int sp;
        l->w[i] = kind == PARSLEY ? (sv > 0.35f ? lsz * 1.4f : 1.0f) : blade_w(kind, sv, L, 0, &sp);
    }
    int count = 0;
    for (int y = 0; y < l->c.h; y++)
        for (int x = 0; x < l->c.w; x++) {
            V2 p = v2(l->ox + x + 0.5f, l->oy + y + 0.5f);
            float v, d, sv = rib_nearest(l, p, &v, &d);
            uint32_t col = 0;
            if (kind == PARSLEY) {
                int in = d < 0.75f;
                int leaflet = 0;
                for (int k = 0; k < nl && !in; k++) {
                    for (int lobe = 0; lobe < 3 && !in; lobe++) {
                        float a = atan2f(ld[k].y, ld[k].x) + (lobe - 1) * 0.75f;
                        V2 cc = vadd(lc[k], vmul(vang(a), lsz * (lobe == 1 ? 0.5f : 0.38f)));
                        V2 e = vsub(p, cc);
                        float ang = atan2f(e.y, e.x), rr = lsz * 0.42f * (0.86f + 0.14f * cosf(ang * 6));
                        if (vlen(e) < rr) { in = 1; leaflet = 1; }
                    }
                }
                if (in) {
                    col = leaflet ? (vcross(ld[0], vsub(p, lc[0])) > 0 && (x + y) % 5 == 0 ? dark[kind] : base[kind]) : ribc[kind];
                    if (leaflet && d < 0.75f) col = ribc[kind];
                }
            } else {
                int spike;
                float w = blade_w(kind, sv, L, v, &spike);
                if (spike && sv < 0.97f) col = 0xF2E8B0;
                else if (fabsf(v) <= w && !(sv <= 0.0f && d > 1.0f) && !(sv >= 1.0f && d > 0.8f)) {
                    float rel = fabsf(v) / w;
                    col = v < 0 ? base[kind] : dark[kind];
                    if (rel > 0.55f && rel < 0.9f && ((x * 3 + y * 5) % 11 == 0)) col = mixc(col, 0xFFFFFF, 0.15f);
                    float along = sv * L - fabsf(v) * 0.9f;
                    if (fabsf(v) > 1.0f && rel < 0.85f && fmodf(along + 40, kind == THISTLE ? 6.5f : 5.5f) < 0.8f) col = vein[kind];
                    if (fabsf(v) < (kind == KALE || kind == MILKWEED ? 0.9f : 0.7f)) col = ribc[kind];
                }
            }
            if (col) { put(&l->c, x, y, rgb(col)); count++; }
        }
    inner_edge(&l->c, rgb(rim[kind]));
    l->full = l->left = count;
    l->t = keep_texture(r, &l->c, 1);
    l->dirty = 0;
    (void)s;
}

static void add_flower(Garden *s, V2 p) {
    if (s->nflower < MAXFLOWER) s->flower[s->nflower++] = p;
}

/* Stem and flowers of the host plant, on one canvas the size of the screen. */
static Canvas make_plant(Garden *s, int kind) {
    Canvas c = canvas_new(s->lw, s->lh);
    float th = kind == PARSLEY ? 1.2f : 1.7f;
    static const uint32_t lite[4] = { 0x9CCC74, 0x5DBA4A, 0x5EA052, 0x9CC8B0 };
    static const uint32_t dk[4] = { 0x5E944A, 0x3A8A30, 0x3A6E34, 0x6A9A84 };
    for (int i = 0; i < 400; i++) {
        V2 p = stem_at(s, i / 399.0f);
        fill_ellipse(&c, p.x, p.y, th, th, rgb(dk[kind]));
    }
    for (int i = 0; i < 400; i++) {
        V2 p = stem_at(s, i / 399.0f);
        fill_ellipse(&c, p.x - 0.5f, p.y, th * 0.55f, th * 0.55f, rgb(lite[kind]));
    }
    if (kind == THISTLE)                                  /* spiny wings on the stem */
        for (int i = 0; i < 26; i++) {
            V2 p = stem_at(s, 0.05f + i * 0.035f);
            int sd = i % 2 ? 1 : -1;
            put(&c, (int)(p.x + sd * (th + 1)), (int)p.y, rgb(0xF2E8B0));
        }
    V2 top = s->stem[STEMN - 1];
    s->nflower = 0;
    if (kind == MILKWEED) {                               /* balls of little pink flowers */
        V2 heads[3] = { v2(top.x, top.y - 4), v2(top.x - 8, top.y + 9), v2(top.x + 8, top.y + 12) };
        for (int h = 0; h < 3; h++) {
            V2 hp = heads[h];
            float rr = h ? 4.6f : 5.6f;
            cline(&c, hp.x, hp.y + rr * 0.6f, h ? stem_at(s, 0.9f - h * 0.04f).x : top.x, h ? stem_at(s, 0.9f - h * 0.04f).y : top.y, rgb(dk[kind]));
            for (int k = 0; k < 40; k++) {
                float a = frand(0, 2 * PI_F), d = sqrtf(frand(0, 1)) * rr;
                float x = hp.x + cosf(a) * d, y = hp.y + sinf(a) * d * 0.85f;
                uint32_t col = y < hp.y - rr * 0.2f ? 0xF4B8D2 : y < hp.y + rr * 0.3f ? 0xE58BB6 : 0xC0507F;
                fill_rect(&c, (int)x, (int)y, (int)x + 1, (int)y, rgb(col));
                put(&c, (int)x, (int)y + 1, rgb(0xA8406C));
            }
            add_flower(s, v2(hp.x, hp.y - rr * 0.7f));
        }
    } else if (kind == PARSLEY) {                         /* yellow umbrellas, like dill */
        V2 heads[2] = { v2(top.x, top.y - 2), v2(top.x - 10, top.y + 10) };
        cline(&c, heads[1].x, heads[1].y, stem_at(s, 0.86f).x, stem_at(s, 0.86f).y, rgb(dk[kind]));
        for (int h = 0; h < 2; h++) {
            float rr = h ? 6.0f : 8.0f;
            for (int k = 0; k < 9; k++) {
                float f = k / 8.0f - 0.5f;
                V2 e = v2(heads[h].x + f * 2 * rr, heads[h].y - rr * 0.75f + f * f * rr * 1.2f);
                cline(&c, heads[h].x, heads[h].y, e.x, e.y, rgb(0x6CB84A));
                fill_ellipse(&c, e.x, e.y - 0.5f, 1.4f, 1.1f, rgb(0xE8D83A));
                put(&c, (int)e.x, (int)e.y - 1, rgb(0xF8EC7A));
                put(&c, (int)e.x + 1, (int)e.y, rgb(0xB8A820));
            }
            add_flower(s, v2(heads[h].x, heads[h].y - rr * 0.8f));
        }
    } else if (kind == THISTLE) {                         /* purple brushes on spiny cups */
        V2 heads[3] = { v2(top.x, top.y - 2), v2(top.x - 9, top.y + 10), v2(top.x + 9, top.y + 15) };
        for (int h = 0; h < 3; h++) {
            V2 hp = heads[h];
            if (h) cline(&c, hp.x, hp.y + 2, stem_at(s, 0.88f - h * 0.05f).x, stem_at(s, 0.88f - h * 0.05f).y, rgb(dk[kind]));
            fill_ellipse(&c, hp.x, hp.y + 1.5f, 3.4f, 3.0f, rgb(0x5E9A50));
            for (int k = 0; k < 6; k++) put(&c, (int)(hp.x - 3 + k), (int)(hp.y + 1 + (k % 2)), rgb(0x3E6E36));
            put(&c, (int)hp.x - 4, (int)hp.y + 2, rgb(0xF2E8B0));
            put(&c, (int)hp.x + 4, (int)hp.y + 2, rgb(0xF2E8B0));
            for (int k = -3; k <= 3; k++) {
                int len = 4 - abs(k) / 2 + (k & 1);
                for (int j = 0; j < len; j++)
                    put(&c, (int)hp.x + k, (int)hp.y - 1 - j, rgb(j == len - 1 ? 0xD890EA : (k & 1) ? 0x9A48C0 : 0xB862D8));
            }
            add_flower(s, v2(hp.x, hp.y - 4));
        }
    } else {                                              /* kale gone to flower: small yellow crosses */
        for (int k = 0; k < 9; k++) {
            V2 p = v2(top.x + (k % 2 ? 2.5f : -2.5f) + frand(-1, 1), top.y + 14 - k * 2.4f);
            cline(&c, p.x, p.y, top.x, p.y + 1.5f, rgb(dk[kind]));
            put(&c, (int)p.x, (int)p.y, rgb(0xE0A820));
            put(&c, (int)p.x - 1, (int)p.y, rgb(0xF5D93A)); put(&c, (int)p.x + 1, (int)p.y, rgb(0xF5D93A));
            put(&c, (int)p.x, (int)p.y - 1, rgb(0xF5D93A)); put(&c, (int)p.x, (int)p.y + 1, rgb(0xF5D93A));
        }
        fill_ellipse(&c, top.x, top.y - 1.5f, 1.4f, 1.8f, rgb(0x8CB860));
        add_flower(s, v2(top.x, top.y + 4));
        add_flower(s, v2(top.x + 2, top.y + 10));
    }
    return c;
}

/* Leaves along the stem: opposite pairs for milkweed, one at a time for the others. */
static void make_leaves(Garden *s, SDL_Renderer *r, int kind) {
    float slotT[MAXLEAF];
    int slotSide[MAXLEAF], n = 0;
    if (kind == MILKWEED) {
        static const float ts[4] = { 0.18f, 0.40f, 0.60f, 0.78f };
        for (int i = 0; i < 4; i++) { slotT[n] = ts[i]; slotSide[n++] = -1; slotT[n] = ts[i]; slotSide[n++] = 1; }
    } else {
        int m = kind == KALE ? 6 : 7;
        for (int i = 0; i < m; i++) {
            slotT[n] = kind == KALE ? 0.12f + i * 0.13f : 0.12f + i * 0.12f;
            slotSide[n++] = i % 2 ? -1 : 1;
        }
    }
    float baseLen = kind == MILKWEED ? 50 : kind == PARSLEY ? 44 : kind == THISTLE ? 50 : 48;
    s->nleaf = n;
    s->bridge = -1;
    float bestB = 9;
    for (int i = 0; i < n; i++) {
        Leaf *l = &s->leaf[i];
        memset(l, 0, sizeof *l);
        float t = slotT[i];
        l->stemT = t;
        l->side = slotSide[i];
        l->len = baseLen * (1.08f - 0.42f * t) * frand(0.94f, 1.06f);
        float ang = (0.42f - t * 0.75f) + frand(-0.06f, 0.06f);   /* radians below level: low leaves droop */
        V2 B = stem_at(s, t);
        V2 dir = v2(cosf(ang) * l->side, sinf(ang));
        V2 T = vadd(B, vmul(dir, l->len));
        V2 C = vadd(vlerp(B, T, 0.5f), v2(0, l->len * 0.12f));
        for (int k = 0; k < RIB; k++) {
            float q = (float)k / (RIB - 1);
            l->rib[k] = vadd(vadd(vmul(B, (1 - q) * (1 - q)), vmul(C, 2 * q * (1 - q))), vmul(T, q * q));
        }
        if (l->side > 0 && fabsf(t - 0.6f) < bestB) { bestB = fabsf(t - 0.6f); s->bridge = i; }
    }
    /* the bridge leaf reaches toward the twig, a little longer and level */
    Leaf *b = &s->leaf[s->bridge];
    b->bridge = 1;
    b->len = baseLen * 1.05f;
    {
        V2 B = stem_at(s, b->stemT), T = vadd(B, v2(b->len * 0.99f, -b->len * 0.08f));
        V2 C = vadd(vlerp(B, T, 0.5f), v2(0, b->len * 0.1f));
        for (int k = 0; k < RIB; k++) {
            float q = (float)k / (RIB - 1);
            b->rib[k] = vadd(vadd(vmul(B, (1 - q) * (1 - q)), vmul(C, 2 * q * (1 - q))), vmul(T, q * q));
        }
    }
    for (int i = 0; i < n; i++) make_leaf(s, r, &s->leaf[i], kind);
    /* the egg goes on a left leaf in the middle */
    float best = 9;
    for (int i = 0; i < n; i++)
        if (s->leaf[i].side < 0 && fabsf(s->leaf[i].stemT - 0.45f) < best) { best = fabsf(s->leaf[i].stemT - 0.45f); s->eggLeaf = i; }
}

/* A dry twig: a stick up from the ground with a branch that reaches the bridge leaf's tip. */
static Canvas make_twig(Garden *s, float lenMax) {
    Leaf *b = &s->leaf[s->bridge];
    s->E = vadd(b->rib[RIB - 1], v2(1.0f, 0.5f));
    float jx = fminf(s->E.x + fmaxf(0.17f * s->lw, 2.1f * lenMax), s->lw * 0.84f);
    s->J = v2(jx, s->E.y - 5);
    s->stickX = jx;
    int nb = 16;
    s->ntw = 0;
    V2 C = vadd(vlerp(s->E, s->J, 0.5f), v2(0, -3));
    for (int i = 0; i < nb; i++) {
        float q = (float)i / (nb - 1);
        s->tw[s->ntw++] = vadd(vadd(vmul(s->E, (1 - q) * (1 - q)), vmul(C, 2 * q * (1 - q))), vmul(s->J, q * q));
    }
    float topY = s->J.y - (lenMax + 26);
    for (int i = 1; i < NTW - nb; i++) {
        float q = (float)i / (NTW - nb - 1);
        s->tw[s->ntw++] = v2(jx - 1.6f * q, s->J.y + (topY - s->J.y) * q);
    }
    s->twd[0] = 0;
    for (int i = 1; i < s->ntw; i++) s->twd[i] = s->twd[i - 1] + vlen(vsub(s->tw[i], s->tw[i - 1]));
    s->dJ = s->twd[nb - 1];
    s->dH = s->dJ * 0.42f;
    Canvas c = canvas_new(s->lw, s->lh);
    /* the stick, from the soil to the top, then the branch */
    for (float y = s->gy + 3; y > topY; y -= 0.25f) {
        float q = (s->J.y - y) / (s->J.y - topY);
        float x = y > s->J.y ? jx + 1.0f * (y - s->J.y) / (s->gy + 3 - s->J.y) : jx - 1.6f * q;
        float th = y > s->J.y ? 1.8f : 1.8f - 0.7f * q;
        fill_ellipse(&c, x, y, th, 0.6f, rgb(0x6A4224));
        fill_ellipse(&c, x - 0.4f, y, th * 0.5f, 0.6f, rgb(0x9A6A3E));
    }
    V2 sp0 = v2(jx - 1.3f, topY + 14), sp1 = v2(jx + 5, topY + 6);   /* a little side shoot */
    for (int i = 0; i <= 20; i++) fill_ellipse(&c, lerpf(sp0.x, sp1.x, i / 20.0f), lerpf(sp0.y, sp1.y, i / 20.0f), 0.8f, 0.8f, rgb(0x7A5030));
    for (int i = 0; i < 200; i++) {
        float q = i / 199.0f;
        V2 p = vadd(vadd(vmul(s->E, (1 - q) * (1 - q)), vmul(C, 2 * q * (1 - q))), vmul(s->J, q * q));
        float th = 0.9f + 0.5f * q;
        fill_ellipse(&c, p.x, p.y, th, th, rgb(0x6A4224));
        fill_ellipse(&c, p.x, p.y - 0.4f, th * 0.55f, th * 0.45f, rgb(0x9A6A3E));
    }
    outline(&c, rgb(0x3E2614));
    s->H = vadd(s->tw[0], v2(0, 0));
    {   /* H: the point at distance dH along the twig */
        float d = s->dH;
        for (int i = 1; i < s->ntw; i++)
            if (s->twd[i] >= d) { s->H = vlerp(s->tw[i - 1], s->tw[i], (d - s->twd[i - 1]) / (s->twd[i] - s->twd[i - 1] + 1e-6f)); break; }
    }
    return c;
}

static V2 twig_at(const Garden *s, float d) {
    if (d <= 0) return s->tw[0];
    for (int i = 1; i < s->ntw; i++)
        if (s->twd[i] >= d) return vlerp(s->tw[i - 1], s->tw[i], (d - s->twd[i - 1]) / (s->twd[i] - s->twd[i - 1] + 1e-6f));
    return s->tw[s->ntw - 1];
}

/* ---------- caterpillars ---------- */

/* The colour at t (0 tail .. 1 head) and v (-1..1 across the back) of a caterpillar in instar inst. */
static uint32_t cat_color(int sp, int inst, float t, float v, float R, float len, int *headOut) {
    float hf = clampf(1.5f * R / len, 0.07f, 0.2f);
    int head = t > 1 - hf;
    float av = fabsf(v), f = t / (1 - hf) * 11, fr = f - floorf(f);
    int seg = (int)f;
    uint32_t c;
    *headOut = head;
    switch (sp) {
    case MONARCH:
        if (inst == 0) { c = head ? 0x1A1614 : 0xD6DCC8; break; }
        if (head) { c = (t > 1 - hf * 0.45f) || av > 0.6f ? 0x1A1614 : 0xF2D040; break; }
        c = fr < 0.26f ? 0x18161A : fr < 0.44f ? 0xF6F4EA : fr < 0.76f ? 0xF2C81E : 0xF6F4EA;
        break;
    case SWALLOWTAIL:
        if (inst < 3) {
            if (head) { c = 0x1E1A16; break; }
            c = (t > 0.4f && t < 0.56f) || t < 0.1f ? 0xF0F0E6 : 0x2E2A26;
            if (fabsf(fr - 0.5f) < 0.15f && av > 0.55f && av < 0.85f) c = 0xE08A30;
            break;
        }
        if (head) { c = av < 0.3f ? 0x1E1A16 : 0x92CE58; break; }
        c = 0x8CD050;
        if (fr > 0.40f && fr < 0.64f) {
            c = 0x141414;
            if (fr > 0.45f && fr < 0.59f && ((av > 0.4f && av < 0.75f) || av < 0.12f)) c = 0xF5B830;
        }
        break;
    case PAINTED:
        if (inst < 2) { c = head ? 0x3A3020 : 0xC6D690; break; }
        if (head) { c = 0x1A1612; break; }
        c = (hash3(seg, (int)(v * 3 + 3), 9) & 3) == 0 ? 0x5A5048 : 0x3A3430;
        if (av > 0.52f && av < 0.82f && fr < 0.75f) c = 0xE8D060;
        break;
    default:   /* cabbage white */
        if (head) { c = 0x6EA840; break; }
        c = (hash3(seg, (int)(fr * 4), (int)(v * 4 + 4)) & 3) == 0 ? 0x6AA03C : 0x7EB848;
        if (inst > 0 && av < 0.14f) c = 0xD8E070;
        if (inst > 1 && av > 0.7f && av < 0.92f && fr < 0.45f) c = 0xE6E070;
        break;
    }
    if (av > 0.78f && !head) c = mixc(c, 0, 0.18f);
    return c;
}

static float cat_radius(float t, float R, float bulgeAt) {
    float r = R * powf(fminf(1, (t + 0.06f) / 0.24f), 0.6f);
    if (t > 0.93f) r *= 0.88f;
    r *= 1 + 0.12f * expf(-(t - bulgeAt) * (t - bulgeAt) / 0.006f);
    return r;
}

/* Draws a caterpillar along pts (tail first) into a sprite. skin: a shed skin, pale and crumpled. */
static void draw_cat(Dyn *d, const V2 *pts, float R, float len, int sp, int inst, float bulgeAt, int skin) {
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (int k = 0; k < K; k++) {
        x0 = fminf(x0, pts[k].x); x1 = fmaxf(x1, pts[k].x);
        y0 = fminf(y0, pts[k].y); y1 = fmaxf(y1, pts[k].y);
    }
    int ix0 = (int)floorf(x0 - R - 1), ix1 = (int)ceilf(x1 + R + 1), iy0 = (int)floorf(y0 - R - 1), iy1 = (int)ceilf(y1 + R + 1);
    for (int y = iy0; y <= iy1; y++)
        for (int x = ix0; x <= ix1; x++) {
            V2 p = v2(x + 0.5f, y + 0.5f);
            float best = 1e9f, bt = 0, bs = 1;
            for (int k = 0; k < K - 1; k++) {
                V2 a = pts[k], ab = vsub(pts[k + 1], a);
                float l2 = vdot(ab, ab), q = l2 > 1e-6f ? clampf(vdot(vsub(p, a), ab) / l2, 0, 1) : 0;
                float dd = vlen(vsub(p, vadd(a, vmul(ab, q))));
                if (dd < best) { best = dd; bt = (k + q) / (K - 1); bs = vcross(ab, vsub(p, a)) >= 0 ? 1 : -1; }
            }
            float rr = cat_radius(bt, R, bulgeAt);
            if (best > rr) continue;
            int head;
            uint32_t c = cat_color(sp, inst, bt, bs * best / rr, R, len, &head);
            if (skin) c = mixc(c, 0xD8D0B8, 0.7f);
            dset(d, x, y, rgb(c));
        }
    if (skin) { outline(&d->c, rgb(0xA89C80)); return; }
    outline(&d->c, rgb(SPECIES[sp].catLine));
    /* extras: the monarch's black feelers, the painted lady's spines, little eyes */
    V2 hd = vnorm(vsub(pts[K - 1], pts[K - 3])), hn = vperp(hd);
    if (sp == MONARCH && inst > 0) {
        float tt[2] = { 0.80f, 0.16f };
        for (int f = 0; f < 2; f++) {
            int k = (int)(tt[f] * (K - 1));
            V2 dir = vnorm(vsub(pts[k + 1], pts[k])), n = vperp(dir);
            float lenF = fmaxf(1.6f, R * (f ? 1.0f : 1.5f));
            for (int sd = -1; sd <= 1; sd += 2) {
                V2 b0 = vadd(pts[k], vmul(n, sd * R * 0.8f));
                V2 tip = vadd(b0, vadd(vmul(n, sd * lenF * 0.7f), vmul(dir, (f ? -1 : 1) * lenF * 0.75f)));
                dline(d, b0, tip, rgb(0x141010));
            }
        }
    }
    if (sp == PAINTED && inst > 1)
        for (int k = 3; k < K - 3; k += 3) {
            V2 dir = vnorm(vsub(pts[k + 1], pts[k])), n = vperp(dir);
            for (int sd = -1; sd <= 1; sd += 2) {
                V2 b0 = vadd(pts[k], vmul(n, sd * (R + 0.6f)));
                dset(d, (int)floorf(b0.x), (int)floorf(b0.y), rgb(0x2A2018));
                V2 b1 = vadd(b0, vmul(n, sd * 1.0f));
                dset(d, (int)floorf(b1.x), (int)floorf(b1.y), rgb(0xE8D8A0));
            }
        }
    if (R >= 2.2f)
        for (int sd = -1; sd <= 1; sd += 2) {
            V2 e = vadd(vadd(pts[K - 1], vmul(hd, R * 0.35f)), vmul(hn, sd * R * 0.45f));
            dset(d, (int)floorf(e.x), (int)floorf(e.y), rgb(0xFFFFFF));
        }
}

/* Body points from the trail behind the head (tail first). */
static void body_from_trail(const Cat *c, float L, V2 *pts) {
    pts[K - 1] = c->head;
    V2 prev = c->head;
    int i = c->nt - 1;
    float acc = 0;
    for (int k = K - 2; k >= 0; k--) {
        float need = L * (float)(K - 1 - k) / (K - 1);
        while (i >= 0) {
            float seg = vlen(vsub(c->trail[i], prev));
            if (acc + seg >= need && seg > 1e-6f) {
                prev = vlerp(prev, c->trail[i], (need - acc) / seg);
                acc = need;
                break;
            }
            acc += seg;
            prev = c->trail[i];
            i--;
        }
        pts[k] = prev;
    }
}

/* A body hanging from H: straight down, curling up into a J as curl goes to 1. */
static void body_hang(V2 H, float L, float alpha, float curl, V2 *pts) {
    float ds = L / (K - 1), th = alpha;
    V2 p = H;
    for (int k = 0; k < K; k++) {
        pts[k] = p;
        if (k * ds > 0.5f * L) th += curl * 2.25f / (0.5f * L) * ds;
        p = vadd(p, vmul(vang(th), ds));
    }
}

static void trail_push(Cat *c) {
    if (c->nt > 0 && vlen(vsub(c->head, c->trail[c->nt - 1])) < 0.35f) return;
    if (c->nt >= TRAIL) {
        memmove(c->trail, c->trail + 200, sizeof(V2) * (TRAIL - 200));
        c->nt -= 200;
    }
    c->trail[c->nt++] = c->head;
}

static void cat_reset(Cat *c, V2 at, float heading) {
    memset(c, 0, sizeof *c);
    c->head = at;
    c->heading = heading;
    c->trail[0] = at;
    c->nt = 1;
    c->leaf = -1;
    c->task = T_IDLE;
}

static void wp_add(Cat *c, V2 p) { if (c->nwp < MAXWP) c->wp[c->nwp++] = p; }

static void wp_rib(Cat *c, const Leaf *l, float s0, float s1) {
    int n = (int)(fabsf(s1 - s0) * l->len / 2.5f) + 1;
    for (int i = 1; i <= n; i++) wp_add(c, rib_at(l, lerpf(s0, s1, (float)i / n)));
}

static void wp_stem(Garden *s, Cat *c, float t0, float t1) {
    float L = fabsf(t1 - t0) * vlen(vsub(s->stem[STEMN - 1], s->stem[0]));
    int n = (int)(L / 2.5f) + 1;
    for (int i = 1; i <= n; i++) wp_add(c, stem_at(s, lerpf(t0, t1, (float)i / n)));
}

static void route_to(Garden *s, int leaf, float sTarget) {
    Cat *c = &s->cat;
    c->nwp = c->wpi = 0;
    if (c->leaf == leaf) wp_rib(c, &s->leaf[leaf], rib_nearest(&s->leaf[leaf], c->head, NULL, NULL), sTarget);
    else {
        float t0 = 0.5f;
        if (c->leaf >= 0) {
            wp_rib(c, &s->leaf[c->leaf], rib_nearest(&s->leaf[c->leaf], c->head, NULL, NULL), 0);
            t0 = s->leaf[c->leaf].stemT;
        }
        wp_stem(s, c, t0, s->leaf[leaf].stemT);
        wp_rib(c, &s->leaf[leaf], 0, sTarget);
    }
    if (!c->nwp) wp_add(c, rib_at(&s->leaf[leaf], sTarget));
    c->goal = leaf;
    c->walked = 0;
    c->taskT = 0;
    c->task = T_WALK;
}

static float lenmax(const Garden *s) { return SPECIES[s->sp].len; }

static void route_twig(Garden *s) {
    route_to(s, s->bridge, 1.0f);
    Cat *c = &s->cat;
    float target = SPECIES[s->sp].girdled ? s->dJ + 3 + c->len : s->dH + c->len + 0.5f;
    for (float d = 1.5f; d < target; d += 2.5f) wp_add(c, twig_at(s, d));
    wp_add(c, twig_at(s, target));
    c->goal = -1;
    c->then = T_DONE;
}

/* The leaf pixel farthest out along the midrib; *fs gets how far out it is (0 base .. 1 tip). */
static V2 leaf_farthest(const Leaf *l, float *fs) {
    V2 best = l->rib[0];
    float bs = -1;
    for (int y = 0; y < l->c.h; y++)
        for (int x = 0; x < l->c.w; x++) {
            if (!l->c.px[y * l->c.w + x]) continue;
            V2 p = v2(l->ox + x + 0.5f, l->oy + y + 0.5f);
            float sv = rib_nearest(l, p, NULL, NULL);
            if (sv > bs) { bs = sv; best = p; }
        }
    *fs = bs < 0 ? 0 : bs;
    return best;
}

static void crumbs(Garden *s, V2 at, int n, uint32_t col) {
    for (int i = 0; i < n && s->ncrumb < MAXCRUMB; i++) {
        Crumb *q = &s->crumb[s->ncrumb++];
        q->x = at.x; q->y = at.y;
        q->vx = frand(-8, 8); q->vy = frand(-10, 2);
        q->life = frand(1.5f, 3.0f);
        q->col = col;
    }
}

/* One bite out of a leaf: clears a disc and gives the new edge a chewed rim. */
static int bite(Garden *s, Leaf *l, V2 m, float r) {
    int n = 0;
    int x0 = (int)floorf(m.x - r - 2), x1 = (int)ceilf(m.x + r + 2), y0 = (int)floorf(m.y - r - 2), y1 = (int)ceilf(m.y + r + 2);
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            float dx = x + 0.5f - m.x, dy = y + 0.5f - m.y;
            if (dx * dx + dy * dy <= r * r && get(&l->c, x - l->ox, y - l->oy)) { put(&l->c, x - l->ox, y - l->oy, 0); n++; }
        }
    if (!n) return 0;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            int cx = x - l->ox, cy = y - l->oy;
            if (get(&l->c, cx, cy) && (!get(&l->c, cx - 1, cy) || !get(&l->c, cx + 1, cy) || !get(&l->c, cx, cy - 1) || !get(&l->c, cx, cy + 1)))
                put(&l->c, cx, cy, rgb(l->chew));
        }
    l->left -= n;
    l->dirty = 1;
    crumbs(s, m, n / 4 + 1, l->base);
    return n;
}

/* Steers the head along the waypoints. Returns 1 on arrival. */
static int cat_walk(Garden *s, float dt, float speed) {
    Cat *c = &s->cat;
    if (c->wpi >= c->nwp) return 1;
    float rho = fmaxf(0.8f, c->R * 1.2f);
    c->wave += dt * speed / fmaxf(2, c->len * 0.3f) * 2 * PI_F;
    float v = speed * (0.45f + 0.9f * fmaxf(0, sinf(c->wave)));
    float stepLeft = v * dt;
    for (int guard = 0; guard < 8 && stepLeft > 0; guard++) {
        V2 tgt = c->wp[c->wpi], d = vsub(tgt, c->head);
        float dist = vlen(d);
        int last = c->wpi == c->nwp - 1;
        if ((!last && dist < rho) || (last && dist < 0.4f)) {
            c->wpi++;
            if (c->wpi >= c->nwp) return 1;
            continue;
        }
        float want = atan2f(d.y, d.x), diff = wrap_angle(want - c->heading);
        float turn = stepLeft / rho * (dist < 2 * rho ? 3.0f : 1.0f);
        if (last && dist < 1.5f * rho) turn = 9;          /* never circle round the spot it wants */
        c->heading += clampf(diff, -turn, turn);
        float st = fminf(stepLeft, dist) * (fabsf(diff) > 1.6f ? 0.5f : 1.0f);
        c->head = vadd(c->head, vmul(vang(c->heading), st));
        c->walked += st;
        trail_push(c);
        stepLeft -= st + 1e-4f;
        if (st < 1e-4f) break;
    }
    return 0;
}

static void cat_back(Cat *c, float d) {
    while (d > 0 && c->nt > 1) {
        V2 p = c->trail[c->nt - 1];
        float l = vlen(vsub(c->head, p));
        if (l > d) { c->head = vadd(c->head, vmul(vsub(p, c->head), d / l)); return; }
        c->head = p;
        c->nt--;
        d -= l;
    }
}

static void molt(Garden *s) {
    Cat *c = &s->cat;
    Skin *k = &s->skin;
    memcpy(k->pts, c->pts, sizeof k->pts);
    k->on = 1; k->R = c->R; k->len = c->len; k->t = 0; k->inst = c->inst; k->falling = 0;
    c->inst++;
    c->instT = 0;
    c->moltPending = 0;
}

static void cat_decide(Garden *s) {
    Cat *c = &s->cat;
    if (c->instT > INSTAR_T[c->inst]) {
        if (c->inst == 4) { route_twig(s); return; }
        c->moltPending = 1;
    }
    int cur = c->leaf >= 0 ? c->leaf : s->eggLeaf;
    if (c->inst < 2) {                                 /* small ones chew holes */
        int li = cur;
        if (s->leaf[li].left < s->leaf[li].full * 0.6f || s->leaf[li].bridge) {
            for (int i = 0; i < s->nleaf; i++)
                if (!s->leaf[i].bridge && s->leaf[i].left > s->leaf[i].full * 0.75f &&
                    fabsf(s->leaf[i].stemT - s->leaf[cur].stemT) < fabsf(s->leaf[li].stemT - s->leaf[cur].stemT) + (li == cur ? 9 : 0))
                    li = i;
        }
        float sv = frand(0.3f, 0.85f);
        route_to(s, li, sv);
        c->then = T_HOLE;
        c->holeSide = irand(2) ? 1 : -1;
        return;
    }
    int best = -1;                                     /* big ones eat leaves down from the tip */
    float bestD = 1e9f;
    for (int i = 0; i < s->nleaf; i++) {
        Leaf *l = &s->leaf[i];
        float fs;
        leaf_farthest(l, &fs);
        if (l->bridge || l->left < l->full * 0.3f || fs < 0.4f) continue;
        if (s->bug.leaf == i && !s->bug.flying) s->bug.stay = 0;   /* the ladybug politely moves */
        float d = i == cur ? -1 : fabsf(l->stemT - s->leaf[cur].stemT) + (l->side != s->leaf[cur].side ? 0.05f : 0);
        if (d < bestD) { bestD = d; best = i; }
    }
    if (best < 0) best = cur;
    Leaf *l = &s->leaf[best];
    float fs;
    leaf_farthest(l, &fs);
    route_to(s, best, fmaxf(0.15f, fs - (c->R * 1.6f + 0.5f) / l->len));
    c->then = T_TIP;
}

static void cat_step(Garden *s, float dt) {
    Cat *c = &s->cat;
    const Species *sp = &SPECIES[s->sp];
    if (c->task == T_HOLE || c->task == T_TIP) c->instT += dt;   /* a skin lasts so much eating */
    c->len = sp->len * INSTAR_LEN[c->inst] * (1 + 0.1f * fminf(1, c->instT / INSTAR_T[c->inst]));
    c->R = fmaxf(0.9f, c->len * sp->fat);
    c->bob *= 1 - fminf(1, dt * 4);
    float speed = 2.5f + c->len * 0.32f;
    switch (c->task) {
    case T_WALK: {
        if (c->moltPending && c->walked > c->len * 0.7f) { c->afterRest = T_WALK; c->task = T_REST; c->taskT = 0; break; }
        c->taskT += dt;
        int arrived = cat_walk(s, dt, c->then == T_DONE ? speed * 1.5f : speed);
        if (c->taskT > 40 && c->wpi < c->nwp) { c->head = c->wp[c->nwp - 1]; trail_push(c); arrived = 1; }   /* stuck: just get there */
        if (arrived) {
            c->leaf = c->goal;
            if (c->moltPending) { c->afterRest = c->then; c->task = T_REST; c->taskT = 0; }
            else { c->task = c->then; c->taskT = 0; }
            if (c->task == T_HOLE) { c->holeR = 0.5f; c->holeMax = c->inst ? 2.6f : 1.8f; c->hole = v2(0, 0); }
        }
        break;
    }
    case T_REST:
        c->taskT += dt;
        c->shiver = c->taskT < 1.8f ? 0.25f : 0;
        if (c->taskT > 1.8f) {
            molt(s);
            c->task = c->afterRest;
            c->taskT = 0;
            if (c->task == T_HOLE) { c->holeR = 0.5f; c->holeMax = c->inst ? 2.6f : 1.8f; c->hole = v2(0, 0); }
        }
        break;
    case T_HOLE: {
        Leaf *l = &s->leaf[c->leaf];
        V2 f = vang(c->heading), n = vmul(vperp(f), (float)c->holeSide);
        if (c->hole.x == 0 && c->hole.y == 0) c->hole = vadd(c->head, vadd(vmul(f, c->R * 0.8f), vmul(n, c->R + 1.2f)));
        c->taskT += dt;
        c->biteT -= dt;
        c->bobPh += dt * 6;
        c->bob = c->holeSide * c->R * 0.7f + sinf(c->bobPh) * c->R * 0.3f;
        if (c->biteT <= 0) {
            c->holeR = fminf(c->holeMax, c->holeR + 0.35f);
            bite(s, l, c->hole, c->holeR);
            c->biteT = 0.45f;
        }
        if (c->taskT > 3.6f) cat_decide(s);
        break;
    }
    case T_TIP: {
        Leaf *l = &s->leaf[c->leaf];
        c->bobPh += dt * 5;
        c->biteT -= dt;
        c->taskT += dt;
        if (c->biteT <= 0) {                       /* bite the farthest bit of leaf left, so it shrinks from the tip */
            float fs;
            V2 far = leaf_farthest(l, &fs);
            float sh = rib_nearest(l, c->head, NULL, NULL), want = fs - (c->R * 1.6f + 0.5f) / l->len;
            if (sh > want + 0.02f) cat_back(c, (sh - want) * l->len);
            V2 f = vang(c->heading), n = vperp(f), m = vadd(c->head, vmul(f, c->R * 0.9f));
            m = vlerp(m, far, 0.85f);
            c->bob = clampf(vdot(vsub(m, c->head), n), -c->R, c->R);
            bite(s, l, m, fmaxf(1.0f, c->R * 0.95f));
            c->biteT = 0.26f;
            if (fs < 0.22f || l->left < l->full * 0.22f || c->instT > INSTAR_T[c->inst] || c->taskT > 16) cat_decide(s);
        }
        break;
    }
    case T_DONE:
        break;
    default:
        break;
    }
    body_from_trail(c, c->len * (0.96f + 0.04f * sinf(c->wave)), c->pts);
    if (c->bob != 0 || c->shiver > 0)
        for (int k = K * 3 / 4; k < K; k++) {
            float w = (float)(k - K * 3 / 4) / (K - 1 - K * 3 / 4);
            V2 dir = vnorm(vsub(c->pts[k], c->pts[k - 1]));
            c->pts[k] = vadd(c->pts[k], vmul(vperp(dir), c->bob * w * w));
        }
    if (c->shiver > 0)
        for (int k = 0; k < K; k++) c->pts[k] = vadd(c->pts[k], v2(frand(-c->shiver, c->shiver), frand(-c->shiver, c->shiver)));
}

/* ---------- chrysalis ---------- */

/* A chrysalis from P0 (where it is fastened) along A. p: how far along it is (0-1); the wings show
   through near the end. stretch: longer while it forms. open: an empty shell after the butterfly is out. */
static void draw_chrysalis(Dyn *d, int sp, V2 P0, V2 A, float p, float sc, float stretch, float open, float crack) {
    const Species *S = &SPECIES[sp];
    V2 N = vperp(A);
    float Lc = S->chLen * sc * stretch, W = S->chW * sc / sqrtf(stretch);
    int r = (int)ceilf(Lc + 3);
    float k1 = smooth((p - 0.6f) / 0.2f), k2 = smooth((p - 0.78f) / 0.2f);
    for (int y = (int)floorf(P0.y) - r; y <= (int)P0.y + r; y++)
        for (int x = (int)floorf(P0.x) - r; x <= (int)P0.x + r; x++) {
            V2 q = vsub(v2(x + 0.5f, y + 0.5f), P0);
            float a = vdot(q, A), b = vdot(q, N);
            if (a < 0 || a > Lc + 2.2f * sc) continue;
            float stalk = 1.4f * sc, qq = (a - stalk) / (Lc - stalk), w;
            uint32_t col, wingc = 0x2A2A2A;
            int dot = 0;
            if (a < stalk) { if (fabsf(b) > 0.7f) continue; dset(d, x, y, rgb(0x2A2420)); continue; }
            if (sp == MONARCH) {
                w = W * powf(sinp(PI_F * clampf(0.1f + 0.9f * qq, 0, 1)), 0.55f);
                col = b < -0.4f * w ? 0x9EE6B8 : b > 0.4f * w ? 0x4FAE76 : 0x63C98B;
                if (qq > 0.22f && qq < 0.30f) { col = 0x1E1E1E; dot = ((int)floorf(b + 20) % 2) == 0; }
                if ((fabsf(qq - 0.8f) < 0.05f && fabsf(fabsf(b) - w * 0.45f) < 0.6f)) dot = 1;
                wingc = (fabsf(sinf((b + qq * 6) * 1.6f)) < 0.3f || fabsf(b) > w - 1.2f) ? 0x141210 : 0xE8781A;
            } else if (sp == PAINTED) {
                w = W * powf(sinp(PI_F * clampf(0.08f + 0.92f * qq, 0, 1)), 0.7f);
                if (b > 0) for (int k = 0; k < 3; k++) w += 0.9f * sc * expf(-(qq - 0.25f - 0.2f * k) * (qq - 0.25f - 0.2f * k) / 0.003f);
                col = b < -0.35f * w ? 0xBCA484 : b > 0.4f * w ? 0x7A6248 : 0xA48A68;
                if ((x * 7 + y * 3) % 9 == 0) col = 0x8E7656;
                for (int k = 0; k < 3; k++) if (fabsf(qq - 0.25f - 0.2f * k) < 0.05f && b > w - 1.4f) dot = 1;
                wingc = fabsf(sinf((b + qq * 5) * 1.5f)) < 0.35f ? 0x1E1812 : 0xE8803A;
            } else if (sp == SWALLOWTAIL) {
                w = W * (qq < 0.18f ? 0.4f + 0.6f * qq / 0.18f : qq < 0.62f ? 1.0f : 1.0f - 0.45f * (qq - 0.62f) / 0.38f);
                if (qq > 0.9f && fabsf(b) < 0.3f * W) continue;
                col = b < -0.3f * w ? 0xB8DC7A : b > 0.45f * w ? 0x76A848 : 0x9CCB5E;
                if (qq > 0.3f && qq < 0.48f && b < -0.1f * w) col = 0xE2E07A;
                wingc = (fabsf(b) > w - 1.6f && (int)(qq * 12) % 2) ? 0xF2D040 : 0x1E1C22;
            } else {
                w = W * (qq < 0.25f ? 0.35f + 0.65f * qq / 0.25f : qq < 0.6f ? 1.0f : fmaxf(0.12f, 1 - (qq - 0.6f) / 0.4f));
                if (b < 0) w += 0.9f * sc * expf(-(qq - 0.45f) * (qq - 0.45f) / 0.004f);
                col = b < -0.3f * w ? 0xC6E29C : b > 0.45f * w ? 0x86B25C : 0xA8D27C;
                if (fabsf(b + 0.55f * w) < 0.5f) col = 0xE6E28A;
                wingc = (qq > 0.62f && qq < 0.74f && b > 0.1f * w) ? 0x2A2A2A : 0xEEEEE2;
            }
            if (fabsf(b) > w) continue;
            if (dot) col = 0xF2C440;
            col = mixc(col, sp == WHITE ? 0xC8CCB4 : 0x2E4840, k1 * 0.7f);
            int inWing = qq > 0.3f && qq < 0.96f && b < w * (0.55f - 1.3f * (qq - 0.3f));
            if (inWing) col = mixc(col, wingc, k2 * 0.9f);
            else col = mixc(col, sp == WHITE ? 0xD8DCCC : 0x1E2220, k2 * 0.6f);
            uint32_t px = rgb(col);
            if (open > 0) {
                if (qq > 0.82f && fabsf(b) < w * 0.45f) continue;
                px = RGBA(mixc(0xD8E2CC, col, 0.25f), 0xB0);
            }
            if (crack > 0 && qq > 1 - 0.25f * crack && fabsf(b) < 0.6f) px = rgb(0x1A1A1A);
            dset(d, x, y, px);
        }
    outline(&d->c, open > 0 ? RGBA(0x8A9478, 0xB0) : rgb(S->chLine));
}

/* ---------- butterflies ---------- */

static const float TFX[10] = { 0.9f, 4, 9, 15, 19.2f, 20.2f, 18.6f, 15, 9.5f, 2.6f };
static const float TFY[10] = { -1.2f, -5, -8.6f, -10.6f, -10.4f, -8.4f, -4.6f, -0.8f, 1.6f, 1.4f };
static const float THX[9] = { 0.9f, 6, 11.2f, 14.6f, 14.6f, 12, 7.4f, 3.2f, 1.2f };
static const float THY[9] = { 0.2f, -0.2f, 1.6f, 5, 9.4f, 12.8f, 14.2f, 12.4f, 7.4f };
static const float TTX[3] = { 7.2f, 8.6f, 10.8f }, TTY[3] = { 13.2f, 18.8f, 12.4f };

/* Upper side of a wing seen from above, at wing coordinates (x out, y back) with edge = distance to its rim. */
static uint32_t top_color(int sp, int fore, float x, float y, float edge) {
    float vr = sqrtf((x - 1) * (x - 1) + y * y), phi = atan2f(y, x - 1);
    switch (sp) {
    case MONARCH: {
        if (edge < 1.7f) {
            int spot = edge > 0.5f && edge < 1.3f && ((int)floorf((x + y * 0.7f) * 0.9f) % 2 == 0);
            return spot ? 0xFFFFFF : 0x161210;
        }
        if (fore && x > 13.5f && y < -5.0f) return ((int)(x * 0.8f) + (int)(y * 0.8f)) % 3 == 0 ? 0xF6C070 : 0x161210;
        static const float fv[6] = { -0.95f, -0.72f, -0.5f, -0.3f, -0.1f, 0.12f }, hv[5] = { 0.18f, 0.48f, 0.78f, 1.08f, 1.38f };
        const float *vv = fore ? fv : hv;
        for (int i = 0; i < (fore ? 6 : 5); i++)
            if (vr > 2.5f && fabsf(sinf(phi - vv[i])) * vr < 0.55f) return 0x161210;
        return fore ? 0xF07A12 : 0xF28A26;
    }
    case PAINTED:
        if (edge < 0.9f) return 0x3A2A1E;
        if (fore && x > 12.0f && y < -3.5f) return (edge > 1.4f && ((int)(x * 0.9f + y) % 4 == 0)) ? 0xFFFFFF : 0x2A2018;
        if (fore && x > 7.5f && x < 11 && y < -1 && y > -7) return 0x2A2018;
        if (!fore && edge > 1.6f && edge < 2.8f && ((int)floorf(phi * 7) % 2 == 0)) return 0x2A2018;
        return vr < 3.5f ? 0xC07A44 : 0xF39A50;
    case SWALLOWTAIL: {
        int gapSpot = (int)floorf(phi * 11 + 0.5f) % 2 == 0;   /* breaks the rows into spots */
        if (!fore && (x - 3.0f) * (x - 3.0f) + (y - 11.6f) * (y - 11.6f) < 2.6f)
            return (x - 3.0f) * (x - 3.0f) + (y - 11.6f) * (y - 11.6f) < 0.7f ? 0x101010 : 0xF08A2A;
        if (edge > 0.7f && edge < 1.3f && gapSpot) return 0xF2D040;
        if (edge > 2.3f && edge < 3.3f && vr > 7 && gapSpot) return 0xF2D040;
        if (!fore && edge >= 1.4f && edge <= 2.2f && y > 5) return 0x4E7ED4;
        return 0x1E1C24;
    }
    default:
        if (fore && x > 14.5f && y < -6.0f) return 0x3E3E46;
        if (fore && (x - 12) * (x - 12) + (y + 2.5f) * (y + 2.5f) < 2.2f) return 0x2A2A30;
        if (!fore && (x - 9) * (x - 9) + (y + 0.2f) * (y + 0.2f) < 1.2f) return 0x5A5A60;
        if (vr < 3.0f) return 0xBDBDB4;
        return edge < 0.7f ? 0xD8D6C8 : 0xF6F4EA;
    }
}

/* Butterfly seen from above, head up, wings spread by `span` (1 open .. near 0 folded up). */
static Canvas make_top(int sp, float sc, float span, int *cx, int *cy) {
    float sz = SPECIES[sp].wing * sc;
    int hw = (int)ceilf(21 * sz) + 2, up = (int)ceilf(13.5f * sz) + 2, down = (int)ceilf(19.5f * sz) + 2;
    Canvas c = canvas_new(2 * hw, up + down);
    *cx = hw;
    *cy = up;
    for (int y = 0; y < c.h; y++)
        for (int x = 0; x < c.w; x++) {
            float wx = fabsf((x + 0.5f - hw) / span) / sz, wy = (y + 0.5f - up) / sz;
            uint32_t col = 0;
            if (in_poly(TFX, TFY, 10, wx, wy)) col = top_color(sp, 1, wx, wy, poly_edge(TFX, TFY, 10, wx, wy));
            else if (in_poly(THX, THY, 9, wx, wy)) col = top_color(sp, 0, wx, wy, poly_edge(THX, THY, 9, wx, wy));
            else if (sp == SWALLOWTAIL && in_poly(TTX, TTY, 3, wx, wy)) col = 0x1E1C24;
            if (col) put(&c, x, y, rgb(col));
        }
    const Species *S = &SPECIES[sp];
    outline(&c, rgb(0x1A1410));
    float bs = fmaxf(0.55f, sz);
    fill_ellipse(&c, hw, up + 5.2f * bs, 1.0f * bs + 0.2f, 4.4f * bs, rgb(S->body));
    fill_ellipse(&c, hw, up + 0.0f, 1.4f * bs + 0.2f, 2.3f * bs, rgb(S->body));
    fill_ellipse(&c, hw, up - 3.0f * bs, 1.25f * bs, 1.25f * bs, rgb(S->body));
    if (sz > 0.6f) {
        put(&c, hw - 1, up - 1, rgb(S->bodyDot));
        put(&c, hw, up + 3, rgb(S->bodyDot));
        put(&c, hw - 1, (int)(up + 6 * bs), rgb(S->bodyDot));
    }
    for (int sd = -1; sd <= 1; sd += 2) {
        float ax = hw + sd * 0.6f * bs, ay = up - 3.8f * bs, bx = hw + sd * 4.2f * bs, by = up - 12.0f * bs;
        cline(&c, ax, ay, bx, by, rgb(0x1A1410));
        put(&c, (int)floorf(bx), (int)floorf(by), rgb(0x1A1410));
        put(&c, (int)floorf(bx) + sd, (int)floorf(by), rgb(0x1A1410));
    }
    return c;
}

/* Underside of a closed wing, seen from the side: o out from the body, b toward the tail. */
static uint32_t side_color(int sp, int fore, float o, float b, float edge) {
    float vr = sqrtf(o * o + b * b), phi = atan2f(b, o);
    switch (sp) {
    case MONARCH: {
        if (edge < 1.4f) return (edge > 0.4f && edge < 1.1f && ((int)floorf((o + b) * 0.9f) % 2 == 0)) ? 0xFFFFFF : 0x161210;
        static const float fv[4] = { -0.12f, 0.08f, 0.3f, 0.52f }, hv[5] = { 0.2f, 0.5f, 0.8f, 1.1f, 1.4f };
        const float *vv = fore ? fv : hv;
        for (int i = 0; i < (fore ? 4 : 5); i++)
            if (vr > 2.0f && fabsf(sinf(phi - vv[i])) * vr < 0.6f) return 0x161210;
        if (fore && o > 13 && b < 1) return 0x161210;
        return fore ? 0xEE8A2A : 0xF2B05A;
    }
    case PAINTED:
        if (fore) {
            if (o > 12 && b < 3) return (edge > 1 && ((int)(o + b) % 3 == 0)) ? 0xFFFFFF : 0x2A2420;
            return edge < 0.8f ? 0x5A4A3A : 0xF0A07A;
        }
        if (edge > 1.2f && edge < 3.2f && ((int)floorf(phi * 8) % 2 == 0)) return edge < 2.2f ? 0x5A7AC8 : 0x1A1A1A;
        if (sinf(o * 1.3f) * sinf(b * 1.1f) > 0.55f) return 0xEDE6D6;
        return (hash3((int)o, (int)b, 3) & 1) ? 0xA89070 : 0x8C7458;
    case SWALLOWTAIL:
        if (edge > 0.7f && edge < 1.6f && ((int)floorf(phi * 9) % 2 == 0)) return 0xF2D040;
        if (!fore && edge > 2.2f && edge < 3.6f && ((int)floorf(phi * 7) % 2 == 0)) return 0xF09030;
        if (fore && edge > 2.4f && edge < 3.4f && vr > 5) return 0xF2D040;
        return 0x1E1C22;
    default:
        if (fore) {
            if (o > 13 && b < 2) return 0xEDEAB8;
            if ((o - 10) * (o - 10) + (b - 4) * (b - 4) < 2.0f) return 0x2A2A30;
            return 0xF4F2E6;
        }
        return vr < 4 ? 0xC8CCA4 : 0xE6E8A8;
    }
}

static const float SFO[10] = { 0, 5, 11, 16.5f, 18.6f, 18, 14, 8, 2.5f, 0 };
static const float SFB[10] = { -0.6f, -2.0f, -3.0f, -2.6f, -0.8f, 2.6f, 7, 9, 7.5f, 3 };
static const float SHO[9] = { 0, 5, 10, 13.2f, 13.4f, 11, 6.5f, 2.5f, 0.3f };
static const float SHB[9] = { 0, -0.6f, 0.6f, 3.6f, 8, 11.6f, 13, 11, 6 };

/* Abdomen points in body coordinates (a forward, d toward the back) for curl 0..1. */
static void abdomen(float sz, float fat, float curl, V2 *out, float *rad) {
    float ab = 7.0f * sz * (1 + 0.35f * fat), r0 = (1.1f + 0.2f * sz) * (1 + 0.45f * fat);
    V2 p = v2(-1.4f, 2.0f);
    float ang = PI_F;
    for (int i = 0; i < 8; i++) {
        out[i] = p;
        rad[i] = r0 * (1 - 0.5f * i / 7.0f);
        ang += curl * 0.2f;   /* bends toward the belly */
        p = vadd(p, vmul(v2(cosf(ang), sinf(ang)), ab / 7));
    }
}

/* A butterfly from the side with closed wings, holding on at G. A points to its head, D to its back.
   sw sweeps the wings back toward the tail; k is wing size while they pump up (crumpled below 1);
   fat swells the abdomen just after it comes out; curl bends the abdomen to lay an egg. */
static void draw_side(Dyn *d, int sp, V2 G, V2 A, V2 D, float sw, float k, float fat, float curl) {
    const Species *S = &SPECIES[sp];
    float sz = S->wing;
#define BW(a, dd) vadd(G, vadd(vmul(A, (a)), vmul(D, (dd))))
    V2 root = BW(0.2f, 3.4f);
    V2 O = vadd(vmul(D, cosf(sw)), vmul(A, -sinf(sw))), Bk = vadd(vmul(A, -cosf(sw)), vmul(D, -sinf(sw)));
    for (int wing = 0; wing < 2; wing++) {
        const float *po = wing ? SHO : SFO, *pb = wing ? SHB : SFB;
        int n = wing ? 9 : 10;
        float xs[12], ys[12], wo[12], wb[12];
        float sc = sz * k;
        for (int i = 0; i < n; i++) {
            float jo = (1 - k) * 1.1f * sinf(i * 2.7f + wing), jb = (1 - k) * 1.1f * cosf(i * 1.9f);
            wo[i] = po[i] + jo / fmaxf(0.3f, k);
            wb[i] = pb[i] + jb / fmaxf(0.3f, k);
            V2 p = vadd(root, vadd(vmul(O, wo[i] * sc), vmul(Bk, wb[i] * sc)));
            xs[i] = p.x; ys[i] = p.y;
        }
        float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
        for (int i = 0; i < n; i++) { x0 = fminf(x0, xs[i]); x1 = fmaxf(x1, xs[i]); y0 = fminf(y0, ys[i]); y1 = fmaxf(y1, ys[i]); }
        for (int y = (int)floorf(y0); y <= (int)ceilf(y1); y++)
            for (int x = (int)floorf(x0); x <= (int)ceilf(x1); x++) {
                float px = x + 0.5f, py = y + 0.5f;
                if (!in_poly(xs, ys, n, px, py)) continue;
                V2 q = vsub(v2(px, py), root);
                float o = vdot(q, O) / sc, b = vdot(q, Bk) / sc;
                uint32_t col = side_color(sp, !wing, o, b, poly_edge(xs, ys, n, px, py) / sc);
                if (k < 0.98f && sinf(o * 2.1f + b * 1.3f) > 0.8f) col = mixc(col, 0, 0.3f * (1 - k));   /* crumple folds */
                if (wing == 0 && dget(d, x, y)) continue;
                dset(d, x, y, rgb(col));
            }
        if (wing == 0 && k > 0.6f) outline(&d->c, rgb(0x1A1410));
    }
    if (sp == SWALLOWTAIL && k > 0.7f) {    /* the tail on the hind wing */
        V2 t0 = vadd(root, vadd(vmul(O, 6.5f * sz * k), vmul(Bk, 12.4f * sz * k)));
        V2 t1 = vadd(root, vadd(vmul(O, 8.0f * sz * k), vmul(Bk, 16.5f * sz * k)));
        dline(d, t0, t1, rgb(0x1E1C22));
    }
    outline(&d->c, rgb(0x1A1410));
    V2 ab[8];
    float ar[8];
    abdomen(sz, fat, curl, ab, ar);
    for (int i = 0; i < 8; i++) ddisc(d, BW(ab[i].x, ab[i].y), ar[i], rgb(i % 2 && sp == MONARCH ? 0x3A3230 : S->body));
    /* thorax and head */
    for (int y = -4; y <= 4; y++)
        for (int x = -4; x <= 4; x++) {
            V2 q = v2(x * 0.5f, y * 0.5f);
            if ((q.x * q.x) / 4.4f + (q.y * q.y) / 2.3f <= 1) {
                V2 p = BW(q.x, 2.4f + q.y);
                dset(d, (int)floorf(p.x), (int)floorf(p.y), rgb(S->body));
            }
        }
    ddisc(d, BW(2.5f, 2.7f), 1.25f, rgb(S->body));
    V2 eye = BW(2.9f, 2.9f);
    dset(d, (int)floorf(eye.x), (int)floorf(eye.y), rgb(0x3A3A3A));
    V2 dotp = BW(0.2f, 2.0f);
    dset(d, (int)floorf(dotp.x), (int)floorf(dotp.y), rgb(S->bodyDot));
    outline(&d->c, rgb(0x1A1410));
    /* legs and feelers */
    static const float la[3] = { -0.6f, 0.4f, 1.4f }, fa[3] = { -1.8f, 0.2f, 2.4f };
    for (int i = 0; i < 3; i++) dline(d, BW(la[i], 1.2f), BW(fa[i], 0.0f), rgb(0x1A1410));
    V2 a0 = BW(3.2f, 3.4f), a1 = BW(3.2f + 6.5f * sz * 0.57f, 3.4f + 6.5f * sz * 0.82f);
    dline(d, a0, a1, rgb(0x1A1410));
    ddisc(d, a1, 0.7f, rgb(0x1A1410));
#undef BW
}

static V2 abdomen_tip(int sp, float curl, V2 A, V2 D) {
    V2 ab[8];
    float ar[8];
    abdomen(SPECIES[sp].wing, 0, curl, ab, ar);
    return vadd(vmul(A, ab[7].x), vmul(D, ab[7].y - ar[7] * 0.5f));
}

/* ---------- egg and ladybug ---------- */

static void draw_egg(Dyn *d, int sp, V2 E, float dev, int lean, float crack, float eaten, float sc) {
    float hw, H;
    switch (sp) {
    case MONARCH: hw = 1.9f; H = 6.0f; break;
    case SWALLOWTAIL: hw = 2.3f; H = 4.6f; break;
    case PAINTED: hw = 2.0f; H = 4.8f; break;
    default: hw = 1.5f; H = 6.2f; break;
    }
    hw *= sc; H *= sc;
    uint32_t base = SPECIES[sp].egg;
    float top = H * (1 - eaten);
    for (int y = -(int)ceilf(H) - 1; y <= 0; y++)
        for (int x = -4; x <= 4; x++) {
            float hy = -(y + 0.5f);
            if (hy > top) continue;
            float q = hy / H, w;
            if (sp == SWALLOWTAIL) w = hw * sqrtf(fmaxf(0, 1 - (2 * q - 1) * (2 * q - 1)));
            else if (sp == PAINTED) w = hw * (q < 0.15f ? 0.6f + q / 0.15f * 0.4f : q > 0.85f ? 1 - (q - 0.85f) * 2.5f : 1);
            else w = hw * powf(sinp(PI_F * clampf(q * 0.92f + 0.06f, 0, 1)), sp == MONARCH ? 0.6f : 0.4f) * (q > 0.5f ? 1 - (q - 0.5f) * 0.6f : 1);
            float xs = x + 0.5f - lean * q * 1.2f;
            if (fabsf(xs) > w) continue;
            uint32_t col = ((x + 10) % 2 && sp != SWALLOWTAIL) ? mixc(base, 0xFFFFFF, 0.3f) : base;
            if (xs > w * 0.5f) col = mixc(col, 0x000000, 0.12f);
            if (q > 1 - 0.38f * dev) col = mixc(col, 0x3A3A3A, dev * 0.85f);
            if (crack > 0 && q > 1 - 0.3f * crack) {
                if (q > 0.88f) continue;
                if (fabsf(q - (0.82f - 0.06f * sinf(xs * 3))) < 0.06f) col = 0x2A2A2A;
            }
            if (eaten > 0 && hy > top - 1) col = mixc(base, 0xFFFFFF, 0.5f);
            dset(d, (int)floorf(E.x) + x, (int)floorf(E.y) + y, rgb(col));
        }
    outline(&d->c, rgb(mixc(base, 0x000000, 0.45f)));
}

static void draw_ladybug(Dyn *d, V2 p, float heading, int fly, float legPh) {
    V2 f = vang(heading), n = vperp(f);
    if (!fly)
        for (int i = -1; i <= 1; i++)
            for (int sd = -1; sd <= 1; sd += 2) {
                float sw = sinf(legPh + i * 2 + (sd > 0 ? 3.1f : 0)) * 0.7f;
                V2 a = vadd(p, vadd(vmul(f, i * 1.3f), vmul(n, sd * 2.0f)));
                V2 b = vadd(a, vadd(vmul(n, sd * 1.4f), vmul(f, sw)));
                dline(d, a, b, rgb(0x1A1410));
            }
    for (int y = -6; y <= 6; y++)
        for (int x = -6; x <= 6; x++) {
            V2 q = vsub(v2((int)floorf(p.x) + x + 0.5f, (int)floorf(p.y) + y + 0.5f), p);
            float a = vdot(q, f), b = vdot(q, n);
            uint32_t col = 0;
            if (fly) {
                float bb = fabsf(b) - 1.6f;
                if (a < 1.5f && a > -3.5f && bb > 0 && bb < 2.6f) col = 0xE8F4FF;
            }
            float ob = fly ? (fabsf(b) - 1.2f) : b;
            if (fly && fabsf(b) < 1.2f) ob = 99;
            if ((a * a) / 9.0f + (ob * ob) / 7.3f <= 1) {
                col = 0xE02820;
                if (a > 1.2f && ob < -0.6f) col = 0xFF6A50;
                float sb = fly ? ob : fabsf(b) - 1.3f;
                if ((a - 0.4f) * (a - 0.4f) + sb * sb < 0.6f || (a + 1.4f) * (a + 1.4f) + (fabsf(b) - 1.0f) * (fabsf(b) - 1.0f) < 0.5f) col = 0x1A1410;
                if (!fly && fabsf(b) < 0.3f) col = 0x1A1410;
            }
            if (a > 2.3f && a < 3.9f && fabsf(b) < 1.7f && (a - 2.3f) * (a - 2.3f) / 2.6f + b * b / 2.9f <= 1)
                col = (fabsf(b) > 0.7f && a < 3.0f) ? 0xFFFFFF : 0x1A1410;
            if (col) dset(d, (int)floorf(p.x) + x, (int)floorf(p.y) + y, rgb(col));
        }
    outline(&d->c, rgb(0x3A0A08));
}

/* ---------- the ring of stages ---------- */

static void make_ring(Garden *s, SDL_Renderer *r) {
    for (int i = 0; i < 4; i++) {
        if (s->icon[i]) SDL_DestroyTexture(s->icon[i]);
        s->icon[i] = NULL;
    }
    if (s->ringBg) SDL_DestroyTexture(s->ringBg);
    float u = s->u;
    int ts = s->ts;
    float th = s->labels ? 7.0f * ts / u : 0, gap = s->labels ? 1.2f : 0;
    float half[4];
    for (int i = 0; i < 4; i++) half[i] = s->labels ? text_width(STAGE[i], ts) / u / 2 : 0;
    /* an oval ring: wide enough that the side labels clear the bottom picture, and short */
    float ic = 7.5f, Rx = 2 * ic + 2, Ry = ic + 4, pad = 2.5f, m = 3;
    float left = fmaxf(Rx + ic, Rx - ic + 2 * half[3]), right = fmaxf(Rx + ic, Rx - ic + 2 * half[1]);
    float up = Ry + ic + gap + th, down = Ry + ic + gap + th;
    float pw = left + right + 2 * pad, ph = up + down + 2 * pad;
    s->ringCx = m + pad + left;
    s->ringCy = m + pad + up;
    s->ringRx = Rx;
    s->ringRy = Ry;
    s->iconHalf = ic;
    s->panel = (SDL_Rect){ (int)(m * u), (int)(m * u), (int)ceilf(pw * u), (int)ceilf(ph * u) };
    /* the panel: a card with a dotted ring and arrows going round clockwise */
    int cw = (int)ceilf(pw), chh = (int)ceilf(ph);
    Canvas c = canvas_new(cw, chh);
    fill_round_rect(&c, 0, 0, cw - 1, chh - 1, 4, rgb(0xC8B890));
    fill_round_rect(&c, 1, 1, cw - 2, chh - 2, 3, rgb(0xFFF8E6));
    float cx = s->ringCx - m, cy = s->ringCy - m;
    for (int i = 0; i < 200; i++) {
        float a = i * 2 * PI_F / 200;
        if (i % 5 < 3) put(&c, (int)floorf(cx + cosf(a) * Rx), (int)floorf(cy + sinf(a) * Ry), rgb(0xC0A274));
    }
    for (int q = 0; q < 4; q++) {                  /* arrowheads between the pictures, going round clockwise */
        float a = -PI_F / 2 + PI_F / 4 + q * PI_F / 2;
        V2 p = v2(cx + cosf(a) * Rx, cy + sinf(a) * Ry), t = vnorm(v2(-sinf(a) * Rx, cosf(a) * Ry)), n = vperp(t);
        float xs[3] = { p.x + t.x * 2.2f, p.x - t.x * 1.2f + n.x * 1.8f, p.x - t.x * 1.2f - n.x * 1.8f };
        float ys[3] = { p.y + t.y * 2.2f, p.y - t.y * 1.2f + n.y * 1.8f, p.y - t.y * 1.2f - n.y * 1.8f };
        fill_poly(&c, xs, ys, 3, rgb(0xA8844E));
    }
    s->ringBg = keep_texture(r, &c, 0);
    free(c.px);
    /* four little pictures in this butterfly's colours */
    int isz = 15;
    for (int i = 0; i < 4; i++) {
        Dyn d;
        d.c = canvas_new(isz, isz);
        d.ox = 0; d.oy = 0;
        if (i == 0) {
            fill_ellipse(&d.c, 7.5f, 11.0f, 7.0f, 3.0f, rgb(0x4E9A40));
            fill_ellipse(&d.c, 7.5f, 10.8f, 6.0f, 2.0f, rgb(0x86D066));
            Dyn e;
            e.c = canvas_new(isz, isz);
            e.ox = 0; e.oy = 0;
            draw_egg(&e, s->sp, v2(7.5f, 11.5f), 0, 0, 0, 0, 1.2f);
            for (int k = 0; k < isz * isz; k++) if (e.c.px[k]) d.c.px[k] = e.c.px[k];
            free(e.c.px);
        } else if (i == 1) {
            V2 pts[K];
            for (int k = 0; k < K; k++) {
                float t = (float)k / (K - 1);
                pts[k] = v2(2.0f + t * 11.0f, 8.5f - sinf(t * PI_F * 1.6f) * 2.0f);
            }
            draw_cat(&d, pts, 2.2f, 13, s->sp, 4, -1, 0);
        } else if (i == 2) {
            cline(&d.c, 2, 1.5f, 13, 1.5f, rgb(0x7A5030));
            draw_chrysalis(&d, s->sp, v2(7.5f, 2.2f), v2(0, 1), 0.3f, 0.75f, 1, 0, 0);
        } else {
            int cx2, cy2;
            Canvas t = make_top(s->sp, 0.36f / SPECIES[s->sp].wing, 1, &cx2, &cy2);
            for (int y = 0; y < t.h; y++)
                for (int x = 0; x < t.w; x++)
                    if (get(&t, x, y)) put(&d.c, x - cx2 + 7, y - cy2 + 6, get(&t, x, y));
            free(t.px);
        }
        s->icon[i] = keep_texture(r, &d.c, 0);
        free(d.c.px);
    }
    if (s->labels)
        for (int i = 0; i < 4; i++) {
            if (!s->label[i]) {
                Canvas t = text_canvas(STAGE[i], rgb(0xFFFFFF));
                s->label[i] = keep_texture(r, &t, 0);
                free(t.px);
            }
            float tw = text_width(STAGE[i], ts) / u, x, y;
            if (i == 0) { x = s->ringCx - tw / 2; y = s->ringCy - Ry - ic - gap - th; }
            else if (i == 2) { x = s->ringCx - tw / 2; y = s->ringCy + Ry + ic + gap; }
            else if (i == 1) { x = s->ringCx + Rx - ic; y = s->ringCy + ic + gap; }
            else { x = s->ringCx - Rx + ic - tw; y = s->ringCy + ic + gap; }
            s->labelPos[i] = v2(x * u, y * u);
        }
}

/* ---------- the story ---------- */

static void place_mother(Garden *s, int anywhere) {
    Leaf *l = &s->leaf[s->eggLeaf];
    float sv = 0.6f;
    V2 f = rib_dir(l, sv), n = vperp(f);
    if (n.y > 0) n = vmul(n, -1);
    s->eggPos = vadd(rib_at(l, sv), vmul(n, w_at(l, sv) * 0.3f));
    s->side = -l->side;
    V2 A = v2((float)s->side, 0), D = v2(0, -1);
    s->grip = vsub(s->eggPos, abdomen_tip(s->sp, 1, A, D));
    Flyer *fl = &s->fly;
    memset(fl, 0, sizeof *fl);
    fl->on = 1;
    fl->speed = 30;
    int fromRight = irand(2);
    fl->pos = anywhere ? v2(s->lw * 0.72f, s->lh * 0.3f) : v2(fromRight ? s->lw + 25.0f : -25.0f, s->lh * frand(0.15f, 0.4f));
    fl->path[0] = v2(s->stemX + frand(-10, 30), s->lh * frand(0.12f, 0.22f));
    fl->path[1] = vadd(s->grip, v2(-s->side * 14.0f, -14));
    fl->path[2] = s->grip;
    fl->npath = 3;
}

static void new_cycle(Garden *s, SDL_Renderer *r) {
    int sp;
    do sp = irand(NSP);
    while (sp == s->lastSp && NSP > 1);
    s->sp = s->lastSp = sp;
    s->cycles++;
    for (int i = 0; i < s->nleaf; i++) {
        if (s->leaf[i].t) SDL_DestroyTexture(s->leaf[i].t);
        free(s->leaf[i].c.px);
        s->leaf[i].t = NULL;
        s->leaf[i].c.px = NULL;
    }
    if (s->plantTex) SDL_DestroyTexture(s->plantTex);
    if (s->twigTex) SDL_DestroyTexture(s->twigTex);
    for (int i = 0; i < TOPF; i++) if (s->top[i]) SDL_DestroyTexture(s->top[i]);
    int kind = SPECIES[sp].plant;
    s->stemX = fmaxf(s->lw * 0.34f, (s->panel.x + s->panel.w) / s->u + 14);   /* flowers clear of the ring */
    V2 b = v2(s->stemX, s->gy + 2), t = v2(s->stemX + 3, s->lh * 0.2f), c = v2(s->stemX - 4, (b.y + t.y) / 2);
    for (int i = 0; i < STEMN; i++) {
        float q = (float)i / (STEMN - 1);
        s->stem[i] = vadd(vadd(vmul(b, (1 - q) * (1 - q)), vmul(c, 2 * q * (1 - q))), vmul(t, q * q));
    }
    Canvas pc = make_plant(s, kind);
    s->plantTex = canvas_texture(r, &pc);
    make_leaves(s, r, kind);
    Canvas tc = make_twig(s, lenmax(s));
    s->twigTex = canvas_texture(r, &tc);
    static const float spans[TOPF] = { 1.0f, 0.68f, 0.38f, 0.12f };
    for (int i = 0; i < TOPF; i++) {
        Canvas tp = make_top(sp, 1, spans[i], &s->topCx, &s->topCy);
        s->topW = tp.w;
        s->topH = tp.h;
        s->top[i] = canvas_texture(r, &tp);
    }
    /* lawn flowers stay; keep the ones after the plant's */
    make_ring(s, r);
    s->egg = 0; s->laid = 0; s->eggEaten = 0; s->crack = 0;
    cat_reset(&s->cat, v2(0, 0), 0);
    s->skin.on = 0;
    s->ncrumb = 0;
    s->silk = s->girdle = 0;
    s->bug.leaf = -1;
    s->bug.flying = 0;
    s->bug.stay = 0;
    s->cycleT = 0;
}

static void set_phase(Garden *s, int p) {
    s->phase = p;
    s->phaseT = 0;
    if (p == P_EMERGE) g_rush = 0;     /* F: fast-forward stops when the butterfly comes out */
}

/* Where the chrysalis is fastened and which way it points (hangs down, or stands up on its belt). */
static void pupa_frame(Garden *s, float sway) {
    if (SPECIES[s->sp].girdled) {
        s->P0 = vadd(twig_at(s, s->dJ + 3), v2(-2.6f, 0));
        s->A = vang(-PI_F / 2 - 0.22f + sway * 0.3f);
    } else {
        s->P0 = vadd(s->H, v2(0, 1.4f));
        s->A = vang(PI_F / 2 + sway);
    }
}

static void jump_to_emerge(Garden *s) {
    s->fly.on = 0;
    s->egg = 0;
    s->skin.on = 0;
    s->ncrumb = 0;
    s->cat.visible = 0;
    s->laid = 1;
    s->silk = 1;
    s->girdle = 1;
    set_phase(s, P_EMERGE);
}

/* Which stage the ring shows, and how far round toward the next one the marker is. */
static void stage_of(const Garden *s, int *st, float *pr) {
    float t = s->phaseT;
    switch (s->phase) {
    case P_ARRIVE: *st = 3; *pr = 0.75f + 0.2f * fminf(1, t / 10); break;
    case P_LAY: if (s->laid) { *st = 0; *pr = 0; } else { *st = 3; *pr = 0.96f; } break;
    case P_EGG: *st = 0; *pr = 0.85f * t / EGG_T; break;
    case P_HATCH: if (t < 1.0f) { *st = 0; *pr = 0.9f; } else { *st = 1; *pr = 0; } break;
    case P_SHELL: *st = 1; *pr = 0.03f; break;
    case P_LARVA: *st = 1; *pr = 0.05f + 0.85f * (s->cat.inst + fminf(1, s->cat.instT / INSTAR_T[s->cat.inst])) / 5; break;
    case P_SILK: case P_HANG: *st = 1; *pr = 0.93f; break;
    case P_PUPATE: if (t < 1.0f) { *st = 1; *pr = 0.97f; } else { *st = 2; *pr = 0; } break;
    case P_CHRYS: *st = 2; *pr = 0.05f + 0.9f * t / CHRYS_T; break;
    case P_EMERGE: if (t < 1.5f) { *st = 2; *pr = 0.97f; } else { *st = 3; *pr = 0; } break;
    case P_PUMP: *st = 3; *pr = 0.05f + 0.1f * t / PUMP_T; break;
    case P_DRY: *st = 3; *pr = 0.15f + 0.1f * t / DRY_T; break;
    case P_FLY: *st = 3; *pr = 0.25f + 0.4f * fminf(1, t / 25); break;
    default: *st = 3; *pr = 0.7f; break;
    }
}

static const char *activity(const Garden *s) {
    switch (s->phase) {
    case P_ARRIVE: return "A BUTTERFLY LOOKS FOR ITS PLANT";
    case P_LAY: return s->laid ? "IT LAID AN EGG!" : "LAYING AN EGG";
    case P_EGG: return "THE EGG IS GROWING A CATERPILLAR";
    case P_HATCH: return "HATCHING";
    case P_SHELL: return "EATING ITS EGGSHELL";
    case P_LARVA:
        if (s->cat.task == T_REST) return "SHEDDING ITS SKIN";
        if (s->cat.then == T_DONE) return "LOOKING FOR A GOOD TWIG";
        if (s->cat.task == T_HOLE || s->cat.task == T_TIP) return "EATING LEAVES";
        return "CRAWLING";
    case P_SILK: return "SPINNING SILK";
    case P_HANG: return SPECIES[s->sp].girdled ? "TIED ON WITH A SILK BELT" : "HANGING IN A J";
    case P_PUPATE: return "TURNING INTO A CHRYSALIS";
    case P_CHRYS: return s->phaseT > CHRYS_T * 0.75f ? "YOU CAN SEE THE WINGS!" : "CHANGING INSIDE THE CHRYSALIS";
    case P_EMERGE: return "COMING OUT";
    case P_PUMP: return "PUMPING UP ITS WINGS";
    case P_DRY: return "DRYING ITS WINGS";
    case P_FLY: return s->fly.sitting ? "SIPPING NECTAR" : "FLYING AROUND THE GARDEN";
    default: return "OFF TO FIND A NEW PLANT";
    }
}

static int day_of(const Garden *s) {
    int st;
    float pr;
    stage_of(s, &st, &pr);
    if (s->phase == P_ARRIVE || (s->phase == P_LAY && !s->laid) || s->phase >= P_FLY) return 0;
    static const float start[4] = { 1, 5, 18, 28 }, len[4] = { 4, 13, 10, 1 };
    return (int)(start[st] + pr * len[st]);
}

/* Where the new butterfly holds on once it is out: under the empty shell, or on the stick above it. */
static V2 adult_hang(const Garden *s) {
    const Species *S = &SPECIES[s->sp];
    V2 shellEnd = vadd(s->P0, vmul(s->A, S->chLen));
    return S->girdled ? v2(s->stickX - 1.2f, shellEnd.y - 7) : vadd(shellEnd, v2(-2.2f, 2.6f));
}

/* ---------- step ---------- */

static void flyer_step(Flyer *f, float dt) {
    f->flap += dt;
    if (f->sitting) return;
    V2 tgt = f->path[f->pi], d = vsub(tgt, f->pos);
    float dist = vlen(d);
    int last = f->pi == f->npath - 1;
    float sp = f->speed * (last ? clampf(dist / 12, 0.25f, 1) : 1);
    if (dist > 1e-3f) f->pos = vadd(f->pos, vmul(d, fminf(sp * dt, dist) / dist));
    float wob = fminf(1, dist / 10);
    f->pos.x += sinf(f->flap * 2.3f) * 9 * dt * wob;
    f->pos.y += cosf(f->flap * 4.1f) * 16 * dt * wob;
    if (!last && dist < 5) f->pi++;
}

static int flyer_arrived(const Flyer *f) { return f->pi == f->npath - 1 && vlen(vsub(f->path[f->pi], f->pos)) < 0.8f; }

static V2 bee_flower(const Garden *s) {
    int n = s->nflower + s->nlawn, i = irand(n);
    return i < s->nflower ? s->flower[i] : s->lawn[i - s->nflower];
}

static void bee_step(Garden *s, float dt) {
    Bee *b = &s->bee;
    b->flap += dt;
    b->timer -= dt;
    if (b->sitting) {
        if (b->timer <= 0) { b->sitting = 0; b->timer = frand(4, 8); b->away = irand(4) == 0; }
        else return;
        if (!b->away) b->target = bee_flower(s);
        else b->target = v2(irand(2) ? -20.0f : s->lw + 20.0f, s->lh * frand(0.2f, 0.6f));
    }
    V2 d = vsub(b->target, b->pos);
    float dist = vlen(d);
    if (fabsf(d.x) > 0.5f) b->dir = d.x > 0 ? 1 : -1;
    if (dist > 1e-3f) b->pos = vadd(b->pos, vmul(d, fminf(34 * dt, dist) / dist));
    b->pos.y += sinf(b->flap * 7) * 6 * dt;
    if (dist < 1.2f) {
        if (b->away) {        /* out of sight for a while, then back */
            b->away = 0;
            b->timer = frand(3, 8);
            b->sitting = 1;
            b->pos = b->target;
            return;
        }
        b->sitting = 1;
        b->timer = frand(1.5f, 3.5f);
    }
}

static int bug_ok(const Garden *s, int i) {
    const Leaf *l = &s->leaf[i];
    return l->left > l->full * 0.7f && i != s->cat.leaf && i != s->cat.goal && i != s->eggLeaf;
}

static void bug_step(Garden *s, float dt) {
    Ladybug *g = &s->bug;
    if (g->flying) {
        g->t += dt / 1.6f;
        V2 m = vadd(vlerp(g->from, g->to, 0.5f), v2(0, -14));
        float q = fminf(1, g->t);
        g->pos = vadd(vadd(vmul(g->from, (1 - q) * (1 - q)), vmul(m, 2 * q * (1 - q))), vmul(g->to, q * q));
        V2 dd = vsub(g->to, g->from);
        g->heading = atan2f(dd.y, dd.x);
        if (q >= 1) { g->flying = 0; g->stay = frand(10, 20); }
        return;
    }
    if (g->leaf < 0 || g->stay <= 0 || !bug_ok(s, g->leaf) || !leaf_has(&s->leaf[g->leaf], g->pos.x, g->pos.y)) {
        int opts[MAXLEAF], n = 0;
        for (int i = 0; i < s->nleaf; i++) if (bug_ok(s, i) && i != g->leaf) opts[n++] = i;
        if (!n) return;
        int i = opts[irand(n)];
        g->from = g->leaf < 0 ? v2(-10, s->lh * 0.3f) : g->pos;
        g->leaf = i;
        g->s = frand(0.25f, 0.8f);
        g->to = rib_at(&s->leaf[i], g->s);
        g->flying = 1;
        g->t = 0;
        return;
    }
    g->stay -= dt;
    Leaf *l = &s->leaf[g->leaf];
    V2 tgt = vadd(rib_at(l, g->s), vmul(vperp(rib_dir(l, g->s)), g->v * w_at(l, g->s)));
    V2 d = vsub(tgt, g->pos);
    float dist = vlen(d);
    if (dist < 0.5f) {
        g->s = clampf(g->s + frand(-0.3f, 0.3f), 0.2f, 0.85f);
        g->v = frand(-0.4f, 0.4f);
        return;
    }
    float want = atan2f(d.y, d.x);
    g->heading += clampf(wrap_angle(want - g->heading), -2 * dt, 2 * dt);
    g->pos = vadd(g->pos, vmul(vang(g->heading), fminf(dist, 4 * dt)));
    g->legPh += dt * 14;
}

static void step(void *p, float dt) {
    Garden *s = p;
    if (dt > 0.1f) dt = 0.1f;
    s->t += dt;
    s->phaseT += dt;
    s->cycleT += dt;
    if (g_force_boom && s->phase != P_FADE) {
        if (s->phase < P_EMERGE) jump_to_emerge(s);
        g_force_boom = 0;
    }
    for (int i = 0; i < NCLOUD; i++) {
        Cloud *c = &s->cloud[i];
        c->x += c->v * dt;
        if (c->x > s->lw + 2) { c->shape = irand(4); c->x = -(float)s->cloudW[c->shape] - frand(0, 30); c->y = frand(0.03f, 0.3f) * s->lh; }
    }
    bee_step(s, dt);
    bug_step(s, dt);
    for (int i = 0; i < s->ncrumb; i++) {
        Crumb *q = &s->crumb[i];
        q->life -= dt;
        if (q->y < s->gy) { q->vy += 50 * dt; q->x += q->vx * dt; q->y += q->vy * dt; q->vx *= 1 - dt; }
        if (q->life <= 0) s->crumb[i--] = s->crumb[--s->ncrumb];
    }
    if (s->skin.on) {
        Skin *k = &s->skin;
        k->t += dt;
        if (k->falling) {
            k->vy += 40 * dt;
            float dy = k->vy * dt;
            if (k->pts[K - 1].y < s->gy) for (int i = 0; i < K; i++) k->pts[i].y += dy;
        } else {
            V2 tail = k->pts[0];
            float q = fminf(1, k->t / 2.5f) * 0.55f;
            for (int i = 1; i < K; i++) k->pts[i] = vlerp(k->pts[i], tail, q * dt * 0.8f);
        }
        if (k->t > 6) k->on = 0;
    }
    Cat *c = &s->cat;
    Flyer *f = &s->fly;
    const Species *S = &SPECIES[s->sp];
    float t = s->phaseT;
    if (f->on && s->phase != P_LAY) flyer_step(f, dt);
    switch (s->phase) {
    case P_ARRIVE:
        if (flyer_arrived(f)) { f->sitting = 1; set_phase(s, P_LAY); }
        break;
    case P_LAY:
        if (t >= 1.7f && !s->laid) { s->laid = 1; s->egg = 1; crumbs(s, s->eggPos, 0, 0); }
        if (t >= LAY_T) {
            f->sitting = 0;
            f->pi = 0;
            f->npath = 1;
            f->path[0] = v2(irand(2) ? -30.0f : s->lw + 30.0f, s->lh * frand(0.0f, 0.2f));
            f->speed = 26;
            set_phase(s, P_EGG);
        }
        break;
    case P_EGG:
        if (f->on && (f->pos.x < -25 || f->pos.x > s->lw + 25)) f->on = 0;
        if (t >= EGG_T) {
            set_phase(s, P_HATCH);
        }
        break;
    case P_HATCH:
        s->crack = smooth(t / 1.0f);
        if (f->on && (f->pos.x < -25 || f->pos.x > s->lw + 25)) f->on = 0;
        if (t >= 1.0f && !c->visible) {
            Leaf *l = &s->leaf[s->eggLeaf];
            float hh = 4.5f;
            V2 dir = rib_dir(l, 0.6f);
            cat_reset(c, vadd(s->eggPos, v2(0, -hh)), atan2f(dir.y, dir.x));
            c->visible = 1;
            c->leaf = s->eggLeaf;
            c->inst = 0;
            c->len = S->len * INSTAR_LEN[0];
            c->R = fmaxf(0.9f, c->len * S->fat);
            c->nwp = 0;
            wp_add(c, vadd(s->eggPos, vmul(dir, c->len + 2)));
            c->goal = s->eggLeaf;
            c->task = T_WALK;
            c->then = T_IDLE;
        }
        if (c->visible) {
            c->len = S->len * INSTAR_LEN[0];
            c->R = fmaxf(0.9f, c->len * S->fat);
            cat_walk(s, dt, 1.6f);
            body_from_trail(c, c->len, c->pts);
        }
        if (t >= HATCH_T) {
            c->nwp = c->wpi = 0;
            wp_add(c, vadd(s->eggPos, vmul(vang(c->heading), -0.5f)));
            set_phase(s, P_SHELL);
        }
        break;
    case P_SHELL:
        if (vlen(vsub(c->head, s->eggPos)) > 2.2f && c->wpi < c->nwp) cat_walk(s, dt, 1.8f);
        else {
            c->bobPh += dt * 6;
            c->bob = sinf(c->bobPh) * 0.6f;
            s->eggEaten = fminf(1, s->eggEaten + dt / 3.5f);
            if (s->eggEaten >= 1) {
                s->egg = 0;
                c->instT = 0;
                cat_decide(s);
                set_phase(s, P_LARVA);
            }
        }
        c->len = S->len * INSTAR_LEN[0];
        c->R = fmaxf(0.9f, c->len * S->fat);
        body_from_trail(c, c->len, c->pts);
        if (c->bob != 0)
            for (int k = K * 3 / 4; k < K; k++) {
                float w = (float)(k - K * 3 / 4) / (K - 1 - K * 3 / 4);
                c->pts[k] = vadd(c->pts[k], vmul(vperp(vnorm(vsub(c->pts[k], c->pts[k - 1]))), c->bob * w * w));
            }
        break;
    case P_LARVA:
        cat_step(s, dt);
        if (c->task == T_DONE) set_phase(s, P_SILK);
        break;
    case P_SILK:
        s->silk = fminf(1, t / SILK_T);
        c->bobPh += dt * 7;
        body_from_trail(c, c->len, c->pts);
        for (int k = K * 3 / 4; k < K; k++) {
            float w = (float)(k - K * 3 / 4) / (K - 1 - K * 3 / 4);
            c->pts[k] = vadd(c->pts[k], vmul(vperp(vnorm(vsub(c->pts[k], c->pts[k - 1]))), sinf(c->bobPh) * c->R * 0.6f * w * w));
        }
        if (t >= SILK_T) { set_phase(s, P_HANG); memcpy(s->from, c->pts, sizeof c->pts); }
        break;
    case P_HANG: {
        const V2 *from = s->from;
        V2 pose[K];
        if (S->girdled) {
            pupa_frame(s, 0);
            for (int k = 0; k < K; k++) {
                float q = (float)k / (K - 1);
                pose[k] = vadd(vadd(s->P0, vmul(s->A, q * c->len * 0.92f)), v2(-0.6f * sinf(PI_F * q), 0));
            }
            float bl = smooth(t / 1.0f);
            for (int k = 0; k < K; k++) c->pts[k] = vlerp(from[k], pose[k], bl);
            s->girdle = smooth((t - 1.2f) / 2.0f);
        } else {
            V2 dd = vsub(from[K - 1], from[0]);
            float a0 = atan2f(dd.y, dd.x), sway = sinf(s->t * 1.3f) * 0.05f;
            float a = lerpf(a0, PI_F / 2, smooth(t / 1.4f)) + sway;
            body_hang(vadd(s->H, v2(0, 1.0f)), c->len * lerpf(1, 0.9f, smooth((t - 1.4f) / 2)), a, smooth((t - 1.4f) / 2.0f), pose);
            float bl = smooth(t / 0.4f);
            for (int k = 0; k < K; k++) c->pts[k] = vlerp(from[k], pose[k], bl);
        }
        if (t >= HANG_T) set_phase(s, P_PUPATE);
        break;
    }
    case P_PUPATE:
        pupa_frame(s, sinf(s->t * 9) * 0.1f * (1 - smooth((t - 1) / 3)));
        if (t >= 4.0f && !s->skin.falling && s->skin.on == 0 && t < 4.2f) {
            Skin *k = &s->skin;
            k->on = 1; k->falling = 1; k->t = 3; k->vy = 0; k->R = c->R * 0.9f; k->len = 4; k->inst = c->inst;
            for (int i = 0; i < K; i++) k->pts[i] = vadd(s->P0, vmul(s->A, 1.0f + 2.5f * i / (K - 1)));
        }
        if (t >= PUPATE_T) { s->skin.falling = 0; set_phase(s, P_CHRYS); }
        break;
    case P_CHRYS:
        pupa_frame(s, sinf(s->t * 0.9f) * 0.05f);
        if (t >= CHRYS_T) set_phase(s, P_EMERGE);
        break;
    case P_EMERGE:
        pupa_frame(s, 0);
        if (t >= EMERGE_T) set_phase(s, P_PUMP);
        break;
    case P_PUMP:
        pupa_frame(s, 0);
        if (t >= PUMP_T) set_phase(s, P_DRY);
        break;
    case P_DRY:
        pupa_frame(s, 0);
        if (t >= DRY_T) {
            set_phase(s, P_FLY);
            memset(f, 0, sizeof *f);
            f->on = 1;
            f->speed = 30;
            f->pos = vadd(adult_hang(s), v2(S->girdled ? -3.0f : 3.0f, 0));
            f->path[0] = vadd(f->pos, v2(-8, -18));
            f->path[1] = s->flower[irand(s->nflower)];
            f->npath = 2;
            s->fly.sitT = 0;
        }
        break;
    case P_FLY:
        if (f->sitting) {
            f->sitT -= dt;
            if (f->sitT <= 0) {
                f->sitting = 0;
                f->pi = 0;
                if (f->speed < 31 && s->nlawn) {
                    f->path[0] = s->lawn[irand(s->nlawn)];
                    f->npath = 1;
                    f->speed = 31;
                } else {
                    f->path[0] = v2(s->lw * frand(0.4f, 0.8f), s->lh * 0.12f);
                    f->path[1] = v2(irand(2) ? -30.0f : s->lw + 30.0f, s->lh * frand(0.0f, 0.15f));
                    f->npath = 2;
                    f->speed = 32;
                }
            }
        } else if (flyer_arrived(f) && f->speed < 32) {
            f->sitting = 1;
            f->sitT = 3.0f;
        }
        if (f->pos.x < -25 || f->pos.x > s->lw + 25) { f->on = 0; set_phase(s, P_FADE); }
        break;
    case P_FADE:
        break;
    }
    /* zoom in while the egg and caterpillar are tiny */
    int zoom = (s->phase == P_LAY && s->laid) || s->phase == P_EGG || s->phase == P_HATCH || s->phase == P_SHELL ||
               (s->phase == P_LARVA && c->inst < 2);
    V2 at = s->phase <= P_EGG ? vadd(s->eggPos, v2(0, -2.5f)) : vlerp(c->pts[K / 2], c->head, 0.3f);
    if (s->lensK <= 0.01f) s->lensAt = at;
    s->lensAt = vlerp(s->lensAt, at, fminf(1, dt * 3));
    s->lensK = clampf(s->lensK + (zoom ? dt : -dt) * 2, 0, 1);
}

/* ---------- draw ---------- */

static void copy_at(SDL_Renderer *r, const Garden *s, SDL_Texture *t, float x, float y, int w, int h, int flip) {
    float u = s->u;
    SDL_Rect d = { (int)floorf(x * u), (int)floorf(y * u), (int)ceilf(w * u), (int)ceilf(h * u) };
    SDL_RenderCopyEx(r, t, NULL, &d, 0, NULL, flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
}

static void dyn_show(SDL_Renderer *r, const Garden *s, Dyn *d, int alpha) {
    if (!d->used || !d->t) return;
    SDL_UpdateTexture(d->t, NULL, d->c.px, d->c.w * 4);
    SDL_SetTextureAlphaMod(d->t, (Uint8)alpha);
    copy_at(r, s, d->t, (float)d->ox, (float)d->oy, d->c.w, d->c.h, 0);
    d->used = 0;
}

static void draw_top(SDL_Renderer *r, Garden *s, V2 pos, int frame) {
    copy_at(r, s, s->top[frame], floorf(pos.x) - s->topCx, floorf(pos.y) - s->topCy, s->topW, s->topH, 0);
}

static int flap_frame(float t, float rate) {
    static const int seq[6] = { 0, 1, 2, 3, 2, 1 };
    return seq[(int)(t * rate) % 6];
}

/* The newly hatched butterfly by its chrysalis, from coming out to flying off. */
static void draw_adult(Garden *s, SDL_Renderer *r, int front) {
    const Species *S = &SPECIES[s->sp];
    float t = s->phaseT, k = 1, fat = 0;
    V2 shellEnd = vadd(s->P0, vmul(s->A, S->chLen));
    V2 A = v2(0, -1), D = v2(S->girdled ? -1.0f : 1.0f, 0), G;
    V2 hang = adult_hang(s);
    int behind = 0;
    if (s->phase == P_EMERGE) {
        if (t < 1.0f) return;
        float q = smooth((t - 1.0f) / 1.5f);
        k = 0.4f;
        fat = 1;
        if (S->girdled) {
            G = vlerp(vadd(s->P0, vmul(s->A, S->chLen * 0.4f)), hang, q);
            behind = t < 2.6f;
        } else {
            if (t < 2.6f) {
                A = v2(0, 1);
                D = v2(-1, 0);
                G = vlerp(vadd(s->P0, vmul(s->A, S->chLen * 0.3f)), vadd(shellEnd, v2(0, 3)), q);
                behind = 1;
            } else {
                float f = smooth((t - 2.6f) / 1.2f), ang = PI_F / 2 + f * PI_F;
                A = vang(ang);
                D = vperp(A);
                G = vlerp(vadd(shellEnd, v2(0, 3)), hang, f);
            }
        }
    } else if (s->phase == P_PUMP) {
        G = hang;
        k = 0.4f + 0.6f * smooth(t / PUMP_T);
        fat = 1 - smooth(t / PUMP_T);
    } else {
        G = hang;
    }
    if (behind != !front) return;
    if (s->phase == P_DRY && t > 2.2f) {             /* opens its wings slowly, twice */
        float o = 0.5f - 0.5f * cosf((t - 2.2f) / (DRY_T - 2.2f) * 4 * PI_F);
        if (o > 0.12f) {
            int fr = o > 0.85f ? 0 : o > 0.55f ? 1 : o > 0.3f ? 2 : 3;
            draw_top(r, s, vadd(G, vmul(D, 3.0f)), fr);
            return;
        }
    }
    dyn_start(&s->sideD, G.x, G.y);
    float sw = 1.15f + (s->phase == P_EMERGE ? 0.2f : 0) + 0.04f * sinf(s->t * 2);
    draw_side(&s->sideD, s->sp, G, A, D, sw, k, fat, 0);
    dyn_show(r, s, &s->sideD, 255);
}

static void draw_info(Garden *s, SDL_Renderer *r) {
    if (!g_show_info) return;
    char line[4][96];
    const Species *S = &SPECIES[s->sp];
    int st;
    float pr;
    stage_of(s, &st, &pr);
    snprintf(line[0], sizeof line[0], "%s ON %s", S->name, S->plantName);
    if (st == 1 && s->phase == P_LARVA) snprintf(line[1], sizeof line[1], "%s   SKIN %d OF 5", STAGE[st], s->cat.inst + 1);
    else snprintf(line[1], sizeof line[1], "%s", STAGE[st]);
    snprintf(line[2], sizeof line[2], "%s", activity(s));
    int day = day_of(s), eaten = 0, full = 0;
    for (int i = 0; i < s->nleaf; i++) { eaten += s->leaf[i].full - s->leaf[i].left; full += s->leaf[i].full; }
    if (day) snprintf(line[3], sizeof line[3], "ABOUT DAY %d OF 30   LEAF EATEN %d PER 100", day, full ? eaten * 100 / full : 0);
    else snprintf(line[3], sizeof line[3], "ROUND %d   TIME %d:%02d", s->cycles, (int)s->cycleT / 60, (int)s->cycleT % 60);
    int sc = s->h / 360 > 2 ? s->h / 360 : 2, step = 9 * sc, y0 = s->h - 12 - 4 * step;
    for (int i = 0; i < 4; i++) draw_text_shadow(r, line[i], 12, y0 + i * step, sc, hexcol(0xFFFFFF));
}

static void draw_ring(Garden *s, SDL_Renderer *r) {
    float u = s->u;
    int st;
    float pr;
    stage_of(s, &st, &pr);
    SDL_Rect pd = s->panel;
    SDL_RenderCopy(r, s->ringBg, NULL, &pd);
    float cx = s->ringCx, cy = s->ringCy, Rx = s->ringRx, Ry = s->ringRy, ic = s->iconHalf;
    V2 ip[4] = { v2(cx, cy - Ry), v2(cx + Rx, cy), v2(cx, cy + Ry), v2(cx - Rx, cy) };
    float pulse = 0.75f + 0.25f * sinf(s->t * 3);
    SDL_Color halo = { 255, 214, 90, (Uint8)(200 * pulse) };
    fill_circle(r, ip[st].x * u, ip[st].y * u, (ic + 1.6f) * u, halo);
    float a = -PI_F / 2 + (st + clampf(pr, 0, 1)) * PI_F / 2;
    fill_circle(r, (cx + cosf(a) * Rx) * u, (cy + sinf(a) * Ry) * u, 2.1f * u, hexcol(0xFFFFFF));
    fill_circle(r, (cx + cosf(a) * Rx) * u, (cy + sinf(a) * Ry) * u, 1.4f * u, hexcol(0xE8502A));
    for (int i = 0; i < 4; i++) {
        SDL_SetTextureAlphaMod(s->icon[i], i == st ? 255 : 140);
        copy_at(r, s, s->icon[i], ip[i].x - 7.5f, ip[i].y - 7.5f, 15, 15, 0);
    }
    if (!s->labels) return;
    for (int i = 0; i < 4; i++) {
        int w = text_width(STAGE[i], s->ts);
        SDL_Rect d = { (int)s->labelPos[i].x, (int)s->labelPos[i].y, w, 7 * s->ts };
        SDL_Color c = i == st ? hexcol(0xC8400C) : hexcol(0x8A7A62);
        SDL_SetTextureColorMod(s->label[i], c.r, c.g, c.b);
        SDL_RenderCopy(r, s->label[i], NULL, &d);
        if (i == st) { d.x += s->ts > 2 ? 1 : 0; SDL_RenderCopy(r, s->label[i], NULL, &d); }
    }
}

/* Copies part of a texture laid out in world pixels (at wx, wy) into the bubble, magnified. */
static void lens_copy(SDL_Renderer *r, const Garden *s, SDL_Texture *t, int wx, int wy, int w, int h, float M) {
    float k = s->u * M, half = s->lensK * 26 / M;
    int x0 = (int)floorf(s->lensAt.x - half) - 1, y0 = (int)floorf(s->lensAt.y - half) - 1, x1 = x0 + (int)(2 * half) + 3, y1 = y0 + (int)(2 * half) + 3;
    if (x0 < wx) x0 = wx;
    if (y0 < wy) y0 = wy;
    if (x1 > wx + w) x1 = wx + w;
    if (y1 > wy + h) y1 = wy + h;
    if (x1 <= x0 || y1 <= y0) return;
    SDL_Rect src = { x0 - wx, y0 - wy, x1 - x0, y1 - y0 };
    SDL_Rect dst = { (int)floorf(s->lensC.x * s->u + (x0 - s->lensAt.x) * k), (int)floorf(s->lensC.y * s->u + (y0 - s->lensAt.y) * k),
                     (int)ceilf((x1 - x0) * k), (int)ceilf((y1 - y0) * k) };
    SDL_RenderCopy(r, t, &src, &dst);
}

/* A round zoom bubble with a little tail pointing at the egg or the tiny caterpillar. */
static void draw_lens(Garden *s, SDL_Renderer *r) {
    if (s->lensK <= 0.01f) return;
    float u = s->u, rad = 26 * s->lensK, M = 2.5f;
    float panelBottom = (s->panel.y + s->panel.h) / u;
    /* beside it, away from the stem; above or below if there is no room at the side */
    float side = s->lensAt.x < s->stemX ? -1.0f : 1.0f;
    V2 c = vadd(s->lensAt, v2(side * 42, -14));
    c.x = clampf(c.x, rad + 3, s->lw - rad - 3);
    if (fabsf(c.x - s->lensAt.x) < rad + 8) c = v2(s->lensAt.x + side * 10, s->lensAt.y - 44);
    if (c.y - rad < panelBottom + 3 && c.x - rad < (s->panel.x + s->panel.w) / u) c.y = panelBottom + rad + 4;
    c.y = clampf(c.y, rad + 3, s->lh - rad - 3);
    s->lensC = c;
    V2 d = vnorm(vsub(s->lensAt, c)), n = vperp(d);
    V2 tip = vsub(s->lensAt, vmul(d, 5));
    SDL_Color rim = hexcol(0xFFFFFF), dark = hexcol(0x5A4630);
    fill_triangle(r, (c.x + n.x * rad * 0.35f) * u, (c.y + n.y * rad * 0.35f) * u, (c.x - n.x * rad * 0.35f) * u,
                  (c.y - n.y * rad * 0.35f) * u, tip.x * u, tip.y * u, rim);
    fill_circle(r, c.x * u, c.y * u, (rad + 2.2f) * u, dark);
    fill_circle(r, c.x * u, c.y * u, (rad + 1.4f) * u, rim);
    SDL_Rect old;
    SDL_bool had = SDL_RenderIsClipEnabled(r);
    SDL_RenderGetClipRect(r, &old);
    int hs = u > 2 ? (int)u : 2, cy = (int)(c.y * u), R = (int)(rad * u);
    for (int y = cy - R; y < cy + R; y += hs) {
        float dy = y + hs * 0.5f - cy, half = sqrtf(fmaxf(0, (float)R * R - dy * dy));
        SDL_Rect clip = { (int)(c.x * u - half), y, (int)(2 * half), hs };
        if (clip.w <= 0) continue;
        SDL_RenderSetClipRect(r, &clip);
        lens_copy(r, s, s->sky, 0, 0, s->lw, s->lh, M);
        lens_copy(r, s, s->land, 0, 0, s->lw, s->lh, M);
        lens_copy(r, s, s->twigTex, 0, 0, s->lw, s->lh, M);
        lens_copy(r, s, s->plantTex, 0, 0, s->lw, s->lh, M);
        for (int i = 0; i < s->nleaf; i++) lens_copy(r, s, s->leaf[i].t, s->leaf[i].ox, s->leaf[i].oy, s->leaf[i].c.w, s->leaf[i].c.h, M);
        if (s->drewSkin) lens_copy(r, s, s->skinD.t, s->skinD.ox, s->skinD.oy, s->skinD.c.w, s->skinD.c.h, M);
        if (s->drewEgg) lens_copy(r, s, s->eggD.t, s->eggD.ox, s->eggD.oy, s->eggD.c.w, s->eggD.c.h, M);
        if (s->drewCat) lens_copy(r, s, s->catD.t, s->catD.ox, s->catD.oy, s->catD.c.w, s->catD.c.h, M);
    }
    if (had) SDL_RenderSetClipRect(r, &old);
    else SDL_RenderSetClipRect(r, NULL);
}

static void draw(void *p, SDL_Renderer *r) {
    Garden *s = p;
    const Species *S = &SPECIES[s->sp];
    float u = s->u, t = s->phaseT;
    int lwU = (int)ceilf(s->lw * u), lhU = (int)ceilf(s->lh * u);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_Rect full = { 0, 0, lwU, lhU };
    SDL_RenderCopy(r, s->sky, NULL, &full);
    static const int raySeq[4] = { 0, 1, 2, 1 };
    copy_at(r, s, s->rays[raySeq[(int)(s->t / 1.1f) % 4]], s->sunPos.x - RAYS_D / 2.0f, s->sunPos.y - RAYS_D / 2.0f, RAYS_D, RAYS_D, 0);
    copy_at(r, s, s->sun, s->sunPos.x - SUN_D / 2.0f, s->sunPos.y - SUN_D / 2.0f, SUN_D, SUN_D, 0);
    for (int i = 0; i < NCLOUD; i++) {
        const Cloud *c = &s->cloud[i];
        copy_at(r, s, s->cloudTex[c->shape], floorf(c->x), floorf(c->y), s->cloudW[c->shape], s->cloudH[c->shape], 0);
    }
    SDL_RenderCopy(r, s->land, NULL, &full);
    SDL_RenderCopy(r, s->twigTex, NULL, &full);
    SDL_RenderCopy(r, s->plantTex, NULL, &full);
    for (int i = 0; i < s->nleaf; i++) {
        Leaf *l = &s->leaf[i];
        if (l->dirty) { SDL_UpdateTexture(l->t, NULL, l->c.px, l->c.w * 4); l->dirty = 0; }
        copy_at(r, s, l->t, (float)l->ox, (float)l->oy, l->c.w, l->c.h, 0);
    }
    /* silk pad and the ladybug */
    if (s->silk > 0 && !S->girdled) {
        SDL_SetRenderDrawColor(r, 255, 255, 255, (Uint8)(230 * s->silk));
        SDL_Rect pad = { (int)floorf((s->H.x - 1) * u), (int)floorf((s->H.y + 0.6f) * u), (int)ceilf(3 * u), (int)ceilf(1 * u) };
        SDL_RenderFillRect(r, &pad);
    }
    if (s->bug.leaf >= 0 || s->bug.flying) {
        dyn_start(&s->bugD, s->bug.pos.x, s->bug.pos.y);
        draw_ladybug(&s->bugD, s->bug.pos, s->bug.heading, s->bug.flying, s->bug.legPh);
        dyn_show(r, s, &s->bugD, 255);
    }
    /* shed skin */
    if (s->skin.on) {
        Skin *k = &s->skin;
        dyn_start(&s->skinD, k->pts[K / 2].x, k->pts[K / 2].y);
        draw_cat(&s->skinD, k->pts, fmaxf(0.7f, k->R * (1 - 0.3f * fminf(1, k->t / 2))), k->len, s->sp, k->inst, -1, 1);
        dyn_show(r, s, &s->skinD, (int)(255 * clampf((6 - k->t) / 2, 0, 1)));
        s->drewSkin = 1;
    }
    /* the egg */
    if (s->egg) {
        int lean = 0;
        if (s->phase == P_EGG) {
            float burst = sinf(s->t * 0.9f) > 0.75f - 0.4f * t / EGG_T ? 1 : 0;
            lean = burst ? (int)lroundf(sinf(s->t * 11)) : 0;
        }
        float dev = s->phase == P_EGG ? smooth(t / EGG_T) : s->phase > P_EGG ? 1 : 0;
        dyn_start(&s->eggD, s->eggPos.x, s->eggPos.y - 3);
        draw_egg(&s->eggD, s->sp, s->eggPos, dev, lean, s->phase >= P_HATCH ? s->crack : 0, s->eggEaten, 1.0f);
        if (s->phase == P_LAY && t < 2.4f) {                 /* a little sparkle as it is laid */
            float q = (t - 1.7f) / 0.7f;
            if (q > 0 && q < 1)
                for (int i = 0; i < 4; i++) {
                    V2 sp = vadd(s->eggPos, vmul(vang(i * PI_F / 2 + 0.6f), 3 + q * 3));
                    dset(&s->eggD, (int)sp.x, (int)sp.y - 3, rgb(0xFFF6B0));
                }
        }
        dyn_show(r, s, &s->eggD, 255);
        s->drewEgg = 1;
    }
    /* caterpillar */
    int showCat = s->cat.visible && (s->phase >= P_HATCH && s->phase <= P_PUPATE);
    if (s->phase == P_PUPATE && t >= 4.0f) showCat = 0;
    if ((s->phase >= P_PUPATE && s->phase <= P_FLY) || (s->phase == P_FADE && !s->fade)) {   /* the shell stays after */
        float pp = s->phase == P_CHRYS ? t / CHRYS_T : s->phase > P_CHRYS ? 1 : 0;
        float stretch = s->phase == P_PUPATE ? 1.25f - 0.25f * smooth((t - 1) / 3.5f) : 1;
        float open = s->phase >= P_EMERGE && !(s->phase == P_EMERGE && t < 1.0f) ? 1 : 0;
        float crack = s->phase == P_EMERGE ? smooth(t / 1.0f) : 0;
        int adult = s->phase >= P_EMERGE && s->phase <= P_DRY;
        if (adult) draw_adult(s, r, 0);
        if (s->phase != P_PUPATE || t >= 1.0f) {
            dyn_start(&s->pupaD, s->P0.x, s->P0.y + s->A.y * 8);
            draw_chrysalis(&s->pupaD, s->sp, s->P0, s->A, pp, 1, stretch, open, crack);
            if (S->girdled) {                                /* the silk belt */
                V2 m = vadd(s->P0, vmul(s->A, S->chLen * 0.45f));
                dline(&s->pupaD, vadd(m, v2(-S->chW, 0.5f)), v2(s->stickX + 1.2f, m.y + 1.6f), RGBA(0xFFFFFF, 0xE0));
            }
            dyn_show(r, s, &s->pupaD, 255);
        }
        if (adult) draw_adult(s, r, 1);
    }
    if (showCat) {
        Cat *c = &s->cat;
        V2 pts[K];
        memcpy(pts, c->pts, sizeof pts);
        float R = c->R;
        if (s->phase == P_PUPATE) {                          /* straightens, then the skin slides up and off */
            float q = smooth(t / 1.0f), L = lerpf(c->len * 0.9f, S->chLen * 1.15f, q);
            if (S->girdled) {
                for (int k = 0; k < K; k++) pts[k] = vadd(s->P0, vmul(s->A, L * k / (K - 1)));
            } else body_hang(vadd(s->H, v2(0, 1.0f)), L, PI_F / 2, 1 - q, pts);
            if (t >= 1.0f) {
                float a = lerpf(S->chLen * 1.15f, 2.5f, smooth((t - 1.0f) / 3.0f));
                for (int k = 0; k < K; k++) pts[k] = vadd(s->P0, vmul(s->A, a * k / (K - 1)));
                R = c->R * (1.1f + 0.4f * smooth((t - 1.0f) / 3.0f));
            }
        }
        dyn_start(&s->catD, (pts[0].x + pts[K - 1].x) / 2, (pts[0].y + pts[K - 1].y) / 2);
        float bulge = s->phase == P_LARVA && c->task == T_WALK ? fmodf(c->wave / (2 * PI_F), 1.0f) : -1;
        draw_cat(&s->catD, pts, R, c->len, s->sp, c->inst, bulge, 0);
        if (S->girdled && s->phase == P_HANG && s->girdle > 0) {
            V2 m = pts[(int)(K * 0.6f)];
            V2 end = vlerp(m, v2(s->stickX + 1.2f, m.y + 1.6f), s->girdle);
            dline(&s->catD, vadd(m, v2(-c->R, 0.5f)), end, rgb(0xFFFFFF));
        }
        if (s->phase == P_SILK && !S->girdled)
            for (int i = 0; i < (int)(s->silk * 6); i++) dset(&s->catD, (int)(s->H.x - 2 + i % 4), (int)(s->H.y + 1 + i / 4), rgb(0xFFFFFF));
        dyn_show(r, s, &s->catD, 255);
        s->drewCat = 1;
    }
    /* crumbs */
    for (int i = 0; i < s->ncrumb; i++) {
        const Crumb *q = &s->crumb[i];
        SDL_Color c = hexcol(q->col);
        c.a = (Uint8)(255 * clampf(q->life, 0, 1));
        set_color(r, c);
        SDL_Rect rc = { (int)floorf(q->x * u), (int)floorf(q->y * u), (int)ceilf(u), (int)ceilf(u) };
        SDL_RenderFillRect(r, &rc);
    }
    /* the mother laying, or a butterfly flying */
    Flyer *f = &s->fly;
    if (f->on) {
        if (s->phase == P_LAY && f->sitting) {
            float curl = smooth((t - 0.5f) / 1.0f) * (1 - smooth((t - 2.0f) / 0.6f));
            V2 A = v2((float)s->side, 0), D = v2(0, -1);
            dyn_start(&s->sideD, s->grip.x, s->grip.y - 8);
            draw_side(&s->sideD, s->sp, s->grip, A, D, 0.22f + 0.05f * sinf(s->t * 3), 1, 0, curl);
            dyn_show(r, s, &s->sideD, 255);
        } else {
            int fr = f->sitting ? (int)(1.5f + 1.5f * sinf(f->flap * 1.5f)) : flap_frame(f->flap, 11);
            if (!f->sitting && f->pi == f->npath - 1 && vlen(vsub(f->path[f->pi], f->pos)) < 6) fr = flap_frame(f->flap, 6);
            draw_top(r, s, f->pos, fr);
        }
    }
    /* the bee */
    {
        Bee *b = &s->bee;
        int fr = b->sitting ? 1 : (int)(s->t * 30) % 2;
        copy_at(r, s, s->beeTex[fr], floorf(b->pos.x) - 6, floorf(b->pos.y) - 8, 13, 10, b->dir < 0);
    }
    draw_lens(s, r);
    s->drewEgg = s->drewCat = s->drewSkin = 0;
    draw_ring(s, r);
    draw_info(s, r);
    if (s->phase == P_FADE) {
        float a = t < FADE_T / 2 ? t / (FADE_T / 2) : 1 - (t - FADE_T / 2) / (FADE_T / 2);
        SDL_SetRenderDrawColor(r, 0xEA, 0xF6, 0xFF, (Uint8)(255 * clampf(a, 0, 1)));
        SDL_RenderFillRect(r, NULL);
    }
}

/* The fade between rounds swaps in the next butterfly's plant halfway, when the screen is pale. */
static void fade_tick(Garden *s, SDL_Renderer *r) {
    if (s->phase == P_FADE && s->phaseT >= FADE_T / 2 && !s->fade) {
        new_cycle(s, r);
        place_mother(s, 0);
        s->fade = 1;
    }
    if (s->phase == P_FADE && s->phaseT >= FADE_T) { s->fade = 0; set_phase(s, P_ARRIVE); }
}

/* ---------- lifecycle ---------- */

static void step_outer(void *p, float dt) {
    Garden *s = p;
    step(p, dt);
    fade_tick(s, s->r);
}

static void *create(SDL_Renderer *r, int w, int h) {
    Garden *s = calloc(1, sizeof *s);
    s->r = r;
    s->w = w;
    s->h = h;
    s->u = h >= 360 ? (float)(int)lroundf(h / 180.0f) : h / 180.0f;
    if (s->u <= 0) s->u = 1;
    s->lw = (int)ceilf(w / s->u);
    s->lh = (int)ceilf(h / s->u);
    s->labels = h >= 300;
    s->ts = (int)lroundf(s->u * 0.5f);
    if (s->ts < 1) s->ts = 1;
    s->gy = s->lh * 0.86f;
    s->sunPos = v2(s->lw * 0.86f, s->lh * 0.14f);
    s->lastSp = -1;
    s->bug.leaf = -1;
    Canvas sky = make_sky(s);
    s->sky = canvas_texture(r, &sky);
    Canvas sun = make_sun();
    s->sun = canvas_texture(r, &sun);
    for (int i = 0; i < 3; i++) { Canvas c = make_rays(i); s->rays[i] = canvas_texture(r, &c); }
    for (int i = 0; i < 4; i++) {
        Canvas c = make_cloud(26 + i * 6 + irand(4));
        s->cloudW[i] = c.w;
        s->cloudH[i] = c.h;
        s->cloudTex[i] = canvas_texture(r, &c);
    }
    for (int i = 0; i < NCLOUD; i++) {
        s->cloud[i].shape = irand(4);
        s->cloud[i].x = (i + frand(0.1f, 0.9f)) / NCLOUD * (s->lw + 40) - 40;
        s->cloud[i].y = frand(0.03f, 0.3f) * s->lh;
        s->cloud[i].v = frand(1.2f, 2.6f);
    }
    for (int i = 0; i < 2; i++) { Canvas c = make_bee(i); s->beeTex[i] = canvas_texture(r, &c); }
    dyn_make(r, &s->catD, 80, 80);
    dyn_make(r, &s->skinD, 80, 80);
    dyn_make(r, &s->pupaD, 56, 56);
    dyn_make(r, &s->sideD, 72, 72);
    dyn_make(r, &s->eggD, 20, 20);
    dyn_make(r, &s->bugD, 16, 16);
    make_ring(s, r);                     /* lays out the ring panel, which the plant keeps clear of */
    s->stemX = fmaxf(s->lw * 0.34f, (s->panel.x + s->panel.w) / s->u + 14);
    new_cycle(s, r);
    Canvas land = make_land(s);          /* after the plant, so lawn flowers come after the plant's */
    s->land = canvas_texture(r, &land);
    place_mother(s, 1);
    s->bee.pos = v2(s->lw * 0.15f, s->lh * 0.5f);
    s->bee.sitting = 1;
    s->bee.timer = 1;
    s->bee.dir = 1;
    set_phase(s, P_ARRIVE);
    return s;
}

static void destroy(void *p) {
    Garden *s = p;
    SDL_Texture *all[] = { s->sky, s->land, s->sun, s->rays[0], s->rays[1], s->rays[2], s->cloudTex[0], s->cloudTex[1],
                           s->cloudTex[2], s->cloudTex[3], s->beeTex[0], s->beeTex[1], s->plantTex, s->twigTex, s->ringBg,
                           s->icon[0], s->icon[1], s->icon[2], s->icon[3], s->label[0], s->label[1], s->label[2],
                           s->label[3], s->top[0], s->top[1], s->top[2], s->top[3] };
    for (size_t i = 0; i < sizeof all / sizeof all[0]; i++) if (all[i]) SDL_DestroyTexture(all[i]);
    for (int i = 0; i < s->nleaf; i++) { if (s->leaf[i].t) SDL_DestroyTexture(s->leaf[i].t); free(s->leaf[i].c.px); }
    Dyn *ds[] = { &s->catD, &s->skinD, &s->pupaD, &s->sideD, &s->eggD, &s->bugD };
    for (int i = 0; i < 6; i++) { if (ds[i]->t) SDL_DestroyTexture(ds[i]->t); free(ds[i]->c.px); }
    free(s);
}

/* A new egg has just been laid: the natural place to cut away. */
static int at_rest(void *p) {
    const Garden *s = p;
    return s->phase == P_EGG;
}

static int keep_clear(void *p, SDL_Rect *out, int max) {
    const Garden *s = p;
    int n = 0;
    if (max > n) out[n++] = s->panel;
    if (g_show_info && max > n) {
        int sc = s->h / 360 > 2 ? s->h / 360 : 2;
        out[n++] = (SDL_Rect){ 0, s->h - 24 - 4 * 9 * sc, 12 + text_width("ABOUT DAY 30 OF 30   LEAF EATEN 100 PER 100", sc) + 12, 24 + 4 * 9 * sc };
    }
    return n;
}

const Saver saver_butterfly = { "butterfly", "BUTTERFLY", create, step_outer, draw, destroy, at_rest, keep_clear,
                                "B EMERGE   F TO EMERGE   I INFO" };
