/* WORD: a word of the day on a classroom chalkboard. A picture card taped to the board
   shows the word as a little moving pixel-art picture (a cat blinks and flicks its tail,
   the sun's rays turn, a fish swims). Next to it the word is spelled out in chalk, one
   letter at a time, while that letter lights up on the alphabet strip above the board.
   Dots and dashes appear under its sounds (a dash where two letters make one sound, like
   the SH in FISH), a star slides along under them to sound it out, and the whole word
   bounces. Then a short sentence writes itself across the bottom. After a while the
   eraser wipes the board and it starts over, now and then with the vowels in pink, and
   every few rounds the board shows yesterday's and tomorrow's words too.
   The word comes from the date, so it stays the same all day (something to talk about
   at dinner) and changes at midnight. The words are a plain table at the bottom of the
   pictures: a word, its sounds, a sentence and a picture. */
#include "../engine.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PW 96                    /* every picture is drawn on a PW x PH canvas */
#define PH 72
#define PIC_FPS 15               /* and redrawn this many times a second */
#define MAXLEN 12                /* longest word the board can hold */
#define PARTS 160

typedef void (*PicFn)(Canvas *c, float t);

typedef struct {
    const char *word;            /* capitals */
    const char *sounds;          /* its sounds, spaced: "F I SH"; lowercase = silent ("C A K e"); NULL: one per letter */
    const char *sentence;        /* short and kind, in capitals; it should contain the word */
    PicFn pic;
} Word;

/* ---------- small drawing helpers for the pictures (colors are 0xRRGGBB) ---------- */

static float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static float lerp(float a, float b, float t) { return a + (b - a) * t; }
static float ease(float k) { k = clampf(k, 0, 1); return k * k * (3 - 2 * k); }

static uint32_t mixc(uint32_t a, uint32_t b, float t) {
    int r = (int)lerp((float)((a >> 16) & 255), (float)((b >> 16) & 255), t);
    int g = (int)lerp((float)((a >> 8) & 255), (float)((b >> 8) & 255), t);
    int bl = (int)lerp((float)(a & 255), (float)(b & 255), t);
    return (uint32_t)(r << 16 | g << 8 | bl);
}

static uint32_t shade(uint32_t hex, float f) {
    int r = (int)(((hex >> 16) & 255) * f), g = (int)(((hex >> 8) & 255) * f), b = (int)((hex & 255) * f);
    r = r > 255 ? 255 : r; g = g > 255 ? 255 : g; b = b > 255 ? 255 : b;
    return (uint32_t)(r << 16 | g << 8 | b);
}

static void R(Canvas *c, int x0, int y0, int x1, int y1, uint32_t hex) { fill_rect(c, x0, y0, x1, y1, rgb(hex)); }
static void E(Canvas *c, float cx, float cy, float rx, float ry, uint32_t hex) { fill_ellipse(c, cx, cy, rx, ry, rgb(hex)); }
static void D(Canvas *c, int x, int y, uint32_t hex) { put(c, x, y, rgb(hex)); }

static void tri(Canvas *c, float x0, float y0, float x1, float y1, float x2, float y2, uint32_t hex) {
    fill_poly(c, (float[]){ x0, x1, x2 }, (float[]){ y0, y1, y2 }, 3, rgb(hex));
}

static void quad(Canvas *c, float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, uint32_t hex) {
    fill_poly(c, (float[]){ x0, x1, x2, x3 }, (float[]){ y0, y1, y2, y3 }, 4, rgb(hex));
}

/* A polygon turned by angle a around (cx, cy); the points are relative to that pivot. */
static void rpoly(Canvas *c, float cx, float cy, float a, const float *xs, const float *ys, int n, uint32_t hex) {
    float X[16], Y[16], co = cosf(a), si = sinf(a);
    for (int i = 0; i < n && i < 16; i++) {
        X[i] = cx + xs[i] * co - ys[i] * si;
        Y[i] = cy + xs[i] * si + ys[i] * co;
    }
    fill_poly(c, X, Y, n < 16 ? n : 16, rgb(hex));
}

static void ln(Canvas *c, float x0, float y0, float x1, float y1, uint32_t hex) {
    int n = (int)ceilf(fmaxf(fabsf(x1 - x0), fabsf(y1 - y0)));
    for (int i = 0; i <= n; i++) {
        float u = n ? (float)i / n : 0;
        put(c, (int)floorf(lerp(x0, x1, u) + 0.5f), (int)floorf(lerp(y0, y1, u) + 0.5f), rgb(hex));
    }
}

/* A thick stroke: round dabs of radius r along the line. */
static void brush(Canvas *c, float x0, float y0, float x1, float y1, float r, uint32_t hex) {
    int n = (int)ceilf(hypotf(x1 - x0, y1 - y0) * 2) + 1;
    for (int i = 0; i <= n; i++) {
        float u = (float)i / n;
        fill_ellipse(c, lerp(x0, x1, u), lerp(y0, y1, u), r, r, rgb(hex));
    }
}

static void tint(Canvas *c, int x, int y, uint32_t to, float a) {
    uint32_t p = get(c, x, y);
    if (p) put(c, x, y, rgb(mixc(p >> 8, to, a)));
}

static void grad(Canvas *c, uint32_t top, uint32_t bot) {
    for (int y = 0; y < c->h; y++) fill_rect(c, 0, y, c->w - 1, y, rgb(mixc(top, bot, (float)y / (c->h - 1))));
}

static void sky(Canvas *c) { grad(c, 0x6EC0F5, 0xD4F0FF); }

static void grass(Canvas *c, int y, uint32_t col) {
    for (int x = 0; x < c->w; x++) {
        int top = y + (int)(sinf(x * 0.11f) * 1.2f);
        fill_rect(c, x, top, x, c->h - 1, rgb(col));
        put(c, x, top, rgb(shade(col, 1.2f)));
        if (x % 6 == 2) put(c, x, top - 1, rgb(shade(col, 1.2f)));
        if (x % 9 == 4) put(c, x, top + 5 + x % 4, rgb(shade(col, 0.85f)));
    }
}

static void cloud(Canvas *c, float x, float y, float s) {
    E(c, x - 5 * s, y + 1, 5 * s, 3.5f * s, 0xE4F0FA);
    E(c, x + 5 * s, y + 1, 5 * s, 3.5f * s, 0xE4F0FA);
    E(c, x - 5 * s, y, 5 * s, 3.5f * s, 0xFFFFFF);
    E(c, x + 5 * s, y, 5 * s, 3.5f * s, 0xFFFFFF);
    E(c, x, y - 2.5f * s, 6 * s, 4.5f * s, 0xFFFFFF);
}

static void twinkles(Canvas *c, float t, int n, int maxY) {
    for (int i = 0; i < n; i++) {
        int x = (i * 41 + 7) % c->w, y = (i * 29 + 3) % (maxY > 1 ? maxY : 1);
        float tw = 0.5f + 0.5f * sinf(t * 2.5f + i * 1.7f);
        D(c, x, y, mixc(0x7880B8, 0xFFF6D0, tw));
        if (tw > 0.85f && i % 3 == 0) {
            D(c, x - 1, y, 0xB8B8D8); D(c, x + 1, y, 0xB8B8D8);
            D(c, x, y - 1, 0xB8B8D8); D(c, x, y + 1, 0xB8B8D8);
        }
    }
}

static void sparkle(Canvas *c, int x, int y, uint32_t hex) {
    D(c, x, y, 0xFFFFFF);
    D(c, x - 1, y, hex); D(c, x + 1, y, hex); D(c, x, y - 1, hex); D(c, x, y + 1, hex);
}

/* A little music note. */
static void note(Canvas *c, int x, int y, uint32_t hex) {
    R(c, x, y, x + 1, y + 1, hex);
    R(c, x + 2, y - 5, x + 2, y + 1, hex);
    D(c, x + 3, y - 5, hex); D(c, x + 4, y - 4, hex);
}

/* Eyes: a dark dot with a white glint, or a line when shut. */
static void eye(Canvas *c, int x, int y, int shut) {
    if (shut) { R(c, x - 1, y + 1, x + 2, y + 1, 0x1E1410); return; }
    R(c, x, y, x + 1, y + 2, 0x1E1410);
    D(c, x, y, 0xFFFFFF);
}

static int blink(float t, float ph) { return fmodf(t + ph, 3.7f) < 0.16f; }

/* The subject of a picture is drawn on a clear layer, outlined, then laid on the scene. */
static uint32_t LAYPX[PW * PH];
static Canvas LAY = { PW, PH, LAYPX };

static Canvas *lay(void) {
    memset(LAYPX, 0, sizeof LAYPX);
    return &LAY;
}

static void flush(Canvas *c, uint32_t ol) {
    outline(&LAY, rgb(ol));
    for (int i = 0; i < PW * PH; i++)
        if (LAYPX[i]) c->px[i] = LAYPX[i];
}

/* The same, leaning: each row slides sideways by k per pixel below (or above) py. */
static void flush_lean(Canvas *c, uint32_t ol, float py, float k) {
    outline(&LAY, rgb(ol));
    for (int y = 0; y < PH; y++) {
        int dx = (int)lroundf((y - py) * k);
        for (int x = 0; x < PW; x++)
            if (LAYPX[y * PW + x]) put(c, x + dx, y, LAYPX[y * PW + x]);
    }
}

/* ---------- the pictures ---------- */

static void pic_cat(Canvas *c, float t) {
    grad(c, 0xF7E4BC, 0xEDCF98);
    R(c, 8, 6, 34, 30, 0xFFFFFF);                          /* a sunny window */
    for (int y = 8; y <= 28; y++) R(c, 10, y, 32, y, mixc(0x7CC8FF, 0xC8ECFF, (y - 8) / 20.0f));
    E(c, 16, 13, 3.5f, 3.5f, 0xFFE14D);
    R(c, 20, 8, 21, 28, 0xFFFFFF);
    R(c, 10, 17, 32, 18, 0xFFFFFF);
    R(c, 6, 30, 36, 32, 0xC8925A);
    R(c, 0, 56, 95, 71, 0xC8925A);                         /* floorboards and a rug */
    for (int x = 0; x < 96; x += 12) R(c, x, 57, x, 71, 0xA8743E);
    R(c, 0, 56, 95, 56, 0xA8743E);
    E(c, 60, 64, 32, 6, 0xD8584A);
    E(c, 60, 64, 26, 4, 0xEC8A6A);
    for (int y = 33; y < 72; y++)                          /* the sunbeam the cat sits in */
        for (int x = (int)lerp(10, 46, (y - 33) / 38.0f); x <= (int)lerp(32, 84, (y - 33) / 38.0f); x++) tint(c, x, y, 0xFFF6D0, 0.22f);
    Canvas *L = lay();
    float flick = sinf(t * 2.3f);
    for (int i = 0; i <= 16; i++) {                        /* the tail curls up, its tip flicking */
        float u = i / 16.0f;
        float x = 71 + u * 12 + flick * u * u * 5, y = 60 - u * u * 24;
        E(L, x, y, 2.6f - u * 0.5f, 2.6f - u * 0.5f, i > 13 ? 0xC8681C : 0xF29A3A);
    }
    E(L, 60, 50, 14, 13, 0xF29A3A);
    E(L, 60, 53, 7, 9, 0xFFE6C4);
    E(L, 54, 62, 4.5f, 2.6f, 0xFFE6C4);
    E(L, 66, 62, 4.5f, 2.6f, 0xFFE6C4);
    tri(L, 47, 27, 50, 11, 58, 21, 0xF29A3A);
    tri(L, 62, 21, 70, 11, 73, 27, 0xF29A3A);
    tri(L, 50, 23, 51, 15, 56, 21, 0xFFA8B8);
    tri(L, 64, 21, 69, 15, 70, 23, 0xFFA8B8);
    E(L, 60, 30, 13.5f, 11, 0xF29A3A);
    E(L, 60, 35, 6, 3.5f, 0xFFE6C4);
    R(L, 59, 20, 60, 24, 0xC8681C); R(L, 55, 21, 56, 23, 0xC8681C); R(L, 63, 21, 64, 23, 0xC8681C);
    R(L, 47, 46, 49, 47, 0xC8681C); R(L, 46, 50, 48, 51, 0xC8681C);
    R(L, 71, 46, 73, 47, 0xC8681C); R(L, 72, 50, 74, 51, 0xC8681C);
    flush(c, 0x5A3010);
    if (blink(t, 0.4f)) {
        R(c, 52, 30, 56, 30, 0x5A3010); R(c, 64, 30, 68, 30, 0x5A3010);
    } else {
        E(c, 54.5f, 30, 2.6f, 2.8f, 0x8ED04A); R(c, 54, 28, 54, 32, 0x1A1A1A); D(c, 53, 29, 0xFFFFFF);
        E(c, 66.5f, 30, 2.6f, 2.8f, 0x8ED04A); R(c, 66, 28, 66, 32, 0x1A1A1A); D(c, 65, 29, 0xFFFFFF);
    }
    tri(c, 58, 33, 63, 33, 60.5f, 35.5f, 0xFF7A96);
    D(c, 58, 37, 0x5A3010); D(c, 59, 38, 0x5A3010); D(c, 60, 37, 0x5A3010); D(c, 61, 38, 0x5A3010); D(c, 62, 37, 0x5A3010);
    ln(c, 51, 35, 41, 33, 0x7A4A20); ln(c, 51, 37, 41, 38, 0x7A4A20);
    ln(c, 70, 35, 80, 33, 0x7A4A20); ln(c, 70, 37, 80, 38, 0x7A4A20);
}

static void pic_dog(Canvas *c, float t) {
    sky(c);
    cloud(c, fmodf(t * 2, 120) - 12, 12, 1);
    grass(c, 52, 0x6CC25A);
    for (int i = 0; i < 5; i++) {                          /* a few flowers */
        int fx = 6 + i * 21, fy = 60 + (i * 5) % 7;
        D(c, fx, fy, 0xFFFFFF); D(c, fx - 1, fy, 0xFFFFFF); D(c, fx + 1, fy, 0xFFFFFF); D(c, fx, fy - 1, 0xFFFFFF); D(c, fx, fy + 1, 0xFFFFFF);
        D(c, fx, fy, 0xFFD23F);
    }
    Canvas *L = lay();
    float wag = sinf(t * 13) * 0.55f;
    brush(L, 58, 56, 58 + cosf(0.8f + wag) * 13, 56 - sinf(0.8f + wag) * 13, 2.2f, 0xD9A05B);
    E(L, 48, 52, 14, 12, 0xD9A05B);
    E(L, 48, 55, 7, 8.5f, 0xFFF4E0);
    E(L, 42, 63, 4.5f, 2.6f, 0xFFF4E0);
    E(L, 54, 63, 4.5f, 2.6f, 0xFFF4E0);
    E(L, 48, 30, 14, 12.5f, 0xD9A05B);
    E(L, 48, 25, 2.5f, 7, 0xFFF4E0);
    E(L, 48, 37, 8, 5.5f, 0xFFF4E0);
    R(L, 37, 43, 59, 44, 0xE04848);
    float ear = sinf(t * 9) > 0.6f ? 1 : 0;
    E(L, 34, 32 + ear, 4.5f, 9.5f, 0x8A5A2E);
    E(L, 62, 32 + ear, 4.5f, 9.5f, 0x8A5A2E);
    flush(c, 0x4A2A10);
    E(c, 48.5f, 46.5f, 1.7f, 1.7f, 0xFFD23F);
    int sh = blink(t, 1.1f);
    eye(c, 42, 28, sh);
    eye(c, 53, 28, sh);
    E(c, 48.5f, 34.5f, 2.8f, 1.9f, 0x2A1A10);
    D(c, 47, 34, 0x8A8A8A);
    D(c, 48, 36, 0x2A1A10);
    ln(c, 48, 37, 45, 38, 0x2A1A10); ln(c, 49, 37, 52, 38, 0x2A1A10);
    int pant = sinf(t * 9) > 0;
    E(c, 48.5f, 40.5f + pant, 2.2f, 2 + pant, 0xFF7A8A);
    R(c, 48, 40, 48, 41 + pant, 0xE05A70);
}

static void pic_sun(Canvas *c, float t) {
    grad(c, 0x4FAEF0, 0xC4ECFF);
    cloud(c, fmodf(t * 3 + 70, 130) - 15, 58, 1.1f);
    cloud(c, fmodf(t * 2 + 10, 130) - 15, 16, 0.8f);
    float cx = 48, cy = 36;
    for (int i = 0; i < 12; i++) {                         /* the rays turn slowly */
        float a = t * 0.5f + i * PI_F / 6, len = i % 2 ? 29 : 25;
        float x0 = cx + cosf(a - 0.13f) * 18, y0 = cy + sinf(a - 0.13f) * 18;
        float x1 = cx + cosf(a + 0.13f) * 18, y1 = cy + sinf(a + 0.13f) * 18;
        tri(c, x0, y0, x1, y1, cx + cosf(a) * len, cy + sinf(a) * len, i % 2 ? 0xFF9A2E : 0xFFC23A);
    }
    Canvas *L = lay();
    E(L, cx, cy, 17, 17, 0xFFD93D);
    E(L, cx + 2, cy + 2, 14, 14, 0xFFC93A);
    E(L, cx - 1, cy - 1, 13, 13, 0xFFE55A);
    E(L, cx - 6, cy - 8, 3, 2, 0xFFF4A8);
    flush(c, 0xD8801A);
    int sh = blink(t, 2.0f);
    if (sh) { R(c, 40, 33, 43, 33, 0x7A4A10); R(c, 53, 33, 56, 33, 0x7A4A10); }
    else { E(c, 42, 32, 1.8f, 2.4f, 0x5A3410); E(c, 55, 32, 1.8f, 2.4f, 0x5A3410); D(c, 41, 31, 0xFFFFFF); D(c, 54, 31, 0xFFFFFF); }
    E(c, 38, 38, 2.5f, 1.6f, 0xFF9A7A);
    E(c, 59, 38, 2.5f, 1.6f, 0xFF9A7A);
    for (int x = -6; x <= 6; x++) D(c, (int)cx + x, (int)(cy + 5 + (36 - x * x) / 12.0f), 0x7A4A10);
}

/* A fish facing dir (1 = right), its tail swishing. */
static void fish_shape(Canvas *L, float x, float y, int dir, float wag, uint32_t body, uint32_t fin, uint32_t belly) {
    float d = (float)dir;
    tri(L, x - d * 11, y, x - d * 22, y - 9 + wag, x - d * 22, y + 9 + wag, fin);
    tri(L, x - d * 6, y - 7, x + d * 3, y - 13, x + d * 6, y - 6, fin);
    tri(L, x - d * 2, y + 7, x - d * 6, y + 12, x + d * 3, y + 7, fin);
    E(L, x, y, 14, 9, body);
    E(L, x + d * 1, y + 4, 10, 4, belly);
}

static void pic_fish(Canvas *c, float t) {
    grad(c, 0x4FB4E8, 0x1A5E92);
    for (int y = 0; y < 50; y++)                           /* sunbeams */
        for (int x = 0; x < PW; x++)
            if (((x + y / 2 + (int)(t * 3)) % 18) < 2) tint(c, x, y, 0xFFFFFF, 0.12f);
    for (int x = 0; x < PW; x++) {
        int sy = 64 + (int)(sinf(x * 0.3f) * 1.0f);
        R(c, x, sy, x, PH - 1, 0xE8C985);
        if (x % 5 == 2) D(c, x, sy + 3, 0xCFAE6A);
    }
    for (int k = 0; k < 3; k++) {                          /* seaweed */
        float bx = 8 + k * 38 + (k == 2 ? 6 : 0);
        int hgt = 30 - k * 5;
        for (int i = 0; i < hgt; i++) {
            int xx = (int)(bx + sinf(i * 0.25f - t * 2.2f + k) * (float)i / hgt * 3);
            D(c, xx, 64 - i, 0x5CC05E); D(c, xx + 1, 64 - i, 0x2E8A3E);
        }
    }
    tri(c, 58, 66, 61, 61, 64, 66, 0xFF7A9A);
    float s = t * 0.6f, fx = 48 + sinf(s) * 18, fy = 32 + sinf(t * 1.7f) * 3;
    int dir = cosf(s) >= 0 ? 1 : -1;
    for (int i = 0; i < 4; i++) {                          /* bubbles from its mouth */
        float k = fmodf(t * 0.5f + i * 0.25f, 1.0f);
        int bx = (int)(fx + dir * 15 + sinf(k * 9 + i) * 1.5f), by = (int)(fy - 3 - k * 34);
        D(c, bx - 1, by, 0xCFEFFF); D(c, bx + 1, by, 0xCFEFFF); D(c, bx, by - 1, 0xFFFFFF); D(c, bx, by + 1, 0xCFEFFF);
    }
    Canvas *L = lay();
    fish_shape(L, fx, fy, dir, sinf(t * 10) * 2, 0xFF8A2A, 0xFF6A1A, 0xFFC070);
    flush(c, 0x7A2A08);
    int ex = (int)(fx + dir * 7);
    R(c, ex - 1, (int)fy - 4, ex + 1, (int)fy - 2, 0xFFFFFF);
    D(c, ex + (dir > 0 ? 1 : -1), (int)fy - 3, 0x111111);
    for (int k = -4; k <= 4; k++) D(c, (int)(fx + dir * (3 - abs(k) / 3)), (int)fy + k, 0xE06A1A);
    D(c, (int)(fx + dir * 13), (int)fy + 1, 0x7A2A08);
}

