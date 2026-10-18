/* BOOKS: a cozy wooden bookshelf in lamplight. Every few seconds a book slides out
   of its slot, flies to the middle, turns to show its cover and opens up. Sparkles
   float out and the page shows a little moving picture: the moon, a rainbow, a fish,
   a heart, a rocket or the Waffle Cat. Then it closes and flies home.
   (Idea by a six-year-old.) */
#include "../engine.h"
#include <math.h>
#include <stdlib.h>

#define MAXBOOKS 600
#define PARTS 160
#define MOTES 60
#define CAT_W 56
#define CAT_H 36
#define CAT_FRAMES 4

Canvas make_waffle_cat(int frame);   /* rainbow.c */

enum { REST, LIFT, FLYOUT, OPEN, SHOW, CLOSE, FLYBACK, DROP };
enum { PIC_MOON, PIC_RAINBOW, PIC_FISH, PIC_HEART, PIC_ROCKET, PIC_CAT, PICS };
enum { P_SPARKLE, P_STAR, P_DOT };

static const uint32_t SPINES[10] = { 0x7A1F2B, 0x1F3A68, 0x2D5A34, 0xC9A227, 0x1F7A7A,
                                     0x6B2D5C, 0x6B4226, 0xC8642A, 0x9E2F2A, 0x3E5F8F };
static const uint32_t BANDS[6] = { 0xFF4D4D, 0xFF9A3C, 0xFFE14D, 0x5BD46B, 0x4DA6FF, 0x9B6BFF };
static const uint32_t SPARKS[6] = { 0xFFE27A, 0xFFFFFF, 0xFF9EC8, 0x9ED8FF, 0xA8F0C0, 0xC9A8FF };
static const char DROPCAP[PICS] = { 'M', 'R', 'F', 'H', 'R', 'C' };

#define GOLD 0xE2B84A
#define GOLD_HI 0xF7DE8E
#define GOLD_LO 0x9A7424
#define PAPER 0xF4E8C8
#define INK 0x6E5C45

typedef struct { int x, y, w, h, col; unsigned seed; } Book;   /* art pixels, y = top */
typedef struct { float x, y, vx, vy, life, max, size, rot, ph; int kind; SDL_Color c; } Part;
typedef struct { float x, y, vx, vy, ph; } Mote;

typedef struct {
    int w, h, px, W, H;                  /* screen size, size of one art pixel, art size */
    int cw, ch, pw, ph, picw, pich, picx, picy;
    float t, stateT, dur, picT, spawn;
    float lampX, lampY, lampR, glowX, glowY;
    int hasLamp, lampW, lampH, lampFloor, needTex;
    int state, cur, pic, lastPic, picFrame;
    Book books[MAXBOOKS];
    int nbooks;
    SDL_Texture *shelf, *spine, *cover, *left, *right, *picTex, *cat[CAT_FRAMES];
    Canvas art;
    Part parts[PARTS];
    Mote motes[MOTES];
} Books;

/* ---------- little helpers ---------- */

static float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static float lerp(float a, float b, float t) { return a + (b - a) * t; }
static float ease(float k) { k = clampf(k, 0, 1); return k * k * (3 - 2 * k); }

static uint32_t shade(uint32_t hex, float f) {
    int r = (int)(((hex >> 16) & 255) * f), g = (int)(((hex >> 8) & 255) * f), b = (int)((hex & 255) * f);
    r = r > 255 ? 255 : r; g = g > 255 ? 255 : g; b = b > 255 ? 255 : b;
    return (uint32_t)(r << 16 | g << 8 | b);
}

static uint32_t mixc(uint32_t a, uint32_t b, float t) {
    int r = (int)lerp((float)((a >> 16) & 255), (float)((b >> 16) & 255), t);
    int g = (int)lerp((float)((a >> 8) & 255), (float)((b >> 8) & 255), t);
    int bl = (int)lerp((float)(a & 255), (float)(b & 255), t);
    return (uint32_t)(r << 16 | g << 8 | bl);
}

static void mul_px(Canvas *c, int x, int y, float f) {
    uint32_t p = get(c, x, y);
    if (p) put(c, x, y, rgb(shade(p >> 8, f)));
}

static void tint_px(Canvas *c, int x, int y, uint32_t to, float t) {
    uint32_t p = get(c, x, y);
    if (p) put(c, x, y, rgb(mixc(p >> 8, to, t)));
}

static unsigned lcg(unsigned *s) { *s = *s * 1103515245u + 12345u; return (*s >> 16) & 0x7FFF; }

static void blit(Canvas *dst, const Canvas *src, int x, int y, int flip) {
    for (int j = 0; j < src->h; j++)
        for (int i = 0; i < src->w; i++) {
            uint32_t p = src->px[j * src->w + (flip ? src->w - 1 - i : i)];
            if (p) put(dst, x + i, y + j, p);
        }
}

static void hline(Canvas *c, int x0, int x1, int y, uint32_t hex) { fill_rect(c, x0, y, x1, y, rgb(hex)); }

static void gradient(Canvas *c, uint32_t top, uint32_t bot) {
    for (int y = 0; y < c->h; y++) hline(c, 0, c->w - 1, y, mixc(top, bot, (float)y / (c->h - 1)));
}

/* A leaf shape from (px,py) pointing at angle a. */
static void leaf(Canvas *c, float px, float py, float a, float len, float hw, uint32_t hex) {
    float dx = cosf(a), dy = sinf(a), nx = -dy, ny = dx;
    float xs[4] = { px, px + dx * len * 0.5f + nx * hw, px + dx * len, px + dx * len * 0.5f - nx * hw };
    float ys[4] = { py, py + dy * len * 0.5f + ny * hw, py + dy * len, py + dy * len * 0.5f - ny * hw };
    fill_poly(c, xs, ys, 4, rgb(hex));
}

/* ---------- book spines ---------- */

static void gold_band(Canvas *c, int x, int y, int w) {
    for (int i = 0; i < w; i++)
        put(c, x + i, y, rgb(i == 0 ? GOLD_HI : (i >= w - 2 ? GOLD_LO : GOLD)));
}

static void draw_spine(Canvas *c, int x, int y, int w, int h, uint32_t col, unsigned seed) {
    unsigned s = seed;
    fill_rect(c, x, y, x + w - 1, y + h - 1, rgb(col));
    for (int j = y; j < y + h; j++) {
        put(c, x, j, rgb(shade(col, 1.3f)));
        if (w > 5) put(c, x + 1, j, rgb(shade(col, 1.12f)));
        put(c, x + w - 2, j, rgb(shade(col, 0.8f)));
        put(c, x + w - 1, j, rgb(shade(col, 0.55f)));
    }
    hline(c, x, x + w - 2, y, shade(col, 1.2f));
    int bands = (int)(lcg(&s) % 3), title = (int)(lcg(&s) % 3);
    if (h >= 14) {
        if (bands >= 1) { gold_band(c, x, y + 2, w); gold_band(c, x, y + h - 3, w); }
        if (bands == 2 && h >= 20) { gold_band(c, x, y + 4, w); gold_band(c, x, y + h - 5, w); }
    }
    int t0 = y + h * 3 / 10, t1 = y + h * 6 / 10, cx = x + (w - 1) / 2;
    if (title == 0) {                                  /* gold letters running down */
        for (int j = t0; j < t1; j++) {
            if ((j - t0) % 3 == 2 || lcg(&s) % 6 == 0) continue;
            put(c, cx, j, rgb(GOLD));
            if (w >= 8) put(c, cx + 1, j, rgb(GOLD_LO));
        }
    } else if (title == 1 && w >= 6) {                 /* a cream paper label */
        int l1 = y + h / 2;
        fill_rect(c, x + 2, t0, x + w - 3, l1, rgb(0xEAD9B0));
        for (int j = t0 + 1; j < l1; j += 2) hline(c, x + 3, x + w - 4, j, 0x8A7558);
        hline(c, x + 2, x + w - 3, l1, 0xB8A57E);
    } else {                                           /* a dark band with gold dots */
        int l1 = y + h * 45 / 100;
        fill_rect(c, x, t0, x + w - 1, l1, rgb(shade(col, 0.55f)));
        put(c, x, t0, rgb(shade(col, 0.8f)));
        for (int j = t0 + 1; j < l1; j += 2) put(c, cx, j, rgb(GOLD));
    }
    if (h >= 22 && lcg(&s) % 2) {                      /* a little emblem near the bottom */
        put(c, cx, y + h - 8, rgb(GOLD));
        put(c, cx, y + h - 9, rgb(GOLD_HI));
    }
}

/* ---------- shelf decorations (drawn with a 1-pixel margin for the outline) ---------- */

