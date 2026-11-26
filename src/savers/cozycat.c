/* Cozy Fall Cat: a warm, dry 360p pixel-art room with a crackling brick fireplace, a sleeping
   cat curled snugly in a cat bed beside the fire, a Thanksgiving turkey on the table, and a
   curtained window looking out onto a rainy autumn afternoon. */
#include "../engine.h"
#include <math.h>
#include <stdlib.h>

#define ROWS 360
#define MAX_RAIN 80
#define MAX_LEAVES 20
#define MAX_EMBERS 20
#define MAX_STEAM 12

typedef struct { float x, y, l; } Rain;
typedef struct { float x, y, vx, vy, ph; uint32_t col; } Leaf;
typedef struct { float x, y, vy, life, max; } Ember;
typedef struct { float x, y, vy, ph, life, max; } Steam;

typedef struct {
    int w, h, px, W, H, fireTopY;
    int candleX, candleY, mantleX0, mantleX1, mantleY, rodX0, rodX1, rodY;
    float scale, t, zzzT, steamT;
    SDL_Texture *small, *room, *outside, *glow, *lampGlow, *fire[4], *cat[2], *turkey;
    SDL_Rect winRect, fireRect, catRect, turkeyRect, glowRect, lampGlowRect;
    Rain rain[MAX_RAIN];
    Leaf leaves[MAX_LEAVES];
    Ember embers[MAX_EMBERS];
    Steam steam[MAX_STEAM];
} CozyCat;

static Canvas make_fire(int fr) {
    Canvas c = canvas_new(56, 40);
    /* Logs with bark grain */
    fill_round_rect(&c, 6, 30, 49, 37, 2, rgb(0x3E2212));
    fill_rect(&c, 10, 32, 45, 35, rgb(0x5A321C));
    fill_rect(&c, 14, 28, 42, 32, rgb(0x4A2816));
    /* Multi-layered pixel flames */
    int sw = (fr % 2) ? 2 : -2;
    int h0 = 19 + (fr == 1 ? 3 : fr == 3 ? -2 : 0);
    int h1 = 26 + (fr == 2 ? 4 : fr == 0 ? 2 : 0);
    fill_poly(&c, (float[]){ 10, 17 + sw, 25 }, (float[]){ 32, (float)(32 - h0), 32 }, 3, rgb(0xFF4D10));
    fill_poly(&c, (float[]){ 19, 28 - sw, 37 }, (float[]){ 32, (float)(32 - h1), 32 }, 3, rgb(0xFF6A14));
    fill_poly(&c, (float[]){ 30, 38 + sw, 46 }, (float[]){ 32, (float)(32 - h0), 32 }, 3, rgb(0xFF4D10));
    fill_poly(&c, (float[]){ 22, 28 - sw, 34 }, (float[]){ 32, (float)(32 - h1 + 8), 32 }, 3, rgb(0xFFD84A));
    fill_poly(&c, (float[]){ 13, 17 + sw, 21 }, (float[]){ 32, (float)(32 - h0 + 6), 32 }, 3, rgb(0xFFD84A));
    fill_poly(&c, (float[]){ 34, 38 + sw, 42 }, (float[]){ 32, (float)(32 - h0 + 6), 32 }, 3, rgb(0xFFD84A));
    return c;
}

/* Soft Bayer-dithered pixel-art warm firelight halo (no hard circle edges!). */
static Canvas make_fire_glow(int rad) {
    static const int B4[4][4] = {
        { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 }
    };
    int sz = rad * 2 + 1;
    Canvas c = canvas_new(sz, sz);
    for (int y = 0; y < sz; y++)
        for (int x = 0; x < sz; x++) {
            float dx = (float)(x - rad) / rad, dy = (float)(y - rad) / rad;
            float d = sqrtf(dx * dx + dy * dy);
            if (d >= 1.0f) continue;
            float intensity = (1.0f - d) * (1.0f - d);
            float threshold = B4[y & 3][x & 3] / 16.0f;
            if (intensity * 1.15f > threshold) {
                uint8_t a = (uint8_t)(48 * intensity);
                c.px[y * sz + x] = (0xFF9030u << 8) | a;
            }
        }
    return c;
}

/* Detailed 76x46 pixel-art orange tabby curled snugly inside a bolstered cat bed. */
static Canvas make_sleeping_cat(int fr) {
    Canvas c = canvas_new(76, 46);
    /* Plush bolstered round cat bed in cool blueberry-slate for contrast with orange tabby */
    fill_ellipse(&c, 38, 30, 34, 13.5f, rgb(0x3E4E78));
    fill_ellipse(&c, 38, 28, 31, 11.5f, rgb(0x5C70A2));
    fill_ellipse(&c, 38, 28, 25, 8.5f, rgb(0xF6EAD8));
    /* Curled orange tabby cat centered inside the cushion */
    int b = fr ? -1 : 0;
    fill_ellipse(&c, 34, 26 + b, 17.0f, 8.8f, rgb(0xE68A32));
    fill_rect(&c, 22, 19 + b, 24, 26 + b, rgb(0xC2681A));
    fill_rect(&c, 30, 18 + b, 32, 25 + b, rgb(0xC2681A));
    fill_rect(&c, 38, 19 + b, 40, 24 + b, rgb(0xC2681A));
    /* Tucked paws, curled tail with gentle tip wiggle, cheeks & whiskers */
    fill_round_rect(&c, 18, 29, 36, 33, 2, rgb(0xD9791C));
    fill_rect(&c, 19, 30, 22, 32, rgb(0xFFE2B8));
    fill_ellipse(&c, 42, 30 - (fr ? 1 : 0), 4.5f, 2.5f, rgb(0xFFE2B8));
    /* Head, ears (slight ear twitch on fr == 1) & sleepy face */
    fill_ellipse(&c, 48, 23, 8.8f, 7.2f, rgb(0xF09B42));
    fill_poly(&c, (float[]){ 42, 44, 47 }, (float[]){ 17, 9 + (fr ? 1 : 0), 16 }, 3, rgb(0xE68A32));
    fill_poly(&c, (float[]){ 50, 53, 55 }, (float[]){ 16, 9, 17 }, 3, rgb(0xE68A32));
    fill_poly(&c, (float[]){ 43, 44, 46 }, (float[]){ 16, 11 + (fr ? 1 : 0), 16 }, 3, rgb(0xFF9EB5));
    fill_poly(&c, (float[]){ 51, 53, 54 }, (float[]){ 16, 11, 16 }, 3, rgb(0xFF9EB5));
    fill_rect(&c, 44, 23, 47, 23, rgb(0x3A1E08));
    fill_rect(&c, 50, 23, 53, 23, rgb(0x3A1E08));
    fill_rect(&c, 48, 25, 49, 25, rgb(0xFF9EB5));
    put(&c, 39, 23, rgb(0xFFF0DC)); put(&c, 40, 25, rgb(0xFFF0DC));
    put(&c, 57, 23, rgb(0xFFF0DC)); put(&c, 56, 25, rgb(0xFFF0DC));
    outline(&c, rgb(0x2E180A));
    return c;
}

