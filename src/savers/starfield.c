/* Starfield: flying forward through space at warp speed. */
#include "../engine.h"
#include <math.h>
#include <stdlib.h>

typedef struct {
    float x, y, z, px, py;
    int hasPrev;
} Star;

typedef struct {
    int w, h, n;
    float line;
    Star *s;
} Starfield;

static void reset_star(Star *s, int far) {
    s->x = frand(-1, 1);
    s->y = frand(-1, 1);
    s->z = far ? 1.0f : frand(0.15f, 1.0f);
    s->hasPrev = 0;
}

static void *create(SDL_Renderer *r, int w, int h) {
    (void)r;
    Starfield *f = calloc(1, sizeof *f);
    f->w = w;
    f->h = h;
    f->n = w * h / 2200;
    if (f->n < 140) f->n = 140;
    if (f->n > 700) f->n = 700;
    f->line = h / 400.0f > 1.2f ? h / 400.0f : 1.2f;
    f->s = calloc((size_t)f->n, sizeof(Star));
    for (int i = 0; i < f->n; i++) reset_star(&f->s[i], 0);
    return f;
}

static void step(void *p, float dt) {
    Starfield *f = p;
    for (int i = 0; i < f->n; i++) {
        f->s[i].z -= 0.32f * dt;
        if (f->s[i].z <= 0.03f) reset_star(&f->s[i], 1);
    }
}

static void draw(void *p, SDL_Renderer *r) {
    Starfield *f = p;
    float cx = f->w * 0.5f, cy = f->h * 0.5f, fov = f->w * 0.5f;
    SDL_SetRenderDrawColor(r, 2, 2, 10, 255);
    SDL_RenderFillRect(r, NULL);
    for (int i = 0; i < f->n; i++) {
        Star *s = &f->s[i];
        float sx = cx + s->x * fov / s->z, sy = cy + s->y * fov / s->z;
        if (sx < -10 || sy < -10 || sx > f->w + 10 || sy > f->h + 10) {
            reset_star(s, 1);
            continue;
        }
        float b = 1.0f - s->z;                       /* nearer = brighter */
        Uint8 v = (Uint8)(130 + 125 * b);
        SDL_Color c = { (Uint8)(v * 0.88f), (Uint8)(v * 0.92f), v, 255 };
        float width = f->line * (1.0f + 3.0f * b * b);
        /* Streak from the previous position, or (first frame) from a bit closer to the centre. */
        float tx = s->hasPrev ? s->px : cx + (sx - cx) * 0.94f, ty = s->hasPrev ? s->py : cy + (sy - cy) * 0.94f;
        thick_line(r, tx, ty, sx, sy, width, c);
        fill_circle(r, sx, sy, width * 0.6f, c);
        s->px = sx;
        s->py = sy;
        s->hasPrev = 1;
    }
}

static void destroy(void *p) {
    Starfield *f = p;
    free(f->s);
    free(f);
}

const Saver saver_starfield = { "starfield", "STARS", create, step, draw, destroy };
