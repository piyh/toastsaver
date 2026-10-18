/* DICE: a felt dice tray. A d4, d6, d8, d10, d12 and d20, each in its own bright
   colour, tumble in from the side, bounce, spin and settle. Every result flies up to
   a little chalkboard that adds them up. Top rolls get sparkles and a happy hop (a
   natural 20 gets a gold glow and a tiny "CRIT!"); a 1 gets a sad droop and a little
   rain cloud. Everything is pixel art drawn at a low resolution and scaled up. */
#include "../engine.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

enum { D4, D6, D8, D10, D12, D20, KINDS };
enum { ROLL, SUM, EXIT, GAP };
enum { CALM, MAXED, CRIT, ONE, FAIL };
enum { P_SPARK, P_CONFETTI, P_DUST, P_RAIN, P_CHALK };

#define MAXDICE 6
#define PARTS 260
#define POPS 8
#define NB 5
#define WOMP_COL 0xD4DCEA

static const int SIDES[KINDS] = { 4, 6, 8, 10, 12, 20 };
static const char *const NAMES[KINDS] = { "D4", "D6", "D8", "D10", "D12", "D20" };
static const uint32_t BODY[KINDS] = { 0xF0464E, 0xFF9230, 0xFFD23C, 0x26C6BE, 0x4F86F7, 0xA862F0 };
static const uint32_t CONFETTI[6] = { 0xFF5A6E, 0xFFD23C, 0x5BD46B, 0x4DA6FF, 0xC98BFF, 0xFFFFFF };
static const float BOUNCE[NB] = { 0.26f, 0.52f, 0.71f, 0.84f, 0.93f };

/* The handfuls we roll, picked at random (the first one is always the first roll). */
static const int SETS[][MAXDICE + 1] = {
    { 3, D6, D8, D20 }, { 1, D20 }, { 2, D6, D6 }, { 3, D6, D6, D6 },
    { 6, D4, D6, D8, D10, D12, D20 }, { 2, D8, D12 }, { 2, D10, D10 }, { 1, D20 },
    { 2, D4, D4 }, { 2, D20, D20 }, { 1, D12 }, { 3, D4, D6, D8 }, { 2, D6, D20 },
    { 1, D10 }, { 4, D6, D6, D6, D6 }, { 1, D20 }, { 2, D8, D8 }, { 3, D10, D12, D20 },
};
#define NSETS ((int)(sizeof SETS / sizeof SETS[0]))

typedef struct {
    int kind, value, shown, rollN, axis, react, settled, tallied, counted;
    float sx, sy, tx, ty, t0, dur, z0, spin, u;
    float x, y, z, ang, lsx, lsy, gsx, gsy, tint, impact, impactT, st;
    uint32_t tintCol;
} Die;

typedef struct { float x, y, vx, vy, life, max, ph; int kind; uint32_t col; } Part;
typedef struct { float x0, y0, t; int value, on; } Fly;
typedef struct { char txt[8]; float x, y, t, max; int sc; uint32_t col; } Pop;

typedef struct {
    int w, h, S, W, H, R, F, SP, small;   /* small: a picker tile, so a simpler board */
    int bx, by, bw, bh;
    float t, stateT, tick, hold;
    int state, n, rows, set, fromLeft, total, shown, hasCrit;
    char expr[48];
    Die die[MAXDICE];
    Fly fly[MAXDICE];
    Pop pop[POPS];
    Part parts[PARTS];
    Canvas sprite[KINDS][21];
    Canvas under, top;
    unsigned char *mark;
    SDL_Texture *felt, *underTex, *topTex;
} Dice;

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

/* f > 1 lightens towards white, f < 1 darkens. */
static uint32_t lit(uint32_t hex, float f) { return f >= 1 ? mixc(hex, 0xFFFFFF, f - 1) : shade(hex, f); }

static uint32_t rgba(uint32_t hex, int a) { return (hex << 8) | (uint32_t)(a < 0 ? 0 : (a > 255 ? 255 : a)); }

static void hline(Canvas *c, int x0, int x1, int y, uint32_t hex) { fill_rect(c, x0, y, x1, y, rgb(hex)); }

static void mul_px(Canvas *c, int x, int y, float f) {
    uint32_t p = get(c, x, y);
    if (p) put(c, x, y, rgb(shade(p >> 8, f)));
}

/* The font has no + or =, so those two live here. */
static const char *const PLUS[FONT_H] = { ".....", "..#..", "..#..", "#####", "..#..", "..#..", "....." };
static const char *const EQUALS[FONT_H] = { ".....", ".....", "#####", ".....", "#####", ".....", "....." };

/* 5x7 text straight onto a canvas, sc canvas pixels per font dot. */
static void glyphs(Canvas *c, const char *s, int x, int y, int sc, uint32_t col) {
    for (; *s; s++, x += (FONT_W + 1) * sc) {
        const char *const *g = *s == '+' ? PLUS : (*s == '=' ? EQUALS : font_glyph(*s));
        for (int j = 0; g && j < FONT_H; j++)
            for (int i = 0; i < FONT_W; i++)
                if (g[j][i] == '#') fill_rect(c, x + i * sc, y + j * sc, x + i * sc + sc - 1, y + j * sc + sc - 1, col);
    }
}

static int glyphs_w(const char *s, int sc) { int n = (int)strlen(s); return n ? n * (FONT_W + 1) * sc - sc : 0; }

/* ---------- the dice (one upright sprite per face) ---------- */

static const float LX = -0.6f, LY = -0.8f;   /* light from the top left */

static void facet(Canvas *c, float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3,
                  int n, float cx, float cy, uint32_t base, float k) {
    float xs[4] = { x0, x1, x2, x3 }, ys[4] = { y0, y1, y2, y3 };
    float mx = 0, my = 0;
    for (int i = 0; i < n; i++) { mx += xs[i]; my += ys[i]; }
    mx = mx / n - cx; my = my / n - cy;
    float l = sqrtf(mx * mx + my * my);
    float d = l > 0.01f ? (mx * LX + my * LY) / l : 0;
    fill_poly(c, xs, ys, n, rgb(lit(base, 1 + k * d)));
}

static void pip(Canvas *c, float x, float y, float r, uint32_t base, uint32_t ink) {
    fill_ellipse(c, x, y, r + 1, r + 1, rgb(shade(base, 0.45f)));
    fill_ellipse(c, x, y, r, r, rgb(ink));
    put(c, (int)(x - r * 0.4f), (int)(y - r * 0.4f), rgb(lit(ink, 1.6f)));
}

