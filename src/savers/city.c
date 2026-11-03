/* City: a sunny street in pixel art. Towers, brick blocks, little shops and houses line a sidewalk
   where people stroll, stand about on their phones, wear very cool sunglasses, walk dogs (one of
   them a corgi with a very big butt) and ride bikes, while cars and a bus stop at the lights.
   Cats sit on fences, awnings and roofs or stroll along, pigeons peck at the pavement, chimneys
   smoke and flocks of birds cross the sky. */
#include "../engine.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define ROWS 360              /* the scene is drawn on a grid at least this many pixels tall */
#define MAXA 110
#define MAXB 90
#define MAXCLOUD 8
#define MAXPERCH 40
#define MAXSPOT 24
#define MAXPROP 6

enum { R_WALKER, R_SHADES, R_STANDER, R_DOGWALK, R_BIGBUTT, R_BIKE, R_CAR, R_BUS, R_CATWALK,
       R_CATSIT, R_PIGEON };
enum { S_WALK, S_STAND, S_SNIFF, S_FLEE, S_AWAY };

static const uint32_t SKIN[6] = { 0xF8D8BC, 0xF0C29C, 0xD9A070, 0xB57A4C, 0x8D5A3A, 0x5E3A24 };
static const uint32_t SHIRT[12] = { 0xD94F4F, 0x3F7FD6, 0x4FB86A, 0xF2C230, 0xB45FD0, 0xF28A3C, 0xEDEDED,
                                    0x2E9C9C, 0xE86FA0, 0x555B6E, 0x2A2A30, 0x8AC8F0 };
static const uint32_t PANTS[7] = { 0x2B3A67, 0x3A3A3A, 0x6B4F2E, 0x5A6B7E, 0x7A3A3A, 0x2F5D3A, 0xC8B48A };
static const uint32_t HAIRC[7] = { 0x2A1A10, 0x6B3E1E, 0xD9B05A, 0x141414, 0xB04A1E, 0x9A9A9A, 0x4A2A18 };
static const uint32_t SHOES[5] = { 0x2A2A2A, 0xF4F4F4, 0x7A4424, 0xD94F4F, 0x3F6FB6 };
static const uint32_t CARC[8] = { 0xD23A3A, 0x2F6FD0, 0x3AA860, 0xE8E8E8, 0x6A3AA8, 0xE87A2A, 0x2A2E38, 0x8AA0B8 };
static const uint32_t CATC[5][3] = {           /* body, dark, belly */
    { 0xE8923A, 0xB5651C, 0xFFE2BC }, { 0x2A2A2E, 0x151517, 0x55555C }, { 0x9AA0AA, 0x666C78, 0xE4E8EE },
    { 0xF4F0E8, 0xC8A27A, 0xFFFFFF }, { 0xC8924E, 0x6E4A22, 0xF0D8A8 },
};

typedef struct { uint32_t c[3]; int L, legH, bh, pointy, spots, curl; } Breed;
static const Breed BREEDS[6] = {
    { { 0xD9A05B, 0x9C6A2E, 0xF5DDB0 }, 22, 7, 8, 0, 0, 0 },   /* golden */
    { { 0x34343A, 0x18181C, 0x5A5A62 }, 22, 7, 8, 0, 0, 0 },   /* black lab */
    { { 0xF4F2EC, 0x2A2A2E, 0xFFFFFF }, 22, 7, 7, 0, 1, 0 },   /* spotty */
    { { 0x8A4E28, 0x52301A, 0xC08A5E }, 26, 3, 6, 0, 0, 0 },   /* sausage dog */
    { { 0x9AA0AA, 0x4A505A, 0xF0F2F6 }, 22, 7, 8, 1, 0, 1 },   /* husky */
    { { 0xE8D4A8, 0xA88A5A, 0xFFF6E0 }, 14, 4, 6, 1, 0, 0 },   /* little terrier */
};
static const uint32_t CORGI[3] = { 0xE0913E, 0xA35F1E, 0xFFF6EA };

typedef struct {
    int role, state, dir, pose, glasses, helmet, perch, hairStyle, outfit, prop, kid, breed, basket, kind, stripes;
    float x, y, vx, v0, ph, t, timer, tx, glint;
    uint32_t skin, shirt, shirt2, pants, hair, cap, shoe, car;
    const uint32_t *pet;
} Actor;

typedef struct { float x, y, vx, ph, size; int gull; } Bird;
typedef struct { float x, y, v; int k; } Cloud;
typedef struct { int x, y; float ph; } Spot;
/* Street furniture that people walk in front of or behind, so it is sorted with them. */
typedef struct { SDL_Texture *t; int x, y, w, h, base, light; } Prop;

typedef struct {
    int w, h, px, W, H;
    float scale;                                   /* < 1: a small view (a picker tile) drawn shrunk */
    SDL_Texture *small;                            /* where a small view is drawn before shrinking */
    int back, curb, laneBike, laneL, laneR;        /* y rows of the scene */
    int crossX, crossW, lightX, lightY, stopX;
    float t;
    SDL_Texture *sky, *far, *near, *cloudTex[3];
    int cloudW[3], cloudH[3];
    Actor a[MAXA];
    int na;
    Bird bird[MAXB];
    int nb;
    Cloud cloud[MAXCLOUD];
    Spot perch[MAXPERCH], chimney[MAXSPOT], antenna[MAXSPOT];
    int nperch, nchim, nant;
    Prop prop[MAXPROP];
    int nprop;
} City;

/* ---------- colours ---------- */

static uint32_t shade(uint32_t hex, float k) {
    int r = (int)(((hex >> 16) & 255) * k), g = (int)(((hex >> 8) & 255) * k), b = (int)((hex & 255) * k);
    return ((uint32_t)(r > 255 ? 255 : r) << 16) | ((uint32_t)(g > 255 ? 255 : g) << 8) | (uint32_t)(b > 255 ? 255 : b);
}

static uint32_t mix(uint32_t a, uint32_t b, float k) {
    int r = (int)(((a >> 16) & 255) * (1 - k) + ((b >> 16) & 255) * k);
    int g = (int)(((a >> 8) & 255) * (1 - k) + ((b >> 8) & 255) * k);
    int bl = (int)((a & 255) * (1 - k) + (b & 255) * k);
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)bl;
}

/* ---------- drawing on the renderer, in scene pixels ---------- */

static void box(const City *s, SDL_Renderer *r, int x, int y, int w, int h, uint32_t hex, int a) {
    if (w <= 0 || h <= 0) return;
    SDL_SetRenderDrawColor(r, (hex >> 16) & 255, (hex >> 8) & 255, hex & 255, (Uint8)a);
    SDL_Rect d = { x * s->px, y * s->px, w * s->px, h * s->px };
    SDL_RenderFillRect(r, &d);
}
#define R(x, y, w, h, c) box(s, r, (x), (y), (w), (h), (c), 255)
/* In an actor's own frame: o is forward of X (mirrored when facing left), y is absolute. */
#define F(o, y, w, h, c) box(s, r, D > 0 ? X + (o) : X - (o) - (w) + 1, (y), (w), (h), (c), 255)

static void ell(const City *s, SDL_Renderer *r, float cx, float cy, float rx, float ry, uint32_t c, int a) {
    int n = (int)ceilf(ry);
    for (int dy = -n; dy <= n; dy++) {
        float k = 1 - (dy * dy) / ((ry + 0.5f) * (ry + 0.5f));
        if (k <= 0) continue;
        int half = (int)roundf(rx * sqrtf(k));
        box(s, r, (int)roundf(cx) - half, (int)roundf(cy) + dy, 2 * half + 1, 1, c, a);
    }
}
#define FE(o, y, rx, ry, c) ell(s, r, D > 0 ? X + (o) : X - (o), (y), (rx), (ry), (c), 255)

static void line(const City *s, SDL_Renderer *r, float x0, float y0, float x1, float y1, int t, uint32_t c) {
    float dx = x1 - x0, dy = y1 - y0;
    int n = (int)ceilf(fmaxf(fabsf(dx), fabsf(dy)));
    for (int i = 0; i <= n; i++) {
        float k = n ? (float)i / n : 0;
        R((int)roundf(x0 + dx * k) - t / 2, (int)roundf(y0 + dy * k) - t / 2, t, t, c);
    }
}
#define FL(o0, y0, o1, y1, t, c) line(s, r, X + D * (o0), (y0), X + D * (o1), (y1), (t), (c))

/* ---------- drawing on canvases (the still scenery) ---------- */

static void rect(Canvas *c, int x, int y, int w, int h, uint32_t hex) {
    if (w > 0 && h > 0) fill_rect(c, x, y, x + w - 1, y + h - 1, rgb(hex));
}
static void dot(Canvas *c, int x, int y, uint32_t hex) { put(c, x, y, rgb(hex)); }

static void cline(Canvas *c, int x0, int y0, int x1, int y1, uint32_t hex) {
    int n = abs(x1 - x0) > abs(y1 - y0) ? abs(x1 - x0) : abs(y1 - y0);
    for (int i = 0; i <= n; i++) {
        float k = n ? (float)i / n : 0;
        dot(c, (int)roundf(x0 + (x1 - x0) * k), (int)roundf(y0 + (y1 - y0) * k), hex);
    }
}

static void ring(Canvas *c, float cx, float cy, float rad, uint32_t hex) {
    for (int i = 0; i < 32; i++) {
        float a = i * 2 * PI_F / 32;
        dot(c, (int)roundf(cx + cosf(a) * rad), (int)roundf(cy + sinf(a) * rad), hex);
    }
}

static void stamp_text(Canvas *c, const char *str, int x, int y, uint32_t hex) {
    Canvas t = text_canvas(str, rgb(hex));
    for (int j = 0; j < t.h; j++)
        for (int i = 0; i < t.w; i++)
            if (t.px[j * t.w + i]) put(c, x + i, y + j, t.px[j * t.w + i]);
    free(t.px);
}

static void add_spot(Spot *list, int *n, int x, int y) {
    if (*n < MAXSPOT) list[(*n)++] = (Spot){ x, y, frand(0, 6.28f) };
}
static void add_perch(City *s, int x, int y) {
    if (s->nperch < MAXPERCH) s->perch[s->nperch++] = (Spot){ x, y, 0 };
}

static void window(Canvas *c, int x, int y, int w, int h, uint32_t frame, int mullion) {
    rect(c, x - 1, y - 1, w + 2, h + 2, frame);
    int k = rand() % 12;
    uint32_t glass = k < 2 ? 0xFFE08A : k < 4 ? 0x6F95BE : 0x93BEE0;
    rect(c, x, y, w, h, glass);
    rect(c, x, y, w, 1, k < 2 ? 0xFFF2C4 : 0xC4E2F6);
    if (k >= 2 && rand() % 2)                                        /* a glint across the glass */
        for (int i = 0; i < h - 2 && i < w - 1; i++) dot(c, x + w - 2 - i, y + 1 + i, 0xDCF0FC);
    if (k < 2 && w >= 6) {                                           /* lit, with curtains */
        rect(c, x, y, 2, h, 0xE07A7A);
        rect(c, x + w - 2, y, 2, h, 0xE07A7A);
        if (rand() % 6 == 0) {                                       /* a cat looking out */
            rect(c, x + 2, y + h - 3, 3, 3, 0x2A2A2E);
            dot(c, x + 2, y + h - 4, 0x2A2A2E); dot(c, x + 4, y + h - 4, 0x2A2A2E);
        }
    } else if (k == 4) {
        for (int yy = y + 1; yy < y + h; yy += 2) rect(c, x, yy, w, 1, 0xE4E0D4);   /* blinds */
    }
    if (mullion) { rect(c, x + w / 2, y, 1, h, frame); rect(c, x, y + h / 2, w, 1, frame); }
    rect(c, x - 2, y + h + 1, w + 4, 1, shade(frame, 1.3f));
}

static void bricks(Canvas *c, int x0, int y0, int x1, int y1, uint32_t col) {
    for (int y = y0; y <= y1; y++) {
        int row = (y - y0) / 4;
        for (int x = x0; x <= x1; x++) {
            if ((y - y0) % 4 == 3) dot(c, x, y, shade(col, 1.18f));
            else if ((x - x0 + (row % 2) * 4) % 8 == 0) dot(c, x, y, shade(col, 1.12f));
            else if ((x * 7 + y * 13) % 23 == 0) dot(c, x, y, shade(col, 0.9f));
        }
    }
}

