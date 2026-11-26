/* Horse: a sunny pixel-art pasture with rolling hills, red barn and silo, wooden fences,
   wildflowers, drifting clouds, and horses galloping across the meadow with hoof dust. */
#include "../engine.h"
#include <math.h>
#include <stdlib.h>

#define HORSE_W 54
#define HORSE_H 38
#define HORSE_FRAMES 7
#define COATS 5
#define MAX_HORSES 7
#define CLOUD_SHAPES 5
#define MAX_CLOUDS 7
#define MAX_DUST 24

static const float BAYER[4][4] = {
    { 0.0f / 16, 8.0f / 16, 2.0f / 16, 10.0f / 16 },
    { 12.0f / 16, 4.0f / 16, 14.0f / 16, 6.0f / 16 },
    { 3.0f / 16, 11.0f / 16, 1.0f / 16, 9.0f / 16 },
    { 15.0f / 16, 7.0f / 16, 13.0f / 16, 5.0f / 16 },
};

static uint32_t dither_mix(uint32_t a, uint32_t b, float t, int x, int y, float step) {
    t = t < 0 ? 0 : t > 1 ? 1 : t;
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

typedef struct { uint32_t body, shade, mane, hoof, outline; int pinto; } Coat;
static const Coat PALETTE[COATS] = {
    { 0xA8542A, 0x7A3918, 0x3A1E10, 0x281A14, 0x2E160A, 0 }, /* chestnut */
    { 0xE8BA68, 0xC49240, 0xFFF3D4, 0x3A2E24, 0x4A3214, 0 }, /* palomino */
    { 0x36343A, 0x201E24, 0x16141A, 0x101014, 0x0A0A0E, 0 }, /* black */
    { 0xEDEAF0, 0xC8C4D0, 0xDCD8E4, 0x4A4652, 0x3A3644, 0 }, /* grey / white */
    { 0xB86634, 0x86441E, 0x261812, 0x1E1612, 0x2A140A, 1 }, /* pinto */
};

/* Pixel-art horse facing right: 0..3 gallop, 4 standing alert, 5..6 head-down grazing/eating. */
static Canvas make_horse(int coatIdx, int frame, float haze) {
    const Coat *ct = &PALETTE[coatIdx];
    Canvas c = canvas_new(HORSE_W, HORSE_H);
    int gallop = frame < 4;
    int graze = frame >= 5;
    int lift = gallop ? (frame % 2) : 0;

    /* Tail (flowing in gallop, calm & gently swishing while standing/grazing) */
    if (gallop) {
        static const float tail[6][2] = { {11, 18}, {8, 19}, {5.5f, 21}, {4, 24}, {3.5f, 27}, {4.5f, 30} };
        for (int i = 0; i < 6; i++)
            fill_ellipse(&c, tail[i][0] - (i > 2 ? lift : 0), tail[i][1] - lift, 2.4f, 2.0f, rgb(ct->mane));
    } else {
        int swish = frame == 6 ? -1 : 0;
        for (int i = 0; i < 6; i++)
            fill_ellipse(&c, 11.0f - i * 0.55f + (i > 2 ? swish : 0), 19.0f + i * 2.1f, 2.2f, 2.0f, rgb(ct->mane));
    }

    /* Four legs: far/shaded legs first (0, 2), then near/lit legs (1, 3) */
    static const int hips[4] = { 13, 16, 30, 33 };
    static const int legOrd[4] = { 0, 2, 1, 3 };
    if (gallop) {
        float ph = frame * PI_F / 2;
        for (int o = 0; o < 4; o++) {
            int i = legOrd[o];
            float a = ph + (i < 2 ? 0 : PI_F) + (i % 2) * 0.75f;
            int front = i >= 2;
            float reach = front ? 5.2f : -5.2f;
            float swing = cosf(a);
            int hx = hips[i], hy = 23 - lift;
            int kx = hx + (int)roundf(swing * 4.5f + reach * 0.35f), ky = hy + 5 - (swing > 0.3f ? 1 : 0);
            int fx = kx + (int)roundf(sinf(a) * 4.6f + swing * 3.0f), fy = 33 - lift - (sinf(a) > 0.1f ? 3 : 0);
            uint32_t col = i % 2 ? ct->body : ct->shade;
            fill_poly(&c, (float[]){ hx - 1, hx + 2, kx + 2, kx - 1 }, (float[]){ hy, hy, ky, ky }, 4, rgb(col));
            fill_poly(&c, (float[]){ kx - 1, kx + 1, fx + 1, fx - 1 }, (float[]){ ky, ky, fy, fy }, 4, rgb(col));
            fill_rect(&c, fx - 1, fy, fx + 2, fy + 1, rgb(ct->hoof));
        }
    } else {
        static const int standX[4] = { 13, 16, 30, 33 };
        for (int o = 0; o < 4; o++) {
            int i = legOrd[o];
            int hx = standX[i], hy = 23;
            int fx = hx + (graze && i == 3 ? 2 : graze && i == 2 ? -1 : 0), fy = 33;
            uint32_t col = i % 2 ? ct->body : ct->shade;
            fill_poly(&c, (float[]){ hx - 1, hx + 2, fx + 2, fx - 1 }, (float[]){ hy, hy, fy, fy }, 4, rgb(col));
            fill_rect(&c, fx - 1, fy, fx + 2, fy + 1, rgb(ct->hoof));
        }
    }

    /* Barrel body + belly shading */
    fill_ellipse(&c, 24, 20 - lift, 12.8f, 6.8f, rgb(ct->body));
    for (int x = 14; x <= 34; x++) put(&c, x, 25 - lift, rgb(ct->shade));
    if (ct->pinto) {
        fill_ellipse(&c, 19, 18 - lift, 4.0f, 3.2f, rgb(0xFFFFFF));
        fill_ellipse(&c, 28, 21 - lift, 3.2f, 2.5f, rgb(0xFFFFFF));
    }

    if (graze) {
        /* Head lowered right to the grass with subtle chewing nod */
        int nod = frame == 6 ? -1 : 0;
        fill_poly(&c, (float[]){ 31, 36, 44, 39 }, (float[]){ 17, 20, 30 + nod, 31 + nod }, 4, rgb(ct->body));
        fill_ellipse(&c, 42.5f, 30.5f + nod, 4.6f, 3.5f, rgb(ct->body));
        fill_ellipse(&c, 45.5f, 33.0f + nod, 2.6f, 2.2f, rgb(ct->shade));
        put(&c, 47, 33 + nod, rgb(ct->hoof));
        fill_poly(&c, (float[]){ 40, 44, 42 }, (float[]){ 28 + nod, 25 + nod, 29 + nod }, 3, rgb(ct->body));
        fill_rect(&c, 43, 29 + nod, 44, 30 + nod, rgb(0x18141A));
        put(&c, 43, 29 + nod, rgb(0xFFFFFF));
        for (int i = 0; i < 4; i++)
            fill_ellipse(&c, 35.5f + i * 1.8f, 18.0f + i * 2.5f + nod * 0.4f, 2.0f, 1.6f, rgb(ct->mane));
        if (frame == 5) {
            fill_rect(&c, 47, 34, 47, 36, rgb(0x4EB83A));
            fill_rect(&c, 49, 33, 49, 36, rgb(0x72DA4E));
            put(&c, 48, 35, rgb(0x4EB83A));
        }
    } else {
        /* Neck, head, muzzle, ear (upright gallop or calm standing pose) */
        fill_poly(&c, (float[]){ 31, 37, 42, 35 }, (float[]){ 18 - lift, 8 - lift, 10 - lift, 21 - lift }, 4, rgb(ct->body));
        fill_ellipse(&c, 41, 11 - lift, 5.5f, 4.0f, rgb(ct->body));
        fill_ellipse(&c, 46, 13 - lift, 3.0f, 2.5f, rgb(ct->shade));
        put(&c, 47, 12 - lift, rgb(ct->hoof));
        fill_poly(&c, (float[]){ 38, 40, 41 }, (float[]){ 8 - lift, 3 - lift, 8 - lift }, 3, rgb(ct->body));
        fill_rect(&c, 42, 10 - lift, 43, 11 - lift, rgb(0x18141A));
        put(&c, 42, 10 - lift, rgb(0xFFFFFF));
        for (int i = 0; i < 5; i++)
            fill_ellipse(&c, 37 - i * 1.3f + (gallop && (i % 2) ? (frame % 2) : 0),
                         7 + i * 2.2f - lift, 2.2f, 1.8f, rgb(ct->mane));
    }

    outline(&c, rgb(ct->outline));
    if (haze > 0) {
        const uint8_t hr = 0xCB, hg = 0xE8, hb = 0xDC;
        for (int i = 0; i < c.w * c.h; i++) {
            uint32_t p = c.px[i];
            if ((p & 255u) == 0) continue;
            uint8_t r = (uint8_t)((p >> 24) & 255u), g = (uint8_t)((p >> 16) & 255u), b = (uint8_t)((p >> 8) & 255u);
            r = (uint8_t)(r + (hr - r) * haze);
            g = (uint8_t)(g + (hg - g) * haze);
            b = (uint8_t)(b + (hb - b) * haze);
            c.px[i] = ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8) | 255u;
        }
    }
    return c;
}

