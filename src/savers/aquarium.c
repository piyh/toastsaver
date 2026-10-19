/* Aquarium: pixel-art fish, a passing shark or sea turtle, drifting jellyfish, a crab on
   the sand, swaying seaweed, coral and a bubbling treasure chest. */
#include "../engine.h"
#include <math.h>
#include <stdlib.h>

enum { CLOWN, TANG, PUFFER, ANGEL, SMALL_SPECIES, SHARK = SMALL_SPECIES, TURTLE, SPECIES };
#define MAX_FISH 16
#define MAX_BUBBLES 140
#define WEEDS 7
#define PEBBLES 70
#define JELLIES 2
#define SNOW 40

static uint32_t rgba(uint32_t hex, uint32_t a) { return (hex << 8) | a; }

/* Paints `col` over pixels of `under` inside a rectangle (used for stripes and bellies). */
static void recolor(Canvas *c, int x0, int y0, int x1, int y1, uint32_t under, uint32_t col) {
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            if (get(c, x, y) == under) put(c, x, y, col);
}

static uint32_t tint(uint32_t p, float k) {   /* k < 1 darkens, > 1 lightens toward white */
    int ch[3];
    for (int i = 0; i < 3; i++) {
        float v = (float)((p >> (24 - 8 * i)) & 0xFF);
        v = k < 1 ? v * k : v + (255 - v) * (k - 1);
        ch[i] = v > 255 ? 255 : (int)v;
    }
    return (uint32_t)ch[0] << 24 | (uint32_t)ch[1] << 16 | (uint32_t)ch[2] << 8 | (p & 0xFF);
}

/* Rounds a sprite: per column, the top rows go a shade darker and the bottom rows lighter. */
static void shade(Canvas *c) {
    for (int x = 0; x < c->w; x++) {
        int top = -1, bot = -1;
        for (int y = 0; y < c->h; y++)
            if (get(c, x, y)) { if (top < 0) top = y; bot = y; }
        if (top < 0 || bot - top < 4) continue;
        int band = (bot - top + 1) / 4;
        for (int y = top; y <= bot; y++) {
            uint32_t p = get(c, x, y);
            if (!p) continue;
            if (y < top + band) put(c, x, y, tint(p, 0.82f));
            else if (y > bot - band) put(c, x, y, tint(p, 1.18f));
        }
    }
}

/* A round-ended stroke, for coral branches. */
static void stroke(Canvas *c, float x0, float y0, float x1, float y1, float r, uint32_t col) {
    for (int i = 0; i <= 12; i++) {
        float t = i / 12.0f;
        fill_ellipse(c, x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, r, r, col);
    }
}

static void eye(Canvas *c, int x, int y) {
    put(c, x, y, rgb(0x111111)); put(c, x + 1, y, rgb(0x111111));
    put(c, x, y + 1, rgb(0x111111)); put(c, x + 1, y + 1, rgb(0x111111));
    put(c, x, y, rgb(0xFFFFFF));
}

static void tail(Canvas *c, float x0, float cy, int frame, float spread, uint32_t col) {
    float wig = frame ? -1.5f : 1.5f;
    float xs[3] = { x0, 0, 0 }, ys[3] = { cy, cy - spread + wig, cy + spread + wig };
    fill_poly(c, xs, ys, 3, col);
}

