/* City: a sunny street in pixel art. Towers, shops and little houses line a sidewalk where people
   stroll, stand about on their phones, wear very cool sunglasses, walk dogs (one of them has a
   very big butt) and ride bikes, while cars and a bus roll by. Cats sit on fences and roofs or
   stroll along, pigeons peck at the pavement, and flocks of birds cross the sky. */
#include "../engine.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MAXA 96
#define MAXB 80
#define MAXCLOUD 8
#define MAXPERCH 24

enum { R_WALKER, R_SHADES, R_STANDER, R_DOGWALK, R_BIGBUTT, R_BIKE, R_CAR, R_BUS, R_CATWALK,
       R_CATSIT, R_PIGEON };
enum { S_WALK, S_STAND, S_SNIFF, S_FLEE, S_AWAY };

static const uint32_t SKIN[5] = { 0xF6D2B0, 0xE8B88A, 0xC68E5E, 0x8D5A3A, 0x5C3A24 };
static const uint32_t SHIRT[10] = { 0xD94F4F, 0x3F7FD6, 0x4FB86A, 0xF2C230, 0xB45FD0, 0xF28A3C, 0xEDEDED,
                                    0x2E9C9C, 0xE86FA0, 0x555B6E };
static const uint32_t PANTS[6] = { 0x2B3A67, 0x3A3A3A, 0x6B4F2E, 0x5A6B7E, 0x7A3A3A, 0x2F5D3A };
static const uint32_t HAIRC[6] = { 0x2A1A10, 0x6B3E1E, 0xC9A04A, 0x111111, 0xB04A1E, 0x8A8A8A };
static const uint32_t CARC[7] = { 0xD23A3A, 0x2F6FD0, 0xF2C230, 0x3AA860, 0xE8E8E8, 0x6A3AA8, 0xE87A2A };
static const uint32_t DOGC[6][3] = {           /* body, dark, belly */
    { 0xD9A05B, 0x9C6A2E, 0xF5DDB0 }, { 0x3A3A3A, 0x1A1A1A, 0x6A6A6A }, { 0xF0EAD8, 0x8A6A4A, 0xFFFFFF },
    { 0x7A4E2E, 0x4A2E18, 0xB88A5E }, { 0x9AA0AA, 0x5A606A, 0xD8DCE2 }, { 0xE2C48A, 0xA07A3A, 0xFFF0D0 },
};
static const uint32_t CATC[5][3] = {
    { 0xE8923A, 0xB5651C, 0xFFD9A8 }, { 0x2A2A2E, 0x151517, 0x55555C }, { 0x8E949E, 0x5E646E, 0xD8DCE2 },
    { 0xF2EEE6, 0xC8A27A, 0xFFFFFF }, { 0xC8924E, 0x6E4A22, 0xF0D8A8 },
};

typedef struct {
    int role, state, dir, pose, glasses, helmet, size, perch;
    float x, y, vx, ph, t, timer, tx, glint;
    uint32_t skin, shirt, pants, hair, car;
    const uint32_t *pet;
} Actor;

typedef struct { float x, y, vx, ph, size; int n; } Bird;
typedef struct { float x, y, v; int w; } Cloud;
typedef struct { int x, y; } Perch;

typedef struct {
    int w, h, px, W, H;
    int back, curb, laneBike, laneL, laneR;        /* y rows in art pixels */
    float t, hydrant;
    SDL_Texture *sky, *far, *near;
    Actor a[MAXA];
    int na;
    Bird bird[MAXB];
    int nb;
    Cloud cloud[MAXCLOUD];
    Perch perch[MAXPERCH];
    int nperch;
} City;

/* ---------- small drawing helpers ---------- */

static void box(const City *s, SDL_Renderer *r, int x, int y, int w, int h, uint32_t hex, int a) {
    SDL_SetRenderDrawColor(r, (hex >> 16) & 255, (hex >> 8) & 255, hex & 255, (Uint8)a);
    SDL_Rect d = { x * s->px, y * s->px, w * s->px, h * s->px };
    SDL_RenderFillRect(r, &d);
}
#define BOX(x, y, w, h, c) box(s, r, (x), (y), (w), (h), (c), 255)

/* A box in an actor's own frame: o is forward from x, oy up (negative) from the feet. */
static void lb(const City *s, SDL_Renderer *r, float x, float y, int dir, int o, int oy, int w, int h, uint32_t c) {
    int ix = (int)floorf(x), iy = (int)floorf(y);
    BOX(dir > 0 ? ix + o : ix - o - w + 1, iy + oy, w, h, c);
}

static void seg(const City *s, SDL_Renderer *r, int x0, int y0, int x1, int y1, uint32_t c) {
    int n = abs(x1 - x0) > abs(y1 - y0) ? abs(x1 - x0) : abs(y1 - y0);
    for (int i = 0; i <= n; i++) {
        float k = n ? (float)i / n : 0;
        BOX((int)roundf(x0 + (x1 - x0) * k), (int)roundf(y0 + (y1 - y0) * k), 1, 1, c);
    }
}

static uint32_t shade(uint32_t hex, float k) {
    int r = (int)(((hex >> 16) & 255) * k), g = (int)(((hex >> 8) & 255) * k), b = (int)((hex & 255) * k);
    return ((uint32_t)(r > 255 ? 255 : r) << 16) | ((uint32_t)(g > 255 ? 255 : g) << 8) | (uint32_t)(b > 255 ? 255 : b);
}

static float ground_y(const City *s, int lane) { return (float)(s->back + 6 + lane * (s->curb - s->back - 8) / 4); }

/* ---------- the still scenery, drawn once ---------- */

static void windows(Canvas *c, int x0, int y0, int x1, int y1, uint32_t glass, int gx, int gy) {
    for (int y = y0; y + 2 <= y1; y += gy)
        for (int x = x0; x + 1 <= x1; x += gx) {
            int k = rand() % 8;
            uint32_t col = k == 0 ? 0xFFE59A : k == 1 ? 0x6F9FC8 : glass;
            fill_rect(c, x, y, x + 1, y + 1, rgb(col));
            put(c, x, y, rgb(shade(col, 1.25f)));
        }
}

static void add_perch(City *s, int x, int y) {
    if (s->nperch < MAXPERCH) s->perch[s->nperch++] = (Perch){ x, y };
}

