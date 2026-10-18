/* RAINBOW: the Waffle Cat flies across the night sky on a wavy rainbow.
   Every so often it shimmers, goes *poof*, rainbow lines zoom across the
   screen, and it pops back out at the end of the rainbow.
   (Idea and story by a six-year-old.) */
#include "../engine.h"
#include <math.h>
#include <stdlib.h>

#define CAT_W 56
#define CAT_H 36
#define CAT_FRAMES 4
#define TRAIL 240
#define STARS 140
#define BITS 90
#define STREAKS 4

static const uint32_t BANDS[6] = { 0xFF4D4D, 0xFF9A3C, 0xFFE14D, 0x5BD46B, 0x4DA6FF, 0x9B6BFF };

/* Original pixel art: an orange tabby with a waffle for a body, facing right. */
Canvas make_waffle_cat(int frame) {
    Canvas c = canvas_new(CAT_W, CAT_H);
    int wag = frame % 2 ? -1 : 1, paddle = frame % 2 ? 1 : -1;
    /* tail */
    static const float tail[6][2] = { {6, 19}, {4, 17.5f}, {2.5f, 15.5f}, {2, 13}, {2.5f, 10.5f}, {4, 9} };
    for (int i = 0; i < 6; i++)
        fill_ellipse(&c, tail[i][0], tail[i][1] + (i > 2 ? wag : 0), 1.8f, 1.8f, rgb(i % 2 ? 0xD9791C : 0xF3A03F));
    /* legs (paddling) */
    static const int legs[4] = { 9, 15, 23, 29 };
    for (int i = 0; i < 4; i++) {
        int dx = (i % 2 ? paddle : -paddle);
        fill_rect(&c, legs[i] + dx, 29, legs[i] + dx + 2, 33, rgb(0xF3A03F));
        fill_rect(&c, legs[i] + dx, 33, legs[i] + dx + 2, 33, rgb(0xFFE2B8));
    }
    /* waffle body with grid and highlights */
    uint32_t w = rgb(0xE5A23A);
    fill_round_rect(&c, 6, 9, 33, 30, 3, w);
    for (int y = 10; y <= 29; y++)
        for (int x = 7; x <= 32; x++) {
            if (get(&c, x, y) != w) continue;
            if (x % 5 == 0 || y % 5 == 3) put(&c, x, y, rgb(0xB97A22));
            else if (x % 5 == 1 && y % 5 == 4) put(&c, x, y, rgb(0xF5C76B));
        }
    /* syrup across the top, dripping */
    fill_rect(&c, 8, 9, 31, 10, rgb(0x9C4F12));
    fill_rect(&c, 9, 9, 30, 9, rgb(0xC46A1C));
    fill_rect(&c, 12, 11, 13, 13, rgb(0x9C4F12));
    fill_rect(&c, 20, 11, 21, 15, rgb(0x9C4F12));
    fill_rect(&c, 27, 11, 28, 12, rgb(0x9C4F12));
    /* butter */
    fill_rect(&c, 16, 5, 23, 9, rgb(0xFFE27A));
    fill_rect(&c, 16, 5, 23, 5, rgb(0xFFF3B8));
    fill_rect(&c, 23, 6, 23, 9, rgb(0xE8C14F));
    /* ears, then head */
    fill_poly(&c, (float[]){ 33, 36, 40 }, (float[]){ 13, 3, 11 }, 3, rgb(0xF3A03F));
    fill_poly(&c, (float[]){ 42, 47, 49 }, (float[]){ 11, 3, 14 }, 3, rgb(0xF3A03F));
    fill_poly(&c, (float[]){ 35, 36.5f, 38.5f }, (float[]){ 11, 6, 11 }, 3, rgb(0xFF9EB5));
    fill_poly(&c, (float[]){ 44, 46.5f, 47.5f }, (float[]){ 11, 6, 12 }, 3, rgb(0xFF9EB5));
    fill_ellipse(&c, 41, 18, 9, 8, rgb(0xF3A03F));
    fill_rect(&c, 39, 11, 39, 12, rgb(0xD9791C));
    fill_rect(&c, 41, 10, 41, 12, rgb(0xD9791C));
    fill_rect(&c, 43, 11, 43, 12, rgb(0xD9791C));
    fill_ellipse(&c, 42, 21.5f, 4.5f, 3, rgb(0xFFE2B8));
    /* big friendly eyes */
    fill_rect(&c, 36, 15, 37, 17, rgb(0x1A1A1A));
    fill_rect(&c, 45, 15, 46, 17, rgb(0x1A1A1A));
    put(&c, 36, 15, rgb(0xFFFFFF));
    put(&c, 45, 15, rgb(0xFFFFFF));
    /* nose, smile, cheeks */
    fill_rect(&c, 41, 19, 42, 19, rgb(0xFF8FB1));
    put(&c, 39, 21, rgb(0x5A2A10)); put(&c, 40, 22, rgb(0x5A2A10)); put(&c, 41, 21, rgb(0x5A2A10));
    put(&c, 42, 22, rgb(0x5A2A10)); put(&c, 43, 21, rgb(0x5A2A10));
    fill_rect(&c, 34, 20, 35, 20, rgb(0xFF9EB5));
    fill_rect(&c, 48, 20, 49, 20, rgb(0xFF9EB5));
    outline(&c, rgb(0x3A1E08));
    return c;
}