static Canvas make_lamp(int w, int h) {
    Canvas c = canvas_new(w + 2, h + 2);
    float cx = w / 2.0f + 1;
    int top = 1, bot = top + (int)(h * 0.42f), icx = (int)cx;
    fill_rect(&c, icx - 1, bot - 2, icx, h - 3, rgb(0xC8A048));
    fill_rect(&c, icx - 1, bot - 2, icx - 1, h - 3, rgb(0xEED080));
    fill_ellipse(&c, cx, (bot + h) / 2.0f, 2, 1.6f, rgb(0xC8A048));
    fill_round_rect(&c, (int)(cx - w * 0.3f), h - 3, (int)(cx + w * 0.3f), h, 1, rgb(0xB88E38));
    hline(&c, (int)(cx - w * 0.3f) + 1, (int)(cx + w * 0.3f) - 1, h - 3, 0xF0D488);
    fill_ellipse(&c, cx, (float)bot + 0.5f, w * 0.17f, 1.6f, rgb(0xFFF8E0));
    float xs[4] = { cx - w * 0.25f, cx + w * 0.25f, cx + w * 0.49f, cx - w * 0.49f };
    float ys[4] = { (float)top, (float)top, (float)bot, (float)bot };
    fill_poly(&c, xs, ys, 4, rgb(0xFFD58A));
    for (int y = top; y < bot; y++)
        for (int x = 0; x < c.w; x++) {
            if (get(&c, x, y) != rgb(0xFFD58A)) continue;
            uint32_t col = mixc(0xF0A848, 0xFFE6A8, (float)(y - top) / (bot - top));
            if ((x + 300 - icx) % 3 == 0) col = shade(col, 0.94f);
            put(&c, x, y, rgb(col));
        }
    for (int x = 0; x < c.w; x++) {
        if (get(&c, x, top)) put(&c, x, top, rgb(0xC98638));
        if (get(&c, x, bot - 1)) put(&c, x, bot - 1, rgb(0xE6A24C));
    }
    outline(&c, rgb(0x2A170A));
    return c;
}

static Canvas make_plant(int w, int h) {
    Canvas c = canvas_new(w + 2, h + 2);
    float cx = w / 2.0f + 1;
    int potTop = h - (int)(h * 0.38f);
    static const uint32_t greens[3] = { 0x3F8A42, 0x5DAE52, 0x2F6E36 };
    for (int i = 0; i < 9; i++) {
        float a = -PI_F * (0.12f + 0.76f * i / 8.0f) + frand(-0.1f, 0.1f);
        float len = h * (i % 2 ? 0.48f : 0.6f) * frand(0.85f, 1.05f);
        leaf(&c, cx, (float)potTop + 1, a, len, len * 0.2f, greens[i % 3]);
    }
    float xs[4] = { cx - w * 0.32f, cx + w * 0.32f, cx + w * 0.22f, cx - w * 0.22f };
    float ys[4] = { (float)potTop, (float)potTop, (float)h + 1, (float)h + 1 };
    fill_poly(&c, xs, ys, 4, rgb(0xC0603A));
    for (int y = potTop; y <= h; y++)
        for (int x = (int)cx; x < c.w; x++)
            if (get(&c, x, y) == rgb(0xC0603A) && x > cx + w * 0.12f) put(&c, x, y, rgb(0x9C4A2C));
    fill_rect(&c, (int)(cx - w * 0.36f), potTop, (int)(cx + w * 0.36f), potTop + 1, rgb(0xD9784E));
    hline(&c, (int)(cx - w * 0.36f), (int)(cx + w * 0.36f), potTop, 0xEE9A70);
    outline(&c, rgb(0x1E1408));
    return c;
}

static Canvas make_duck(int w, int h) {
    Canvas c = canvas_new(w + 2, h + 2);
    float bx = w * 0.45f + 1, by = h * 0.68f + 1;
    fill_poly(&c, (float[]){ 1, bx - w * 0.1f, bx }, (float[]){ by - h * 0.4f, by - h * 0.2f, by + h * 0.1f }, 3, rgb(0xFFD23F));
    fill_ellipse(&c, bx, by, w * 0.42f, h * 0.3f, rgb(0xFFD23F));
    fill_ellipse(&c, bx + w * 0.02f, by + h * 0.12f, w * 0.38f, h * 0.15f, rgb(0xF2B92C));
    float hx = w * 0.66f + 1, hy = h * 0.34f + 1, hr = w * 0.2f;
    fill_ellipse(&c, hx, hy, hr, hr, rgb(0xFFD23F));
    fill_poly(&c, (float[]){ hx + hr * 0.6f, (float)w + 1, hx + hr * 0.6f },
              (float[]){ hy - hr * 0.1f, hy + hr * 0.25f, hy + hr * 0.6f }, 3, rgb(0xFF8C1A));
    fill_ellipse(&c, bx - w * 0.05f, by - h * 0.05f, w * 0.18f, h * 0.12f, rgb(0xF2B92C));
    put(&c, (int)(hx + hr * 0.25f), (int)(hy - hr * 0.3f), rgb(0x1A1A1A));
    put(&c, (int)(hx + hr * 0.25f), (int)(hy - hr * 0.3f) - 1, rgb(0xFFFFFF));
    for (int x = 0; x < c.w; x++)                      /* a little highlight */
        for (int y = 0; y < c.h; y++)
            if (get(&c, x, y) == rgb(0xFFD23F) && !get(&c, x, y - 1)) put(&c, x, y, rgb(0xFFEC8C));
    outline(&c, rgb(0x5A3A08));
    return c;
}

/* ---------- the shelf (built once, at art resolution) ---------- */

static float glow_at(const Books *b, float x, float y) {
    float dx = (x - b->glowX) / (b->H * 0.8f), dy = (y - b->glowY) / (b->H * 0.6f);
    float lx = (x - b->lampX) / b->lampR, ly = (y - b->lampY) / b->lampR;
    return 0.5f + 0.62f * expf(-(dx * dx + dy * dy)) + 0.35f * expf(-(lx * lx + ly * ly));
}

static void wood_grain(Canvas *c, int x0, int y0, int x1, int y1, uint32_t base, int vertical) {
    fill_rect(c, x0, y0, x1, y1, rgb(base));
    int n = vertical ? x1 - x0 + 1 : y1 - y0 + 1, len = vertical ? y1 - y0 + 1 : x1 - x0 + 1;
    for (int k = 0; k < n; k++)
        for (int p = irand(12); p < len; p += 6 + irand(26)) {
            int run = 4 + irand(18);
            uint32_t col = shade(base, irand(3) ? 0.86f : 1.1f);
            for (int q = p; q < p + run && q < len; q++)
                if (vertical) put(c, x0 + k, y0 + q, rgb(col));
                else put(c, x0 + q, y0 + k, rgb(col));
            p += run;
        }
}

static void plank(Canvas *c, int x0, int x1, int y0, int y1) {
    wood_grain(c, x0, y0, x1, y1, 0x93602F, 0);
    hline(c, x0, x1, y0, 0xE0A86C);
    if (y1 - y0 >= 3) hline(c, x0, x1, y0 + 1, 0xB57C45);
    hline(c, x0, x1, y1, 0x5A361A);
    if (x1 - x0 > 30 && y1 - y0 >= 5 && irand(2)) {   /* a knot */
        float kx = (float)(x0 + 10 + irand(x1 - x0 - 20)), ky = (y0 + y1) / 2.0f + 0.5f;
        fill_ellipse(c, kx, ky, 2.5f, 1.2f, rgb(0x6E4422));
        put(c, (int)kx, (int)ky, rgb(0x4E2E14));
    }
}

static void post(Canvas *c, int x0, int x1, int y0, int y1) {
    wood_grain(c, x0, y0, x1, y1, 0x8E5C2E, 1);
    fill_rect(c, x0, y0, x0, y1, rgb(0xC08A50));
    fill_rect(c, x1, y0, x1, y1, rgb(0x56341A));
}

typedef struct { int kind, x; } Deco;   /* kind: 0 none, 1 lamp, 2 plant, 3 duck */