static void tree(Canvas *c, int x, int base, int hgt) {
    fill_rect(c, x - 1, base - hgt, x, base - 1, rgb(0x6B4423));
    uint32_t g = rand() % 2 ? 0x3E9A48 : 0x4FAE4E;
    fill_ellipse(c, (float)x, (float)(base - hgt - 4), 9, 8, rgb(shade(g, 0.8f)));
    fill_ellipse(c, (float)x - 1, (float)(base - hgt - 5), 8, 7, rgb(g));
    fill_ellipse(c, (float)x - 3, (float)(base - hgt - 8), 3, 2, rgb(shade(g, 1.2f)));
}

static void build_far(City *s, Canvas *c) {
    int x = -4;
    while (x < s->W) {
        int w = 10 + irand(16), h = (int)(s->back * (0.4f + 0.35f * frand(0, 1)));
        uint32_t col = (x / 7) % 2 ? 0x9DB4D4 : 0xA9BEDA;
        fill_rect(c, x, s->back - h, x + w - 1, s->back, rgb(col));
        fill_rect(c, x, s->back - h, x + w - 1, s->back - h, rgb(shade(col, 1.08f)));
        if (rand() % 3 == 0) fill_rect(c, x + w / 2, s->back - h - 8, x + w / 2, s->back - h, rgb(col));
        for (int y = s->back - h + 4; y < s->back - 4; y += 5)
            for (int xx = x + 2; xx < x + w - 2; xx += 4)
                if (rand() % 3) put(c, xx, y, rgb(shade(col, 1.12f)));
        x += w - 1 + irand(3);
    }
}

static void build_near(City *s, Canvas *c) {
    int x = -2, prev = -1;
    while (x < s->W) {
        int kind = irand(10), gap = 0;
        if (kind == prev && kind < 5) kind = 9 - kind;
        prev = kind;
        int base = s->back;
        if (kind < 3) {                                   /* tower */
            int w = 26 + irand(18), h = (int)(s->back * (0.42f + 0.28f * frand(0, 1)));
            static const uint32_t cols[5] = { 0x7E8DA6, 0xB8A58A, 0x8A6F66, 0x9BA6A0, 0xC6B7A0 };
            uint32_t col = cols[irand(5)];
            fill_rect(c, x, base - h, x + w - 1, base, rgb(col));
            fill_rect(c, x, base - h, x + 1, base, rgb(shade(col, 1.15f)));
            fill_rect(c, x + w - 2, base - h, x + w - 1, base, rgb(shade(col, 0.8f)));
            fill_rect(c, x - 1, base - h - 2, x + w, base - h - 1, rgb(shade(col, 0.7f)));
            fill_rect(c, x + w / 2 - 3, base - h - 6, x + w / 2 + 2, base - h - 3, rgb(shade(col, 0.85f)));
            fill_rect(c, x + w / 2, base - h - 14, x + w / 2, base - h - 7, rgb(0x55555C));
            windows(c, x + 4, base - h + 5, x + w - 5, base - 14, 0x9FC6E6, 5, 6);
            fill_rect(c, x + w / 2 - 3, base - 9, x + w / 2 + 2, base, rgb(0x3E3226));       /* door */
            fill_rect(c, x + w / 2 - 2, base - 8, x + w / 2 - 1, base - 1, rgb(0xA8D4F0));
            fill_rect(c, x + w / 2 + 0, base - 8, x + w / 2 + 1, base - 1, rgb(0xA8D4F0));
            add_perch(s, x + w / 3, base - h - 2);
            x += w;
        } else if (kind < 5) {                            /* mid-rise */
            int w = 22 + irand(16), h = (int)(s->back * (0.3f + 0.2f * frand(0, 1)));
            static const uint32_t cols[5] = { 0xC9694B, 0xB4574A, 0xD8B070, 0x7B9A6E, 0xA4745E };
            uint32_t col = cols[irand(5)];
            fill_rect(c, x, base - h, x + w - 1, base, rgb(col));
            fill_rect(c, x, base - h, x + w - 1, base - h, rgb(shade(col, 1.2f)));
            fill_rect(c, x - 1, base - h - 2, x + w, base - h - 1, rgb(shade(col, 0.7f)));
            for (int i = 0; i < w - 3; i += 4) put(c, x + 2 + i, base - h - 3, rgb(shade(col, 0.7f)));
            windows(c, x + 3, base - h + 4, x + w - 4, base - 14, 0xA8CBE8, 5, 6);
            fill_rect(c, x + 3, base - 11, x + 8, base, rgb(0x3E3226));
            fill_rect(c, x + 4, base - 10, x + 7, base - 1, rgb(0xE8D8B0));
            add_perch(s, x + w - 6, base - h - 2);
            x += w;
        } else if (kind < 7) {                            /* shop with an awning */
            int w = 20 + irand(10), h = 26 + irand(10);
            static const uint32_t cols[4] = { 0xE8D2A0, 0xD0E0C8, 0xE8B8B0, 0xB8D0E8 };
            static const uint32_t awn[4] = { 0xD94F4F, 0x2E9C9C, 0xF2C230, 0x6A3AA8 };
            int k = irand(4);
            fill_rect(c, x, base - h, x + w - 1, base, rgb(cols[k]));
            fill_rect(c, x - 1, base - h - 1, x + w, base - h, rgb(shade(cols[k], 0.7f)));
            windows(c, x + 3, base - h + 4, x + w - 4, base - h + 7, 0xA8CBE8, 5, 4);
            fill_rect(c, x + 2, base - 15, x + w - 3, base - 8, rgb(awn[k]));
            for (int i = x + 2; i < x + w - 3; i += 2) fill_rect(c, i, base - 15, i, base - 8, rgb(0xF4F0E8));
            fill_rect(c, x + 2, base - 7, x + w - 3, base - 7, rgb(shade(awn[k], 0.6f)));
            fill_rect(c, x + 3, base - 6, x + w - 4, base, rgb(0x2E3A4A));
            fill_rect(c, x + 4, base - 6, x + w / 2 - 1, base, rgb(0x9FD0E8));
            fill_rect(c, x + w / 2 + 1, base - 6, x + w - 5, base, rgb(0x9FD0E8));
            add_perch(s, x + w / 2, base - 15);
            x += w;
        } else if (kind < 9) {                            /* a little house */
            int w = 24 + irand(8), h = 17 + irand(5);
            static const uint32_t cols[5] = { 0xF2E2B8, 0xE8B8B8, 0xB8D8E8, 0xD8E8B8, 0xF4F0E8 };
            static const uint32_t roofs[4] = { 0x9E3A2E, 0x5A4A6A, 0x3A5A7A, 0x6A4A2E };
            uint32_t col = cols[irand(5)], roof = roofs[irand(4)];
            int top = base - h;
            fill_rect(c, x + 1, top, x + w - 2, base, rgb(col));
            fill_rect(c, x + w - 3, top, x + w - 2, base, rgb(shade(col, 0.85f)));
            float xs[3] = { (float)x - 2, (float)x + w / 2.0f, (float)x + w + 1 }, ys[3] = { (float)top, (float)top - 12, (float)top };
            fill_poly(c, xs, ys, 3, rgb(roof));
            fill_rect(c, x - 2, top, x + w + 1, top, rgb(shade(roof, 0.7f)));
            fill_rect(c, x + w - 8, top - 12, x + w - 6, top - 4, rgb(0x8A4A3A));            /* chimney */
            fill_rect(c, x + 3, top + 4, x + 7, top + 9, rgb(0xA8D4F0));
            fill_rect(c, x + 5, top + 4, x + 5, top + 9, rgb(0xF4F0E8));
            fill_rect(c, x + 3, top + 6, x + 7, top + 6, rgb(0xF4F0E8));
            fill_rect(c, x + w - 10, top + 5, x + w - 6, base, rgb(0x8A5A32));              /* door */
            put(c, x + w - 7, base - 5, rgb(0xFFE27A));
            for (int i = x - 2; i < x + w + 2; i += 2) fill_rect(c, i, base - 5, i, base, rgb(0xFAFAF0));   /* fence */
            fill_rect(c, x - 2, base - 4, x + w + 1, base - 4, rgb(0xFAFAF0));
            fill_rect(c, x - 2, base - 1, x + w + 1, base - 1, rgb(0xFAFAF0));
            add_perch(s, x + 3 + irand(w - 6), base - 5);
            add_perch(s, x + w / 2 + irand(5) - 2, top - 12);
            x += w + 2;
        } else {                                          /* a patch of park */
            int w = 18 + irand(10);
            fill_rect(c, x, base - 3, x + w - 1, base, rgb(0x5DB05A));
            tree(c, x + w / 2, base - 3, 12 + irand(6));
            fill_rect(c, x + 1, base - 6, x + 5, base - 5, rgb(0x8A5A32));                   /* bench */
            fill_rect(c, x + 1, base - 4, x + 1, base - 3, rgb(0x3A3A3A));
            fill_rect(c, x + 5, base - 4, x + 5, base - 3, rgb(0x3A3A3A));
            fill_rect(c, x + 1, base - 8, x + 5, base - 7, rgb(0x7A4A2A));
            gap = w;
            x += w;
        }
        (void)gap;
    }
    outline(c, rgb(0x2A2230));
}