static void number(Canvas *c, int v, float cx, float cy, int sc, uint32_t base, uint32_t ink) {
    char s[4];
    snprintf(s, sizeof s, "%d", v);
    int w = glyphs_w(s, sc), x = (int)floorf(cx - w / 2.0f + 0.5f), y = (int)floorf(cy - FONT_H * sc / 2.0f + 0.5f);
    uint32_t dark = rgb(shade(base, 0.32f));
    int bar = v == 6 || v == 9, by = y + FONT_H * sc + 1;   /* 6 and 9 get a little bar underneath */
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            glyphs(c, s, x + dx, y + dy, sc, dark);
            if (bar) fill_rect(c, x + dx, by + dy, x + w - 1 + dx, by + dy, dark);
        }
    glyphs(c, s, x, y + 1, sc, rgb(ink == 0xFFFFFF ? shade(base, 0.55f) : 0xB07A10));
    glyphs(c, s, x, y, sc, rgb(ink));
    if (bar) fill_rect(c, x, by, x + w - 1, by, rgb(ink));
}

static Canvas make_die(int kind, int v, float R, int SP, uint32_t ink) {
    Canvas c = canvas_new(SP, SP);
    uint32_t base = BODY[kind];
    float cx = SP / 2.0f, cy = SP / 2.0f, ox[8], oy[8], k = 0.64f, fy = 0;
    int n = 0, sc = R >= 15 ? 2 : 1;
    switch (kind) {
    case D4:
        n = 3; cy += R * 0.22f; k = 0.6f;
        for (int i = 0; i < 3; i++) { float a = (-90 + 120 * i) * PI_F / 180; ox[i] = cosf(a) * R * 1.3f; oy[i] = sinf(a) * R * 1.3f; }
        fy = R * 0.06f;
        break;
    case D6: {
        float s = R * 0.9f, ch = R * 0.26f;
        float px[8] = { -s + ch, s - ch, s, s, s - ch, -s + ch, -s, -s };
        float py[8] = { -s, -s, -s + ch, s - ch, s, s, s - ch, -s + ch };
        n = 8; k = 0.74f;
        for (int i = 0; i < 8; i++) { ox[i] = px[i]; oy[i] = py[i]; }
        break;
    }
    case D8:
        n = 4; k = 0.6f;
        ox[0] = 0; oy[0] = -R * 1.22f; ox[1] = R * 1.02f; oy[1] = 0; ox[2] = 0; oy[2] = R * 1.22f; ox[3] = -R * 1.02f; oy[3] = 0;
        break;
    case D10: {                                        /* a kite face with a side facet each way */
        static const float P[6][2] = { { 0, -1.24f }, { 1.08f, -0.02f }, { 0.86f, 0.52f }, { 0, 1.0f }, { -0.86f, 0.52f }, { -1.08f, -0.02f } };
        n = 6; cy -= R * 0.04f;
        for (int i = 0; i < 6; i++) { ox[i] = P[i][0] * R; oy[i] = P[i][1] * R; }
        break;
    }
    case D12:
        n = 5; k = 0.64f; cy += R * 0.06f;
        for (int i = 0; i < 5; i++) { float a = (-90 + 72 * i) * PI_F / 180; ox[i] = cosf(a) * R * 1.14f; oy[i] = sinf(a) * R * 1.14f; }
        fy = R * 0.04f;
        break;
    default:
        n = 6;
        for (int i = 0; i < 6; i++) { float a = (-90 + 60 * i) * PI_F / 180; ox[i] = cosf(a) * R * 1.16f; oy[i] = sinf(a) * R * 1.16f; }
        break;
    }
    for (int i = 0; i < n; i++) { ox[i] += cx; oy[i] += cy; }
    fill_poly(&c, ox, oy, n, rgb(base));

    if (kind == D10) {
        float kx[4] = { ox[0], cx + R * 0.64f, ox[0], cx - R * 0.64f }, ky[4] = { oy[0], cy + R * 0.2f, cy + R * 0.74f, cy + R * 0.2f };
        for (int sd = 0; sd < 2; sd++) {
            int a = sd ? 1 : 5, b = sd ? 2 : 4, kk = sd ? 1 : 3;
            facet(&c, ox[0], oy[0], ox[a], oy[a], kx[kk], ky[kk], 0, 0, 3, cx, cy, base, 0.4f);
            float xs[5] = { ox[a], ox[b], ox[3], kx[2], kx[kk] }, ys[5] = { oy[a], oy[b], oy[3], ky[2], ky[kk] };
            float mx = (xs[0] + xs[1] + xs[2]) / 3 - cx, my = (ys[0] + ys[1] + ys[2]) / 3 - cy, l = sqrtf(mx * mx + my * my);
            fill_poly(&c, xs, ys, 5, rgb(lit(base, 0.88f + 0.4f * (mx * LX + my * LY) / l)));
        }
        fill_poly(&c, kx, ky, 4, rgb(lit(base, 1.06f)));
        for (int s = 0; s <= 24; s++) {                /* lit upper-left edge of the face */
            put(&c, (int)lerp(kx[0], kx[3], s / 24.0f), (int)lerp(ky[0], ky[3], s / 24.0f), rgb(lit(base, 1.3f)));
        }
        fy = R * 0.2f;
    } else if (kind == D20) {
        /* a triangle face in the middle with nine more around it */
        float f = 0.74f, ax[3], ay[3];
        for (int i = 0; i < 3; i++) { ax[i] = cx + (ox[i * 2] - cx) * f; ay[i] = cy + (oy[i * 2] - cy) * f; }
        for (int i = 0; i < 3; i++) {
            int v0 = i * 2, v1 = i * 2 + 1, v2 = (i * 2 + 2) % 6, a = i, b = (i + 1) % 3;
            facet(&c, ax[a], ay[a], ox[v1], oy[v1], ax[b], ay[b], 0, 0, 3, cx, cy, base, 0.22f);
            facet(&c, ox[v0], oy[v0], ox[v1], oy[v1], ax[a], ay[a], 0, 0, 3, cx, cy, shade(base, 0.86f), 0.42f);
            facet(&c, ox[v1], oy[v1], ox[v2], oy[v2], ax[b], ay[b], 0, 0, 3, cx, cy, shade(base, 0.86f), 0.42f);
        }
        fill_poly(&c, ax, ay, 3, rgb(lit(base, 1.08f)));
        for (int i = 0; i < 3; i++) {                  /* bright edges around the face */
            int j = (i + 1) % 3;
            for (int s = 0; s <= 20; s++) {
                float px = lerp(ax[i], ax[j], s / 20.0f), py = lerp(ay[i], ay[j], s / 20.0f);
                if (py < cy + R * 0.2f) put(&c, (int)px, (int)py, rgb(lit(base, 1.35f)));
            }
        }
        fy = R * 0.16f;
    } else {
        float ix[8], iy[8], fcx = cx, fcy = cy + fy;
        for (int i = 0; i < n; i++) { ix[i] = fcx + (ox[i] - cx) * k; iy[i] = fcy + (oy[i] - cy) * k; }
        for (int i = 0; i < n; i++) {
            int j = (i + 1) % n;
            facet(&c, ox[i], oy[i], ox[j], oy[j], ix[j], iy[j], ix[i], iy[i], 4, cx, cy, base, 0.42f);
        }
        fill_poly(&c, ix, iy, n, rgb(lit(base, 1.06f)));
        for (int i = 0; i < n; i++) {                  /* a lit rim on the top-left edges of the face */
            int j = (i + 1) % n;
            float nx = (iy[j] - iy[i]), ny = -(ix[j] - ix[i]);
            if (nx * LX + ny * LY >= 0) continue;
            for (int s = 0; s <= 24; s++)
                put(&c, (int)lerp(ix[i], ix[j], s / 24.0f), (int)lerp(iy[i], iy[j], s / 24.0f), rgb(lit(base, 1.3f)));
        }
    }

    if (kind == D6) {
        static const float P[7][6][2] = {
            {{0}}, {{0, 0}}, {{-1, -1}, {1, 1}}, {{-1, -1}, {0, 0}, {1, 1}},
            {{-1, -1}, {1, -1}, {-1, 1}, {1, 1}}, {{-1, -1}, {1, -1}, {0, 0}, {-1, 1}, {1, 1}},
            {{-1, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {1, 1}},
        };
        float sp = R * 0.38f, pr = R * 0.12f > 1.2f ? R * 0.12f : 1.2f;
        for (int i = 0; i < v; i++) pip(&c, cx + P[v][i][0] * sp, cy + P[v][i][1] * sp, pr, base, ink);
    } else {
        number(&c, v, cx, cy + fy, sc, base, ink);
    }
    return c;
}

/* ---------- the tray and the chalkboard (built once) ---------- */

static void wood(Canvas *c, int x0, int y0, int x1, int y1, uint32_t base, int vertical) {
    fill_rect(c, x0, y0, x1, y1, rgb(base));
    int n = vertical ? x1 - x0 + 1 : y1 - y0 + 1, len = vertical ? y1 - y0 + 1 : x1 - x0 + 1;
    for (int k = 0; k < n; k++)
        for (int p = irand(12); p < len; p += 6 + irand(24)) {
            int run = 4 + irand(16);
            uint32_t col = shade(base, irand(3) ? 0.86f : 1.1f);
            for (int q = p; q < p + run && q < len; q++)
                if (vertical) put(c, x0 + k, y0 + q, rgb(col));
                else put(c, x0 + q, y0 + k, rgb(col));
            p += run;
        }
}

static void build_tray(Dice *d, Canvas *c) {
    int W = d->W, H = d->H, F = d->F;
    /* felt: speckled green, brighter in the middle */
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            float dx = (x - W * 0.5f) / (W * 0.62f), dy = (y - H * 0.52f) / (H * 0.7f);
            float f = 1.12f - 0.38f * (dx * dx + dy * dy);
            int n = irand(16);
            if (n == 0) f *= 0.9f;
            else if (n == 1) f *= 1.08f;
            uint32_t col = shade(0x2A7A4A, f);
            if ((x + y) % 2 == 0 && irand(5) == 0) col = shade(col, 0.95f);
            put(c, x, y, rgb(col));
        }
    /* stitching just inside the rim */
    int s0 = F + 3;
    for (int x = s0; x <= W - 1 - s0; x++)
        if ((x - s0) % 4 < 2) { put(c, x, s0, rgb(0x5EAE76)); put(c, x, H - 1 - s0, rgb(0x4E9A66)); }
    for (int y = s0; y <= H - 1 - s0; y++)
        if ((y - s0) % 4 < 2) { put(c, s0, y, rgb(0x5EAE76)); put(c, W - 1 - s0, y, rgb(0x4E9A66)); }
    /* the rim casts a soft shadow onto the felt */
    static const float sh[4] = { 0.55f, 0.7f, 0.83f, 0.93f };
    for (int i = 0; i < 4; i++) {
        for (int x = F; x < W - F; x++) mul_px(c, x, F + i, sh[i]);
        for (int y = F + i; y < H - F; y++) mul_px(c, F + i, y, sh[i]);
    }
    for (int x = F; x < W - F; x++) mul_px(c, x, H - F - 1, 0.82f);
    for (int y = F; y < H - F; y++) mul_px(c, W - F - 1, y, 0.82f);
    /* wooden rim */
    wood(c, 0, 0, W - 1, F - 1, 0x9A6236, 0);
    wood(c, 0, H - F, W - 1, H - 1, 0x84522C, 0);
    wood(c, 0, F, F - 1, H - F - 1, 0x905A31, 1);
    wood(c, W - F, F, W - 1, H - F - 1, 0x7E4E2A, 1);
    for (int i = 0; i < F; i++) {                      /* mitred corners */
        put(c, i, i, rgb(0x5E3A1E)); put(c, W - 1 - i, i, rgb(0x5E3A1E));
        put(c, i, H - 1 - i, rgb(0x5E3A1E)); put(c, W - 1 - i, H - 1 - i, rgb(0x5E3A1E));
    }
    hline(c, 0, W - 1, 0, 0x3A2212);
    hline(c, 1, W - 2, 1, 0xD6A068);
    hline(c, F - 1, W - F, F - 1, 0x4A2C16);
    hline(c, F, W - F - 1, H - F, 0xC08A54);
    hline(c, 0, W - 1, H - 1, 0x2E1A0C);
    fill_rect(c, 0, 0, 0, H - 1, rgb(0x3A2212));
    fill_rect(c, 1, 1, 1, H - 2, rgb(0xC9925A));
    fill_rect(c, W - 1, 0, W - 1, H - 1, rgb(0x2E1A0C));
    fill_rect(c, F - 1, F, F - 1, H - F, rgb(0x4A2C16));
    fill_rect(c, W - F, F, W - F, H - F, rgb(0xB07A46));
    /* brass corner caps */
    int cs = F + 2;
    int cx[4] = { 0, W - cs, 0, W - cs }, cy[4] = { 0, 0, H - cs, H - cs };
    for (int i = 0; i < 4; i++) {
        fill_rect(c, cx[i], cy[i], cx[i] + cs - 1, cy[i] + cs - 1, rgb(0xC9A040));
        hline(c, cx[i], cx[i] + cs - 1, cy[i], 0xF2D68A);
        fill_rect(c, cx[i], cy[i], cx[i], cy[i] + cs - 1, rgb(0xE8C870));
        hline(c, cx[i], cx[i] + cs - 1, cy[i] + cs - 1, 0x7A5A1C);
        fill_rect(c, cx[i] + cs - 1, cy[i], cx[i] + cs - 1, cy[i] + cs - 1, rgb(0x8A6820));
        int rx = cx[i] + cs / 2, ry = cy[i] + cs / 2;
        put(c, rx, ry, rgb(0x6E5016)); put(c, rx - 1, ry - 1, rgb(0xFFF0B8));
    }

    /* the chalkboard, hanging over the top rim */
    int bx = d->bx, by = d->by, bw = d->bw, bh = d->bh;
    for (int y = by + 2; y < by + bh + 3; y++)
        for (int x = bx + 3; x < bx + bw + 3; x++) mul_px(c, x, y, 0.7f);
    wood(c, bx, by, bx + bw - 1, by + bh - 1, 0xB27A44, 0);
    hline(c, bx, bx + bw - 1, by, 0xE8B57A);
    fill_rect(c, bx, by, bx, by + bh - 1, rgb(0xD9A46A));
    hline(c, bx, bx + bw - 1, by + bh - 1, 0x5E3A1E);
    fill_rect(c, bx + bw - 1, by, bx + bw - 1, by + bh - 1, rgb(0x6E4422));
    int ix0 = bx + 3, iy0 = by + 3, ix1 = bx + bw - 4, iy1 = by + bh - 4;
    for (int y = iy0; y <= iy1; y++)
        for (int x = ix0; x <= ix1; x++) {
            uint32_t col = 0x2C3A34;
            float sm = sinf(x * 0.21f + y * 0.13f) + sinf(x * 0.07f - y * 0.3f);
            if (sm > 1.3f) col = 0x34443D;                 /* old chalk smudges */
            if (irand(9) == 0) col = shade(col, 1.08f);
            put(c, x, y, rgb(col));
        }
    hline(c, ix0, ix1, iy0, 0x1C2622);
    fill_rect(c, ix0, iy0, ix0, iy1, rgb(0x1C2622));
    if (!d->small) {                                   /* a stub of chalk */
        fill_rect(c, ix1 - 8, iy1 - 1, ix1 - 3, iy1, rgb(0xF2F0E6));
        hline(c, ix1 - 8, ix1 - 3, iy1, 0xC8C6BC);
    }
    for (int i = 0; i < 2; i++) {                      /* little nails */
        int nx = i ? bx + bw - 3 : bx + 2, ny = by + 1;
        put(c, nx, ny, rgb(0xE0E4EA)); put(c, nx, ny + 1, rgb(0x707882));
    }
}

