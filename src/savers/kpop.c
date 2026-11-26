/* K-pop (KPop Demon Hunters): 360p detailed pixel-art concert arena featuring HUNTR/X
   (Rumi with her long purple dragon-braid and Saingeom spirit sword, Mira with flowing
   coral-pink hair and Gok-do polearm, Zoey with twin space buns and Shin-kal daggers),
   Derpy the blue striped tiger with Sussie the gat-hat magpie, HUNTR/X LED stage screen,
   Honmoon barrier waves, dithered spotlights, demon wisps, and layered fan lightsticks. */
#include "../engine.h"
#include <math.h>
#include <stdlib.h>

#define ROWS 360
#define HUNTER_W 96
#define HUNTER_H 120
#define TIGER_W 52
#define TIGER_H 44
#define MAX_SPARKS 84
#define LIGHTSTICKS 34
#define EQ_BARS 22
#define MAX_DEMONS 3

enum { RUMI, MIRA, ZOEY };

typedef struct { float x, y, vx, vy, life, max; uint32_t col; } Spark;
typedef struct { float x, y, vx, ph; int alive; } Demon;

typedef struct {
    int w, h, px, W, H;
    float scale, t, beat, nextSpark, demonT;
    SDL_Texture *small, *bg, *logo, *hunters[3][4], *tiger[2];
    int logoW, logoH;
    Spark sp[MAX_SPARKS];
    Demon demons[MAX_DEMONS];
} Kpop;

static void spawn_spark(Kpop *k, float x, float y, uint32_t colOverride) {
    static const uint32_t PAL[5] = { 0x64F0FF, 0xB078FF, 0xFF5CA8, 0xFFE055, 0xFFFFFF };
    for (int i = 0; i < MAX_SPARKS; i++) {
        if (k->sp[i].life > 0) continue;
        float a = frand(0, 2 * PI_F);
        float s = frand(0.05f, 0.18f) * k->H;
        k->sp[i] = (Spark){ x, y, cosf(a) * s, sinf(a) * s, 0, frand(0.9f, 1.8f), colOverride ? colOverride : PAL[irand(5)] };
        k->sp[i].life = k->sp[i].max;
        return;
    }
}