static void cornice(Canvas *c, int x, int top, int w, uint32_t col) {
    rect(c, x - 2, top - 4, w + 4, 4, shade(col, 0.72f));
    rect(c, x - 2, top - 4, w + 4, 1, shade(col, 1.15f));
    for (int i = 0; i < w; i += 3) dot(c, x + i, top, shade(col, 0.7f));
}

static void tree(Canvas *c, int x, int base, int h, int big) {
    int tw = big ? 5 : 4, rad = big ? 16 : 11;
    rect(c, x - tw / 2, base - h, tw, h, 0x6B4423);
    rect(c, x + tw / 2 - 1, base - h, 1, h, 0x4E301A);
    cline(c, x, base - h + 4, x - 6, base - h - 2, 0x6B4423);
    cline(c, x, base - h + 6, x + 7, base - h - 1, 0x6B4423);
    int blossom = rand() % 5 == 0;
    uint32_t g = blossom ? 0xF0A0C0 : (rand() % 2 ? 0x3E9A48 : 0x4FAE4E);
    float cy = base - h - rad * 0.55f;
    fill_ellipse(c, (float)x + 2, cy + 2, rad + 1.5f, rad * 0.85f + 1, rgb(shade(g, 0.72f)));
    fill_ellipse(c, (float)x - rad * 0.45f, cy + 1, rad * 0.7f, rad * 0.6f, rgb(shade(g, 0.86f)));
    fill_ellipse(c, (float)x + rad * 0.4f, cy, rad * 0.75f, rad * 0.65f, rgb(g));
    fill_ellipse(c, (float)x - rad * 0.1f, cy - rad * 0.35f, rad * 0.75f, rad * 0.55f, rgb(g));
    fill_ellipse(c, (float)x - rad * 0.3f, cy - rad * 0.55f, rad * 0.35f, rad * 0.25f, rgb(shade(g, 1.18f)));
    for (int i = 0; i < rad * 6; i++) {                              /* leaves */
        int dx = (int)frand(-rad, (float)rad), dy = (int)frand(-rad * 0.8f, rad * 0.8f);
        uint32_t p = get(c, x + dx, (int)cy + dy);
        if (p && p != rgb(0x6B4423)) dot(c, x + dx, (int)cy + dy, rand() % 2 ? shade(g, 1.2f) : shade(g, 0.75f));
    }
}

/* ---------- the buildings ---------- */

static int tower(City *s, Canvas *c, int x, int base) {
    int style = irand(3), w = 54 + irand(4) * 10, h = (int)(s->back * (0.45f + 0.27f * frand(0, 1)));
    static const uint32_t glassC[3] = { 0x6F8FB0, 0x5E7E9A, 0x7A96B4 };
    static const uint32_t stoneC[3] = { 0xC9B79A, 0xB8A58A, 0xD4C6A8 };
    static const uint32_t brickC[3] = { 0xA8553E, 0x9A4A3A, 0xB46446 };
    uint32_t col = style == 0 ? glassC[irand(3)] : style == 1 ? stoneC[irand(3)] : brickC[irand(3)];
    int top = base - h;
    rect(c, x, top, w, h, col);
    if (style == 2) bricks(c, x, top, x + w - 1, base, col);
    rect(c, x, top, 2, h, shade(col, 1.12f));
    rect(c, x + w - 3, top, 3, h, shade(col, 0.82f));
    cornice(c, x, top, w, col);
    uint32_t frame = style == 0 ? shade(col, 0.65f) : style == 1 ? 0x7A6A55 : 0xEDE6D8;
    int cw = style == 0 ? 9 : 10, chh = style == 0 ? 11 : 14, ww = style == 0 ? 7 : 5, wh = style == 0 ? 9 : 8;
    int cols = (w - 8) / cw, x0 = x + (w - cols * cw) / 2 + (cw - ww) / 2;
    for (int y = top + 8, row = 0; y + wh < base - 26; y += chh, row++) {
        if (style == 1 && row % 2 == 0) rect(c, x + 2, y - 3, w - 5, 1, shade(col, 0.86f));
        for (int i = 0; i < cols; i++) window(c, x0 + i * cw, y, ww, wh, frame, style == 0 && ww >= 7);
    }
    int dx = x + w / 2;                                              /* the way in */
    rect(c, dx - 12, base - 24, 24, 3, shade(col, 0.55f));
    rect(c, dx - 12, base - 24, 24, 1, shade(col, 0.9f));
    rect(c, dx - 8, base - 20, 16, 20, 0x3A3A44);
    rect(c, dx - 7, base - 19, 6, 19, 0xA8D4F0);
    rect(c, dx + 1, base - 19, 6, 19, 0xA8D4F0);
    dot(c, dx - 2, base - 10, 0xE8E8E8); dot(c, dx + 1, base - 10, 0xE8E8E8);
    rect(c, dx - 10, base - 1, 20, 1, 0xD8D2C4);
    int roof = irand(3);
    if (roof == 0) {                                                 /* water tower */
        int tx = x + 8 + irand(w - 30);
        cline(c, tx + 2, top - 4, tx + 2, top - 12, 0x4A3A2A); cline(c, tx + 12, top - 4, tx + 12, top - 12, 0x4A3A2A);
        cline(c, tx + 2, top - 6, tx + 12, top - 11, 0x4A3A2A);
        rect(c, tx, top - 26, 15, 14, 0x8A5A3A);
        for (int yy = top - 24; yy < top - 12; yy += 4) rect(c, tx, yy, 15, 1, 0x5E3A22);
        rect(c, tx, top - 26, 2, 14, 0xA87048);
        float xs[3] = { (float)tx - 1, (float)tx + 7.5f, (float)tx + 16 }, ys[3] = { (float)top - 26, (float)top - 33, (float)top - 26 };
        fill_poly(c, xs, ys, 3, rgb(0x5E4A3A));
    } else if (roof == 1) {                                          /* antenna */
        int ax = x + w / 2 + irand(9) - 4;
        rect(c, ax, top - 30, 1, 26, 0x55555C);
        rect(c, ax - 3, top - 20, 7, 1, 0x55555C);
        rect(c, ax - 2, top - 26, 5, 1, 0x55555C);
        add_spot(s->antenna, &s->nant, ax, top - 31);
    }
    if (rand() % 2) {                                                /* air conditioning */
        int ax = x + w - 18;
        rect(c, ax, top - 9, 11, 5, 0xB8BCC4);
        for (int i = 1; i < 11; i += 2) rect(c, ax + i, top - 8, 1, 3, 0x8A8E96);
    }
    add_perch(s, x + 6 + irand(w - 12), top - 4);
    return w;
}

static int midrise(City *s, Canvas *c, int x, int base) {
    int w = 46 + irand(4) * 12, h = (int)(s->back * (0.28f + 0.2f * frand(0, 1))), top = base - h;
    static const uint32_t cols[5] = { 0xB4574A, 0xA8553E, 0xC9694B, 0x8A6F66, 0xD8B070 };
    uint32_t col = cols[irand(5)];
    rect(c, x, top, w, h, col);
    if (col != 0xD8B070) bricks(c, x, top, x + w - 1, base, col);
    rect(c, x + w - 3, top, 3, h, shade(col, 0.82f));
    cornice(c, x, top, w, col);
    int fe = rand() % 2, fex = x + w - 20;
    int cols_ = (w - (fe ? 22 : 6)) / 12;
    for (int y = top + 8; y + 9 < base - 24; y += 16) {
        for (int i = 0; i < cols_; i++) {
            int wx = x + 6 + i * 12;
            window(c, wx, y, 6, 9, 0xEDE6D8, 1);
            if (rand() % 3 == 0) {                                   /* flower box */
                rect(c, wx - 1, y + 11, 8, 2, 0x7A4A2A);
                for (int k = 0; k < 4; k++) dot(c, wx + k * 2, y + 10, k % 2 ? 0xF25A7A : 0xFFD24A);
            }
        }
        if (fe) {                                                    /* fire escape */
            rect(c, fex, y + 9, 16, 1, 0x2A2A2E);
            rect(c, fex, y + 5, 16, 1, 0x2A2A2E);
            for (int i = 0; i <= 16; i += 4) rect(c, fex + i, y + 5, 1, 4, 0x2A2A2E);
            if (y + 25 < base - 24) cline(c, fex + 3, y + 10, fex + 12, y + 25, 0x2A2A2E);
            window(c, fex + 4, y, 6, 8, 0xEDE6D8, 1);
            if (rand() % 3 == 0) add_perch(s, fex + 8, y + 9);
        }
    }
    rect(c, x + 6, base - 18, 12, 18, 0x5E3A22);                     /* door and stoop */
    rect(c, x + 8, base - 16, 8, 6, 0xA8D4F0);
    dot(c, x + 15, base - 7, 0xFFD24A);
    rect(c, x + 4, base - 2, 16, 2, 0xC8C0B0);
    add_perch(s, x + 6 + irand(w - 12), top - 4);
    return w;
}

static int shop(City *s, Canvas *c, int x, int base) {
    static const char *const NAMES[6] = { "CAFE", "BOOKS", "TOYS", "PIZZA", "FLOWERS", "BAKERY" };
    static const uint32_t walls[5] = { 0xE8D2A0, 0xD0E0C8, 0xE8B8B0, 0xB8D0E8, 0xF0E6D2 };
    static const uint32_t awn[5] = { 0xD94F4F, 0x2E9C9C, 0xE8A82A, 0x6A3AA8, 0x3F7FD6 };
    int k = irand(6), wk = irand(5), ak = irand(5);
    int w = 50 + irand(3) * 8, h = 58 + irand(16), top = base - h;
    rect(c, x, top, w, h, walls[wk]);
    rect(c, x + w - 2, top, 2, h, shade(walls[wk], 0.85f));
    cornice(c, x, top, w, walls[wk]);
    for (int i = 0; i < 2; i++) window(c, x + 8 + i * (w - 24), top + 6, 8, 9, 0xF4F0E8, 1);
    int sy = base - 40;                                              /* sign */
    rect(c, x + 3, sy, w - 6, 11, 0x2E3A4A);
    rect(c, x + 3, sy, w - 6, 1, 0x4E5A6A);
    int tw = text_width(NAMES[k], 1);
    if (tw <= w - 10) stamp_text(c, NAMES[k], x + (w - tw) / 2, sy + 2, 0xFFE8A0);
    int ay = base - 28;                                              /* striped awning */
    for (int i = 0; i < w - 4; i++) rect(c, x + 2 + i, ay, 1, 8, (i / 4) % 2 ? 0xF8F4EC : awn[ak]);
    for (int i = 0; i < w - 4; i += 6) fill_ellipse(c, x + 5.0f + i, (float)ay + 8, 3, 2, rgb((i / 4) % 2 ? 0xF8F4EC : awn[ak]));
    rect(c, x + 2, ay, w - 4, 1, shade(awn[ak], 0.7f));
    rect(c, x + 3, ay + 11, w - 6, 2, shade(walls[wk], 0.7f));       /* its shadow */
    int wy = base - 16, wx1 = x + w - 16;                            /* shop window and door */
    rect(c, x + 3, wy - 1, wx1 - x - 4, 16, 0x3A3A44);
    rect(c, x + 4, wy, wx1 - x - 6, 14, 0xBFE2F2);
    rect(c, x + 4, wy, wx1 - x - 6, 1, 0xE4F4FC);
    int ix = x + 6, iw = wx1 - x - 10;
    switch (k) {
    case 0:                                                          /* cafe tables */
        for (int i = 0; i + 8 <= iw; i += 12) {
            rect(c, ix + i, wy + 8, 8, 1, 0x7A4A2A); rect(c, ix + i + 3, wy + 9, 2, 5, 0x7A4A2A);
            rect(c, ix + i + 1, wy + 6, 2, 2, 0xFFFFFF); rect(c, ix + i + 5, wy + 6, 2, 2, 0xFFFFFF);
        }
        break;
    case 1:                                                          /* bookshelf */
        rect(c, ix, wy + 7, iw, 1, 0x7A4A2A); rect(c, ix, wy + 13, iw, 1, 0x7A4A2A);
        for (int i = 0; i < iw; i += 2) { rect(c, ix + i, wy + 2 + irand(2), 2, 5, SHIRT[irand(12)]); rect(c, ix + i, wy + 9, 2, 4, SHIRT[irand(12)]); }
        break;
    case 2:                                                          /* toys */
        for (int i = 0; i + 6 <= iw; i += 7) fill_ellipse(c, ix + i + 3.0f, wy + 10.0f, 3, 3, rgb(SHIRT[irand(9)]));
        fill_ellipse(c, ix + iw - 5.0f, wy + 6.0f, 3, 3, rgb(0xB07A4A));
        break;
    case 3:                                                          /* pizza */
        fill_ellipse(c, ix + iw / 2.0f, wy + 8.0f, 6, 4, rgb(0xE8A84A));
        fill_ellipse(c, ix + iw / 2.0f, wy + 8.0f, 5, 3, rgb(0xF2D06A));
        for (int i = 0; i < 5; i++) dot(c, ix + iw / 2 - 4 + i * 2, wy + 7 + i % 2 * 2, 0xC8322A);
        break;
    case 4:                                                          /* flowers, inside and out */
        for (int i = 0; i + 5 <= iw; i += 6) {
            rect(c, ix + i, wy + 10, 4, 4, 0xB86A3A); dot(c, ix + i + 1, wy + 8, 0xF25A7A); dot(c, ix + i + 2, wy + 7, 0xFFD24A);
            rect(c, ix + i + 1, wy + 9, 2, 1, 0x3E9A48);
        }
        for (int i = 0; i < 3; i++) {
            rect(c, x + 4 + i * 6, base + 3, 4, 4, 0xB86A3A);
            dot(c, x + 5 + i * 6, base + 1, 0xF25A7A); dot(c, x + 6 + i * 6, base + 2, 0x3E9A48);
        }
        break;
    default:                                                         /* bakery */
        for (int i = 0; i + 7 <= iw; i += 8) fill_ellipse(c, ix + i + 3.5f, wy + 10.0f, 3.5f, 2, rgb(0xC8863A));
        rect(c, ix + 1, wy + 3, 6, 4, 0xF8E8F0); rect(c, ix + 1, wy + 2, 6, 1, 0xF25A7A);
        break;
    }
    rect(c, wx1, wy - 1, 12, 17, 0x3A3A44);                          /* door */
    rect(c, wx1 + 1, wy, 10, 16, 0xBFE2F2);
    rect(c, wx1 + 3, wy + 3, 6, 3, 0xD94F4F);                         /* OPEN */
    dot(c, wx1 + 9, wy + 9, 0xE8E8E8);
    add_perch(s, x + 6 + irand(w - 12), ay);
    return w;
}