/* ---------- rolling ---------- */

static void new_roll(Dice *d) {
    d->set = d->t < 0.5f ? 0 : 1 + irand(NSETS - 1);
    const int *set = SETS[d->set];
    int n = set[0], R = d->R;
    d->n = n;
    d->fromLeft = irand(2);
    d->total = d->shown = d->hasCrit = 0;
    d->expr[0] = 0;
    if (n == 1) snprintf(d->expr, sizeof d->expr, "%s", NAMES[set[1]]);
    memset(d->fly, 0, sizeof d->fly);

    float x0 = d->F + 8, x1 = d->W - d->F - 8, y0 = d->by + d->bh + 4, y1 = d->H - d->F - 12;
    float px = 2.0f * R + 18, py = 2.0f * R + 20;
    int cols = n;
    while (cols > 1 && cols * px > x1 - x0) cols--;
    int rows = (n + cols - 1) / cols;
    cols = (n + rows - 1) / rows;                       /* even rows: 3 + 3, not 4 + 2 */
    d->rows = rows;
    if (rows > 1 || d->small) y1 = d->H - d->F - (d->small ? 3 : 6);
    if (rows * py > y1 - y0) py = (y1 - y0) / rows;
    if (cols * px > x1 - x0) px = (x1 - x0) / cols;
    float dir = d->fromLeft ? 1.0f : -1.0f;
    for (int i = 0; i < n; i++) {
        Die *e = &d->die[i];
        memset(e, 0, sizeof *e);
        int row = i / cols, col = i % cols, m = row == rows - 1 ? n - row * cols : cols;
        e->kind = set[i + 1];
        int s = SIDES[e->kind], r = irand(100);
        e->value = 1 + irand(s);
        if (e->kind == D20) e->value = r < 11 ? 20 : (r < 19 ? 1 : 2 + irand(18));  /* a little extra drama */
        e->tx = (x0 + x1) * 0.5f + (col - (m - 1) * 0.5f) * px + frand(-3, 3);
        e->ty = (y0 + y1) * 0.5f + (row - (rows - 1) * 0.5f) * py + frand(-3, 3);
        e->sx = d->fromLeft ? -R * 1.2f - frand(0, 16) : d->W + R * 1.2f + frand(0, 16);
        e->sy = e->ty + frand(-14, 14);
        int order = d->fromLeft ? n - 1 - i : i;             /* the far ones go first */
        e->t0 = order * 0.14f + frand(0, 0.08f);
        e->dur = frand(1.6f, 2.0f);
        e->z0 = d->H * frand(0.26f, 0.38f);
        e->spin = -dir * frand(2.5f, 4.5f) * PI_F;
        e->rollN = 4 + irand(4);
        e->axis = irand(2);
        e->x = e->sx; e->y = e->sy; e->z = e->z0;
        e->lsx = e->lsy = e->gsx = e->gsy = 1;
        e->shown = 1 + irand(s);
        e->u = -1;
    }
    d->state = ROLL;
    d->stateT = 0;
}