/* Classic 3/4 side-profile roasted Thanksgiving turkey (84x52): plump glazed breast, tucked
   wing, sculpted foreground thigh, and compact cartoon drumstick bones on a garnished platter. */
static Canvas make_turkey(void) {
    Canvas c = canvas_new(84, 52);
    /* Gleaming silver serving platter */
    fill_ellipse(&c, 42, 42, 36, 7.0f, rgb(0x9EA8B8));
    fill_ellipse(&c, 42, 40, 35, 6.0f, rgb(0xE4E8EE));
    fill_ellipse(&c, 42, 39, 31, 4.6f, rgb(0xF6F8FC));
    /* Crisp green parsley/lettuce tufts, red cranberries & golden lemon garnish */
    fill_ellipse(&c, 15, 40, 5.5f, 3.2f, rgb(0x3B8A34));
    fill_ellipse(&c, 19, 39, 4.0f, 2.5f, rgb(0x58B44A));
    fill_ellipse(&c, 67, 40, 5.5f, 3.2f, rgb(0x3B8A34));
    fill_ellipse(&c, 63, 39, 4.0f, 2.5f, rgb(0x58B44A));
    fill_ellipse(&c, 27, 42, 4.2f, 2.5f, rgb(0x4CA440));
    fill_ellipse(&c, 55, 42, 4.2f, 2.5f, rgb(0x4CA440));
    fill_ellipse(&c, 22, 42, 2.0f, 1.8f, rgb(0xD82C34));
    fill_ellipse(&c, 24, 41, 1.6f, 1.5f, rgb(0xFF4A52));
    fill_ellipse(&c, 60, 41, 2.2f, 1.8f, rgb(0xF2C24A));
    fill_ellipse(&c, 62, 42, 1.8f, 1.5f, rgb(0xD82C34));

    /* Far background drumstick behind body: short roast leg + tiny ivory bone knuckles */
    fill_poly(&c, (float[]){ 48, 57, 59, 50 }, (float[]){ 22, 15, 18, 25 }, 4, rgb(0x8A3A10));
    fill_poly(&c, (float[]){ 57, 61, 62, 58 }, (float[]){ 16, 13, 15, 18 }, 4, rgb(0xF6ECD8));
    fill_ellipse(&c, 62.0f, 13.0f, 1.6f, 1.6f, rgb(0xFFF8EC));
    fill_ellipse(&c, 63.0f, 15.0f, 1.6f, 1.6f, rgb(0xFFF8EC));

    /* Plump low oval roast turkey body resting snugly on the platter */
    fill_ellipse(&c, 36, 30, 20.5f, 10.2f, rgb(0x7A320C));
    fill_ellipse(&c, 36, 28, 20.0f, 11.0f, rgb(0xB8561A));
    fill_ellipse(&c, 33, 25, 16.0f, 8.2f, rgb(0xCF6E26));
    fill_ellipse(&c, 29, 21, 10.5f, 4.8f, rgb(0xE68A38));
    fill_ellipse(&c, 26, 19, 4.5f, 1.8f, rgb(0xFFD488));
    put(&c, 35, 22, rgb(0x9E4414)); put(&c, 39, 24, rgb(0x9E4414)); put(&c, 29, 26, rgb(0x9E4414));

    /* Neatly tucked front wing nestled along the lower-left flank */
    fill_ellipse(&c, 28, 33, 8.2f, 4.0f, rgb(0x6E2B0A));
    fill_ellipse(&c, 28, 32, 7.6f, 3.6f, rgb(0xC66420));
    fill_ellipse(&c, 27, 30, 5.2f, 2.0f, rgb(0xE28434));

    /* Prominent foreground thigh + compact drumstick hugging right flank with crisp knuckle bone */
    fill_ellipse(&c, 46, 31, 9.5f, 6.8f, rgb(0x642608));
    fill_ellipse(&c, 47, 30, 8.5f, 5.8f, rgb(0xB85418));
    fill_ellipse(&c, 47, 27, 5.5f, 3.2f, rgb(0xDA7A2C));
    fill_poly(&c, (float[]){ 49, 60, 62, 51 }, (float[]){ 27, 21, 25, 33 }, 4, rgb(0xB85418));
    fill_poly(&c, (float[]){ 50, 60, 61, 51 }, (float[]){ 26, 21, 23, 30 }, 4, rgb(0xDA7A2C));
    fill_rect(&c, 59, 21, 62, 25, rgb(0x6E2B0A));
    fill_poly(&c, (float[]){ 61, 66, 67, 62 }, (float[]){ 22, 19, 21, 24 }, 4, rgb(0xF6ECD8));
    fill_ellipse(&c, 67.0f, 19.0f, 1.8f, 1.8f, rgb(0xFFF8EC));
    fill_ellipse(&c, 68.0f, 21.5f, 1.8f, 1.8f, rgb(0xFFF8EC));

    outline(&c, rgb(0x2E180A));
    return c;
}

/* Leafy dappled autumn tree cluster in pixel art (no overlapping geometric discs). */
static void autumn_tree(Canvas *c, int cx, int cy, int w, int h, uint32_t c0, uint32_t c1, uint32_t c2) {
    fill_rect(c, cx - 3, cy, cx + 3, c->h - 1, rgb(0x3A281A));
    fill_rect(c, cx - 1, cy, cx, c->h - 1, rgb(0x4E3624));
    for (int i = 0; i < 16; i++) {
        float lx = cx + cosf(i * 2.1f) * w * (0.25f + 0.25f * (i % 3));
        float ly = cy - h * 0.2f + sinf(i * 1.7f) * h * 0.35f;
        float lr = 7.0f + (i % 4) * 2.5f;
        uint32_t col = i % 3 == 0 ? c0 : i % 3 == 1 ? c1 : c2;
        fill_ellipse(c, lx, ly, lr, lr * 0.85f, rgb(col));
    }
}