static void pic_tree(Canvas *c, float t) {
    sky(c);
    cloud(c, fmodf(t * 2 + 30, 130) - 15, 10, 0.9f);
    grass(c, 58, 0x6CC25A);
    Canvas *L = lay();
    R(L, 43, 34, 53, 62, 0x8A5A32);
    R(L, 50, 34, 53, 62, 0x6E4422);
    tri(L, 40, 63, 45, 55, 45, 63, 0x8A5A32);
    tri(L, 51, 55, 57, 63, 51, 63, 0x6E4422);
    brush(L, 48, 44, 38, 36, 1.5f, 0x8A5A32);
    float sw = sinf(t * 1.1f) * 1.2f;
    E(L, 48 + sw, 24, 26, 16, 0x4FAE4A);
    E(L, 30 + sw, 32, 13, 10, 0x4FAE4A);
    E(L, 66 + sw, 32, 13, 10, 0x4FAE4A);
    E(L, 48 + sw, 12, 15, 9, 0x4FAE4A);
    E(L, 40 + sw, 17, 9, 6, 0x6CC65A);
    E(L, 30 + sw, 28, 6, 4, 0x6CC65A);
    E(L, 58 + sw, 10, 6, 3, 0x6CC65A);
    flush(c, 0x2E5A2A);
    static const float AP[7][2] = { { 34, 26 }, { 50, 18 }, { 62, 28 }, { 42, 36 }, { 70, 36 }, { 26, 36 }, { 56, 38 } };
    for (int i = 0; i < 7; i++) {
        float ax = AP[i][0] + sw, ay = AP[i][1] + sinf(t * 2 + i) * 0.4f;
        E(c, ax, ay, 2.6f, 2.6f, 0xE0302A);
        D(c, (int)ax - 1, (int)ay - 1, 0xFF9A8A);
        D(c, (int)ax, (int)ay - 3, 0x5A3A1A);
    }
}

static void pic_cake(Canvas *c, float t) {
    grad(c, 0xFFE0EC, 0xF8C8DC);
    for (int i = 0; i < 9; i++) {                          /* bunting */
        float x = 4 + i * 11;
        tri(c, x, 4 + sinf(i * 0.9f) * 1, x + 9, 4 + sinf(i * 0.9f + 0.9f) * 1, x + 4.5f, 12, (uint32_t[]){ 0xFF6A6A, 0xFFD23F, 0x6AC8FF, 0x7AD86A }[i % 4]);
    }
    for (int x = 0; x < PW; x++) D(c, x, 4 + (int)(sinf(x * 0.08f) * 1), 0x8A5A6A);
    R(c, 0, 60, 95, 71, 0xA8743E);
    R(c, 0, 60, 95, 61, 0xC8925A);
    E(c, 48, 60, 31, 3.5f, 0xFFFFFF);
    Canvas *L = lay();
    R(L, 24, 38, 72, 58, 0xFFF2DC);
    E(L, 48, 58, 24, 3, 0xFFF2DC);
    R(L, 24, 47, 72, 49, 0xFF8AB8);
    E(L, 48, 38, 24, 5, 0xFF8AB8);
    for (int i = 0; i < 7; i++) {                          /* frosting drips */
        int x = 26 + i * 7, len = 3 + (i * 5) % 4;
        R(L, x, 40, x + 2, 40 + len, 0xFF8AB8);
        E(L, x + 1.0f, 40.0f + len, 1.6f, 1.4f, 0xFF8AB8);
    }
    for (int i = 0; i < 3; i++) {
        int x = 37 + i * 11;
        R(L, x, 24, x + 2, 36, 0xFFFFFF);
        for (int y = 25; y < 36; y += 3) R(L, x, y, x + 2, y, 0x6AA8FF);
    }
    flush(c, 0x9A4A6A);
    static const uint32_t SPR[4] = { 0x6AC8FF, 0xFFD23F, 0x7AD86A, 0xFF6A6A };
    for (int i = 0; i < 14; i++) D(c, 28 + (i * 13) % 42, 51 + (i * 7) % 6, SPR[i % 4]);
    for (int i = 0; i < 3; i++) {                          /* flickering flames */
        float x = 38 + i * 11, fl = sinf(t * 17 + i * 2.1f) * 0.6f + sinf(t * 23 + i) * 0.4f;
        for (int k = 0; k < 3; k++)
            for (int y = 10; y < 26; y++)
                for (int xx = (int)x - 6; xx <= (int)x + 6; xx++)
                    if (hypotf(xx - x, (y - 19) * 1.2f) < 6 - k * 0) tint(c, xx, y, 0xFFF0A0, 0.04f);
        E(c, x + fl * 0.5f, 20.5f, 2.2f, 3.6f + fl * 0.4f, 0xFF8A1A);
        E(c, x + fl * 0.3f, 21.5f, 1.2f, 2.2f, 0xFFE45A);
    }
}

static void pic_boat(Canvas *c, float t) {
    grad(c, 0x6EC0F5, 0xD8F2FF);
    E(c, 80, 12, 6, 6, 0xFFE14D);
    cloud(c, fmodf(t * 2, 130) - 15, 14, 0.8f);
    for (int y = 48; y < PH; y++) R(c, 0, y, 95, y, mixc(0x3A9AD8, 0x1A5A9A, (y - 48) / 24.0f));
    for (int x = 0; x < PW; x++) {
        if ((int)(x + t * 6) % 12 < 4) D(c, x, 52 + (int)(sinf(x * 0.5f + t) * 1), 0x9AD8FF);
        if ((int)(x - t * 4) % 15 < 3) D(c, x, 62, 0x7AC0F0);
    }
    float b = sinf(t * 1.6f) * 1.5f, lean = sinf(t * 1.6f + 0.6f) * 0.04f;
    Canvas *L = lay();
    quad(L, 22, 48 + b, 74, 48 + b, 66, 58 + b, 30, 58 + b, 0xD84A3A);
    R(L, 25, 51 + (int)b, 71, 52 + (int)b, 0xFFFFFF);
    R(L, 47, 12 + (int)b, 48, 48 + (int)b, 0x8A5A32);
    tri(L, 50, 14 + b, 50, 45 + b, 72, 45 + b, 0xFFFFFF);
    tri(L, 45, 18 + b, 45, 45 + b, 28, 45 + b, 0xF2F2F2);
    float fw = sinf(t * 6);
    tri(L, 48, 10 + b, 48, 15 + b, 56 + fw, 12.5f + b + fw * 0.5f, 0xFFD23F);
    flush_lean(c, 0x2A2A3A, 48 + b, lean);
    for (int x = 0; x < PW; x++)                           /* the water laps over the hull */
        if (sinf(x * 0.45f + t * 2.5f) > 0.2f) D(c, x, 57 + (int)b, 0x3A9AD8);
}

static void pic_frog(Canvas *c, float t) {
    grad(c, 0x9ED8FF, 0xD8F0FF);
    for (int x = 0; x < PW; x += 3) R(c, x, 26 - (x * 7) % 9, x, 32, 0x5A9A4A);
    for (int y = 32; y < PH; y++) R(c, 0, y, 95, y, mixc(0x4AA8D0, 0x2A78A8, (y - 32) / 40.0f));
    for (int i = 0; i < 6; i++) {
        float k = fmodf(t * 0.3f + i / 6.0f, 1.0f);
        int x = (i * 37) % PW, y = 36 + (i * 11) % 30;
        R(c, x - (int)(k * 4), y, x + (int)(k * 4), y, mixc(0x9AD8F0, 0x3A90C0, k));
    }
    E(c, 48, 60, 34, 8, 0x3E8A3A);
    E(c, 48, 59, 33, 7, 0x5CB04A);
    tri(c, 48, 59, 58, 67, 66, 66, 0x3A90C0);
    Canvas *L = lay();
    E(L, 33, 57, 7, 3.5f, 0x5CC040);
    E(L, 63, 57, 7, 3.5f, 0x5CC040);
    E(L, 48, 48, 17, 11, 0x6CCC48);
    E(L, 48, 52, 10, 6.5f, 0xD4F49A);
    E(L, 48, 37, 16, 9, 0x6CCC48);
    E(L, 38, 29, 6.5f, 6.5f, 0x6CCC48);
    E(L, 58, 29, 6.5f, 6.5f, 0x6CCC48);
    float puff = fmodf(t, 2.6f) < 0.8f ? sinf(fmodf(t, 2.6f) / 0.8f * PI_F) : 0;
    if (puff > 0.05f) E(L, 48, 45, 5 + puff * 3, 3 + puff * 2.5f, 0xE8FAC0);
    flush(c, 0x2A5A1A);
    int sh = blink(t, 0.7f);
    for (int k = 0; k < 2; k++) {
        float ex = k ? 58 : 38;
        if (sh) { R(c, (int)ex - 4, 29, (int)ex + 3, 29, 0x2A5A1A); continue; }
        E(c, ex, 29, 4.5f, 4.5f, 0xFFFFFF);
        E(c, ex + 0.5f, 29.5f, 2.2f, 2.8f, 0x1A1A1A);
        D(c, (int)ex - 1, 27, 0xFFFFFF);
    }
    for (int x = -10; x <= 10; x++) D(c, 48 + x, (int)(38 + (100 - x * x) / 40.0f), 0x2A5A1A);
    D(c, 45, 34, 0x2A5A1A); D(c, 51, 34, 0x2A5A1A);
    float fx = 48 + cosf(t * 1.3f) * 34, fy = 12 + sinf(t * 2.6f) * 5;   /* a fly buzzes about */
    D(c, (int)fx, (int)fy, 0x1A1A1A); D(c, (int)fx + 1, (int)fy, 0x1A1A1A);
    int up = (int)(t * 20) % 2;
    D(c, (int)fx, (int)fy - 1 - up, 0xFFFFFF); D(c, (int)fx + 1, (int)fy - 1 - up, 0xE8F4FF);
}

static void star_shape(Canvas *c, float cx, float cy, float r, float rot, uint32_t hex) {
    float xs[10], ys[10];
    for (int i = 0; i < 10; i++) {
        float a = rot + i * PI_F / 5 - PI_F / 2, rr = i % 2 ? r * 0.48f : r;
        xs[i] = cx + cosf(a) * rr;
        ys[i] = cy + sinf(a) * rr;
    }
    fill_poly(c, xs, ys, 10, rgb(hex));
}

static void pic_star(Canvas *c, float t) {
    grad(c, 0x121640, 0x2E2A6A);
    twinkles(c, t, 26, PH);
    float s = 1 + 0.05f * sinf(t * 3), rot = sinf(t * 0.8f) * 0.12f;
    Canvas *L = lay();
    star_shape(L, 48, 38, 30 * s, rot, 0xFFD93D);
    star_shape(L, 49, 39, 24 * s, rot, 0xFFC12E);
    star_shape(L, 47.5f, 37.5f, 23 * s, rot, 0xFFE45A);
    flush(c, 0xB0700A);
    int sh = blink(t, 0.5f);
    eye(c, 42, 34, sh);
    eye(c, 53, 34, sh);
    E(c, 39, 41, 2.2f, 1.4f, 0xFFA07A);
    E(c, 58, 41, 2.2f, 1.4f, 0xFFA07A);
    for (int x = -4; x <= 4; x++) D(c, 48 + x, (int)(41 + (16 - x * x) / 7.0f), 0x7A4A10);
    for (int i = 0; i < 5; i++) {                          /* sparkles around it */
        float k = fmodf(t * 0.7f + i * 0.2f, 1.0f);
        if (k > 0.5f) continue;
        float a = i * 1.3f + 0.4f;
        sparkle(c, (int)(48 + cosf(a) * 38), (int)(38 + sinf(a) * 28), 0xFFE9A0);
    }
}

static void pic_moon(Canvas *c, float t) {
    grad(c, 0x182658, 0x3B3F86);
    twinkles(c, t, 22, 50);
    float ph = fmodf(t, 6.0f);
    if (ph < 0.8f) {                                       /* a shooting star */
        float k = ph / 0.8f, sx = 10 + 40 * k, sy = 4 + 14 * k;
        for (int i = 0; i < 7; i++) D(c, (int)sx - i, (int)(sy - i * 0.4f), mixc(0xFFFFFF, 0x2A3070, i / 7.0f));
    }
    for (int x = 0; x < PW; x++) {
        int hy = 62 - (int)(3 * sinf(x * 0.09f) + 2 * sinf(x * 0.23f + 1));
        R(c, x, hy, x, PH - 1, 0x1A2148);
        D(c, x, hy, 0x2E3A72);
    }
    float Rm = 24, mx = 50, my = 34 + sinf(t * 1.2f) * 1.2f, kx = mx + 13, ky = my - 8, kr = 20;
    for (int y = (int)(my - Rm) - 1; y <= (int)(my + Rm) + 1; y++)
        for (int x = (int)(mx - Rm) - 1; x <= (int)(mx + Rm) + 1; x++) {
            float d = hypotf(x + 0.5f - mx, y + 0.5f - my), e = hypotf(x + 0.5f - kx, y + 0.5f - ky);
            if (d > Rm || e < kr) continue;
            D(c, x, y, d > Rm - 1.3f || e < kr + 1.2f ? 0xE8B83A : 0xFFE27A);
        }
    int ex = (int)(mx - 12), ey = (int)(my - 1);
    D(c, ex - 1, ey, 0x8A6420); D(c, ex, ey + 1, 0x8A6420); D(c, ex + 1, ey + 1, 0x8A6420); D(c, ex + 2, ey, 0x8A6420);
    E(c, ex - 1, ey + 5, 1.6f, 1, 0xFFA0A0);
    int sx = (int)(mx - 8), sy = (int)(my + 10);
    D(c, sx - 1, sy, 0x8A6420); D(c, sx, sy + 1, 0x8A6420); D(c, sx + 1, sy, 0x8A6420);
    for (int i = 0; i < 3; i++) {                          /* sleepy z's */
        float k = fmodf(t * 0.35f + i / 3.0f, 1.0f);
        int zx = (int)(mx + 4 + k * 12), zy = (int)(my - 18 - k * 14);
        uint32_t zc = mixc(0xDDE4FF, 0x283070, k);
        R(c, zx, zy, zx + 3, zy, zc); D(c, zx + 2, zy + 1, zc); D(c, zx + 1, zy + 2, zc); R(c, zx, zy + 3, zx + 3, zy + 3, zc);
    }
}

static void pic_bug(Canvas *c, float t) {
    grad(c, 0xD4F2B8, 0xA8DC8A);
    Canvas *L = lay();
    float xs[6] = { 4, 30, 70, 94, 70, 30 }, ys[6] = { 40, 18, 16, 38, 62, 64 };
    fill_poly(L, xs, ys, 6, rgb(0x5CB84A));
    flush(c, 0x2E6A2A);
    for (int x = 6; x < 92; x++) D(c, x, 40 - (int)((x - 6) * 0.03f), 0x9AE07A);
    for (int i = 0; i < 6; i++) {
        float x = 18 + i * 13;
        ln(c, x, 40, x + 9, 26 + (i % 2) * 2, 0x8AD06A);
        ln(c, x, 40, x + 9, 54 - (i % 2) * 2, 0x8AD06A);
    }
    for (int x = 0; x < 6; x++) D(c, x, 40, 0x6E4422);
    float s = t * 0.45f, bx = 48 + sinf(s) * 22, by = 39;
    int dir = cosf(s) >= 0 ? 1 : -1, stepk = (int)(t * 8) % 2;
    L = lay();
    for (int k = -1; k <= 1; k++) {                        /* six little legs */
        int lx = (int)bx + k * 5, off = (k + stepk) % 2 ? 1 : -1;
        ln(L, lx, by - 7, lx + off, by - 11, 0x1A1A1A);
        ln(L, lx, by + 7, lx - off, by + 11, 0x1A1A1A);
    }
    E(L, bx + dir * 11, by, 5, 5, 0x1A1A1A);
    ln(L, bx + dir * 14, by - 3, bx + dir * 18, by - 7, 0x1A1A1A);
    ln(L, bx + dir * 14, by + 3, bx + dir * 18, by + 7, 0x1A1A1A);
    E(L, bx, by, 11, 9, 0xE23A2A);
    flush(c, 0x3A0A0A);
    R(c, (int)bx, (int)by - 8, (int)bx, (int)by + 8, 0x1A1A1A);
    static const float SP[6][2] = { { -5, -4 }, { -6, 3 }, { 4, -5 }, { 5, 3 }, { -1, -6 }, { 0, 5 } };
    for (int i = 0; i < 6; i++) {
        float sx = SP[i][0] * dir, sy = SP[i][1];
        if (fabsf(sx) < 2) sx = sx < 0 ? -2.5f : 2.5f;
        E(c, bx + sx, by + sy, 1.8f, 1.8f, 0x1A1A1A);
    }
    D(c, (int)(bx - dir * 6), (int)by - 5, 0xFF9A8A);
    D(c, (int)(bx + dir * 12), (int)by - 2, 0xFFFFFF);
    D(c, (int)(bx + dir * 12), (int)by + 2, 0xFFFFFF);
}

static void pic_hat(Canvas *c, float t) {
    grad(c, 0xE4D4F8, 0xC4B0E8);
    for (int i = 0; i < 8; i++) sparkle(c, (i * 37 + 5) % PW, (i * 23 + 4) % 40, 0xF0E8FF);
    R(c, 0, 60, 95, 71, 0xC83A4A);
    R(c, 0, 60, 95, 61, 0xE85A6A);
    for (int x = 2; x < PW; x += 4) R(c, x, 69, x + 1, 71, 0xFFD23F);
    float k = fmodf(t, 7.0f), up = k < 1.5f ? 0 : k < 2.3f ? ease((k - 1.5f) / 0.8f) : k < 5.2f ? 1 : k < 6 ? 1 - ease((k - 5.2f) / 0.8f) : 0;
    float h = up * 16, wig = sinf(t * 6) * up;
    Canvas *L = lay();
    R(L, 32, 26, 64, 55, 0x2A2A3A);
    R(L, 35, 26, 38, 55, 0x40405A);
    R(L, 32, 45, 64, 50, 0xD83A4A);
    R(L, 32, 46, 64, 46, 0xF0606A);
    E(L, 48, 56, 25, 4, 0x2A2A3A);
    E(L, 48, 26, 16, 3.5f, 0x40405A);
    if (h > 0.5f) {                                        /* a bunny peeks out */
        E(L, 43 + wig, 26 - h * 0.55f, 3, h * 0.55f + 1, 0xFFFFFF);
        E(L, 53 - wig, 26 - h * 0.55f, 3, h * 0.55f + 1, 0xFFFFFF);
        E(L, 43 + wig, 26 - h * 0.55f, 1.4f, h * 0.45f, 0xFFB8C8);
        E(L, 53 - wig, 26 - h * 0.55f, 1.4f, h * 0.45f, 0xFFB8C8);
        if (h > 10) E(L, 48, 27, 9, 6 * (h - 10) / 6 + 0.5f, 0xFFFFFF);
    }
    flush(c, 0x14141E);
    E(c, 48, 27, 14, 2.5f, 0x14141E);
    if (h > 10) {
        E(c, 48, 27, 9, 4.5f * (h - 10) / 6, 0xFFFFFF);
        if (h > 13) { eye(c, 44, 23, blink(t, 0.2f)); eye(c, 51, 23, blink(t, 0.2f)); D(c, 48, 26, 0xFF8AA0); }
        R(c, 34, 28, 62, 28, 0x2A2A3A);
        for (int i = 0; i < 4; i++) {
            float a = t * 1.5f + i * PI_F / 2;
            sparkle(c, (int)(48 + cosf(a) * 24), (int)(18 + sinf(a) * 8), 0xFFE27A);
        }
    }
}