static Part *add_part(Dice *d, float x, float y, float vx, float vy, int kind, uint32_t col, float life) {
    for (int i = 0; i < PARTS; i++) {
        Part *q = &d->parts[i];
        if (q->life > 0) continue;
        *q = (Part){ x, y, vx, vy, life, life, frand(0, 6.28f), kind, col };
        return q;
    }
    return NULL;
}

static void add_pop(Dice *d, const char *txt, float x, float y, int sc, uint32_t col, float life) {
    for (int i = 0; i < POPS; i++) {
        Pop *p = &d->pop[i];
        if (p->t < p->max) continue;
        *p = (Pop){ { 0 }, x, y, 0, life, sc, col };
        snprintf(p->txt, sizeof p->txt, "%s", txt);
        return;
    }
}

static void burst(Dice *d, float x, float y, int n, float speed) {
    for (int i = 0; i < n; i++) {
        float a = frand(0, 2 * PI_F), v = d->H * speed * frand(0.4f, 1.0f);
        add_part(d, x, y, cosf(a) * v, sinf(a) * v - d->H * 0.15f, P_SPARK, irand(3) ? 0xFFE27A : 0xFFFFFF, frand(0.6f, 1.1f));
    }
}

static void settle(Dice *d, Die *e) {
    int s = SIDES[e->kind];
    float x = e->x, y = e->y - d->R;
    e->settled = 1;
    e->st = 0;
    if (e->value == s && e->kind == D20) {
        e->react = CRIT;
        d->hasCrit = 1;
        burst(d, x, y, 34, 0.55f);
        for (int i = 0; i < 36; i++)
            add_part(d, x + frand(-1, 1) * d->R, y, frand(-1, 1) * d->H * 0.3f, -d->H * frand(0.5f, 0.9f),
                     P_CONFETTI, CONFETTI[irand(6)], frand(1.6f, 2.4f));
        add_pop(d, "CRIT!", x, y - d->R * 0.6f, 2, 0xFFD84A, 2.6f);
    } else if (e->value == s) {
        e->react = MAXED;
        burst(d, x, y, 16, 0.4f);
        add_pop(d, "MAX!", x, y - d->R * 0.5f, 1, 0xFFE27A, 1.6f);
    } else if (e->value == 1 && e->kind == D20) {
        e->react = FAIL;
        add_pop(d, "WOMP", x, y - d->R * 1.35f, 1, WOMP_COL, 2.2f);   /* sits on top of the cloud */
    } else if (e->value == 1) {
        e->react = ONE;
    } else {
        e->react = CALM;
    }
}

