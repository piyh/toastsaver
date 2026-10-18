/* Flying toasters: winged chrome toasters and slices of toast drifting down-left. */
#include "../engine.h"
#include <math.h>
#include <stdlib.h>

#define TOASTER_W 64
#define TOASTER_H 46
#define TOAST_W 28
#define TOAST_H 28
#define WING_FRAMES 4
#define TOAST_SHADES 3

static void feather(Canvas *c, float px, float py, float a, float len, float hw, uint32_t col) {
    float dx = cosf(a), dy = sinf(a), nx = -dy, ny = dx;
    float xs[5] = { px + nx * hw * 0.6f, px + dx * len * 0.65f + nx * hw, px + dx * len,
                    px + dx * len * 0.65f - nx * hw, px - nx * hw * 0.6f };
    float ys[5] = { py + ny * hw * 0.6f, py + dy * len * 0.65f + ny * hw, py + dy * len,
                    py + dy * len * 0.65f - ny * hw, py - ny * hw * 0.6f };
    fill_poly(c, xs, ys, 5, col);
}

static void wing(Canvas *c, float px, float py, float flapDeg, int dir) {
    static const float len[3] = { 17.0f, 14.5f, 11.0f };
    static const uint32_t shade[3] = { 0xFFFFFF, 0xE9EEF5, 0xD2D9E4 };
    for (int k = 2; k >= 0; k--) {
        float deg = flapDeg + k * 17.0f;
        float a = (dir > 0 ? deg : 180.0f - deg) * PI_F / 180.0f;
        feather(c, px, py, a, len[k], 2.6f, rgb(shade[k]));
    }
}

Canvas make_toaster(int frame) {
    static const float flap[WING_FRAMES] = { -58.0f, -32.0f, -8.0f, 14.0f };
    Canvas c = canvas_new(TOASTER_W, TOASTER_H);
    const int ox = 16, oy = 14;
    wing(&c, ox + 4, oy + 3, flap[frame], -1);
    wing(&c, ox + 28, oy + 3, flap[frame], +1);
    fill_round_rect(&c, ox, oy, ox + 31, oy + 21, 5, rgb(0xC3C9D3));
    fill_round_rect(&c, ox + 1, oy + 1, ox + 30, oy + 5, 3, rgb(0xE6EAF0));
    fill_rect(&c, ox + 2, oy + 17, ox + 29, oy + 20, rgb(0x9CA3B0));
    fill_rect(&c, ox + 3, oy + 7, ox + 4, oy + 15, rgb(0xF7F9FC));
    fill_rect(&c, ox + 6, oy + 8, ox + 6, oy + 12, rgb(0xF7F9FC));
    fill_rect(&c, ox + 7, oy, ox + 14, oy + 1, rgb(0x2A2E36));
    fill_rect(&c, ox + 18, oy, ox + 25, oy + 1, rgb(0x2A2E36));
    fill_rect(&c, ox + 19, oy - 2, ox + 24, oy - 1, rgb(0xD9A55B));
    fill_rect(&c, ox + 28, oy + 6, ox + 28, oy + 16, rgb(0x5A606C));
    fill_rect(&c, ox + 29, oy + 9, ox + 33, oy + 11, rgb(0x3A3F4A));
    fill_ellipse(&c, ox + 9.5f, oy + 13.5f, 2.2f, 2.2f, rgb(0xD8433B));
    put(&c, ox + 9, oy + 12, rgb(0xFFB3AA));
    fill_rect(&c, ox + 15, oy + 13, ox + 15, oy + 13, rgb(0x3A3F4A));
    fill_rect(&c, ox + 16, oy + 14, ox + 19, oy + 14, rgb(0x3A3F4A));
    fill_rect(&c, ox + 20, oy + 13, ox + 20, oy + 13, rgb(0x3A3F4A));
    fill_rect(&c, ox + 3, oy + 22, ox + 6, oy + 23, rgb(0x3A3F4A));
    fill_rect(&c, ox + 25, oy + 22, ox + 28, oy + 23, rgb(0x3A3F4A));
    outline(&c, rgb(0x1E2128));
    return c;
}

static Canvas make_toast(int shade, int buttered) {
    static const uint32_t crumb[TOAST_SHADES] = { 0xF4DCA6, 0xE2B064, 0xB97C3C };
    static const uint32_t crust[TOAST_SHADES] = { 0xB8793A, 0x94592A, 0x6E3F18 };
    static const int dots[][2] = { {8,13}, {17,9}, {12,18}, {20,16}, {7,20}, {15,14}, {11,7} };
    Canvas c = canvas_new(TOAST_W, TOAST_H);
    fill_ellipse(&c, 14, 10, 12, 8, rgb(crust[shade]));
    fill_round_rect(&c, 3, 9, 24, 25, 3, rgb(crust[shade]));
    fill_ellipse(&c, 14, 10.5f, 9.5f, 6, rgb(crumb[shade]));
    fill_round_rect(&c, 5, 10, 22, 23, 2, rgb(crumb[shade]));
    for (size_t i = 0; i < sizeof dots / sizeof dots[0]; i++)
        put(&c, dots[i][0], dots[i][1], rgb(crust[shade]));
    if (buttered) {
        fill_rect(&c, 10, 12, 17, 16, rgb(0xFFE27A));
        fill_rect(&c, 10, 12, 17, 12, rgb(0xFFF3B8));
        fill_rect(&c, 17, 13, 17, 16, rgb(0xE8C14F));
    }
    outline(&c, rgb(0x3A230F));
    return c;
}

