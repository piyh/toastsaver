/* Mystify: two bouncing, color-shifting shapes leaving echo trails. */
#include "../engine.h"
#include <math.h>
#include <stdlib.h>

#define SHAPES 2
#define PTS 4
#define TRAIL 7

typedef struct {
    float x[PTS], y[PTS], vx[PTS], vy[PTS];
    float hist[TRAIL][PTS][2];
    int nhist, head;
} Shape;

typedef struct {
    int w, h;
    float hue, acc, lw;
    Shape s[SHAPES];
} Mystify;

static void *create(SDL_Renderer *r, int w, int h) {
    (void)r;
    Mystify *m = calloc(1, sizeof *m);
    m->w = w;
    m->h = h;
    m->lw = h / 270.0f > 1.5f ? h / 270.0f : 1.5f;
    m->hue = frand(0, 360);
    float sp = (float)(w < h ? w : h);
    for (int k = 0; k < SHAPES; k++)
        for (int i = 0; i < PTS; i++) {
            Shape *s = &m->s[k];
            s->x[i] = frand(0, (float)w);
            s->y[i] = frand(0, (float)h);
            s->vx[i] = frand(0.12f, 0.32f) * sp * (irand(2) ? 1 : -1);
            s->vy[i] = frand(0.12f, 0.32f) * sp * (irand(2) ? 1 : -1);
        }
    return m;
}

static void step(void *p, float dt) {
    Mystify *m = p;
    m->hue += 18.0f * dt;
    for (int k = 0; k < SHAPES; k++) {
        Shape *s = &m->s[k];
        for (int i = 0; i < PTS; i++) {
            s->x[i] += s->vx[i] * dt;
            s->y[i] += s->vy[i] * dt;
            if (s->x[i] < 0) { s->x[i] = 0; s->vx[i] = fabsf(s->vx[i]); }
            if (s->x[i] > m->w) { s->x[i] = (float)m->w; s->vx[i] = -fabsf(s->vx[i]); }
            if (s->y[i] < 0) { s->y[i] = 0; s->vy[i] = fabsf(s->vy[i]); }
            if (s->y[i] > m->h) { s->y[i] = (float)m->h; s->vy[i] = -fabsf(s->vy[i]); }
        }
    }
    m->acc += dt;
    if (m->acc >= 0.07f) {           /* record an echo of each shape */
        m->acc = 0;
        for (int k = 0; k < SHAPES; k++) {
            Shape *s = &m->s[k];
            s->head = (s->head + 1) % TRAIL;
            for (int i = 0; i < PTS; i++) {
                s->hist[s->head][i][0] = s->x[i];
                s->hist[s->head][i][1] = s->y[i];
            }
            if (s->nhist < TRAIL) s->nhist++;
        }
    }
}

static void draw(void *p, SDL_Renderer *r) {
    Mystify *m = p;
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderFillRect(r, NULL);
    for (int k = 0; k < SHAPES; k++) {
        Shape *s = &m->s[k];
        for (int e = s->nhist - 1; e >= 0; e--) {      /* oldest echo first */
            int idx = (s->head - e + TRAIL) % TRAIL;
            float fade = 1.0f - (float)e / TRAIL;
            SDL_Color c = hsv(m->hue + k * 150.0f, 0.9f, 0.15f + 0.85f * fade);
            for (int i = 0; i < PTS; i++) {
                int j = (i + 1) % PTS;
                thick_line(r, s->hist[idx][i][0], s->hist[idx][i][1],
                           s->hist[idx][j][0], s->hist[idx][j][1], m->lw, c);
            }
        }
    }
}

static void destroy(void *p) { free(p); }

const Saver saver_mystify = { "mystify", "MYSTIFY", create, step, draw, destroy };