/* Where the next tally lands on the chalkboard. */
static void board_target(const Dice *d, float *x, float *y) {
    *x = d->bx + d->bw * 0.5f;
    *y = d->by + d->bh * 0.62f;
}

static void update_die(Dice *d, Die *e, int idx, float T, float dt) {
    int R = d->R;
    float u = clampf((T - e->t0) / e->dur, 0, 1);
    if (T < e->t0) return;
    float prev = e->u;
    e->u = u;
    float g = 1 - powf(1 - u, 2.2f);
    e->x = lerp(e->sx, e->tx, g);
    e->y = lerp(e->sy, e->ty, g);
    if (u < BOUNCE[0]) {
        float s = u / BOUNCE[0];
        e->z = e->z0 * (1 - s * s);
    } else {
        float hh = e->z0 * 0.3f;
        e->z = 0;
        for (int k = 0; k < NB - 1; k++, hh *= 0.4f)
            if (u < BOUNCE[k + 1]) {
                float s = (u - BOUNCE[k]) / (BOUNCE[k + 1] - BOUNCE[k]);
                e->z = 4 * hh * s * (1 - s);
                break;
            }
    }
    for (int k = 0; k < NB; k++)
        if (prev < BOUNCE[k] && u >= BOUNCE[k]) {
            e->impact = 1 - k * 0.2f;
            e->impactT = T;
            for (int i = 0; i < 5 - k; i++)            /* a puff of felt fluff */
                add_part(d, e->x + frand(-1, 1) * R * 0.8f, e->y + R * 0.7f, frand(-1, 1) * d->H * 0.12f,
                         -frand(0.02f, 0.12f) * d->H, P_DUST, 0x8FCB9E, frand(0.3f, 0.6f));
        }
    e->ang = e->spin * powf(1 - u, 3);
    float roll = e->rollN * PI_F * (1 - powf(1 - u, 2.5f));
    int k = (int)floorf(roll / PI_F + 0.5f), s = SIDES[e->kind];
    if (k >= e->rollN) e->shown = e->value;
    else e->shown = 1 + (int)((unsigned)(idx * 7919 + k * 104729 + d->set * 31 + e->value * 13) % (unsigned)s);
    float sq = 0.55f + 0.45f * fabsf(cosf(roll));
    e->lsx = e->axis ? 1 : sq;
    e->lsy = e->axis ? sq : 1;
    float imp = e->impact * expf(-(T - e->impactT) * 12);
    e->gsx = 1 + 0.16f * imp;
    e->gsy = 1 - 0.2f * imp;
    e->tint = 0;

    if (u >= 1 && !e->settled) settle(d, e);
    if (!e->settled) return;
    e->st += dt;
    float st = e->st;
    switch (e->react) {
    case CRIT:
    case MAXED: {
        int hops = e->react == CRIT ? 2 : 1;
        for (int h = 0; h < hops; h++) {
            float s = (st - 0.05f - h * 0.42f) / 0.38f;
            if (s > 0 && s < 1) {
                e->z = R * (h ? 0.4f : 0.65f) * 4 * s * (1 - s);
                e->ang = 0.2f * sinf(s * 2 * PI_F);
                e->gsy = 1 + 0.08f * sinf(s * PI_F);
                e->gsx = 1 - 0.05f * sinf(s * PI_F);
            }
        }
        e->tintCol = 0xFFFFFF;                         /* a bright flash as it lands */
        e->tint = 0.7f * expf(-st * 5);
        if (e->react == CRIT) e->tint += 0.12f * (0.5f + 0.5f * sinf(st * 5));
        break;
    }
    case ONE:
    case FAIL: {
        float dr = ease(st / 0.6f) * (1 - ease((st - 2.6f) / 0.6f));
        e->gsy = 1 - 0.1f * dr;
        e->gsx = 1 + 0.05f * dr;
        e->tintCol = 0x8A929E;
        e->tint = 0.5f * dr;
        if (e->react == FAIL) e->x += sinf(st * 46) * R * 0.14f * clampf(1 - st / 0.8f, 0, 1);
        if (st > 0.5f && st < 2.6f && irand(100) < (e->react == FAIL ? 45 : 28))   /* rain from the cloud */
            add_part(d, e->x + frand(-0.6f, 0.6f) * R, e->y - R * 1.9f, 0, d->H * 0.5f, P_RAIN, 0x9DB8D8, (R * 0.85f) / (d->H * 0.5f));
        break;
    }
    default:
        e->ang = 0.07f * expf(-st * 6) * sinf(st * 22);
        break;
    }
    float wait = e->react == CRIT ? 1.0f : (e->react == MAXED ? 0.7f : 0.45f);
    if (!e->tallied && st > wait) {
        e->tallied = 1;
        Fly *f = &d->fly[idx];
        *f = (Fly){ e->x, e->y - R, 0, e->value, 1 };
    }
}