static int house(City *s, Canvas *c, int x, int base) {
    static const uint32_t walls[5] = { 0xF2E2B8, 0xE8B8B8, 0xB8D8E8, 0xD8E8B8, 0xF4F0E8 };
    static const uint32_t roofs[4] = { 0x9E3A2E, 0x5A4A6A, 0x3A5A7A, 0x6A4A2E };
    uint32_t col = walls[irand(5)], roof = roofs[irand(4)];
    int w = 52 + irand(3) * 6, h = 32 + irand(8), top = base - h;
    rect(c, x + 2, top, w - 4, h, col);
    for (int y = top + 2; y < base; y += 3) rect(c, x + 2, y, w - 4, 1, shade(col, 0.93f));
    rect(c, x + w - 5, top, 3, h, shade(col, 0.85f));
    int rh = 22 + irand(4);
    int chx = x + w - 16;                                            /* chimney */
    rect(c, chx, top - rh + 2, 7, rh, 0x9A4A3A);
    bricks(c, chx, top - rh + 2, chx + 6, top, 0x9A4A3A);
    rect(c, chx - 1, top - rh + 1, 9, 2, 0x5E4A42);
    add_spot(s->chimney, &s->nchim, chx + 3, top - rh);
    float xs[3] = { (float)x - 4, (float)x + w / 2.0f, (float)x + w + 4 }, ys[3] = { (float)top + 1, (float)top - rh, (float)top + 1 };
    fill_poly(c, xs, ys, 3, rgb(roof));
    for (int y = top - rh + 2; y <= top; y++)                        /* shingles */
        for (int xx = x - 4; xx <= x + w + 4; xx++) {
            if (get(c, xx, y) != rgb(roof)) continue;
            int row = (top - y) / 3;
            if ((top - y) % 3 == 0) dot(c, xx, y, shade(roof, 0.75f));
            else if ((xx + row * 3) % 6 == 0) dot(c, xx, y, shade(roof, 0.82f));
        }
    cline(c, x - 4, top + 1, x + w / 2, top - rh, shade(roof, 1.3f));
    cline(c, x + w / 2, top - rh, x + w + 4, top + 1, shade(roof, 0.6f));
    rect(c, x - 4, top + 1, w + 9, 2, 0xF4F0E8);
    window(c, x + 7, top + 9, 11, 10, 0xF8F4EC, 1);                  /* front window and flower box */
    rect(c, x + 6, top + 21, 13, 3, 0x7A4A2A);
    for (int i = 0; i < 6; i++) dot(c, x + 7 + i * 2, top + 20, i % 2 ? 0xF25A7A : 0xFFD24A);
    ring(c, x + w / 2.0f, top - rh / 2.0f + 1, 3, 0xF8F4EC);            /* attic window */
    fill_ellipse(c, x + w / 2.0f, top - rh / 2.0f + 1, 2, 2, rgb(0x93BEE0));
    int dx = x + w - 22;                                             /* door, porch and steps */
    rect(c, dx - 3, top + 4, 18, 2, shade(roof, 0.9f));
    rect(c, dx, top + 8, 12, h - 8, 0x7A4424);
    rect(c, dx + 1, top + 9, 10, h - 10, 0x8A5430);
    rect(c, dx + 3, top + 11, 6, 5, 0xBFE2F2);
    dot(c, dx + 9, base - 10, 0xFFD24A);
    rect(c, dx - 2, base - 2, 16, 2, 0xC8C0B0);
    fill_ellipse(c, x + 8.0f, base - 4.0f, 7, 4, rgb(0x3E8A42));      /* bush */
    fill_ellipse(c, x + 6.0f, base - 6.0f, 4, 2, rgb(0x52A856));
    for (int i = x - 3; i < x + w + 3; i += 4) {                     /* picket fence */
        rect(c, i, base - 9, 2, 9, 0xFAFAF0);
        dot(c, i, base - 10, 0xFAFAF0);
        rect(c, i + 1, base - 9, 1, 9, 0xD8D8CC);
    }
    rect(c, x - 3, base - 7, w + 6, 1, 0xFAFAF0);
    rect(c, x - 3, base - 3, w + 6, 1, 0xFAFAF0);
    int mx = x + w - 6;                                              /* mailbox */
    rect(c, mx, base - 9, 1, 9, 0x5E3A22);
    rect(c, mx - 3, base - 13, 7, 4, 0x3F6FB6);
    rect(c, mx + 3, base - 16, 1, 4, 0xD23A3A);
    add_perch(s, x + 4 + irand(w - 16), base - 10);
    add_perch(s, x + w / 2 + irand(7) - 3, top - rh + 1);
    return w + 6;
}

static int park(City *s, Canvas *c, int x, int base) {
    (void)s;
    int w = 44 + irand(16);
    rect(c, x, base - 5, w, 6, 0x5DB05A);
    for (int i = 0; i < w; i++)
        if (rand() % 2) dot(c, x + i, base - 6 - rand() % 2, rand() % 2 ? 0x4A9A48 : 0x72C46A);
    tree(c, x + w / 2 + 6, base - 4, 22 + irand(8), 1);
    rect(c, x + 3, base - 11, 14, 2, 0x8A5A32);                      /* bench */
    rect(c, x + 3, base - 16, 14, 2, 0x7A4A2A);
    rect(c, x + 4, base - 9, 1, 4, 0x3A3A3A); rect(c, x + 15, base - 9, 1, 4, 0x3A3A3A);
    for (int i = 0; i < 6; i++) dot(c, x + 2 + irand(w - 4), base - 5, i % 2 ? 0xF25A7A : 0xFFD24A);
    return w;
}

static void build_far(City *s, Canvas *c) {
    int x = -4;
    while (x < s->W) {
        int w = 18 + irand(30), h = (int)(s->back * (0.4f + 0.38f * frand(0, 1)));
        uint32_t col = rand() % 2 ? 0x9DB4D4 : 0xA9BEDA;
        int top = s->back - h;
        rect(c, x, top, w, h + 1, col);
        rect(c, x, top, w, 1, shade(col, 1.08f));
        if (rand() % 3 == 0) rect(c, x + 4, top - 8, w - 8, 8, col);
        if (rand() % 3 == 0) rect(c, x + w / 2, top - 18, 1, 18, col);
        for (int y = top + 5; y < s->back - 6; y += 6)
            for (int xx = x + 3; xx < x + w - 3; xx += 5)
                if (rand() % 3) rect(c, xx, y, 2, 2, shade(col, 1.1f));
        x += w - 2 + irand(4);
    }
    for (int y = s->back - 40; y <= s->back; y++)                    /* haze near the ground */
        for (int x2 = 0; x2 < s->W; x2++) {
            uint32_t p = get(c, x2, y);
            if (p) dot(c, x2, y, mix(p >> 8, 0xD4E4F2, (y - (s->back - 40)) / 60.0f));
        }
}

static void build_near(City *s, Canvas *c) {
    int x = -6, prev = -1;
    while (x < s->W) {
        int kind = irand(10);
        if (kind == prev && kind < 5) kind = 9 - kind;
        prev = kind;
        if (kind < 3) x += tower(s, c, x, s->back);
        else if (kind < 5) x += midrise(s, c, x, s->back);
        else if (kind < 7) x += shop(s, c, x, s->back);
        else if (kind < 9) x += house(s, c, x, s->back);
        else x += park(s, c, x, s->back);
    }
    outline(c, rgb(0x2A2230));
}

static void bike_sign(Canvas *c, int x, int y) {
    ring(c, x - 5.0f, (float)y, 3, 0xF4F0E8);
    ring(c, x + 5.0f, (float)y, 3, 0xF4F0E8);
    cline(c, x - 5, y, x, y, 0xF4F0E8);
    cline(c, x, y, x - 2, y - 4, 0xF4F0E8);
    cline(c, x - 2, y - 4, x + 3, y - 4, 0xF4F0E8);
    cline(c, x, y, x + 3, y - 4, 0xF4F0E8);
    cline(c, x + 3, y - 4, x + 5, y, 0xF4F0E8);
}