static Canvas make_fish(int species, int frame) {
    Canvas c;
    if (species == CLOWN) {
        c = canvas_new(22, 13);
        uint32_t o = rgb(0xFF8A1F);
        tail(&c, 4.5f, 6.5f, frame, 4.5f, rgb(0xF06A00));
        fill_ellipse(&c, 12, 6.5f, 8.5f, 5, o);
        fill_poly(&c, (float[]){ 9, 12, 14 }, (float[]){ 2, 0, 2.5f }, 3, rgb(0xF06A00));
        recolor(&c, 8, 0, 9, 12, o, rgb(0xFFFFFF));
        recolor(&c, 14, 0, 15, 12, o, rgb(0xFFFFFF));
        recolor(&c, 7, 0, 7, 12, o, rgb(0x2A1200));
        recolor(&c, 16, 0, 16, 12, o, rgb(0x2A1200));
        shade(&c);
        eye(&c, 17, 4);
        outline(&c, rgb(0x3A1A00));
    } else if (species == TANG) {
        c = canvas_new(24, 14);
        uint32_t b = rgb(0x2563EB);
        tail(&c, 4.5f, 7, frame, 5, rgb(0xFFD23F));
        fill_ellipse(&c, 13, 7, 9, 5.5f, b);
        fill_ellipse(&c, 13, 6, 6, 2.4f, rgb(0x0B1A40));
        fill_ellipse(&c, 14, 6.4f, 4, 1.2f, b);
        fill_poly(&c, (float[]){ 10, 15, 13 }, (float[]){ 12, 12, 14 }, 3, rgb(0xFFD23F));
        shade(&c);
        eye(&c, 18, 4);
        outline(&c, rgb(0x0A1430));
    } else if (species == PUFFER) {
        c = canvas_new(19, 16);
        uint32_t g = rgb(0xC8E05A);
        tail(&c, 3.5f, 8, frame, 3.5f, rgb(0xA7C23E));
        fill_ellipse(&c, 10, 8, 7.5f, 6.8f, g);
        recolor(&c, 0, 9, 18, 15, g, rgb(0xF2F7C8));
        static const int spots[][2] = { {7,4}, {11,3}, {6,7}, {10,6}, {13,9}, {8,9} };
        for (int i = 0; i < 6; i++)
            if (get(&c, spots[i][0], spots[i][1]) == g) put(&c, spots[i][0], spots[i][1], rgb(0x6E8A23));
        fill_poly(&c, (float[]){ 8, 11, 9 }, (float[]){ 10, 10, 13 }, 3, rgb(0xA7C23E));
        shade(&c);
        fill_rect(&c, 13, 5, 15, 7, rgb(0xFFFFFF));
        fill_rect(&c, 14, 6, 15, 7, rgb(0x111111));
        put(&c, 17, 10, rgb(0x6E3F18));
        outline(&c, rgb(0x2F3A0E));
    } else if (species == ANGEL) {               /* queen angelfish: tall, with long fins */
        c = canvas_new(21, 24);
        uint32_t y = rgb(0xFFD447), fin = rgb(0x2F7BE6);
        tail(&c, 5.5f, 12, frame, 3.5f, rgb(0xFFB020));
        fill_poly(&c, (float[]){ 15, 2, 6 }, (float[]){ 5, 0, 9 }, 3, fin);
        fill_poly(&c, (float[]){ 15, 2, 6 }, (float[]){ 19, 24, 15 }, 3, fin);
        fill_ellipse(&c, 12.5f, 12, 7, 7.5f, y);
        recolor(&c, 9, 0, 9, 23, y, rgb(0x3A7BD5));
        recolor(&c, 14, 0, 14, 23, y, rgb(0x3A7BD5));
        shade(&c);
        eye(&c, 16, 9);
        put(&c, 19, 13, rgb(0x6A3A00));
        outline(&c, rgb(0x3A2400));
    } else if (species == SHARK) {
        c = canvas_new(60, 26);
        uint32_t g = rgb(0x7D8C9C), fin = rgb(0x6A7988);
        float w = frame ? 1.5f : -1.5f;
        fill_poly(&c, (float[]){ 13, 2, 7 }, (float[]){ 11, 0 + w, 12 }, 3, fin);   /* tail lobes */
        fill_poly(&c, (float[]){ 13, 4, 8 }, (float[]){ 14, 23 + w, 13 }, 3, fin);
        fill_poly(&c, (float[]){ 9, 22, 22, 9 }, (float[]){ 11, 8, 18, 15 }, 4, g);  /* tail stalk */
        fill_ellipse(&c, 33, 13, 21, 7, g);
        fill_poly(&c, (float[]){ 46, 59, 57, 46 }, (float[]){ 7.5f, 12, 16, 18.5f }, 4, g);  /* snout */
        fill_poly(&c, (float[]){ 37, 29, 26 }, (float[]){ 7, 0, 7 }, 3, fin);        /* dorsal fin */
        fill_poly(&c, (float[]){ 18, 15, 14 }, (float[]){ 9, 6, 10 }, 3, fin);
        fill_poly(&c, (float[]){ 42, 31, 36 }, (float[]){ 17, 25, 18 }, 3, fin);     /* pectoral */
        recolor(&c, 10, 15, 59, 25, g, rgb(0xE4EAEF));                               /* white belly */
        shade(&c);
        for (int x = 41; x <= 45; x += 2) recolor(&c, x, 11, x, 15, g, rgb(0x55626F));
        fill_rect(&c, 49, 10, 50, 11, rgb(0x111111));
        put(&c, 49, 10, rgb(0xFFFFFF));
        for (int x = 51; x <= 55; x++) put(&c, x, 16 - (x == 51 || x == 55), rgb(0x3C4650));
        outline(&c, rgb(0x1E2A36));
    } else {                                     /* sea turtle */
        c = canvas_new(38, 24);
        uint32_t skin = rgb(0x9CC069);
        if (frame) fill_poly(&c, (float[]){ 25, 20, 11 }, (float[]){ 11, 8, 1 }, 3, skin);
        else fill_poly(&c, (float[]){ 25, 20, 12 }, (float[]){ 14, 16, 23 }, 3, skin);
        fill_ellipse(&c, 5, 15, 4, 2.2f, skin);
        fill_ellipse(&c, 31, 11, 5.5f, 3.6f, skin);
        fill_ellipse(&c, 17, 11, 12, 7.5f, rgb(0x6B8E3A));
        static const float plates[][2] = { {12,8}, {18,7}, {24,9}, {14,13}, {21,13} };
        for (int i = 0; i < 5; i++) fill_ellipse(&c, plates[i][0], plates[i][1], 2.6f, 2.0f, rgb(0x4E6B27));
        recolor(&c, 4, 15, 30, 19, rgb(0x6B8E3A), rgb(0xD8C77A));                    /* shell rim */
        shade(&c);
        eye(&c, 32, 9);
        put(&c, 35, 13, rgb(0x3D5220));
        outline(&c, rgb(0x223313));
    }
    return c;
}