static void pic_ball(Canvas *c, float t) {
    grad(c, 0x6EC0F5, 0xD4F0FF);
    E(c, 14, 10, 5, 5, 0xFFE14D);
    for (int y = 34; y < 44; y++) R(c, 0, y, 95, y, mixc(0x3A9AD8, 0x5AB4E8, (y - 34) / 10.0f));
    for (int x = 0; x < PW; x += 7) D(c, x + (int)(t * 3) % 7, 37, 0xFFFFFF);
    for (int y = 44; y < PH; y++) R(c, 0, y, 95, y, mixc(0xF8E0A0, 0xE8C478, (y - 44) / 28.0f));
    for (int i = 0; i < 12; i++) D(c, (i * 29) % PW, 50 + (i * 13) % 20, 0xD8B068);
    float ph = fmodf(t * 0.9f, 1.0f), hgt = 4 * ph * (1 - ph);   /* 0 at the bounce, 1 at the top */
    float ground = 60, R0 = 12, sq = ph < 0.08f || ph > 0.92f ? 0.85f : 1;
    float by = ground - R0 * sq - hgt * 30;
    E(c, 48, ground + 1, 13 * (1 - hgt * 0.4f), 2.5f * (1 - hgt * 0.4f), 0xC8A060);
    static const uint32_t SEG[4] = { 0xE83A3A, 0xFFFFFF, 0x3A7AE8, 0xFFD23F };
    float spin = t * 2;
    for (int y = (int)(by - R0 * sq) - 1; y <= (int)(by + R0 * sq) + 1; y++)
        for (int x = 34; x <= 62; x++) {
            float dx = (x + 0.5f - 48) / (R0 / sq), dy = (y + 0.5f - by) / (R0 * sq);
            if (dx * dx + dy * dy > 1) continue;
            float a = atan2f(dy, dx * 1.6f) + spin;
            int seg = (int)floorf(a / (PI_F / 3));
            seg = ((seg % 4) + 4) % 4;
            D(c, x, y, dx * dx + dy * dy > 0.82f ? shade(SEG[seg], 0.78f) : SEG[seg]);
        }
    E(c, 48, by, 3, 3 * sq, 0xFFFFFF);
    E(c, 43, by - 6 * sq, 2, 1.5f, 0xFFFFFF);
}

static void pic_bed(Canvas *c, float t) {
    grad(c, 0x4A5A9A, 0x34407A);
    for (int x = 4; x < PW; x += 10) for (int y = 4; y < 60; y += 10) D(c, x + (y / 10) % 2 * 5, y, 0x6A7AB8);
    R(c, 62, 6, 86, 26, 0xC8A070);                         /* a window with the moon */
    R(c, 64, 8, 84, 24, 0x1A2050);
    E(c, 78, 13, 3.5f, 3.5f, 0xFFE27A);
    E(c, 80, 12, 3, 3, 0x1A2050);
    D(c, 68, 11, 0xFFFFFF); D(c, 71, 19, 0xFFFFFF);
    R(c, 74, 8, 74, 24, 0xC8A070);
    R(c, 0, 62, 95, 71, 0x8A5A3A);
    R(c, 0, 62, 95, 62, 0xA8743E);
    float br = sinf(t * 1.6f);
    Canvas *L = lay();
    fill_round_rect(L, 10, 26, 20, 64, 4, rgb(0xB0763A));
    fill_round_rect(L, 78, 40, 86, 64, 3, rgb(0xB0763A));
    R(L, 18, 46, 80, 56, 0xFFFFFF);
    E(L, 28, 44, 9, 4, 0xFFFFFF);
    E(L, 30, 38, 6, 5.5f, 0xB0763A);                       /* teddy, asleep */
    E(L, 25, 33, 2.6f, 2.6f, 0xB0763A);
    E(L, 35, 33, 2.6f, 2.6f, 0xB0763A);
    E(L, 30, 40, 3, 2, 0xE8C090);
    R(L, 34, 43 - (int)(br > 0), 80, 56, 0x5A8AE8);
    R(L, 34, 43 - (int)(br > 0), 80, 44 - (int)(br > 0), 0xFFFFFF);
    R(L, 12, 64, 13, 67, 0x6E4422);
    R(L, 83, 64, 84, 67, 0x6E4422);
    flush(c, 0x1E1A30);
    for (int i = 0; i < 9; i++) D(c, 38 + (i * 7) % 40, 47 + (i * 3) % 8, 0xFFE27A);
    R(c, 27, 37, 28, 37, 0x3A2010);
    R(c, 32, 37, 33, 37, 0x3A2010);
    D(c, 30, 39, 0x3A2010);
    for (int i = 0; i < 3; i++) {
        float k = fmodf(t * 0.35f + i / 3.0f, 1.0f);
        int zx = (int)(36 + k * 14), zy = (int)(30 - k * 22);
        uint32_t zc = mixc(0xFFFFFF, 0x4A5A9A, k);
        R(c, zx, zy, zx + 3, zy, zc); D(c, zx + 2, zy + 1, zc); D(c, zx + 1, zy + 2, zc); R(c, zx, zy + 3, zx + 3, zy + 3, zc);
    }
}

static void wheel(Canvas *c, float x, float y, float r, float spin) {
    E(c, x, y, r, r, 0x222228);
    E(c, x, y, r * 0.5f, r * 0.5f, 0xB8BCC8);
    D(c, (int)(x + cosf(spin) * r * 0.75f), (int)(y + sinf(spin) * r * 0.75f), 0x6A6A78);
    D(c, (int)(x - cosf(spin) * r * 0.75f), (int)(y - sinf(spin) * r * 0.75f), 0x6A6A78);
}

/* A road scrolling left under a vehicle, with trees going by behind. */
static void road(Canvas *c, float t, int top) {
    sky(c);
    cloud(c, fmodf(-t * 3, 130) + 115, 10, 0.9f);
    for (int x = 0; x < PW; x++) {
        int hy = top - 8 - (int)(4 * sinf((x + t * 4) * 0.06f));
        R(c, x, hy, x, top - 1, 0x8AD07A);
    }
    for (int i = 0; i < 4; i++) {
        float tx = fmodf(i * 34 - t * 18, 136);
        if (tx < 0) tx += 136;
        tx -= 20;
        R(c, (int)tx, top - 14, (int)tx + 1, top - 2, 0x8A5A32);
        E(c, tx + 1, top - 17, 6, 7, 0x4FAE4A);
        E(c, tx - 1, top - 19, 3, 3, 0x6CC65A);
    }
    R(c, 0, top, 95, PH - 1, 0x5A5A64);
    R(c, 0, top, 95, top, 0x8A8A94);
    for (int x = 0; x < PW; x++)
        if ((int)(x + t * 40) % 14 < 7) D(c, x, top + (PH - top) / 2, 0xFFE27A);
}

static void pic_car(Canvas *c, float t) {
    road(c, t, 56);
    float b = (int)(t * 6) % 2 ? 0.5f : 0;
    for (int i = 0; i < 3; i++) {                          /* puffs from the exhaust */
        float k = fmodf(t * 1.2f + i / 3.0f, 1.0f);
        E(c, 20 - k * 14, 51 - k * 6, 1 + k * 3, 1 + k * 3, mixc(0xE8E8E8, 0xB8D8F0, k));
    }
    Canvas *L = lay();
    fill_round_rect(L, 20, 38 + (int)b, 76, 52 + (int)b, 4, rgb(0xE03A3A));
    quad(L, 32, 39 + b, 40, 27 + b, 60, 27 + b, 68, 39 + b, 0xE03A3A);
    quad(L, 35, 38 + b, 41, 30 + b, 49, 30 + b, 49, 38 + b, 0xA8DCFF);
    quad(L, 52, 38 + b, 52, 30 + b, 59, 30 + b, 65, 38 + b, 0xA8DCFF);
    R(L, 21, 44 + (int)b, 75, 44 + (int)b, 0xB82A2A);
    E(L, 74, 42 + b, 2, 1.6f, 0xFFE27A);
    R(L, 20, 41 + (int)b, 21, 43 + (int)b, 0xFF8A3A);
    R(L, 46, 41 + (int)b, 49, 41 + (int)b, 0x8A1A1A);
    flush(c, 0x3A1010);
    D(c, 38, 32 + (int)b, 0xFFFFFF);
    D(c, 55, 32 + (int)b, 0xFFFFFF);
    wheel(c, 32, 53, 6, -t * 12);
    wheel(c, 64, 53, 6, -t * 12);
}

static void pic_bus(Canvas *c, float t) {
    road(c, t, 58);
    float b = (int)(t * 5) % 2 ? 0.5f : 0;
    static const uint32_t KID[4] = { 0xF0C29C, 0x8D5A3A, 0xD9A070, 0xF8D8BC };
    static const uint32_t HAIR[4] = { 0x6B3E1E, 0x141414, 0xD9B05A, 0xB04A1E };
    Canvas *L = lay();
    fill_round_rect(L, 8, 24 + (int)b, 88, 54 + (int)b, 4, rgb(0xF5C02E));
    R(L, 8, 44 + (int)b, 88, 45 + (int)b, 0x2A2A2A);
    R(L, 8, 48 + (int)b, 88, 48 + (int)b, 0x2A2A2A);
    for (int i = 0; i < 5; i++) R(L, 12 + i * 12, 28 + (int)b, 21 + i * 12, 38 + (int)b, 0xA8DCFF);
    R(L, 74, 28 + (int)b, 84, 52 + (int)b, 0xA8DCFF);         /* the door at the front */
    R(L, 79, 28 + (int)b, 79, 52 + (int)b, 0x2A2A2A);
    E(L, 87, 40 + b, 1.5f, 2, 0xFFFFFF);
    R(L, 6, 50 + (int)b, 9, 52 + (int)b, 0x3A3A3A);
    flush(c, 0x3A2A08);
    for (int i = 0; i < 5; i++) {                          /* kids at the windows */
        int x = 16 + i * 12, y = 34 + (int)b + ((int)(t * 3 + i) % 3 == 0 ? -1 : 0);
        E(c, x + 0.5f, y + 0.5f, 3, 3, KID[i % 4]);
        R(c, x - 2, y - 3, x + 3, y - 2, HAIR[i % 4]);
        D(c, x - 1, y, 0x1E1410); D(c, x + 2, y, 0x1E1410);
        R(c, x - 3, y + 3, x + 4, y + 4, (uint32_t[]){ 0xD94F4F, 0x3F7FD6, 0x4FB86A, 0xB45FD0 }[i % 4]);
    }
    wheel(c, 24, 56, 6, -t * 10);
    wheel(c, 72, 56, 6, -t * 10);
}

static void pic_pig(Canvas *c, float t) {
    sky(c);
    cloud(c, fmodf(t * 2 + 60, 130) - 15, 12, 0.9f);
    for (int x = 0; x < PW; x += 8) { R(c, x, 34, x + 1, 50, 0xC8925A); }
    R(c, 0, 37, 95, 38, 0xC8925A);
    R(c, 0, 43, 95, 44, 0xC8925A);
    grass(c, 48, 0x7CC85A);
    E(c, 48, 63, 38, 6, 0x8A5A3A);
    E(c, 48, 62, 36, 4.5f, 0xA8744A);
    Canvas *L = lay();
    float curl = sinf(t * 7) > 0 ? 1 : 0;
    for (int i = 0; i < 10; i++) {                         /* the curly tail */
        float a = i * 0.7f + curl * 0.6f;
        D(L, (int)(22 - i * 0.4f + cosf(a) * 2.2f), (int)(40 + sinf(a) * 2.2f), 0xF2A0B4);
        D(L, (int)(23 - i * 0.4f + cosf(a) * 2.2f), (int)(40 + sinf(a) * 2.2f), 0xF2A0B4);
    }
    for (int i = 0; i < 4; i++) {
        int lx = 30 + i * 10 + (i > 1 ? 2 : 0), lift = ((int)(t * 2) + i) % 4 == 0 ? 1 : 0;
        R(L, lx, 52, lx + 4, 60 - lift, 0xF5A8B8);
        R(L, lx, 59 - lift, lx + 4, 60 - lift, 0x8A4A5A);
    }
    E(L, 46, 44, 22, 13, 0xF5A8B8);
    E(L, 70, 38, 11, 10, 0xF5A8B8);
    tri(L, 64, 31, 66, 23, 72, 30, 0xF5A8B8);
    tri(L, 71, 30, 76, 24, 77, 33, 0xE8889C);
    E(L, 80, 41, 4.5f, 5, 0xF0909E);
    E(L, 46, 49, 15, 5, 0xFFC0CC);
    E(L, 38, 40, 4, 3, 0xA8744A);
    E(L, 56, 50, 3, 2, 0xA8744A);
    flush(c, 0x9A4A5A);
    D(c, 80, 39, 0x9A4A5A); D(c, 81, 42, 0x9A4A5A);
    eye(c, 72, 34, blink(t, 1.6f));
    ln(c, 72, 45, 76, 46, 0x9A4A5A);
}

static void pic_hen(Canvas *c, float t) {
    sky(c);
    R(c, 62, 18, 95, 50, 0xC8443A);                        /* the barn */
    tri(c, 58, 20, 78, 6, 99, 20, 0x8A2A2A);
    R(c, 72, 30, 86, 50, 0xF4E8D8);
    R(c, 74, 32, 84, 50, 0x8A2A2A);
    ln(c, 74, 32, 84, 50, 0xF4E8D8); ln(c, 84, 32, 74, 50, 0xF4E8D8);
    grass(c, 50, 0x8AC85A);
    for (int y = 56; y < PH; y++) R(c, 0, y, 60, y, 0xD8B878);
    for (int i = 0; i < 10; i++) D(c, 8 + (i * 7) % 30, 62 + (i * 3) % 6, 0xFFE27A);
    float k = fmodf(t, 2.4f), peck = k < 0.6f ? sinf(k / 0.6f * PI_F) : 0;
    float hx = 30 - peck * 4, hy = 32 + peck * 22;
    Canvas *L = lay();
    tri(L, 56, 44, 66, 26, 68, 44, 0x8A4A1A);              /* tail feathers */
    tri(L, 54, 46, 62, 30, 64, 46, 0xA85A24);
    R(L, 40, 54, 41, 62, 0xF0A030);
    R(L, 48, 54, 49, 62, 0xF0A030);
    R(L, 38, 62, 42, 62, 0xF0A030);
    R(L, 46, 62, 50, 62, 0xF0A030);
    E(L, 46, 45, 15, 11, 0xC8783A);
    brush(L, 38, 40, hx + 2, hy + 2, 4.5f, 0xC8783A);
    E(L, hx, hy, 6, 6, 0xC8783A);
    E(L, 48, 44, 9, 6, 0xA85A24);
    tri(L, hx - 3, hy - 5, hx - 1, hy - 10, hx + 1, hy - 5, 0xE83030);
    tri(L, hx, hy - 5, hx + 2, hy - 10, hx + 4, hy - 5, 0xE83030);
    tri(L, hx - 6, hy - 1, hx - 11, hy + 1, hx - 6, hy + 2, 0xFFB030);
    E(L, hx - 4, hy + 5, 1.5f, 2.5f, 0xE83030);
    flush(c, 0x4A2A10);
    eye(c, (int)hx - 3, (int)hy - 2, blink(t, 0.9f));
    for (int i = 0; i < 3; i++) R(c, 44 + i * 3, 42 + i, 46 + i * 3, 42 + i, 0x8A4A1A);
}

static void pic_egg(Canvas *c, float t) {
    grad(c, 0xF8E4B0, 0xF0D090);
    for (int i = 0; i < 30; i++) ln(c, (i * 17) % PW, 52 + (i * 7) % 20, (i * 17) % PW + 5, 51 + (i * 7) % 20 + (i % 3), 0xE0B860);
    float k = fmodf(t, 9.0f);
    float wob = (k > 1 && k < 2.2f) || (k > 3.2f && k < 4.4f) ? sinf(k * 18) * 0.12f : 0;
    int cracked = k > 4.4f, open = k > 5.4f && k < 8.2f;
    float lift = open ? ease((k - 5.4f) / 0.4f) * (k > 7.8f ? 1 - ease((k - 7.8f) / 0.4f) : 1) : 0;
    E(c, 48, 60, 30, 9, 0x9A6A30);                         /* the straw nest */
    E(c, 48, 58, 26, 6, 0x6A4A20);
    Canvas *L = lay();
    if (open && lift > 0.5f) {                             /* a chick peeks out */
        E(L, 48, 30, 9, 7, 0xFFD83A);
        E(L, 46, 26, 3, 2, 0xFFE87A);
    }
    for (int y = 18; y <= 58; y++) {                       /* the egg, tipping as it wobbles */
        float v = (y - 40) / (y < 40 ? 22.0f : 18.0f), hw = 15 * sqrtf(fmaxf(0, 1 - v * v));
        if (y < 40) hw *= 0.92f;
        if (hw < 0.5f) continue;
        float dx = (58 - y) * wob;
        int dy = cracked && y < 34 ? (int)(lift * 9) : 0;  /* the top of the shell lifts off */
        for (int x = (int)(48 + dx - hw); x <= (int)(48 + dx + hw); x++)
            put(L, x, y - dy, rgb(x > 48 + dx + hw * 0.4f ? 0xF0E4D0 : 0xFFF8EC));
    }
    flush(c, 0x8A6A3A);
    E(c, 41, 30, 2.5f, 4, 0xFFFFFF);
    if (cracked) {
        float dx = (58 - 34) * wob;
        for (int i = 0; i < 8; i++) {
            int x = (int)(36 + dx) + i * 3;
            ln(c, x, 34 + (i % 2) * 2, x + 3, 34 + ((i + 1) % 2) * 2, 0x8A6A3A);
        }
    }
    if (open && lift > 0.5f) {
        eye(c, 44, 28, blink(t, 0.3f));
        eye(c, 51, 28, blink(t, 0.3f));
        tri(c, 46, 31, 50, 31, 48, 33.5f, 0xFF8A1A);
    }
}

static void pic_duck(Canvas *c, float t) {
    grad(c, 0x9ED8FF, 0xD8F0FF);
    cloud(c, fmodf(t * 2 + 20, 130) - 15, 10, 0.8f);
    for (int x = 0; x < PW; x += 2) R(c, x, 30 - (x * 5) % 7, x, 36, (x / 2) % 3 ? 0x5A9A4A : 0x4A8A3A);
    for (int x = 4; x < PW; x += 23) { R(c, x, 22, x + 1, 30, 0x5A9A4A); E(c, x + 0.5f, 22, 1.5f, 4, 0x8A5A2A); }
    for (int y = 36; y < PH; y++) R(c, 0, y, 95, y, mixc(0x5AB0E0, 0x2A78B0, (y - 36) / 36.0f));
    float b = sinf(t * 2) * 1.2f, dx = sinf(t * 0.4f) * 6;
    for (int i = 0; i < 3; i++) {                          /* ripples */
        float k = fmodf(t * 0.4f + i / 3.0f, 1.0f);
        float rx = 18 + k * 26, ry = 3 + k * 4;
        for (int a = 0; a < 40; a++) {
            float an = a * PI_F / 20;
            if (sinf(an) < 0) continue;
            D(c, (int)(48 + dx + cosf(an) * rx), (int)(54 + sinf(an) * ry), mixc(0xC8ECFF, 0x4A9ACA, k));
        }
    }
    Canvas *L = lay();
    tri(L, 30 + dx, 44 + b, 26 + dx, 34 + b, 38 + dx, 42 + b, 0xFFD23F);
    E(L, 46 + dx, 47 + b, 17, 9, 0xFFD23F);
    E(L, 60 + dx, 30 + b, 8.5f, 8, 0xFFD23F);
    brush(L, 56 + dx, 38 + b, 58 + dx, 42 + b, 4, 0xFFD23F);
    E(L, 69 + dx, 32 + b, 5, 2.2f, 0xFF8A1A);
    E(L, 44 + dx, 45 + b, 9, 5, 0xF2BC2E);
    flush(c, 0x8A5A08);
    R(c, 64 + (int)dx, 33 + (int)b, 72 + (int)dx, 33 + (int)b, 0xC85A0A);
    eye(c, 62 + (int)dx, 27 + (int)b, blink(t, 1.3f));
    D(c, 57 + (int)dx, 26 + (int)b, 0xFFF0A0);
    for (int x = 0; x < PW; x++)                           /* the water over its tummy */
        if (x > 26 + dx && x < 66 + dx && sinf(x * 0.6f + t * 3) > -0.2f) R(c, x, 54 + (int)b, x, 56 + (int)b, 0x5AB0E0);
    R(c, (int)(26 + dx), 54 + (int)b, (int)(66 + dx), 54 + (int)b, 0xC8ECFF);
}

static void pic_bee(Canvas *c, float t) {
    sky(c);
    grass(c, 58, 0x6CC25A);
    R(c, 74, 34, 75, 60, 0x3E8A3A);                        /* a big flower */
    E(c, 80, 48, 5, 2, 0x4FAE4A);
    for (int i = 0; i < 8; i++) {
        float a = i * PI_F / 4;
        E(c, 74.5f + cosf(a) * 7, 26 + sinf(a) * 7, 4.5f, 4.5f, 0xFF7AB0);
    }
    E(c, 74.5f, 26, 5, 5, 0xFFC23A);
    E(c, 73.5f, 25, 2, 2, 0xFFE27A);
    float u = t * 1.1f, bx = 38 + sinf(u) * 24, by = 30 + sinf(u * 2) * 10;
    int dir = cosf(u) >= 0 ? 1 : -1;
    for (int i = 1; i <= 6; i++) {                         /* its dotted path */
        float uu = u - i * 0.12f;
        if (i % 2) D(c, (int)(38 + sinf(uu) * 24), (int)(30 + sinf(uu * 2) * 10), 0xFFFFFF);
    }
    int flap = (int)(t * 24) % 2;
    Canvas *L = lay();
    E(L, bx - dir * 2, by - 8 - flap, 5, 4 - flap, 0xE8F6FF);
    E(L, bx + dir * 3, by - 7 + flap, 4, 3 + flap, 0xFFFFFF);
    tri(L, bx - dir * 9, by, bx - dir * 13, by + 1, bx - dir * 9, by + 2, 0x2A2A2A);
    E(L, bx, by, 10, 7, 0xFFD23F);
    for (int k = -1; k <= 1; k += 2) R(L, (int)(bx + k * 3) - 1, (int)by - 7, (int)(bx + k * 3) + 1, (int)by + 7, 0x2A2A2A);
    E(L, bx + dir * 10, by, 5, 5, 0x2A2A2A);
    ln(L, bx + dir * 12, by - 4, bx + dir * 14, by - 9, 0x2A2A2A);
    flush(c, 0x2A2008);
    D(c, (int)(bx + dir * 12), (int)by - 1, 0xFFFFFF);
    D(c, (int)(bx + dir * 11), (int)by + 2, 0xFF9AA0);
}