static void build_shelf(Books *b, Canvas *c) {
    int W = b->W, H = b->H;
    int R = H >= 120 ? 4 : 3;
    int crown = H * 6 / 100 > 3 ? H * 6 / 100 : 3, base = H * 7 / 100 > 4 ? H * 7 / 100 : 4;
    float rowH = (float)(H - crown - base) / R;
    int pt = (int)(rowH * 0.17f) > 3 ? (int)(rowH * 0.17f) : 3;
    int sp = H * 35 / 1000 > 3 ? H * 35 / 1000 : 3;
    int B = (int)(W / 125.0f + 0.5f);
    if (B < 1) B = 1;
    if (B > 8) B = 8;
    float bayW = (float)(W - (B + 1) * sp) / B;

    /* back panel: vertical boards */
    for (int x = 0; x < W;) {
        int bw = 12 + irand(8);
        uint32_t col = shade(0x5C3A22, frand(0.9f, 1.06f));
        wood_grain(c, x, 0, x + bw - 1, H - 1, col, 1);
        fill_rect(c, x + bw - 1, 0, x + bw - 1, H - 1, rgb(0x2E1A0E));
        x += bw;
    }

    /* where the lamp and friends go */
    Deco deco[4][8] = { { { 0, 0 } } };
    for (int r = 0; r < R; r++)
        for (int bb = 0; bb < B; bb++) deco[r][bb].x = (int)(bayW * frand(0.2f, 0.7f));
    b->lampW = (int)((rowH - pt) * 0.62f) | 1;
    deco[0][(B - 1) / 2] = (Deco){ 1, (int)(bayW * 0.5f) - b->lampW / 2 + irand(7) - 3 };
    for (int k = 2; k <= 3; k++) {
        int r = 1 + irand(R - 1), bb = irand(B);
        if (!deco[r][bb].kind) deco[r][bb].kind = k;
    }

    b->nbooks = 0;
    for (int r = 0; r < R; r++) {
        int y0 = crown + (int)(r * rowH), y1 = crown + (int)((r + 1) * rowH);
        int floorY = y1 - pt, ch = floorY - y0;
        for (int bb = 0; bb < B; bb++) {
            int x0 = sp + (int)(bb * (bayW + sp)), x1 = sp + (int)(bb * (bayW + sp) + bayW);
            Deco *d = &deco[r][bb];
            int x = x0 + irand(3), placed = 0;
            while (x < x1 - 3) {
                if (d->kind && !placed && x >= x0 + d->x && ch >= 14) {
                    int dw, dh;
                    Canvas dc = { 0, 0, NULL };
                    placed = 1;
                    if (d->kind == 1) { dw = b->lampW; dh = (int)(ch * 0.9f); }
                    else if (d->kind == 2) { dw = (int)(ch * 0.62f); dh = (int)(ch * 0.78f); dc = make_plant(dw, dh); }
                    else { dw = (int)(ch * 0.5f); dh = (int)(ch * 0.42f); dc = make_duck(dw, dh); }
                    if (x + dw + 4 <= x1) {
                        x += 2;
                        if (d->kind == 1) {                /* the lamp is drawn after the light pass */
                            b->hasLamp = 1;
                            b->lampH = dh;
                            b->lampFloor = floorY;
                            b->lampX = x + dw / 2.0f;
                            b->lampY = floorY - dh * 0.75f;
                            b->lampR = ch * 1.1f;
                            b->glowX = b->lampX;
                            b->glowY = floorY + rowH * 0.4f;
                        } else {
                            blit(c, &dc, x - 1, floorY - dh - 1, irand(2));
                        }
                        x += dw + 2;
                    }
                    free(dc.px);
                    continue;
                }
                int r1 = irand(100);
                if (r1 < 4) { x += 2 + irand(4); continue; }  /* a little space */
                if (r1 < 9 && ch >= 16 && x + 18 < x1 && x > x0 + 6) { /* books lying flat */
                    int n = 2 + irand(2), yy = floorY;
                    for (int k = 0; k < n; k++) {
                        int fw = 13 + irand(5), fh = 3 + irand(2), fx = x + irand(3);
                        uint32_t col = SPINES[irand(10)];
                        yy -= fh;
                        fill_rect(c, fx, yy, fx + fw - 1, yy + fh - 1, rgb(col));
                        hline(c, fx, fx + fw - 1, yy, shade(col, 1.25f));
                        hline(c, fx, fx + fw - 1, yy + fh - 1, shade(col, 0.6f));
                        fill_rect(c, fx + fw - 3, yy + 1, fx + fw - 2, yy + fh - 2, rgb(0xEADCB8));
                        fill_rect(c, fx + 3, yy + 1, fx + 3, yy + fh - 2, rgb(GOLD));
                    }
                    x += 21;
                    continue;
                }
                int bw = r1 < 20 ? 9 + irand(4) : 5 + irand(5);
                if (x + bw > x1) { if (x1 - x >= 4) bw = x1 - x; else break; }
                if (d->kind && !placed && x + bw > x0 + d->x) d->x = x - x0 + bw;  /* keep its spot */
                int bh = (int)(ch * frand(0.62f, 0.93f));
                if (bh < 6) bh = 6;
                if (b->nbooks < MAXBOOKS) {
                    Book *bk = &b->books[b->nbooks++];
                    *bk = (Book){ x, floorY - bh, bw, bh, irand(10), (unsigned)rand() };
                    draw_spine(c, bk->x, bk->y, bk->w, bk->h, SPINES[bk->col], bk->seed);
                }
                x += bw;
            }
            /* soft shadows under the shelf above and beside the uprights */
            static const float under[4] = { 0.5f, 0.66f, 0.8f, 0.92f };
            static const float side[3] = { 0.62f, 0.8f, 0.92f };
            for (int i = 0; i < 4; i++)
                for (int xx = x0; xx < x1; xx++) mul_px(c, xx, y0 + i, under[i]);
            for (int i = 0; i < 3; i++)
                for (int yy = y0; yy < floorY; yy++) mul_px(c, x0 + i, yy, side[i]);
            for (int yy = y0; yy < floorY; yy++) mul_px(c, x1 - 1, yy, 0.82f);
            for (int xx = x0; xx < x1; xx++) mul_px(c, xx, floorY - 1, 0.82f);
            plank(c, x0, x1 - 1, floorY, y1 - 1);
        }
    }
    /* uprights, crown and base */
    for (int bb = 0; bb <= B; bb++) {
        int x0 = bb == B ? W - sp : (int)(bb * (bayW + sp));
        post(c, x0, bb == B ? W - 1 : x0 + sp - 1, crown, H - base);
    }
    wood_grain(c, 0, 0, W - 1, crown - 1, 0x8A5A2E, 0);
    hline(c, 0, W - 1, 0, 0x3A2212);
    hline(c, 0, W - 1, 1, 0xB07840);
    if (crown >= 6)
        for (int x = 0; x < W; x += 4) fill_rect(c, x, crown - 3, x + 1, crown - 2, rgb(0x6E4422));
    hline(c, 0, W - 1, crown - 1, 0x3E2412);
    wood_grain(c, 0, H - base, W - 1, H - 1, 0x7A4C26, 0);
    hline(c, 0, W - 1, H - base, 0xC08850);
    hline(c, 0, W - 1, H - base + 1, 0x9A6634);
    if (base >= 6) hline(c, sp, W - sp, H - base + base / 2, 0x55331A);

    /* warm lamplight over everything */
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            uint32_t p = c->px[y * W + x] >> 8;
            float f = glow_at(b, x + 0.5f, y + 0.5f);
            int r = (int)(((p >> 16) & 255) * f * 1.05f), g = (int)(((p >> 8) & 255) * f * 0.97f), bl = (int)((p & 255) * f * 0.82f);
            r = r > 255 ? 255 : r; g = g > 255 ? 255 : g; bl = bl > 255 ? 255 : bl;
            c->px[y * W + x] = rgb((uint32_t)(r << 16 | g << 8 | bl));
        }
    /* and the lamp itself, which glows on its own */
    if (b->hasLamp) {
        Canvas lamp = make_lamp(b->lampW, b->lampH);
        blit(c, &lamp, (int)(b->lampX - b->lampW / 2.0f) - 1, b->lampFloor - b->lampH - 1, 0);
        free(lamp.px);
    }
}

/* ---------- the book's cover and pages ---------- */

static void emblem(Canvas *c, float cx, float cy, float r, int pic, uint32_t bg) {
    uint32_t g = rgb(GOLD), k = rgb(bg);
    switch (pic) {
    case PIC_MOON:
        fill_ellipse(c, cx - r * 0.1f, cy, r * 0.8f, r * 0.8f, g);
        fill_ellipse(c, cx + r * 0.3f, cy - r * 0.25f, r * 0.68f, r * 0.68f, k);
        put(c, (int)(cx + r * 0.55f), (int)(cy + r * 0.45f), g);
        break;
    case PIC_RAINBOW:
        for (int y = (int)(cy - r); y <= (int)(cy + r * 0.5f); y++)
            for (int x = (int)(cx - r); x <= (int)(cx + r); x++) {
                float d = hypotf(x + 0.5f - cx, y + 0.5f - (cy + r * 0.5f)) / r;
                if ((d > 0.78f && d < 0.98f) || (d > 0.44f && d < 0.62f)) put(c, x, y, g);
            }
        break;
    case PIC_FISH:
        fill_ellipse(c, cx + r * 0.12f, cy, r * 0.6f, r * 0.38f, g);
        fill_poly(c, (float[]){ cx - r * 0.35f, cx - r * 0.85f, cx - r * 0.85f },
                  (float[]){ cy, cy - r * 0.4f, cy + r * 0.4f }, 3, g);
        put(c, (int)(cx + r * 0.4f), (int)(cy - r * 0.1f), k);
        break;
    case PIC_HEART:
        fill_ellipse(c, cx - r * 0.35f, cy - r * 0.2f, r * 0.4f, r * 0.4f, g);
        fill_ellipse(c, cx + r * 0.35f, cy - r * 0.2f, r * 0.4f, r * 0.4f, g);
        fill_poly(c, (float[]){ cx - r * 0.75f, cx + r * 0.75f, cx }, (float[]){ cy - r * 0.05f, cy - r * 0.05f, cy + r * 0.8f }, 3, g);
        break;
    case PIC_ROCKET:
        fill_ellipse(c, cx, cy - r * 0.1f, r * 0.3f, r * 0.75f, g);
        fill_poly(c, (float[]){ cx - r * 0.25f, cx - r * 0.6f, cx - r * 0.25f }, (float[]){ cy + r * 0.1f, cy + r * 0.75f, cy + r * 0.5f }, 3, g);
        fill_poly(c, (float[]){ cx + r * 0.25f, cx + r * 0.6f, cx + r * 0.25f }, (float[]){ cy + r * 0.1f, cy + r * 0.75f, cy + r * 0.5f }, 3, g);
        fill_ellipse(c, cx, cy - r * 0.2f, r * 0.13f, r * 0.13f, k);
        break;
    default:
        fill_ellipse(c, cx, cy + r * 0.15f, r * 0.62f, r * 0.5f, g);
        fill_poly(c, (float[]){ cx - r * 0.6f, cx - r * 0.5f, cx - r * 0.15f }, (float[]){ cy, cy - r * 0.75f, cy - r * 0.25f }, 3, g);
        fill_poly(c, (float[]){ cx + r * 0.6f, cx + r * 0.5f, cx + r * 0.15f }, (float[]){ cy, cy - r * 0.75f, cy - r * 0.25f }, 3, g);
        put(c, (int)(cx - r * 0.25f), (int)(cy + r * 0.1f), k);
        put(c, (int)(cx + r * 0.25f), (int)(cy + r * 0.1f), k);
        break;
    }
}