/* Sculpted 96x120 pixel-art HUNTR/X idol-warrior sprite with 4-frame dynamic dance/combat cycle. */
static Canvas make_hunter(int who, int frame) {
    Canvas c = canvas_new(HUNTER_W, HUNTER_H);
    static const int BOB[4] = { 0, -2, -1, -2 };
    int b = BOB[frame];
    int sway = (frame == 1 ? 1 : frame == 3 ? -1 : 0);
    int step = (frame == 1 ? 2 : frame == 3 ? -2 : 0);
    int cx = 48 + sway;

    /* Back hair silhouette first so it falls cleanly behind shoulders & arms */
    if (who == RUMI) {
        /* Thick woven lavender-purple dragon braid sweeping past knees */
        for (int i = 0; i < 25; i++) {
            float bx = cx - 12 - i * 0.42f + sinf(i * 0.55f + frame) * 1.5f;
            float by = 28 + i * 3.3f + b;
            float r = 4.6f - i * 0.11f;
            fill_ellipse(&c, bx, by, r, r * 0.85f, rgb(i % 2 ? 0x6E2EE0 : 0x9C62FF));
            if (i % 2 == 0) put(&c, (int)bx - 1, (int)by - 1, rgb(0xD4B8FF));
        }
        fill_rect(&c, cx - 23, 106 + b, cx - 19, 111 + b, rgb(0xFFD24A));
    } else if (who == MIRA) {
        /* Layered flowing waist-length coral-pink hair */
        fill_poly(&c, (float[]){ cx - 15, cx + 15, cx + 21 + sway, cx - 20 + sway },
                  (float[]){ 14 + b, 14 + b, 78 + b, 78 + b }, 4, rgb(0xC82870));
        fill_poly(&c, (float[]){ cx - 12, cx + 12, cx + 17 + sway, cx - 16 + sway },
                  (float[]){ 16 + b, 16 + b, 74 + b, 74 + b }, 4, rgb(0xFF4F9A));
        fill_rect(&c, cx - 12, 42 + b, cx - 9, 66 + b, rgb(0xFF9CD0));
        fill_rect(&c, cx + 9, 42 + b, cx + 12, 66 + b, rgb(0xFF9CD0));
    } else {
        /* Zoey's trailing twin bun ribbons behind shoulders */
        fill_poly(&c, (float[]){ cx - 14, cx - 11, cx - 19 + sway, cx - 22 + sway },
                  (float[]){ 12 + b, 12 + b, 40 + b, 38 + b }, 4, rgb(0x28D4C8));
        fill_poly(&c, (float[]){ cx + 11, cx + 14, cx + 22 + sway, cx + 19 + sway },
                  (float[]){ 12 + b, 12 + b, 40 + b, 38 + b }, 4, rgb(0x28D4C8));
    }

    /* Sculpted dynamic legs & tall platform combat boots (distinct stance per member) */
    int stance = who == ZOEY ? 3 : who == RUMI ? 2 : 1;
    float lkX0 = cx - 6 - stance - step * 0.4f, rkX0 = cx + 6 + stance + step * 0.4f;
    float lfX = cx - 8 - stance * 1.4f - step * 0.6f, rfX = cx + 8 + stance * 1.4f + step * 0.6f;
    fill_poly(&c, (float[]){ cx - 9, cx - 2, rkX0 * 0.0f + lkX0 + 3, lkX0 - 3 },
              (float[]){ 72 + b, 72 + b, 92 + b, 92 + b }, 4, rgb(0xF6D4BA));
    fill_poly(&c, (float[]){ cx + 2, cx + 9, rkX0 + 3, rkX0 - 3 },
              (float[]){ 72 + b, 72 + b, 92 + b, 92 + b }, 4, rgb(0xF6D4BA));
    /* Knee shadows */
    fill_rect(&c, (int)lkX0 - 2, 81 + b, (int)lkX0 + 2, 82 + b, rgb(0xE2B292));
    fill_rect(&c, (int)rkX0 - 2, 81 + b, (int)rkX0 + 2, 82 + b, rgb(0xE2B292));
    /* High boots with metal buckles & platform soles */
    fill_poly(&c, (float[]){ lkX0 - 4, lkX0 + 4, lfX + 4, lfX - 4 },
              (float[]){ 87 + b, 87 + b, 114 + b, 114 + b }, 4, rgb(0x1B172B));
    fill_poly(&c, (float[]){ rkX0 - 4, rkX0 + 4, rfX + 4, rfX - 4 },
              (float[]){ 87 + b, 87 + b, 114 + b, 114 + b }, 4, rgb(0x1B172B));
    uint32_t buck = who == MIRA ? 0xD0DCF0 : 0xFFD24A;
    fill_rect(&c, (int)lkX0 - 4, 88 + b, (int)lkX0 + 4, 90 + b, rgb(buck));
    fill_rect(&c, (int)rkX0 - 4, 88 + b, (int)rkX0 + 4, 90 + b, rgb(buck));
    fill_rect(&c, (int)lfX - 5, 111 + b, (int)lfX + 4, 115 + b, rgb(0x322C4A));
    fill_rect(&c, (int)rfX - 4, 111 + b, (int)rfX + 5, 115 + b, rgb(0x322C4A));

    /* Flared pleated K-pop stage skirt / shorts + chain belt */
    uint32_t jkt = who == RUMI ? 0xFFD23F : who == MIRA ? 0x2A2240 : 0x28D4C8;
    uint32_t jktD = who == RUMI ? 0xCE9210 : who == MIRA ? 0x483C68 : 0x168E86;
    uint32_t trim = who == RUMI ? 0x8F4DFF : who == MIRA ? 0xFF4F9A : 0xFFD24A;
    fill_poly(&c, (float[]){ cx - 10, cx + 10, cx + 15, cx - 15 },
              (float[]){ 63 + b, 63 + b, 76 + b, 76 + b }, 4, rgb(0x1C182E));
    for (int px = cx - 9; px <= cx + 9; px += 6)
        fill_rect(&c, px, 66 + b, px + 1, 75 + b, rgb(0x302848));
    fill_rect(&c, cx - 14, 74 + b, cx + 14, 75 + b, rgb(trim));
    fill_rect(&c, cx - 10, 62 + b, cx + 10, 64 + b, rgb(buck));

    /* Tapered torso & tailored cropped idol jacket over graphic stage top */
    fill_poly(&c, (float[]){ cx - 11, cx + 11, cx + 8, cx - 8 },
              (float[]){ 40 + b, 40 + b, 62 + b, 62 + b }, 4, rgb(jkt));
    fill_poly(&c, (float[]){ cx - 5, cx + 5, cx + 4, cx - 4 },
              (float[]){ 42 + b, 42 + b, 62 + b, 62 + b }, 4, rgb(0x161224));
    fill_rect(&c, cx - 11, 41 + b, cx - 6, 56 + b, rgb(jktD));
    fill_rect(&c, cx + 6, 41 + b, cx + 11, 56 + b, rgb(jktD));
    fill_rect(&c, cx - 7, 39 + b, cx + 7, 42 + b, rgb(trim)); /* collar */
    fill_rect(&c, cx - 1, 47 + b, cx + 1, 51 + b, rgb(0x64F0FF)); /* glowing emblem */

    /* Graceful neck + chin shadow */
    fill_rect(&c, cx - 3, 33 + b, cx + 3, 40 + b, rgb(0xF6D4BA));
    fill_rect(&c, cx - 3, 33 + b, cx + 3, 35 + b, rgb(0xDFB090));

    /* Dynamic weapon poses */
    if (who == RUMI) {
        fill_poly(&c, (float[]){ cx - 11, cx - 17, cx - 15, cx - 10 }, (float[]){ 41 + b, 53 + b, 57 + b, 49 + b }, 4, rgb(jkt));
        fill_rect(&c, cx - 17, 54 + b, cx - 13, 58 + b, rgb(0xF6D4BA));
        fill_poly(&c, (float[]){ cx + 11, cx + 21, cx + 23, cx + 10 }, (float[]){ 41 + b, 33 + b - step, 37 + b - step, 47 + b }, 4, rgb(jkt));
        fill_rect(&c, cx + 20, 32 + b - step, cx + 24, 36 + b - step, rgb(0xF6D4BA));
        fill_rect(&c, cx + 18, 29 + b - step, cx + 27, 32 + b - step, rgb(0xFFD23F)); /* gold hilt */
        /* Ornate glowing Saingeom spirit sword */
        fill_poly(&c, (float[]){ cx + 21, cx + 26, cx + 43, cx + 39 },
                  (float[]){ 30 + b - step, 30 + b - step, 3 + b - step, 3 + b - step }, 4, rgb(0x4EE8FF));
        fill_poly(&c, (float[]){ cx + 23, cx + 25, cx + 42, cx + 40 },
                  (float[]){ 28 + b - step, 28 + b - step, 5 + b - step, 5 + b - step }, 4, rgb(0xFFFFFF));
    } else if (who == MIRA) {
        fill_poly(&c, (float[]){ cx - 11, cx - 21, cx - 20, cx - 10 }, (float[]){ 41 + b, 43 + b + step, 47 + b + step, 47 + b }, 4, rgb(jkt));
        fill_poly(&c, (float[]){ cx + 11, cx + 19, cx + 18, cx + 10 }, (float[]){ 41 + b, 50 + b, 54 + b, 47 + b }, 4, rgb(jkt));
        fill_rect(&c, cx - 23, 42 + b + step, cx - 19, 46 + b + step, rgb(0xF6D4BA));
        fill_rect(&c, cx + 16, 49 + b, cx + 20, 53 + b, rgb(0xF6D4BA));
        /* Two-handed curved Gok-do crescent polearm/glaive */
        fill_poly(&c, (float[]){ cx - 28, cx - 26, cx - 14, cx - 16 },
                  (float[]){ 104 + b, 104 + b, 15 + b, 15 + b }, 4, rgb(0xD8DCE8));
        fill_poly(&c, (float[]){ cx - 19, cx - 6, cx - 13, cx - 24 },
                  (float[]){ 18 + b, 2 + b, 24 + b, 27 + b }, 4, rgb(0xFF4F9A));
        fill_poly(&c, (float[]){ cx - 16, cx - 9, cx - 14 }, (float[]){ 16 + b, 6 + b, 21 + b }, 3, rgb(0xFFEAF6));
    } else {
        /* Zoey dual-wielding glowing Shin-kal daggers */
        fill_poly(&c, (float[]){ cx - 11, cx - 22, cx - 23, cx - 10 }, (float[]){ 41 + b, 34 + b - step, 38 + b - step, 47 + b }, 4, rgb(jkt));
        fill_poly(&c, (float[]){ cx + 11, cx + 22, cx + 23, cx + 10 }, (float[]){ 41 + b, 34 + b + step, 38 + b + step, 47 + b }, 4, rgb(jkt));
        fill_rect(&c, cx - 25, 33 + b - step, cx - 21, 37 + b - step, rgb(0xF6D4BA));
        fill_rect(&c, cx + 21, 33 + b + step, cx + 25, 37 + b + step, rgb(0xF6D4BA));
        fill_rect(&c, cx - 27, 32 + b - step, cx - 23, 35 + b - step, rgb(0xFFD23F));
        fill_rect(&c, cx + 23, 32 + b + step, cx + 27, 35 + b + step, rgb(0xFFD23F));
        fill_poly(&c, (float[]){ cx - 23, cx - 40, cx - 39, cx - 26 },
                  (float[]){ 32 + b - step, 14 + b - step, 18 + b - step, 36 + b - step }, 4, rgb(0x4EE8FF));
        fill_poly(&c, (float[]){ cx + 23, cx + 40, cx + 39, cx + 26 },
                  (float[]){ 32 + b + step, 14 + b + step, 18 + b + step, 36 + b + step }, 4, rgb(0x4EE8FF));
        fill_poly(&c, (float[]){ cx - 24, cx - 38, cx - 26 }, (float[]){ 32 + b - step, 16 + b - step, 34 + b - step }, 3, rgb(0xFFFFFF));
        fill_poly(&c, (float[]){ cx + 24, cx + 38, cx + 26 }, (float[]){ 32 + b + step, 16 + b + step, 34 + b + step }, 3, rgb(0xFFFFFF));
    }

    /* Sculpted anime idol face: tapered jawline, curved upper lashes, layered iris & subtle smile */
    fill_ellipse(&c, (float)cx, 23 + b, 10.2f, 9.8f, rgb(0xF6D4BA));
    fill_ellipse(&c, (float)cx, 29 + b, 7.2f, 5.8f, rgb(0xF6D4BA));
    uint32_t eyeC = who == RUMI ? 0x7A44E0 : who == MIRA ? 0xD04088 : 0x269088;
    fill_rect(&c, cx - 7, 22 + b, cx - 3, 26 + b, rgb(0xFFFFFF));
    fill_rect(&c, cx + 3, 22 + b, cx + 7, 26 + b, rgb(0xFFFFFF));
    fill_rect(&c, cx - 6, 22 + b, cx - 3, 26 + b, rgb(eyeC));
    fill_rect(&c, cx + 3, 22 + b, cx + 6, 26 + b, rgb(eyeC));
    fill_rect(&c, cx - 5, 23 + b, cx - 4, 25 + b, rgb(0x140E22));
    fill_rect(&c, cx + 4, 23 + b, cx + 5, 25 + b, rgb(0x140E22));
    put(&c, cx - 6, 22 + b, rgb(0xFFFFFF)); put(&c, cx + 4, 22 + b, rgb(0xFFFFFF));
    fill_rect(&c, cx - 8, 21 + b, cx - 3, 21 + b, rgb(0x181028)); /* curved upper lash */
    fill_rect(&c, cx + 3, 21 + b, cx + 8, 21 + b, rgb(0x181028));
    put(&c, cx - 8, 27 + b, rgb(0xFFA0B4)); put(&c, cx - 7, 27 + b, rgb(0xFFA0B4));
    put(&c, cx + 7, 27 + b, rgb(0xFFA0B4)); put(&c, cx + 8, 27 + b, rgb(0xFFA0B4));
    put(&c, cx - 1, 30 + b, rgb(0xC85272)); put(&c, cx, 30 + b, rgb(0xC85272)); put(&c, cx + 1, 29 + b, rgb(0xC85272));

    /* Detailed layered hairstyles framing each face */
    if (who == RUMI) {
        fill_ellipse(&c, (float)cx, 13 + b, 11.4f, 6.0f, rgb(0x8F4DFF));
        fill_poly(&c, (float[]){ cx - 10, cx + 6, cx + 2, cx - 11 }, (float[]){ 13 + b, 12 + b, 20 + b, 22 + b }, 4, rgb(0xA870FF));
        fill_rect(&c, cx - 11, 15 + b, cx - 8, 27 + b, rgb(0x8F4DFF));
        fill_rect(&c, cx + 8, 15 + b, cx + 11, 27 + b, rgb(0x8F4DFF));
        fill_rect(&c, cx - 6, 11 + b, cx + 4, 12 + b, rgb(0xD4B8FF));
    } else if (who == MIRA) {
        fill_ellipse(&c, (float)cx, 13 + b, 11.4f, 6.0f, rgb(0xFF4F9A));
        fill_ellipse(&c, cx - 10, 9 + b, 4.2f, 4.2f, rgb(0xC82870));
        fill_ellipse(&c, cx + 10, 9 + b, 4.2f, 4.2f, rgb(0xC82870));
        fill_rect(&c, cx - 11, 15 + b, cx - 8, 30 + b, rgb(0xFF8CC0));
        fill_rect(&c, cx + 8, 15 + b, cx + 11, 30 + b, rgb(0xFF8CC0));
        fill_rect(&c, cx - 5, 11 + b, cx + 5, 12 + b, rgb(0xFFB4DA));
    } else {
        fill_ellipse(&c, (float)cx, 13 + b, 10.8f, 5.5f, rgb(0x241E36));
        fill_ellipse(&c, cx - 12, 8 + b, 5.8f, 5.8f, rgb(0x241E36));
        fill_ellipse(&c, cx + 12, 8 + b, 5.8f, 5.8f, rgb(0x241E36));
        fill_rect(&c, cx - 14, 11 + b, cx - 10, 12 + b, rgb(0xFFD24A)); /* gold bun ties */
        fill_rect(&c, cx + 10, 11 + b, cx + 14, 12 + b, rgb(0xFFD24A));
        fill_rect(&c, cx - 13, 6 + b, cx - 10, 7 + b, rgb(0x4E4272));
        fill_rect(&c, cx + 10, 6 + b, cx + 13, 7 + b, rgb(0x4E4272));
        fill_rect(&c, cx - 6, 15 + b, cx - 3, 19 + b, rgb(0x241E36)); /* choppy fringe */
        fill_rect(&c, cx - 1, 15 + b, cx + 2, 20 + b, rgb(0x241E36));
        fill_rect(&c, cx + 4, 15 + b, cx + 7, 19 + b, rgb(0x241E36));
        fill_rect(&c, cx - 11, 15 + b, cx - 8, 28 + b, rgb(0x241E36));
        fill_rect(&c, cx + 8, 15 + b, cx + 11, 28 + b, rgb(0x241E36));
    }

    outline(&c, rgb(0x120820));
    return c;
}