static void pic_kite(Canvas *c, float t) {
    grad(c, 0x5AB4F0, 0xD0EEFF);
    cloud(c, fmodf(t * 3, 130) - 15, 12, 1);
    cloud(c, fmodf(t * 2 + 70, 130) - 15, 30, 0.7f);
    for (int x = 0; x < PW; x++) {
        int hy = 58 - (int)(sinf(x * 0.05f + 0.4f) * 5);
        R(c, x, hy, x, PH - 1, 0x6CC25A);
        D(c, x, hy, 0x8FDB72);
    }
    float kx = 54 + sinf(t * 0.8f) * 6, ky = 26 + sinf(t * 1.3f) * 4, a = sinf(t * 0.9f) * 0.15f;
    for (int i = 0; i <= 40; i++) {                        /* the string */
        float u = i / 40.0f;
        D(c, (int)lerp(10, kx, u), (int)(lerp(66, ky + 14, u) + sinf(u * PI_F) * 5), 0x5A4A3A);
    }
    for (int i = 1; i <= 9; i++) {                         /* the tail with its bows */
        float tx = kx + sinf(t * 4 - i * 0.7f) * 3 - i * 1.2f, ty = ky + 15 + i * 2.6f;
        D(c, (int)tx, (int)ty, 0x5A4A3A);
        if (i % 3 == 0) { tri(c, tx - 3, ty - 2, tx, ty, tx - 3, ty + 2, 0xFF5A8A); tri(c, tx + 3, ty - 2, tx, ty, tx + 3, ty + 2, 0xFF5A8A); }
    }
    Canvas *L = lay();
    rpoly(L, kx, ky, a, (float[]){ 0, 0, -11 }, (float[]){ 0, -16, 0 }, 3, 0xE83A3A);
    rpoly(L, kx, ky, a, (float[]){ 0, 0, 11 }, (float[]){ 0, -16, 0 }, 3, 0xFFD23F);
    rpoly(L, kx, ky, a, (float[]){ 0, -11, 0 }, (float[]){ 0, 0, 16 }, 3, 0x3A7AE8);
    rpoly(L, kx, ky, a, (float[]){ 0, 11, 0 }, (float[]){ 0, 0, 16 }, 3, 0x4FB84A);
    flush(c, 0x2A2A3A);
    for (int i = -15; i <= 15; i++) D(c, (int)(kx - sinf(a) * i), (int)(ky + cosf(a) * i), 0x7A5A3A);
    for (int i = -10; i <= 10; i++) D(c, (int)(kx + cosf(a) * i), (int)(ky + sinf(a) * i), 0x7A5A3A);
}

static void pic_rain(Canvas *c, float t) {
    grad(c, 0x8A9AB8, 0xC0CCE0);
    R(c, 0, 58, 95, 71, 0x6A9A5A);
    R(c, 0, 58, 95, 58, 0x8ABA6A);
    for (int i = 0; i < 3; i++) {                          /* puddles, rippling */
        float px = 18 + i * 30;
        E(c, px, 64, 11, 2.5f, 0x7A9AC8);
        float k = fmodf(t * 1.3f + i * 0.37f, 1.0f);
        for (int a = 0; a < 30; a++) {
            float an = a * PI_F / 15;
            D(c, (int)(px + cosf(an) * k * 9), (int)(64 + sinf(an) * k * 2), mixc(0xFFFFFF, 0x7A9AC8, k));
        }
    }
    for (int i = 0; i < 30; i++) {                         /* raindrops */
        float sp = 36 + (i % 3) * 8, x = 12 + (i * 23) % 74;
        float y = fmodf(i * 11 + t * sp, 44.0f) + 22;
        if (y > 60) continue;
        R(c, (int)x, (int)y - 2, (int)x, (int)y, 0x8AC0FF);
        R(c, (int)x - 1, (int)y + 1, (int)x + 1, (int)y + 2, 0x3A78D8);
    }
    float b = sinf(t * 1.2f) * 1;
    Canvas *L = lay();
    E(L, 30, 18 + b, 13, 10, 0xE4E8F0);
    E(L, 50, 13 + b, 16, 12, 0xF4F6FA);
    E(L, 68, 18 + b, 13, 10, 0xE4E8F0);
    R(L, 22, 18 + (int)b, 74, 26 + (int)b, 0xE4E8F0);
    E(L, 48, 24 + b, 28, 5, 0xD0D6E4);
    flush(c, 0x5A6A8A);
    eye(c, 43, 16 + (int)b, blink(t, 2.4f));
    eye(c, 54, 16 + (int)b, blink(t, 2.4f));
    for (int x = -3; x <= 3; x++) D(c, 49 + x, 21 + (int)b + (9 - x * x) / 5, 0x5A6A8A);
    E(c, 39, 21 + b, 2, 1.2f, 0xFFB8C8);
    E(c, 60, 21 + b, 2, 1.2f, 0xFFB8C8);
}

static void pic_snow(Canvas *c, float t) {
    grad(c, 0x9EC0E8, 0xE0EEFF);
    for (int x = 0; x < PW; x++) {
        int h1 = 44 - (int)(6 * sinf(x * 0.05f + 1)), h2 = 56 - (int)(3 * sinf(x * 0.08f));
        R(c, x, h1, x, PH - 1, 0xDCE8F8);
        R(c, x, h2, x, PH - 1, 0xFFFFFF);
        D(c, x, h2, 0xE8F0FF);
    }
    for (int i = 0; i < 4; i++) {                          /* pines on the far hill */
        int px = 6 + i * 26 + (i > 1 ? 30 : 0);
        if (px > 90) continue;
        tri(c, px, 26, px - 6, 44, px + 6, 44, 0x3A7A5A);
        tri(c, px, 26, px - 3, 33, px + 3, 33, 0xFFFFFF);
    }
    Canvas *L = lay();
    E(L, 48, 56, 13, 10, 0xFFFFFF);
    E(L, 48, 40, 10, 8, 0xFFFFFF);
    E(L, 48, 26, 8, 7, 0xFFFFFF);
    E(L, 52, 58, 8, 6, 0xE4ECF8);
    float wave = sinf(t * 3) * 2;
    ln(L, 38, 38, 28, 30 + wave, 0x6E4422);
    ln(L, 58, 38, 68, 32, 0x6E4422);
    ln(L, 28, 30 + wave, 25, 28 + wave, 0x6E4422);
    R(L, 39, 32, 57, 34, 0xE83A3A);
    R(L, 52, 33, 55, 40, 0xE83A3A);
    R(L, 41, 15, 55, 17, 0x2A2A3A);
    R(L, 43, 7, 53, 16, 0x2A2A3A);
    R(L, 43, 13, 53, 14, 0x3A7AE8);
    flush(c, 0x5A6A8A);
    eye(c, 45, 23, blink(t, 0.6f));
    eye(c, 50, 23, blink(t, 0.6f));
    tri(c, 48, 26, 48, 28, 55, 27, 0xFF8A1A);
    for (int x = -2; x <= 2; x++) D(c, 48 + x, 30 + (x * x) / 3, 0x2A2A3A);
    for (int i = 0; i < 3; i++) R(c, 48, 38 + i * 4, 49, 39 + i * 4, 0x2A2A3A);
    for (int i = 0; i < 30; i++) {                         /* snowflakes drifting down */
        float x = fmodf(i * 31 + sinf(t + i) * 4, (float)PW), y = fmodf(i * 13 + t * (8 + i % 4 * 3), (float)PH);
        D(c, (int)x, (int)y, 0xFFFFFF);
        if (i % 4 == 0) { D(c, (int)x - 1, (int)y, 0xF0F6FF); D(c, (int)x + 1, (int)y, 0xF0F6FF); D(c, (int)x, (int)y - 1, 0xF0F6FF); D(c, (int)x, (int)y + 1, 0xF0F6FF); }
    }
}

static void pic_cup(Canvas *c, float t) {
    for (int y = 0; y < 58; y++)
        for (int x = 0; x < PW; x++) D(c, x, y, ((x / 8) + (y / 8)) % 2 ? 0xFFF4DC : 0xF4E0C0);
    R(c, 0, 58, 95, 71, 0xA8743E);
    R(c, 0, 58, 95, 59, 0xC8925A);
    E(c, 46, 60, 22, 3, 0x8A5A2A);
    for (int i = 0; i < 3; i++) {                          /* curly steam */
        for (int y = 4; y < 26; y++) {
            float k = (26 - y) / 22.0f, x = 38 + i * 8 + sinf(y * 0.35f - t * 3 + i * 2) * 2.5f;
            if (fmodf(y + t * 6 + i * 4, 10) < 7) tint(c, (int)x, y, 0xFFFFFF, 0.75f * (1 - k * 0.8f));
        }
    }
    Canvas *L = lay();
    for (int a = 0; a <= 20; a++) {
        float an = -PI_F / 2 + a * PI_F / 20;
        E(L, 62 + cosf(an) * 8, 43 + sinf(an) * 8, 2.2f, 2.2f, 0xD84A4A);
    }
    fill_round_rect(L, 28, 28, 64, 60, 4, rgb(0xD84A4A));
    R(L, 31, 30, 33, 56, 0xE8706A);
    E(L, 46, 29, 18, 3.5f, 0xF06A64);
    E(L, 46, 29.5f, 15, 2.5f, 0x7A4A2A);
    flush(c, 0x6A1A1A);
    R(c, 40, 28, 42, 30, 0xFFFFFF);
    R(c, 49, 29, 51, 30, 0xFFF0F4);
    R(c, 44, 30, 46, 31, 0xFFFFFF);
    E(c, 46, 45, 6, 5, 0xFFFFFF);                          /* a heart on the mug */
    E(c, 43.5f, 43, 3, 3, 0xFF8AB0);
    E(c, 48.5f, 43, 3, 3, 0xFF8AB0);
    tri(c, 40.5f, 44, 51.5f, 44, 46, 50, 0xFF8AB0);
}

static void pic_box(Canvas *c, float t) {
    grad(c, 0xC8E0D0, 0xA8C8B8);
    R(c, 0, 58, 95, 71, 0xC8925A);
    for (int x = 0; x < 96; x += 14) R(c, x, 59, x, 71, 0xA8743E);
    R(c, 0, 58, 95, 58, 0xA8743E);
    float k = fmodf(t, 6.5f), up = k < 1 ? 0 : k < 1.6f ? ease((k - 1) / 0.6f) : k < 5 ? 1 : k < 5.6f ? 1 - ease((k - 5) / 0.6f) : 0;
    Canvas *L = lay();
    quad(L, 28, 34, 34, 22, 66, 22, 72, 34, 0x8A5A2A);     /* the back flap and inside */
    quad(L, 26, 36, 70, 36, 76, 30, 32, 30, 0x5A3A1A);
    if (up > 0.02f) {                                      /* a kitten pops up */
        float ky = 36 - up * 12;
        tri(L, 41, ky - 4, 42, ky - 11, 46, ky - 6, 0x9AA0AA);
        tri(L, 52, ky - 6, 56, ky - 11, 57, ky - 4, 0x9AA0AA);
        E(L, 49, ky, 9, 7, 0x9AA0AA);
        E(L, 49, ky + 3, 4, 2.5f, 0xE4E8EE);
        if (up > 0.6f) { E(L, 42, 36, 2.5f, 2, 0xE4E8EE); E(L, 56, 36, 2.5f, 2, 0xE4E8EE); }
    }
    R(L, 26, 36, 70, 62, 0xC8925A);
    R(L, 26, 36, 70, 37, 0xDCA86A);
    quad(L, 70, 36, 78, 30, 78, 56, 70, 62, 0xA8743E);
    quad(L, 26, 36, 14, 44, 16, 46, 26, 40, 0xDCA86A);     /* open flaps */
    quad(L, 70, 36, 84, 40, 82, 42, 70, 39, 0xB88450);
    R(L, 46, 37, 50, 62, 0xE8D0A0);
    flush(c, 0x4A2A10);
    if (up > 0.3f) {
        float ky = 36 - up * 12;
        eye(c, 45, (int)ky - 2, blink(t, 0.8f));
        eye(c, 52, (int)ky - 2, blink(t, 0.8f));
        D(c, 49, (int)ky + 1, 0xFF8AA0);
        R(c, 35, (int)ky + 2, 41, (int)ky + 1, 0x6A6A78);
        R(c, 57, (int)ky + 1, 63, (int)ky + 2, 0x6A6A78);
    }
}

static void pic_fox(Canvas *c, float t) {
    grad(c, 0xA8D8F0, 0xE0F4E0);
    for (int i = 0; i < 6; i++) {                          /* the forest behind */
        int px = 4 + i * 18;
        tri(c, px, 8 + (i % 2) * 6, px - 9, 52, px + 9, 52, i % 2 ? 0x3A7A4A : 0x2E6A3E);
    }
    grass(c, 54, 0x7CC25A);
    float sw = sinf(t * 1.6f);
    Canvas *L = lay();
    for (int i = 0; i <= 14; i++) {                        /* the big bushy tail */
        float u = i / 14.0f;
        float x = 60 + u * 18 + sw * u * 3, y = 60 - u * 20 + u * u * 4;
        float r = 3 + sinf(u * PI_F) * 4;
        E(L, x, y, r, r, i > 11 ? 0xFFF4E8 : 0xF07A2A);
    }
    E(L, 52, 48, 13, 12, 0xF07A2A);
    E(L, 44, 48, 5, 9, 0xFFF4E8);
    R(L, 40, 54, 43, 64, 0xF07A2A);
    R(L, 49, 54, 52, 64, 0xF07A2A);
    R(L, 40, 61, 43, 64, 0x3A2010);
    R(L, 49, 61, 52, 64, 0x3A2010);
    E(L, 40, 30, 10, 9, 0xF07A2A);
    tri(L, 32, 28, 22, 34, 34, 37, 0xF07A2A);
    tri(L, 32, 33, 22, 34, 36, 38, 0xFFF4E8);
    tri(L, 33, 24, 35, 12, 41, 22, 0xF07A2A);
    tri(L, 42, 22, 47, 11, 49, 24, 0xF07A2A);
    tri(L, 34, 18, 35, 12, 38, 16, 0x3A2010);
    tri(L, 45, 16, 47, 11, 48, 17, 0x3A2010);
    E(L, 44, 34, 5, 4, 0xFFF4E8);
    flush(c, 0x5A2A08);
    E(c, 22.5f, 34, 1.6f, 1.4f, 0x1A1A1A);
    eye(c, 35, 27, blink(t, 1.9f));
}

static void pic_cow(Canvas *c, float t) {
    sky(c);
    cloud(c, fmodf(t * 2 + 50, 130) - 15, 10, 0.9f);
    for (int x = 2; x < PW; x += 12) R(c, x, 30, x + 1, 46, 0xFFFFFF);
    R(c, 0, 33, 95, 34, 0xFFFFFF);
    R(c, 0, 39, 95, 40, 0xFFFFFF);
    grass(c, 46, 0x7CC85A);
    float chew = sinf(t * 6) > 0 ? 1 : 0, sw = sinf(t * 2.2f);
    Canvas *L = lay();
    ln(L, 76, 32, 82 + sw * 2, 46, 0xF4F4F4);
    E(L, 82 + sw * 2, 48, 2, 2.5f, 0x2A2A2A);
    for (int i = 0; i < 4; i++) {
        int lx = 30 + i * 12 + (i > 1 ? 4 : 0);
        R(L, lx, 46, lx + 4, 60, 0xF4F4F4);
        R(L, lx, 58, lx + 4, 60, 0x3A3A3A);
    }
    fill_round_rect(L, 26, 26, 78, 50, 8, rgb(0xF8F8F8));
    E(L, 58, 51, 5, 3, 0xFFA8B8);
    E(L, 22, 32, 9, 10, 0xF8F8F8);
    E(L, 18 + chew * 0.5f, 40, 7, 5, 0xFFA8B8);
    tri(L, 16, 23, 13, 17, 19, 22, 0xF4E8C8);
    tri(L, 26, 22, 29, 16, 28, 23, 0xF4E8C8);
    E(L, 12, 27, 4, 2, 0xF4F4F4);
    E(L, 31, 26, 4, 2, 0x2A2A2A);
    flush(c, 0x2A2A2A);
    static const float SP[5][3] = { { 36, 32, 6 }, { 52, 40, 7 }, { 66, 30, 5 }, { 44, 46, 3.5f }, { 74, 42, 4 } };
    for (int i = 0; i < 5; i++)
        for (int y = (int)(SP[i][1] - SP[i][2]); y <= (int)(SP[i][1] + SP[i][2]); y++)
            for (int x = (int)(SP[i][0] - SP[i][2] * 1.3f); x <= (int)(SP[i][0] + SP[i][2] * 1.3f); x++) {
                float dx = (x - SP[i][0]) / (SP[i][2] * 1.3f), dy = (y - SP[i][1]) / SP[i][2];
                if (dx * dx + dy * dy < 1 + 0.3f * sinf(x * 1.3f + y) && get(c, x, y) == rgb(0xF8F8F8)) D(c, x, y, 0x2A2A2A);
            }
    E(c, 24, 30, 4, 4, 0x2A2A2A);
    eye(c, 20, 29, blink(t, 1.0f));
    D(c, 15 + (int)chew, 40, 0x8A4A5A);
    D(c, 20 + (int)chew, 40, 0x8A4A5A);
    E(c, 30, 44, 2, 2, 0xFFD23F);
}

static void pic_owl(Canvas *c, float t) {
    grad(c, 0x10183A, 0x2A2A5A);
    twinkles(c, t, 18, PH);
    E(c, 80, 14, 9, 9, 0xFFF4C8);
    E(c, 77, 12, 2, 2, 0xE8DCA8);
    E(c, 83, 17, 1.5f, 1.5f, 0xE8DCA8);
    brush(c, 0, 58, 96, 56, 2.5f, 0x6E4422);
    E(c, 14, 54, 4, 2, 0x3E8A3A);
    E(c, 84, 52, 5, 2, 0x3E8A3A);
    float tilt = sinf(t * 0.7f) * 1.5f;
    Canvas *L = lay();
    E(L, 48, 42, 15, 17, 0x9A6A3A);
    E(L, 48, 47, 9, 10, 0xE8D0A0);
    E(L, 35, 44, 5, 11, 0x7A4E26);
    E(L, 61, 44, 5, 11, 0x7A4E26);
    E(L, 48 + tilt, 27, 14, 11, 0x9A6A3A);
    tri(L, 36 + tilt, 22, 36 + tilt, 12, 42 + tilt, 19, 0x9A6A3A);
    tri(L, 54 + tilt, 19, 60 + tilt, 12, 60 + tilt, 22, 0x9A6A3A);
    E(L, 42 + tilt, 28, 6.5f, 6.5f, 0xF0E0C0);
    E(L, 54 + tilt, 28, 6.5f, 6.5f, 0xF0E0C0);
    R(L, 42, 56, 44, 59, 0xFF9A2A);
    R(L, 52, 56, 54, 59, 0xFF9A2A);
    flush(c, 0x3A2010);
    for (int i = 0; i < 6; i++) { int x = 43 + (i % 3) * 4, y = 44 + (i / 3) * 5; D(c, x, y, 0xA8805A); D(c, x + 1, y + 1, 0xA8805A); D(c, x + 2, y, 0xA8805A); }
    for (int k = 0; k < 2; k++) {
        float ex = (k ? 54 : 42) + tilt;
        if (blink(t, 0.3f) || fmodf(t, 9) < 0.5f) { R(c, (int)ex - 4, 28, (int)ex + 4, 28, 0x5A3A1A); continue; }
        E(c, ex, 28, 4.5f, 4.5f, 0xFFD23F);
        E(c, ex + 0.5f, 28.5f, 2.3f, 2.3f, 0x1A1A1A);
        D(c, (int)ex - 1, 26, 0xFFFFFF);
    }
    tri(c, 46 + tilt, 32, 50 + tilt, 32, 48 + tilt, 36, 0xFF9A2A);
}