/* A row of make-believe gold letters, centred on cx. */
static void title_row(Canvas *c, int cx, int y, int maxw, unsigned *s, uint32_t hex, uint32_t bg) {
    int lens[24], n = 0, total = 0;
    while (n < 24) {
        int l = (int)(lcg(s) % 3) + 2;
        int add = l * 3 + (n ? 3 : 0);
        if (total + add > maxw) break;
        lens[n++] = l;
        total += add;
    }
    int x = cx - total / 2;
    for (int i = 0; i < n; i++) {
        if (i) x += 3;
        for (int k = 0; k < lens[i]; k++, x += 3) {
            fill_rect(c, x, y, x + 1, y + 2, rgb(hex));
            if (lcg(s) % 3 == 0) put(c, x + (int)(lcg(s) % 2), y + 1, rgb(bg));
        }
    }
}

static Canvas make_cover(const Books *b, const Book *bk, int pic) {
    int W = b->cw, H = b->ch;
    uint32_t col = SPINES[bk->col], deep = shade(col, 0.68f);
    unsigned s = bk->seed;
    Canvas c = canvas_new(W, H);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) put(&c, x, y, rgb((x + y) & 1 ? shade(col, 0.95f) : col));
    fill_rect(&c, 2, 0, 2, H - 1, rgb(shade(col, 0.7f)));
    fill_rect(&c, 3, 0, 3, H - 1, rgb(shade(col, 1.15f)));
    int fx0 = 6, fy0 = 5, fx1 = W - 6, fy1 = H - 6;
    hline(&c, fx0, fx1, fy0, GOLD); hline(&c, fx0, fx1, fy1, GOLD);
    fill_rect(&c, fx0, fy0, fx0, fy1, rgb(GOLD)); fill_rect(&c, fx1, fy0, fx1, fy1, rgb(GOLD));
    hline(&c, fx0 + 1, fx1 + 1, fy1 + 1, shade(col, 0.55f));
    fill_rect(&c, fx1 + 1, fy0 + 1, fx1 + 1, fy1 + 1, rgb(shade(col, 0.55f)));
    int cs[4][2] = { { fx0 + 2, fy0 + 2 }, { fx1 - 2, fy0 + 2 }, { fx0 + 2, fy1 - 2 }, { fx1 - 2, fy1 - 2 } };
    for (int i = 0; i < 4; i++) {
        put(&c, cs[i][0], cs[i][1], rgb(GOLD_HI));
        put(&c, cs[i][0] - 1, cs[i][1], rgb(GOLD)); put(&c, cs[i][0] + 1, cs[i][1], rgb(GOLD));
        put(&c, cs[i][0], cs[i][1] - 1, rgb(GOLD)); put(&c, cs[i][0], cs[i][1] + 1, rgb(GOLD));
    }
    int mid = (fx0 + fx1) / 2;
    title_row(&c, mid, fy0 + H / 9, fx1 - fx0 - 8, &s, GOLD_HI, col);
    title_row(&c, mid, fy0 + H / 9 + 5, (fx1 - fx0) * 6 / 10, &s, GOLD, col);
    float mcx = mid + 0.5f, mcy = H * 0.58f, mr = W * 0.22f;
    fill_ellipse(&c, mcx, mcy, mr + 1, mr + 1, rgb(GOLD_LO));
    fill_ellipse(&c, mcx, mcy, mr, mr, rgb(GOLD));
    fill_ellipse(&c, mcx, mcy, mr - 1.5f, mr - 1.5f, rgb(deep));
    emblem(&c, mcx, mcy, mr - 3, pic, deep);
    hline(&c, mid - 5, mid + 5, (int)(mcy + mr) + 5, GOLD);
    put(&c, mid, (int)(mcy + mr) + 6, rgb(GOLD));
    hline(&c, 0, W - 1, 0, shade(col, 1.25f));
    hline(&c, 0, W - 1, H - 1, shade(col, 0.45f));
    fill_rect(&c, W - 1, 0, W - 1, H - 1, rgb(shade(col, 0.45f)));
    fill_rect(&c, 0, 0, 0, H - 1, rgb(shade(col, 0.5f)));
    return c;
}

static void text_line(Canvas *c, int x0, int x1, int y, unsigned *s) {
    for (int x = x0; x < x1;) {
        int l = 2 + (int)(lcg(s) % 6);
        if (x + l > x1) l = x1 - x;
        hline(c, x, x + l - 1, y, lcg(s) % 4 ? INK : 0x8A765A);
        x += l + 2;
    }
}

static void paper(Canvas *c, int left) {
    int W = c->w, H = c->h;
    fill_rect(c, 0, 0, W - 1, H - 1, rgb(PAPER));
    for (int x = 0; x < W; x++) { mul_px(c, x, 0, 0.94f); mul_px(c, x, H - 1, 0.92f); }
    for (int y = 0; y < H; y++) mul_px(c, left ? 0 : W - 1, y, 0.92f);
}

static void gutter(Canvas *c, int left) {
    static const float g[5] = { 0.7f, 0.8f, 0.88f, 0.94f, 0.97f };
    for (int i = 0; i < 5; i++)
        for (int y = 0; y < c->h; y++) mul_px(c, left ? c->w - 1 - i : i, y, g[i]);
}

static Canvas make_left_page(const Books *b, const Book *bk, int pic) {
    Canvas c = canvas_new(b->pw, b->ph);
    unsigned s = bk->seed * 7 + 3;
    paper(&c, 1);
    int x0 = 5, x1 = b->pw - 6;
    uint32_t col = SPINES[bk->col];
    fill_rect(&c, x0, 5, x0 + 10, 16, rgb(col));
    hline(&c, x0, x0 + 10, 5, GOLD); hline(&c, x0, x0 + 10, 16, GOLD);
    fill_rect(&c, x0, 5, x0, 16, rgb(GOLD)); fill_rect(&c, x0 + 10, 5, x0 + 10, 16, rgb(GOLD));
    const char *const *gl = font_glyph(DROPCAP[pic]);
    for (int j = 0; gl && j < FONT_H; j++)
        for (int i = 0; i < FONT_W; i++)
            if (gl[j][i] == '#') put(&c, x0 + 3 + i, 7 + j, rgb(0xFFF4D8));
    int line = 0, para = 6 + (int)(lcg(&s) % 4), indent = 0;
    for (int y = 6; y < b->ph - 9; y += 3, line++) {
        int start = (y <= 17 ? x0 + 13 : x0) + indent, end = x1;
        indent = 0;
        if (line == para) {                            /* end of a paragraph */
            end = x0 + (x1 - x0) * (40 + (int)(lcg(&s) % 30)) / 100;
            para += 7 + (int)(lcg(&s) % 4);
            indent = 4;
            text_line(&c, start, end, y, &s);
            y += 2;
            continue;
        }
        text_line(&c, start, end, y, &s);
    }
    hline(&c, b->pw / 2 - 1, b->pw / 2 + 1, b->ph - 4, 0x9A8668);
    gutter(&c, 1);
    return c;
}

static Canvas make_right_page(const Books *b, const Book *bk) {
    Canvas c = canvas_new(b->pw, b->ph);
    unsigned s = bk->seed * 13 + 5;
    paper(&c, 0);
    int x0 = b->picx - 1, y0 = b->picy - 1, x1 = b->picx + b->picw, y1 = b->picy + b->pich;
    fill_rect(&c, x0 - 1, y0 - 1, x1 + 1, y1 + 1, rgb(0xE4D2A8));
    fill_rect(&c, x0, y0, x1, y1, rgb(0x7A5A3A));
    put(&c, x0 - 1, y0 - 1, rgb(GOLD)); put(&c, x1 + 1, y0 - 1, rgb(GOLD));
    put(&c, x0 - 1, y1 + 1, rgb(GOLD)); put(&c, x1 + 1, y1 + 1, rgb(GOLD));
    int mid = (x0 + x1) / 2, line = 0;
    for (int y = y1 + 5; y < b->ph - 8; y += 3, line++) {
        int half = (x1 - x0) * (line % 2 ? 30 : 42) / 100;
        text_line(&c, mid - half, mid + half, y, &s);
    }
    hline(&c, b->pw / 2 - 1, b->pw / 2 + 1, b->ph - 4, 0x9A8668);
    gutter(&c, 0);
    return c;
}

/* ---------- the little moving pictures ---------- */