/* Derpy the chubby blue tiger + Sussie the gat-hat magpie perched on his head (52x44). */
static Canvas make_tiger(int frame) {
    Canvas c = canvas_new(TIGER_W, TIGER_H);
    int b = frame ? -1 : 0;
    fill_round_rect(&c, 36, 28 + b, 45, 31 + b, 1, rgb(0x3A88E8));
    fill_rect(&c, 45, 25 - b, 48, 28 - b, rgb(0x163060));
    fill_ellipse(&c, 25, 30 + b, 13.0f, 9.0f, rgb(0x3A88E8));
    fill_ellipse(&c, 24, 33 + b, 7.5f, 4.8f, rgb(0xD8ECFF));
    fill_rect(&c, 15, 36, 19, 39, rgb(0xD8ECFF));
    fill_rect(&c, 29, 36, 33, 39, rgb(0xD8ECFF));
    fill_rect(&c, 32, 25 + b, 36, 27 + b, rgb(0x163060));
    fill_rect(&c, 31, 30 + b, 36, 31 + b, rgb(0x163060));
    /* Big round head, ears & bright golden eyes */
    fill_ellipse(&c, 21, 20 + b, 12.0f, 10.0f, rgb(0x3A88E8));
    fill_ellipse(&c, 11, 13 + b, 3.8f, 3.8f, rgb(0x3A88E8));
    fill_ellipse(&c, 31, 13 + b, 3.8f, 3.8f, rgb(0x3A88E8));
    fill_rect(&c, 19, 12 + b, 23, 15 + b, rgb(0x163060));
    fill_ellipse(&c, 16, 20 + b, 3.5f, 3.5f, rgb(0xFFE04A));
    fill_ellipse(&c, 26, 20 + b, 3.5f, 3.5f, rgb(0xFFE04A));
    fill_rect(&c, 15, 18 + b, 17, 22 + b, rgb(0x101018));
    fill_rect(&c, 25, 18 + b, 27, 22 + b, rgb(0x101018));
    fill_ellipse(&c, 21, 25 + b, 5.2f, 3.2f, rgb(0xD8ECFF));
    fill_rect(&c, 19, 23 + b, 23, 24 + b, rgb(0xFF7FA8));
    fill_rect(&c, 18, 26 + b, 24, 27 + b, rgb(0x161420)); /* happy grin */
    /* Sussie the magpie perched on Derpy's head wearing a tiny Korean gat hat */
    fill_ellipse(&c, 21, 9 + b, 3.2f, 2.5f, rgb(0x1C1E2E));
    fill_rect(&c, 20, 9 + b, 22, 10 + b, rgb(0xFFFFFF));
    put(&c, 24, 8 + b, rgb(0xFFD24A));
    fill_rect(&c, 16, 5 + b, 26, 6 + b, rgb(0x101018)); /* gat brim */
    fill_rect(&c, 19, 2 + b, 23, 5 + b, rgb(0x101018)); /* gat crown */
    outline(&c, rgb(0x0E1B3A));
    return c;
}