static Canvas make_cloud(int w) {
    int h = w * 3 / 5;
    Canvas c = canvas_new(w, h);
    fill_ellipse(&c, w * 0.5f, h * 0.65f, w * 0.44f, h * 0.28f, rgb(0xFFFFFF));
    fill_ellipse(&c, w * 0.35f, h * 0.48f, w * 0.26f, h * 0.30f, rgb(0xFFFFFF));
    fill_ellipse(&c, w * 0.62f, h * 0.50f, w * 0.24f, h * 0.26f, rgb(0xFFFFFF));
    for (int y = (int)(h * 0.72f); y < h; y++)
        for (int x = 0; x < w; x++)
            if (get(&c, x, y)) put(&c, x, y, rgb(0xE2F0FC));
    return c;
}

static Canvas make_sun(void) {
    Canvas c = canvas_new(21, 21);
    fill_ellipse(&c, 10.5f, 10.5f, 9.5f, 9.5f, rgb(0xFFE055));
    fill_ellipse(&c, 9.5f, 9.5f, 6.5f, 6.5f, rgb(0xFFF3A8));
    return c;
}

enum { H_GALLOP, H_GRAZE, H_STAND };

typedef struct {
    int coat, foal, dir, state;
    float x, y, vx, ph, jumpT, acc, dustT, stateT;
} Horse;