static Canvas make_jelly(int frame) {
    Canvas c = canvas_new(15, 24);
    uint32_t bell = rgba(0xF6A6DA, 0xD8), rim = rgba(0xC2589E, 0xE0);
    float rx = frame ? 5.5f : 7, ry = frame ? 6.5f : 5, by = frame ? 7 : 5;
    fill_ellipse(&c, 7.5f, by, rx, ry, bell);
    for (int y = (int)by + 1; y < 24; y++)
        for (int x = 0; x < 15; x++) put(&c, x, y, 0);
    fill_ellipse(&c, 7.5f, by - ry * 0.4f, rx * 0.45f, ry * 0.3f, rgba(0xFFE0F4, 0xE8));
    outline(&c, rim);
    int top = (int)by + 2;
    for (int k = 0; k < 4; k++) {
        float x0 = frame ? 4.5f + k * 2 : 2.5f + k * 3.3f;
        for (int y = top; y < 24 - (k % 2) * 3; y++)
            put(&c, (int)(x0 + sinf(y * 0.6f + k + frame * 2) * (frame ? 0.6f : 1.2f)), y, rgba(0xF3B6E4, 0xA0));
    }
    return c;
}

static Canvas make_crab(int frame) {
    Canvas c = canvas_new(22, 13);
    uint32_t red = rgb(0xE5482E), dark = rgb(0x8B1E10);
    for (int i = 0; i < 3; i++) {                       /* legs, scuttling between two poses */
        int off = (i + frame) % 2;
        put(&c, 6 - i, 10 + off, dark); put(&c, 5 - i, 11 + off, dark);
        put(&c, 15 + i, 10 + off, dark); put(&c, 16 + i, 11 + off, dark);
    }
    fill_ellipse(&c, 11, 8, 7, 3.6f, red);
    fill_ellipse(&c, 2.6f, 4.5f, 2.4f, 2.2f, red);       /* claws */
    fill_ellipse(&c, 19.4f, 4.5f, 2.4f, 2.2f, red);
    put(&c, 2, 2, 0); put(&c, 19, 2, 0);
    put(&c, 4, 6, red); put(&c, 5, 7, red); put(&c, 17, 6, red); put(&c, 16, 7, red);
    put(&c, 8, 3, dark); put(&c, 8, 4, dark); put(&c, 13, 3, dark); put(&c, 13, 4, dark);
    shade(&c);
    put(&c, 8, 2, rgb(0x111111)); put(&c, 13, 2, rgb(0x111111));
    outline(&c, rgb(0x4A0E06));
    return c;
}