static void *create(SDL_Renderer *r, int w, int h) {
    Kpop *k = calloc(1, sizeof *k);
    k->w = w;
    k->h = h;
    if (h >= ROWS) {
        k->px = h / ROWS;
        k->scale = 1;
        k->W = (w + k->px - 1) / k->px;
        k->H = (h + k->px - 1) / k->px;
    } else {
        k->px = 1;
        k->scale = (float)h / ROWS;
        k->H = ROWS;
        k->W = (int)ceilf(w / k->scale);
    }
    int W = k->W, H = k->H;

    Canvas bg = canvas_new(W, H);
    for (int y = 0; y < H; y++) {
        float t = (float)y / H;
        uint32_t col = ((uint32_t)(14 + 24 * t) << 16) | ((uint32_t)(8 + 10 * t) << 8) | (uint32_t)(38 + 44 * t);
        fill_rect(&bg, 0, y, W - 1, y, rgb(col));
    }
    /* Seoul skyline silhouettes */
    for (int x = 0; x < W; x += 26) {
        int bh = 42 + (x * 37) % 68;
        fill_rect(&bg, x, (int)(H * 0.64f) - bh, x + 22, (int)(H * 0.64f), rgb(0x120C28));
        for (int wy = (int)(H * 0.64f) - bh + 5; wy < (int)(H * 0.63f); wy += 7)
            if ((x + wy) % 3 == 0) fill_rect(&bg, x + 6, wy, x + 9, wy + 2, rgb(0x64E8FF));
    }
    /* Overhead concert lighting truss */
    fill_rect(&bg, 0, 12, W - 1, 18, rgb(0x282042));
    for (int x = 24; x < W; x += 48) fill_rect(&bg, x, 18, x + 8, 22, rgb(0x64F0FF));
    /* Jumbotron LED screen frame behind stage */
    int jx0 = (int)(W * 0.12f), jx1 = (int)(W * 0.88f), jy0 = (int)(H * 0.14f), jy1 = (int)(H * 0.64f);
    fill_rect(&bg, jx0 - 4, jy0 - 4, jx1 + 4, jy1, rgb(0x2E2252));
    fill_rect(&bg, jx0, jy0, jx1, jy1, rgb(0x160E30));
    /* Perspective 3D concert stage deck */
    int st0 = (int)(H * 0.64f), st1 = (int)(H * 0.84f);
    fill_rect(&bg, 0, st0, W - 1, st1, rgb(0x241646));
    for (int x = 0; x <= W; x += W / 10) {
        float xBot = W * 0.5f + (x - W * 0.5f) * 1.45f;
        for (int s = 0; s <= 40; s++) {
            float f = s / 40.0f;
            put(&bg, (int)(x + (xBot - x) * f), st0 + (int)((st1 - st0) * f), rgb(0x3D2C6E));
        }
    }
    fill_rect(&bg, 0, st1, W - 1, H - 1, rgb(0x120A26));
    k->bg = canvas_texture(r, &bg);

    Canvas lc = text_canvas("HUNTR/X", rgb(0x64F0FF));
    k->logoW = lc.w;
    k->logoH = lc.h;
    k->logo = canvas_texture(r, &lc);

    for (int i = 0; i < 3; i++)
        for (int fr = 0; fr < 4; fr++) {
            Canvas hc = make_hunter(i, fr);
            k->hunters[i][fr] = canvas_texture(r, &hc);
        }
    for (int fr = 0; fr < 2; fr++) {
        Canvas tc = make_tiger(fr);
        k->tiger[fr] = canvas_texture(r, &tc);
    }
    k->demonT = 2.0f;
    return k;
}