static void build_ground(City *s, Canvas *c) {
    int W = s->W, H = s->H;
    rect(c, 0, s->back + 1, W, s->curb - s->back, 0xCEC7B8);          /* sidewalk */
    rect(c, 0, s->back + 1, W, 2, 0xB2AB9C);
    for (int y = s->back + 14; y < s->curb; y += 12) {
        rect(c, 0, y, W, 1, 0xB8B0A2);
        rect(c, 0, y + 1, W, 1, 0xDAD4C6);
    }
    for (int y = s->back + 3, row = 0; y < s->curb; y += 12, row++)
        for (int x = (row % 2) * 16; x < W; x += 32) rect(c, x, y, 1, 11, 0xB8B0A2);
    for (int i = 0; i < W / 70; i++) {                               /* cracks and gum */
        int cx = irand(W), cy = s->back + 6 + irand(s->curb - s->back - 10);
        for (int k = 0; k < 6; k++) dot(c, cx + k, cy + (k % 3 == 1) - (k > 3), 0xA8A092);
        dot(c, irand(W), s->back + 6 + irand(s->curb - s->back - 10), 0x9A948A);
    }
    rect(c, 0, s->curb + 1, W, 2, 0xEAE4D8);                          /* curb and gutter */
    rect(c, 0, s->curb + 3, W, 3, 0xA8A296);
    rect(c, 0, s->curb + 6, W, 1, 0x3A3D45);
    rect(c, 0, s->curb + 7, W, H - s->curb - 7, 0x4A4E58);           /* road */
    for (int i = 0; i < W * (H - s->curb) / 30; i++)
        dot(c, irand(W), s->curb + 7 + irand(H - s->curb - 7), rand() % 2 ? 0x555A64 : 0x42464F);
    int bl = s->laneBike + 6;
    for (int x = 0; x < W; x += 16) rect(c, x, bl, 9, 1, 0xF4F0E8);
    for (int x = 90; x < W; x += 220) bike_sign(c, x, s->laneBike - 3);
    int ml = s->laneL + 4;
    rect(c, 0, ml, W, 1, 0xF2C230);
    rect(c, 0, ml + 2, W, 1, 0xF2C230);
    for (int y = s->curb + 9; y < H - 1; y += 7)                       /* crosswalk */
        rect(c, s->crossX, y, s->crossW, 4, 0xEEEAE0);
    rect(c, s->crossX - 10, ml + 4, 2, H - ml - 4, 0xEEEAE0);         /* stop lines */
    rect(c, s->crossX + s->crossW + 8, s->curb + 9, 2, ml - s->curb - 10, 0xEEEAE0);
    int mx = (s->crossX + W / 2) % W;                                 /* manhole */
    fill_ellipse(c, (float)mx, s->laneL + 12.0f, 8, 2.5f, rgb(0x3A3D45));
    fill_ellipse(c, (float)mx, s->laneL + 12.0f, 6, 1.6f, rgb(0x5A5E68));
    for (int x = 130; x < W; x += 260) {                             /* storm drains */
        rect(c, x, s->curb + 3, 12, 3, 0x2A2C32);
        for (int i = 1; i < 12; i += 2) rect(c, x + i, s->curb + 3, 1, 3, 0x6A6E76);
    }
    for (int x = 50; x < W; x += 180) {                              /* lamps */
        int y0 = s->back + 4;
        rect(c, x - 2, y0 - 4, 5, 5, 0x2E3A3A);
        rect(c, x, y0 - 46, 2, 44, 0x2E3A3A);
        cline(c, x + 1, y0 - 46, x + 6, y0 - 50, 0x2E3A3A);
        rect(c, x + 5, y0 - 50, 6, 2, 0x2E3A3A);
        rect(c, x + 6, y0 - 48, 4, 4, 0xFFF2B0);
        rect(c, x + 2, y0 - 38, 6, 10, SHIRT[irand(6)]);
        rect(c, x + 2, y0 - 38, 6, 1, 0xFFFFFF);
    }
    for (int x = 20; x < W; x += 150) {                              /* street trees */
        if (abs(x - s->crossX) < 30 || abs(x - s->stopX) < 30) continue;
        rect(c, x - 6, s->back + 7, 12, 3, 0x6A5A48);
        for (int i = -5; i < 6; i += 2) dot(c, x + i, s->back + 8, 0x3A3028);
        tree(c, x, s->back + 8, 26, 0);
    }
    outline(c, rgb(0x2A2230));
}

/* Cuts what was drawn on a full-size canvas down to its own small sprite. */
static void add_prop(City *s, SDL_Renderer *ren, Canvas *full, int base, int light) {
    outline(full, rgb(0x2A2230));
    int x0 = full->w, y0 = full->h, x1 = -1, y1 = -1;
    for (int y = 0; y < full->h; y++)
        for (int x = 0; x < full->w; x++)
            if (full->px[y * full->w + x]) {
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < y0) y0 = y;
                if (y > y1) y1 = y;
            }
    if (x1 < 0 || s->nprop >= MAXPROP) { free(full->px); return; }
    Canvas c = canvas_new(x1 - x0 + 1, y1 - y0 + 1);
    for (int y = y0; y <= y1; y++) memcpy(&c.px[(y - y0) * c.w], &full->px[y * full->w + x0], (size_t)c.w * 4);
    free(full->px);
    s->prop[s->nprop++] = (Prop){ canvas_texture(ren, &c), x0, y0, x1 - x0 + 1, y1 - y0 + 1, base, light };
}

static void build_props(City *s, SDL_Renderer *ren) {
    int bx = s->stopX;
    Canvas c = canvas_new(s->W, s->H);                               /* bus stop sign */
    rect(&c, bx, s->back + 2, 2, 40, 0x3A3D45);
    rect(&c, bx - 9, s->back + 4, 21, 11, 0x2F6FD0);
    stamp_text(&c, "BUS", bx - 7, s->back + 6, 0xFFFFFF);
    add_prop(s, ren, &c, s->back + 42, 0);
    c = canvas_new(s->W, s->H);                                      /* its bench */
    rect(&c, bx + 8, s->back + 22, 20, 2, 0x8A5A32);
    rect(&c, bx + 8, s->back + 16, 20, 2, 0x7A4A2A);
    rect(&c, bx + 9, s->back + 24, 1, 5, 0x3A3A3A); rect(&c, bx + 26, s->back + 24, 1, 5, 0x3A3A3A);
    add_prop(s, ren, &c, s->back + 29, 0);
    int lx = s->lightX;
    c = canvas_new(s->W, s->H);                                      /* traffic light */
    rect(&c, lx, s->curb - 52, 2, 52, 0x2E3A3A);
    rect(&c, lx - 4, s->curb - 2, 10, 3, 0x2E3A3A);
    rect(&c, lx - 3, s->lightY - 1, 8, 20, 0x22262C);
    for (int i = 0; i < 3; i++) fill_ellipse(&c, lx + 1.0f, s->lightY + 3.0f + i * 6, 2, 2, rgb(0x3A3E46));
    add_prop(s, ren, &c, s->curb, 1);
    int hx = s->crossX - 40;
    c = canvas_new(s->W, s->H);                                      /* hydrant */
    rect(&c, hx, s->curb - 10, 6, 10, 0xD23A3A);
    rect(&c, hx - 1, s->curb - 12, 8, 2, 0xA82A2A);
    rect(&c, hx + 1, s->curb - 14, 4, 2, 0xD23A3A);
    rect(&c, hx - 2, s->curb - 7, 10, 2, 0xE85A5A);
    rect(&c, hx + 1, s->curb - 9, 1, 8, 0xF07A7A);
    rect(&c, hx - 1, s->curb - 1, 8, 1, 0x8A2222);
    add_prop(s, ren, &c, s->curb, 0);
}

static Canvas make_cloud(int w) {
    int h = w * 2 / 5;
    Canvas c = canvas_new(w, h);
    int n = 4 + w / 14;
    for (int i = 0; i < n; i++) {
        float cx = frand(w * 0.2f, w * 0.8f), r = frand(h * 0.3f, h * 0.5f);
        fill_ellipse(&c, cx, h - r * 0.9f, r * 1.2f, r, rgb(0xFFFFFF));
    }
    fill_ellipse(&c, w * 0.5f, h * 0.78f, w * 0.48f, h * 0.22f, rgb(0xFFFFFF));
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            if (!get(&c, x, y)) continue;
            if (y > h * 0.7f) dot(&c, x, y, 0xDCE8F4);
            else if (!get(&c, x, y - 2)) dot(&c, x, y, 0xFFFFFF);
            else if (y > h * 0.55f) dot(&c, x, y, 0xEEF4FA);
        }
    return c;
}

/* ---------- actors ---------- */

static int car_len(const Actor *a) {
    return a->role == R_BUS ? 86 : a->kind == 1 ? 38 : a->kind == 3 ? 46 : 44;
}

static float ground_y(const City *s, int lane) {
    return (float)(s->back + 12 + lane * (s->curb - s->back - 18) / 4);
}

static void look(Actor *a) {
    a->skin = SKIN[irand(6)];
    a->shirt = SHIRT[irand(12)];
    a->shirt2 = SHIRT[irand(12)];
    a->pants = PANTS[irand(7)];
    a->hair = HAIRC[irand(7)];
    a->cap = SHIRT[irand(12)];
    a->shoe = SHOES[irand(5)];
    a->hairStyle = irand(6);
    a->outfit = irand(4);
    a->stripes = rand() % 4 == 0;
    a->helmet = rand() % 2;
    a->breed = irand(6);
    a->basket = rand() % 3 == 0 ? 1 + irand(2) : 0;
    a->kind = irand(4);
    a->car = CARC[irand(8)];
    a->glint = frand(1, 5);
}

static void place_car(City *s, Actor *a, int initial) {
    float L = (float)car_len(a);
    a->x = initial ? frand(0, (float)s->W) : a->dir > 0 ? -L : s->W + L;
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < s->na; i++) {
            const Actor *o = &s->a[i];
            if (o == a || (o->role != R_CAR && o->role != R_BUS) || o->dir != a->dir) continue;
            float need = (car_len(o) + L) / 2 + 16;
            float ahead = (o->x - a->x) * a->dir;
            if (initial ? fabsf(ahead) < need : ahead < need) a->x = o->x - a->dir * need;
        }
}

static void spawn(City *s, Actor *a, int initial) {
    int role = a->role;
    float W = (float)s->W;
    memset(a, 0, sizeof *a);
    a->role = role;
    look(a);
    a->dir = rand() % 2 ? 1 : -1;
    a->ph = frand(0, 6.28f);
    a->state = S_WALK;
    a->glasses = role == R_SHADES || role == R_BIGBUTT || (role == R_BIKE && rand() % 3 == 0);
    a->kid = role == R_WALKER && rand() % 4 == 0;
    a->prop = role == R_WALKER || role == R_SHADES ? irand(6) : 0;          /* 1 coffee, 2 bag, 3 backpack */
    if (a->kid) a->prop = rand() % 2 ? 4 : 0;                                /* 4 balloon */
    if (role == R_SHADES && rand() % 2) { a->outfit = 1; a->shirt = 0x26262C; a->shirt2 = 0xF4F4F4; }
    a->x = initial ? frand(-10, W + 10) : (a->dir > 0 ? -40.0f : W + 40);
    switch (role) {
    case R_WALKER: case R_SHADES: case R_DOGWALK: case R_BIGBUTT:
        a->y = ground_y(s, irand(5));
        a->vx = a->kid ? frand(22, 28) : frand(18, 30);
        if (role == R_BIGBUTT) a->vx = 16;
        break;
    case R_STANDER:
        a->y = ground_y(s, irand(5));
        a->vx = frand(18, 26);
        a->tx = frand(W * 0.08f, W * 0.92f);
        a->pose = irand(4);
        if (initial) { a->x = a->tx; a->state = S_STAND; a->timer = frand(3, 20); }
        else a->dir = a->x < a->tx ? 1 : -1;
        break;
    case R_BIKE:
        a->y = (float)s->laneBike;
        a->vx = frand(52, 80);
        break;
    case R_CAR: case R_BUS:
        a->dir = rand() % 2 ? 1 : -1;
        a->y = (float)(a->dir > 0 ? s->laneR : s->laneL);
        a->v0 = role == R_BUS ? frand(34, 42) : frand(46, 70);
        a->vx = a->v0;
        if (role == R_CAR && a->kind == 3 && rand() % 3 == 0) a->pose = 1;  /* ice cream van */
        if (role == R_BUS) a->car = rand() % 2 ? 0xD94F4F : 0x2E9C9C;
        place_car(s, a, initial);
        break;
    case R_CATWALK:
        a->y = ground_y(s, irand(5));
        a->vx = frand(14, 24);
        a->pet = CATC[irand(5)];
        a->timer = frand(4, 12);
        break;
    case R_CATSIT:
        if (s->nperch) {
            a->perch = irand(s->nperch);
            a->x = (float)s->perch[a->perch].x;
            a->y = (float)s->perch[a->perch].y;
            a->dir = a->x < W / 2 ? 1 : -1;
        }
        a->pet = CATC[irand(5)];
        a->timer = frand(2, 6);
        break;
    case R_PIGEON:
        a->y = ground_y(s, irand(5));
        a->x = frand(0, W);
        a->timer = frand(1, 4);
        break;
    }
}

static void add(City *s, int role, int n) {
    for (int i = 0; i < n && s->na < MAXA; i++) {
        s->a[s->na].role = role;
        spawn(s, &s->a[s->na], 1);
        s->na++;
    }
}