static Canvas make_starfish(void) {
    Canvas c = canvas_new(13, 13);
    float xs[10], ys[10];
    for (int i = 0; i < 10; i++) {
        float a = -PI_F / 2 + i * PI_F / 5, r = i % 2 ? 2.6f : 6.2f;
        xs[i] = 6.5f + cosf(a) * r;
        ys[i] = 7 + sinf(a) * r;
    }
    fill_poly(&c, xs, ys, 10, rgb(0xFF8A3D));
    shade(&c);
    static const int dots[][2] = { {6,3}, {3,6}, {9,6}, {5,9}, {8,9}, {6,6} };
    for (int i = 0; i < 6; i++) put(&c, dots[i][0], dots[i][1], rgb(0xFFD6A8));
    outline(&c, rgb(0x7A2E0A));
    return c;
}

static Canvas make_coral(void) {
    Canvas c = canvas_new(34, 30);
    uint32_t p = rgb(0xFF6F91);
    static const float br[][4] = {
        {17,29,17,18}, {17,21,9,11}, {9,11,6,3}, {9,11,13,4}, {17,18,25,9},
        {25,9,23,2}, {25,9,30,5}, {17,18,17,6}, {12,24,4,17},
    };
    for (int i = 0; i < 9; i++) stroke(&c, br[i][0], br[i][1], br[i][2], br[i][3], 1.7f, p);
    shade(&c);
    static const int tips[][2] = { {6,3}, {13,4}, {23,2}, {30,5}, {17,6}, {4,17} };
    for (int i = 0; i < 6; i++) put(&c, tips[i][0], tips[i][1] - 1, rgb(0xFFC2D1));
    outline(&c, rgb(0x7A1F3D));
    return c;
}

static Canvas make_brain_coral(void) {
    Canvas c = canvas_new(30, 15);
    uint32_t o = rgb(0xE9A24E);
    fill_ellipse(&c, 15, 15, 14, 14, o);
    for (int k = 0; k < 2; k++)
        for (int x = 0; x < 30; x++) {
            int y = 6 + k * 4 + (int)lroundf(sinf(x * 0.5f + k * 2.0f) * 1.2f);
            if (get(&c, x, y) == o) put(&c, x, y, rgb(0xC9853A));
        }
    shade(&c);
    outline(&c, rgb(0x5C3510));
    return c;
}

static Canvas make_bubble(void) {
    Canvas c = canvas_new(7, 7);
    fill_ellipse(&c, 3.5f, 3.5f, 3.5f, 3.5f, rgb(0xBDEFFF));
    fill_ellipse(&c, 3.5f, 3.5f, 2.4f, 2.4f, 0);
    put(&c, 2, 2, rgb(0xFFFFFF));
    return c;
}

static Canvas make_chest(void) {
    Canvas c = canvas_new(26, 18);
    fill_rect(&c, 2, 8, 23, 17, rgb(0x8B5A2B));
    fill_round_rect(&c, 2, 2, 23, 9, 3, rgb(0xA0692F));
    fill_rect(&c, 2, 8, 23, 8, rgb(0x5E3A17));
    fill_rect(&c, 6, 2, 7, 17, rgb(0xFFC93C));
    fill_rect(&c, 18, 2, 19, 17, rgb(0xFFC93C));
    fill_rect(&c, 12, 7, 13, 10, rgb(0xFFE07A));
    put(&c, 9, 5, rgb(0xFFE07A)); put(&c, 15, 13, rgb(0xFFE07A));
    outline(&c, rgb(0x2E1A08));
    return c;
}

typedef struct { int species, frame, far; float x, baseY, vx, bobAmp, bobF, phase, flick, scare, dodge; } Fish;
typedef struct { float x, y, vy, phase; int alive; } Bubble;
typedef struct { float x, y, vx, phase; } Jelly;

typedef struct {
    int w, h, sc, farSc, sandY, nfish, nfar;
    float t, chestTimer, sandTimer;
    SDL_Texture *fish[SPECIES][2], *jelly[2], *crab[2], *bubble, *chest, *star, *coral, *brain;
    int fw[SPECIES], fh[SPECIES];
    Fish f[MAX_FISH];
    Fish vis[2];                       /* the shark and the turtle, each now and then */
    int visOn[2];
    float visWait[2];
    Jelly j[JELLIES];
    float crabX, crabVx, crabWait, crabStep;
    Bubble b[MAX_BUBBLES];
    float weedX[WEEDS], weedH[WEEDS], weedP[WEEDS];
    int pebble[PEBBLES][3];
    float snow[SNOW][3];
    SDL_Rect chestRect, starRect[2], coralRect, brainRect;
} Aquarium;