static void build_ground(City *s, Canvas *c) {
    int W = s->W, H = s->H;
    fill_rect(c, 0, s->back + 1, W - 1, s->curb, rgb(0xC9C2B4));                         /* sidewalk */
    for (int x = 0; x < W; x += 14) fill_rect(c, x, s->back + 1, x, s->curb, rgb(0xB8B0A2));
    for (int y = s->back + 8; y < s->curb; y += 8) fill_rect(c, 0, y, W - 1, y, rgb(0xBDB5A7));
    fill_rect(c, 0, s->back + 1, W - 1, s->back + 1, rgb(0xE2DCD0));
    fill_rect(c, 0, s->curb + 1, W - 1, s->curb + 2, rgb(0xE8E2D6));                    /* curb */
    fill_rect(c, 0, s->curb + 3, W - 1, s->curb + 3, rgb(0x8A8478));
    fill_rect(c, 0, s->curb + 4, W - 1, H - 1, rgb(0x4A4E58));                           /* road */
    for (int y = s->curb + 4; y < H; y += 2)
        for (int x = (y / 2) % 2; x < W; x += 6) put(c, x, y, rgb(0x50545E));
    int bl = s->laneBike + 4;
    for (int x = 0; x < W; x += 12) fill_rect(c, x, bl, x + 6, bl, rgb(0xF4F0E8));      /* bike lane edge */
    int ml = (s->laneL + s->laneR) / 2 + 1;
    for (int x = 4; x < W; x += 16) fill_rect(c, x, ml, x + 8, ml, rgb(0xF2C230));      /* centre line */
    /* lamps */
    for (int x = 40; x < W; x += 92) {
        fill_rect(c, x, s->back - 18, x, s->back + 2, rgb(0x2E3A3A));
        fill_rect(c, x, s->back - 18, x + 4, s->back - 18, rgb(0x2E3A3A));
        fill_rect(c, x + 3, s->back - 17, x + 5, s->back - 16, rgb(0xFFF2B0));
    }
    /* trees on the pavement */
    for (int x = 12; x < W; x += 138) tree(c, x, s->back + 5, 9);
    /* hydrant */
    int hx = (int)s->hydrant;
    fill_rect(c, hx, s->curb - 4, hx + 3, s->curb, rgb(0xD23A3A));
    fill_rect(c, hx - 1, s->curb - 3, hx + 4, s->curb - 2, rgb(0xE85A5A));
    fill_rect(c, hx, s->curb - 5, hx + 3, s->curb - 5, rgb(0xA82A2A));
    outline(c, rgb(0x2A2230));
}

/* ---------- actors ---------- */