static void twinkles(Canvas *c, float t, int n, int maxY) {
    for (int i = 0; i < n; i++) {
        int x = (i * 41 + 7) % c->w, y = (i * 29 + 3) % (maxY > 1 ? maxY : 1);
        float tw = 0.5f + 0.5f * sinf(t * 3 + i * 1.7f);
        put(c, x, y, rgb(mixc(0x8890C0, 0xFFF6D0, tw)));
        if (tw > 0.85f && i % 3 == 0) {
            put(c, x - 1, y, rgb(0xC8C0A0)); put(c, x + 1, y, rgb(0xC8C0A0));
            put(c, x, y - 1, rgb(0xC8C0A0)); put(c, x, y + 1, rgb(0xC8C0A0));
        }
    }
}

static void cat_pos(const Books *b, float t, float *x, float *y, float *s) {
    *s = b->picw >= CAT_W + 6 ? 1.0f : (b->picw - 6) / (float)CAT_W;
    *x = (b->picw - CAT_W * *s) / 2 + 3;
    *y = (b->pich - CAT_H * *s) / 2 + sinf(t * 3) * 1.5f;
}

static void pic_moon(Canvas *c, float t) {
    int W = c->w, H = c->h;
    gradient(c, 0x182658, 0x3B3F86);
    twinkles(c, t, 16, H * 7 / 10);
    float ph = fmodf(t, 4.5f);
    if (ph < 0.7f) {                                   /* a shooting star */
        float k = ph / 0.7f, sx = W * (0.05f + 0.5f * k), sy = 3 + H * 0.25f * k;
        for (int i = 0; i < 6; i++) put(c, (int)sx - i, (int)(sy - i * 0.5f), rgb(mixc(0xFFFFFF, 0x3B3F86, i / 6.0f)));
    }
    for (int x = 0; x < W; x++) {
        int hy = H - 5 - (int)(2.5f * sinf(x * 0.12f) + 1.5f * sinf(x * 0.31f + 1));
        fill_rect(c, x, hy, x, H - 1, rgb(0x1A2148));
        put(c, x, hy, rgb(0x2E3A72));
    }
    float R = H * 0.28f, mx = W * 0.58f, my = H * 0.42f + sinf(t * 1.3f) * 1.2f;
    float kx = mx + R * 0.5f, ky = my - R * 0.3f, kr = R * 0.8f;
    for (int y = (int)(my - R) - 1; y <= (int)(my + R) + 1; y++)
        for (int x = (int)(mx - R) - 1; x <= (int)(mx + R) + 1; x++) {
            float d = hypotf(x + 0.5f - mx, y + 0.5f - my), e = hypotf(x + 0.5f - kx, y + 0.5f - ky);
            if (d > R || e < kr) continue;
            put(c, x, y, rgb(d > R - 1.3f || e < kr + 1.2f ? 0xF2C94C : 0xFFE27A));
        }
    int ex = (int)(mx - R * 0.5f), ey = (int)(my - R * 0.05f);
    put(c, ex - 1, ey, rgb(0x8A6420)); put(c, ex, ey + 1, rgb(0x8A6420)); put(c, ex + 1, ey, rgb(0x8A6420));
    put(c, ex - 1, ey + 3, rgb(0xFF9E9E));
    int sx = (int)(mx - R * 0.25f), sy = (int)(my + R * 0.45f);
    put(c, sx - 1, sy, rgb(0x8A6420)); put(c, sx, sy + 1, rgb(0x8A6420)); put(c, sx + 1, sy, rgb(0x8A6420));
    for (int i = 0; i < 2; i++) {                      /* sleepy z's */
        float k = fmodf(t * 0.4f + i * 0.5f, 1.0f);
        int zx = (int)(mx + R * 0.1f + k * 6), zy = (int)(my - R * 0.8f - k * H * 0.3f);
        uint32_t zc = mixc(0xDDE4FF, 0x283070, k);
        hline(c, zx, zx + 3, zy, zc); put(c, zx + 2, zy + 1, rgb(zc)); put(c, zx + 1, zy + 2, rgb(zc)); hline(c, zx, zx + 3, zy + 3, zc);
    }
}

static void cloud(Canvas *c, float x, float y) {
    fill_ellipse(c, x - 4, y + 1, 4, 3, rgb(0xDCEBFA));
    fill_ellipse(c, x + 4, y + 1, 4, 3, rgb(0xDCEBFA));
    fill_ellipse(c, x - 4, y, 4, 3, rgb(0xFFFFFF));
    fill_ellipse(c, x + 4, y, 4, 3, rgb(0xFFFFFF));
    fill_ellipse(c, x, y - 2, 5, 4, rgb(0xFFFFFF));
}

static void pic_rainbow(Canvas *c, float t) {
    int W = c->w, H = c->h;
    gradient(c, 0x7CC8FF, 0xD8F2FF);
    float sa = t * 1.5f;                               /* the sun */
    for (int i = 0; i < 8; i++) {
        float a = sa + i * PI_F / 4;
        put(c, (int)(W * 0.14f + cosf(a) * 6.5f), (int)(H * 0.18f + sinf(a) * 6.5f), rgb(0xFFC93C));
    }
    fill_ellipse(c, W * 0.14f, H * 0.18f, 4, 4, rgb(0xFFE14D));
    float cx = W * 0.5f, cy = H * 0.88f, Ro = fminf(W * 0.46f, H * 0.82f), bt = Ro * 0.1f;
    float rev = clampf(t / 1.4f, 0, 1);
    for (int y = 0; y < (int)cy; y++)
        for (int x = 0; x < W; x++) {
            float d = hypotf(x + 0.5f - cx, y + 0.5f - cy), k = (Ro - d) / bt;
            if (k < 0 || k >= 6) continue;
            float a = atan2f(cy - y - 0.5f, x + 0.5f - cx);
            if ((PI_F - a) / PI_F <= rev) put(c, x, y, rgb(BANDS[(int)k]));
        }
    for (int i = 0; i < 2; i++) {                      /* two birds */
        float bx = fmodf(t * 6 + i * W * 0.5f, W + 10.0f) - 5, by = H * (0.15f + 0.12f * i);
        int up = (int)(t * 4 + i) % 2;
        put(c, (int)bx - 1, (int)by - up, rgb(0x34405A)); put(c, (int)bx, (int)by, rgb(0x34405A)); put(c, (int)bx + 1, (int)by - up, rgb(0x34405A));
    }
    float rm = Ro - 3 * bt;
    for (int y = H - 4; y < H; y++) hline(c, 0, W - 1, y, y == H - 4 ? 0x7FD86E : 0x5CBF5A);
    cloud(c, cx - rm, cy - 2 + sinf(t * 2) * 0.8f);
    cloud(c, cx + rm, cy - 2 + sinf(t * 2 + 1.5f) * 0.8f);
    if (rev >= 1) {                                    /* a sparkle sliding along */
        float a = PI_F * (1 - fmodf(t * 0.35f, 1.0f));
        int sx = (int)(cx + cosf(a) * rm), sy = (int)(cy - sinf(a) * rm);
        put(c, sx, sy, rgb(0xFFFFFF));
        put(c, sx - 1, sy, rgb(0xFFFFFF)); put(c, sx + 1, sy, rgb(0xFFFFFF));
        put(c, sx, sy - 1, rgb(0xFFFFFF)); put(c, sx, sy + 1, rgb(0xFFFFFF));
    }
}

static void pic_fish(Canvas *c, float t) {
    int W = c->w, H = c->h;
    gradient(c, 0x45ACE4, 0x175A8C);
    for (int y = 0; y < H * 7 / 10; y++)               /* sunbeams */
        for (int x = 0; x < W; x++)
            if (((x + y / 2 + (int)(t * 3)) % 16) < 2) tint_px(c, x, y, 0xFFFFFF, 0.12f);
    for (int x = 0; x < W; x++) {
        int sy = H - 5 + (int)(sinf(x * 0.4f) * 1.0f);
        fill_rect(c, x, sy, x, H - 1, rgb(0xE8C985));
        if (x % 5 == 2) put(c, x, sy + 2, rgb(0xCFAE6A));
    }
    for (int k = 0; k < 2; k++) {                      /* seaweed */
        float bx = W * (0.12f + 0.74f * k);
        int hgt = (int)(H * (0.55f - 0.12f * k));
        for (int i = 0; i < hgt; i++) {
            int xx = (int)(bx + sinf(i * 0.25f - t * 2.2f + k) * (float)i / hgt * 2.5f);
            put(c, xx, H - 5 - i, rgb(0x5CC05E));
            put(c, xx + 1, H - 5 - i, rgb(0x2E8A3E));
        }
    }
    fill_poly(c, (float[]){ W * 0.62f, W * 0.62f + 2.5f, W * 0.62f + 5 }, (float[]){ H - 3.0f, H - 7.0f, H - 3.0f }, 3, rgb(0xFF7A9A));
    for (int i = 0; i < 4; i++) {                      /* bubbles */
        float k = fmodf(t * 0.35f + i * 0.25f, 1.0f);
        int bx = (int)(W * 0.86f + sinf(k * 9 + i) * 1.5f), by = (int)(H - 8 - k * (H - 10));
        put(c, bx - 1, by, rgb(0xCFEFFF)); put(c, bx + 1, by, rgb(0xCFEFFF));
        put(c, bx, by - 1, rgb(0xFFFFFF)); put(c, bx, by + 1, rgb(0xCFEFFF));
    }
    Canvas f = canvas_new(26, 18);                     /* a clownfish */
    int wag = (int)(sinf(t * 12) * 1.5f);
    fill_poly(&f, (float[]){ 7, 1, 1 }, (float[]){ 9, 3.0f + wag, 15.0f + wag }, 3, rgb(0xFF8A2A));
    fill_poly(&f, (float[]){ 9, 13, 16 }, (float[]){ 5, 1, 5 }, 3, rgb(0xFF8A2A));
    fill_ellipse(&f, 13, 9, 8, 5.5f, rgb(0xFF8A2A));
    for (int y = 0; y < 18; y++)
        for (int x = 0; x < 26; x++) {
            if (get(&f, x, y) != rgb(0xFF8A2A)) continue;
            if (x == 10 || x == 11 || x == 16) put(&f, x, y, rgb(0xFFFFFF));
            else if (x == 9 || x == 12 || x == 15 || x == 17) put(&f, x, y, rgb(0x2A1A10));
            else if (y >= 11) put(&f, x, y, rgb(0xFFB060));
        }
    fill_rect(&f, 18, 6, 19, 7, rgb(0xFFFFFF));
    put(&f, 19, 7, rgb(0x111111));
    put(&f, 21, 10, rgb(0x8A3A10));
    outline(&f, rgb(0x5A2A08));
    float s = t * 0.8f;
    blit(c, &f, (int)(W / 2 + sinf(s) * W * 0.28f) - 13, (int)(H * 0.42f + sinf(t * 2.1f) * 2) - 9, cosf(s) < 0);
    free(f.px);
}