static int fish_scale(const Aquarium *a, const Fish *f) { return f->far ? a->farSc : a->sc; }
static float fish_y(const Aquarium *a, const Fish *f) {
    return f->baseY + sinf(a->t * f->bobF * 6.28f + f->phase) * f->bobAmp;
}

static void spawn(Aquarium *a, Fish *f, int species, float speed, int anywhere) {
    f->species = species;
    int right = irand(2);
    f->vx = right ? speed : -speed;
    float fw = (float)a->fw[species] * fish_scale(a, f);
    f->x = anywhere ? frand(0, (float)a->w) : (right ? -fw : (float)a->w);
    f->baseY = frand(0.08f, 0.85f) * a->sandY;
    f->bobAmp = frand(0.01f, 0.03f) * a->h;
    f->bobF = frand(0.3f, 0.8f);
    f->phase = frand(0, 6.28f);
    f->flick = frand(0, 1);
    f->scare = 0;
}

static void spawn_fish(Aquarium *a, Fish *f, int anywhere) {
    spawn(a, f, irand(SMALL_SPECIES), frand(0.06f, 0.16f) * a->w * (f->far ? 0.6f : 1), anywhere);
    if (f->far) f->baseY = frand(0.1f, 0.75f) * a->sandY;
}

static void spawn_visitor(Aquarium *a, int k) {
    Fish *v = &a->vis[k];
    spawn(a, v, SHARK + k, (k ? 0.045f : 0.075f) * a->w, 0);
    v->baseY = frand(0.15f, 0.55f) * a->sandY;
    v->bobAmp = 0.012f * a->h;
    v->bobF = 0.2f;
    a->visOn[k] = 1;
}

static void spawn_jelly(Aquarium *a, Jelly *j, int anywhere) {
    int right = irand(2);
    j->vx = (right ? 1 : -1) * frand(0.008f, 0.018f) * a->w;
    j->x = anywhere ? frand(0, (float)a->w) : (right ? -15.0f * a->sc : (float)a->w);
    j->y = frand(0.1f, 0.5f) * a->sandY;
    j->phase = frand(0, 6.28f);
}

static void add_bubble(Aquarium *a, float x, float y) {
    for (int i = 0; i < MAX_BUBBLES; i++)
        if (!a->b[i].alive) {
            a->b[i] = (Bubble){ x, y, -frand(0.10f, 0.2f) * a->h, frand(0, 6.28f), 1 };
            return;
        }
}