typedef struct {
    float x, y, vx, vy, life, max, sc;
} Dust;

typedef struct {
    int shape, layer;
    float x, y;
} Cloud;

typedef struct {
    int w, h, u, lw, lh;
    float t;
    SDL_Texture *sky, *sun, *hill[3], *cloud[CLOUD_SHAPES], *horse[COATS][HORSE_FRAMES][2];
    int cloudW[CLOUD_SHAPES], cloudH[CLOUD_SHAPES];
    Cloud c[MAX_CLOUDS];
    Horse hs[MAX_HORSES];
    Dust dust[MAX_DUST];
} HorseSaver;

static void spawn_dust(HorseSaver *s, float x, float y, float dir, float sc) {
    for (int i = 0; i < MAX_DUST; i++) {
        if (s->dust[i].life > 0) continue;
        s->dust[i] = (Dust){ x, y, -dir * frand(10, 22) * sc, -frand(6, 14) * sc, 0, frand(0.3f, 0.55f), sc };
        s->dust[i].life = s->dust[i].max;
        return;
    }
}

static void reset_horse(HorseSaver *s, Horse *hr, int idx, int initial) {
    static const float LANE_Y[MAX_HORSES - 1] = { 0.725f, 0.760f, 0.815f, 0.855f, 0.900f, 0.945f };
    hr->coat = idx % COATS;
    hr->foal = (idx == MAX_HORSES - 1);
    /* Pair the foal on the same lane/direction as horse #4 */
    int lane = hr->foal ? 4 : idx;
    hr->dir = (lane % 2 == 0) ? 1 : -1;
    hr->y = s->h * LANE_Y[lane] + (hr->foal ? 2.0f * s->u : 0);
    float depth = (hr->y - s->h * 0.725f) / (s->h * 0.225f);
    if (depth < 0) depth = 0;
    if (depth > 1) depth = 1;
    float sc = (float)s->u * (0.52f + 0.72f * depth) * (hr->foal ? 0.68f : 1.0f);
    hr->vx = hr->dir * s->w * (0.07f + 0.09f * depth) * (hr->foal ? 0.96f : 1.0f);
    if (initial) {
        if (hr->foal) hr->x = s->hs[4].x - hr->dir * 38 * sc;
        else hr->x = ((idx + 0.5f) / (MAX_HORSES - 1)) * s->w;
    } else {
        hr->x = hr->dir > 0 ? -HORSE_W * sc : (float)s->w;
    }
    hr->ph = frand(0, 4);
    hr->jumpT = 0;
    hr->dustT = frand(0.1f, 0.3f);
    if (initial && (idx == 1 || idx == 3 || idx == 4 || hr->foal)) {
        hr->state = H_GRAZE;
        hr->stateT = hr->foal ? s->hs[4].stateT : frand(4.5f, 9.0f);
    } else {
        hr->state = H_GALLOP;
        hr->stateT = frand(2.5f, 5.5f);
    }
}