static void step(void *p, float dt) {
    Kpop *k = p;
    k->t += dt;
    k->beat = k->t * 2.15f;
    k->nextSpark -= dt;
    if (k->nextSpark <= 0) {
        k->nextSpark = frand(0.05f, 0.12f);
        spawn_spark(k, frand(0.18f, 0.82f) * k->W, frand(0.36f, 0.72f) * k->H, 0);
    }
    for (int i = 0; i < MAX_SPARKS; i++) {
        Spark *s = &k->sp[i];
        if (s->life <= 0) continue;
        s->life -= dt;
        s->x += s->vx * dt;
        s->y += s->vy * dt;
        s->vy += 0.04f * k->H * dt;
    }
    /* Cute shadowy demon wisps popping into Honmoon sparkles */
    k->demonT -= dt;
    if (k->demonT <= 0) {
        k->demonT = frand(2.5f, 4.5f);
        for (int i = 0; i < MAX_DEMONS; i++) {
            if (k->demons[i].alive) continue;
            int left = irand(2);
            k->demons[i] = (Demon){ left ? -10.0f : k->W + 10.0f, frand(0.68f, 0.74f) * k->H,
                                    (left ? 1 : -1) * k->W * 0.10f, frand(0, 6.28f), 1 };
            break;
        }
    }
    for (int i = 0; i < MAX_DEMONS; i++) {
        Demon *dm = &k->demons[i];
        if (!dm->alive) continue;
        dm->x += dm->vx * dt;
        dm->ph += dt * 6.0f;
        if (fabsf(dm->x - k->W * 0.5f) < k->W * 0.25f) {
            dm->alive = 0;
            for (int s = 0; s < 10; s++) spawn_spark(k, dm->x, dm->y, 0x64F0FF);
        }
    }
}