typedef struct {
    float x, y, speed, flapPhase, flapRate;
    int isToast, shade, buttered;
} Flyer;

typedef struct {
    int w, h, scale, count;
    Flyer *f;
    SDL_Texture *toaster[WING_FRAMES];
    SDL_Texture *toast[TOAST_SHADES][2];
} Toasters;

static int spr_w(const Toasters *t, const Flyer *f) { return (f->isToast ? TOAST_W : TOASTER_W) * t->scale; }
static int spr_h(const Toasters *t, const Flyer *f) { return (f->isToast ? TOAST_H : TOASTER_H) * t->scale; }

static int overlaps(const Toasters *t, int self, float x, float y) {
    const Flyer *me = &t->f[self];
    float pad = 6.0f * t->scale;
    for (int i = 0; i < t->count; i++) {
        const Flyer *o = &t->f[i];
        if (i != self && x < o->x + spr_w(t, o) + pad && o->x < x + spr_w(t, me) + pad &&
            y < o->y + spr_h(t, o) + pad && o->y < y + spr_h(t, me) + pad)
            return 1;
    }
    return 0;
}

static void respawn(Toasters *t, int i, int anywhere) {
    Flyer *f = &t->f[i];
    float x = 0, y = 0;
    for (int tries = 0; tries < 24; tries++) {
        if (anywhere) {
            x = frand(-0.1f * t->w, 1.1f * t->w);
            y = frand(-0.2f * t->h, 0.9f * t->h);
        } else if (frand(0, (float)(t->w + t->h)) < t->w) {
            x = frand(0.15f * t->w, 1.2f * t->w);
            y = -(float)spr_h(t, f) - frand(0, 0.3f * t->h);
        } else {
            x = t->w + frand(0, 0.3f * t->w);
            y = frand(-0.3f * t->h, 0.6f * t->h);
        }
        if (!overlaps(t, i, x, y)) break;
    }
    f->x = x;
    f->y = y;
    if (f->isToast) {
        f->shade = irand(TOAST_SHADES);
        f->buttered = irand(3) == 0;
    }
}

static int by_speed(const void *a, const void *b) {
    float d = ((const Flyer *)a)->speed - ((const Flyer *)b)->speed;
    return (d > 0) - (d < 0);
}

static void *create(SDL_Renderer *r, int w, int h) {
    Toasters *t = calloc(1, sizeof *t);
    t->w = w;
    t->h = h;
    t->scale = (h + 135) / 270 > 1 ? (h + 135) / 270 : 1;
    float logical = ((float)w / t->scale) * ((float)h / t->scale);
    int toasters = (int)(logical / 13000.0f);
    if (toasters < 6) toasters = 6;
    if (toasters > 18) toasters = 18;
    t->count = toasters + toasters * 2 / 3;
    t->f = calloc((size_t)t->count, sizeof(Flyer));
    for (int i = 0; i < t->count; i++) {
        Flyer *f = &t->f[i];
        f->isToast = i >= toasters;
        f->speed = frand(38.0f, 95.0f) * t->scale;
        f->flapRate = 0.9f + f->speed / (60.0f * t->scale);
        f->flapPhase = frand(0, 1);
        f->x = f->y = -100000.0f;
    }
    qsort(t->f, (size_t)t->count, sizeof(Flyer), by_speed);
    for (int i = 0; i < t->count; i++) respawn(t, i, 1);
    for (int i = 0; i < WING_FRAMES; i++) {
        Canvas c = make_toaster(i);
        t->toaster[i] = canvas_texture(r, &c);
    }
    for (int s = 0; s < TOAST_SHADES; s++)
        for (int b = 0; b < 2; b++) {
            Canvas c = make_toast(s, b);
            t->toast[s][b] = canvas_texture(r, &c);
        }
    return t;
}

static void step(void *p, float dt) {
    Toasters *t = p;
    for (int i = 0; i < t->count; i++) {
        Flyer *f = &t->f[i];
        f->x -= f->speed * dt;
        f->y += f->speed * 0.5f * dt;
        f->flapPhase = fmodf(f->flapPhase + f->flapRate * dt, 1.0f);
        if (f->x < -spr_w(t, f) || f->y > t->h) respawn(t, i, 0);
    }
}

static void draw(void *p, SDL_Renderer *r) {
    static const int seq[6] = { 0, 1, 2, 3, 2, 1 };
    Toasters *t = p;
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderFillRect(r, NULL);
    for (int i = 0; i < t->count; i++) {
        const Flyer *f = &t->f[i];
        SDL_Rect dst = { (int)f->x, (int)f->y, spr_w(t, f), spr_h(t, f) };
        if (dst.x > t->w || dst.y + dst.h < 0) continue;
        SDL_Texture *tex = f->isToast ? t->toast[f->shade][f->buttered]
                                      : t->toaster[seq[(int)(f->flapPhase * 6) % 6]];
        SDL_RenderCopy(r, tex, NULL, &dst);
    }
}

static void destroy(void *p) {
    Toasters *t = p;
    for (int i = 0; i < WING_FRAMES; i++) SDL_DestroyTexture(t->toaster[i]);
    for (int s = 0; s < TOAST_SHADES; s++)
        for (int b = 0; b < 2; b++) SDL_DestroyTexture(t->toast[s][b]);
    free(t->f);
    free(t);
}

const Saver saver_toasters = { "toasters", "TOASTERS", create, step, draw, destroy };