enum { FLY, SHIMMER, POOF, STREAK };

typedef struct { float x, y, t; int brk; } TrailPt;   /* brk: start of a new ribbon */
typedef struct { float x, y, vx, vy, life, max; SDL_Color c; } Bit;
typedef struct { float y, start, len; } Streak;

typedef struct {
    int w, h, sc, frame, state, head, count;
    float t, x, baseY, speed, boost, frameAcc, stateT, nextTrick, catY;
    float fromX, toX;
    SDL_Texture *cat[CAT_FRAMES];
    TrailPt trail[TRAIL];
    Bit bits[BITS];
    Streak streak[STREAKS];
    float star[STARS][3];
    float shootX, shootY, shootT;
} Rainbow;

static float catw(const Rainbow *r) { return (float)CAT_W * r->sc; }
static float cath(const Rainbow *r) { return (float)CAT_H * r->sc; }

static void push_trail(Rainbow *r, float x, float y) {
    int brk = r->count == 0 || x < r->trail[r->head].x;   /* wrapped back to the left edge */
    r->head = (r->head + 1) % TRAIL;
    r->trail[r->head] = (TrailPt){ x, y, r->t, brk };
    if (r->count < TRAIL) r->count++;
}

static void burst(Rainbow *r, float x, float y, int n, float speed) {
    for (int i = 0, k = 0; i < BITS && k < n; i++) {
        if (r->bits[i].life > 0) continue;
        float a = frand(0, 2 * PI_F), s = frand(0.3f, 1.0f) * speed;
        r->bits[i] = (Bit){ x, y, cosf(a) * s, sinf(a) * s, 0, frand(0.6f, 1.3f), hexcol(BANDS[irand(6)]) };
        r->bits[i].life = r->bits[i].max;
        k++;
    }
}

static void new_pass(Rainbow *r) {
    r->x = -catw(r);
    r->baseY = frand(0.2f, 0.65f) * r->h;
    r->speed = r->w * frand(0.16f, 0.22f);
}

static void *create(SDL_Renderer *ren, int w, int h) {
    Rainbow *r = calloc(1, sizeof *r);
    r->w = w;
    r->h = h;
    r->sc = h / 200 > 1 ? h / 200 : 1;
    for (int i = 0; i < CAT_FRAMES; i++) {
        Canvas c = make_waffle_cat(i);
        r->cat[i] = canvas_texture(ren, &c);
    }
    for (int i = 0; i < STARS; i++) {
        r->star[i][0] = frand(0, (float)w);
        r->star[i][1] = frand(0, (float)h);
        r->star[i][2] = frand(0, 6.28f);
    }
    new_pass(r);
    r->x = frand(0.05f, 0.35f) * w;          /* start already on screen */
    r->nextTrick = frand(2.5f, 4.0f);
    r->shootT = -frand(2, 6);
    return r;
}