static void draw_scene(const Kpop *k, SDL_Renderer *r) {
    int W = k->W, H = k->H, u = k->px;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_RenderCopy(r, k->bg, NULL, &(SDL_Rect){ 0, 0, W * u, H * u });

    /* Pixel-art LED equalizer bars on the jumbotron */
    static const uint32_t BEAMS[4] = { 0x64F0FF, 0xB078FF, 0xFF4FA8, 0xFFE055 };
    float eqW = W * 0.72f, eqX0 = W * 0.14f, barW = eqW / EQ_BARS;
    for (int i = 0; i < EQ_BARS; i++) {
        float amp = 0.22f + 0.75f * fabsf(sinf(k->beat * 1.6f + i * 0.55f) * cosf(k->t * 2.3f - i * 0.3f));
        int blocks = (int)(amp * 14);
        SDL_Color bc = hsv((float)((185 + i * 12 + (int)(k->t * 35)) % 360), 0.75f, 1.0f);
        bc.a = 135;
        set_color(r, bc);
        for (int b = 0; b < blocks; b++) {
            SDL_Rect seg = { (int)(eqX0 + i * barW + 2) * u, ((int)(H * 0.64f) - (b + 1) * 7) * u,
                             (int)(barW - 4) * u, 5 * u };
            SDL_RenderFillRect(r, &seg);
        }
    }

    /* Glowing HUNTR/X neon logo at top of stage screen */
    {
        int lw = k->logoW * 3, lh = k->logoH * 3;
        SDL_RenderCopy(r, k->logo, NULL, &(SDL_Rect){ (W - lw) / 2 * u, (int)(H * 0.17f) * u, lw * u, lh * u });
    }

    /* Stepped pixel-art stage spotlights sweeping across jumbotron and stage */
    for (int i = 0; i < 4; i++) {
        int bx = (int)(W * (0.18f + 0.21f * i));
        float sweep = sinf(k->t * (1.1f + i * 0.23f) + i * 1.5f) * W * 0.16f;
        SDL_Color bc = hexcol(BEAMS[i]);
        bc.a = 24;
        set_color(r, bc);
        for (int y = 20; y < (int)(H * 0.74f); y += 6) {
            float f = (float)(y - 20) / (H * 0.74f - 20);
            int cx = bx + (int)(sweep * f);
            int hw = (int)(4 + f * W * 0.085f);
            SDL_RenderFillRect(r, &(SDL_Rect){ (cx - hw) * u, y * u, hw * 2 * u, 6 * u });
        }
    }

    /* Shadowy demon wisps entering backstage */
    for (int i = 0; i < MAX_DEMONS; i++) {
        const Demon *dm = &k->demons[i];
        if (!dm->alive) continue;
        int dx = (int)dm->x, dy = (int)(dm->y + sinf(dm->ph) * 4);
        set_color(r, hexcol(0x360C3E));
        SDL_RenderFillRect(r, &(SDL_Rect){ (dx - 6) * u, (dy - 6) * u, 12 * u, 12 * u });
        set_color(r, hexcol(0xFF3A70));
        SDL_RenderFillRect(r, &(SDL_Rect){ (dx - 3) * u, (dy - 2) * u, 2 * u, 2 * u });
        SDL_RenderFillRect(r, &(SDL_Rect){ (dx + 1) * u, (dy - 2) * u, 2 * u, 2 * u });
    }

    /* Performers sorted back-to-front by foot baseline: Mira/Zoey (0.75) -> Derpy (0.77) -> Rumi (0.78) */
    int fr = (int)(k->beat * 2) % 4;
    static const int order[2] = { MIRA, ZOEY };
    for (int p = 0; p < 2; p++) {
        int slot = p == 0 ? 0 : 2;
        int cx = (int)(W * (0.31f + slot * 0.19f)), by = (int)(H * 0.75f);
        set_color(r, (SDL_Color){ 8, 4, 18, 95 });
        SDL_RenderFillRect(r, &(SDL_Rect){ (cx - 18) * u, (by - 4) * u, 36 * u, 6 * u });
        SDL_RenderCopy(r, k->hunters[order[p]][fr], NULL,
                       &(SDL_Rect){ (cx - HUNTER_W / 2) * u, (by - HUNTER_H) * u, HUNTER_W * u, HUNTER_H * u });
    }
    {
        int tx = (int)(W * 0.12f), ty = (int)(H * 0.77f) - TIGER_H;
        set_color(r, (SDL_Color){ 8, 4, 18, 95 });
        SDL_RenderFillRect(r, &(SDL_Rect){ (tx + 8) * u, (int)(H * 0.77f - 4) * u, (TIGER_W - 16) * u, 5 * u });
        SDL_RenderCopy(r, k->tiger[fr % 2], NULL, &(SDL_Rect){ tx * u, ty * u, TIGER_W * u, TIGER_H * u });
    }
    {
        int cx = (int)(W * (0.31f + 0.19f)), by = (int)(H * 0.78f);
        set_color(r, (SDL_Color){ 8, 4, 18, 105 });
        SDL_RenderFillRect(r, &(SDL_Rect){ (cx - 18) * u, (by - 4) * u, 36 * u, 6 * u });
        SDL_RenderCopy(r, k->hunters[RUMI][fr], NULL,
                       &(SDL_Rect){ (cx - HUNTER_W / 2) * u, (by - HUNTER_H) * u, HUNTER_W * u, HUNTER_H * u });
    }

    /* Shimmering Honmoon neon barrier wave along front edge of stage */
    for (int x = 0; x < W; x += 4) {
        int wy = (int)(H * 0.82f + sinf(x * 0.02f + k->t * 4.5f) * 3);
        set_color(r, (SDL_Color){ 100, 240, 255, 200 });
        SDL_RenderFillRect(r, &(SDL_Rect){ x * u, wy * u, 4 * u, 3 * u });
    }

    /* Honmoon spirit sparks */
    for (int i = 0; i < MAX_SPARKS; i++) {
        const Spark *s = &k->sp[i];
        if (s->life <= 0) continue;
        SDL_Color c = hexcol(s->col);
        c.a = (Uint8)(255 * (s->life / s->max));
        set_color(r, c);
        SDL_RenderFillRect(r, &(SDL_Rect){ (int)s->x * u, (int)s->y * u, 3 * u, 3 * u });
    }

    /* Crowd silhouettes & waving lightsticks drawn strictly back-to-front by row */
    for (int row = 2; row >= 0; row--) {
        for (int i = row; i < LIGHTSTICKS; i += 3) {
            int lx = (int)(W * (i + 0.5f) / LIGHTSTICKS);
            int wave = (int)(sinf(k->beat * PI_F + (i % 4) * 0.45f) * 7);
            int headY = H - 14 - row * 5;
            set_color(r, hexcol(0x0D051A));
            SDL_RenderFillRect(r, &(SDL_Rect){ (lx - 7) * u, (headY - 4) * u, 14 * u, 24 * u });
            SDL_RenderFillRect(r, &(SDL_Rect){ (lx - 5) * u, (headY - 10) * u, 10 * u, 6 * u });
            set_color(r, hexcol(0x3A2A5A));
            SDL_RenderFillRect(r, &(SDL_Rect){ (lx + wave / 2) * u, (headY - 22) * u, 2 * u, 14 * u });
            set_color(r, hexcol(BEAMS[i % 4]));
            SDL_RenderFillRect(r, &(SDL_Rect){ (lx + wave - 3) * u, (headY - 28) * u, 7 * u, 7 * u });
            set_color(r, hexcol(0xFFFFFF));
            SDL_RenderFillRect(r, &(SDL_Rect){ (lx + wave - 1) * u, (headY - 26) * u, 3 * u, 3 * u });
        }
    }
}