static void *create(SDL_Renderer *r, int w, int h) {
    CozyCat *s = calloc(1, sizeof *s);
    s->w = w;
    s->h = h;
    if (h >= ROWS) {
        s->px = h / ROWS;
        s->scale = 1;
        s->W = (w + s->px - 1) / s->px;
        s->H = (h + s->px - 1) / s->px;
    } else {
        s->px = 1;
        s->scale = (float)h / ROWS;
        s->H = ROWS;
        s->W = (int)ceilf(w / s->scale);
    }
    int W = s->W, H = s->H;

    int wx = (int)(W * 0.56f), wy = (int)(H * 0.11f), ww = (int)(W * 0.34f), wh = (int)(H * 0.45f);
    s->winRect = (SDL_Rect){ wx, wy, ww, wh };

    /* 360p pixel-art rainy autumn outside scene */
    Canvas out = canvas_new(ww, wh);
    for (int y = 0; y < wh; y++) {
        float k = (float)y / wh;
        uint32_t sky = ((uint32_t)(66 + 34 * k) << 16) | ((uint32_t)(84 + 30 * k) << 8) | (uint32_t)(114 + 22 * k);
        fill_rect(&out, 0, y, ww - 1, y, rgb(sky));
    }
    /* Soft rain clouds outside */
    fill_ellipse(&out, ww * 0.28f, wh * 0.18f, ww * 0.22f, wh * 0.09f, rgb(0x5A6B84));
    fill_ellipse(&out, ww * 0.68f, wh * 0.24f, ww * 0.26f, wh * 0.10f, rgb(0x52627A));
    fill_ellipse(&out, ww * 0.3f, wh * 1.02f, ww * 0.54f, wh * 0.32f, rgb(0x5B4A30));
    fill_ellipse(&out, ww * 0.8f, wh * 1.05f, ww * 0.50f, wh * 0.30f, rgb(0x4A3C28));
    autumn_tree(&out, (int)(ww * 0.20f), (int)(wh * 0.62f), (int)(ww * 0.16f), (int)(wh * 0.32f), 0xD95A26, 0xE69A28, 0xB83828);
    autumn_tree(&out, (int)(ww * 0.52f), (int)(wh * 0.58f), (int)(ww * 0.18f), (int)(wh * 0.34f), 0xE69A28, 0xC83A2A, 0xF0B838);
    autumn_tree(&out, (int)(ww * 0.82f), (int)(wh * 0.64f), (int)(ww * 0.15f), (int)(wh * 0.30f), 0xB83828, 0xD95A26, 0xE68028);
    s->outside = canvas_texture(r, &out);

    /* 360p pixel-art indoor room with rich autumn contrast */
    Canvas rm = canvas_new(W, H);
    int floorY = (int)(H * 0.74f);
    /* Warm sage-green striped wallpaper above chair rail */
    for (int x = 0; x < W; x++)
        fill_rect(&rm, x, 0, x, floorY - 47, rgb((x / 12) % 2 ? 0x5D7660 : 0x546C57));
    /* Cream chair-rail molding, warm oak ceiling beam & honey-walnut wainscoting */
    fill_rect(&rm, 0, 0, W - 1, 10, rgb(0x4E2E1A));
    fill_rect(&rm, 0, floorY - 47, W - 1, floorY - 45, rgb(0xDDD0B8));
    fill_rect(&rm, 0, floorY - 44, W - 1, floorY - 43, rgb(0xB8A88E));
    fill_rect(&rm, 0, floorY - 42, W - 1, floorY - 1, rgb(0x6C4226));
    for (int x = 10; x < W; x += 24)
        fill_rect(&rm, x, floorY - 38, x + 1, floorY - 6, rgb(0x52301A));
    /* Warm oak floorboards with staggered plank joints */
    fill_rect(&rm, 0, floorY, W - 1, H - 1, rgb(0x603820));
    for (int y = floorY + 10, row = 0; y < H; y += 12, row++) {
        fill_rect(&rm, 0, y, W - 1, y + 1, rgb(0x422414));
        for (int x = (row % 2) * 32; x < W; x += 64)
            fill_rect(&rm, x, y, x + 1, y + 11 < H ? y + 11 : H - 1, rgb(0x422414));
    }

    /* Brass-framed blue-sky autumn landscape painting between fireplace and window */
    int px0 = (int)(W * 0.37f), py0 = (int)(H * 0.17f);
    fill_rect(&rm, px0 - 3, py0 - 3, px0 + 47, py0 + 35, rgb(0xC8983E));
    fill_rect(&rm, px0 - 1, py0 - 1, px0 + 45, py0 + 33, rgb(0x8C6422));
    fill_rect(&rm, px0, py0, px0 + 44, py0 + 32, rgb(0x6494C8));
    fill_ellipse(&rm, px0 + 34, py0 + 8, 4, 4, rgb(0xFFD452));
    fill_ellipse(&rm, px0 + 22, py0 + 28, 22, 9, rgb(0x487E46));
    fill_ellipse(&rm, px0 + 13, py0 + 18, 7, 6, rgb(0xD95A26));
    fill_ellipse(&rm, px0 + 28, py0 + 20, 6, 5, rgb(0xE8A028));

    /* Braided hearth rug: deep teal, mustard gold, cranberry red & soft cream */
    fill_ellipse(&rm, W * 0.39f, H * 0.87f, W * 0.20f, H * 0.08f, rgb(0x2C5D68));
    fill_ellipse(&rm, W * 0.39f, H * 0.87f, W * 0.17f, H * 0.06f, rgb(0xDCA038));
    fill_ellipse(&rm, W * 0.39f, H * 0.87f, W * 0.14f, H * 0.046f, rgb(0xA83238));
    fill_ellipse(&rm, W * 0.39f, H * 0.87f, W * 0.10f, H * 0.030f, rgb(0xE8D8BE));

    /* Left-wall brass wall sconce & cozy firewood log rack */
    int fx = (int)(W * 0.08f), fw = (int)(W * 0.24f), fy = (int)(H * 0.34f), fh = (int)(H * 0.42f);
    {
        int sx = fx / 2, sy = (int)(H * 0.28f);
        fill_rect(&rm, sx - 2, sy - 8, sx + 2, sy + 12, rgb(0xC8983E));
        fill_round_rect(&rm, sx - 6, sy - 4, sx + 6, sy + 7, 2, rgb(0x8C6422));
        fill_rect(&rm, sx - 4, sy - 2, sx + 4, sy + 5, rgb(0xFFE28A));
        int rx0 = 6, rx1 = fx - 12, ry1 = floorY + 5;
        fill_rect(&rm, rx0, ry1 - 24, rx0 + 2, ry1, rgb(0x3B2316));
        fill_rect(&rm, rx1 - 2, ry1 - 24, rx1, ry1, rgb(0x3B2316));
        fill_rect(&rm, rx0, ry1 - 4, rx1, ry1 - 2, rgb(0x4E2E1A));
        static const int LX[5] = { 13, 21, 29, 17, 25 };
        static const int LY[5] = { -7, -7, -7, -14, -14 };
        for (int i = 0; i < 5; i++) {
            fill_ellipse(&rm, (float)LX[i], (float)(ry1 + LY[i]), 4.5f, 4.2f, rgb(0x4E2E1A));
            fill_ellipse(&rm, (float)LX[i], (float)(ry1 + LY[i]), 3.0f, 2.8f, rgb(i % 2 ? 0xD4B07C : 0xB88E5A));
            put(&rm, LX[i], ry1 + LY[i], rgb(0x8C5E34));
        }
    }

    /* Full chimney breast + multi-toned brick fireplace & slate hearth */
    static const uint32_t BRICK[4] = { 0xA64030, 0xB8523A, 0x8E3628, 0x9C4836 };
    fill_rect(&rm, fx + 12, 10, fx + fw - 12, fy, rgb(0x4C2E24));
    fill_rect(&rm, fx, fy, fx + fw, fy + fh, rgb(0x4C2E24));
    for (int y = 10, row = 0; y < fy + fh; y += 8, row++) {
        int x0 = y < fy ? fx + 12 : fx, x1 = y < fy ? fx + fw - 12 : fx + fw;
        int off = (row % 2) ? 8 : 0;
        int by1 = y + 6 < fy + fh ? y + 6 : fy + fh;
        for (int x = x0 - off, col = 0; x <= x1; x += 16, col++) {
            int bx0 = x < x0 ? x0 : x, bx1 = x + 14 > x1 ? x1 : x + 14;
            if (bx0 <= bx1) fill_rect(&rm, bx0, y + 1, bx1, by1, rgb(BRICK[(row * 7 + col * 3) & 3]));
        }
    }
    /* Autumn leaf wreath + berries + cranberry bow hanging on chimney breast */
    {
        int wcx = fx + fw / 2, wcy = fy - 52;
        static const uint32_t WL[4] = { 0x3B7D44, 0xD95A26, 0xE8A828, 0xB83828 };
        for (int i = 0; i < 16; i++) {
            float a = i * 2.0f * PI_F / 16.0f;
            fill_ellipse(&rm, wcx + cosf(a) * 14.0f, wcy + sinf(a) * 14.0f, 4.5f, 4.2f, rgb(WL[i % 4]));
            if (i % 4 == 1) fill_ellipse(&rm, wcx + cosf(a) * 15.0f, wcy + sinf(a) * 15.0f, 1.5f, 1.5f, rgb(0xD82C34));
        }
        fill_poly(&rm, (float[]){ wcx, wcx - 8, wcx - 5 }, (float[]){ wcy + 14, wcy + 24, wcy + 25 }, 3, rgb(0x942434));
        fill_poly(&rm, (float[]){ wcx, wcx + 8, wcx + 5 }, (float[]){ wcy + 14, wcy + 24, wcy + 25 }, 3, rgb(0x942434));
        fill_ellipse(&rm, (float)wcx, (float)(wcy + 14), 4.0f, 2.5f, rgb(0xC83A4A));
    }
    fill_round_rect(&rm, fx - 8, fy - 8, fx + fw + 8, fy + 2, 2, rgb(0x4E2B18));
    fill_rect(&rm, fx - 10, fy + fh, fx + fw + 10, fy + fh + 6, rgb(0x8A929A)); /* cool slate hearth */
    fill_rect(&rm, fx - 10, fy + fh, fx + fw + 10, fy + fh + 1, rgb(0xAAB4BE));
    s->mantleX0 = fx - 4; s->mantleX1 = fx + fw + 4; s->mantleY = fy + 2;
    /* Candle, teal mug, potted trailing plant, mini books & pumpkin on mantelpiece */
    fill_rect(&rm, fx + 10, fy - 18, fx + 15, fy - 9, rgb(0xFFF6E0));
    s->candleX = fx + 12; s->candleY = fy - 20;
    fill_round_rect(&rm, fx + 24, fy - 16, fx + 32, fy - 9, 1, rgb(0x3496A8)); /* teal mug */
    fill_rect(&rm, fx + 32, fy - 14, fx + 34, fy - 11, rgb(0x3496A8));
    fill_rect(&rm, fx + 46, fy - 16, fx + 54, fy - 9, rgb(0xC8603A)); /* terracotta pot */
    fill_ellipse(&rm, fx + 50, fy - 18, 6, 4, rgb(0x3B8E44));
    fill_ellipse(&rm, fx + 48, fy - 19, 3, 2, rgb(0x64BA58));
    fill_ellipse(&rm, fx + 55, fy - 13, 3, 4, rgb(0x3B8E44));
    fill_rect(&rm, fx + 68, fy - 20, fx + 73, fy - 9, rgb(0x2C4C88)); /* navy book */
    fill_rect(&rm, fx + 74, fy - 18, fx + 79, fy - 9, rgb(0xE0A432)); /* mustard book */
    fill_ellipse(&rm, fx + fw * 0.82f, fy - 14, 7, 6, rgb(0xEE7218));
    fill_rect(&rm, fx + (int)(fw * 0.82f) - 1, fy - 22, fx + (int)(fw * 0.82f) + 1, fy - 19, rgb(0x4A6B2A));

    /* Mid-wall walnut bookcase, colorful books, folded plaid throw & warm table lamp */
    {
        int bx0 = fx + fw + 22, bw = 82, by0 = floorY - 66, by1 = floorY + 6;
        fill_rect(&rm, bx0, by0, bx0 + bw, by1, rgb(0x4E2E1A));
        fill_rect(&rm, bx0 + 4, by0 + 4, bx0 + bw - 4, by0 + 30, rgb(0x341E12));
        fill_rect(&rm, bx0 + 4, by0 + 34, bx0 + bw - 4, by1 - 6, rgb(0x341E12));
        static const uint32_t BKC[6] = { 0x3496A8, 0xE0A432, 0xB83838, 0xF0E2C8, 0x4E8E4A, 0x2C4C88 };
        for (int b = 0; b < 8; b++) {
            int bh = 18 + (b * 5) % 6;
            fill_rect(&rm, bx0 + 7 + b * 8, by0 + 30 - bh, bx0 + 13 + b * 8, by0 + 29, rgb(BKC[b % 6]));
            fill_rect(&rm, bx0 + 8 + b * 8, by0 + 33 - bh, bx0 + 12 + b * 8, by0 + 34 - bh, rgb(0xF6EAD0));
        }
        for (int b = 0; b < 7; b++) {
            int bh = 20 + (b * 7) % 6;
            fill_rect(&rm, bx0 + 8 + b * 8, by1 - 6 - bh, bx0 + 14 + b * 8, by1 - 7, rgb(BKC[(b + 3) % 6]));
        }
        /* Cozy plaid blanket draped neatly over right edge */
        fill_round_rect(&rm, bx0 + bw - 22, by0 - 2, bx0 + bw + 3, by0 + 32, 2, rgb(0x9E2C36));
        fill_rect(&rm, bx0 + bw - 22, by0 + 8, bx0 + bw + 3, by0 + 10, rgb(0xE6B444));
        fill_rect(&rm, bx0 + bw - 22, by0 + 20, bx0 + bw + 3, by0 + 22, rgb(0xE6B444));
        fill_rect(&rm, bx0 + bw - 14, by0 - 2, bx0 + bw - 12, by0 + 32, rgb(0x2C4E78));
        /* Cozy reading lamp on top of bookcase */
        int lx = bx0 + 28;
        fill_ellipse(&rm, (float)lx, (float)(by0 - 7), 6.5f, 7.5f, rgb(0x3496A8));
        fill_rect(&rm, lx - 1, by0 - 20, lx + 1, by0 - 14, rgb(0xC8983E));
        fill_poly(&rm, (float[]){ lx - 13, lx - 8, lx + 8, lx + 13 },
                  (float[]){ (float)(by0 - 18), (float)(by0 - 34), (float)(by0 - 34), (float)(by0 - 18) }, 4, rgb(0xF6D27A));
        fill_rect(&rm, lx - 12, by0 - 20, lx + 12, by0 - 18, rgb(0xDFAA42));
        Canvas lgc = make_fire_glow(56);
        s->lampGlow = canvas_texture(r, &lgc);
        s->lampGlowRect = (SDL_Rect){ lx - 56, by0 - 26 - 56, 113, 113 };
    }

    int fbx = fx + fw / 5, fbw = fw * 3 / 5, fby = fy + fh * 2 / 5, fbh = fh * 3 / 5;
    fill_round_rect(&rm, fbx, fby, fbx + fbw, fby + fbh, 6, rgb(0x1E1210));
    s->fireTopY = fby + 2;
    s->fireRect = (SDL_Rect){ fbx + (fbw - 56) / 2, fby + fbh - 40, 56, 40 };
    Canvas gc = make_fire_glow(96);
    s->glow = canvas_texture(r, &gc);
    s->glowRect = (SDL_Rect){ s->fireRect.x + s->fireRect.w / 2 - 96, s->fireRect.y + s->fireRect.h / 2 - 96, 193, 193 };

    /* Cat food bowl (left of rug) & ball of yarn with wavy thread (right of rug) */
    {
        int fxB = (int)(W * 0.14f), fyB = (int)(H * 0.89f);
        fill_ellipse(&rm, (float)fxB, (float)(fyB + 2), 9.0f, 3.8f, rgb(0x287080));
        fill_ellipse(&rm, (float)fxB, (float)fyB, 8.5f, 3.2f, rgb(0x3894A4));
        fill_ellipse(&rm, (float)fxB, (float)(fyB - 1), 6.0f, 1.8f, rgb(0x8A5630));
        int yx = (int)(W * 0.20f), yy = (int)(H * 0.94f);
        fill_ellipse(&rm, (float)yx, (float)yy, 7.0f, 6.5f, rgb(0xE05274));
        fill_rect(&rm, yx - 4, yy - 3, yx + 3, yy - 2, rgb(0xB43454));
        fill_rect(&rm, yx - 3, yy + 2, yx + 4, yy + 3, rgb(0xB43454));
        for (int s0 = 0; s0 < 18; s0++) put(&rm, yx + 7 + s0, yy + 4 + (s0 / 4) % 2, rgb(0xE05274));
    }

    /* Pulled-out cherry-walnut ladder-back chairs with crisp gap around cream-linen honey-oak dining table */
    int tx = (int)(W * 0.65f), tw = (int)(W * 0.24f), ty = (int)(H * 0.69f);
    int seatY = ty + 32;
    for (int side = 0; side < 2; side++) {
        int cx = side ? tx + tw + 9 : tx - 30;
        int bx = side ? cx + 17 : cx;
        int fxL = side ? cx : cx + 17;
        /* Floor contact shadows + outlined cherry-walnut legs */
        fill_rect(&rm, bx - 1, H - 18, bx + 5, H - 16, rgb(0x341B0E));
        fill_rect(&rm, fxL - 1, H - 18, fxL + 4, H - 16, rgb(0x341B0E));
        fill_rect(&rm, bx - 1, ty - 21, bx + 5, H - 17, rgb(0x2E180A));
        fill_rect(&rm, bx, ty - 20, bx + 4, H - 18, rgb(0x5E321E));
        fill_rect(&rm, bx + 1, ty - 20, bx + 2, H - 18, rgb(0x78442C));
        fill_rect(&rm, fxL - 1, seatY, fxL + 4, H - 17, rgb(0x2E180A));
        fill_rect(&rm, fxL, seatY + 1, fxL + 3, H - 18, rgb(0x5E321E));
        /* Two ladder-back rails & lower seat with cranberry cushion clearly below tabletop */
        fill_rect(&rm, cx - 1, ty - 21, cx + 22, ty - 15, rgb(0x2E180A));
        fill_rect(&rm, cx, ty - 20, cx + 21, ty - 16, rgb(0x5E321E));
        fill_rect(&rm, cx, ty - 20, cx + 21, ty - 19, rgb(0x78442C));
        fill_rect(&rm, cx - 1, ty - 7, cx + 22, ty - 1, rgb(0x2E180A));
        fill_rect(&rm, cx, ty - 6, cx + 21, ty - 2, rgb(0x5E321E));
        fill_rect(&rm, cx - 1, seatY - 4, cx + 22, seatY + 5, rgb(0x2E180A));
        fill_rect(&rm, cx, seatY, cx + 21, seatY + 4, rgb(0x5E321E));
        fill_rect(&rm, cx + 1, seatY - 3, cx + 20, seatY, rgb(0x9E2A38));
        fill_rect(&rm, cx + 2, seatY - 3, cx + 19, seatY - 2, rgb(0xC23C4C));
    }
    /* Outlined honey-oak table legs with floor contact shadows */
    int legX[2] = { tx + tw / 10, tx + tw * 9 / 10 - 6 };
    for (int l = 0; l < 2; l++) {
        fill_rect(&rm, legX[l] - 1, H - 17, legX[l] + 7, H - 15, rgb(0x341B0E));
        fill_rect(&rm, legX[l] - 1, ty + 14, legX[l] + 7, H - 16, rgb(0x2E180A));
        fill_rect(&rm, legX[l], ty + 14, legX[l] + 6, H - 17, rgb(0x9C6638));
        fill_rect(&rm, legX[l] + 1, ty + 14, legX[l] + 2, H - 17, rgb(0xB87E4E));
    }
    /* Outlined cream-linen Thanksgiving tablecloth with folds & gold hem stripe */
    fill_round_rect(&rm, tx - 4, ty - 2, tx + tw + 4, ty + 16, 2, rgb(0x2E180A));
    fill_round_rect(&rm, tx - 3, ty - 1, tx + tw + 3, ty + 15, 2, rgb(0xEADCC2));
    fill_rect(&rm, tx - 3, ty - 1, tx + tw + 3, ty + 4, rgb(0xF7EFE0));
    fill_rect(&rm, tx - 3, ty + 4, tx + tw + 3, ty + 5, rgb(0xD6C4A4));
    for (int fxF = tx + 10; fxF < tx + tw - 8; fxF += 16)
        fill_rect(&rm, fxF, ty + 6, fxF + 1, ty + 14, rgb(0xD6C4A4));
    fill_rect(&rm, tx - 3, ty + 13, tx + tw + 3, ty + 14, rgb(0xD8A844));
    /* Draped forest-green & gold table runner hanging slightly below cloth hem */
    fill_rect(&rm, tx + tw / 4 - 1, ty - 1, tx + tw * 3 / 4 + 1, ty + 20, rgb(0x2E180A));
    fill_rect(&rm, tx + tw / 4, ty - 1, tx + tw * 3 / 4, ty + 19, rgb(0x2B6A4A));
    fill_rect(&rm, tx + tw / 4, ty - 1, tx + tw / 4 + 2, ty + 19, rgb(0xE8BA48));
    fill_rect(&rm, tx + tw * 3 / 4 - 2, ty - 1, tx + tw * 3 / 4, ty + 19, rgb(0xE8BA48));
    /* Teal ceramic bowl with ruby cranberry sauce (left) & golden pumpkin pie in fluted tin (right) */
    fill_ellipse(&rm, tx + 18, ty - 1, 8.0f, 4.0f, rgb(0x2E180A));
    fill_ellipse(&rm, tx + 18, ty - 2, 7.2f, 3.6f, rgb(0x2E7488));
    fill_ellipse(&rm, tx + 18, ty - 3, 6.5f, 2.6f, rgb(0x4694A8));
    fill_ellipse(&rm, tx + 18, ty - 4, 5.0f, 1.9f, rgb(0xA81E3E));
    fill_ellipse(&rm, (float)(tx + tw - 18), (float)(ty - 1), 10.8f, 4.4f, rgb(0x5A6472));
    fill_ellipse(&rm, (float)(tx + tw - 18), (float)(ty - 2), 10.0f, 4.0f, rgb(0xD8DEE8));
    fill_ellipse(&rm, (float)(tx + tw - 18), (float)(ty - 3), 8.6f, 3.0f, rgb(0xD66E1E));
    fill_ellipse(&rm, (float)(tx + tw - 18), (float)(ty - 5), 2.4f, 1.9f, rgb(0xFFF8F0));
    s->turkeyRect = (SDL_Rect){ tx + (tw - 84) / 2, ty - 45, 84, 52 };

    /* Cut window hole + frame, mullions, cranberry curtains, leaf garland, sill succulent & teacup */
    fill_rect(&rm, wx - 6, wy - 6, wx + ww + 6, wy + wh + 6, rgb(0x3B2316));
    fill_rect(&rm, wx, wy, wx + ww - 1, wy + wh - 1, 0);
    fill_rect(&rm, wx + ww / 2 - 2, wy, wx + ww / 2 + 1, wy + wh - 1, rgb(0x3B2316));
    fill_rect(&rm, wx, wy + wh / 2 - 2, wx + ww - 1, wy + wh / 2 + 1, rgb(0x3B2316));
    fill_rect(&rm, wx - 16, wy - 6, wx + 10, wy + wh + 14, rgb(0x942434));
    fill_rect(&rm, wx - 12, wy - 6, wx - 8, wy + wh + 14, rgb(0x6E1826));
    fill_rect(&rm, wx + 6, wy - 6, wx + 10, wy + wh + 14, rgb(0xB03244));
    fill_rect(&rm, wx + ww - 10, wy - 6, wx + ww + 16, wy + wh + 14, rgb(0x942434));
    fill_rect(&rm, wx + ww + 8, wy - 6, wx + ww + 12, wy + wh + 14, rgb(0x6E1826));
    fill_rect(&rm, wx + ww - 10, wy - 6, wx + ww - 6, wy + wh + 14, rgb(0xB03244));
    fill_rect(&rm, wx - 16, wy + wh * 2 / 3, wx + 10, wy + wh * 2 / 3 + 3, rgb(0xF0C048));
    fill_rect(&rm, wx + ww - 10, wy + wh * 2 / 3, wx + ww + 16, wy + wh * 2 / 3 + 3, rgb(0xF0C048));
    fill_round_rect(&rm, wx - 20, wy - 10, wx + ww + 20, wy - 6, 2, rgb(0x4E2B18));
    /* Leaf swag along curtain rod + windowsill with potted succulent & teacup */
    s->rodX0 = wx - 12; s->rodX1 = wx + ww + 12; s->rodY = wy - 4;
    static const uint32_t GC[4] = { 0x3B7D44, 0xD95A26, 0xE8A828, 0x4EA242 };
    for (int i = 0; i <= 18; i++) {
        int gx = wx - 12 + (ww + 24) * i / 18;
        int gy = wy - 5 + (int)(sinf(i * 0.9f) * 2.5f);
        fill_ellipse(&rm, (float)gx, (float)gy, 4.0f, 3.0f, rgb(GC[i % 4]));
    }
    fill_round_rect(&rm, wx - 10, wy + wh, wx + ww + 10, wy + wh + 6, 1, rgb(0x522E1C));
    fill_rect(&rm, wx - 10, wy + wh, wx + ww + 10, wy + wh + 1, rgb(0x6E4226));
    /* Potted green succulent (left sill) & cream/mustard teacup (right sill) */
    fill_rect(&rm, wx + 20, wy + wh - 7, wx + 28, wy + wh - 1, rgb(0xC8603A));
    fill_ellipse(&rm, (float)(wx + 24), (float)(wy + wh - 9), 5.0f, 3.5f, rgb(0x3B8E44));
    fill_ellipse(&rm, (float)(wx + 24), (float)(wy + wh - 11), 2.5f, 2.5f, rgb(0x64BA58));
    fill_round_rect(&rm, wx + ww - 30, wy + wh - 7, wx + ww - 22, wy + wh - 1, 1, rgb(0xF4ECE0));
    fill_rect(&rm, wx + ww - 22, wy + wh - 6, wx + ww - 20, wy + wh - 3, rgb(0xF4ECE0));
    fill_rect(&rm, wx + ww - 29, wy + wh - 4, wx + ww - 23, wy + wh - 3, rgb(0xE0A432));

    s->room = canvas_texture(r, &rm);
    for (int i = 0; i < 4; i++) { Canvas fc = make_fire(i); s->fire[i] = canvas_texture(r, &fc); }
    for (int i = 0; i < 2; i++) { Canvas cc = make_sleeping_cat(i); s->cat[i] = canvas_texture(r, &cc); }
    Canvas tc = make_turkey();
    s->turkey = canvas_texture(r, &tc);
    s->catRect = (SDL_Rect){ (int)(W * 0.39f) - 38, (int)(H * 0.85f) - 30, 76, 46 };

    for (int i = 0; i < MAX_RAIN; i++) { s->rain[i].x = frand(0, 1); s->rain[i].y = frand(0, 1); s->rain[i].l = frand(0.05f, 0.11f); }
    static const uint32_t LCOL[4] = { 0xE86A28, 0xF2A82E, 0xC83A2A, 0xD98020 };
    for (int i = 0; i < MAX_LEAVES; i++) s->leaves[i] = (Leaf){ frand(0, 1), frand(0, 1), frand(-0.08f, 0.06f), frand(0.10f, 0.22f), frand(0, 6.28f), LCOL[i % 4] };
    return s;
}