static void *create(SDL_Renderer *r, int w, int h) {
    HorseSaver *s = calloc(1, sizeof *s);
    s->w = w;
    s->h = h;
    s->u = h / 180 > 1 ? h / 180 : 1;
    int u = s->u, lw = s->lw = (w + u - 1) / u, lh = s->lh = (h + u - 1) / u;

    /* Dithered pixel-art sky */
    Canvas sky = canvas_new(lw, lh);
    for (int y = 0; y < lh; y++)
        for (int x = 0; x < lw; x++)
            put(&sky, x, y, dither_mix(0x52A8E8, 0xCBEBFC, (float)y / (lh * 0.68f), x, y, 8.0f));
    s->sky = canvas_texture(r, &sky);
    Canvas sun = make_sun();
    s->sun = canvas_texture(r, &sun);

    /* Three rolling pixel-art hills (with misty far-hill palette, red barn + silo, and fence) */
    static const float hb[3] = { 0.56f, 0.65f, 0.785f }, ha[3] = { 0.045f, 0.032f, 0.018f };
    static const uint32_t rim[3] = { 0xCBE8DC, 0x9CD88A, 0x82DA56 };
    static const uint32_t hiC[3] = { 0xADD6C6, 0x6EC068, 0x50B43A };
    static const uint32_t loC[3] = { 0x98C6B6, 0x58A852, 0x34882E };
    for (int k = 0; k < 3; k++) {
        Canvas c = canvas_new(lw, lh);
        float p1 = frand(0, 6.28f), p2 = frand(0, 6.28f);
        int *top = calloc((size_t)lw, sizeof(int));
        for (int x = 0; x < lw; x++) {
            float xn = (float)x / lh;
            top[x] = (int)(lh * (hb[k] + ha[k] * sinf(xn * 3.4f + p1) + ha[k] * 0.4f * sinf(xn * 8.2f + p2)));
            for (int y = top[x]; y < lh; y++)
                put(&c, x, y, y == top[x] ? rgb(rim[k])
                              : dither_mix(hiC[k], loC[k], (float)(y - top[x]) / (lh * 0.22f), x, y, 1.0f));
        }
        if (k == 1) {
            /* Red barn with white X-door, loft window, and silver-domed silo */
            int bx = (int)(lw * 0.18f), by = top[bx] + 3, bw = 20, bh = 13;
            /* Silo */
            fill_rect(&c, bx - 7, by - 14, bx - 2, by, rgb(0xD8D4CC));
            fill_rect(&c, bx - 7, by - 14, bx - 6, by, rgb(0xB8B4AC));
            fill_ellipse(&c, bx - 4.5f, by - 14, 2.8f, 2.5f, rgb(0xA8B0B8));
            /* Barn body + roof + white trim */
            fill_rect(&c, bx, by - bh, bx + bw, by, rgb(0xC63428));
            fill_rect(&c, bx, by - bh, bx + bw, by - bh + 1, rgb(0x9E221A));
            fill_poly(&c, (float[]){ bx - 2, bx + bw * 0.5f, bx + bw + 2 },
                      (float[]){ (float)(by - bh), (float)(by - bh - 7), (float)(by - bh) }, 3, rgb(0x782018));
            fill_rect(&c, bx + bw / 2 - 2, by - bh - 4, bx + bw / 2 + 2, by - bh - 1, rgb(0xFFF6D6));
            fill_rect(&c, bx + bw / 2 - 4, by - 7, bx + bw / 2 + 4, by, rgb(0x9A241A));
            for (int d = -4; d <= 4; d++) {
                put(&c, bx + bw / 2 + d, by - 7, rgb(0xFFFFFF));
                put(&c, bx + bw / 2 - 4, by - 3 + d / 2, rgb(0xFFFFFF));
                put(&c, bx + bw / 2 + 4, by - 3 + d / 2, rgb(0xFFFFFF));
                put(&c, bx + bw / 2 + d, by - 3 - d / 2, rgb(0xFFFFFF));
                put(&c, bx + bw / 2 + d, by - 3 + d / 2, rgb(0xFFFFFF));
            }
            /* Wooden pasture fence stopping cleanly on both sides of the barn/silo */
            int clear0 = bx - 9, clear1 = bx + bw + 3;
            for (int x = 0; x < lw; x += 10) {
                if (x + 9 >= clear0 && x <= clear1) continue;
                int fy = top[x] + 1;
                fill_rect(&c, x, fy - 4, x, fy, rgb(0xA67244));
                if (x + 10 < lw && (x + 10 < clear0 || x > clear1)) {
                    int fy2 = top[x + 10] + 1;
                    for (int xx = 1; xx < 10; xx++) {
                        int ry = fy + (fy2 - fy) * xx / 10;
                        put(&c, x + xx, ry - 3, rgb(0xBA8454));
                        put(&c, x + xx, ry - 1, rgb(0xBA8454));
                    }
                }
            }
        } else if (k == 2) {
            static const uint32_t dots[3] = { 0xFFFFFF, 0xFFE14D, 0xFF9CC4 };
            for (int i = 0; i < lw / 4; i++) {
                int x = irand(lw), y = top[x] + 2 + irand(lh - top[x] - 2);
                put(&c, x, y, rgb(dots[irand(3)]));
            }
        }
        free(top);
        s->hill[k] = canvas_texture(r, &c);
    }

    for (int i = 0; i < CLOUD_SHAPES; i++) {
        Canvas c = make_cloud(24 + i * 5);
        s->cloudW[i] = c.w;
        s->cloudH[i] = c.h;
        s->cloud[i] = canvas_texture(r, &c);
    }
    for (int i = 0; i < MAX_CLOUDS; i++) {
        s->c[i] = (Cloud){ irand(CLOUD_SHAPES), i % 2, frand(-20, (float)w), frand(0.04f, 0.28f) * h };
    }
    for (int k = 0; k < COATS; k++)
        for (int fr = 0; fr < HORSE_FRAMES; fr++) {
            Canvas hc0 = make_horse(k, fr, 0.0f);
            Canvas hc1 = make_horse(k, fr, 0.20f);
            s->horse[k][fr][0] = canvas_texture(r, &hc0);
            s->horse[k][fr][1] = canvas_texture(r, &hc1);
        }
    for (int i = 0; i < MAX_HORSES; i++) reset_horse(s, &s->hs[i], i, 1);
    return s;
}