static int in_heart(float x, float y) {
    float a = x * x + y * y - 1;
    return a * a * a - x * x * y * y * y <= 0;
}

static void pic_heart(Canvas *c, float t) {
    int W = c->w, H = c->h;
    static const char *MINI[4] = { ".#.#.", "#####", ".###.", "..#.." };
    static const uint32_t MC[3] = { 0xFF6FA0, 0xC77DFF, 0xFF9E5E };
    gradient(c, 0xFFE0EC, 0xFFB8D2);
    for (int y = 2; y < H; y += 6)
        for (int x = (y / 6) % 2 * 3 + 1; x < W; x += 6) put(c, x, y, rgb(0xFFF2F7));
    for (int i = 0; i < 5; i++) {
        float k = fmodf(t * 0.3f + i * 0.2f, 1.0f);
        int mx = (int)(i * W / 5.0f + 3 + sinf(k * 6 + i) * 2), my = (int)(H + 2 - k * (H + 6));
        for (int j = 0; j < 4; j++)
            for (int q = 0; q < 5; q++)
                if (MINI[j][q] == '#') put(c, mx + q, my + j, rgb(MC[i % 3]));
    }
    float bt = fmodf(t, 1.1f);
    float s = 1 + 0.12f * expf(-bt * bt * 90) + 0.07f * expf(-(bt - 0.22f) * (bt - 0.22f) * 90);
    float R = H * 0.3f * s, cx = W * 0.5f, cy = H * 0.5f;
    for (int y = (int)(cy - R * 1.4f); y <= (int)(cy + R * 1.4f); y++)
        for (int x = (int)(cx - R * 1.3f); x <= (int)(cx + R * 1.3f); x++) {
            float X = (x + 0.5f - cx) / R, Y = (cy - y - 0.5f) / R + 0.2f, e = 1.0f / R;
            if (!in_heart(X, Y)) continue;
            int edge = !in_heart(X - e, Y) || !in_heart(X + e, Y) || !in_heart(X, Y - e) || !in_heart(X, Y + e);
            put(c, x, y, rgb(edge ? 0x9A1030 : 0xF0304A));
        }
    fill_ellipse(c, cx - R * 0.5f, cy - R * 0.55f, R * 0.18f, R * 0.12f, rgb(0xFF9AAA));
    int ex = (int)(cx - R * 0.3f), ey = (int)(cy - R * 0.15f);
    fill_rect(c, ex, ey, ex, ey + 1, rgb(0x5A0A1A));
    fill_rect(c, (int)(cx + R * 0.3f), ey, (int)(cx + R * 0.3f), ey + 1, rgb(0x5A0A1A));
    put(c, (int)cx - 1, ey + 3, rgb(0x5A0A1A)); put(c, (int)cx, ey + 4, rgb(0x5A0A1A)); put(c, (int)cx + 1, ey + 3, rgb(0x5A0A1A));
}

static void pic_rocket(Canvas *c, float t) {
    int W = c->w, H = c->h;
    gradient(c, 0x0E1033, 0x2A1A55);
    for (int i = 0; i < 20; i++) {                     /* stars whooshing past */
        float sp = 8 + (i % 3) * 7;
        int x = (i * 37 + 5) % W, y = (int)fmodf(i * 13 + t * sp * 2, (float)H);
        put(c, x, y, rgb(i % 3 == 2 ? 0xFFFFFF : 0xAAB4E0));
        if (i % 3 == 2) put(c, x, y - 1, rgb(0x5A6090));
    }
    float pr = H * 0.17f, px = W * 0.16f, py = H * 0.8f;
    fill_ellipse(c, px, py, pr, pr, rgb(0xE07A5F));
    for (int y = (int)(py - pr); y <= (int)(py + pr); y += 3)
        for (int x = (int)(px - pr); x <= (int)(px + pr); x++)
            if (hypotf(x + 0.5f - px, y + 0.5f - py) < pr - 0.5f) put(c, x, y, rgb(0xF29A7A));
    for (int y = (int)(py - pr); y <= (int)(py + pr); y++)          /* the ring */
        for (int x = (int)(px - pr * 1.9f); x <= (int)(px + pr * 1.9f); x++) {
            float dx = (x + 0.5f - px) / (pr * 1.8f), dy = (y + 0.5f - py) / (pr * 0.45f);
            float d = dx * dx + dy * dy;
            if (d > 0.8f && d < 1.15f && !(dy < 0 && hypotf(x + 0.5f - px, y + 0.5f - py) < pr))
                put(c, x, y, rgb(0xF2CC8F));
        }
    Canvas r = canvas_new(22, 38);
    int len = 4 + ((int)(t * 18) % 3) * 2;
    fill_ellipse(&r, 11, 27.0f + len * 0.5f, 3, len * 0.5f + 1, rgb(0xFF8A2A));
    fill_ellipse(&r, 11, 27.0f + len * 0.35f, 1.6f, len * 0.35f + 0.5f, rgb(0xFFE14D));
    fill_poly(&r, (float[]){ 6, 1, 6 }, (float[]){ 16, 26, 23 }, 3, rgb(0xE84545));
    fill_poly(&r, (float[]){ 16, 21, 16 }, (float[]){ 16, 26, 23 }, 3, rgb(0xE84545));
    fill_ellipse(&r, 11, 14, 5.5f, 11, rgb(0xF2F2F2));
    for (int y = 0; y < 38; y++)
        for (int x = 0; x < 22; x++)
            if (get(&r, x, y) == rgb(0xF2F2F2)) {
                if (y < 8) put(&r, x, y, rgb(0xE84545));
                else if (x >= 14) put(&r, x, y, rgb(0xC9CED8));
            }
    fill_rect(&r, 9, 24, 13, 25, rgb(0x7A808C));
    fill_ellipse(&r, 11, 14, 3, 3, rgb(0x8A9BB0));
    fill_ellipse(&r, 11, 14, 2, 2, rgb(0x6EC6FF));
    put(&r, 10, 13, rgb(0xFFFFFF));
    fill_rect(&r, 11, 18, 11, 25, rgb(0xE84545));
    outline(&r, rgb(0x1A1A2E));
    blit(c, &r, W / 2 - 11 + (int)(t * 14) % 2, (int)(H * 0.5f - 21 + sinf(t * 1.3f) * 2), 0);
    free(r.px);
}

static void pic_cat(const Books *b, Canvas *c, float t) {
    float cx, cy, cs;
    gradient(c, 0x0B1033, 0x2A1450);
    twinkles(c, t, 14, c->h);
    cat_pos(b, t, &cx, &cy, &cs);
    int ty = (int)(cy + 19 * cs), end = (int)(cx + 8 * cs);
    for (int x = 0; x < end; x++) {
        int wave = (x / 4 + (int)(t * 8)) % 2;
        for (int k = 0; k < 6; k++) fill_rect(c, x, ty + (k - 3) * 2 + wave, x, ty + (k - 3) * 2 + wave + 1, rgb(BANDS[k]));
    }
}

static void render_pic(Books *b) {
    Canvas *c = &b->art;
    float t = b->picT;
    switch (b->pic) {
    case PIC_MOON: pic_moon(c, t); break;
    case PIC_RAINBOW: pic_rainbow(c, t); break;
    case PIC_FISH: pic_fish(c, t); break;
    case PIC_HEART: pic_heart(c, t); break;
    case PIC_ROCKET: pic_rocket(c, t); break;
    default: pic_cat(b, c, t); break;
    }
    if (b->picTex) SDL_UpdateTexture(b->picTex, NULL, c->px, c->w * 4);
}

/* ---------- life cycle ---------- */

static void drop_textures(Books *b) {
    SDL_Texture **ts[4] = { &b->spine, &b->cover, &b->left, &b->right };
    for (int i = 0; i < 4; i++)
        if (*ts[i]) { SDL_DestroyTexture(*ts[i]); *ts[i] = NULL; }
}