static void pic_drum(Canvas *c, float t) {
    grad(c, 0xFFE8B8, 0xF8C888);
    for (int y = 0; y < PH; y++)                           /* a spotlight */
        for (int x = 0; x < PW; x++)
            if (abs(x - 48) < 10 + y * 0.5f) tint(c, x, y, 0xFFFFF0, 0.35f);
    R(c, 0, 62, 95, 71, 0x8A4A2A);
    R(c, 0, 62, 95, 62, 0xA8643A);
    float k = fmodf(t * 2.4f, 2.0f);
    int left = k < 1;
    float hit = left ? k : k - 1, up = hit < 0.25f ? hit / 0.25f : 1 - (hit - 0.25f) / 0.75f;
    Canvas *L = lay();
    R(L, 26, 36, 70, 60, 0xD8303A);
    E(L, 48, 60, 22, 4, 0xD8303A);
    for (int i = 0; i < 8; i++) {
        float x = 26 + i * 5.5f;
        ln(L, x, 38, x + 5.5f, 57, 0xFFD23F);
        ln(L, x + 5.5f, 38, x, 57, 0xFFD23F);
    }
    R(L, 26, 37, 70, 39, 0xE8B84A);
    R(L, 26, 56, 70, 58, 0xE8B84A);
    E(L, 48, 36, 22, 5, 0xFFF4E0);
    E(L, 48, 35.5f, 19, 3.5f, 0xFFFFF4);
    float sa = 0.5f + up * 0.6f, sb = 0.5f + (1 - up) * 0.15f;
    float a1 = left ? sa : sb, a2 = left ? sb : sa;
    brush(L, 38, 34, 38 - cosf(a1) * 22, 34 - sinf(a1) * 22, 1.2f, 0xD8A868);
    brush(L, 58, 34, 58 + cosf(a2) * 22, 34 - sinf(a2) * 22, 1.2f, 0xD8A868);
    E(L, 38, 34, 1.8f, 1.8f, 0xF0D8A8);
    E(L, 58, 34, 1.8f, 1.8f, 0xF0D8A8);
    flush(c, 0x5A1A1A);
    if (hit < 0.3f) {                                      /* boom: the drum shakes */
        for (int i = 0; i < 3; i++) {
            float x = left ? 30 - i * 3 : 66 + i * 3;
            ln(c, x, 26 - i * 2, x + (left ? -2 : 2), 22 - i * 2, 0xFFFFFF);
        }
    }
    for (int i = 0; i < 3; i++) {                          /* music notes float up */
        float u = fmodf(t * 0.4f + i / 3.0f, 1.0f);
        note(c, (int)(18 + i * 26 + sinf(u * 6 + i) * 3), (int)(30 - u * 26), mixc(0x8A3A6A, 0xFFE8B8, u * u));
    }
}

static void pic_nest(Canvas *c, float t) {
    sky(c);
    cloud(c, fmodf(t * 2 + 80, 130) - 15, 10, 0.8f);
    brush(c, 96, 52, 4, 60, 3, 0x6E4422);
    brush(c, 70, 54, 84, 40, 1.5f, 0x6E4422);
    for (int i = 0; i < 7; i++) {
        float lx = 10 + i * 13, ly = 56 + (i % 2) * 6 - 6;
        E(c, lx, ly, 4.5f, 2.2f, i % 2 ? 0x4FAE4A : 0x6CC65A);
    }
    E(c, 84, 38, 5, 3, 0x4FAE4A);
    static const uint32_t BABY[3] = { 0x8AA8D8, 0x9AB4E0, 0x7A98C8 };
    Canvas *L = lay();
    for (int i = 0; i < 3; i++) {                          /* three babies, beaks open in turn */
        float bx = 36 + i * 12, open = fmodf(t * 1.5f + i * 0.6f, 1.8f) < 0.6f;
        float by = 40 - (open ? 3 : 0) + (i == 1 ? -2 : 0);
        E(L, bx, by + 6, 6, 6, BABY[i]);
        E(L, bx, by, 5, 5, BABY[i]);
        R(L, (int)bx - 1, (int)by - 6, (int)bx, (int)by - 4, BABY[i]);
        if (open) {
            tri(L, bx - 3, by + 1, bx + 3, by + 1, bx, by - 3, 0xFFC23A);
            tri(L, bx - 3, by + 2, bx + 3, by + 2, bx, by + 6, 0xFFC23A);
            tri(L, bx - 2, by + 1.5f, bx + 2, by + 1.5f, bx, by + 4, 0xE84A3A);
        } else {
            tri(L, bx - 2.5f, by + 1, bx + 2.5f, by + 1, bx, by + 4, 0xFFC23A);
        }
    }
    E(L, 48, 52, 26, 9, 0x8A5A2A);
    E(L, 48, 49, 23, 4, 0x6A4220);
    for (int i = 0; i < 16; i++) ln(L, 24 + i * 3, 48 + (i % 3) * 2, 28 + i * 3, 56 + (i % 2) * 2, 0xB08040);
    flush(c, 0x3A2010);
    E(c, 48, 50, 21, 2, 0x8A5A2A);
    for (int i = 0; i < 3; i++) {
        float bx = 36 + i * 12, open = fmodf(t * 1.5f + i * 0.6f, 1.8f) < 0.6f;
        float by = 40 - (open ? 3 : 0) + (i == 1 ? -2 : 0);
        eye(c, (int)bx - 3, (int)by - 2, 0);
        eye(c, (int)bx + 2, (int)by - 2, 0);
    }
}

static void pic_crab(Canvas *c, float t) {
    grad(c, 0x7CC8F5, 0xD8F2FF);
    for (int y = 30; y < 40; y++) R(c, 0, y, 95, y, mixc(0x2A90D0, 0x5AB4E8, (y - 30) / 10.0f));
    for (int x = 0; x < PW; x++) if ((int)(x + t * 5) % 11 < 3) D(c, x, 39, 0xFFFFFF);
    for (int y = 40; y < PH; y++) R(c, 0, y, 95, y, mixc(0xF8E0A0, 0xE8C478, (y - 40) / 32.0f));
    E(c, 14, 64, 4, 2.5f, 0xF0A8B8);
    E(c, 84, 50, 3, 2, 0xFFFFFF);
    float x = 48 + sinf(t * 0.7f) * 14, y = 48, step = sinf(t * 10);
    float snip = fmodf(t, 1.6f) < 0.25f ? 1 : 0;
    Canvas *L = lay();
    for (int k = 0; k < 3; k++) {                          /* legs scuttling */
        float o = (k % 2 ? step : -step) * 1.5f;
        brush(L, x - 10, y + 2 + k * 2, x - 18 + o, y + 8 + k * 3, 0.8f, 0xD83A2A);
        brush(L, x + 10, y + 2 + k * 2, x + 18 - o, y + 8 + k * 3, 0.8f, 0xD83A2A);
    }
    brush(L, x - 12, y - 2, x - 20, y - 10, 1.5f, 0xE8483A);
    brush(L, x + 12, y - 2, x + 20, y - 10, 1.5f, 0xE8483A);
    for (int s = -1; s <= 1; s += 2) {                     /* claws, snipping */
        float cx = x + s * 22, cy = y - 14;
        E(L, cx, cy + 2, 5, 4, 0xE8483A);
        tri(L, cx - 4, cy, cx + s * 2, cy - 9 + snip * 3, cx + 4, cy, 0xE8483A);
        tri(L, cx - 1, cy + 1, cx + s * 8, cy - 4 + snip * 2, cx + 3, cy + 3, 0xE8483A);
    }
    R(L, (int)x - 5, (int)y - 16, (int)x - 4, (int)y - 6, 0xD83A2A);
    R(L, (int)x + 4, (int)y - 16, (int)x + 5, (int)y - 6, 0xD83A2A);
    E(L, x - 4.5f, y - 17, 3, 3, 0xFFFFFF);
    E(L, x + 4.5f, y - 17, 3, 3, 0xFFFFFF);
    E(L, x, y, 15, 10, 0xE8483A);
    E(L, x - 3, y - 3, 6, 3, 0xF8705A);
    flush(c, 0x5A1008);
    if (blink(t, 0.5f)) { R(c, (int)x - 7, (int)y - 17, (int)x - 2, (int)y - 17, 0x1A1A1A); R(c, (int)x + 2, (int)y - 17, (int)x + 7, (int)y - 17, 0x1A1A1A); }
    else { R(c, (int)x - 5, (int)y - 18, (int)x - 4, (int)y - 16, 0x1A1A1A); R(c, (int)x + 4, (int)y - 18, (int)x + 5, (int)y - 16, 0x1A1A1A); }
    for (int i = -4; i <= 4; i++) D(c, (int)x + i, (int)y + 3 + (16 - i * i) / 8, 0x7A1A10);
}

static void pic_bird(Canvas *c, float t) {
    sky(c);
    cloud(c, fmodf(t * 2 + 40, 130) - 15, 12, 0.9f);
    brush(c, 0, 62, 96, 50, 2.5f, 0x6E4422);
    for (int i = 0; i < 6; i++) E(c, 8 + i * 17, 60 - i * 2 + (i % 2) * 4 - 2, 4.5f, 2.2f, i % 2 ? 0x4FAE4A : 0x6CC65A);
    float k = fmodf(t, 3.0f), sing = k > 1.2f && k < 2.6f && (int)(t * 6) % 2;
    float hop = k > 0.2f && k < 0.5f ? sinf((k - 0.2f) / 0.3f * PI_F) * 3 : 0;
    float bx = 44, by = 42 - hop;
    Canvas *L = lay();
    tri(L, bx - 10, by + 2, bx - 22, by + 10, bx - 18, by + 13, 0x2A5AC8);
    E(L, bx, by, 13, 10, 0x3A7AE8);
    E(L, bx + 3, by + 4, 8, 6, 0xF08A3A);
    E(L, bx + 9, by - 9, 8, 7.5f, 0x3A7AE8);
    E(L, bx - 3, by - 1, 8, 6, 0x2A5AC8);
    if (sing) {
        tri(L, bx + 16, by - 10, bx + 22, by - 13, bx + 16, by - 8, 0xFFC23A);
        tri(L, bx + 16, by - 8, bx + 22, by - 5, bx + 16, by - 6, 0xFFC23A);
    } else {
        tri(L, bx + 16, by - 10, bx + 22, by - 8, bx + 16, by - 6, 0xFFC23A);
    }
    R(L, (int)bx - 2, (int)by + 9, (int)bx - 2, (int)by + 12, 0xD8862A);
    R(L, (int)bx + 3, (int)by + 9, (int)bx + 3, (int)by + 12, 0xD8862A);
    flush(c, 0x14285A);
    eye(c, (int)bx + 11, (int)by - 11, blink(t, 1.5f));
    if (k > 1.2f) {
        for (int i = 0; i < 3; i++) {
            float u = fmodf(t * 0.6f + i / 3.0f, 1.0f);
            note(c, (int)(bx + 26 + u * 14 + i * 3), (int)(by - 14 - u * 20), mixc(0x3A3A8A, 0xD4F0FF, u * u));
        }
    }
}

static void pic_bell(Canvas *c, float t) {
    grad(c, 0x3A6A5A, 0x2A5048);
    for (int i = 0; i < 12; i++) sparkle(c, (i * 37 + 5) % PW, (i * 23 + 9) % PH, 0x5A9A7A);
    float a = sinf(t * 2.6f) * 0.38f, cl = sinf(t * 2.6f - 0.6f) * 0.5f;
    if (fabsf(a) > 0.3f) {                                 /* ding dong */
        float s = a > 0 ? 1 : -1;
        for (int i = 0; i < 3; i++) {
            float rr = 30 + i * 5;
            for (int k = -4; k <= 4; k++) {
                float an = (s > 0 ? 0.35f : PI_F - 0.35f) + k * 0.08f;
                D(c, (int)(48 + cosf(an) * rr), (int)(44 + sinf(an) * rr * 0.6f), 0xFFF4C8);
            }
        }
    }
    Canvas *L = lay();
    float cx = 48, cy = 12;
    rpoly(L, cx, cy, a, (float[]){ 0, 0, 0 }, (float[]){ 34, 34, 34 }, 3, 0xFFD23F);
    float bxs[10] = { -6, 6, 9, 10, 15, 16, -16, -15, -10, -9 }, bys[10] = { 4, 4, 10, 22, 31, 35, 35, 31, 22, 10 };
    float cxp = cx + sinf(-a - cl * 0.3f) * 38, cyp = cy + cosf(a + cl * 0.3f) * 38;
    E(L, cxp, cyp, 3.5f, 3.5f, 0xC8902A);
    rpoly(L, cx, cy, a, bxs, bys, 10, 0xF2C230);
    float hxs[4] = { -4, -1, -1, -4 }, hys[4] = { 6, 6, 30, 30 };
    rpoly(L, cx, cy, a, hxs, hys, 4, 0xFFE87A);
    float lxs[4] = { -16, 16, 16, -16 }, lys[4] = { 31, 31, 35, 35 };
    rpoly(L, cx, cy, a, lxs, lys, 4, 0xD8A020);
    E(L, cx, cy + 2, 4, 3, 0xE8B02A);
    flush(c, 0x6A4A08);
    tri(c, 48, 6, 38, 2, 38, 10, 0xE83A3A);                /* a red bow on top */
    tri(c, 48, 6, 58, 2, 58, 10, 0xE83A3A);
    E(c, 48, 6, 2.5f, 2.5f, 0xC82A2A);
    tri(c, 46, 7, 42, 16, 45, 16, 0xE83A3A);
    tri(c, 50, 7, 54, 16, 51, 16, 0xE83A3A);
}

static void pic_sock(Canvas *c, float t) {
    sky(c);
    cloud(c, fmodf(t * 3 + 20, 130) - 15, 10, 0.8f);
    grass(c, 62, 0x6CC25A);
    R(c, 2, 8, 3, 64, 0x8A5A32);
    R(c, 92, 8, 93, 64, 0x8A5A32);
    for (int x = 3; x < 92; x++) D(c, x, 10 + (int)(sinf((x - 3) / 89.0f * PI_F) * 4), 0xEEEEEE);
    float sw = sinf(t * 1.8f) * 0.18f;
    static const uint32_t ST[3] = { 0xE83A3A, 0xFFFFFF, 0x3A7AE8 };
    Canvas *L = lay();
    for (int y = 13; y < 40; y++) R(L, 36, y, 50, y, ST[((y - 13) / 3) % 3]);
    R(L, 36, 13, 50, 15, 0xFFFFFF);
    E(L, 43, 44, 8, 7, ST[0]);
    R(L, 43, 38, 62, 51, ST[2]);
    E(L, 62, 44.5f, 6.5f, 6.5f, ST[0]);
    E(L, 40, 47, 4.5f, 4, ST[0]);
    flush_lean(c, 0x2A2A4A, 14, sw);
    R(c, 38, 9, 39, 16, 0xC8925A);                         /* clothes pegs */
    R(c, 47, 10, 48, 17, 0xC8925A);
    float sw2 = sinf(t * 1.8f + 0.7f) * 0.2f;              /* a little shirt beside it */
    L = lay();
    R(L, 70, 14, 84, 34, 0xFFD23F);
    quad(L, 70, 14, 64, 20, 67, 23, 70, 20, 0xFFD23F);
    quad(L, 84, 14, 90, 20, 87, 23, 84, 20, 0xFFD23F);
    E(L, 77, 14, 3, 1.5f, 0xE8B02A);
    flush_lean(c, 0x5A4A08, 14, sw2);
    R(c, 71, 11, 72, 16, 0xC8925A);
}

static void pic_milk(Canvas *c, float t) {
    for (int y = 0; y < 56; y++)
        for (int x = 0; x < PW; x++) D(c, x, y, ((x / 8) + (y / 8)) % 2 ? 0xDCEEFF : 0xC8E0F8);
    R(c, 0, 56, 95, 71, 0xC8925A);
    R(c, 0, 56, 95, 57, 0xDCA86A);
    Canvas *L = lay();
    R(L, 16, 22, 40, 60, 0xFFFFFF);                        /* the carton */
    tri(L, 16, 22, 28, 10, 40, 22, 0xF0F0F4);
    tri(L, 28, 10, 40, 22, 40, 18, 0xD8D8E0);
    R(L, 26, 6, 30, 10, 0xF0F0F4);
    R(L, 16, 30, 40, 34, 0x3A7AE8);
    R(L, 34, 22, 40, 60, 0xE4E4EC);
    E(L, 22, 44, 3, 2.5f, 0x2A2A2A);
    E(L, 31, 52, 3.5f, 2.5f, 0x2A2A2A);
    E(L, 27, 40, 2, 1.5f, 0x2A2A2A);
    R(L, 54, 26, 76, 60, 0xE0F0FF);                        /* a glass of milk */
    R(L, 55, 31, 75, 59, 0xFFFFFF);
    R(L, 55, 31, 57, 59, 0xF0F4FA);
    E(L, 65, 31, 10, 1.5f, 0xF8F8FF);
    ln(L, 62, 31, 70, 6, 0xFF5A8A);                        /* a bendy straw */
    ln(L, 63, 31, 71, 6, 0xFF5A8A);
    ln(L, 70, 6, 76, 4, 0xFF5A8A);
    ln(L, 71, 7, 77, 5, 0xFF5A8A);
    flush(c, 0x4A5A7A);
    for (int i = 0; i < 4; i++) {                          /* bubbles at the top */
        float u = fmodf(t * 0.7f + i / 4.0f, 1.0f);
        D(c, 58 + i * 4 + (int)(sinf(u * 6) * 1), (int)(33 - u * 2), 0xE0E8F4);
    }
    for (int i = 0; i < 3; i++) {
        float k = fmodf(t * 0.5f + i * 0.33f, 1.0f);
        if (k < 0.3f) sparkle(c, 58 + i * 7, 40 + i * 6, 0xE8F0FF);
    }
}

static void pic_bear(Canvas *c, float t) {
    grad(c, 0xFFE8C8, 0xF8D4A8);
    for (int i = 0; i < 9; i++) {
        int x = (i * 23 + 6) % PW, y = (i * 17 + 5) % 44;
        D(c, x, y, 0xFFC0A0); D(c, x - 1, y, 0xFFC0A0); D(c, x + 1, y, 0xFFC0A0); D(c, x, y - 1, 0xFFC0A0); D(c, x, y + 1, 0xFFC0A0);
    }
    R(c, 0, 60, 95, 71, 0x7AB0D8);
    E(c, 48, 66, 34, 5, 0x9AC8E8);
    float wave = sinf(t * 5) * 0.45f - 1.0f;
    Canvas *L = lay();
    E(L, 36, 50, 4.5f, 8, 0xB0763A);
    brush(L, 59, 44, 59 + cosf(wave) * 11, 44 + sinf(wave) * 11, 4, 0xB0763A);
    E(L, 48, 50, 14, 13, 0xB0763A);
    E(L, 48, 53, 8, 8, 0xE8C090);
    E(L, 40, 62, 6.5f, 5, 0xB0763A);
    E(L, 56, 62, 6.5f, 5, 0xB0763A);
    E(L, 40, 63, 3.5f, 3, 0xE8C090);
    E(L, 56, 63, 3.5f, 3, 0xE8C090);
    E(L, 38, 20, 5, 5, 0xB0763A);
    E(L, 58, 20, 5, 5, 0xB0763A);
    E(L, 38, 20, 2.5f, 2.5f, 0xE8C090);
    E(L, 58, 20, 2.5f, 2.5f, 0xE8C090);
    E(L, 48, 29, 13, 11.5f, 0xB0763A);
    E(L, 48, 34, 5.5f, 4, 0xE8C090);
    flush(c, 0x4A2A10);
    E(c, 59 + cosf(wave) * 11, 44 + sinf(wave) * 11, 2, 2, 0xE8C090);
    tri(c, 48, 40, 41, 37, 41, 43, 0xE83A5A);              /* bow tie */
    tri(c, 48, 40, 55, 37, 55, 43, 0xE83A5A);
    E(c, 48, 40, 1.5f, 1.5f, 0xC82A4A);
    int sh = blink(t, 0.9f);
    eye(c, 43, 26, sh);
    eye(c, 52, 26, sh);
    E(c, 48.5f, 32.5f, 2.2f, 1.6f, 0x2A1A10);
    D(c, 48, 34, 0x2A1A10);
    D(c, 47, 35, 0x2A1A10); D(c, 50, 35, 0x2A1A10);
    E(c, 40, 32, 2, 1.2f, 0xFF9A8A);
    E(c, 57, 32, 2, 1.2f, 0xFF9A8A);
}

