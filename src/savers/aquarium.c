/* Aquarium: pixel-art fish, swaying seaweed and a bubbling treasure chest. */
#include "../engine.h"
#include <math.h>
#include <stdlib.h>

#define SPECIES 3
#define MAX_FISH 14
#define MAX_BUBBLES 140
#define WEEDS 7
#define PEBBLES 70

/* Paints `col` over pixels of `under` inside a rectangle (used for stripes and bellies). */
static void recolor(Canvas *c, int x0, int y0, int x1, int y1, uint32_t under, uint32_t col) {
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            if (get(c, x, y) == under) put(c, x, y, col);
}

static void tail(Canvas *c, float x0, float cy, int frame, float spread, uint32_t col) {
    float wig = frame ? -1.5f : 1.5f;
    float xs[3] = { x0, 0, 0 }, ys[3] = { cy, cy - spread + wig, cy + spread + wig };
    fill_poly(c, xs, ys, 3, col);
}

static Canvas make_fish(int species, int frame) {
    Canvas c;
    if (species == 0) {                         /* clownfish */
        c = canvas_new(22, 13);
        uint32_t o = rgb(0xFF8A1F);
        tail(&c, 4.5f, 6.5f, frame, 4.5f, rgb(0xF06A00));
        fill_ellipse(&c, 12, 6.5f, 8.5f, 5, o);
        fill_poly(&c, (float[]){ 9, 12, 14 }, (float[]){ 2, 0, 2.5f }, 3, rgb(0xF06A00));
        recolor(&c, 8, 0, 9, 12, o, rgb(0xFFFFFF));
        recolor(&c, 14, 0, 15, 12, o, rgb(0xFFFFFF));
        put(&c, 17, 5, rgb(0x111111)); put(&c, 18, 5, rgb(0x111111)); put(&c, 17, 4, rgb(0xFFFFFF));
        outline(&c, rgb(0x3A1A00));
    } else if (species == 1) {                  /* blue tang */
        c = canvas_new(24, 14);
        uint32_t b = rgb(0x2563EB);
        tail(&c, 4.5f, 7, frame, 5, rgb(0xFFD23F));
        fill_ellipse(&c, 13, 7, 9, 5.5f, b);
        fill_ellipse(&c, 13, 6, 6, 2.4f, rgb(0x0B1A40));
        fill_ellipse(&c, 14, 6.4f, 4, 1.2f, b);
        put(&c, 19, 5, rgb(0x111111)); put(&c, 19, 4, rgb(0xFFFFFF));
        fill_poly(&c, (float[]){ 10, 15, 13 }, (float[]){ 12, 12, 14 }, 3, rgb(0xFFD23F));
        outline(&c, rgb(0x0A1430));
    } else {                                    /* pufferfish */
        c = canvas_new(19, 16);
        uint32_t g = rgb(0xC8E05A);
        tail(&c, 3.5f, 8, frame, 3.5f, rgb(0xA7C23E));
        fill_ellipse(&c, 10, 8, 7.5f, 6.8f, g);
        recolor(&c, 0, 9, 18, 15, g, rgb(0xF2F7C8));
        static const int spots[][2] = { {7,4}, {11,3}, {6,7}, {10,6}, {13,9}, {8,9} };
        for (int i = 0; i < 6; i++)
            if (get(&c, spots[i][0], spots[i][1]) == g) put(&c, spots[i][0], spots[i][1], rgb(0x6E8A23));
        fill_rect(&c, 13, 5, 15, 7, rgb(0xFFFFFF));
        fill_rect(&c, 14, 6, 15, 7, rgb(0x111111));
        fill_poly(&c, (float[]){ 8, 11, 9 }, (float[]){ 10, 10, 13 }, 3, rgb(0xA7C23E));
        put(&c, 17, 10, rgb(0x6E3F18));
        outline(&c, rgb(0x2F3A0E));
    }
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

typedef struct { int species, frame; float x, baseY, vx, bobAmp, bobF, phase, flick; } Fish;
typedef struct { float x, y, vy, phase; int alive; } Bubble;

typedef struct {
    int w, h, sc, sandY, nfish;
    float t, chestTimer, sandTimer;
    SDL_Texture *fish[SPECIES][2], *bubble, *chest;
    int fw[SPECIES], fh[SPECIES];
    Fish f[MAX_FISH];
    Bubble b[MAX_BUBBLES];
    float weedX[WEEDS], weedH[WEEDS], weedP[WEEDS];
    int pebble[PEBBLES][3];
    SDL_Rect chestRect;
} Aquarium;

static void spawn_fish(Aquarium *a, Fish *f, int anywhere) {
    f->species = irand(SPECIES);
    float speed = frand(0.06f, 0.16f) * a->w;
    int right = irand(2);
    f->vx = right ? speed : -speed;
    float fw = (float)a->fw[f->species] * a->sc;
    f->x = anywhere ? frand(0, (float)a->w) : (right ? -fw : (float)a->w);
    f->baseY = frand(0.08f, 0.85f) * a->sandY;
    f->bobAmp = frand(0.01f, 0.03f) * a->h;
    f->bobF = frand(0.3f, 0.8f);
    f->phase = frand(0, 6.28f);
    f->flick = frand(0, 1);
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
    a->sandY = h - h / 9;
    for (int s = 0; s < SPECIES; s++)
        for (int fr = 0; fr < 2; fr++) {
            Canvas c = make_fish(s, fr);
            a->fw[s] = c.w;
            a->fh[s] = c.h;
            a->fish[s][fr] = canvas_texture(r, &c);
        }
    Canvas bc = make_bubble(), cc = make_chest();
    a->bubble = canvas_texture(r, &bc);
    a->chestRect = (SDL_Rect){ (int)(w * 0.7f), a->sandY - 14 * a->sc, 26 * a->sc, 18 * a->sc };
    a->chest = canvas_texture(r, &cc);
    a->nfish = 6 + w * h / 300000;
    if (a->nfish > MAX_FISH) a->nfish = MAX_FISH;
    for (int i = 0; i < a->nfish; i++) spawn_fish(a, &a->f[i], 1);
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
    return a;
}

static void step(void *p, float dt) {
    Aquarium *a = p;
    a->t += dt;
    for (int i = 0; i < a->nfish; i++) {
        Fish *f = &a->f[i];
        f->x += f->vx * dt;
        f->flick += dt * 4;
        f->frame = (int)f->flick % 2;
        float fw = (float)a->fw[f->species] * a->sc;
        if (f->x < -fw - 5 || f->x > a->w + 5) spawn_fish(a, f, 0);
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

static void draw(void *p, SDL_Renderer *r) {
    Aquarium *a = p;
    vgradient(r, a->w, a->h, hexcol(0x1B6CA8), hexcol(0x0B2E59));
    for (int i = 0; i < 4; i++) {                       /* light rays */
        float x = a->w * (0.15f + 0.25f * i) + sinf(a->t * 0.3f + i) * a->w * 0.05f;
        SDL_Color c = { 255, 255, 255, 16 };
        fill_triangle(r, x - a->w * 0.03f, 0, x + a->w * 0.03f, 0, x + a->w * 0.12f, (float)a->sandY, c);
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
    SDL_SetRenderDrawColor(r, 0xD9, 0xB9, 0x7A, 255);   /* sand */
    SDL_Rect sand = { 0, a->sandY, a->w, a->h - a->sandY };
    SDL_RenderFillRect(r, &sand);
    for (int i = 0; i < PEBBLES; i++) {
        static const uint32_t pc[3] = { 0xB89A5E, 0xC8A96B, 0xEAD3A2 };
        set_color(r, hexcol(pc[a->pebble[i][2]]));
        SDL_Rect q = { a->pebble[i][0], a->pebble[i][1], a->sc * 2, a->sc };
        SDL_RenderFillRect(r, &q);
    }
    SDL_RenderCopy(r, a->chest, NULL, &a->chestRect);
    for (int i = 0; i < a->nfish; i++) {
        Fish *f = &a->f[i];
        float y = f->baseY + sinf(a->t * f->bobF * 6.28f + f->phase) * f->bobAmp;
        SDL_Rect d = { (int)f->x, (int)y, a->fw[f->species] * a->sc, a->fh[f->species] * a->sc };
        SDL_RenderCopyEx(r, a->fish[f->species][f->frame], NULL, &d, 0, NULL,
                         f->vx < 0 ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
    }
    for (int i = 0; i < MAX_BUBBLES; i++) {
        Bubble *b = &a->b[i];
        if (!b->alive) continue;
        int s = 7 * a->sc / 2 + 2;
        SDL_Rect d = { (int)(b->x + sinf(b->phase) * a->sc * 2), (int)b->y, s, s };
        SDL_RenderCopy(r, a->bubble, NULL, &d);
    }
}

static void destroy(void *p) {
    Aquarium *a = p;
    for (int s = 0; s < SPECIES; s++)
        for (int fr = 0; fr < 2; fr++) SDL_DestroyTexture(a->fish[s][fr]);
    SDL_DestroyTexture(a->bubble);
    SDL_DestroyTexture(a->chest);
    free(a);
}

const Saver saver_aquarium = { "aquarium", "FISH", create, step, draw, destroy };