static void step(void *p, float dt) {
    CozyCat *s = p;
    s->t += dt;
    s->zzzT += dt;
    for (int i = 0; i < MAX_RAIN; i++) {
        s->rain[i].x -= 0.27f * dt;
        s->rain[i].y += 1.35f * dt;
        if (s->rain[i].y > 1.0f) { s->rain[i].y = -0.1f; s->rain[i].x = frand(0, 1.20f); }
    }
    for (int i = 0; i < MAX_LEAVES; i++) {
        Leaf *lf = &s->leaves[i];
        lf->ph += dt * 3.0f;
        lf->x += (lf->vx + sinf(lf->ph) * 0.06f) * dt;
        lf->y += lf->vy * dt;
        if (lf->y > 1.05f) { lf->y = -0.05f; lf->x = frand(0, 1); }
    }
    for (int i = 0; i < MAX_EMBERS; i++) {
        Ember *em = &s->embers[i];
        if (em->life <= 0) {
            if (frand(0, 1) < dt * 6) { em->x = frand(-0.4f, 0.4f); em->y = 0; em->vy = frand(0.3f, 0.7f); em->max = em->life = frand(0.5f, 1.0f); }
            continue;
        }
        em->life -= dt;
        em->y += em->vy * dt;
    }
    s->steamT -= dt;
    if (s->steamT <= 0) {
        s->steamT = 0.25f;
        for (int i = 0; i < MAX_STEAM; i++) {
            if (s->steam[i].life > 0) continue;
            s->steam[i] = (Steam){ frand(-12, 12), 0, frand(10, 18), frand(0, 6.28f), 0, frand(1.2f, 2.0f) };
            s->steam[i].life = s->steam[i].max;
            break;
        }
    }
    for (int i = 0; i < MAX_STEAM; i++) {
        if (s->steam[i].life <= 0) continue;
        s->steam[i].life -= dt;
        s->steam[i].y += s->steam[i].vy * dt;
    }
}