static void pic_book(Canvas *c, float t) {
    grad(c, 0x6A4A8A, 0x4A3A6A);
    twinkles(c, t, 14, 30);
    R(c, 0, 58, 95, 71, 0xA8743E);
    R(c, 0, 58, 95, 59, 0xC8925A);
    Canvas *L = lay();
    quad(L, 10, 30, 48, 34, 48, 62, 10, 58, 0x3A7AE8);     /* the covers */
    quad(L, 48, 34, 86, 30, 86, 58, 48, 62, 0x3A7AE8);
    quad(L, 12, 27, 48, 31, 48, 58, 12, 55, 0xFFF8E8);     /* the pages */
    quad(L, 48, 31, 84, 27, 84, 55, 48, 58, 0xFFF8E8);
    flush(c, 0x2A2A4A);
    for (int i = 0; i < 7; i++) {
        float y = 34 + i * 3;
        ln(c, 16, y - 1 + i * 0.1f, 44, y + 2 + i * 0.1f, 0xB0A890);
        ln(c, 52, y + 2 - i * 0.1f, 80, y - 1 - i * 0.1f, 0xB0A890);
    }
    E(c, 64, 42, 5, 4, 0xFF8A3A);                          /* a little picture */
    tri(c, 58, 42, 55, 39, 55, 45, 0xFF8A3A);
    R(c, 48, 31, 48, 58, 0xC8C0A8);
    float k = fmodf(t * 0.4f, 1.0f);
    if (k < 0.5f) {                                        /* a page turning over */
        float a = ease(k / 0.5f) * PI_F, x = 48 + cosf(a) * 36, lift = sinf(a) * 8;
        quad(c, 48, 31, x, 27 - lift, x, 55 - lift, 48, 58, cosf(a) > 0 ? 0xF0E8D0 : 0xFFF8E8);
        ln(c, x, 27 - lift, x, 55 - lift, 0x8A8070);
    }
    for (int i = 0; i < 4; i++) {
        float u = fmodf(t * 0.35f + i / 4.0f, 1.0f);
        sparkle(c, (int)(30 + i * 12 + sinf(u * 5 + i) * 4), (int)(28 - u * 24), mixc(0xFFE27A, 0x6A4A8A, u));
    }
}

static void pic_apple(Canvas *c, float t) {
    grad(c, 0xFFF0D0, 0xFFE0B0);
    for (int y = 58; y < PH; y++)
        for (int x = 0; x < PW; x++) D(c, x, y, ((x / 6) + (y / 6)) % 2 ? 0xFFFFFF : 0xE8604A);
    float sw = sinf(t * 1.2f) * 0.6f, lf = sinf(t * 2.5f) * 1.5f;
    Canvas *L = lay();
    E(L, 40 + sw, 40, 15, 17, 0xE0302A);
    E(L, 56 + sw, 40, 15, 17, 0xE0302A);
    E(L, 48 + sw, 52, 18, 8, 0xE0302A);
    E(L, 58 + sw, 44, 9, 12, 0xC02822);
    R(L, 47 + (int)sw, 16, 49 + (int)sw, 26, 0x6E4422);
    quad(L, 49 + sw, 22, 56 + sw, 14 + lf, 66 + sw, 15 + lf, 58 + sw, 23, 0x5CB84A);
    flush(c, 0x6A1010);
    E(c, 48 + sw, 25, 5, 2, 0xA02020);
    E(c, 38 + sw, 32, 3, 5, 0xFF7A6A);
    D(c, 37 + (int)sw, 30, 0xFFFFFF);
    D(c, 37 + (int)sw, 31, 0xFFFFFF);
    ln(c, 50 + sw, 22, 62 + sw, 15 + lf, 0x3E8A3A);
    float k = fmodf(t, 7.0f), up = k < 2 ? 0 : k < 2.5f ? (k - 2) / 0.5f : k < 5.5f ? 1 : k < 6 ? 1 - (k - 5.5f) / 0.5f : 0;
    E(c, 60 + sw, 46, 2.2f, 2.2f, 0x6A1A10);               /* a little worm says hi */
    if (up > 0) {
        float wx = 60 + sw + up * 4 + sinf(t * 6) * up, wy = 46 - up * 5;
        brush(c, 60 + sw, 46, wx, wy, 1.8f, 0x8AD04A);
        E(c, wx, wy, 2.6f, 2.6f, 0x8AD04A);
        D(c, (int)wx, (int)wy - 1, 0x1A1A1A);
        D(c, (int)wx + 2, (int)wy - 1, 0x1A1A1A);
        D(c, (int)wx + 1, (int)wy + 1, 0x4A7A2A);
    }
}

static void pic_house(Canvas *c, float t) {
    sky(c);
    cloud(c, fmodf(t * 2 + 90, 130) - 15, 10, 0.8f);
    grass(c, 58, 0x6CC25A);
    quad(c, 44, 72, 52, 72, 51, 60, 45, 60, 0xD8C8A8);
    for (int i = 0; i < 3; i++) {                          /* chimney smoke */
        float k = fmodf(t * 0.4f + i / 3.0f, 1.0f);
        E(c, 66 + k * 8 + sinf(k * 6) * 2, 14 - k * 14, 2 + k * 3, 2 + k * 3, mixc(0xF0F0F0, 0xC8E8FF, k));
    }
    Canvas *L = lay();
    R(L, 62, 14, 68, 28, 0xB0503A);
    R(L, 61, 13, 69, 14, 0x8A3A2A);
    R(L, 26, 34, 70, 60, 0xF8E08A);
    tri(L, 20, 36, 48, 14, 76, 36, 0xD84A3A);
    R(L, 44, 44, 52, 60, 0x3A7AE8);
    R(L, 30, 40, 40, 50, 0xA8DCFF);
    R(L, 56, 40, 66, 50, 0xA8DCFF);
    E(L, 48, 26, 4, 4, 0xA8DCFF);
    flush(c, 0x3A2010);
    for (int y = 20; y < 36; y += 3) for (int x = 20; x < 76; x++) if (get(c, x, y) == rgb(0xD84A3A)) D(c, x, y, 0xB83A2A);
    R(c, 35, 40, 35, 50, 0xFFFFFF); R(c, 30, 45, 40, 45, 0xFFFFFF);
    R(c, 61, 40, 61, 50, 0xFFFFFF); R(c, 56, 45, 66, 45, 0xFFFFFF);
    D(c, 50, 52, 0xFFD23F);
    R(c, 28, 50, 42, 51, 0xFFFFFF); R(c, 54, 50, 68, 51, 0xFFFFFF);
    for (int i = 0; i < 4; i++) {                          /* flowers by the door */
        int fx = 30 + i * 4 + (i > 1 ? 24 : 0);
        D(c, fx, 58, 0x3E8A3A); D(c, fx, 57, (uint32_t[]){ 0xFF5A8A, 0xFFD23F, 0xB45FD0, 0xFF8A3A }[i]);
    }
    float bx = fmodf(t * 10, 130) - 15, by = 22 + sinf(t * 3) * 3;   /* a bird flies over */
    int up = (int)(t * 5) % 2;
    D(c, (int)bx - 1, (int)by - up, 0x34405A); D(c, (int)bx, (int)by, 0x34405A); D(c, (int)bx + 1, (int)by - up, 0x34405A);
    D(c, (int)bx - 2, (int)by - up, 0x34405A); D(c, (int)bx + 2, (int)by - up, 0x34405A);
}

static void pic_train(Canvas *c, float t) {
    sky(c);
    for (int x = 0; x < PW; x++) {
        int hy = 46 - (int)(5 * sinf((x + t * 3) * 0.07f));
        R(c, x, hy, x, 60, 0x8AD07A);
    }
    R(c, 0, 60, 95, 71, 0x9A8A70);
    for (int x = 0; x < PW; x++) if ((int)(x + t * 30) % 8 < 3) R(c, x, 63, x, 66, 0x6E4422);
    R(c, 0, 62, 95, 62, 0x8A8A94);
    for (int i = 0; i < 4; i++) {                          /* puffs of steam drifting back */
        float k = fmodf(t * 0.8f + i / 4.0f, 1.0f);
        E(c, 28 + k * 50, 16 - k * 12, 3 + k * 5, 2.5f + k * 4, mixc(0xFFFFFF, 0xC8E4FF, k));
    }
    float spin = -t * 9, rod = cosf(spin) * 3;
    Canvas *L = lay();
    R(L, 22, 34, 56, 52, 0xD8303A);                        /* the boiler */
    R(L, 22, 34, 56, 36, 0xF0505A);
    for (int i = 0; i < 3; i++) R(L, 30 + i * 9, 34, 31 + i * 9, 52, 0xFFD23F);
    R(L, 56, 22, 76, 54, 0x2E6AC8);                        /* the cab */
    R(L, 54, 20, 78, 22, 0x2A2A3A);
    R(L, 60, 26, 72, 36, 0xA8DCFF);
    R(L, 26, 20, 32, 34, 0x2A2A3A);                        /* the smokestack */
    R(L, 24, 18, 34, 21, 0x2A2A3A);
    E(L, 44, 33, 4, 3, 0xFFD23F);
    quad(L, 22, 46, 22, 56, 12, 58, 14, 52, 0x6A6A78);     /* the cowcatcher */
    E(L, 20, 40, 2.5f, 2.5f, 0xFFF4A0);
    R(L, 20, 52, 78, 54, 0x2A2A3A);
    flush(c, 0x1A1A2A);
    E(c, 66, 31, 3, 3, 0xF0C29C);                          /* the driver waves */
    R(c, 63, 27, 69, 28, 0x3A3A8A);
    D(c, 65, 31, 0x1E1410); D(c, 67, 31, 0x1E1410);
    wheel(c, 30, 57, 5, spin);
    wheel(c, 44, 57, 5, spin);
    wheel(c, 66, 55, 7, spin);
    ln(c, 30 + rod, 57 + sinf(spin) * 3, 66 + rod, 55 + sinf(spin) * 3, 0xB8BCC8);
}

/* ---------- the word list ---------- */

/* Add words here. Pictures are drawn on a PW x PH canvas at time t (seconds); the words
   take turns by date, one a day, in this order. Keep to the 5x7 font's letters. */
static const Word WORDS[] = {
    { "CAT", "C A T", "THE CAT NAPS IN THE SUN.", pic_cat },
    { "SUN", "S U N", "THE SUN IS HOT AND BIG.", pic_sun },
    { "FISH", "F I SH", "A FISH CAN SWIM FAST.", pic_fish },
    { "DOG", "D O G", "MY DOG CAN RUN AND JUMP!", pic_dog },
    { "TREE", "T R EE", "APPLES GROW ON A TREE.", pic_tree },
    { "CAKE", "C A K e", "MAKE A WISH ON THE CAKE!", pic_cake },
    { "BUG", "B U G", "A RED BUG IS ON THE LEAF.", pic_bug },
    { "BOAT", "B OA T", "THE BOAT SAILS ON THE SEA.", pic_boat },
    { "FROG", "F R O G", "A GREEN FROG SAYS RIBBIT!", pic_frog },
    { "HAT", "H A T", "A BUNNY IS IN THE HAT!", pic_hat },
    { "STAR", "S T AR", "I SEE A STAR AT NIGHT.", pic_star },
    { "PIG", "P I G", "THE PIG PLAYS IN THE MUD.", pic_pig },
    { "MOON", "M OO N", "THE MOON IS UP. GOOD NIGHT!", pic_moon },
    { "BALL", "B A LL", "THE BALL GOES UP AND DOWN.", pic_ball },
    { "HEN", "H E N", "THE HEN PECKS AT THE CORN.", pic_hen },
    { "DUCK", "D U CK", "THE DUCK SWIMS IN THE POND.", pic_duck },
    { "BED", "B E D", "TEDDY NAPS IN HIS BED.", pic_bed },
    { "KITE", "K I T e", "MY KITE FLIES UP HIGH.", pic_kite },
    { "CAR", "C AR", "THE RED CAR GOES FAST.", pic_car },
    { "BEE", "B EE", "A BEE GOES BUZZ, BUZZ!", pic_bee },
    { "RAIN", "R AI N", "RAIN FALLS FROM THE CLOUD.", pic_rain },
    { "EGG", "E GG", "A CHICK IS IN THE EGG!", pic_egg },
    { "BUS", "B U S", "WE RIDE THE BUS TO SCHOOL.", pic_bus },
    { "SNOW", "S N OW", "THE SNOW IS SOFT AND COLD.", pic_snow },
    { "CUP", "C U P", "HOT COCOA IN MY CUP.", pic_cup },
    { "CRAB", "C R A B", "THE CRAB WALKS SIDEWAYS.", pic_crab },
    { "BOX", "B O X", "A KITTEN HIDES IN THE BOX.", pic_box },
    { "BIRD", "B IR D", "THE BIRD SINGS A SONG.", pic_bird },
    { "FOX", "F O X", "THE FOX HAS A BIG TAIL.", pic_fox },
    { "BELL", "B E LL", "RING THE BELL! DING DONG!", pic_bell },
    { "COW", "C OW", "THE COW SAYS MOO!", pic_cow },
    { "SOCK", "S O CK", "MY SOCK HAS STRIPES.", pic_sock },
    { "OWL", "OW L", "THE OWL IS UP AT NIGHT.", pic_owl },
    { "MILK", "M I L K", "I LIKE A GLASS OF MILK.", pic_milk },
    { "DRUM", "D R U M", "BANG THE DRUM! BOOM, BOOM!", pic_drum },
    { "BEAR", "B EAR", "MY TEDDY BEAR WAVES HI!", pic_bear },
    { "NEST", "N E S T", "BABY BIRDS SING IN THE NEST.", pic_nest },
    { "BOOK", "B OO K", "I LIKE TO READ A BOOK.", pic_book },
    { "APPLE", "A PP LE", "I EAT A RED APPLE.", pic_apple },
    { "HOUSE", "H OU S e", "THIS IS MY HOUSE.", pic_house },
    { "TRAIN", "T R AI N", "THE TRAIN GOES CHOO CHOO!", pic_train },
};
#define NWORDS (int)(sizeof WORDS / sizeof WORDS[0])

static const char *const DAYS[7] = { "SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY" };

/* ---------- the saver ---------- */

enum { ST_FLIP, ST_REST, ST_SPELL, ST_SOUND, ST_BOUNCE, ST_TYPE, ST_HOLD, ST_ERASE, ST_REVIEW };
static const char *const STATE_NAMES[] = { "FLIP", "REST", "SPELL", "SOUND IT OUT", "BOUNCE", "SENTENCE", "HOLD", "ERASE", "REVIEW" };
enum { P_DUST, P_STAR };

#define CHALK 0xF4F2EA
#define CHALK_YELLOW 0xFFE27A
#define CHALK_PINK 0xFF9EC8
#define CHALK_BLUE 0x9ED8FF
#define LETTER_GAP 0.7f          /* seconds between letters as the word is spelled */
#define SOUND_STEP 0.8f          /* seconds per sound as the star slides under the word */
#define TYPE_STEP 0.1f           /* seconds per letter of the sentence */
#define HOLD_SECS 16.0f
#define ERASE_SECS 2.6f
#define REVIEW_SECS 13.0f
#define REVIEW_EVERY 3           /* the board shows yesterday and tomorrow every this many rounds */

static const char ATLAS_CHARS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!?.,'-:";
#define DOT 6                    /* chalk letters: each font dot is DOT x DOT bits of chalk */

typedef struct { float x, y, vx, vy, life, max, size; int kind; uint32_t col; } Part;
typedef struct { int start, len, silent; } Sound;

typedef struct {
    int w, h, S, W, H;                       /* screen, art pixel size, art size */
    float t, stateT, dur, picT, smudge;
    int state, loop, pending, nextWord, wday, ls0;
    long days;
    float checkT, eraseWordX, eraseSentX;
    SDL_Rect wordRect, sentRect, dayRect, infoRect;
    int word;                                /* index into WORDS */
    int n, nsounds, sentLines, sentBreak, hiStart, hiLen;
    Sound sounds[MAXLEN];
    /* layout, in screen pixels */
    SDL_Rect board, card, pic, reviewPic[2], reviewCard[2];
    float ps, rps;
    int ls, ss, ws, wordX, wordY, marksY, arrowY, sentY, colX, colW, review0Y;
    int stripX, stripY, stripCW, stripCH, hasStrip;
    int restX, restY, eraserW, eraserH;
    float eraserX, eraserY;
    SDL_Texture *room, *atlas, *eraser, *picTex, *revTex[2];
    Canvas art, revArt[2];
    int picFrame, revFrame;
    int revWord[2];
    Part parts[PARTS];
} Board;

/* ---------- days and words ---------- */

static long days_from_civil(int y, int m, int d) {   /* days since 1970-01-01 */
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    long yoe = y - era * 400;
    long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static int word_for_day(long day) { return (int)(((day % NWORDS) + NWORDS) % NWORDS); }

static void today(long *days, int *wday) {
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    *days = days_from_civil(tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);
    *wday = tm->tm_wday;
    const char *force = getenv("TOASTSAVER_WORD");   /* for tests: show this word (an index) */
    if (force && *force) *days = atoi(force) + (long)NWORDS * 1000;
}

static int is_vowel(char ch) { return ch == 'A' || ch == 'E' || ch == 'I' || ch == 'O' || ch == 'U'; }

/* Splits the word into its sounds from the table's "F I SH" (lowercase letters are silent). */
static void parse_sounds(Board *b) {
    const Word *wd = &WORDS[b->word];
    int n = b->n, k = 0, pos = 0, ok = wd->sounds != NULL;
    for (const char *p = wd->sounds; ok && *p && k < MAXLEN;) {
        while (*p == ' ') p++;
        if (!*p) break;
        int len = 0, silent = *p >= 'a' && *p <= 'z';
        while (p[len] && p[len] != ' ') len++;
        b->sounds[k++] = (Sound){ pos, len, silent };
        pos += len;
        p += len;
    }
    if (!ok || pos != n) {
        for (k = 0; k < n && k < MAXLEN; k++) b->sounds[k] = (Sound){ k, 1, 0 };
    }
    b->nsounds = k;
}

/* ---------- textures ---------- */

static int atlas_index(char ch) {
    const char *p = strchr(ATLAS_CHARS, ch);
    return ch && p ? (int)(p - ATLAS_CHARS) : -1;
}

/* Every character in rough white chalk, side by side. */
static SDL_Texture *make_atlas(SDL_Renderer *ren) {
    int n = (int)strlen(ATLAS_CHARS), cw = FONT_W * DOT, ch = FONT_H * DOT;
    Canvas c = canvas_new(n * cw, ch);
    unsigned s = 12345;
    for (int i = 0; i < n; i++) {
        const char *const *g = font_glyph(ATLAS_CHARS[i]);
        for (int r = 0; r < FONT_H && g; r++)
            for (int q = 0; q < FONT_W; q++) {
                if (g[r][q] != '#') continue;
                for (int y = 0; y < DOT; y++)
                    for (int x = 0; x < DOT; x++) {
                        s = s * 1103515245u + 12345u;
                        int v = (s >> 16) % 100;
                        int edge = (x == 0 && (q == 0 || g[r][q - 1] != '#')) || (x == DOT - 1 && (q == FONT_W - 1 || g[r][q + 1] != '#')) ||
                                   (y == 0 && (r == 0 || g[r - 1][q] != '#')) || (y == DOT - 1 && (r == FONT_H - 1 || g[r + 1][q] != '#'));
                        if (v < (edge ? 7 : 1)) continue;           /* chalk skips a little */
                        uint32_t a = v < 14 ? 0xB0 : 0xFF, lvl = 228 + v % 28;
                        put(&c, i * cw + q * DOT + x, r * DOT + y, lvl << 24 | lvl << 16 | lvl << 8 | a);
                    }
            }
    }
    return canvas_texture(ren, &c);
}

static void wood(Canvas *c, int x0, int y0, int x1, int y1, uint32_t base, int vertical) {
    fill_rect(c, x0, y0, x1, y1, rgb(base));
    int n = vertical ? x1 - x0 + 1 : y1 - y0 + 1, len = vertical ? y1 - y0 + 1 : x1 - x0 + 1;
    for (int k = 0; k < n; k++)
        for (int p = irand(12); p < len; p += 6 + irand(24)) {
            int run = 4 + irand(16);
            uint32_t col = shade(base, irand(3) ? 0.88f : 1.08f);
            for (int q = p; q < p + run && q < len; q++)
                if (vertical) put(c, x0 + k, y0 + q, rgb(col));
                else put(c, x0 + q, y0 + k, rgb(col));
            p += run;
        }
}

static void dots_glyph(Canvas *c, char ch, int x, int y, uint32_t hex) {
    const char *const *g = font_glyph(ch);
    for (int r = 0; r < FONT_H && g; r++)
        for (int q = 0; q < FONT_W; q++)
            if (g[r][q] == '#') put(c, x + q, y + r, rgb(hex));
}

/* The classroom wall, the alphabet strip, the chalkboard and its tray, in art pixels. */
static void build_room(Board *b, Canvas *c, int *bx0, int *by0, int *bx1, int *by1, int *F, int *trayY) {
    int W = b->W, H = b->H;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            uint32_t col = mixc(0xF4E4BA, 0xE8D29E, (float)y / H);
            if ((x / 6) % 2) col = shade(col, 0.97f);
            put(c, x, y, rgb(col));
        }
    int cw = W / 27;
    b->hasStrip = cw >= 8 && H >= 100;
    int top = 2;
    if (b->hasStrip) {                                     /* A to Z along the top */
        static const uint32_t CARDS[6] = { 0xFFB0B0, 0xFFD890, 0xFFF0A0, 0xB8E8A8, 0xA8D8F8, 0xD8B8F0 };
        int ch = FONT_H + 4, x0 = (W - 26 * cw) / 2;
        for (int x = 0; x < W; x++) put(c, x, top + 1 + (int)(sinf(x * PI_F / cw) * 0.6f), rgb(0xA87850));
        for (int i = 0; i < 26; i++) {
            int x = x0 + i * cw;
            fill_rect(c, x + 1, top + 1, x + cw - 2, top + ch, rgb(CARDS[i % 6]));
            fill_rect(c, x + 1, top + ch, x + cw - 2, top + ch, rgb(shade(CARDS[i % 6], 0.82f)));
            dots_glyph(c, (char)('A' + i), x + (cw - FONT_W) / 2, top + 3, 0x4A3A2A);
            put(c, x + cw / 2, top + 1, rgb(0xD84A4A));
        }
        b->stripX = x0; b->stripY = top + 1; b->stripCW = cw; b->stripCH = ch;
        top += ch + 4;
    }
    *F = H / 40 > 3 ? H / 40 : 3;
    *bx0 = W * 4 / 100;
    *bx1 = W - 1 - W * 4 / 100;
    *by0 = top + 1;
    *by1 = H * 84 / 100;
    if (*by1 - *by0 < 30) *by1 = *by0 + 30 < H - 6 ? *by0 + 30 : H - 6;
    int f = *F;
    fill_rect(c, *bx0 + 2, *by0 + 2, *bx1 + 2, *by1 + 2, rgb(0xC8B080));        /* shadow on the wall */
    wood(c, *bx0, *by0, *bx1, *by1, 0x9A6236, 0);
    wood(c, *bx0, *by0, *bx0 + f - 1, *by1, 0x9A6236, 1);
    wood(c, *bx1 - f + 1, *by0, *bx1, *by1, 0x9A6236, 1);
    fill_rect(c, *bx0, *by0, *bx1, *by0, rgb(0xC6864C));
    fill_rect(c, *bx0, *by0, *bx0, *by1, rgb(0xB87A44));
    for (int y = *by0 + f; y <= *by1 - f; y++)            /* green chalkboard, old smudges and all */
        for (int x = *bx0 + f; x <= *bx1 - f; x++) {
            float g = 1.0f + 0.05f * sinf(x * 0.13f + y * 0.07f) + 0.04f * sinf(x * 0.031f - y * 0.05f);
            uint32_t col = shade(0x2F5A46, g);
            if (irand(10) == 0) col = shade(col, 1.08f);
            put(c, x, y, rgb(col));
        }
    for (int k = 0; k < 6; k++) {
        float cx = frand((float)*bx0, (float)*bx1), cy = frand((float)*by0, (float)*by1), rx = frand(10, 30), ry = frand(3, 8);
        for (int y = (int)(cy - ry); y <= (int)(cy + ry); y++)
            for (int x = (int)(cx - rx); x <= (int)(cx + rx); x++) {
                float dx = (x - cx) / rx, dy = (y - cy) / ry;
                if (dx * dx + dy * dy < 1 && x > *bx0 + f && x < *bx1 - f && y > *by0 + f && y < *by1 - f && irand(3)) tint(c, x, y, 0xFFFFFF, 0.05f);
            }
    }
    fill_rect(c, *bx0 + f, *by0 + f, *bx1 - f, *by0 + f, rgb(0x1C3A2C));
    fill_rect(c, *bx0 + f, *by0 + f, *bx0 + f, *by1 - f, rgb(0x1C3A2C));
    int ty = *by1 + 1, th = H / 60 > 2 ? H / 60 : 2;       /* the chalk tray */
    wood(c, *bx0 - 2, ty, *bx1 + 2, ty + th, 0xB07444, 0);
    fill_rect(c, *bx0 - 2, ty, *bx1 + 2, ty, rgb(0xD8A06A));
    fill_rect(c, *bx0 - 2, ty + th + 1, *bx1 + 2, ty + th + 1, rgb(0x7A4A22));
    *trayY = ty;
    static const uint32_t STICKS[4] = { 0xF4F2EA, 0xFFE27A, 0xFF9EC8, 0x9ED8FF };
    for (int i = 0; i < 4; i++) {
        int x = *bx0 + f + 6 + i * 9;
        fill_rect(c, x, ty - 2, x + 6, ty - 1, rgb(STICKS[i]));
        fill_rect(c, x, ty - 1, x + 6, ty - 1, rgb(shade(STICKS[i], 0.8f)));
    }
    int wy = ty + th + 2;                                  /* wood panelling below */
    if (wy < H) {
        fill_rect(c, 0, wy, W - 1, H - 1, rgb(0xB88A58));
        for (int x = 0; x < W; x++)
            for (int y = wy; y < H; y++)
                if ((x * 7 + y * 3) % 23 == 0 || (x % 40 > 36)) put(c, x, y, rgb(0xAA7E4E));
        fill_rect(c, 0, wy, W - 1, wy, rgb(0x8A5A32));
        fill_rect(c, 0, wy + 1, W - 1, wy + 1, rgb(0xD8A878));
    }
}