static void choose_book(Books *b) {
    b->cur = irand(b->nbooks);
    do b->pic = irand(PICS); while (b->pic == b->lastPic);
    b->lastPic = b->pic;
    b->picT = 0;
    b->picFrame = -1;
    b->needTex = 1;
}

/* Textures need the renderer, so they are made in draw() for the chosen book. */
static void make_textures(Books *b, SDL_Renderer *ren) {
    drop_textures(b);
    b->needTex = 0;
    const Book *bk = &b->books[b->cur];
    Canvas sp = canvas_new(bk->w, bk->h);
    draw_spine(&sp, 0, 0, bk->w, bk->h, SPINES[bk->col], bk->seed);
    b->spine = canvas_texture(ren, &sp);
    Canvas cv = make_cover(b, bk, b->pic);
    b->cover = canvas_texture(ren, &cv);
    Canvas lp = make_left_page(b, bk, b->pic);
    b->left = canvas_texture(ren, &lp);
    Canvas rp = make_right_page(b, bk);
    b->right = canvas_texture(ren, &rp);
}

static void *create(SDL_Renderer *ren, int w, int h) {
    Books *b = calloc(1, sizeof *b);
    b->w = w;
    b->h = h;
    b->px = h / 180 > 1 ? h / 180 : 1;
    b->W = (w + b->px - 1) / b->px;
    b->H = (h + b->px - 1) / b->px;
    b->ch = (int)(b->H * 0.6f);
    b->cw = (int)(b->ch * 0.74f);
    b->pw = b->cw - 2;
    b->ph = b->ch - 4;
    b->picw = b->pw - 11;
    b->pich = (int)(b->picw * 0.78f);
    if (b->pich > b->ph * 62 / 100) b->pich = b->ph * 62 / 100;
    b->picx = (b->pw - b->picw) / 2 + 1;
    b->picy = 6;
    b->glowX = b->W * 0.5f; b->glowY = b->H * 0.4f;
    b->lampX = b->W * 0.5f; b->lampY = 0; b->lampR = b->H * 0.2f;

    Canvas c = canvas_new(b->W, b->H);
    build_shelf(b, &c);
    b->shelf = canvas_texture(ren, &c);
    for (int i = 0; i < CAT_FRAMES; i++) {
        Canvas k = make_waffle_cat(i);
        b->cat[i] = canvas_texture(ren, &k);
    }
    b->art = canvas_new(b->picw, b->pich);
    b->picTex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, b->picw, b->pich);
    if (b->picTex) SDL_SetTextureScaleMode(b->picTex, SDL_ScaleModeNearest);
    for (int i = 0; i < MOTES; i++)
        b->motes[i] = (Mote){ frand(0, (float)w), frand(0, (float)h), frand(-1, 1) * b->px, frand(-0.8f, 0.4f) * b->px, frand(0, 6.28f) };
    b->lastPic = -1;
    b->state = REST;
    b->dur = 1.2f;
    choose_book(b);
    return b;
}

static void add_part(Books *b, float x, float y, float vx, float vy, int kind, float size, SDL_Color c, float life) {
    for (int i = 0; i < PARTS; i++) {
        Part *p = &b->parts[i];
        if (p->life > 0) continue;
        *p = (Part){ x, y, vx, vy, life, life, size, frand(0, 6.28f), frand(0, 6.28f), kind, c };
        return;
    }
}

static void sparkle_burst(Books *b, float x, float y, int n) {
    float S = (float)b->px;
    for (int i = 0; i < n; i++) {
        float a = -PI_F * frand(0.1f, 0.9f), sp = b->h * frand(0.12f, 0.4f);
        int kind = irand(3) == 0 ? P_STAR : P_SPARKLE;
        add_part(b, x + frand(-1, 1) * b->cw * S * 0.4f, y, cosf(a) * sp, sinf(a) * sp, kind,
                 S * (kind == P_STAR ? frand(2.2f, 3.6f) : frand(1.6f, 2.8f)), hexcol(SPARKS[irand(6)]), frand(1.2f, 2.4f));
    }
}

static void slot_center(const Books *b, float *x, float *y) {
    const Book *bk = &b->books[b->cur];
    *x = (bk->x + bk->w * 0.5f) * b->px;
    *y = (bk->y + bk->h * 0.5f) * b->px;
}

static float lift_amount(const Books *b) { return b->books[b->cur].h * b->px * 0.5f; }

static void set_state(Books *b, int st, float dur) { b->state = st; b->stateT = 0; b->dur = dur; }

static void step(void *p, float dt) {
    Books *b = p;
    float S = (float)b->px;
    b->t += dt;
    b->stateT += dt;
    if (b->state >= OPEN && b->state <= CLOSE) b->picT += dt;
    if (b->stateT >= b->dur) {
        switch (b->state) {
        case REST: set_state(b, LIFT, 0.9f); break;
        case LIFT: set_state(b, FLYOUT, 1.5f); break;
        case FLYOUT: set_state(b, OPEN, 1.0f); break;
        case OPEN:
            set_state(b, SHOW, 5.5f);
            sparkle_burst(b, b->w * 0.5f, b->h * 0.5f - b->ch * S * 0.3f, 30);
            break;
        case SHOW: set_state(b, CLOSE, 0.9f); break;
        case CLOSE: set_state(b, FLYBACK, 1.5f); break;
        case FLYBACK: set_state(b, DROP, 0.6f); break;
        default: {
            const Book *bk = &b->books[b->cur];
            for (int i = 0; i < 10; i++)              /* a little puff of dust */
                add_part(b, (bk->x + frand(0, (float)bk->w)) * S, (bk->y + bk->h - frand(0, 2)) * S,
                         frand(-1, 1) * S * 8, -frand(1, 5) * S, P_DOT, S, (SDL_Color){ 230, 205, 160, 200 }, frand(0.5f, 0.9f));
            set_state(b, REST, frand(1.8f, 3.0f));
            choose_book(b);
            break;
        }
        }
    }
    if (b->state == SHOW) {                            /* sparkles float up out of the pages */
        b->spawn += dt * 9;
        while (b->spawn >= 1) {
            b->spawn -= 1;
            int kind = irand(3) == 0 ? P_STAR : (irand(3) ? P_SPARKLE : P_DOT);
            float x = b->w * 0.5f + frand(-1, 1) * b->pw * S, y = b->h * 0.5f - b->ch * S * frand(0.25f, 0.45f);
            add_part(b, x, y, frand(-1, 1) * b->h * 0.03f, -b->h * frand(0.05f, 0.12f), kind,
                     S * (kind == P_STAR ? frand(2, 3.2f) : frand(1.4f, 2.4f)), hexcol(SPARKS[irand(6)]), frand(1.6f, 3.0f));
        }
    }
    for (int i = 0; i < PARTS; i++) {
        Part *q = &b->parts[i];
        if (q->life <= 0) continue;
        q->life -= dt;
        q->x += (q->vx + sinf(b->t * 3 + q->ph) * b->h * 0.02f) * dt;
        q->y += q->vy * dt;
        q->vx *= 1 - 1.5f * dt;
        q->vy = q->vy * (1 - 1.2f * dt) - (q->kind != P_DOT ? b->h * 0.02f * dt : 0);
        q->rot += dt * 1.5f;
    }
    for (int i = 0; i < MOTES; i++) {
        Mote *m = &b->motes[i];
        m->x += (m->vx + sinf(b->t * 0.6f + m->ph) * S * 0.8f) * dt;
        m->y += (m->vy + cosf(b->t * 0.45f + m->ph) * S * 0.5f) * dt;
        if (m->x < 0) m->x += b->w;
        if (m->x > b->w) m->x -= b->w;
        if (m->y < 0) m->y += b->h;
        if (m->y > b->h) m->y -= b->h;
    }
}

static void star(SDL_Renderer *r, float x, float y, float R, float rot, SDL_Color c) {
    SDL_Vertex v[11];
    int idx[30];
    v[0] = (SDL_Vertex){ { x, y }, c, { 0, 0 } };
    for (int i = 0; i < 10; i++) {
        float a = rot + i * PI_F / 5 - PI_F / 2, rr = i % 2 ? R * 0.45f : R;
        v[i + 1] = (SDL_Vertex){ { x + cosf(a) * rr, y + sinf(a) * rr }, c, { 0, 0 } };
        idx[i * 3] = 0; idx[i * 3 + 1] = i + 1; idx[i * 3 + 2] = (i + 1) % 10 + 1;
    }
    SDL_RenderGeometry(r, NULL, v, 11, idx, 30);
}

static void sparkle(SDL_Renderer *r, float x, float y, float s, SDL_Color c) {
    thick_line(r, x - s, y, x + s, y, s * 0.35f, c);
    thick_line(r, x, y - s, x, y + s, s * 0.35f, c);
}

static void frect(SDL_Renderer *r, float x, float y, float w, float h, SDL_Color c) {
    SDL_FRect rc = { x, y, w, h };
    set_color(r, c);
    SDL_RenderFillRectF(r, &rc);
}