static void *create(SDL_Renderer *r, int w, int h) {
    Aquarium *a = calloc(1, sizeof *a);
    a->w = w;
    a->h = h;
    a->sc = (h + 110) / 220 > 1 ? (h + 110) / 220 : 1;
    a->farSc = (a->sc * 2 + 1) / 3 > 1 ? (a->sc * 2 + 1) / 3 : 1;
    a->sandY = h - h / 9;
    for (int s = 0; s < SPECIES; s++)
        for (int fr = 0; fr < 2; fr++) {
            Canvas c = make_fish(s, fr);
            a->fw[s] = c.w;
            a->fh[s] = c.h;
            a->fish[s][fr] = canvas_texture(r, &c);
        }
    for (int fr = 0; fr < 2; fr++) {
        Canvas jc = make_jelly(fr), cc = make_crab(fr);
        a->jelly[fr] = canvas_texture(r, &jc);
        a->crab[fr] = canvas_texture(r, &cc);
    }
    Canvas bc = make_bubble(), cc = make_chest(), sc = make_starfish(), co = make_coral(), br = make_brain_coral();
    a->bubble = canvas_texture(r, &bc);
    a->chest = canvas_texture(r, &cc);
    a->star = canvas_texture(r, &sc);
    a->coral = canvas_texture(r, &co);
    a->brain = canvas_texture(r, &br);
    int s = a->sc;
    a->chestRect = (SDL_Rect){ (int)(w * 0.7f), a->sandY - 14 * s, 26 * s, 18 * s };
    a->coralRect = (SDL_Rect){ (int)(w * 0.12f), a->sandY - 27 * s, 34 * s, 30 * s };
    a->brainRect = (SDL_Rect){ (int)(w * 0.45f), a->sandY - 12 * s, 30 * s, 15 * s };
    a->starRect[0] = (SDL_Rect){ (int)(w * 0.3f), a->sandY + (h - a->sandY) / 3, 13 * s, 13 * s };
    a->starRect[1] = (SDL_Rect){ (int)(w * 0.88f), a->sandY + (h - a->sandY) / 2, 13 * s, 13 * s };
    a->nfish = 6 + w * h / 300000;
    if (a->nfish > MAX_FISH - 4) a->nfish = MAX_FISH - 4;
    a->nfar = a->farSc < a->sc ? 4 : 0;
    a->nfish += a->nfar;
    for (int i = 0; i < a->nfish; i++) {
        a->f[i].far = i < a->nfar;
        spawn_fish(a, &a->f[i], 1);
    }
    a->visWait[0] = frand(2, 6);      /* the shark shows up soon: it's the favourite */
    a->visWait[1] = frand(10, 20);
    for (int i = 0; i < JELLIES; i++) spawn_jelly(a, &a->j[i], 1);
    a->crabX = frand(0.2f, 0.8f) * w;
    a->crabVx = 0.03f * w;
    for (int i = 0; i < WEEDS; i++) {
        a->weedX[i] = frand(0.03f, 0.97f) * w;
        a->weedH[i] = frand(0.15f, 0.4f) * h;
        a->weedP[i] = frand(0, 6.28f);
    }
    for (int i = 0; i < PEBBLES; i++) {
        a->pebble[i][0] = irand(w);
        a->pebble[i][1] = a->sandY + irand(h - a->sandY);
        a->pebble[i][2] = irand(3);
    }
    for (int i = 0; i < SNOW; i++) {
        a->snow[i][0] = frand(0, (float)w);
        a->snow[i][1] = frand(0, (float)a->sandY);
        a->snow[i][2] = frand(0, 6.28f);
    }
    return a;
}

/* Small fish near the shark turn and dart away from it for a moment. */
static void flee(Aquarium *a, Fish *f, float dt) {
    if (f->scare > 0) {
        f->scare -= dt;
        f->baseY += f->dodge * dt * f->scare;
        float lo = 0.05f * a->sandY, hi = 0.88f * a->sandY;
        f->baseY = f->baseY < lo ? lo : (f->baseY > hi ? hi : f->baseY);
        return;
    }
    if (f->far || !a->visOn[0]) return;
    const Fish *s = &a->vis[0];
    float sx = s->x + a->fw[SHARK] * a->sc * 0.5f, sy = fish_y(a, s) + a->fh[SHARK] * a->sc * 0.5f;
    float dx = f->x + a->fw[f->species] * a->sc * 0.5f - sx, dy = fish_y(a, f) - sy;
    if (fabsf(dx) < 0.2f * a->w && fabsf(dy) < 0.16f * a->h) {
        f->scare = 1.5f;
        f->vx = dx < 0 ? -fabsf(f->vx) : fabsf(f->vx);
        f->dodge = (dy < 0 ? -1 : 1) * 0.1f * a->h;
    }
}