static SDL_Texture *make_eraser(SDL_Renderer *ren, int *w, int *h) {
    Canvas c = canvas_new(24, 10);
    fill_round_rect(&c, 1, 1, 22, 6, 2, rgb(0xC8925A));
    fill_rect(&c, 2, 1, 21, 1, rgb(0xE8B880));
    fill_rect(&c, 5, 3, 18, 4, rgb(0x3A6AC8));
    fill_rect(&c, 1, 7, 22, 8, rgb(0x5A5A64));
    for (int x = 2; x < 22; x += 3) put(&c, x, 8, rgb(0xE8E8E0));
    outline(&c, rgb(0x2A1A10));
    *w = 24; *h = 10;
    return canvas_texture(ren, &c);
}

/* ---------- layout ---------- */

static void layout_word(Board *b) {
    int n = b->n, ls = b->ls;
    if (n * 6 - 1 > 0 && (n * 6 - 1) * ls > b->colW * 92 / 100) ls = b->colW * 92 / 100 / (n * 6 - 1);
    if (ls < 1) ls = 1;
    b->ls = ls;
    b->wordX = b->colX - (n * 6 - 1) * ls / 2;
    int block = 7 * ls + ls * 3 + ls * 2;
    b->wordY = b->card.y + b->card.h / 2 - block / 2;
    int minY = b->board.y + b->ws * 12;
    if (b->wordY < minY) b->wordY = minY;
    b->marksY = b->wordY + 7 * ls + ls * 3 / 2 + ls / 2;
    b->arrowY = b->marksY + ls * 2;
    const char *s = WORDS[b->word].sentence;
    int len = (int)strlen(s), maxW = b->board.w * 94 / 100;
    int ss = b->ls * 3 / 10;
    if (ss < 1) ss = 1;
    while (ss > 1 && (len * 6 - 1) * ss > maxW) ss--;
    b->ss = ss;
    b->sentLines = 1;
    b->sentBreak = len;
    if ((len * 6 - 1) * ss > maxW) {                       /* two lines, split at the space nearest the middle */
        int best = -1;
        for (int i = 0; i < len; i++)
            if (s[i] == ' ' && (best < 0 || abs(i - len / 2) < abs(best - len / 2))) best = i;
        if (best > 0) { b->sentLines = 2; b->sentBreak = best; }
    }
    int below = b->card.y + b->card.h, room = b->board.y + b->board.h - below;
    b->sentY = below + (room - b->sentLines * 10 * ss + 3 * ss) / 2;
    b->hiStart = -1;                                       /* where the word is in its sentence */
    const char *w = WORDS[b->word].word;
    for (const char *p = strstr(s, w); p; p = strstr(p + 1, w)) {
        int at = (int)(p - s), end = at + b->n;
        int before = at == 0 || !(s[at - 1] >= 'A' && s[at - 1] <= 'Z'), after = !(s[end] >= 'A' && s[end] <= 'Z');
        if (before && after) { b->hiStart = at; b->hiLen = b->n; break; }
    }
}

static void set_word(Board *b, int idx) {
    b->word = idx;
    b->n = (int)strlen(WORDS[idx].word);
    if (b->n > MAXLEN) b->n = MAXLEN;
    parse_sounds(b);
    b->ls = b->ls0;
    layout_word(b);
    b->picFrame = -1;
    b->revWord[0] = (idx + NWORDS - 1) % NWORDS;           /* yesterday's and tomorrow's */
    b->revWord[1] = (idx + 1) % NWORDS;
    b->revFrame = -1;
}

static void *create(SDL_Renderer *ren, int w, int h) {
    Board *b = calloc(1, sizeof *b);
    b->w = w;
    b->h = h;
    b->S = h / 180 > 1 ? h / 180 : 1;
    b->W = (w + b->S - 1) / b->S;
    b->H = (h + b->S - 1) / b->S;
    int S = b->S, bx0, by0, bx1, by1, F, trayY;
    Canvas room = canvas_new(b->W, b->H);
    build_room(b, &room, &bx0, &by0, &bx1, &by1, &F, &trayY);
    b->room = canvas_texture(ren, &room);
    b->board = (SDL_Rect){ (bx0 + F) * S, (by0 + F) * S, (bx1 - bx0 - 2 * F + 1) * S, (by1 - by0 - 2 * F + 1) * S };
    b->atlas = make_atlas(ren);
    b->eraser = make_eraser(ren, &b->eraserW, &b->eraserH);
    b->eraserW *= S;
    b->eraserH *= S;
    b->restX = (bx1 - F - 34) * S;
    b->restY = (trayY - 9) * S;
    b->eraserX = (float)b->restX;
    b->eraserY = (float)b->restY;

    SDL_Rect bd = b->board;
    b->ws = (int)(bd.h * 0.045f / FONT_H);
    if (b->ws < 1) b->ws = 1;
    float aw = bd.w * 0.42f / (PW + 4), ah = bd.h * 0.76f / (PH + 4);
    b->ps = aw < ah ? aw : ah;
    if (b->ps >= 2) b->ps = floorf(b->ps);
    int cw = (int)((PW + 4) * b->ps), ch = (int)((PH + 4) * b->ps);
    b->card = (SDL_Rect){ bd.x + bd.w * 4 / 100, bd.y + ((int)(bd.h * 0.80f) - ch) / 2, cw, ch };
    if (b->card.y < bd.y + 2) b->card.y = bd.y + 2;
    b->pic = (SDL_Rect){ b->card.x + (int)(2 * b->ps), b->card.y + (int)(2 * b->ps), (int)(PW * b->ps), (int)(PH * b->ps) };
    int col0 = b->card.x + b->card.w + bd.w * 3 / 100, col1 = bd.x + bd.w * 97 / 100;
    b->colX = (col0 + col1) / 2;
    b->colW = col1 - col0;
    float l1 = b->colW * 0.92f / 29, l2 = bd.h * 0.26f / FONT_H;
    b->ls0 = (int)(l1 < l2 ? l1 : l2);
    if (b->ls0 < 1) b->ls0 = 1;
    float r1 = b->colW * 0.42f / (PW + 4), r2 = bd.h * 0.46f / (PH + 4);
    b->rps = r1 < r2 ? r1 : r2;
    if (b->rps >= 2) b->rps = floorf(b->rps);
    int rcw = (int)((PW + 4) * b->rps), rch = (int)((PH + 4) * b->rps);
    int block = b->ws * 10 + rch + b->ws * 18;
    b->review0Y = b->card.y + b->card.h / 2 - block / 2;
    for (int i = 0; i < 2; i++) {
        int cx = b->colX + (i ? 1 : -1) * b->colW / 4;
        b->reviewCard[i] = (SDL_Rect){ cx - rcw / 2, b->review0Y + b->ws * 10, rcw, rch };
        b->reviewPic[i] = (SDL_Rect){ b->reviewCard[i].x + (int)(2 * b->rps), b->reviewCard[i].y + (int)(2 * b->rps), (int)(PW * b->rps), (int)(PH * b->rps) };
    }

    b->art = canvas_new(PW, PH);
    b->picTex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, PW, PH);
    if (b->picTex) SDL_SetTextureScaleMode(b->picTex, SDL_ScaleModeNearest);
    for (int i = 0; i < 2; i++) {
        b->revArt[i] = canvas_new(PW, PH);
        b->revTex[i] = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, PW, PH);
        if (b->revTex[i]) {
            SDL_SetTextureScaleMode(b->revTex[i], SDL_ScaleModeNearest);
            SDL_SetTextureBlendMode(b->revTex[i], SDL_BLENDMODE_BLEND);
        }
    }
    today(&b->days, &b->wday);
    {
        int ws = b->ws, dw = (9 * 6 - 1) * ws;               /* room for the longest day, WEDNESDAY */
        b->dayRect = (SDL_Rect){ bd.x + bd.w - dw - ws * 7, bd.y + ws * 2, dw + ws * 4, ws * 14 };
    }
    set_word(b, word_for_day(b->days));
    b->nextWord = b->word;
    b->state = ST_REST;
    b->dur = 0.8f;
    b->loop = 1;
    return b;
}

static void destroy(void *p) {
    Board *b = p;
    SDL_DestroyTexture(b->room);
    SDL_DestroyTexture(b->atlas);
    SDL_DestroyTexture(b->eraser);
    if (b->picTex) SDL_DestroyTexture(b->picTex);
    for (int i = 0; i < 2; i++) {
        if (b->revTex[i]) SDL_DestroyTexture(b->revTex[i]);
        free(b->revArt[i].px);
    }
    free(b->art.px);
    free(b);
}

/* ---------- what happens ---------- */

static void add_part(Board *b, float x, float y, float vx, float vy, int kind, float size, uint32_t col, float life) {
    for (int i = 0; i < PARTS; i++) {
        Part *q = &b->parts[i];
        if (q->life > 0) continue;
        *q = (Part){ x, y, vx, vy, life, life, size, kind, col };
        return;
    }
}

static void chalk_puff(Board *b, float x, float y, int n, float spread) {
    for (int i = 0; i < n; i++)
        add_part(b, x + frand(-1, 1) * spread, y + frand(-1, 1) * spread * 0.3f, frand(-1, 1) * b->ls * 3, frand(-2, 0.5f) * b->ls * 2,
                 P_DUST, (float)(b->ls > 3 ? b->ls / 3 : 1), CHALK, frand(0.4f, 0.9f));
}

static float letter_x(const Board *b, int i) { return (float)(b->wordX + i * 6 * b->ls); }
static float appear_at(int i) { return 0.4f + i * LETTER_GAP; }

static float spell_secs(const Board *b) { return appear_at(b->n) + 0.2f; }
static float sound_secs(const Board *b) { return b->nsounds * 0.3f + 0.6f + b->nsounds * SOUND_STEP + 0.4f; }
static float type_secs(const Board *b) { return (float)strlen(WORDS[b->word].sentence) * TYPE_STEP + 0.8f; }

static void set_state(Board *b, int st, float dur) { b->state = st; b->stateT = 0; b->dur = dur; }

/* The region the eraser wipes for the word, and for the sentence. */
static void erase_regions(Board *b) {
    int ls = b->ls, ss = b->ss;
    b->wordRect = (SDL_Rect){ b->wordX - ls * 2, b->wordY - ls * 2, (b->n * 6 - 1) * ls + ls * 4, b->arrowY - b->wordY + ls * 4 };
    const char *s = WORDS[b->word].sentence;
    int len = (int)strlen(s), l1 = b->sentLines == 2 ? b->sentBreak : len;
    int l2 = len - l1 - 1, longest = l1 > l2 ? l1 : l2, wpx = (longest * 6 - 1) * ss;
    b->sentRect = (SDL_Rect){ b->board.x + b->board.w / 2 - wpx / 2 - ss * 4, b->sentY - ss * 3, wpx + ss * 8, b->sentLines * 10 * ss + ss * 3 };
}

static void next_round(Board *b) {
    if (b->pending) { b->pending = 0; set_state(b, ST_FLIP, 1.2f); return; }
    if (b->loop % REVIEW_EVERY == 0) set_state(b, ST_REVIEW, REVIEW_SECS);
    else set_state(b, ST_REST, 3.0f);
}

static void step(void *p, float dt) {
    Board *b = p;
    b->t += dt;
    b->stateT += dt;
    b->picT += dt;
    if (b->state != ST_ERASE && b->smudge > 0) b->smudge -= dt / 5;
    b->checkT -= dt;
    if (b->checkT <= 0) {                                  /* a new day brings a new word */
        b->checkT = 2;
        long days;
        int wday;
        today(&days, &wday);
        b->wday = wday;
        if (days != b->days) {
            b->days = days;
            b->nextWord = word_for_day(days);
            b->pending = b->nextWord != b->word;
        }
    }
    if (g_force_boom) {                                    /* windowed N: the next word, now */
        g_force_boom = 0;
        b->nextWord = (b->word + 1) % NWORDS;
        b->pending = 0;
        b->smudge = 0;
        set_state(b, ST_FLIP, 1.2f);
    }
    if (g_force_egg) {                                     /* windowed E: yesterday and tomorrow, now */
        g_force_egg = 0;
        b->smudge = 0;
        set_state(b, ST_REVIEW, REVIEW_SECS);
    }
    if (b->state == ST_FLIP && b->stateT >= b->dur * 0.5f && b->word != b->nextWord) set_word(b, b->nextWord);
    if (b->stateT >= b->dur) {
        switch (b->state) {
        case ST_FLIP: set_state(b, ST_REST, 1.0f); break;
        case ST_REST: set_state(b, ST_SPELL, spell_secs(b)); break;
        case ST_SPELL: set_state(b, ST_SOUND, sound_secs(b)); break;
        case ST_SOUND:
            set_state(b, ST_BOUNCE, 1.8f);
            for (int i = 0; i < 14; i++)
                add_part(b, b->wordX + frand(0, (float)(b->n * 6 * b->ls)), b->wordY + frand(-1, 1) * b->ls * 2, frand(-1, 1) * b->ls * 6,
                         -frand(2, 7) * b->ls, P_STAR, b->ls * frand(0.8f, 1.4f), (uint32_t[]){ CHALK_YELLOW, CHALK_PINK, CHALK_BLUE, 0xFFFFFF }[i % 4], frand(1.0f, 1.8f));
            break;
        case ST_BOUNCE: set_state(b, ST_TYPE, type_secs(b)); break;
        case ST_TYPE: set_state(b, ST_HOLD, HOLD_SECS); break;
        case ST_HOLD:
            erase_regions(b);
            b->eraseWordX = b->eraseSentX = -1;
            b->smudge = 1;
            set_state(b, ST_ERASE, ERASE_SECS);
            break;
        case ST_ERASE:
            b->loop++;
            b->eraserX = (float)b->restX;
            b->eraserY = (float)b->restY;
            next_round(b);
            break;
        default: next_round(b); break;   /* REVIEW */
        }
    }
    if (b->state == ST_SPELL) {                            /* a puff of chalk as each letter lands */
        for (int i = 0; i < b->n; i++) {
            float a = appear_at(i);
            if (b->stateT >= a && b->stateT - dt < a)
                chalk_puff(b, letter_x(b, i) + 2.5f * b->ls, (float)(b->wordY + 7 * b->ls), 8, 3.0f * b->ls);
        }
    }
    if (b->state == ST_ERASE) {                            /* the eraser's trip: word, sentence, home */
        float k = b->stateT, T = ERASE_SECS;
        SDL_Rect wr = b->wordRect, sr = b->sentRect;
        float ex, ey, eh = (float)b->eraserH;
        float w0x = wr.x - b->eraserW * 0.5f, w1x = (float)(wr.x + wr.w) - b->eraserW * 0.5f;
        float s0x = sr.x - b->eraserW * 0.5f, s1x = (float)(sr.x + sr.w) - b->eraserW * 0.5f;
        float zig = (sinf(b->stateT * 26) * 0.5f + 0.5f);
        if (k < 0.35f) {
            float u = ease(k / 0.35f);
            ex = lerp((float)b->restX, w0x, u); ey = lerp((float)b->restY, wr.y + 0.0f, u);
        } else if (k < 1.15f) {
            float u = (k - 0.35f) / 0.8f;
            ex = lerp(w0x, w1x, u); ey = wr.y + zig * (wr.h - eh);
            b->eraseWordX = ex + b->eraserW * 0.5f;
        } else if (k < 1.45f) {
            float u = ease((k - 1.15f) / 0.3f);
            ex = lerp(w1x, s0x, u); ey = lerp(wr.y + 0.0f, sr.y + 0.0f, u);
            b->eraseWordX = 1e9f;
        } else if (k < 2.25f) {
            float u = (k - 1.45f) / 0.8f;
            ex = lerp(s0x, s1x, u); ey = sr.y + zig * (sr.h - eh * 0.6f);
            b->eraseSentX = ex + b->eraserW * 0.5f;
        } else {
            float u = ease((k - 2.25f) / (T - 2.25f));
            ex = lerp(s1x, (float)b->restX, u); ey = lerp(sr.y + 0.0f, (float)b->restY, u);
            b->eraseSentX = 1e9f;
        }
        if ((k > 0.35f && k < 1.15f) || (k > 1.45f && k < 2.25f))
            if (irand(2)) add_part(b, ex + frand(0, (float)b->eraserW), ey + eh, frand(-1, 1) * b->S * 8, frand(0, 1) * b->S * 10,
                                   P_DUST, (float)b->S, CHALK, frand(0.5f, 1.0f));
        b->eraserX = ex;
        b->eraserY = ey;
    }
    for (int i = 0; i < PARTS; i++) {
        Part *q = &b->parts[i];
        if (q->life <= 0) continue;
        q->life -= dt;
        q->x += q->vx * dt;
        q->y += q->vy * dt;
        q->vx *= 1 - 2.5f * dt;
        q->vy = q->vy * (1 - 2.5f * dt) + (q->kind == P_DUST ? b->h * 0.08f : b->h * 0.04f) * dt;
    }
}