static void draw(void *p, SDL_Renderer *r) {
    Kpop *k = p;
    if (k->scale == 1) { draw_scene(k, r); return; }
    if (!k->small) {
        k->small = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, k->W, k->H);
        if (!k->small) return;
        SDL_SetTextureScaleMode(k->small, SDL_ScaleModeLinear);
    }
    SDL_Texture *old = SDL_GetRenderTarget(r);
    SDL_Rect vp;
    SDL_RenderGetViewport(r, &vp);
    SDL_SetRenderTarget(r, k->small);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);
    draw_scene(k, r);
    SDL_SetRenderTarget(r, old);
    SDL_RenderSetViewport(r, &vp);
    SDL_RenderCopy(r, k->small, NULL, &(SDL_Rect){ 0, 0, k->w, k->h });
}

static void destroy(void *p) {
    Kpop *k = p;
    SDL_DestroyTexture(k->bg);
    SDL_DestroyTexture(k->logo);
    if (k->small) SDL_DestroyTexture(k->small);
    for (int i = 0; i < 3; i++)
        for (int fr = 0; fr < 4; fr++) SDL_DestroyTexture(k->hunters[i][fr]);
    for (int fr = 0; fr < 2; fr++) SDL_DestroyTexture(k->tiger[fr]);
    free(k);
}

const Saver saver_kpop = { "kpop", "KPOP", create, step, draw, destroy, NULL, NULL, NULL, NULL, NULL, NULL };
