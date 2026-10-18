/* RAINBOW: the Waffle Cat flies across the night sky on a wavy rainbow.
   Every 20-30 seconds it slows down and shimmers, goes *poof*, its rainbow races
   ahead with rainbow lines zooming across the screen, and it pops back out
   at the end of the rainbow.
   (Idea and story by a six-year-old.) */
#include "../engine.h"
#include <math.h>
#include <stdlib.h>

#define CAT_W 56
#define CAT_H 36
#define CAT_FRAMES 4
#define TRAIL 240
#define STARS 140
#define BITS 140
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

/* Flying is the main show: the trick is a short treat every 20-30 seconds. */
#define SHIMMER_T 0.8f   /* slow down, flash colours, wobble: something is coming */
#define POOF_T 0.3f      /* gone! just sparkles */
#define STREAK_T 0.7f    /* the rainbow zooms ahead on its own */
#define FLASH_T 0.3f     /* bright pop when the cat comes back out */
#define FIRST_TRICK 12   /* seconds before the first trick (plus a little) */
#define TRICK_GAP 16     /* 16-24 s timer; waiting for the cat to reach the spot makes it ~20-30 */

typedef struct { float x, y, t; int brk; } TrailPt;   /* brk: start of a new ribbon */
typedef struct { float x, y, vx, vy, life, max; SDL_Color c; } Bit;
typedef struct { float y, start, len; } Streak;

typedef struct {
    int w, h, sc, frame, state, head, count, brkNext;
    float t, x, baseY, phase, speed, boost, frameAcc, stateT, nextTrick, fromX;
    float streakT, ringT, ringX, ringY, flash;
    SDL_Texture *cat[CAT_FRAMES];
    TrailPt trail[TRAIL];
    Bit bits[BITS];
    Streak streak[STREAKS];
    float star[STARS][3];
    float shootX, shootY, shootT;
} Rainbow;

static float catw(const Rainbow *r) { return (float)CAT_W * r->sc; }
static float cath(const Rainbow *r) { return (float)CAT_H * r->sc; }

/* The cat bobs by distance flown, not by time, so its height is always a
   function of x: the trail stays smooth through slow-downs and the teleport,
   and the cat pops back out exactly on its rainbow. */
static float cat_y(const Rainbow *r, float x) {
    return r->baseY + sinf(x / r->w * 7.0f + r->phase) * r->h * 0.05f;
}

static void push_trail(Rainbow *r, float x) {
    float tx = x + catw(r) * 0.18f, ty = cat_y(r, x) + cath(r) * 0.55f;
    int brk = r->count == 0 || r->brkNext || tx < r->trail[r->head].x;
    r->brkNext = 0;
    r->head = (r->head + 1) % TRAIL;
    r->trail[r->head] = (TrailPt){ tx, ty, r->t, brk };
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

/* Start a fresh pass just off the left edge. */
static void new_pass(Rainbow *r) {
    r->x = -catw(r);
    r->baseY = frand(0.2f, 0.62f) * r->h;
    r->phase = frand(0, 2 * PI_F);
    r->speed = r->w * frand(0.17f, 0.22f);
    r->brkNext = 1;                              /* never join this ribbon to the last one */
}

static void step(void *p, float dt);

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
    r->streakT = r->ringT = 99;
    r->shootT = -frand(2, 6);
    /* Start already on screen, with a rainbow already behind the cat:
       fly the last couple of seconds before the first frame. */
    new_pass(r);
    float startX = frand(0.05f, 0.3f) * w;
    r->x = startX - r->speed * 2.2f;
    r->nextTrick = 99;
    while (r->x < startX) step(r, 1.0f / 30);
    r->nextTrick = frand(FIRST_TRICK, FIRST_TRICK + 4);
    return r;
}