static void step(void *p, float dt) {
    Aquarium *a = p;
    a->t += dt;
    for (int i = 0; i < a->nfish; i++) {
        Fish *f = &a->f[i];
        flee(a, f, dt);
        float boost = f->scare > 0 ? 1 + 1.6f * f->scare : 1;
        f->x += f->vx * dt * boost;
        f->flick += dt * 4 * boost;
        f->frame = (int)f->flick % 2;
        float fw = (float)a->fw[f->species] * fish_scale(a, f);
        if (f->x < -fw - 5 || f->x > a->w + 5) spawn_fish(a, f, 0);
    }
    for (int k = 0; k < 2; k++) {
        Fish *v = &a->vis[k];
        if (!a->visOn[k]) {
            if ((a->visWait[k] -= dt) <= 0) spawn_visitor(a, k);
            continue;
        }
        v->x += v->vx * dt;
        v->flick += dt * (k ? 1.2f : 2);
        v->frame = (int)v->flick % 2;
        float fw = (float)a->fw[v->species] * a->sc;
        if (v->x < -fw - 5 || v->x > a->w + 5) {
            a->visOn[k] = 0;
            a->visWait[k] = k ? frand(20, 40) : frand(8, 20);
        }
    }
    for (int i = 0; i < JELLIES; i++) {                 /* pulse up, then sink slowly */
        Jelly *j = &a->j[i];
        j->phase += dt * 1.7f;
        float push = sinf(j->phase);
        j->y += (0.018f - (push > 0 ? push : 0) * 0.06f) * a->h * dt;
        if (j->y < 0.06f * a->sandY) j->y += 0.04f * a->h * dt;
        if (j->y > 0.6f * a->sandY) j->y -= 0.04f * a->h * dt;
        j->x += j->vx * dt;
        if (j->x < -20.0f * a->sc || j->x > a->w + 5) spawn_jelly(a, j, 0);
    }
    if (a->crabWait > 0) a->crabWait -= dt;             /* the crab scuttles, pauses, turns */
    else {
        a->crabX += a->crabVx * dt;
        a->crabStep += dt * 8;
        float cw = 22.0f * a->sc;
        if (a->crabX < 0) a->crabVx = fabsf(a->crabVx);
        if (a->crabX > a->w - cw) a->crabVx = -fabsf(a->crabVx);
        if (frand(0, 1) < dt * 0.25f) {
            a->crabWait = frand(1, 3);
            if (irand(2)) a->crabVx = -a->crabVx;
        }
    }
    for (int i = 0; i < SNOW; i++) {
        a->snow[i][1] += 0.008f * a->h * dt;
        if (a->snow[i][1] > a->sandY) { a->snow[i][1] = 0; a->snow[i][0] = frand(0, (float)a->w); }
    }
    a->chestTimer -= dt;
    if (a->chestTimer <= 0) {
        a->chestTimer = frand(0.15f, 0.5f);
        add_bubble(a, a->chestRect.x + a->chestRect.w * frand(0.3f, 0.7f), (float)a->chestRect.y);
    }
    a->sandTimer -= dt;
    if (a->sandTimer <= 0) {
        a->sandTimer = frand(0.4f, 1.2f);
        add_bubble(a, frand(0, (float)a->w), (float)a->sandY);
    }
    for (int i = 0; i < MAX_BUBBLES; i++) {
        Bubble *b = &a->b[i];
        if (!b->alive) continue;
        b->y += b->vy * dt;
        b->phase += dt * 3;
        if (b->y < -10) b->alive = 0;
    }
}