static void step(void *p, float dt) {
    Rainbow *r = p;
    r->t += dt;
    r->stateT += dt;
    r->frameAcc += dt * 9;
    r->frame = (int)r->frameAcc % CAT_FRAMES;
    float y = r->baseY + sinf(r->t * 1.4f) * r->h * 0.05f;

    switch (r->state) {
    case FLY:
    case SHIMMER: {
        float v = r->speed * (1 + r->boost) * (r->state == SHIMMER ? 0.6f : 1.0f);
        r->boost = r->boost > 0 ? r->boost - dt * 1.5f : 0;
        r->x += v * dt;
        r->catY = y;
        push_trail(r, r->x + catw(r) * 0.18f, y + cath(r) * 0.55f);
        if (r->x > r->w + catw(r) * 0.5f) new_pass(r);
        if (r->state == FLY) {
            r->nextTrick -= dt;
            /* Do the trick while the cat is well on screen. */
            if (r->nextTrick <= 0 && r->x > r->w * 0.15f && r->x < r->w * 0.45f) { r->state = SHIMMER; r->stateT = 0; }
        } else {
            if (irand(3) == 0) burst(r, r->x + frand(0, catw(r)), y + frand(0, cath(r)), 1, r->h * 0.1f);
            if (r->stateT > 1.1f) {                      /* poof! */
                burst(r, r->x + catw(r) / 2, y + cath(r) / 2, 60, r->h * 0.45f);
                r->state = POOF;
                r->stateT = 0;
            }
        }
        break;
    }
    case POOF:                                           /* gone for a second */
        if (r->stateT > 1.0f) {
            r->state = STREAK;
            r->stateT = 0;
            r->fromX = r->x;
            r->toX = r->x + r->w * 0.42f;
            if (r->toX > r->w * 0.8f) r->toX = r->w * 0.8f;
            for (int i = 0; i < STREAKS; i++)
                r->streak[i] = (Streak){ frand(0.1f, 0.9f) * r->h, frand(0, 0.5f), frand(0.4f, 0.8f) };
        }
        break;
    case STREAK: {                                       /* rainbow lines come */
        float k = r->stateT / 1.2f;
        if (k > 1) k = 1;
        float e = 1 - (1 - k) * (1 - k);
        float nx = r->fromX + (r->toX - r->fromX) * e;
        push_trail(r, nx + catw(r) * 0.18f, r->catY + cath(r) * 0.55f);
        if (r->stateT > 1.6f) {                          /* pop back out and zoom off */
            r->x = r->toX;
            burst(r, r->x + catw(r) / 2, r->catY + cath(r) / 2, 35, r->h * 0.3f);
            r->state = FLY;
            r->stateT = 0;
            r->boost = 1.5f;
            r->nextTrick = frand(7, 11);
        }
        break;
    }
    }
    for (int i = 0; i < BITS; i++) {
        Bit *b = &r->bits[i];
        if (b->life <= 0) continue;
        b->life -= dt;
        b->x += b->vx * dt;
        b->y += b->vy * dt;
        b->vx *= 1 - 2.0f * dt;
        b->vy *= 1 - 2.0f * dt;
    }
    r->shootT += dt;
    if (r->shootT > 1.2f) { r->shootT = -frand(4, 9); r->shootX = frand(0.3f, 1.0f) * r->w; r->shootY = frand(0, 0.4f) * r->h; }
}

/* One band of the wavy ribbon as a triangle strip, fading towards its tail. */
static void ribbon_band(Rainbow *r, SDL_Renderer *ren, int band, float bandH, float maxAge) {
    static SDL_Vertex v[TRAIL * 2];
    static int idx[TRAIL * 6];
    int n = 0, k = 0;
    for (int i = r->count - 1; i >= 0; i--) {            /* oldest first */
        TrailPt *p = &r->trail[(r->head - i + TRAIL) % TRAIL];
        float age = r->t - p->t;
        if (age > maxAge) continue;
        if (p->brk && n >= 2) {                           /* finish this ribbon, start a new one */
            for (int j = 0; j + 3 < n; j += 2) {
                idx[k++] = j; idx[k++] = j + 1; idx[k++] = j + 2;
                idx[k++] = j + 1; idx[k++] = j + 3; idx[k++] = j + 2;
            }
            if (k) SDL_RenderGeometry(ren, NULL, v, n, idx, k);
            n = k = 0;
        }
        float wave = sinf(p->x * 0.02f - r->t * 6) * bandH * 0.8f;
        float off = (band - 2.5f) * bandH + wave;
        SDL_Color c = hexcol(BANDS[band]);
        c.a = (Uint8)(255 * (1 - age / maxAge));
        v[n++] = (SDL_Vertex){ { p->x, p->y + off - bandH * 0.5f }, c, { 0, 0 } };
        v[n++] = (SDL_Vertex){ { p->x, p->y + off + bandH * 0.5f + 1 }, c, { 0, 0 } };
    }
    for (int i = 0; i + 3 < n; i += 2) {
        idx[k++] = i; idx[k++] = i + 1; idx[k++] = i + 2;
        idx[k++] = i + 1; idx[k++] = i + 3; idx[k++] = i + 2;
    }
    if (k) SDL_RenderGeometry(ren, NULL, v, n, idx, k);
}