static void *create(SDL_Renderer *ren, int w, int h) {
    City *s = calloc(1, sizeof *s);
    s->w = w;
    s->h = h;
    if (h >= ROWS) {
        s->px = h / ROWS;
        s->scale = 1;
        s->W = (w + s->px - 1) / s->px;
        s->H = (h + s->px - 1) / s->px;
    } else {                                       /* small: draw a full-size scene shrunk */
        s->px = 1;
        s->scale = (float)h / ROWS;
        s->H = ROWS;
        s->W = (int)ceilf(w / s->scale);
    }
    s->back = (int)(s->H * 0.62f);
    s->curb = (int)(s->H * 0.79f);
    s->laneBike = s->curb + 20;
    s->laneL = s->curb + 46;
    s->laneR = s->H - 6;
    s->crossW = 30;
    s->crossX = (int)(s->W * 0.62f);
    s->lightX = s->crossX - 18;
    s->lightY = s->curb - 70;
    s->stopX = (int)(s->W * 0.24f);

    Canvas c = canvas_new(s->W, s->H);
    for (int y = 0; y < s->H; y++) {
        float k = (float)y / s->back;
        rect(&c, 0, y, s->W, 1, mix(0x5FA8E8, 0xCFEAFA, k > 1 ? 1 : k));
    }
    float sx = s->W * 0.84f, sy = s->H * 0.14f;
    fill_ellipse(&c, sx, sy, 24, 24, rgb(0xE8F2D8));
    fill_ellipse(&c, sx, sy, 19, 19, rgb(0xFFF6C8));
    fill_ellipse(&c, sx, sy, 15, 15, rgb(0xFFE680));
    fill_ellipse(&c, sx - 4, sy - 4, 6, 5, rgb(0xFFF4B8));
    s->sky = canvas_texture(ren, &c);
    c = canvas_new(s->W, s->H);
    build_far(s, &c);
    s->far = canvas_texture(ren, &c);
    c = canvas_new(s->W, s->H);
    build_near(s, &c);
    build_ground(s, &c);
    s->near = canvas_texture(ren, &c);
    build_props(s, ren);
    for (int i = 0; i < 3; i++) {
        c = make_cloud(50 + i * 22);
        s->cloudW[i] = c.w;
        s->cloudH[i] = c.h;
        s->cloudTex[i] = canvas_texture(ren, &c);
        if (s->cloudTex[i]) SDL_SetTextureAlphaMod(s->cloudTex[i], 235);
    }
    for (int i = 0; i < MAXCLOUD; i++)
        s->cloud[i] = (Cloud){ frand(-60, (float)s->W), frand(6, s->H * 0.3f), frand(3, 9), irand(3) };

    int k = s->W / 640 < 1 ? 1 : s->W / 640 > 3 ? 3 : s->W / 640;
    int flocks = 4 + 2 * k;
    for (int f = 0; f < flocks && s->nb < MAXB - 12; f++) {
        float fx = frand(-40, (float)s->W), fy = frand(16, s->H * 0.42f), v = frand(20, 44) * (rand() % 2 ? 1 : -1);
        int n = 3 + irand(8), gull = f == 0;
        float sz = frand(0.8f, 1.6f);
        for (int i = 0; i < n && s->nb < MAXB; i++)
            s->bird[s->nb++] = (Bird){ fx - i * 8 * (v > 0 ? 1 : -1), fy + (i % 2 ? 1 : -1) * (i / 2) * 7 + frand(-3, 3),
                                       v * frand(0.95f, 1.05f), frand(0, 6.28f), sz, gull };
    }
    add(s, R_CATSIT, 4 * k);
    add(s, R_WALKER, 4 * k);
    add(s, R_SHADES, 3 * k);
    add(s, R_STANDER, 3 * k);
    add(s, R_DOGWALK, 3 * k);
    add(s, R_BIGBUTT, 1);
    add(s, R_BIKE, 3 * k);
    add(s, R_CAR, 3 * k);
    add(s, R_BUS, 1);
    add(s, R_CATWALK, 2);
    add(s, R_PIGEON, 5 * k);
    return s;
}

static void destroy(void *p) {
    City *s = p;
    SDL_DestroyTexture(s->sky);
    SDL_DestroyTexture(s->far);
    SDL_DestroyTexture(s->near);
    for (int i = 0; i < 3; i++) SDL_DestroyTexture(s->cloudTex[i]);
    if (s->small) SDL_DestroyTexture(s->small);
    for (int i = 0; i < s->nprop; i++) SDL_DestroyTexture(s->prop[i].t);
    free(s);
}

/* 0 green, 1 yellow, 2 red, for the cars. */
static int light(const City *s) {
    float t = fmodf(s->t, 17);
    return t < 9 ? 0 : t < 11 ? 1 : 2;
}

static int off_screen(const City *s, const Actor *a) {
    float m = a->role == R_BUS ? 100.0f : 60.0f;
    return (a->dir > 0 && a->x > s->W + m) || (a->dir < 0 && a->x < -m);
}

static int person_near(const City *s, float x, float y, float d) {
    for (int i = 0; i < s->na; i++) {
        const Actor *o = &s->a[i];
        if (o->role > R_BIKE) continue;
        if (fabsf(o->x - x) < d && fabsf(o->y - y) < d) return 1;
    }
    return 0;
}

static void step_car(City *s, Actor *a, float dt) {
    float L = (float)car_len(a), v = a->v0, front = a->x + a->dir * L / 2;
    float stopX = a->dir > 0 ? s->crossX - 12 : s->crossX + s->crossW + 12;
    float dist = (stopX - front) * a->dir;
    int lt = light(s);
    if (lt && dist > -2 && dist < 90 && !(lt == 1 && dist < 16)) v = fminf(v, dist < 2 ? 0 : dist * 1.2f);
    if (a->role == R_BUS && a->dir < 0) {                     /* the bus pulls in at its stop */
        float sd = a->x - L / 2 - s->stopX;
        if (a->state == S_WALK && sd > -2 && sd < 90) v = fminf(v, sd < 2 ? 0 : sd * 1.0f + 3);
        if (a->state == S_WALK && sd < 2 && sd > -6 && a->vx < 1) { a->state = S_STAND; a->timer = 4; }
        if (a->state == S_STAND) { v = 0; a->timer -= dt; if (a->timer <= 0) a->state = S_AWAY; }
    }
    for (int i = 0; i < s->na; i++) {
        const Actor *o = &s->a[i];
        if (o == a || (o->role != R_CAR && o->role != R_BUS) || o->dir != a->dir) continue;
        float gap = (o->x - a->x) * a->dir - (car_len(o) + L) / 2;
        if (gap > -6 && gap < 70) v = fminf(v, fmaxf(0, (gap - 10) * 1.5f));
    }
    if (a->vx < v) a->vx = fminf(v, a->vx + 40 * dt);
    else a->vx = fmaxf(v, a->vx - 140 * dt);
    a->x += a->dir * a->vx * dt;
    a->ph += a->vx * dt / 4.5f;
    if (off_screen(s, a)) spawn(s, a, 0);
}

static void step_actor(City *s, Actor *a, float dt) {
    a->t += dt;
    a->glint -= dt;
    if (a->glint < -0.3f) a->glint = frand(2, 6);
    switch (a->role) {
    case R_WALKER: case R_SHADES: case R_DOGWALK: case R_BIGBUTT:
        if (a->state == S_WALK) {
            a->x += a->dir * a->vx * dt;
            a->ph += a->vx * dt * 0.28f;
            if ((a->role == R_DOGWALK || a->role == R_BIGBUTT) && rand() % 900 == 0) { a->state = S_SNIFF; a->timer = frand(2, 4); }
            else if (a->role == R_WALKER && rand() % 2500 == 0) { a->state = S_STAND; a->timer = frand(2, 5); }
        } else {
            a->timer -= dt;
            if (a->timer <= 0) a->state = S_WALK;
        }
        if (off_screen(s, a)) spawn(s, a, 0);
        break;
    case R_STANDER:
        if (a->state == S_WALK) {
            a->x += a->dir * a->vx * dt;
            a->ph += a->vx * dt * 0.28f;
            if (a->timer == 0 && (a->x - a->tx) * a->dir >= 0) { a->state = S_STAND; a->timer = frand(8, 25); a->x = a->tx; }
            if (a->timer < 0 && off_screen(s, a)) spawn(s, a, 0);
        } else {
            a->timer -= dt;
            if (a->timer <= 0) { a->state = S_WALK; a->timer = -1; a->dir = rand() % 2 ? 1 : -1; }
        }
        break;
    case R_BIKE:
        a->x += a->dir * a->vx * dt;
        a->ph += a->vx * dt / 7;
        if (off_screen(s, a)) spawn(s, a, 0);
        break;
    case R_CAR: case R_BUS:
        step_car(s, a, dt);
        break;
    case R_CATWALK:
        if (a->state == S_WALK) {
            a->x += a->dir * a->vx * dt;
            a->ph += a->vx * dt * 0.45f;
            a->timer -= dt;
            if (a->timer <= 0) { a->state = S_STAND; a->timer = frand(3, 8); }
        } else {
            a->timer -= dt;
            if (a->timer <= 0) { a->state = S_WALK; a->timer = frand(4, 12); if (rand() % 3 == 0) a->dir = -a->dir; }
        }
        if (off_screen(s, a)) spawn(s, a, 0);
        break;
    case R_CATSIT:
        a->timer -= dt;
        if (a->timer <= 0) { a->pose = irand(4); a->timer = frand(2, 7); }
        break;
    case R_PIGEON:
        if (a->state == S_WALK || a->state == S_STAND) {
            a->timer -= dt;
            if (a->state == S_WALK) { a->x += a->dir * a->vx * dt; a->ph += dt * 9; }
            else a->ph += dt * 12;
            if (a->timer <= 0) {
                if (a->state == S_WALK) { a->state = S_STAND; a->timer = frand(0.8f, 3); a->vx = 0; }
                else { a->state = S_WALK; a->dir = rand() % 2 ? 1 : -1; a->vx = frand(10, 18); a->timer = frand(0.5f, 1.5f); }
            }
            if (person_near(s, a->x, a->y, 22)) { a->state = S_FLEE; a->dir = a->x < s->W / 2 ? -1 : 1; a->vx = 76; a->timer = 0; }
        } else if (a->state == S_FLEE) {
            a->timer += dt;
            a->x += a->dir * a->vx * dt;
            a->ph += dt * 30;
            if (a->timer > 2.4f) { a->state = S_AWAY; a->timer = frand(4, 14); }
        } else {
            a->timer -= dt;
            if (a->timer <= 0) spawn(s, a, 1);
        }
        break;
    }
}

static void step(void *p, float dt) {
    City *s = p;
    if (dt > 0.1f) dt = 0.1f;
    s->t += dt;
    for (int i = 0; i < s->na; i++) step_actor(s, &s->a[i], dt);
    for (int i = 0; i < MAXCLOUD; i++) {
        Cloud *c = &s->cloud[i];
        c->x += c->v * dt;
        if (c->x > s->W + 10) { c->x = -(float)s->cloudW[c->k] - 10; c->y = frand(6, s->H * 0.3f); }
    }
    for (int i = 0; i < s->nb; i++) {
        Bird *b = &s->bird[i];
        b->x += b->vx * dt;
        b->ph += dt * (6 + b->size * 2);
        if (b->vx > 0 && b->x > s->W + 30) b->x = -30 - frand(0, 40);
        if (b->vx < 0 && b->x < -30) b->x = s->W + 30 + frand(0, 40);
    }
}

/* ---------- drawing the actors ---------- */