static void step(void *p, float dt) {
    Dice *d = p;
    if (dt > 0.1f) dt = 0.1f;
    d->t += dt;
    d->stateT += dt;
    switch (d->state) {
    case ROLL: {
        int done = 1;
        for (int i = 0; i < d->n; i++) {
            update_die(d, &d->die[i], i, d->stateT, dt);
            if (!d->die[i].tallied || d->fly[i].on) done = 0;
        }
        if (done && d->shown == d->total) {
            d->state = SUM;
            d->stateT = 0;
            d->hold = d->hasCrit ? 3.4f : 2.6f;
            float x, y;
            board_target(d, &x, &y);
            for (int i = 0; i < 8; i++)
                add_part(d, x + frand(-1, 1) * d->bw * 0.3f, y, frand(-1, 1) * d->H * 0.1f, -d->H * frand(0.05f, 0.2f),
                         P_SPARK, 0xFFFFFF, frand(0.4f, 0.8f));
        }
        break;
    }
    case SUM:
        for (int i = 0; i < d->n; i++) update_die(d, &d->die[i], i, 1e6f, dt);
        if (d->stateT > d->hold) { d->state = EXIT; d->stateT = 0; }
        break;
    case EXIT: {
        float dir = d->fromLeft ? 1.0f : -1.0f;
        for (int i = 0; i < d->n; i++) {
            Die *e = &d->die[i];
            int order = d->fromLeft ? d->n - 1 - i : i;
            float k = clampf((d->stateT - order * 0.07f) / 0.6f, 0, 1);
            e->st += dt;
            e->x = e->tx + dir * k * k * (d->W + d->R * 4.0f);
            e->y = e->ty;
            e->z = d->R * 0.5f * fabsf(sinf(k * PI_F * 2)) * (1 - k);
            e->ang = dir * 2.2f * k * k;
            e->gsx = e->gsy = e->lsx = e->lsy = 1;
            e->tint *= 1 - dt * 4;
        }
        if (d->stateT > 0.6f + d->n * 0.07f) { d->state = GAP; d->stateT = 0; }
        break;
    }
    default:
        if (d->stateT > 0.3f) new_roll(d);
        break;
    }

    /* tallies flying up to the board */
    for (int i = 0; i < d->n; i++) {
        Fly *f = &d->fly[i];
        if (!f->on) continue;
        f->t += dt / 0.65f;
        if (f->t >= 1) {
            f->on = 0;
            d->total += f->value;
            d->die[i].counted = 1;
            if (d->n > 1) {                             /* the sum so far, left to right like the dice */
                d->expr[0] = 0;
                for (int j = 0; j < d->n; j++) {
                    if (!d->die[j].counted) continue;
                    char add[8];
                    snprintf(add, sizeof add, "%s%d", d->expr[0] ? "+" : "", d->die[j].value);
                    strncat(d->expr, add, sizeof d->expr - strlen(d->expr) - 1);
                }
            }
            float x, y;
            board_target(d, &x, &y);
            for (int k = 0; k < 6; k++)
                add_part(d, x + frand(-8, 8), y + frand(-4, 4), frand(-1, 1) * d->H * 0.08f, -frand(0, 0.08f) * d->H,
                         P_CHALK, 0xE8E6DC, frand(0.3f, 0.6f));
        }
    }
    d->tick += dt;
    while (d->shown < d->total && d->tick > 0.045f) { d->shown++; d->tick -= 0.045f; }
    if (d->shown >= d->total) d->tick = 0;

    for (int i = 0; i < POPS; i++) d->pop[i].t += dt;
    for (int i = 0; i < PARTS; i++) {
        Part *q = &d->parts[i];
        if (q->life <= 0) continue;
        q->life -= dt;
        q->x += q->vx * dt;
        q->y += q->vy * dt;
        switch (q->kind) {
        case P_SPARK: q->vx *= 1 - 2.5f * dt; q->vy = q->vy * (1 - 2.5f * dt) + d->H * 0.15f * dt; break;
        case P_CONFETTI:
            q->vx *= 1 - 1.8f * dt;
            q->vy = q->vy * (1 - 1.8f * dt) + d->H * 0.9f * dt;
            q->x += sinf(d->t * 7 + q->ph) * d->H * 0.08f * dt;
            break;
        case P_DUST: case P_CHALK: q->vx *= 1 - 4 * dt; q->vy *= 1 - 4 * dt; break;
        default: break;
        }
    }
}

/* ---------- drawing ---------- */

/* Copies a die sprite onto the art canvas, rotated and squashed, nearest-neighbour. */
static void blit_die(Dice *d, const Die *e, float cx, float cy) {
    int gold = e->settled && (e->react == CRIT || e->react == MAXED) && d->state != EXIT;
    const Canvas *sp = &d->sprite[e->kind][gold ? 0 : e->shown];
    Canvas *c = &d->top;
    float ca = cosf(e->ang), sa = sinf(e->ang), h = sp->w * 0.5f;
    float rr = h * 1.45f * fmaxf(e->gsx, e->gsy);
    int x0 = (int)floorf(cx - rr), x1 = (int)ceilf(cx + rr), y0 = (int)floorf(cy - rr), y1 = (int)ceilf(cy + rr);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > c->w - 1) x1 = c->w - 1;
    if (y1 > c->h - 1) y1 = c->h - 1;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            float qx = (x + 0.5f - cx) / e->gsx, qy = (y + 0.5f - cy) / e->gsy;
            float u = (ca * qx + sa * qy) / e->lsx + h, v = (-sa * qx + ca * qy) / e->lsy + h;
            if (u < 0 || v < 0 || u >= sp->w || v >= sp->h) continue;
            uint32_t px = sp->px[(int)v * sp->w + (int)u];
            if (!px) continue;
            if (e->tint > 0.01f) px = rgb(mixc(px >> 8, e->tintCol, e->tint));
            c->px[y * c->w + x] = px;
        }
}

/* A dithered pixel-art halo: solid in the middle, checkered further out. */
static void glow(Canvas *c, float cx, float cy, float rx, float ry, float a) {
    for (int y = (int)(cy - ry) - 1; y <= (int)(cy + ry) + 1; y++)
        for (int x = (int)(cx - rx) - 1; x <= (int)(cx + rx) + 1; x++) {
            float dx = (x + 0.5f - cx) / rx, dy = (y + 0.5f - cy) / ry, q = dx * dx + dy * dy;
            if (q > 1) continue;
            if (q > 0.62f && (x + y) % 2) continue;
            if (q > 0.85f && (x % 2 || y % 2)) continue;
            put(c, x, y, rgba(q < 0.4f ? 0xFFF0A0 : 0xFFD95A, (int)((q < 0.4f ? 245 : 230) * a)));
        }
}