static void sparkle(SDL_Renderer *ren, float x, float y, float s, SDL_Color c) {
    thick_line(ren, x - s, y, x + s, y, s * 0.35f, c);
    thick_line(ren, x, y - s, x, y + s, s * 0.35f, c);
}

static void draw(void *p, SDL_Renderer *ren) {
    Rainbow *r = p;
    vgradient(ren, r->w, r->h, hexcol(0x0B1033), hexcol(0x2A1450));
    for (int i = 0; i < STARS; i++) {
        float tw = 0.5f + 0.5f * sinf(r->t * 2 + r->star[i][2]);
        SDL_Color c = { 255, 255, 230, (Uint8)(60 + 160 * tw) };
        if (i % 9 == 0) sparkle(ren, r->star[i][0], r->star[i][1], r->sc * (1 + tw), c);
        else { set_color(ren, c); SDL_Rect s = { (int)r->star[i][0], (int)r->star[i][1], r->sc, r->sc }; SDL_RenderFillRect(ren, &s); }
    }
    if (r->shootT > 0) {                                 /* the occasional shooting star */
        float k = r->shootT / 1.2f, x = r->shootX - k * r->w * 0.5f, y = r->shootY + k * r->h * 0.25f;
        SDL_Color c = { 255, 255, 255, (Uint8)(200 * (1 - k)) };
        thick_line(ren, x, y, x + r->w * 0.06f, y - r->h * 0.03f, r->sc * 1.2f, c);
    }

    float bandH = 3.0f * r->sc;
    for (int b = 0; b < 6; b++) ribbon_band(r, ren, b, bandH, 2.6f);

    if (r->state == STREAK) {                            /* extra rainbow lines zooming across */
        for (int i = 0; i < STREAKS; i++) {
            float k = (r->stateT - r->streak[i].start) / r->streak[i].len;
            if (k <= 0 || k > 1.6f) continue;
            float head = (k > 1 ? 1 : k) * (r->w + 200), tail = head - r->w * 0.45f;
            Uint8 a = (Uint8)(k > 1 ? 255 * (1.6f - k) / 0.6f : 255);
            for (int b = 0; b < 6; b++) {
                SDL_Color c = hexcol(BANDS[b]);
                c.a = a;
                float y = r->streak[i].y + (b - 2.5f) * bandH * 0.6f;
                thick_line(ren, tail < 0 ? 0 : tail, y, head, y, bandH * 0.6f, c);
            }
        }
    }

    if (r->state == FLY || r->state == SHIMMER) {
        SDL_Rect d = { (int)r->x, (int)r->catY, (int)catw(r), (int)cath(r) };
        SDL_Texture *t = r->cat[r->frame];
        if (r->state == SHIMMER) {
            SDL_Color c = hsv(r->t * 720, 0.6f, 1);
            SDL_SetTextureColorMod(t, c.r, c.g, c.b);
        } else {
            SDL_SetTextureColorMod(t, 255, 255, 255);
        }
        SDL_RenderCopy(ren, t, NULL, &d);
        SDL_SetTextureColorMod(t, 255, 255, 255);
    }
    for (int i = 0; i < BITS; i++) {
        Bit *b = &r->bits[i];
        if (b->life <= 0) continue;
        SDL_Color c = b->c;
        c.a = (Uint8)(255 * b->life / b->max);
        sparkle(ren, b->x, b->y, r->sc * 2.2f * (0.5f + b->life / b->max), c);
    }
}

static void destroy(void *p) {
    Rainbow *r = p;
    for (int i = 0; i < CAT_FRAMES; i++) SDL_DestroyTexture(r->cat[i]);
    free(r);
}

const Saver saver_rainbow = { "rainbow", "RAINBOW", create, step, draw, destroy };