static void draw_fish(Aquarium *a, SDL_Renderer *r, const Fish *f) {
    int s = fish_scale(a, f);
    SDL_Rect d = { (int)f->x, (int)fish_y(a, f), a->fw[f->species] * s, a->fh[f->species] * s };
    SDL_RenderCopyEx(r, a->fish[f->species][f->frame], NULL, &d, 0, NULL,
                     f->vx < 0 ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
}

static void draw(void *p, SDL_Renderer *r) {
    Aquarium *a = p;
    vgradient(r, a->w, a->h, hexcol(0x1B6CA8), hexcol(0x0B2E59));
    for (int i = 0; i < 4; i++) {                       /* light rays */
        float x = a->w * (0.15f + 0.25f * i) + sinf(a->t * 0.3f + i) * a->w * 0.05f;
        SDL_Color c = { 255, 255, 255, 16 };
        fill_triangle(r, x - a->w * 0.03f, 0, x + a->w * 0.03f, 0, x + a->w * 0.12f, (float)a->sandY, c);
    }
    for (int i = 0; i < a->nfar; i++) {                 /* faraway fish, hazy blue */
        SDL_Texture *t = a->fish[a->f[i].species][a->f[i].frame];
        SDL_SetTextureColorMod(t, 80, 130, 175);
        SDL_SetTextureAlphaMod(t, 190);
        draw_fish(a, r, &a->f[i]);
        SDL_SetTextureColorMod(t, 255, 255, 255);
        SDL_SetTextureAlphaMod(t, 255);
    }
    for (int i = 0; i < WEEDS; i++) {                   /* seaweed */
        float x = a->weedX[i], y = (float)a->sandY + a->sc * 2, seg = a->weedH[i] / 8;
        for (int k = 0; k < 8; k++) {
            float sway = sinf(a->t * 0.9f + a->weedP[i] + k * 0.45f) * a->sc * 1.6f * k / 4;
            float nx = x + sway, ny = y - seg;
            thick_line(r, x, y, nx, ny, a->sc * (4.5f - k * 0.35f), k % 2 ? hexcol(0x3CB371) : hexcol(0x2E8B57));
            x = nx;
            y = ny;
        }
    }
    SDL_Color st = hexcol(0xE4C88E), sb = hexcol(0xC9A766);   /* sand, lit from above */
    SDL_Vertex v[4] = {
        { { 0, (float)a->sandY }, st, { 0, 0 } }, { { (float)a->w, (float)a->sandY }, st, { 0, 0 } },
        { { (float)a->w, (float)a->h }, sb, { 0, 0 } }, { { 0, (float)a->h }, sb, { 0, 0 } },
    };
    SDL_RenderGeometry(r, NULL, v, 4, (int[]){ 0, 1, 2, 0, 2, 3 }, 6);
    set_color(r, hexcol(0xF0DCA8));
    SDL_RenderFillRect(r, &(SDL_Rect){ 0, a->sandY, a->w, a->sc });
    for (int i = 0; i < PEBBLES; i++) {
        static const uint32_t pc[3] = { 0xB89A5E, 0xC8A96B, 0xEAD3A2 };
        set_color(r, hexcol(pc[a->pebble[i][2]]));
        SDL_Rect q = { a->pebble[i][0], a->pebble[i][1], a->sc * 2, a->sc };
        SDL_RenderFillRect(r, &q);
    }
    SDL_RenderCopy(r, a->coral, NULL, &a->coralRect);
    SDL_RenderCopy(r, a->brain, NULL, &a->brainRect);
    SDL_RenderCopy(r, a->chest, NULL, &a->chestRect);
    for (int i = 0; i < 2; i++) SDL_RenderCopy(r, a->star, NULL, &a->starRect[i]);
    SDL_Rect cr = { (int)a->crabX, a->sandY + (a->h - a->sandY) / 4 - 9 * a->sc, 22 * a->sc, 13 * a->sc };
    SDL_RenderCopy(r, a->crab[(int)a->crabStep % 2], NULL, &cr);
    for (int i = 0; i < JELLIES; i++) {
        const Jelly *j = &a->j[i];
        SDL_Rect d = { (int)j->x, (int)j->y, 15 * a->sc, 24 * a->sc };
        SDL_RenderCopy(r, a->jelly[sinf(j->phase) > 0], NULL, &d);
    }
    if (a->visOn[1]) draw_fish(a, r, &a->vis[1]);
    for (int i = a->nfar; i < a->nfish; i++) draw_fish(a, r, &a->f[i]);
    if (a->visOn[0]) draw_fish(a, r, &a->vis[0]);
    for (int i = 0; i < MAX_BUBBLES; i++) {
        Bubble *b = &a->b[i];
        if (!b->alive) continue;
        int s = 7 * a->sc / 2 + 2;
        SDL_Rect d = { (int)(b->x + sinf(b->phase) * a->sc * 2), (int)b->y, s, s };
        SDL_RenderCopy(r, a->bubble, NULL, &d);
    }
    int ss = a->sc / 2 > 1 ? a->sc / 2 : 1;             /* drifting specks */
    set_color(r, (SDL_Color){ 220, 240, 255, 50 });
    for (int i = 0; i < SNOW; i++) {
        SDL_Rect q = { (int)(a->snow[i][0] + sinf(a->t * 0.5f + a->snow[i][2]) * a->sc * 3), (int)a->snow[i][1], ss, ss };
        SDL_RenderFillRect(r, &q);
    }
}

static void destroy(void *p) {
    Aquarium *a = p;
    for (int s = 0; s < SPECIES; s++)
        for (int fr = 0; fr < 2; fr++) SDL_DestroyTexture(a->fish[s][fr]);
    for (int fr = 0; fr < 2; fr++) {
        SDL_DestroyTexture(a->jelly[fr]);
        SDL_DestroyTexture(a->crab[fr]);
    }
    SDL_DestroyTexture(a->bubble);
    SDL_DestroyTexture(a->chest);
    SDL_DestroyTexture(a->star);
    SDL_DestroyTexture(a->coral);
    SDL_DestroyTexture(a->brain);
    free(a);
}

const Saver saver_aquarium = { "aquarium", "FISH", create, step, draw, destroy };