static void copy_tex(SDL_Renderer *r, SDL_Texture *t, float x, float y, float w, float h, float bright) {
    if (!t || w <= 0.5f) return;
    Uint8 v = (Uint8)(255 * clampf(bright, 0, 1));
    SDL_FRect d = { x, y, w, h };
    SDL_SetTextureColorMod(t, v, v, v);
    SDL_RenderCopyF(r, t, NULL, &d);
    SDL_SetTextureColorMod(t, 255, 255, 255);
}

/* The book in flight: its spine turns away as the cover turns towards us.
   k = 0 is the lifted spine above its slot, k = 1 the closed book in the middle. */
static void draw_flying(Books *b, SDL_Renderer *r, float k) {
    const Book *bk = &b->books[b->cur];
    float S = (float)b->px, e = ease(k);
    float sx, sy;
    slot_center(b, &sx, &sy);
    sy -= lift_amount(b);
    float h0 = bk->h * S, hn = lerp(h0, b->ch * S, e);
    float a = ease((k - 0.1f) / 0.8f) * PI_F * 0.5f;
    float spw = bk->w * S * (hn / h0) * cosf(a), cow = b->cw * S * (hn / (b->ch * S)) * sinf(a);
    float cx = lerp(sx, b->w * 0.5f, e), cy = lerp(sy, b->h * 0.5f, e) - b->h * 0.12f * sinf(PI_F * k);
    float left = cx - (spw + cow) * 0.5f, top = cy - hn * 0.5f;
    float g = fminf(1, glow_at(b, sx / S, sy / S)), lit = lerp(g, 1, e);
    frect(r, left + 2 * S, top + 3 * S, spw + cow, hn, (SDL_Color){ 0, 0, 0, (Uint8)(70 * e) });
    copy_tex(r, b->spine, left, top, spw, hn, lit * (0.6f + 0.4f * cosf(a)));
    copy_tex(r, b->cover, left + spw, top, cow, hn, lit * (0.55f + 0.45f * sinf(a)));
}

/* The book in the middle, its front cover swung open by angle th (0 shut, PI open). */
static void draw_open(Books *b, SDL_Renderer *r, float th, float bob) {
    const Book *bk = &b->books[b->cur];
    float S = (float)b->px, c = cosf(th), o = (1 - c) * 0.5f;
    float CW = b->cw * S, CH = b->ch * S, PW = b->pw * S, PH = b->ph * S;
    float hx = b->w * 0.5f - CW * 0.5f * (1 - o), top = b->h * 0.5f - CH * 0.5f + bob;
    uint32_t col = SPINES[bk->col];
    float lw = c < 0 ? CW * -c : 0;
    frect(r, hx - lw + 2 * S, top + 3 * S, CW + lw, CH, (SDL_Color){ 0, 0, 0, 70 });
    frect(r, hx, top, CW, CH, hexcol(shade(col, 0.45f)));                 /* back cover */
    frect(r, hx, top + S, CW - S, CH - 2 * S, hexcol(shade(col, 0.8f)));
    frect(r, hx, top + 2 * S, PW, PH, hexcol(0xCDBA8E));                    /* page edges */
    copy_tex(r, b->right, hx, top + S, PW, PH, 1);
    SDL_FRect pr = { hx + b->picx * S, top + S + b->picy * S, b->picw * S, b->pich * S };
    if (b->picTex) SDL_RenderCopyF(r, b->picTex, NULL, &pr);
    if (b->pic == PIC_CAT) {
        float cx, cy, cs;
        cat_pos(b, b->picT, &cx, &cy, &cs);
        SDL_FRect d = { pr.x + cx * S, pr.y + cy * S, CAT_W * cs * S, CAT_H * cs * S };
        SDL_RenderCopyF(r, b->cat[(int)(b->picT * 8) % CAT_FRAMES], NULL, &d);
    }
    if (c > 0) {
        copy_tex(r, b->cover, hx, top, CW * c, CH, 0.55f + 0.45f * c);
    } else {
        frect(r, hx - lw, top, lw, CH, hexcol(shade(col, 0.45f)));         /* inside of the cover */
        frect(r, hx - lw + S * -c, top + S, lw - S * -c, CH - 2 * S, hexcol(shade(col, 0.8f)));
        frect(r, hx - PW * -c, top + 2 * S, PW * -c, PH, hexcol(0xCDBA8E));
        copy_tex(r, b->left, hx - PW * -c, top + S, PW * -c, PH, 0.65f + 0.35f * -c);
    }
    frect(r, hx - S, top + S, 2 * S, CH - 2 * S, (SDL_Color){ 60, 30, 10, 70 });
}

static void draw(void *p, SDL_Renderer *r) {
    Books *b = p;
    float S = (float)b->px;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    if (b->needTex) make_textures(b, r);
    set_color(r, hexcol(0x24150C));
    SDL_RenderFillRect(r, NULL);

    float fl = 0.95f + 0.05f * (0.5f + 0.5f * sinf(b->t * 2.3f) * sinf(b->t * 3.7f + 1));
    Uint8 fv = (Uint8)(255 * fl);
    SDL_SetTextureColorMod(b->shelf, fv, fv, fv);
    SDL_Rect d = { 0, 0, b->W * b->px, b->H * b->px };
    SDL_RenderCopy(r, b->shelf, NULL, &d);
    if (b->hasLamp) {
        for (int i = 0; i < 6; i++)
            fill_circle(r, b->lampX * S, b->lampY * S, b->lampR * S * (0.65f - i * 0.08f), (SDL_Color){ 255, 214, 140, (Uint8)(9 * fl) });
    }

    const Book *bk = &b->books[b->cur];
    if (b->state != REST) {                             /* the empty slot */
        frect(r, bk->x * S, bk->y * S, bk->w * S, bk->h * S, hexcol(0x1C100A));
        frect(r, bk->x * S, bk->y * S, S, bk->h * S, hexcol(0x120A06));
    }
    for (int i = 0; i < MOTES; i++) {                   /* dust drifting in the light */
        Mote *m = &b->motes[i];
        float g = clampf((glow_at(b, m->x / S, m->y / S) - 0.6f) / 0.7f, 0, 1);
        float tw = 0.4f + 0.6f * (0.5f + 0.5f * sinf(b->t * 1.7f + m->ph * 3));
        if (g * tw < 0.05f) continue;
        frect(r, floorf(m->x / S) * S, floorf(m->y / S) * S, S, S, (SDL_Color){ 255, 238, 196, (Uint8)(210 * g * tw) });
    }

    float k = b->stateT / b->dur, focus = 0;
    if (b->state == FLYOUT) focus = ease(k);
    else if (b->state >= OPEN && b->state <= CLOSE) focus = 1;
    else if (b->state == FLYBACK) focus = 1 - ease(k);
    if (focus > 0) {
        frect(r, 0, 0, (float)b->w, (float)b->h, (SDL_Color){ 10, 5, 2, (Uint8)(110 * focus) });
        for (int i = 0; i < 4; i++)                     /* a warm glow around the book */
            fill_circle(r, b->w * 0.5f, b->h * 0.5f, b->ch * S * (1.05f - i * 0.1f), (SDL_Color){ 255, 205, 130, (Uint8)(11 * focus) });
    }

    switch (b->state) {
    case LIFT:
    case DROP: {
        float up = lift_amount(b) * ease(b->state == LIFT ? k : 1 - k);
        float wig = b->state == LIFT ? sinf(k * PI_F * 6) * S * 0.7f * (1 - k) : 0;
        float g = fminf(1, glow_at(b, bk->x + bk->w * 0.5f, bk->y + bk->h * 0.5f)) * fl;
        copy_tex(r, b->spine, bk->x * S + wig, bk->y * S - up, bk->w * S, bk->h * S, g);
        break;
    }
    case FLYOUT: draw_flying(b, r, k); break;
    case FLYBACK: draw_flying(b, r, 1 - k); break;
    case OPEN:
    case SHOW:
    case CLOSE: {
        int frame = (int)(b->picT * 12);
        if (frame != b->picFrame) { b->picFrame = frame; render_pic(b); }
        float th = b->state == OPEN ? PI_F * ease(k) : (b->state == SHOW ? PI_F : PI_F * (1 - ease(k)));
        draw_open(b, r, th, b->state == SHOW ? sinf(PI_F * 4 * k) * S : 0);
        break;
    }
    default: break;
    }

    for (int i = 0; i < PARTS; i++) {
        Part *q = &b->parts[i];
        if (q->life <= 0) continue;
        float f = q->life / q->max, a = f < 0.3f ? f / 0.3f : (f > 0.85f ? (1 - f) / 0.15f : 1);
        SDL_Color c = q->c;
        c.a = (Uint8)(c.a * a);
        if (q->kind == P_STAR) star(r, q->x, q->y, q->size, q->rot, c);
        else if (q->kind == P_SPARKLE) sparkle(r, q->x, q->y, q->size * (0.7f + 0.3f * sinf(q->rot * 4)), c);
        else frect(r, q->x, q->y, q->size, q->size, c);
    }
}

static void destroy(void *p) {
    Books *b = p;
    drop_textures(b);
    SDL_DestroyTexture(b->shelf);
    if (b->picTex) SDL_DestroyTexture(b->picTex);
    for (int i = 0; i < CAT_FRAMES; i++) SDL_DestroyTexture(b->cat[i]);
    free(b->art.px);
    free(b);
}

const Saver saver_books = { "books", "BOOKS", create, step, draw, destroy };