static void step(void *p, float dt) {
    HorseSaver *s = p;
    s->t += dt;
    for (int i = 0; i < MAX_CLOUDS; i++) {
        Cloud *c = &s->c[i];
        c->x += s->w * (c->layer ? 0.014f : 0.009f) * dt;
        if (c->x > s->w + 10) c->x = -(float)s->cloudW[c->shape] * s->u;
    }
    for (int i = 0; i < MAX_HORSES; i++) {
        Horse *hr = &s->hs[i];
        if (hr->foal && hr->x > s->w * 0.05f && hr->x < s->w * 0.90f && s->hs[4].x > s->w * 0.05f && s->hs[4].x < s->w * 0.90f) {
            hr->state = s->hs[4].state;
            hr->dir = s->hs[4].dir;
            hr->vx = s->hs[4].vx * 0.96f;
        } else {
            hr->stateT -= dt;
        }
        if (hr->state == H_GALLOP) {
            hr->x += hr->vx * dt;
            hr->acc += dt * 10.0f;
            hr->ph = hr->acc;
            if (hr->jumpT <= 0 && fabsf(hr->x - s->w * 0.52f) < 12 * s->u && (i % 3 == 0)) hr->jumpT = 0.7f;
            if (hr->jumpT > 0) hr->jumpT -= dt;
            hr->dustT -= dt;
            if (hr->dustT <= 0 && hr->jumpT <= 0) {
                hr->dustT = frand(0.14f, 0.28f);
                float depth = (hr->y - s->h * 0.725f) / (s->h * 0.225f);
                if (depth < 0) depth = 0;
                if (depth > 1) depth = 1;
                float sc = (float)s->u * (0.52f + 0.72f * depth) * (hr->foal ? 0.68f : 1.0f);
                float hoofX = hr->dir > 0 ? hr->x + 16 * sc : hr->x + 34 * sc;
                spawn_dust(s, hoofX, hr->y, (float)hr->dir, sc);
            }
            if (!hr->foal && hr->stateT <= 0 && hr->jumpT <= 0 && hr->x > s->w * 0.12f && hr->x < s->w * 0.82f) {
                hr->state = H_GRAZE;
                hr->stateT = frand(4.5f, 9.5f);
                hr->acc = 0;
            }
        } else if (hr->state == H_GRAZE) {
            hr->jumpT = 0;
            hr->acc += dt * 2.2f;
            hr->ph = hr->acc;
            if (!hr->foal && hr->stateT <= 0) {
                hr->state = H_STAND;
                hr->stateT = frand(1.2f, 2.6f);
            }
        } else {
            if (!hr->foal && hr->stateT <= 0) {
                if (irand(10) < 4) {
                    hr->state = H_GRAZE;
                    hr->stateT = frand(4.0f, 8.0f);
                } else {
                    hr->state = H_GALLOP;
                    hr->stateT = frand(3.0f, 6.5f);
                    if (hr->x > s->w * 0.2f && hr->x < s->w * 0.8f && irand(2)) {
                        hr->dir = -hr->dir;
                        hr->vx = -hr->vx;
                    }
                }
            }
        }
        float mw = HORSE_W * s->u * 1.3f;
        if ((hr->vx > 0 && hr->x > s->w + mw) || (hr->vx < 0 && hr->x < -mw)) reset_horse(s, hr, i, 0);
    }
    for (int i = 0; i < MAX_DUST; i++) {
        Dust *d = &s->dust[i];
        if (d->life <= 0) continue;
        d->life -= dt;
        d->x += d->vx * dt;
        d->y += d->vy * dt;
    }
}