static void cloud(Canvas *c, float x, float y, float s) {
    if (s <= 0.05f) return;
    fill_ellipse(c, x - 6 * s, y + 1 * s, 5 * s, 3.5f * s, rgb(0x8A929E));
    fill_ellipse(c, x + 6 * s, y + 1 * s, 5 * s, 3.5f * s, rgb(0x8A929E));
    fill_ellipse(c, x, y - 1 * s, 6.5f * s, 5 * s, rgb(0x9EA6B2));
    fill_ellipse(c, x - 5 * s, y, 4.5f * s, 3.5f * s, rgb(0xA8B0BC));
    fill_ellipse(c, x + 5 * s, y, 4.5f * s, 3.5f * s, rgb(0xA8B0BC));
    fill_ellipse(c, x - 1 * s, y - 3 * s, 4 * s, 2.5f * s, rgb(0xC4CAD4));
    if (s > 0.7f) {                                    /* a glum little face */
        put(c, (int)(x - 2 * s), (int)(y), rgb(0x4A505A));
        put(c, (int)(x + 2 * s), (int)(y), rgb(0x4A505A));
        put(c, (int)x - 1, (int)(y + 2 * s), rgb(0x4A505A));
        put(c, (int)x, (int)(y + 2 * s) - 1, rgb(0x4A505A));
        put(c, (int)x + 1, (int)(y + 2 * s), rgb(0x4A505A));
    }
}

/* Draws the dark outline around everything opaque on the top layer. */
static void outline_top(Dice *d) {
    Canvas *c = &d->top;
    int W = c->w, H = c->h;
    uint32_t *px = c->px;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            int i = y * W + x;
            d->mark[i] = !px[i] && ((x > 0 && px[i - 1]) || (x < W - 1 && px[i + 1]) ||
                                    (y > 0 && px[i - W]) || (y < H - 1 && px[i + W]));
        }
    for (int i = 0; i < W * H; i++)
        if (d->mark[i]) px[i] = rgb(0x1A1410);
}

static void frect(SDL_Renderer *r, float x, float y, float w, float h, SDL_Color c) {
    SDL_FRect rc = { x, y, w, h };
    set_color(r, c);
    SDL_RenderFillRectF(r, &rc);
}

static SDL_Color col_a(uint32_t hex, float a) {
    SDL_Color c = hexcol(hex);
    c.a = (Uint8)(255 * clampf(a, 0, 1));
    return c;
}

static void draw(void *p, SDL_Renderer *r) {
    Dice *d = p;
    int S = d->S, R = d->R;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    set_color(r, hexcol(0x1E5A36));
    SDL_RenderFillRect(r, NULL);
    SDL_Rect full = { 0, 0, d->W * S, d->H * S };
    SDL_RenderCopy(r, d->felt, NULL, &full);

    Canvas *un = &d->under, *top = &d->top;
    memset(un->px, 0, sizeof(uint32_t) * un->w * un->h);
    memset(top->px, 0, sizeof(uint32_t) * top->w * top->h);
    int active = d->state != GAP;
    float fade = d->state == EXIT ? clampf(1 - d->stateT / 0.5f, 0, 1) : (d->state == GAP ? 0 : 1);

    /* chalk on the board */
    {
        uint32_t chalk = 0xF2F0E6;
        int a = (int)(225 * fade);
        if (d->expr[0] && a > 0 && !d->small) {
            glyphs(un, d->expr, d->bx + (d->bw - glyphs_w(d->expr, 1)) / 2, d->by + 5, 1, rgba(chalk, a * 4 / 5));
        }
        if ((d->total > 0 || d->state == SUM) && a > 0) {
            char s[8];
            snprintf(s, sizeof s, "%d", d->shown);
            int sc = d->small ? 1 : 2, w = glyphs_w(s, sc), x = d->bx + (d->bw - w) / 2, y = d->by + (d->small ? 4 : 15);
            uint32_t col = d->state == SUM ? mixc(chalk, 0xFFE27A, 0.5f + 0.5f * sinf(d->stateT * 6)) : chalk;
            if (d->state == SUM || d->state == EXIT) {      /* underline it twice, chalk-style */
                int k = (int)((w + 8) * clampf(d->stateT * 3, 0, 1));
                if (d->state == EXIT) k = w + 8;
                if (!d->small) fill_rect(un, x - 4, y + 16, x - 4 + k, y + 16, rgba(chalk, a * 3 / 4));
                if (d->n > 1) glyphs(un, "=", x - (d->small ? 8 : 12), y + (d->small ? 0 : 3), 1, rgba(chalk, a));
            }
            glyphs(un, s, x, y, sc, rgba(col, a));
        }
    }

    if (active) {
        int order[MAXDICE];
        for (int i = 0; i < d->n; i++) order[i] = i;
        for (int i = 1; i < d->n; i++)                 /* back to front */
            for (int j = i; j > 0 && d->die[order[j]].y < d->die[order[j - 1]].y; j--) {
                int t = order[j]; order[j] = order[j - 1]; order[j - 1] = t;
            }
        for (int oi = 0; oi < d->n; oi++) {
            Die *e = &d->die[order[oi]];
            if (e->u < 0) continue;
            /* gold glow (and rays for a natural 20) */
            if (e->settled && (e->react == CRIT || e->react == MAXED) && d->state != EXIT) {
                float st = e->st, a = clampf(st * 3, 0, 1) * (e->react == MAXED ? clampf(1 - (st - 1.4f) / 0.6f, 0, 1) : 1);
                if (a > 0.02f) {
                    float gx = e->x, gy = e->y;
                    if (e->react == CRIT) {
                        for (int k = 0; k < 10; k++) {
                            float an = d->t * 0.8f + k * PI_F / 5, an2 = an + 0.13f, L = R * (2.4f + 0.3f * sinf(d->t * 3 + k));
                            float xs[3] = { gx, gx + cosf(an) * L, gx + cosf(an2) * L };
                            float ys[3] = { gy, gy + sinf(an) * L * 0.8f, gy + sinf(an2) * L * 0.8f };
                            fill_poly(un, xs, ys, 3, rgba(k % 2 ? 0xFFD84A : 0xFFF0A0, (int)(215 * a)));
                        }
                    }
                    float gr = R * (1.45f + 0.08f * sinf(d->t * 5));
                    glow(un, gx, gy, gr * 1.2f, gr, a);
                }
            }
            /* the shadow stays on the felt */
            float zf = clampf(1 - e->z / (d->H * 0.5f), 0.3f, 1);
            fill_ellipse(un, e->x + 2 + e->z * 0.15f, e->y + R * 0.42f + e->z * 0.1f, R * 0.98f * zf, R * 0.7f * zf,
                         rgba(0x06180C, (int)(110 * zf)));
            float cy = e->y - e->z;
            if (e->react >= ONE && e->settled) cy += R * (1 - e->gsy);   /* droop down, not up */
            blit_die(d, e, e->x, cy);
            /* the die's name, chalked faintly on the felt */
            if (e->settled && d->state != EXIT && !d->small && d->rows == 1) {
                const char *nm = NAMES[e->kind];
                int a = (int)(140 * clampf(e->st * 3, 0, 1));
                glyphs(un, nm, (int)(e->tx - glyphs_w(nm, 1) / 2.0f + 0.5f), (int)(e->ty + R * 1.25f + 2), 1, rgba(0xBFE6C8, a));
            }
            /* a grumpy little cloud for 1s */
            if (e->settled && (e->react == ONE || e->react == FAIL) && d->state != EXIT) {
                float st = e->st, s = ease((st - 0.2f) / 0.35f) * (1 - ease((st - 2.7f) / 0.4f));
                s *= e->react == FAIL ? 1.25f : 1.0f;
                s *= R / 14.0f;
                cloud(top, e->x, e->y - R * 2.1f + sinf(d->t * 2.5f) * 1.2f, s);
            }
        }
    }

    /* tallies on their way to the board */
    for (int i = 0; i < d->n; i++) {
        Fly *f = &d->fly[i];
        if (!f->on) continue;
        float tx, ty, k = ease(f->t);
        board_target(d, &tx, &ty);
        float x = lerp(f->x0, tx, k), y = lerp(f->y0, ty, k) - sinf(PI_F * f->t) * d->H * 0.16f;
        char s[8];
        snprintf(s, sizeof s, "+%d", f->value);
        int sc = f->t < 0.75f ? 2 : 1;
        glyphs(top, s, (int)(x - glyphs_w(s, sc) / 2.0f), (int)(y - FONT_H * sc / 2.0f), sc, rgb(0xFFFFFF));
    }
    /* CRIT!, MAX! and WOMP */
    for (int i = 0; i < POPS; i++) {
        Pop *q = &d->pop[i];
        if (q->t >= q->max) continue;
        float k = q->t / q->max;
        if (d->state == EXIT || d->state == GAP) continue;
        float y = q->y - d->R * 0.9f - (q->col == WOMP_COL ? -k * 3 : k * 8);
        float in = clampf(q->t / 0.15f, 0, 1);
        if (k > 0.85f && (int)(q->t * 16) % 2) continue;  /* blink out */
        int sc = q->sc, w = glyphs_w(q->txt, sc);
        float bob = (1 - in) * 6 + (q->sc == 2 ? sinf(q->t * 9) * 1.2f : 0);
        if (sc == 2) glyphs(top, q->txt, (int)(q->x - w / 2.0f), (int)(y + bob) + 1, sc, rgb(0xC08A20));
        glyphs(top, q->txt, (int)(q->x - w / 2.0f), (int)(y + bob), sc, rgb(q->col));
    }
    outline_top(d);

    SDL_UpdateTexture(d->underTex, NULL, un->px, un->w * 4);
    SDL_UpdateTexture(d->topTex, NULL, top->px, top->w * 4);
    SDL_RenderCopy(r, d->underTex, NULL, &full);
    SDL_RenderCopy(r, d->topTex, NULL, &full);

    for (int i = 0; i < PARTS; i++) {
        Part *q = &d->parts[i];
        if (q->life <= 0) continue;
        float f = q->life / q->max, x = floorf(q->x) * S, y = floorf(q->y) * S;
        switch (q->kind) {
        case P_SPARK: {
            SDL_Color c = col_a(q->col, f < 0.3f ? f / 0.3f : 1);
            frect(r, x, y, (float)S, (float)S, c);
            if (f > 0.45f) {
                frect(r, x - S, y, (float)S, (float)S, c); frect(r, x + S, y, (float)S, (float)S, c);
                frect(r, x, y - S, (float)S, (float)S, c); frect(r, x, y + S, (float)S, (float)S, c);
            }
            break;
        }
        case P_CONFETTI: {
            int flip = (int)(d->t * 10 + q->ph * 3) % 3;
            frect(r, x, y, (float)S * (flip ? 2 : 1), (float)S * (flip == 1 ? 1 : 2), col_a(q->col, f < 0.25f ? f / 0.25f : 1));
            break;
        }
        case P_RAIN: frect(r, x, y, (float)S, 2.0f * S, col_a(q->col, 0.85f)); break;
        default: frect(r, x, y, (float)S, (float)S, col_a(q->col, f * 0.8f)); break;
        }
    }
}