static void draw_head(const City *s, SDL_Renderer *r, const Actor *a, int X, int top, int D, int down, int helmet) {
    uint32_t skin = a->skin, skinD = shade(skin, 0.82f), hair = a->hair;
    int eye = top + 3 + down, st = helmet ? -1 : a->hairStyle;
    if (st == 1) F(-5, top + 1, 3, 11, hair);                        /* long hair hangs behind */
    if (st == 5) FE(-1, top + 3, 6, 5, hair);                        /* curls */
    F(-3, top, 6, 8, skin);
    F(-4, top + 1, 8, 6, skin);
    F(-1, top + 3, 2, 2, skinD);                                     /* ear */
    F(4, eye + 1, 1, 2, skin);                                       /* nose */
    F(4, eye + 2, 1, 1, skinD);
    F(1, eye + 4, 2, 1, 0xA0524A);                                   /* smile */
    F(0, eye + 3, 1, 1, mix(skin, 0xFF8080, 0.35f));
    switch (st) {
    case -1:                                                         /* bike helmet */
        F(-4, top - 2, 8, 4, a->cap); F(-3, top - 3, 6, 1, a->cap);
        F(-2, top - 2, 1, 2, shade(a->cap, 0.6f)); F(1, top - 2, 1, 2, shade(a->cap, 0.6f));
        F(-4, top + 2, 1, 3, 0x2A2A2A);
        break;
    case 0: F(-4, top - 1, 8, 3, hair); F(-4, top + 2, 2, 3, hair); F(3, top + 1, 1, 1, hair); break;
    case 1: F(-4, top - 1, 8, 3, hair); F(-4, top + 2, 3, 4, hair); break;
    case 2: F(-4, top - 1, 8, 3, hair); FE(-4, top - 1, 2, 2, hair); F(-4, top + 2, 2, 2, hair); break;
    case 3: F(-4, top - 1, 8, 3, a->cap); F(3, top + 1, 3, 1, shade(a->cap, 0.65f)); F(-4, top + 2, 2, 2, hair); break;
    case 4: F(-4, top - 1, 8, 2, hair); break;
    default: F(-4, top - 1, 8, 2, hair); break;
    }
    if (a->glasses) {
        F(0, eye, 4, 2, 0x101018);
        F(-3, eye, 3, 1, 0x101018);
        F(1, eye, 1, 1, 0x7088B8);
        if (a->glint < 0) {                                          /* ting! */
            int cx = D > 0 ? X + 2 : X - 2;
            box(s, r, cx, eye - 3, 1, 7, 0xFFFFFF, 235);
            box(s, r, cx - 3, eye, 7, 1, 0xFFFFFF, 235);
            box(s, r, cx - 1, eye - 1, 3, 3, 0xFFFFFF, 235);
        }
    } else {
        F(2, eye, 1, 2, 0x1A1A22);
        if (st != 4 && st != -1) F(1, eye - 2, 2, 1, shade(hair, 0.85f));
    }
}

static void draw_person(const City *s, SDL_Renderer *r, const Actor *a, int moving, int down) {
    int X = (int)floorf(a->x), Y = (int)floorf(a->y), D = a->dir;
    int legH = a->kid ? 8 : 12, torH = a->kid ? 8 : 11;
    float sw = moving ? sinf(a->ph) : 0, cw = moving ? cosf(a->ph) : 0;
    int hipY = Y - legH, shY = hipY - torH, top = shY - 9;
    uint32_t sh = a->shirt, shD = shade(sh, 0.78f), pa = a->pants, paD = shade(pa, 0.75f);
    uint32_t skin = a->skin, skinD = shade(skin, 0.8f);
    int dress = a->outfit == 2, standing = a->role == R_STANDER && a->state == S_STAND;
    ell(s, r, X, Y, 7, 1.2f, 0x000000, 45);
    /* far arm, far leg (behind the body) */
    float fhx = X + D * sw * 3, fhy = shY + 10;
    if (a->prop == 2) {                                              /* paper bag in the far hand */
        R((int)fhx - 3, (int)fhy, 7, 8, 0xC8A070);
        R((int)fhx - 3, (int)fhy, 7, 1, 0xA88050);
        R((int)fhx - 1, (int)fhy - 2, 3, 2, 0xA88050);
    }
    line(s, r, X, shY + 2, fhx, fhy, 2, shD);
    R((int)fhx - 1, (int)fhy, 2, 2, skinD);
    float ffx = X - D * sw * 4, nfx = X + D * sw * 4;
    int ffy = Y - 2 - (cw < -0.3f ? 2 : 0), nfy = Y - 2 - (cw > 0.3f ? 2 : 0);
    line(s, r, X - D, hipY, ffx, ffy, 3, dress ? skinD : paD);
    line(s, r, X, hipY, nfx, nfy, 3, dress ? skin : pa);
    R(D > 0 ? (int)ffx - 1 : (int)ffx - 2, ffy + 1, 4, 2, shade(a->shoe, 0.8f));
    R(D > 0 ? (int)nfx - 1 : (int)nfx - 2, nfy + 1, 4, 2, a->shoe);
    if (a->prop == 3) { F(-7, shY + 1, 4, 9, 0x3F6FB6); F(-7, shY + 1, 4, 1, 0x5A8AD0); }   /* backpack */
    /* body */
    R(X - 4, shY, 8, torH, sh);
    F(-4, shY, 2, torH, shD);
    if (a->stripes) for (int y = shY + 2; y < hipY - 1; y += 3) R(X - 4, y, 8, 1, shade(sh, 1.25f));
    if (a->outfit == 1) { F(2, shY + 1, 2, torH - 1, a->shirt2); F(1, shY, 1, 4, shD); }
    if (a->outfit == 3) { F(-6, shY - 2, 3, 4, sh); F(0, hipY - 5, 4, 2, shD); F(2, shY + 1, 1, 3, 0xEEEEEE); }
    if (dress) {
        for (int j = 0; j < 7; j++) R(X - 4 - j / 3, hipY - 1 + j, 8 + 2 * (j / 3), 1, j == 6 ? shD : sh);
    } else {
        R(X - 4, hipY - 1, 8, 1, paD);
    }
    if (a->prop == 3) F(-2, shY, 1, 6, 0x2F5596);
    R(X - 1, shY - 1, 3, 2, skinD);
    /* near arm */
    float sx = X, sy = shY + 2;
    if (standing && a->pose == 1) {                                  /* on the phone */
        FL(0, sy, 1, shY + 8, 2, sh);
        FL(1, shY + 8, 4, shY + 4, 2, skin);
        F(4, shY, 2, 4, 0x22222A);
        F(4, shY + 1, 1, 2, 0x9FD8FF);
        down = 1;
    } else if (standing && a->pose == 2) {                           /* waving */
        int wv = (int)roundf(sinf(a->t * 8) * 2);
        FL(0, sy, 2, shY - 3, 2, sh);
        FL(2, shY - 3, 4 + wv, shY - 8, 2, skin);
        F(3 + wv, shY - 10, 2, 2, skin);
    } else if (standing && a->pose == 3) {                           /* arms crossed */
        F(-3, shY + 4, 7, 2, shD);
        F(3, shY + 4, 1, 2, skin);
    } else if (a->prop == 4) {                                       /* holding a balloon */
        FL(0, sy, 3, shY - 1, 2, skin);
        float bx = X + D * 5 + sinf(a->t * 1.7f) * 1.5f, by = top - 14;
        line(s, r, X + D * 3, shY - 2, bx, by + 5, 1, 0xE8E8E8);
        ell(s, r, bx, by, 4, 5, 0xE83A4A, 255);
        R((int)bx - 2, (int)by - 3, 2, 2, 0xFFA0A8);
        R((int)bx, (int)by + 5, 1, 1, 0xB82A3A);
    } else {
        float hx = X - D * sw * 3, hy = shY + 10;
        float mx = sx + (hx - sx) * 0.45f, my = sy + (hy - sy) * 0.45f;
        line(s, r, sx, sy, mx, my, 2, shade(sh, 0.92f));
        line(s, r, mx, my, hx, hy, 2, skin);
        R((int)hx - 1, (int)hy, 2, 2, skin);
        if (a->prop == 1) {                                          /* coffee */
            R((int)hx - 1, (int)hy - 4, 3, 5, 0xF4F0E8);
            R((int)hx - 1, (int)hy - 2, 3, 2, 0x8A5A32);
            R((int)hx - 1, (int)hy - 5, 3, 1, 0x3A3A3A);
        }
    }
    draw_head(s, r, a, X, top, D, down, 0);
}

static void draw_dog(const City *s, SDL_Renderer *r, float fx, float fy, int D, float ph, int moving, int sniff,
                     const Breed *b, int big, float t) {
    int X = (int)floorf(fx), Y = (int)floorf(fy);
    const uint32_t *c = big ? CORGI : b->c;
    uint32_t body = c[0], dark = c[1], belly = c[2];
    int L = big ? 16 : b->L, legH = big ? 4 : b->legH, bh = big ? 8 : b->bh, pointy = big ? 1 : b->pointy;
    int r0 = -L / 2, f = L / 2;
    float bcy = Y - legH - bh / 2.0f;
    ell(s, r, X, Y, L / 2 + 4, 1.2f, 0x000000, 45);
    float wag = sinf(t * (moving ? 14 : 8));
    if (!big) {                                                      /* tail */
        if (b->curl) { FL(r0 + 1, bcy - bh / 2, r0 - 2, bcy - bh / 2 - 4, 2, body); FL(r0 - 2, bcy - bh / 2 - 4, r0 + 2, bcy - bh / 2 - 6, 2, belly); }
        else FL(r0 + 1, bcy - bh / 2 + 1, r0 - 5, bcy - bh / 2 - 4 + wag * 2, 2, body);
    }
    int legs[4] = { r0 + 3, r0 + 5, f - 4, f - 2 };
    if (big) { legs[0] = r0 - 1; legs[1] = r0 + 2; }
    for (int i = 0; i < 4; i++) {                                    /* far legs first */
        int k = i;
        float sv = sinf(ph + (k == 0 || k == 3 ? 0 : PI_F)), cv = cosf(ph + (k == 0 || k == 3 ? 0 : PI_F));
        int dx = moving ? (int)roundf(sv * 2) : 0, lift = moving && cv > 0.4f;
        uint32_t lc = i % 2 == 0 ? dark : body;
        F(legs[k] + dx, (int)(bcy + bh / 2.0f) - 1, 2, legH + 1 - lift, lc);
        F(legs[k] + dx, Y - 1 - lift, 3, 1, i % 2 == 0 ? dark : belly);
    }
    FE(0, bcy, L / 2.0f, bh / 2.0f, body);
    FE(1, bcy + bh / 4.0f, L / 2.0f - 3, bh / 4.0f, belly);
    F(r0 + 3, (int)(bcy - bh / 2.0f), L - 8, 1, shade(body, 1.12f));
    if (!big && b->spots) {
        F(r0 + 4, (int)bcy - 2, 2, 2, dark); F(r0 + 10, (int)bcy - 1, 3, 2, dark); F(r0 + 15, (int)bcy - 3, 2, 2, dark);
    }
    if (big) {                                                       /* the very big butt */
        int wx = (int)roundf(sinf(ph) * 1.2f), wy = (int)roundf(sinf(ph * 2) * 1);
        int hx = r0 - 1 + wx, hy = Y - legH - 9 + wy;
        FE(hx, hy, 9.5f, 9.5f, body);
        FE(hx + 1, hy + 5, 6, 3.5f, belly);
        FE(hx - 2, hy - 4, 4, 2.5f, shade(body, 1.15f));
        for (int j = -6; j <= 6; j++) F(hx - 9 + abs(j) / 3, hy + j, 1, 1, dark);   /* the crease */
        FE(hx - 9, hy - 7, 2.5f, 2.5f, belly);                           /* fluffy tail nub */
        FE(hx - 9 + (int)roundf(wag), hy - 8, 1.5f, 1.5f, 0xFFFFFF);
    }
    int hd = sniff ? 5 : 0, hx = f + 1;
    float hy = bcy - bh / 2.0f - 2 + hd;
    FE(f - 1, bcy - bh / 4.0f, 4, bh / 2.0f, body);                  /* neck */
    FE(hx, hy, 5, 4, body);
    FE(hx + 5, hy + 1, 3, 2, belly);
    F(hx + 7, (int)hy - 1, 2, 2, 0x111111);
    F(hx + 1, (int)hy - 2, 2, 2, 0x111111);
    F(hx + 1, (int)hy - 2, 1, 1, 0xFFFFFF);
    F(hx + 4, (int)hy + 2, 3, 1, dark);
    if (moving && fmodf(t, 2.4f) < 1.4f) F(hx + 5, (int)hy + 3, 2, 2, 0xF07890);   /* panting */
    if (pointy) {
        F(hx - 3, (int)hy - 6, 3, 3, dark); F(hx - 2, (int)hy - 8, 2, 2, dark); F(hx - 2, (int)hy - 5, 1, 2, 0xF0A0A8);
        if (big) { F(hx, (int)hy - 6, 3, 3, dark); F(hx + 1, (int)hy - 8, 1, 2, dark); }
    } else {
        FE(hx - 2, hy + 1, 2, 4, dark);
    }
    F(hx - 4, (int)hy + 3, 2, 4, 0xD23A3A);                          /* collar and tag */
    F(hx - 3, (int)hy + 7, 1, 1, 0xFFD24A);
}