static void draw(void *p, SDL_Renderer *r) {
    HorseSaver *s = p;
    int u = s->u;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_Rect full = { 0, 0, s->lw * u, s->lh * u };
    SDL_RenderCopy(r, s->sky, NULL, &full);
    SDL_Rect sd = { (int)(s->w * 0.84f), (int)(s->h * 0.08f), 21 * u, 21 * u };
    SDL_RenderCopy(r, s->sun, NULL, &sd);
    for (int layer = 0; layer < 2; layer++)
        for (int i = 0; i < MAX_CLOUDS; i++) {
            const Cloud *c = &s->c[i];
            if (c->layer != layer) continue;
            SDL_Rect d = { (int)c->x, (int)c->y, s->cloudW[c->shape] * u, s->cloudH[c->shape] * u };
            SDL_RenderCopy(r, s->cloud[c->shape], NULL, &d);
        }
    SDL_RenderCopy(r, s->hill[0], NULL, &full);
    SDL_RenderCopy(r, s->hill[1], NULL, &full);

    int ord[MAX_HORSES];
    for (int i = 0; i < MAX_HORSES; i++) ord[i] = i;
    for (int i = 0; i < MAX_HORSES; i++)
        for (int j = i + 1; j < MAX_HORSES; j++)
            if (s->hs[ord[j]].y < s->hs[ord[i]].y) { int t = ord[i]; ord[i] = ord[j]; ord[j] = t; }

    float splitY = s->h * 0.79f;
    for (int tier = 0; tier < 2; tier++) {
        for (int i = 0; i < MAX_DUST; i++) {
            const Dust *d = &s->dust[i];
            if (d->life <= 0 || (tier == 0 ? d->y >= splitY : d->y < splitY)) continue;
            set_color(r, (SDL_Color){ 210, 232, 185, (Uint8)(140 * (d->life / d->max)) });
            int sz = (int)(2 * d->sc);
            if (sz < u) sz = u;
            SDL_RenderFillRect(r, &(SDL_Rect){ (int)d->x, (int)d->y - sz, sz, sz });
        }
        for (int i = 0; i < MAX_HORSES; i++) {
            const Horse *hr = &s->hs[ord[i]];
            if (tier == 0 ? hr->y >= splitY : hr->y < splitY) continue;
            float depth = (hr->y - s->h * 0.725f) / (s->h * 0.225f);
            if (depth < 0) depth = 0;
            if (depth > 1) depth = 1;
            float sc = (float)u * (0.52f + 0.72f * depth) * (hr->foal ? 0.68f : 1.0f);
            float jump = hr->jumpT > 0 ? sinf((hr->jumpT / 0.7f) * PI_F) * 14.0f * sc : 0;
            int fr = hr->state == H_GALLOP ? ((int)hr->ph % 4) : hr->state == H_STAND ? 4 : (5 + ((int)hr->ph % 2));
            int shW = (int)(26 * sc * (jump > 0 ? 0.75f : 1.0f));
            int shH = (int)(3 * sc);
            if (shH < u) shH = u;
            set_color(r, (SDL_Color){ 18, 52, 18, (Uint8)(40 + 45 * depth) });
            SDL_RenderFillRect(r, &(SDL_Rect){ (int)(hr->x + 11 * sc), (int)(hr->y - shH / 2), shW, shH });
            SDL_Texture *tex = s->horse[hr->coat][fr][tier == 0 ? 1 : 0];
            SDL_Rect d = { (int)hr->x, (int)(hr->y - 34.0f * sc - jump), (int)(HORSE_W * sc), (int)(HORSE_H * sc) };
            SDL_RenderCopyEx(r, tex, NULL, &d, 0, NULL, hr->dir < 0 ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
        }
        if (tier == 0) SDL_RenderCopy(r, s->hill[2], NULL, &full);
    }
}

static void destroy(void *p) {
    HorseSaver *s = p;
    SDL_DestroyTexture(s->sky);
    SDL_DestroyTexture(s->sun);
    for (int i = 0; i < 3; i++) SDL_DestroyTexture(s->hill[i]);
    for (int i = 0; i < CLOUD_SHAPES; i++) SDL_DestroyTexture(s->cloud[i]);
    for (int k = 0; k < COATS; k++)
        for (int fr = 0; fr < HORSE_FRAMES; fr++) {
            SDL_DestroyTexture(s->horse[k][fr][0]);
            SDL_DestroyTexture(s->horse[k][fr][1]);
        }
    free(s);
}

const Saver saver_horse = { "horse", "HORSE", create, step, draw, destroy, NULL, NULL, NULL, NULL, NULL, NULL };