static void step(void *p, float dt) {
    Rainbow *r = p;
    r->t += dt;
    r->stateT += dt;
    r->streakT += dt;
    r->ringT += dt;
    r->flash = r->flash > dt ? r->flash - dt : 0;
    float k = r->stateT / SHIMMER_T;

    switch (r->state) {
    case FLY:
    case SHIMMER: {
        float v = r->speed * (1 + r->boost);
        if (r->state == SHIMMER) v *= 1 - 0.75f * k * k;  /* brake into the trick */
        r->boost = r->boost > 0 ? r->boost - dt * 1.5f : 0;
        r->x += v * dt;
        push_trail(r, r->x);
        if (r->state == FLY) {
            if (r->x > r->w) new_pass(r);                /* fully off the right edge */
            else {
                r->nextTrick -= dt;
                /* When it's time, do the trick while the cat is well on screen. */
                if (r->nextTrick <= 0 && r->x > r->w * 0.2f && r->x < r->w * 0.42f) { r->state = SHIMMER; r->stateT = 0; }
            }
        } else {
            float cy = cat_y(r, r->x);
            if (frand(0, 1) < 0.25f + 0.75f * k)
                burst(r, r->x + frand(0, catw(r)), cy + frand(0, cath(r)), 1, r->h * (0.08f + 0.1f * k));
            if (r->stateT > SHIMMER_T) {                 /* poof! */
                r->ringX = r->x + catw(r) / 2;
                r->ringY = cy + cath(r) / 2;
                burst(r, r->ringX, r->ringY, 70, r->h * 0.45f);
                r->ringT = 0;
                r->state = POOF;
                r->stateT = 0;
            }
        }
        break;
    }
    case POOF:
        if (r->stateT > POOF_T) {
            r->state = STREAK;
            r->stateT = 0;
            r->streakT = 0;
            r->fromX = r->x;
            for (int i = 0; i < STREAKS; i++)
                r->streak[i] = (Streak){ frand(0.1f, 0.9f) * r->h, frand(0, 0.3f), frand(0.4f, 0.7f) };
        }
        break;
    case STREAK: {                                       /* the rainbow races ahead */
        float toX = fminf(r->fromX + r->w * 0.42f, r->w * 0.8f);
        float s = fminf(r->stateT / STREAK_T, 1);
        r->x = r->fromX + (toX - r->fromX) * s * s * (3 - 2 * s);
        push_trail(r, r->x);
        TrailPt *hp = &r->trail[r->head];
        burst(r, hp->x, hp->y, 1, r->h * 0.05f);
        if (s >= 1) {                                    /* pop back out and zoom off */
            burst(r, r->x + catw(r) / 2, cat_y(r, r->x) + cath(r) / 2, 40, r->h * 0.3f);
            r->flash = FLASH_T;
            r->state = FLY;
            r->stateT = 0;
            r->boost = 1.4f;
            r->nextTrick = frand(TRICK_GAP, TRICK_GAP + 8);
        }
        break;
    }
    }
    r->frameAcc += dt * (r->state == SHIMMER ? 9 + 12 * k : 9);
    r->frame = (int)r->frameAcc % CAT_FRAMES;
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
        /* wavelength scales with the pixel size, so it looks the same from the preview to 4K */
        float wave = sinf(p->x * 0.06f / r->sc - r->t * 6) * bandH * 0.8f;
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

    /* Stars light up as the cat (or the racing rainbow) goes by. */
    int lit = r->state != POOF;
    float lx = r->x + catw(r) / 2, ly = cat_y(r, r->x) + cath(r) / 2, lr = catw(r) * 1.4f;
    for (int i = 0; i < STARS; i++) {
        float sx = r->star[i][0], sy = r->star[i][1];
        float tw = 0.5f + 0.5f * sinf(r->t * 2 + r->star[i][2]), glow = 0;
        if (lit) {
            float dx = sx - lx, dy = sy - ly, d2 = (dx * dx + dy * dy) / (lr * lr);
            if (d2 < 1) glow = 1 - d2;
        }
        float a = 60 + 160 * tw + 120 * glow;
        SDL_Color c = { 255, 255, 230, (Uint8)(a > 255 ? 255 : a) };
        if (i % 9 == 0 || glow > 0.3f) sparkle(ren, sx, sy, r->sc * (1 + tw + 1.5f * glow), c);
        else { set_color(ren, c); SDL_Rect s = { (int)sx, (int)sy, r->sc, r->sc }; SDL_RenderFillRect(ren, &s); }
    }
    if (r->shootT > 0) {                                 /* the occasional shooting star */
        float k = r->shootT / 1.2f, x = r->shootX - k * r->w * 0.5f, y = r->shootY + k * r->h * 0.25f;
        SDL_Color c = { 255, 255, 255, (Uint8)(200 * (1 - k)) };
        thick_line(ren, x, y, x + r->w * 0.06f, y - r->h * 0.03f, r->sc * 1.2f, c);
    }

    float bandH = 3.0f * r->sc;
    for (int b = 0; b < 6; b++) ribbon_band(r, ren, b, bandH, 2.6f);

    for (int i = 0; i < STREAKS && r->streakT < 2; i++) {  /* extra rainbow lines zooming across */
        float k = (r->streakT - r->streak[i].start) / r->streak[i].len;
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

    if (r->ringT < 0.4f) {                               /* poof ring of chunky dots */
        float k = r->ringT / 0.4f, rad = catw(r) * (0.3f + 0.9f * k), s = r->sc * 3.0f * (1 - k * 0.5f);
        for (int i = 0; i < 12; i++) {
            SDL_Color c = hexcol(BANDS[i % 6]);
            c.a = (Uint8)(255 * (1 - k));
            set_color(ren, c);
            float a = i * PI_F / 6;
            SDL_Rect d = { (int)(r->ringX + cosf(a) * rad - s / 2), (int)(r->ringY + sinf(a) * rad - s / 2), (int)s, (int)s };
            SDL_RenderFillRect(ren, &d);
        }
    }

    if (r->state == STREAK) {                            /* a bright star leads the rainbow */
        TrailPt *hp = &r->trail[r->head];
        float tw = 0.5f + 0.5f * sinf(r->t * 30);
        sparkle(ren, hp->x, hp->y, r->sc * (5 + 2 * tw), (SDL_Color){ 255, 255, 255, 255 });
    }

    if (r->state == FLY || r->state == SHIMMER) {
        SDL_Rect d = { (int)r->x, (int)cat_y(r, r->x), (int)catw(r), (int)cath(r) };
        SDL_Texture *t = r->cat[r->frame];
        if (r->state == SHIMMER) {
            float k = r->stateT / SHIMMER_T;
            SDL_Color c = hsv(r->t * (360 + 900 * k), 0.6f, 1);
            SDL_SetTextureColorMod(t, c.r, c.g, c.b);
            d.x += ((int)(r->t * 30) % 2 ? 1 : -1) * (int)(k * 2.5f) * r->sc;   /* wobble */
        }
        SDL_RenderCopy(ren, t, NULL, &d);
        SDL_SetTextureColorMod(t, 255, 255, 255);
        if (r->flash > 0) {                              /* pop! a bright flash as it reappears */
            SDL_SetTextureBlendMode(t, SDL_BLENDMODE_ADD);
            SDL_SetTextureAlphaMod(t, (Uint8)(255 * r->flash / FLASH_T));
            SDL_RenderCopy(ren, t, NULL, &d);
            SDL_SetTextureAlphaMod(t, 255);
            SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
        }
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