static void draw_fairy_lights(SDL_Renderer *r, int x0, int x1, int y, int n, float t, int u, float phaseOff) {
    for (int i = 0; i < n; i++) {
        int lx = x0 + (x1 - x0) * i / (n - 1);
        int ly = y + (i % 2 ? 2 : 0);
        float glow = 0.55f + 0.45f * sinf(t * 2.6f + i * 1.1f + phaseOff);
        set_color(r, (SDL_Color){ 255, 170, 64, (Uint8)(70 * glow) });
        SDL_RenderFillRect(r, &(SDL_Rect){ (lx - 2) * u, (ly - 2) * u, 5 * u, 5 * u });
        set_color(r, (SDL_Color){ 255, 230, 148, (Uint8)(235 * glow) });
        SDL_RenderFillRect(r, &(SDL_Rect){ lx * u, ly * u, 2 * u, 2 * u });
    }
}

static void draw_scene(const CozyCat *s, SDL_Renderer *r) {
    int u = s->px;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

    SDL_RenderCopy(r, s->outside, NULL, &(SDL_Rect){ s->winRect.x * u, s->winRect.y * u, s->winRect.w * u, s->winRect.h * u });
    for (int i = 0; i < MAX_LEAVES; i++) {
        int lx = s->winRect.x + (int)(s->leaves[i].x * s->winRect.w);
        int ly = s->winRect.y + (int)(s->leaves[i].y * s->winRect.h);
        if (lx < s->winRect.x || lx + 3 > s->winRect.x + s->winRect.w ||
            ly < s->winRect.y || ly + 2 > s->winRect.y + s->winRect.h) continue;
        set_color(r, hexcol(s->leaves[i].col));
        SDL_RenderFillRect(r, &(SDL_Rect){ lx * u, ly * u, 3 * u, 2 * u });
    }
    /* Diagonal stepped pixel rain matching its fall slope (-0.27 / 1.35 = -0.20) */
    set_color(r, (SDL_Color){ 200, 226, 255, 155 });
    for (int i = 0; i < MAX_RAIN; i++) {
        int rx = s->winRect.x + (int)(s->rain[i].x * s->winRect.w);
        int ry = s->winRect.y + (int)(s->rain[i].y * s->winRect.h);
        int len = (int)(s->rain[i].l * s->winRect.h);
        for (int k = 0; k < len; k += 4) {
            int dx = -(k / 5);
            if (rx + dx >= s->winRect.x && rx + dx < s->winRect.x + s->winRect.w &&
                ry + k >= s->winRect.y && ry + k < s->winRect.y + s->winRect.h)
                SDL_RenderFillRect(r, &(SDL_Rect){ (rx + dx) * u, (ry + k) * u, u, 4 * u });
        }
    }

    SDL_RenderCopy(r, s->room, NULL, &(SDL_Rect){ 0, 0, s->W * u, s->H * u });

    /* Soft Bayer-dithered firelight, reading lamp glow, candle flicker & twinkling fairy lights */
    SDL_SetTextureAlphaMod(s->lampGlow, (Uint8)(165 + 10 * sinf(s->t * 2.1f)));
    SDL_RenderCopy(r, s->lampGlow, NULL, &(SDL_Rect){ s->lampGlowRect.x * u, s->lampGlowRect.y * u, s->lampGlowRect.w * u, s->lampGlowRect.h * u });
    SDL_SetTextureAlphaMod(s->glow, (Uint8)(210 + 40 * sinf(s->t * 7)));
    SDL_RenderCopy(r, s->glow, NULL, &(SDL_Rect){ s->glowRect.x * u, s->glowRect.y * u, s->glowRect.w * u, s->glowRect.h * u });
    int sway = (int)roundf(sinf(s->t * 9.0f));
    set_color(r, hexcol(0xFF9420));
    SDL_RenderFillRect(r, &(SDL_Rect){ (s->candleX + sway) * u, (s->candleY - 3) * u, 2 * u, 4 * u });
    set_color(r, hexcol(0xFFE66A));
    SDL_RenderFillRect(r, &(SDL_Rect){ (s->candleX + sway) * u, (s->candleY - 2) * u, 2 * u, 2 * u });
    draw_fairy_lights(r, s->mantleX0, s->mantleX1, s->mantleY, 7, s->t, u, 0.0f);
    draw_fairy_lights(r, s->rodX0, s->rodX1, s->rodY, 9, s->t, u, 1.8f);
    SDL_RenderCopy(r, s->fire[(int)(s->t * 8) % 4], NULL,
                   &(SDL_Rect){ s->fireRect.x * u, s->fireRect.y * u, s->fireRect.w * u, s->fireRect.h * u });
    for (int i = 0; i < MAX_EMBERS; i++) {
        const Ember *em = &s->embers[i];
        if (em->life <= 0) continue;
        int ex = s->fireRect.x + s->fireRect.w / 2 + (int)(em->x * s->fireRect.w);
        int ey = s->fireRect.y + s->fireRect.h - 6 - (int)(em->y * s->fireRect.h);
        if (ey < s->fireTopY) continue;
        set_color(r, (SDL_Color){ 255, 204, 68, (Uint8)(255 * (em->life / em->max)) });
        SDL_RenderFillRect(r, &(SDL_Rect){ ex * u, ey * u, 2 * u, 2 * u });
    }

    /* Sleeping cat inside cat bed & rising Zs */
    SDL_RenderCopy(r, s->cat[(int)(s->t * 0.9f) % 2], NULL,
                   &(SDL_Rect){ s->catRect.x * u, s->catRect.y * u, s->catRect.w * u, s->catRect.h * u });
    for (int i = 0; i < 3; i++) {
        float zp = fmodf(s->zzzT * 0.45f + i * 0.33f, 1.0f);
        SDL_Color zc = { 255, 244, 214, (Uint8)(255 * (1.0f - zp)) };
        draw_text_shadow(r, "Z", (s->catRect.x + 50 + (int)(zp * 14)) * u, (s->catRect.y - 2 - (int)(zp * 24)) * u, u, zc);
    }

    /* Roasted turkey + rising steam wisps */
    SDL_RenderCopy(r, s->turkey, NULL,
                   &(SDL_Rect){ s->turkeyRect.x * u, s->turkeyRect.y * u, s->turkeyRect.w * u, s->turkeyRect.h * u });
    for (int i = 0; i < MAX_STEAM; i++) {
        const Steam *st = &s->steam[i];
        if (st->life <= 0) continue;
        set_color(r, (SDL_Color){ 255, 244, 230, (Uint8)(120 * (st->life / st->max)) });
        int sx = s->turkeyRect.x + s->turkeyRect.w / 2 + (int)(st->x + sinf(st->ph + s->t * 4) * 3);
        int sy = s->turkeyRect.y + 8 - (int)st->y;
        SDL_RenderFillRect(r, &(SDL_Rect){ sx * u, sy * u, 2 * u, 2 * u });
    }
}

static void draw(void *p, SDL_Renderer *r) {
    CozyCat *s = p;
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
    SDL_RenderCopy(r, s->small, NULL, &(SDL_Rect){ 0, 0, s->w, s->h });
}

static void destroy(void *p) {
    CozyCat *s = p;
    SDL_DestroyTexture(s->room);
    SDL_DestroyTexture(s->outside);
    SDL_DestroyTexture(s->glow);
    SDL_DestroyTexture(s->lampGlow);
    SDL_DestroyTexture(s->turkey);
    if (s->small) SDL_DestroyTexture(s->small);
    for (int i = 0; i < 4; i++) SDL_DestroyTexture(s->fire[i]);
    for (int i = 0; i < 2; i++) SDL_DestroyTexture(s->cat[i]);
    free(s);
}

const Saver saver_cozycat = { "cozycat", "COZYCAT", create, step, draw, destroy, NULL, NULL, NULL, NULL, NULL, NULL };