/* ---------- drawing ---------- */

static void chalk_char(Board *b, SDL_Renderer *r, char ch, float x, float y, float sc, uint32_t col, float alpha) {
    int i = atlas_index(ch);
    if (i < 0 || alpha <= 0.01f) return;
    SDL_Rect src = { i * FONT_W * DOT, 0, FONT_W * DOT, FONT_H * DOT };
    SDL_FRect d = { x, y, FONT_W * sc, FONT_H * sc };
    SDL_SetTextureColorMod(b->atlas, (col >> 16) & 255, (col >> 8) & 255, col & 255);
    SDL_SetTextureAlphaMod(b->atlas, (Uint8)(255 * clampf(alpha, 0, 1)));
    SDL_RenderCopyF(r, b->atlas, &src, &d);
}

static void chalk_text(Board *b, SDL_Renderer *r, const char *s, int n, float x, float y, float sc, uint32_t col, float alpha) {
    for (int i = 0; i < n && s[i]; i++) chalk_char(b, r, s[i], x + i * 6 * sc, y, sc, col, alpha);
}

static void frect(SDL_Renderer *r, float x, float y, float w, float h, uint32_t col, int a) {
    SDL_FRect rc = { x, y, w, h };
    SDL_SetRenderDrawColor(r, (col >> 16) & 255, (col >> 8) & 255, col & 255, (Uint8)(a < 0 ? 0 : a > 255 ? 255 : a));
    SDL_RenderFillRectF(r, &rc);
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

/* A strip of sticky tape across a corner, turned by angle a. */
static void tape(SDL_Renderer *r, float cx, float cy, float len, float wid, float a) {
    float co = cosf(a), si = sinf(a), hx = len / 2, hy = wid / 2;
    SDL_Color c = { 0xF8, 0xF0, 0xC8, 190 };
    SDL_Vertex v[4] = {
        { { cx - hx * co + hy * si, cy - hx * si - hy * co }, c, { 0, 0 } },
        { { cx + hx * co + hy * si, cy + hx * si - hy * co }, c, { 0, 0 } },
        { { cx + hx * co - hy * si, cy + hx * si + hy * co }, c, { 0, 0 } },
        { { cx - hx * co - hy * si, cy - hx * si + hy * co }, c, { 0, 0 } },
    };
    int idx[6] = { 0, 1, 2, 0, 2, 3 };
    SDL_RenderGeometry(r, NULL, v, 4, idx, 6);
}

static void render_pic(Canvas *c, SDL_Texture *tex, int word, float t) {
    WORDS[word].pic(c, t);
    if (tex) SDL_UpdateTexture(tex, NULL, c->px, c->w * 4);
}

/* A picture card on the board; sx squeezes it sideways (a card flipping over). */
static void draw_card(SDL_Renderer *r, SDL_Rect card, SDL_Rect pic, SDL_Texture *tex, float sx, float alpha, float pop, float ps) {
    if (sx < 0.02f || alpha <= 0.01f) return;
    float cx = card.x + card.w / 2.0f, cy = card.y + card.h / 2.0f, k = pop;
    float w = card.w * sx * k, h = card.h * k;
    Uint8 A = (Uint8)(255 * alpha);
    frect(r, cx - w / 2 + ps * 1.5f, cy - h / 2 + ps * 2, w, h, 0x000000, (int)(70 * alpha));
    frect(r, cx - w / 2, cy - h / 2, w, h, 0xFFF8E8, A);
    if (sx > 0.98f && tex) {
        SDL_FRect d = { cx - pic.w * k / 2, cy - h / 2 + (pic.y - card.y) * k, pic.w * k, pic.h * k };
        SDL_SetTextureAlphaMod(tex, A);
        SDL_RenderCopyF(r, tex, NULL, &d);
    } else if (tex) {
        float pw = pic.w * sx * k;
        SDL_FRect d = { cx - pw / 2, cy - h / 2 + (pic.y - card.y) * k, pw, pic.h * k };
        SDL_SetTextureAlphaMod(tex, A);
        SDL_RenderCopyF(r, tex, NULL, &d);
    }
    if (sx > 0.98f && alpha > 0.5f) {
        float tl = card.w * 0.16f * k, tw = card.h * 0.07f * k;
        tape(r, cx - w / 2 + tl * 0.25f, cy - h / 2 + tw * 0.4f, tl, tw, -0.7f);
        tape(r, cx + w / 2 - tl * 0.25f, cy - h / 2 + tw * 0.4f, tl, tw, 0.7f);
    }
}

static uint32_t letter_color(const Board *b, int i, float *scale, float *dy) {
    char ch = WORDS[b->word].word[i];
    float k = b->stateT;
    *scale = 1;
    *dy = 0;
    switch (b->state) {
    case ST_SPELL: {
        float u = (k - appear_at(i)) / 0.35f;
        if (u < 1) {                                       /* pops in, a little too big, then settles */
            float s = 1.70158f, v = u - 1;
            *scale = 1 + v * v * ((s + 1) * v + s);
        }
        return mixc(CHALK_YELLOW, CHALK, clampf((k - appear_at(i)) / 0.9f, 0, 1));
    }
    case ST_SOUND: {
        float s0 = b->nsounds * 0.3f + 0.6f;
        int cur = (int)floorf((k - s0) / SOUND_STEP);
        if (k >= s0 && cur < b->nsounds) {
            const Sound *sd = &b->sounds[cur];
            if (i >= sd->start && i < sd->start + sd->len) {
                float u = (k - s0) / SOUND_STEP - cur;
                *scale = 1 + 0.15f * sinf(clampf(u * 1.4f, 0, 1) * PI_F);
                *dy = -b->ls * 0.6f * sinf(clampf(u * 1.4f, 0, 1) * PI_F);
                return sd->silent ? 0xB8C8C0 : CHALK_YELLOW;
            }
        }
        return CHALK;
    }
    case ST_BOUNCE: {
        for (int wave = 0; wave < 2; wave++) {
            float u = (k - wave * 0.8f - i * 0.11f) / 0.45f;
            if (u > 0 && u < 1) *dy -= sinf(u * PI_F) * b->ls * 2.2f;
        }
        static const uint32_t RAIN[5] = { 0xFF9E9E, 0xFFD27A, 0xFFF08A, 0xA8F0A0, 0x9ED8FF };
        return mixc(RAIN[i % 5], CHALK, clampf((k - 1.3f) / 0.5f, 0, 1));
    }
    case ST_HOLD:
        if (b->loop % 2 == 0 && k > 1.5f && k < HOLD_SECS - 2 && is_vowel(ch))
            return mixc(CHALK, CHALK_PINK, clampf((k - 1.5f) / 0.6f, 0, 1) * (0.85f + 0.15f * sinf(k * 3)));
        return CHALK;
    default: return CHALK;
    }
}

static void draw_word(Board *b, SDL_Renderer *r) {
    int st = b->state, ls = b->ls;
    if (st < ST_SPELL || st > ST_ERASE) return;
    const char *w = WORDS[b->word].word;
    for (int i = 0; i < b->n; i++) {
        if (st == ST_SPELL && b->stateT < appear_at(i)) break;
        float x = letter_x(b, i), cx = x + 2.5f * ls;
        if (st == ST_ERASE && cx < b->eraseWordX) continue;
        float sc, dy;
        uint32_t col = letter_color(b, i, &sc, &dy);
        float cy = b->wordY + 3.5f * ls + dy;
        chalk_char(b, r, w[i], cx - 2.5f * ls * sc, cy - 3.5f * ls * sc, ls * sc, col, 1);
    }
    if (st == ST_SPELL) return;
    /* sound marks: a dot under a letter, a dash under letters that make one sound */
    float k = b->stateT, s0 = b->nsounds * 0.3f + 0.6f;
    for (int j = 0; j < b->nsounds; j++) {
        const Sound *sd = &b->sounds[j];
        if (sd->silent) continue;
        if (st == ST_SOUND && k < 0.3f * j + 0.2f) break;
        float x0 = letter_x(b, sd->start) + 2.5f * ls, x1 = letter_x(b, sd->start + sd->len - 1) + 2.5f * ls;
        if (st == ST_ERASE && x1 < b->eraseWordX) continue;
        int on = st == ST_SOUND && k >= s0 && (int)floorf((k - s0) / SOUND_STEP) == j;
        uint32_t col = on ? CHALK_YELLOW : CHALK;
        if (sd->len == 1) frect(r, x0 - ls * 0.6f, b->marksY - ls * 0.6f, ls * 1.2f, ls * 1.2f, col, 235);
        else frect(r, x0 - ls * 0.4f, b->marksY - ls * 0.35f, x1 - x0 + ls * 0.8f, ls * 0.7f, col, 235);
    }
    if (st == ST_SOUND && k < s0 - 0.25f) return;
    /* the arrow to read along, and a star sliding under each sound in turn */
    float ax0 = letter_x(b, 0), ax1 = letter_x(b, b->n - 1) + 5 * ls + ls;
    if (st == ST_ERASE) ax0 = fmaxf(ax0, b->eraseWordX);
    if (ax1 > ax0) {
        float th = ls * 0.35f > 1 ? ls * 0.35f : 1;
        frect(r, ax0, b->arrowY - th / 2, ax1 - ax0 - ls * 0.5f, th, CHALK, 200);
        if (st != ST_ERASE || ax1 > b->eraseWordX + ls)
            for (int q = 0; q < (int)(ls * 1.1f); q++)
                frect(r, ax1 - ls * 1.4f + q, b->arrowY - (ls * 1.1f - q) * 0.75f, 1, (ls * 1.1f - q) * 1.5f, CHALK, 200);
    }
    if (st == ST_SOUND && k >= s0) {
        float pos = (k - s0) / SOUND_STEP;
        int cur = (int)floorf(pos);
        if (cur >= b->nsounds) cur = b->nsounds - 1;
        const Sound *sd = &b->sounds[cur];
        float target = (letter_x(b, sd->start) + letter_x(b, sd->start + sd->len - 1)) / 2 + 2.5f * ls;
        float prev = target;
        if (cur > 0) {
            const Sound *pv = &b->sounds[cur - 1];
            prev = (letter_x(b, pv->start) + letter_x(b, pv->start + pv->len - 1)) / 2 + 2.5f * ls;
        } else prev = letter_x(b, 0);
        float x = lerp(prev, target, ease((pos - cur) * 3));
        star(r, x, b->arrowY + ls * 0.1f, ls * 0.9f, b->t, hexcol(CHALK_YELLOW));
    }
}

static void draw_sentence(Board *b, SDL_Renderer *r) {
    int st = b->state;
    if (st < ST_TYPE || st > ST_ERASE) return;
    const char *s = WORDS[b->word].sentence;
    int len = (int)strlen(s), ss = b->ss, shown = len;
    if (st == ST_TYPE) shown = (int)(b->stateT / TYPE_STEP);
    if (shown > len) shown = len;
    float lastX = 0, lastY = 0;
    for (int line = 0; line < b->sentLines; line++) {
        int a = line ? b->sentBreak + 1 : 0, e = line || b->sentLines == 1 ? len : b->sentBreak;
        float x = b->board.x + b->board.w / 2.0f - ((e - a) * 6 - 1) * ss / 2.0f, y = (float)(b->sentY + line * 10 * ss);
        for (int i = a; i < e && i < shown; i++) {
            float cx = x + (i - a) * 6 * ss;
            lastX = cx + 6 * ss; lastY = y;
            if (st == ST_ERASE && cx + 2.5f * ss < b->eraseSentX) continue;
            int hi = b->hiStart >= 0 && i >= b->hiStart && i < b->hiStart + b->hiLen;
            chalk_char(b, r, s[i], cx, y, (float)ss, hi ? CHALK_YELLOW : CHALK, 1);
        }
        if (line == 0 && shown < e) break;
    }
    if (st == ST_TYPE && shown < len) {                    /* the chalk doing the writing */
        float wob = sinf(b->t * 30) * ss * 0.4f;
        if (shown == 0) { lastX = b->board.x + b->board.w / 2.0f - (len * 6 - 1) * ss / 2.0f; lastY = (float)b->sentY; }
        thick_line(r, lastX + ss, lastY + 7 * ss + wob, lastX + ss * 6, lastY + ss + wob, ss * 1.6f, hexcol(CHALK));
        thick_line(r, lastX + ss * 5, lastY + ss * 2 + wob, lastX + ss * 6, lastY + ss + wob, ss * 1.6f, hexcol(0xD8D4C8));
    }
}

static void draw_review(Board *b, SDL_Renderer *r) {
    if (b->state != ST_REVIEW) return;
    float k = b->stateT;
    static const char *const LABEL[2] = { "YESTERDAY", "TOMORROW" };
    for (int i = 0; i < 2; i++) {
        float ai = clampf((k - i * 0.4f) / 0.6f, 0, 1) * clampf((b->dur - k) / 0.6f, 0, 1);
        float u = clampf((k - i * 0.4f) / 0.4f, 0, 1), s = 1.70158f, v = u - 1, pop = 1 + v * v * ((s + 1) * v + s);
        SDL_Rect cd = b->reviewCard[i];
        int ws = b->ws, cx = cd.x + cd.w / 2;
        chalk_text(b, r, LABEL[i], 9, cx - (strlen(LABEL[i]) * 6 - 1) * ws / 2.0f, (float)(cd.y - ws * 10), (float)ws, i ? CHALK_BLUE : CHALK_PINK, ai);
        draw_card(r, cd, b->reviewPic[i], b->revTex[i], 1, ai, u > 0 ? pop : 0.01f, b->rps);
        const char *w = WORDS[b->revWord[i]].word;
        int n = (int)strlen(w), sc = ws * 2;
        chalk_text(b, r, w, n, cx - (n * 6 - 1) * sc / 2.0f, (float)(cd.y + cd.h + ws * 4), (float)sc, CHALK, ai);
    }
}

static void draw(void *p, SDL_Renderer *r) {
    Board *b = p;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_Rect full = { 0, 0, b->W * b->S, b->H * b->S };
    SDL_RenderCopy(r, b->room, NULL, &full);

    int frame = (int)(b->picT * PIC_FPS);
    if (frame != b->picFrame) {
        b->picFrame = frame;
        render_pic(&b->art, b->picTex, b->word, b->picT);
    }
    if (b->state == ST_REVIEW && frame != b->revFrame) {
        b->revFrame = frame;
        for (int i = 0; i < 2; i++) render_pic(&b->revArt[i], b->revTex[i], b->revWord[i], b->picT + i * 1.7f);
    }

    if (b->hasStrip && b->state == ST_SPELL) {             /* the letter lights up on the alphabet strip too */
        for (int i = 0; i < b->n; i++) {
            float u = (b->stateT - appear_at(i)) / 1.0f;
            char ch = WORDS[b->word].word[i];
            if (u < 0 || u > 1 || ch < 'A' || ch > 'Z') continue;
            int S = b->S, x = (b->stripX + (ch - 'A') * b->stripCW + 1) * S, y = b->stripY * S;
            frect(r, (float)x, (float)y, (float)(b->stripCW - 2) * S, (float)b->stripCH * S, 0xFFFFFF, (int)(170 * (1 - u)));
        }
    }

    float sx = 1;                                          /* the picture card, flipping over for a new word */
    if (b->state == ST_FLIP) sx = fabsf(cosf(PI_F * clampf(b->stateT / b->dur, 0, 1)));
    draw_card(r, b->card, b->pic, b->picTex, sx, 1, 1, b->ps);

    const char *day = DAYS[b->wday % 7];                   /* today, in the corner as teachers do */
    int ws = b->ws, dw = ((int)strlen(day) * 6 - 1) * ws;
    float dx = (float)(b->board.x + b->board.w - dw - ws * 5), dy = (float)(b->board.y + ws * 4);
    chalk_text(b, r, day, 9, dx, dy, (float)ws, CHALK, 0.85f);
    frect(r, dx, dy + 9 * ws, (float)dw, (float)(ws > 1 ? ws / 2 + 1 : 1), CHALK, 150);

    if (b->state == ST_HOLD && b->loop % 2 == 0 && b->stateT > 1.5f && b->stateT < HOLD_SECS - 2) {
        float a = clampf((b->stateT - 1.5f) / 0.6f, 0, 1) * clampf((HOLD_SECS - 2 - b->stateT) / 0.6f, 0, 1);
        int sc = b->ws;
        chalk_text(b, r, "VOWELS", 6, (float)b->wordX, (float)(b->wordY - sc * 10), (float)sc, CHALK_PINK, a);
    }
    draw_word(b, r);
    draw_sentence(b, r);
    draw_review(b, r);

    if (b->smudge > 0 && (b->state == ST_ERASE || b->state == ST_REST || b->state == ST_REVIEW || b->state == ST_FLIP)) {
        SDL_Rect rr[2] = { b->wordRect, b->sentRect };      /* the haze an eraser leaves */
        float upto[2] = { b->eraseWordX, b->eraseSentX };
        for (int i = 0; i < 2; i++) {
            float x1 = fminf((float)(rr[i].x + rr[i].w), upto[i]);
            if (b->state != ST_ERASE) x1 = (float)(rr[i].x + rr[i].w);
            if (x1 <= rr[i].x) continue;
            for (int k = 0; k < 9; k++) {                  /* uneven streaks, not a box */
                float j0 = ((k * 37) % 11) / 11.0f, j1 = ((k * 53) % 13) / 13.0f;
                float sx0 = rr[i].x + rr[i].w * 0.12f * j0, sx1 = fminf(x1, rr[i].x + rr[i].w * (1 - 0.12f * j1));
                if (sx1 > sx0)
                    frect(r, sx0, rr[i].y + rr[i].h * k / 9.0f, sx1 - sx0, rr[i].h / 9.0f * 1.4f, CHALK,
                          (int)((7 + (k * 5) % 9) * clampf(b->smudge, 0, 1)));
            }
        }
    }

    float big = b->state == ST_ERASE ? 1 + 0.6f * sinf(PI_F * clampf(b->stateT / ERASE_SECS, 0, 1) * 1.0f) : 1;   /* held up close */
    SDL_FRect er = { b->eraserX + b->eraserW * (1 - big) / 2, b->eraserY + b->eraserH * (1 - big) / 2, b->eraserW * big, b->eraserH * big };
    SDL_RenderCopyF(r, b->eraser, NULL, &er);

    for (int i = 0; i < PARTS; i++) {
        Part *q = &b->parts[i];
        if (q->life <= 0) continue;
        float f = q->life / q->max;
        SDL_Color c = hexcol(q->col);
        c.a = (Uint8)(220 * (f < 0.4f ? f / 0.4f : 1));
        if (q->kind == P_STAR) star(r, q->x, q->y, q->size, q->life * 3, c);
        else frect(r, q->x, q->y, q->size, q->size, q->col, c.a);
    }

    b->infoRect = (SDL_Rect){ 0, 0, 0, 0 };
    if (g_show_info) {
        char line[160];
        int sc = b->h / 360 > 2 ? b->h / 360 : 2;
        snprintf(line, sizeof line, "WORD %d OF %d: %s   DAY %ld   ROUND %d   %s %.0fS", b->word + 1, NWORDS, WORDS[b->word].word,
                 b->days, b->loop, STATE_NAMES[b->state], b->dur - b->stateT > 0 ? b->dur - b->stateT : 0);
        int y = b->h - 10 * sc;
        draw_text_shadow(r, line, 4 * sc, y, sc, hexcol(0xFFFFFF));
        b->infoRect = (SDL_Rect){ 0, y - 2 * sc, text_width(line, sc) + 8 * sc, 11 * sc };
    }
}

/* Between rounds: the board is wiped and only the picture is up. */
static int at_rest(void *p) {
    const Board *b = p;
    return b->state == ST_REST;
}

/* The clock keeps off the picture, the word, the sentence and the day. */
static int keep_clear(void *p, SDL_Rect *out, int max) {
    Board *b = p;
    int n = 0;
    erase_regions(b);
    SDL_Rect cr = b->card;
    SDL_Rect rs[6] = { { cr.x - 4, cr.y - 4, cr.w + 8, cr.h + 8 }, b->wordRect, b->sentRect, b->dayRect,
                       { b->colX - b->colW / 2, b->review0Y, b->colW, b->reviewCard[0].y + b->reviewCard[0].h + b->ws * 20 - b->review0Y },
                       b->infoRect };
    for (int i = 0; i < 6 && n < max; i++)
        if (rs[i].w > 0 && rs[i].h > 0 && (i != 4 || b->state == ST_REVIEW)) out[n++] = rs[i];
    return n;
}

const Saver saver_word = { "word", "WORD", create, step, draw, destroy, at_rest, keep_clear, "N NEXT WORD   E YESTERDAY AND TOMORROW   I INFO" };