/* ---------- life cycle ---------- */

static void *create(SDL_Renderer *r, int w, int h) {
    Dice *d = calloc(1, sizeof *d);
    d->w = w;
    d->h = h;
    d->S = (int)(h / 180.0f + 0.5f);
    if (d->S < 1) d->S = 1;
    d->W = (w + d->S - 1) / d->S;
    d->H = (h + d->S - 1) / d->S;
    d->small = d->H < 130;
    d->R = (int)(d->H * (d->small ? 0.15f : 0.115f) + 0.5f);
    if (d->R < 8) d->R = 8;
    d->F = d->H * 55 / 1000 > 5 ? d->H * 55 / 1000 : 5;
    d->SP = 2 * (int)ceilf(d->R * 1.6f) + 4;
    d->bw = d->W - 2 * d->F - 16 < 116 ? d->W - 2 * d->F - 16 : 116;
    d->bh = 38;
    if (d->small) { d->bw = 40; d->bh = 15; }
    d->bx = (d->W - d->bw) / 2;
    d->by = d->F / 2 - 1 > 1 ? d->F / 2 - 1 : 1;

    for (int k = 0; k < KINDS; k++) {               /* [0] is the top roll with a gold number */
        d->sprite[k][0] = make_die(k, SIDES[k], (float)d->R, d->SP, k == D8 ? 0xFFFFFF : 0xFFE04A);  /* gold on yellow won't show */
        for (int v = 1; v <= SIDES[k]; v++) d->sprite[k][v] = make_die(k, v, (float)d->R, d->SP, 0xFFFFFF);
    }

    Canvas tray = canvas_new(d->W, d->H);
    build_tray(d, &tray);
    d->felt = canvas_texture(r, &tray);
    d->under = canvas_new(d->W, d->H);
    d->top = canvas_new(d->W, d->H);
    d->mark = calloc((size_t)d->W * d->H, 1);
    d->underTex = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, d->W, d->H);
    d->topTex = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, d->W, d->H);
    SDL_Texture *ts[2] = { d->underTex, d->topTex };
    for (int i = 0; i < 2; i++) {
        SDL_SetTextureBlendMode(ts[i], SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(ts[i], SDL_ScaleModeNearest);
    }
    new_roll(d);
    return d;
}

static void destroy(void *p) {
    Dice *d = p;
    for (int k = 0; k < KINDS; k++)
        for (int v = 0; v <= SIDES[k]; v++) free(d->sprite[k][v].px);
    SDL_DestroyTexture(d->felt);
    SDL_DestroyTexture(d->underTex);
    SDL_DestroyTexture(d->topTex);
    free(d->under.px);
    free(d->top.px);
    free(d->mark);
    free(d);
}

const Saver saver_dice = { "dice", "DICE", create, step, draw, destroy };