static void draw_cat_walk(const City *s, SDL_Renderer *r, const Actor *a) {
    int X = (int)floorf(a->x), Y = (int)floorf(a->y), D = a->dir;
    uint32_t body = a->pet[0], dark = a->pet[1], belly = a->pet[2];
    int moving = a->state == S_WALK, legH = 5, r0 = -8, f = 8;
    float bcy = Y - legH - 3;
    ell(s, r, X, Y, 10, 1.2f, 0x000000, 45);
    int sway = (int)roundf(sinf(a->t * 2.2f) * 2);
    FL(r0 + 1, bcy - 1, r0 - 2, bcy - 7, 2, body);                   /* tail up, tip curling */
    FL(r0 - 2, bcy - 7, r0 - 1 + sway, bcy - 12, 2, body);
    FL(r0 - 1 + sway, bcy - 12, r0 + 1 + sway, bcy - 14, 2, dark);
    int legs[4] = { r0 + 2, r0 + 4, f - 4, f - 2 };
    for (int i = 0; i < 4; i++) {
        float sv = sinf(a->ph + (i == 0 || i == 3 ? 0 : PI_F)), cv = cosf(a->ph + (i == 0 || i == 3 ? 0 : PI_F));
        int dx = moving ? (int)roundf(sv * 2) : 0, lift = moving && cv > 0.4f;
        F(legs[i] + dx, (int)bcy + 2, 2, legH + 1 - lift, i % 2 == 0 ? dark : body);
    }
    FE(0, bcy, 8, 3, body);
    FE(1, bcy + 1.5f, 6, 1.5f, belly);
    if (a->pet != CATC[1] && a->pet != CATC[3]) for (int i = 0; i < 4; i++) F(r0 + 3 + i * 3, (int)bcy - 3, 1, 3, dark);
    float hy = bcy - 4;
    FE(f + 1, hy, 4, 3.5f, body);
    F(f - 1, (int)hy - 5, 2, 2, body); F(f - 1, (int)hy - 6, 1, 1, body);
    F(f + 2, (int)hy - 5, 2, 2, body); F(f + 3, (int)hy - 6, 1, 1, body);
    F(f + 2, (int)hy - 4, 1, 1, 0xF0A0A8);
    F(f + 3, (int)hy - 1, 1, 1, 0x7ED860); F(f + 3, (int)hy, 1, 1, 0x1A1A1A);
    F(f + 5, (int)hy + 1, 1, 1, 0xF07890);
    box(s, r, D > 0 ? X + f + 5 : X - f - 8, (int)hy + 1, 4, 1, 0xF4F4F4, 150);   /* whiskers */
    box(s, r, D > 0 ? X + f + 5 : X - f - 8, (int)hy + 3, 4, 1, 0xF4F4F4, 150);
}

static void draw_cat_sit(const City *s, SDL_Renderer *r, const Actor *a) {
    int X = (int)a->x, Y = (int)a->y, D = a->dir;
    uint32_t body = a->pet[0], dark = a->pet[1], belly = a->pet[2];
    int lick = a->pose == 1, blink = a->pose == 2 && fmodf(a->t * 3, 2) < 0.25f, front = a->pose == 3;
    int flick = (int)roundf(sinf(a->t * 3) * 2);
    FL(-3, Y - 1, -8, Y - 1, 2, body);                               /* tail along the ledge */
    FL(-8, Y - 1, -10, Y - 4 - flick, 2, body);
    F(-11, Y - 6 - flick, 2, 2, dark);
    FE(-2, Y - 4, 5, 3.5f, body);
    FE(0, Y - 8, 4, 6, body);
    FE(2, Y - 8, 2, 4, belly);
    F(1, Y - 2, 2, 2, belly); F(3, Y - 2, 2, 2, belly);
    if (body == CATC[0][0] || body == CATC[4][0]) { F(-4, Y - 10, 2, 1, dark); F(-4, Y - 7, 3, 1, dark); F(-5, Y - 4, 3, 1, dark); }
    int hy = lick ? Y - 13 : Y - 16, ho = lick ? 3 : front ? 0 : 2;
    FE(ho, hy, 4.5f, 4, body);
    F(ho - 3, hy - 6, 2, 3, body); F(ho - 3, hy - 7, 1, 1, body);
    F(ho + 2, hy - 6, 2, 3, body); F(ho + 3, hy - 7, 1, 1, body);
    F(ho - 2, hy - 5, 1, 2, 0xF0A0A8); F(ho + 2, hy - 5, 1, 2, 0xF0A0A8);
    uint32_t eye = blink || lick ? dark : 0x7ED860;
    if (front) {
        F(ho - 2, hy - 1, 1, blink ? 1 : 2, eye); F(ho + 1, hy - 1, 1, blink ? 1 : 2, eye);
        F(ho - 1, hy + 1, 2, 1, 0xF07890);
    } else {
        F(ho + 2, hy - 1, 1, blink || lick ? 1 : 2, eye);
        F(ho + 4, hy + 1, 1, 1, 0xF07890);
        box(s, r, D > 0 ? X + ho + 4 : X - ho - 7, hy + 2, 4, 1, 0xF4F4F4, 150);
    }
    if (lick) { F(ho + 3, hy + 3, 2, 4, belly); F(ho + 4, hy + 2, 1, 1, 0xF07890); }
}

static void draw_pigeon(const City *s, SDL_Renderer *r, const Actor *a) {
    int X = (int)a->x, Y = (int)a->y, D = a->dir;
    if (a->state == S_AWAY) return;
    if (a->state == S_FLEE) {
        float k = a->timer;
        Y = (int)(a->y - k * 60 - k * k * 12);
        int up = sinf(a->ph) > 0;
        FE(0, Y - 5, 6, 3, 0x8A90A0);
        FE(5, Y - 7, 2.5f, 2.5f, 0x6E7488);
        FL(-1, Y - 6, -4, up ? Y - 14 : Y + 1, 3, 0xA8AEBC);
        FL(1, Y - 6, 3, up ? Y - 13 : Y + 2, 2, 0x9AA0AE);
        return;
    }
    int peck = a->state == S_STAND && sinf(a->ph) > 0.2f;
    ell(s, r, X, Y, 6, 1, 0x000000, 40);
    int st = a->state == S_WALK && (int)(a->ph * 2) % 2;
    F(st ? -1 : 0, Y - 3, 1, 3, 0xD06A6A); F(st ? 2 : 1, Y - 3, 1, 3, 0xD06A6A);
    F(-9, Y - 7, 4, 2, 0x5E6478);
    FE(0, Y - 6, 6, 3.5f, 0x8A90A0);
    FE(-1, Y - 6, 4, 2.5f, 0x9EA4B4);
    F(-4, Y - 6, 1, 2, 0x4E5468); F(-1, Y - 6, 1, 2, 0x4E5468);
    int hx = peck ? 6 : 4, hy = peck ? Y - 4 : Y - 10;
    F(hx - 2, hy + 1, 3, 3, 0x5E9A7E);                               /* shiny neck */
    F(hx - 1, hy + 2, 2, 2, 0x8A5AA0);
    FE(hx, hy, 2.5f, 2.5f, 0x6E7488);
    F(hx + 1, hy - 1, 1, 1, 0xF2A23A);
    F(hx + 3, hy, 2, 1, 0xC89090);
}

static void wheel(const City *s, SDL_Renderer *r, float cx, float cy, float rad, float ph) {
    for (int i = 0; i < 40; i++) {
        float a = i * 2 * PI_F / 40;
        R((int)roundf(cx + cosf(a) * rad), (int)roundf(cy + sinf(a) * rad), 1, 1, 0x1E1E24);
        R((int)roundf(cx + cosf(a) * (rad - 1)), (int)roundf(cy + sinf(a) * (rad - 1)), 1, 1, 0x2E2E36);
    }
    for (int k = 0; k < 3; k++) {
        float a = ph + k * PI_F / 3;
        line(s, r, cx - cosf(a) * (rad - 2), cy - sinf(a) * (rad - 2), cx + cosf(a) * (rad - 2), cy + sinf(a) * (rad - 2), 1, 0xA8ACB8);
    }
    R((int)cx - 1, (int)cy - 1, 2, 2, 0x8A8E98);
}

static void draw_bike(const City *s, SDL_Renderer *r, const Actor *a) {
    int X = (int)a->x, Y = (int)a->y, D = a->dir;
    uint32_t frame = a->shirt == 0xEDEDED || a->shirt == 0x2A2A30 ? 0xD94F4F : shade(a->shirt, 0.85f);
    ell(s, r, X, Y, 18, 1.5f, 0x000000, 50);
    float pa = a->ph * 0.8f;
    float p1x = X - D * 1 + D * cosf(pa) * 4, p1y = Y - 8 + sinf(pa) * 4;
    float p2x = X - D * 1 - D * cosf(pa) * 4, p2y = Y - 8 - sinf(pa) * 4;
    float hipx = X - D * 4, hipy = Y - 20;
    float k2x = (hipx + p2x) / 2 + D * 3, k2y = (hipy + p2y) / 2 - 2;
    line(s, r, hipx, hipy, k2x, k2y, 3, shade(a->pants, 0.75f));    /* far leg */
    line(s, r, k2x, k2y, p2x, p2y, 2, shade(a->pants, 0.75f));
    R((int)p2x - 1, (int)p2y, 3, 2, shade(a->shoe, 0.8f));
    wheel(s, r, X - D * 10, Y - 8, 7.5f, a->ph);
    wheel(s, r, X + D * 10, Y - 8, 7.5f, a->ph);
    FL(-10, Y - 8, -1, Y - 8, 2, frame);
    FL(-1, Y - 8, -4, Y - 18, 2, frame);
    FL(-4, Y - 17, 7, Y - 17, 2, frame);
    FL(-1, Y - 8, 7, Y - 17, 2, frame);
    FL(7, Y - 17, 10, Y - 8, 1, 0x7A7E88);
    FL(7, Y - 17, 8, Y - 21, 1, 0x7A7E88);
    F(7, Y - 22, 3, 1, 0x1A1A1A);
    F(-7, Y - 20, 6, 2, 0x1A1A1A);
    R(X - D - 1, Y - 9, 2, 2, 0x5A5E68);
    if (a->basket) {
        F(9, Y - 25, 7, 5, 0xB8864A);
        for (int i = 0; i < 7; i += 2) F(9 + i, Y - 25, 1, 5, 0x8A6034);
        if (a->basket == 1) { F(10, Y - 27, 1, 2, 0xF25A7A); F(12, Y - 28, 1, 3, 0xFFD24A); F(14, Y - 27, 1, 2, 0xF25A7A); }
        else { FE(12, Y - 28, 3, 2.5f, 0xE8D4A8); F(14, Y - 29, 1, 1, 0x111111); F(10, Y - 31, 1, 2, 0xA88A5A); }
    }
    float shx = X + D * 1, shy = Y - 31;
    line(s, r, hipx, hipy, shx, shy, 6, a->shirt);                   /* body leaning in */
    if (a->stripes) line(s, r, hipx, hipy - 4, shx, shy + 2, 1, shade(a->shirt, 1.25f));
    float k1x = (hipx + p1x) / 2 + D * 3, k1y = (hipy + p1y) / 2 - 2;
    line(s, r, hipx, hipy, k1x, k1y, 3, a->pants);                   /* near leg */
    line(s, r, k1x, k1y, p1x, p1y, 2, a->pants);
    R((int)p1x - 1, (int)p1y, 3, 2, a->shoe);
    line(s, r, shx, shy + 1, X + D * 5, Y - 26, 2, shade(a->shirt, 0.92f));
    line(s, r, X + D * 5, Y - 26, X + D * 8, Y - 22, 2, a->skin);
    draw_head(s, r, a, X + D * 2, Y - 41, D, 0, a->helmet);
}