static void look(Actor *a) {
    a->skin = SKIN[irand(5)];
    a->shirt = SHIRT[irand(10)];
    a->pants = PANTS[irand(6)];
    a->hair = HAIRC[irand(6)];
    a->helmet = rand() % 2;
    a->size = irand(3);
    a->pet = DOGC[irand(6)];
    a->car = CARC[irand(7)];
    a->glint = frand(1, 5);
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
    float startx = initial ? frand(-10, W + 10) : (a->dir > 0 ? -24.0f : W + 24);
    a->x = startx;
    switch (role) {
    case R_WALKER: case R_SHADES: case R_DOGWALK: case R_BIGBUTT:
        a->y = ground_y(s, irand(5));
        a->vx = frand(9, 15);
        if (role == R_BIGBUTT) a->vx = 8;
        break;
    case R_STANDER:
        a->y = ground_y(s, irand(5));
        a->vx = frand(9, 13);
        a->tx = frand(W * 0.08f, W * 0.92f);
        a->pose = irand(4);
        if (initial) { a->x = a->tx; a->state = S_STAND; a->timer = frand(3, 20); }
        else a->dir = a->x < a->tx ? 1 : -1;
        break;
    case R_BIKE:
        a->y = (float)s->laneBike + 2;
        a->vx = frand(26, 40);
        break;
    case R_CAR:
        a->dir = rand() % 2 ? 1 : -1;
        a->y = (float)(a->dir > 0 ? s->laneR : s->laneL);
        a->vx = frand(22, 36);
        if (!initial) a->x = a->dir > 0 ? -40.0f : W + 40;
        break;
    case R_BUS:
        a->dir = rand() % 2 ? 1 : -1;
        a->y = (float)(a->dir > 0 ? s->laneR : s->laneL);
        a->vx = frand(16, 22);
        a->x = initial ? frand(0, W) : (a->dir > 0 ? -60.0f : W + 60);
        break;
    case R_CATWALK:
        a->y = ground_y(s, irand(5));
        a->vx = frand(7, 12);
        a->pet = CATC[irand(5)];
        a->timer = frand(4, 12);
        break;
    case R_CATSIT:
        if (s->nperch) {
            a->perch = irand(s->nperch);
            a->x = (float)s->perch[a->perch].x;
            a->y = (float)s->perch[a->perch].y;
        }
        a->pet = CATC[irand(5)];
        a->timer = frand(2, 6);
        break;
    case R_PIGEON:
        a->y = ground_y(s, irand(5));
        a->x = frand(0, W);
        a->vx = 0;
        a->timer = frand(1, 4);
        break;
    }
    if (role == R_CATSIT) a->dir = s->perch[a->perch].x < s->W / 2 ? 1 : -1;
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
    s->px = h / 180 > 1 ? h / 180 : 1;
    s->W = (w + s->px - 1) / s->px;
    s->H = (h + s->px - 1) / s->px;
    s->back = (int)(s->H * 0.62f);
    s->curb = (int)(s->H * 0.79f);
    s->laneBike = s->curb + 9;
    s->laneL = s->curb + 22;
    s->laneR = s->H - 4;
    s->hydrant = s->W * 0.3f;

    Canvas c = canvas_new(s->W, s->H);
    for (int y = 0; y < s->H; y++) {
        float k = (float)y / s->back;
        if (k > 1) k = 1;
        uint32_t top = 0x5FA8E8, bot = 0xCFEAFA;
        int r = (int)(((top >> 16) & 255) * (1 - k) + ((bot >> 16) & 255) * k);
        int g = (int)(((top >> 8) & 255) * (1 - k) + ((bot >> 8) & 255) * k);
        int b = (int)((top & 255) * (1 - k) + (bot & 255) * k);
        fill_rect(&c, 0, y, s->W - 1, y, rgb(((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b));
    }
    fill_ellipse(&c, s->W * 0.82f, s->H * 0.15f, 11, 11, rgb(0xFFF4C0));
    fill_ellipse(&c, s->W * 0.82f, s->H * 0.15f, 8, 8, rgb(0xFFE680));
    s->sky = canvas_texture(ren, &c);
    c = canvas_new(s->W, s->H);
    build_far(s, &c);
    s->far = canvas_texture(ren, &c);
    c = canvas_new(s->W, s->H);
    build_near(s, &c);
    build_ground(s, &c);
    s->near = canvas_texture(ren, &c);

    for (int i = 0; i < MAXCLOUD; i++)
        s->cloud[i] = (Cloud){ frand(0, (float)s->W), frand(6, s->H * 0.3f), frand(1.5f, 4.5f), 16 + irand(22) };
    int k = s->W / 330 < 1 ? 1 : s->W / 330;
    s->nb = 0;
    int flocks = 4 + 2 * k;
    for (int f = 0; f < flocks && s->nb < MAXB - 12; f++) {
        float fx = frand(-20, (float)s->W), fy = frand(8, s->H * 0.42f), v = frand(10, 22) * (rand() % 2 ? 1 : -1);
        int n = 3 + irand(8);
        float sz = frand(0.8f, 1.6f);
        for (int i = 0; i < n && s->nb < MAXB; i++)
            s->bird[s->nb++] = (Bird){ fx - fabsf((float)i) * 4 * (v > 0 ? 1 : -1), fy + (i % 2 ? 1 : -1) * (i / 2) * 4 + frand(-2, 2),
                                       v * frand(0.95f, 1.05f), frand(0, 6.28f), sz, 0 };
    }
    s->na = 0;
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
    free(s);
}

static int off_screen(const City *s, const Actor *a) {
    return (a->dir > 0 && a->x > s->W + 30) || (a->dir < 0 && a->x < -30);
}

/* True if a person is close to (x, y). */
static int person_near(const City *s, float x, float y, float d) {
    for (int i = 0; i < s->na; i++) {
        const Actor *o = &s->a[i];
        if (o->role > R_BIKE || o->role == R_PIGEON) continue;
        if (fabsf(o->x - x) < d && fabsf(o->y - y) < d) return 1;
    }
    return 0;
}

static void step_actor(City *s, Actor *a, float dt) {
    a->t += dt;
    a->glint -= dt;
    if (a->glint < -0.25f) a->glint = frand(2, 6);
    switch (a->role) {
    case R_WALKER: case R_SHADES: case R_DOGWALK: case R_BIGBUTT:
        if (a->state == S_WALK) {
            a->x += a->dir * a->vx * dt;
            a->ph += a->vx * dt * 0.55f;
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
            a->ph += a->vx * dt * 0.55f;
            if (a->timer == 0 && (a->x - a->tx) * a->dir >= 0) { a->state = S_STAND; a->timer = frand(8, 25); a->x = a->tx; }
            if (a->timer < 0 && off_screen(s, a)) spawn(s, a, 0);
        } else {
            a->timer -= dt;
            if (a->timer <= 0) { a->state = S_WALK; a->timer = -1; a->dir = rand() % 2 ? 1 : -1; }
        }
        break;
    case R_BIKE:
        a->x += a->dir * a->vx * dt;
        a->ph += a->vx * dt * 0.5f;
        if (off_screen(s, a)) spawn(s, a, 0);
        break;
    case R_CAR: case R_BUS:
        a->x += a->dir * a->vx * dt;
        a->ph += a->vx * dt * 0.4f;
        if (off_screen(s, a)) spawn(s, a, 0);
        break;
    case R_CATWALK:
        if (a->state == S_WALK) {
            a->x += a->dir * a->vx * dt;
            a->ph += a->vx * dt * 0.9f;
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
        if (a->state == S_WALK) {
            a->timer -= dt;
            a->x += a->dir * a->vx * dt;
            a->ph += dt * 9;
            if (a->timer <= 0) { a->state = S_STAND; a->timer = frand(0.8f, 3); a->vx = 0; }
            if (person_near(s, a->x, a->y, 11)) { a->state = S_FLEE; a->dir = a->x < s->W / 2 ? -1 : 1; a->vx = 38; a->timer = 0; }
        } else if (a->state == S_STAND) {
            a->timer -= dt;
            a->ph += dt * 12;                            /* pecking */
            if (a->timer <= 0) { a->state = S_WALK; a->dir = rand() % 2 ? 1 : -1; a->vx = frand(5, 9); a->timer = frand(0.5f, 1.5f); }
            if (person_near(s, a->x, a->y, 11)) { a->state = S_FLEE; a->dir = a->x < s->W / 2 ? -1 : 1; a->vx = 38; a->timer = 0; }
        } else if (a->state == S_FLEE) {
            a->timer += dt;
            a->x += a->dir * a->vx * dt;
            a->ph += dt * 30;
            if (a->timer > 2.4f) { a->state = S_AWAY; a->timer = frand(4, 14); }
        } else {
            a->timer -= dt;
            if (a->timer <= 0) { spawn(s, a, 1); a->x = frand(0, (float)s->W); }
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
        if (c->x > s->W + 30) { c->x = -50; c->y = frand(6, s->H * 0.3f); }
    }
    for (int i = 0; i < s->nb; i++) {
        Bird *b = &s->bird[i];
        b->x += b->vx * dt;
        b->ph += dt * (7 + b->size * 2);
        if (b->vx > 0 && b->x > s->W + 20) b->x = -20 - frand(0, 20);
        if (b->vx < 0 && b->x < -20) b->x = s->W + 20 + frand(0, 20);
    }
}

/* ---------- drawing the actors ---------- */

static void draw_person(const City *s, SDL_Renderer *r, const Actor *a, int moving, int headDrop) {
    int x = (int)floorf(a->x), y = (int)floorf(a->y), d = a->dir;
    float sw = moving ? sinf(a->ph) : 0;
    box(s, r, x - 3, y, 7, 1, 0x000000, 50);
    int l = (int)roundf(sw * 2), up = moving && sw > 0.6f;
    BOX(x - 2 + l, y - 6, 2, 6 - (l > 0 && up), a->pants);
    BOX(x + 0 - l, y - 6, 2, 6 - (l < 0 && up), shade(a->pants, 0.85f));
    BOX(x - 2 + l, y - 1, 2, 1, 0x2A2A2A);
    BOX(x + 0 - l, y - 1, 2, 1, 0x2A2A2A);
    int bob = !moving && ((int)(a->t * 1.4f + a->ph) % 2 == 0) ? 0 : 0;
    BOX(x - 2, y - 12 + bob, 5, 6, a->shirt);
    BOX(x - 2, y - 7, 5, 1, shade(a->shirt, 0.85f));
    int ax = (int)roundf(-sw * 1.5f);
    BOX(x - 3 + (d > 0 ? 0 : 0), y - 11, 1, 4, shade(a->shirt, 0.9f));
    BOX(x - 3, y - 7, 1, 1, a->skin);
    (void)ax;
    if (a->role == R_STANDER && a->state == S_STAND) {
        if (a->pose == 1) {                                  /* looking at a phone */
            lb(s, r, (float)x, (float)y, d, 1, -11, 2, 2, a->shirt);
            lb(s, r, (float)x, (float)y, d, 3, -12, 1, 3, a->skin);
            lb(s, r, (float)x, (float)y, d, 3, -14, 1, 2, 0xBFE8FF);
            headDrop = 1;
        } else if (a->pose == 2) {                           /* waving */
            int wv = sinf(a->t * 8) > 0;
            lb(s, r, (float)x, (float)y, d, 2, -17, 1, 6, a->shirt);
            lb(s, r, (float)x, (float)y, d, 2 + wv, -19, 1, 2, a->skin);
        } else if (a->pose == 3) {                           /* arms crossed */
            lb(s, r, (float)x, (float)y, d, -2, -10, 5, 1, a->skin);
        }
    }
    if (a->role == R_STANDER && a->state == S_STAND && a->pose == 0)
        BOX(x + d * 3 - (d < 0 ? 0 : 0), y - 11, 1, 4, shade(a->shirt, 0.9f));
    else if (!(a->role == R_STANDER && a->state == S_STAND && a->pose)) {
        int sx = (int)roundf(sw * 1.5f);
        lb(s, r, (float)x, (float)y, d, 2 + sx, -11, 1, 4, shade(a->shirt, 0.95f));
        lb(s, r, (float)x, (float)y, d, 2 + sx, -7, 1, 1, a->skin);
    }
    int hy = y - 17 + headDrop;
    BOX(x - 2, hy, 5, 5, a->skin);
    BOX(x - 2, hy - 1, 5, 2, a->hair);
    lb(s, r, (float)x, (float)hy, d, -2, 0, 1, 3, a->hair);
    lb(s, r, (float)x, (float)hy, d, 2, 3, 1, 1, shade(a->skin, 0.8f));
    if (a->glasses) {
        BOX(x - 2, hy + 1, 5, 1, 0x101018);
        BOX(x - 2, hy + 2, 2, 1, 0x101018);
        BOX(x + 1, hy + 2, 2, 1, 0x101018);
        lb(s, r, (float)x, (float)hy, d, -3, 1, 1, 1, 0x101018);
        if (a->glint < 0) {                                  /* ting! */
            BOX(x + d, hy + 1, 1, 1, 0xFFFFFF);
            BOX(x + d, hy - 2, 1, 1, 0xFFFFFF);
            BOX(x + d, hy + 2 + 0, 1, 1, 0x101018);
            BOX(x + d - 1, hy - 1 + 0, 3, 1, 0xFFFFFF);
        }
    } else {
        lb(s, r, (float)x, (float)hy, d, 1, 2, 1, 1, 0x1A1A1A);
    }
}

/* A four-legged animal. big = the dog with the big butt; cat = pointy ears and a tall tail. */
static void draw_quad(const City *s, SDL_Renderer *r, float fx, float fy, int d, float ph, int moving, int sniff,
                      const uint32_t *pc, int L, int big, int cat, float t) {
    int x = (int)floorf(fx), y = (int)floorf(fy);
    int bh = cat ? 3 : 4, leg = 3, r0 = -L / 2, f = r0 + L - 1;
    uint32_t body = pc[0], dark = pc[1], belly = pc[2];
    box(s, r, x - L / 2 - 2, y, L + 5, 1, 0x000000, 50);
    for (int i = 0; i < 4; i++) {
        float sv = sinf(ph + (i == 0 || i == 3 ? 0 : PI_F));
        int dx = moving ? (int)roundf(sv) : 0, lift = moving && sv > 0.5f;
        int o = i < 2 ? r0 + 1 + i : f - 1 + (i - 2);
        if (big && i < 2) o = r0 + 1 + i * 2;
        lb(s, r, (float)x, (float)y, d, o + dx, -leg, 1, leg - lift, (i % 2) ? dark : body);
    }
    if (big) {
        int sway = (int)roundf(sinf(ph) * 1), bob = (int)roundf(sinf(ph * 2) * 1);
        lb(s, r, (float)x, (float)y, d, r0 + 2, -leg - bh, L - 2, bh, body);
        lb(s, r, (float)x, (float)y, d, r0 + 3, -leg - 1, L - 4, 1, belly);
        static const int rw[9] = { 4, 6, 7, 8, 8, 8, 7, 6, 4 };
        for (int j = 0; j < 9; j++)
            lb(s, r, (float)x, (float)y, d, r0 - 4 + (8 - rw[j]) / 2 + sway, -leg - 9 + j + bob + 3, rw[j], 1, j % 2 ? body : shade(body, 1.05f));
        lb(s, r, (float)x, (float)y, d, r0 - 1 + sway, -leg - 6 + bob + 3, 1, 5, shade(body, 0.7f));      /* the crack of it */
        lb(s, r, (float)x, (float)y, d, r0 - 3 + sway, -leg - 8 + bob + 3, 2, 1, belly);                   /* highlights */
        lb(s, r, (float)x, (float)y, d, r0 + 1 + sway, -leg - 5 + bob + 3, 2, 1, belly);
        lb(s, r, (float)x, (float)y, d, r0 - 5 + sway, -leg - 9 + bob + 3, 1, 2, dark);                    /* curly tail */
        lb(s, r, (float)x, (float)y, d, r0 - 6 + sway, -leg - 10 + bob + 3, 1, 1, dark);
    } else {
        lb(s, r, (float)x, (float)y, d, r0, -leg - bh, L, bh, body);
        lb(s, r, (float)x, (float)y, d, r0 + 1, -leg - 1, L - 2, 1, belly);
        lb(s, r, (float)x, (float)y, d, r0, -leg - bh, L, 1, shade(body, 1.1f));
        if (cat) {
            int sway = (int)roundf(sinf(t * 2.2f));
            lb(s, r, (float)x, (float)y, d, r0 - 1, -leg - bh - 4 - sway, 1, 5, body);
            lb(s, r, (float)x, (float)y, d, r0 - 1, -leg - bh - 5 - sway, 1, 1, dark);
        } else {
            int tw = sinf(t * 13) > 0;
            lb(s, r, (float)x, (float)y, d, r0 - 1 - tw, -leg - bh - 2, 1, 3, dark);
        }
    }
    int hd = sniff ? 3 : 0, top = leg + bh;
    lb(s, r, (float)x, (float)y, d, f - 1, -top - 3 + hd, 4, 4, body);
    lb(s, r, (float)x, (float)y, d, f + 3, -top - 1 + hd, 2, 2, cat ? body : belly);
    if (!cat) lb(s, r, (float)x, (float)y, d, f + 4, -top - 1 + hd, 1, 1, 0x1A1A1A);
    lb(s, r, (float)x, (float)y, d, f + 1, -top - 2 + hd, 1, 1, cat ? 0x8DE86A : 0x1A1A1A);
    if (cat) {
        lb(s, r, (float)x, (float)y, d, f - 1, -top - 5 + hd, 1, 2, dark);
        lb(s, r, (float)x, (float)y, d, f + 2, -top - 5 + hd, 1, 2, dark);
    } else {
        lb(s, r, (float)x, (float)y, d, f - 1, -top - 3 + hd, 1, 3, dark);
    }
}

static void draw_sitting_cat(const City *s, SDL_Renderer *r, const Actor *a) {
    int x = (int)a->x, y = (int)a->y, d = a->dir;
    uint32_t body = a->pet[0], dark = a->pet[1], belly = a->pet[2];
    int lick = a->pose == 1, blink = a->pose == 2 && fmodf(a->t * 3, 2) < 0.25f, look = a->pose == 3;
    lb(s, r, (float)x, (float)y, d, -2, -6, 5, 6, body);
    lb(s, r, (float)x, (float)y, d, 1, -5, 2, 5, belly);
    lb(s, r, (float)x, (float)y, d, -3, -4, 1, 4, dark);
    int tw = (int)roundf(sinf(a->t * 3) * 1.5f);                 /* tail swishing along the ledge */
    lb(s, r, (float)x, (float)y, d, -5, -1, 3, 1, body);
    lb(s, r, (float)x, (float)y, d, -6 - tw, -2 - (tw > 0), 1, 2, dark);
    int hy = lick ? -6 : -9;
    lb(s, r, (float)x, (float)y, d, look ? 0 : 1, hy, 4, 4, body);
    lb(s, r, (float)x, (float)y, d, look ? 0 : 1, hy - 1, 1, 1, dark);
    lb(s, r, (float)x, (float)y, d, look ? 3 : 4, hy - 1, 1, 1, dark);
    lb(s, r, (float)x, (float)y, d, 2, hy + 1, 1, 1, blink || lick ? dark : 0x8DE86A);
    lb(s, r, (float)x, (float)y, d, 4, hy + 2, 1, 1, 0xFF9EB8);
    if (lick) lb(s, r, (float)x, (float)y, d, 3, -4, 1, 2, belly);
}

static void draw_pigeon(const City *s, SDL_Renderer *r, const Actor *a) {
    int x = (int)a->x, y = (int)a->y, d = a->dir;
    if (a->state == S_AWAY) return;
    if (a->state == S_FLEE) {
        float k = a->timer;
        y = (int)(a->y - k * 30 - k * k * 6);
        int fl = sinf(a->ph) > 0;
        lb(s, r, (float)x, (float)y, d, -2, -4, 5, 2, 0x8A90A0);
        lb(s, r, (float)x, (float)y, d, 3, -5, 2, 2, 0x6E7488);
        lb(s, r, (float)x, (float)y, d, -1, fl ? -8 : -1, 3, 3, 0xA8AEBC);
        return;
    }
    int peck = a->state == S_STAND && sinf(a->ph) > 0.2f;
    box(s, r, x - 3, y, 6, 1, 0x000000, 40);
    lb(s, r, (float)x, (float)y, d, -2, -4, 5, 3, 0x8A90A0);
    lb(s, r, (float)x, (float)y, d, 0, -4, 2, 2, 0xA8AEBC);
    lb(s, r, (float)x, (float)y, d, -3, -3, 1, 2, 0x5E6478);
    lb(s, r, (float)x, (float)y, d, 3, peck ? -3 : -6, 2, 2, 0x6E7488);
    lb(s, r, (float)x, (float)y, d, 5, peck ? -2 : -5, 1, 1, 0xF2A23A);
    lb(s, r, (float)x, (float)y, d, 3, peck ? -3 : -5, 1, 1, 0xFFE27A);
    int st = (int)(a->ph * 2) % 2 && a->state == S_WALK;
    lb(s, r, (float)x, (float)y, d, st, -1, 1, 1, 0xD06A6A);
    lb(s, r, (float)x, (float)y, d, 1 - st, -1, 1, 1, 0xD06A6A);
}

static void wheel(const City *s, SDL_Renderer *r, int cx, int cy, float ph) {
    static const int ring[16][2] = { {-1,-3},{0,-3},{1,-3},{2,-2},{3,-1},{3,0},{3,1},{2,2},{1,3},{0,3},{-1,3},{-2,2},{-3,1},{-3,0},{-3,-1},{-2,-2} };
    for (int i = 0; i < 16; i++) BOX(cx + ring[i][0], cy + ring[i][1], 1, 1, 0x1E1E24);
    int sx = (int)roundf(cosf(ph * 3) * 2), sy = (int)roundf(sinf(ph * 3) * 2);
    BOX(cx + sx, cy + sy, 1, 1, 0xB8BCC8);
    BOX(cx - sx, cy - sy, 1, 1, 0xB8BCC8);
}

static void draw_bike(const City *s, SDL_Renderer *r, const Actor *a) {
    int x = (int)a->x, y = (int)a->y, d = a->dir;
    box(s, r, x - 6, y, 13, 1, 0x000000, 50);
    #define BX(o) (x + d * (o))
    uint32_t frame = a->shirt == 0xEDEDED ? 0xD94F4F : shade(a->shirt, 0.9f);
    wheel(s, r, BX(-4), y - 4, a->ph);
    wheel(s, r, BX(4), y - 4, a->ph);
    seg(s, r, BX(-4), y - 4, BX(-1), y - 4, frame);
    seg(s, r, BX(-1), y - 4, BX(-2), y - 9, frame);
    seg(s, r, BX(-2), y - 9, BX(3), y - 9, frame);
    seg(s, r, BX(-1), y - 4, BX(3), y - 9, frame);
    seg(s, r, BX(3), y - 9, BX(4), y - 4, 0x555B6E);
    seg(s, r, BX(3), y - 9, BX(3), y - 10, 0x555B6E);
    BOX(BX(-3), y - 10, 3, 1, 0x1A1A1A);
    float pa = a->ph * 3;
    int px1 = BX(-1) + (int)roundf(d * cosf(pa) * 2), py1 = y - 4 + (int)roundf(sinf(pa) * 2);
    int px2 = BX(-1) - (int)roundf(d * cosf(pa) * 2), py2 = y - 4 - (int)roundf(sinf(pa) * 2);
    seg(s, r, BX(-2), y - 10, px2, py2, shade(a->pants, 0.8f));
    BOX(px2, py2, 1, 1, 0x2A2A2A);
    BOX(BX(-3) - (d < 0 ? 1 : 0), y - 17, 3, 7, a->shirt);                     /* torso leaning forward */
    BOX(BX(-1) - (d < 0 ? 0 : 0), y - 16, 1, 1, a->shirt);
    seg(s, r, BX(-2), y - 10, px1, py1, a->pants);
    BOX(px1, py1, 1, 1, 0x2A2A2A);
    seg(s, r, BX(-1), y - 15, BX(3), y - 10, a->skin);
    int hx = BX(0) - (d < 0 ? 2 : 0);
    BOX(hx, y - 22, 4, 4, a->skin);
    if (a->helmet) { BOX(hx - (d > 0 ? 0 : 0), y - 23, 4, 2, 0xF2C230); BOX(hx + (d > 0 ? 0 : 3), y - 22, 1, 1, 0xF2C230); }
    else BOX(hx, y - 23, 4, 2, a->hair);
    if (a->glasses) {
        BOX(hx + (d > 0 ? 0 : 0), y - 21, 4, 1, 0x101018);
        if (a->glint < 0) { BOX(hx + (d > 0 ? 3 : 0), y - 22, 1, 1, 0xFFFFFF); BOX(hx + (d > 0 ? 4 : -1), y - 23, 1, 1, 0xFFFFFF); }
    } else BOX(hx + (d > 0 ? 2 : 1), y - 20, 1, 1, 0x1A1A1A);
}

static void draw_car(const City *s, SDL_Renderer *r, const Actor *a) {
    int x = (int)a->x, y = (int)a->y, d = a->dir;
    int bus = a->role == R_BUS, L = bus ? 38 : 22;
    uint32_t col = bus ? 0xF2C230 : a->car;
    box(s, r, x - L / 2 - 1, y - 1, L + 2, 2, 0x000000, 60);
    int bh = bus ? 11 : 6;
    BOX(x - L / 2, y - 3 - bh, L, bh, col);
    BOX(x - L / 2, y - 3 - bh, L, 1, shade(col, 1.2f));
    BOX(x - L / 2, y - 5, L, 2, shade(col, 0.75f));
    if (bus) {
        for (int i = 0; i < 6; i++) BOX(x - L / 2 + 2 + i * 6, y - 3 - bh + 2, 4, 4, 0xA8D4F0);
        lb(s, r, (float)x, (float)y, d, L / 2 - 6, -3 - bh + 2, 4, 7, 0xA8D4F0);
        BOX(x - L / 2 + 2, y - 3 - bh - 1, L - 4, 1, shade(col, 0.8f));
    } else {
        lb(s, r, (float)x, (float)y, d, -4, -3 - bh - 4, 11, 4, col);
        lb(s, r, (float)x, (float)y, d, -3, -3 - bh - 3, 4, 3, 0xA8D4F0);
        lb(s, r, (float)x, (float)y, d, 2, -3 - bh - 3, 4, 3, 0xA8D4F0);
        lb(s, r, (float)x, (float)y, d, L / 2 - 2, -3 - bh + 1, 2, 2, 0xFFF2B0);
        lb(s, r, (float)x, (float)y, d, -L / 2, -3 - bh + 1, 1, 2, 0xD23A3A);
    }
    int wf = bus ? 12 : 7;
    for (int i = -1; i <= 1; i += 2) {
        int wx = x + i * wf - 1;
        BOX(wx - 1, y - 5, 5, 5, 0x1E1E24);
        BOX(wx + 1, y - 3, 1, 1, 0xB8BCC8);
        if ((int)(a->ph * 2) % 2) BOX(wx, y - 4, 1, 1, 0xB8BCC8);
    }
}

static void draw_actor(const City *s, SDL_Renderer *r, const Actor *a) {
    switch (a->role) {
    case R_WALKER: case R_SHADES: case R_STANDER:
        draw_person(s, r, a, a->state == S_WALK, 0);
        break;
    case R_DOGWALK: case R_BIGBUTT: {
        int moving = a->state == S_WALK, big = a->role == R_BIGBUTT;
        int d = a->dir;
        float dx = a->x + d * (big ? 17 : 14), dy = a->y + 1;
        int L = big ? 7 : (a->size == 0 ? 7 : a->size == 1 ? 9 : 11);
        draw_quad(s, r, dx, dy, d, a->ph * 1.6f, moving, a->state == S_SNIFF, big ? DOGC[5] : a->pet, L, big, 0, a->t);
        draw_person(s, r, a, moving, a->state == S_SNIFF ? 1 : 0);
        float hx = a->x + d * 3, hy = a->y - 7;
        thick_line(r, hx * s->px, hy * s->px, (dx + d * (L / 2 - 1)) * s->px, (dy - 7) * s->px, (float)s->px * 0.8f, (SDL_Color){ 200, 60, 60, 255 });
        break;
    }
    case R_BIKE: draw_bike(s, r, a); break;
    case R_CAR: case R_BUS: draw_car(s, r, a); break;
    case R_CATWALK: draw_quad(s, r, a->x, a->y, a->dir, a->ph, a->state == S_WALK, 0, a->pet, 6, 0, 1, a->t); break;
    case R_CATSIT: draw_sitting_cat(s, r, a); break;
    case R_PIGEON: draw_pigeon(s, r, a); break;
    }
}

static void draw_cloud(const City *s, SDL_Renderer *r, const Cloud *c) {
    int x = (int)c->x, y = (int)c->y;
    for (int i = 0; i < 4; i++) {
        int w = c->w - i * 4 - (i % 2) * 5, h = 5 - (i == 3 ? 3 : 0);
        box(s, r, x + i * 2 + (c->w - w) / 2, y + i * 2 - 4, w, h, 0xFFFFFF, 235);
    }
    box(s, r, x + 3, y + 5, c->w - 4, 2, 0xDCE8F4, 220);
}

static void draw_bird(const City *s, SDL_Renderer *r, const Bird *b) {
    int x = (int)b->x, y = (int)b->y;
    float f = sinf(b->ph);
    int big = b->size > 1.25f, w = big ? 4 : 3, up = (int)roundf(f * (big ? 3 : 2));
    uint32_t c = 0x26303E;
    box(s, r, x, y, 1, 1, c, 255);
    for (int i = 1; i <= w; i++) {
        int dy = (int)roundf(up * (float)i / w) * -1 + (i > w / 2 ? 0 : 0);
        box(s, r, x - i, y + dy + (f < 0 ? 0 : 0), 1, 1, c, 255);
        box(s, r, x + i, y + dy, 1, 1, c, 255);
    }
}

static void draw(void *p, SDL_Renderer *r) {
    City *s = p;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_Rect full = { 0, 0, s->W * s->px, s->H * s->px };
    SDL_RenderCopy(r, s->sky, NULL, &full);
    for (int i = 0; i < MAXCLOUD; i++) draw_cloud(s, r, &s->cloud[i]);
    for (int i = 0; i < s->nb; i++) draw_bird(s, r, &s->bird[i]);
    SDL_RenderCopy(r, s->far, NULL, &full);
    SDL_RenderCopy(r, s->near, NULL, &full);
    int order[MAXA];
    for (int i = 0; i < s->na; i++) order[i] = i;
    for (int i = 1; i < s->na; i++) {                      /* back to front */
        int k = order[i];
        float ky = s->a[k].role == R_CATSIT ? s->a[k].y - 1000 : s->a[k].y;
        int j = i - 1;
        while (j >= 0 && (s->a[order[j]].role == R_CATSIT ? s->a[order[j]].y - 1000 : s->a[order[j]].y) > ky) { order[j + 1] = order[j]; j--; }
        order[j + 1] = k;
    }
    for (int i = 0; i < s->na; i++) draw_actor(s, r, &s->a[order[i]]);
}

const Saver saver_city = { "city", "CITY", create, step, draw, destroy, NULL, NULL };