static void draw_car(const City *s, SDL_Renderer *r, const Actor *a) {
    int X = (int)a->x, Y = (int)a->y, D = a->dir, L = car_len(a), hl = L / 2;
    int bus = a->role == R_BUS, van = !bus && a->kind == 3, taxi = !bus && a->kind == 2, ice = van && a->pose == 1;
    uint32_t col = bus ? a->car : taxi ? 0xF2C230 : ice ? 0xF7B8C8 : a->car;
    uint32_t glass = 0x9FCFEA, dk = shade(col, 0.7f);
    ell(s, r, X, Y, hl + 2, 2, 0x000000, 70);
    if (bus || van) {
        int bh = bus ? 30 : 22;
        F(-hl, Y - 5 - bh, L, bh, col);
        F(-hl, Y - 5 - bh, L, 1, shade(col, 1.2f));
        F(-hl, Y - 7, L, 2, dk);
        if (bus) {
            F(-hl, Y - 16, L, 2, 0xF4F4F4);
            for (int i = 0; i < 7; i++) {
                int wx = -hl + 4 + i * 9;
                F(wx, Y - 31, 7, 9, glass);
                F(wx, Y - 31, 7, 1, 0xDCF0FC);
                unsigned hsh = (unsigned)(i * 2654435761u + (unsigned)(a->car));
                if ((hsh >> 5) % 3) {                                /* passengers */
                    F(wx + 2, Y - 27, 3, 4, SKIN[(hsh >> 8) % 6]);
                    F(wx + 2, Y - 28, 3, 1, HAIRC[(hsh >> 12) % 7]);
                }
            }
            F(hl - 6, Y - 33, 5, 18, glass);
            F(hl - 17, Y - 31, 9, 25, 0x3A3A44);
            F(hl - 16, Y - 30, 3, 23, glass); F(hl - 12, Y - 30, 3, 23, glass);
            F(hl - 14, Y - 36, 12, 4, 0x22262C);
            int tx = D > 0 ? X + hl - 13 : X - hl + 3;
            for (int i = 0; i < 2; i++)
                for (int j = 0; j < 3; j++) R(tx + i * 4 + (j % 2), Y - 35 + j, 2, 1, 0xFFB02A);
        } else {
            F(hl - 8, Y - 25, 7, 9, glass);
            F(-hl + 3, Y - 23, 12, 7, glass);
            F(hl - 12, Y - 25, 1, 18, dk);
            if (ice) {
                for (int i = 0; i < L; i += 6) F(-hl + i, Y - 12, 3, 4, 0xFFFFFF);
                FE(-2, Y - 33, 4, 4, 0xF7A0C0);
                FE(-2, Y - 35, 3, 3, 0xFFF0F4);
                float xs0 = (float)(D > 0 ? X - 6 : X + 6);
                line(s, r, xs0, Y - 30, X - D * 2, Y - 24, 2, 0xD8A050);
                line(s, r, X + D * 2 - 0.0f, Y - 30, X - D * 2, Y - 24, 2, 0xD8A050);
            }
        }
    } else {
        int hatch = a->kind == 1;
        F(-hl + 2, Y - 14, L - 4, 9, col);
        F(-hl, Y - 12, L, 7, col);
        for (int j = 0; j < 9; j++) {                                /* cabin */
            int lo = hatch ? -hl + 3 + j / 4 : -hl + 8 + j / 2, hi = hl - 10 - j / 2;
            int rowy = Y - 14 - 9 + j;
            F(lo, rowy, hi - lo, 1, col);
            if (j > 1 && j < 8) {
                F(lo + 2, rowy, (hi - lo) / 2 - 3, 1, glass);
                F(lo + (hi - lo) / 2 + 1, rowy, (hi - lo) / 2 - 3, 1, glass);
            }
        }
        int cab = hatch ? -hl + 3 : -hl + 8;
        F(cab + 4, Y - 21, 1, 4, 0xDCF0FC);
        F(cab + 15, Y - 20, 3, 3, a->skin);   /* driver */
        F(-hl + 2, Y - 14, L - 4, 1, shade(col, 1.2f));
        F(-hl, Y - 6, L, 1, dk);
        F(-1, Y - 14, 1, 8, dk);
        F(2, Y - 12, 3, 1, shade(col, 0.55f));
        F(hl - 2, Y - 12, 2, 3, 0xFFF4C0);
        F(-hl, Y - 12, 2, 3, 0xD23A3A);
        if (taxi) {
            for (int i = 0; i < L - 6; i += 4) { F(-hl + 3 + i, Y - 10, 2, 2, 0x1A1A1A); F(-hl + 5 + i, Y - 10, 2, 2, 0xF4F4F4); }
            F(-3, Y - 26, 8, 3, 0xF8F0D0);
        }
    }
    F(hl - 1, Y - 8, 2, 3, 0x9A9EA8);
    F(-hl - 1, Y - 8, 2, 3, 0x9A9EA8);
    int wo = bus ? hl - 15 : hl - 9;
    for (int i = -1; i <= 1; i += 2) {
        float cx = X + i * wo;
        ell(s, r, cx, Y - 5, 6, 5, 0x2A2A30, 255);
        ell(s, r, cx, Y - 4, 4.5f, 4.5f, 0x1E1E24, 255);
        ell(s, r, cx, Y - 4, 2.2f, 2.2f, 0xB8BCC8, 255);
        R((int)roundf(cx + cosf(a->ph) * 2), (int)roundf(Y - 4 + sinf(a->ph) * 2), 1, 1, 0x4A4E58);
    }
}

/* dog: for a dog walker, draw just the dog (it is sorted on its own, a step nearer). */
static void draw_actor(const City *s, SDL_Renderer *r, const Actor *a, int dog) {
    switch (a->role) {
    case R_WALKER: case R_SHADES: case R_STANDER:
        draw_person(s, r, a, a->state == S_WALK, 0);
        break;
    case R_DOGWALK: case R_BIGBUTT: {
        int moving = a->state == S_WALK, big = a->role == R_BIGBUTT, D = a->dir;
        const Breed *b = &BREEDS[a->breed];
        int L = big ? 16 : b->L;
        float dx = a->x + D * (L / 2 + 16), dy = a->y + 2;
        if (dog) { draw_dog(s, r, dx, dy, D, a->ph * 1.8f, moving, a->state == S_SNIFF, b, big, a->t); break; }
        draw_person(s, r, a, moving, a->state == S_SNIFF);
        float hx = a->x - D * sinf(a->ph) * 3, hy = a->y - (a->kid ? 8 : 12) - 1;
        line(s, r, hx, hy, dx + D * (L / 2 - 3), dy - (big ? 4 : b->legH) - (big ? 8 : b->bh) - 1, 1, 0xC83A3A);
        break;
    }
    case R_BIKE: draw_bike(s, r, a); break;
    case R_CAR: case R_BUS: draw_car(s, r, a); break;
    case R_CATWALK: draw_cat_walk(s, r, a); break;
    case R_CATSIT: draw_cat_sit(s, r, a); break;
    case R_PIGEON: draw_pigeon(s, r, a); break;
    }
}

static void draw_bird(const City *s, SDL_Renderer *r, const Bird *b) {
    float f = sinf(b->ph);
    int big = b->size > 1.25f, w = big ? 7 : 5, up = (int)roundf(f * (big ? 4 : 3));
    uint32_t c = b->gull ? 0xF4F6FA : 0x26303E, tip = b->gull ? 0x6A7080 : c;
    int x = (int)b->x, y = (int)b->y, d = b->vx > 0 ? 1 : -1;
    R(x - 2, y, 5, 2, c);
    R(x + d * 3, y, 1, 1, c);
    for (int side = -1; side <= 1; side += 2) {
        line(s, r, x + side, y, x + side * w / 2, y - up - 1, 1, c);
        line(s, r, x + side * w / 2, y - up - 1, x + side * w, y - up + (up > 0 ? 1 : -1), 1, tip);
    }
}

static void draw_scene(City *s, SDL_Renderer *r) {
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_Rect full = { 0, 0, s->W * s->px, s->H * s->px };
    SDL_RenderCopy(r, s->sky, NULL, &full);
    for (int i = 0; i < MAXCLOUD; i++) {
        const Cloud *c = &s->cloud[i];
        SDL_Rect d = { (int)c->x * s->px, (int)c->y * s->px, s->cloudW[c->k] * s->px, s->cloudH[c->k] * s->px };
        SDL_RenderCopy(r, s->cloudTex[c->k], NULL, &d);
    }
    for (int i = 0; i < s->nb; i++) draw_bird(s, r, &s->bird[i]);
    SDL_RenderCopy(r, s->far, NULL, &full);
    SDL_RenderCopy(r, s->near, NULL, &full);
    for (int i = 0; i < s->nchim; i++)                               /* chimney smoke */
        for (int k = 0; k < 4; k++) {
            const Spot *c = &s->chimney[i];
            float ph = fmodf(s->t * 0.3f + k * 0.25f + c->ph, 1);
            float rad = 2 + ph * 5;
            ell(s, r, c->x + sinf(ph * 6 + k) * 2 + ph * 12, c->y - ph * 40, rad, rad * 0.8f, 0xE4E8EE, (int)((1 - ph) * 150));
        }
    for (int i = 0; i < s->nant; i++)                                /* blinking antenna lights */
        if (fmodf(s->t + s->antenna[i].ph, 1.6f) < 0.3f) {
            box(s, r, s->antenna[i].x - 2, s->antenna[i].y - 2, 5, 5, 0xFF3A3A, 80);
            box(s, r, s->antenna[i].x - 1, s->antenna[i].y - 1, 3, 3, 0xFF4A4A, 255);
        }
    /* Everything standing on the ground is drawn back to front by where its feet are:
       people, their dogs (a step nearer than them), the cars and the street furniture.
       Perched cats sit on the buildings, behind all of it. */
    enum { MAXITEM = MAXA * 2 + MAXPROP };
    float key[MAXITEM];
    int what[MAXITEM], n = 0;                                        /* >= 0 actor, -1-k dog of actor k, -100-k prop k */
    for (int i = 0; i < s->na; i++) {
        const Actor *a = &s->a[i];
        key[n] = a->role == R_CATSIT ? a->y - 1000 : a->y;
        what[n++] = i;
        if (a->role == R_DOGWALK || a->role == R_BIGBUTT) { key[n] = a->y + 2; what[n++] = -1 - i; }
    }
    for (int i = 0; i < s->nprop; i++) { key[n] = (float)s->prop[i].base; what[n++] = -100 - i; }
    for (int i = 1; i < n; i++) {                                    /* back to front */
        float k = key[i];
        int w = what[i], j = i - 1;
        while (j >= 0 && key[j] > k) { key[j + 1] = key[j]; what[j + 1] = what[j]; j--; }
        key[j + 1] = k;
        what[j + 1] = w;
    }
    for (int i = 0; i < n; i++) {
        int w = what[i];
        if (w >= 0) draw_actor(s, r, &s->a[w], 0);
        else if (w > -100) draw_actor(s, r, &s->a[-1 - w], 1);
        else {
            const Prop *p = &s->prop[-100 - w];
            SDL_Rect d = { p->x * s->px, p->y * s->px, p->w * s->px, p->h * s->px };
            SDL_RenderCopy(r, p->t, NULL, &d);
            if (p->light) {                                          /* the lit lamp */
                static const uint32_t LAMP[3] = { 0x3AE86A, 0xFFC82A, 0xFF3A3A };
                int lt = light(s);
                float ly = s->lightY + 3.0f + (2 - lt) * 6;
                ell(s, r, s->lightX + 1.0f, ly, 4, 4, LAMP[lt], 70);
                ell(s, r, s->lightX + 1.0f, ly, 2, 2, LAMP[lt], 255);
            }
        }
    }
}

/* A small view (a picker tile) is drawn full size off screen, then shrunk into place. */
static void draw(void *p, SDL_Renderer *r) {
    City *s = p;
    if (s->scale == 1) { draw_scene(s, r); return; }
    if (!s->small) {
        s->small = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, s->W, s->H);
        if (!s->small) return;
        SDL_SetTextureScaleMode(s->small, SDL_ScaleModeLinear);
    }
    SDL_Texture *old = SDL_GetRenderTarget(r);
    SDL_Rect vp;
    SDL_RenderGetViewport(r, &vp);
    SDL_SetRenderTarget(r, s->small);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);
    draw_scene(s, r);
    SDL_SetRenderTarget(r, old);
    SDL_RenderSetViewport(r, &vp);
    SDL_Rect d = { 0, 0, s->w, s->h };
    SDL_RenderCopy(r, s->small, NULL, &d);
}

const Saver saver_city = { "city", "CITY", create, step, draw, destroy, NULL, NULL };
