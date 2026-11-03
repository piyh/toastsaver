/* Germs: cells, germs and soap bubbles on a plain blue background. Soft blue cells drift about; spiky green germs chase
   them, and a cell a germ touches turns into a germ. Big white helper cells blow soap bubbles that
   wash germs back into healthy cells. Inspired by the Android live wallpaper Pixel Zombies. Tune it
   in toastsaver.conf: GERM_CELLS, GERM_HELPERS, GERM_SEEDS, GERM_SPEED and GERM_RANGE. When the
   germs run out a few cells catch them again; if every cell turns green, a wave of bubbles washes
   across the slide. A bar in the corner shows the mix, with a dot under it for each side's wins.
   With GERM_WRAP on, the slide has no edges: off one side and back on the other, like Pac-Man. */
#include "../engine.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXP 400
#define MAXSHOT 48
#define MAXPART 600

enum { K_CELL, K_HELPER, K_GERM };
enum { P_RING, P_DOT };

typedef struct {
    int kind, home;             /* what it is now, and what it goes back to when washed */
    float x, y, vx, vy, ph, spin;
    float jx, jy;               /* Brownian jiggle: a small drift that wanders smoothly */
    float tx, ty, think, rest; /* wander target, when to pick another, and a pause */
    float cool, immune, flash; /* bubble reload, can't be caught for a while, fade after a change */
} Cell;

typedef struct { float x, y, vx, vy, life; } Shot;
typedef struct { float x, y, vx, vy, life, max, size; int kind; uint32_t col; } Part;

typedef struct {
    int w, h;
    float u;                    /* one unit of drawing: a cell is about 10 units across */
    int cellWins, germWins;     /* how often the cells cleared the slide, and how often the germs took it */
    float t, noGerms, noCells, wave;
    int startCells, startHelpers;
    Cell c[MAXP];
    int n;
    Shot shot[MAXSHOT];
    Part part[MAXPART];
    int np;
    SDL_Rect hud;
} Germs;

/* ---------- tunables ---------- */

static float cell_drift(const Germs *g) { return 12.0f * g->u; }
static float cell_dash(const Germs *g) { return 26.0f * g->u; }
static float germ_speed(const Germs *g) { return 5.1f * g_config.germSpeed * g->u; }
static float bubble_range(const Germs *g) { return 13.0f * g_config.germRange * g->u; }
#define BUBBLE_SPEED 150.0f

/* With wrapping on, the shortest way from one point to another may go off an edge. */
static float wrapd(float d, float size) {
    return g_config.germWrap ? d - size * floorf(d / size + 0.5f) : d;
}

static float wrapp(float x, float size) {
    return g_config.germWrap ? x - size * floorf(x / size) : x;
}

/* ---------- particles ---------- */

static void burst(Germs *g, float x, float y, int kind, int n, float spread, float life, uint32_t col) {
    for (int i = 0; i < n && g->np < MAXPART; i++) {
        Part *q = &g->part[g->np++];
        float a = frand(0, 2 * PI_F), s = frand(0.2f, 1.0f) * spread * g->u;
        q->x = x;
        q->y = y;
        q->vx = kind == P_RING ? 0 : cosf(a) * s;
        q->vy = kind == P_RING ? 0 : sinf(a) * s;
        q->max = q->life = life * (kind == P_RING ? 1 : frand(0.7f, 1.2f));
        q->size = (kind == P_RING ? 5.0f : frand(0.6f, 1.4f)) * g->u;
        q->kind = kind;
        q->col = col;
    }
}

/* ---------- cells ---------- */

static void spawn(Germs *g, int kind, float x, float y) {
    if (g->n >= MAXP) return;
    Cell *q = &g->c[g->n++];
    memset(q, 0, sizeof *q);
    q->kind = q->home = kind;
    q->x = x;
    q->y = y;
    q->ph = frand(0, 6);
    q->spin = frand(-1, 1);
    q->think = frand(0, 2);
    q->tx = x;
    q->ty = y;
    q->cool = frand(0, 1);
}

static void spawn_inside(Germs *g, int kind) {
    spawn(g, kind, frand(0.04f * g->w, 0.96f * g->w), frand(0.06f * g->h, 0.96f * g->h));
}

static void infect(Cell *q) {
    q->kind = K_GERM;
    q->flash = 1;
    q->immune = 0;
    q->vx = q->vy = 0;
}

static void cure(Germs *g, Cell *q) {
    q->kind = q->home == K_GERM ? K_CELL : q->home;
    q->flash = 1;
    q->immune = 5.0f;
    burst(g, q->x, q->y, P_RING, 1, 0, 1.2f, 0xD8F6FF);
}

static void fire(Germs *g, Cell *from, const Cell *to) {
    for (int i = 0; i < MAXSHOT; i++) {
        Shot *s = &g->shot[i];
        if (s->life > 0) continue;
        float sp = BUBBLE_SPEED * g->u;
        float dx = to->x - from->x, dy = to->y - from->y, d = sqrtf(dx * dx + dy * dy) + 0.001f;
        float lead = d / sp, a = atan2f(dy + to->vy * lead, dx + to->vx * lead);
        if (irand(100) < 45) a += frand(-0.35f, 0.35f);   /* sometimes it just misses */
        s->x = from->x + cosf(a) * 7 * g->u;
        s->y = from->y + sinf(a) * 7 * g->u;
        s->vx = cosf(a) * sp;
        s->vy = sinf(a) * sp;
        s->life = (bubble_range(g) + 6 * g->u) / sp;
        from->cool = 2.0f + frand(0, 0.8f);
        return;
    }
}

/* Where a cell would like to go: away from, toward or just around. Writes a unit vector and speed. */
static void steer(Germs *g, Cell *q, const Cell *near, float ndx, float ndy, float nd, float *dx, float *dy,
                  float *speed, float dt) {
    float w = (float)g->w, h = (float)g->h, u = g->u;
    *dx = *dy = 0;
    *speed = 0;
    int sees = near && nd < (q->kind == K_GERM ? 100 * u : q->kind == K_HELPER ? bubble_range(g) + 14 * u : 58 * u);
    if (sees && q->kind == K_GERM) {                       /* chase the nearest cell */
        *dx = ndx;
        *dy = ndy;
        *speed = germ_speed(g);
    } else if (sees && q->kind == K_CELL) {                /* dash away from the nearest germ */
        *dx = -ndx;
        *dy = -ndy;
        *speed = cell_dash(g);
    } else if (sees && q->kind == K_HELPER) {              /* get in range, back off if too close */
        *dx = ndx;
        *dy = ndy;
        if (nd > bubble_range(g) * 0.85f) *speed = cell_drift(g) * 1.4f;
        else if (nd < 22 * u) { *dx = -*dx; *dy = -*dy; *speed = cell_dash(g); }
        else *speed = 0;
        if (q->cool <= 0 && nd <= bubble_range(g)) fire(g, q, near);
    } else {                                               /* drift */
        float tx = wrapd(q->tx - q->x, w), ty = wrapd(q->ty - q->y, h);
        if (q->rest > 0) q->rest -= dt;
        else {
            q->think -= dt;
            if (q->think <= 0 || tx * tx + ty * ty < 16 * u * u) {
                q->tx = frand(0.04f * w, 0.96f * w);
                q->ty = frand(0.05f * h, 0.96f * h);
                if (irand(3) == 0) { q->tx = q->x + frand(-30, 30) * u; q->ty = q->y + frand(-30, 30) * u; }
                q->think = frand(6.0f, 14.0f);
                if (irand(4) == 0) q->rest = frand(0.4f, 1.5f);
            }
            *dx = tx;
            *dy = ty;
            *speed = q->kind == K_GERM ? germ_speed(g) * 0.45f : cell_drift(g);
        }
    }
    /* stay on the slide: lean away from the edges, unless they wrap around */
    float m = 10 * u;
    float len = sqrtf(*dx * *dx + *dy * *dy);
    if (len > 0.001f) { *dx /= len; *dy /= len; }
    if (g_config.germWrap) return;
    if (q->x < m) *dx += (m - q->x) / m * 1.5f;
    if (q->x > w - m) *dx -= (q->x - (w - m)) / m * 1.5f;
    if (q->y < m) *dy += (m - q->y) / m * 1.5f;
    if (q->y > h - m) *dy -= (q->y - (h - m)) / m * 1.5f;
    len = sqrtf(*dx * *dx + *dy * *dy);
    if (len > 1) { *dx /= len; *dy /= len; }
}

static void step(void *pp, float dt) {
    Germs *g = pp;
    float u = g->u;
    if (dt > 0.1f) dt = 0.1f;
    g->t += dt;

    int ng = 0, nc = 0;
    for (int i = 0; i < g->n; i++) {
        Cell *q = &g->c[i];
        Cell *near = NULL;
        float best = 1e9f, ndx = 0, ndy = 0;
        for (int j = 0; j < g->n; j++) {
            Cell *o = &g->c[j];
            if (i == j || (q->kind == K_GERM) == (o->kind == K_GERM)) continue;
            float dx = wrapd(o->x - q->x, (float)g->w), dy = wrapd(o->y - q->y, (float)g->h), d = sqrtf(dx * dx + dy * dy);
            if (d < best) { best = d; near = o; ndx = dx; ndy = dy; }
        }
        if (q->kind == K_GERM) ng++; else nc++;
        q->cool -= dt;
        q->immune -= dt;
        q->flash -= dt * 0.7f;
        float dx, dy, sp;
        steer(g, q, near, ndx, ndy, best, &dx, &dy, &sp, dt);
        float want = sp * (0.9f + 0.1f * sinf(q->ph * 0.7f));
        q->vx += (dx * want - q->vx) * fminf(1, 4 * dt);
        q->vy += (dy * want - q->vy) * fminf(1, 4 * dt);
        /* a little Brownian motion: a slow random drift (an Ornstein-Uhlenbeck walk) that turns over every
           few seconds and averages about 4 units a second, so paths meander instead of jittering */
        float kick = 4 * u * sqrtf(3 * dt);
        q->jx += -0.5f * q->jx * dt + frand(-kick, kick);
        q->jy += -0.5f * q->jy * dt + frand(-kick, kick);
        q->x = wrapp(q->x + (q->vx + q->jx) * dt, (float)g->w);
        q->y = wrapp(q->y + (q->vy + q->jy) * dt, (float)g->h);
        q->ph += dt * (0.5f + 0.2f * q->spin);
        /* a catch: a germ touching a cell that wasn't just washed */
        if (q->kind == K_GERM && near && best < 8 * u && near->immune <= 0 && q->flash <= 0) infect(near);
    }
    /* keep cells from piling on top of each other */
    for (int i = 0; i < g->n; i++)
        for (int j = i + 1; j < g->n; j++) {
            Cell *a = &g->c[i], *b = &g->c[j];
            float dx = wrapd(b->x - a->x, (float)g->w), dy = wrapd(b->y - a->y, (float)g->h), d2 = dx * dx + dy * dy, m = 9 * u;
            if (d2 < m * m && d2 > 0.001f && (a->kind == K_GERM) == (b->kind == K_GERM)) {
                float d = sqrtf(d2), push = (m - d) / m * 90 * u * dt;
                a->vx -= dx / d * push; a->vy -= dy / d * push;
                b->vx += dx / d * push; b->vy += dy / d * push;
            }
        }
    /* soap bubbles */
    for (int i = 0; i < MAXSHOT; i++) {
        Shot *s = &g->shot[i];
        if (s->life <= 0) continue;
        s->x = wrapp(s->x + s->vx * dt, (float)g->w);
        s->y = wrapp(s->y + s->vy * dt, (float)g->h);
        s->life -= dt;
        int hit = 0;
        for (int j = 0; j < g->n && !hit; j++) {
            Cell *q = &g->c[j];
            float dx = wrapd(q->x - s->x, (float)g->w), dy = wrapd(q->y - s->y, (float)g->h);
            if (q->kind == K_GERM && dx * dx + dy * dy < 6 * u * 6 * u) { cure(g, q); hit = 1; }
        }
        if (hit || s->life <= 0) {
            s->life = 0;
        }
    }
    /* particles */
    for (int i = 0; i < g->np; i++) {
        Part *q = &g->part[i];
        q->life -= dt;
        q->x += q->vx * dt;
        q->y += q->vy * dt;
        q->vx *= 1 - 1.5f * dt;
        q->vy *= 1 - 1.5f * dt;
        if (q->kind == P_RING) q->size += 9 * u * dt;
        if (q->life <= 0) g->part[i--] = g->part[--g->np];
    }
    /* keep the story going */
    g->noGerms = ng == 0 ? g->noGerms + dt : 0;
    g->noCells = nc == 0 ? g->noCells + dt : 0;
    if (g->noGerms > 4 && g_config.germSeeds > 0) {
        for (int i = 0; i < g_config.germSeeds && nc > 0; i++) {   /* a few cells catch the germs again */
            Cell *q = &g->c[irand(g->n)];
            if (q->kind == K_GERM) { i--; continue; }
            infect(q);
            nc--;
        }
        g->noGerms = 0;
        g->cellWins++;
    } else if (g->noGerms > 4) {
        g->noGerms = 0;   /* zero seeds means the germs are not coming back */
    }
    if (g->noCells > 5 && g->wave <= 0) { g->wave = 1; g->germWins++; }   /* all germs: a wave of bubbles */
    if (g->wave > 0) {
        g->wave += g->w * 0.22f * dt;
        for (int i = 0; i < g->n; i++)
            if (g->c[i].kind == K_GERM && g->c[i].x < g->wave) cure(g, &g->c[i]);
        if (g->wave > g->w + 40 * u) { g->wave = 0; g->noCells = 0; }
    }
}

/* ---------- drawing ---------- */

static SDL_Color col(uint32_t hex, float a) {
    SDL_Color c = hexcol(hex);
    c.a = (Uint8)(a < 0 ? 0 : a > 1 ? 255 : a * 255);
    return c;
}

static void ring(SDL_Renderer *r, float cx, float cy, float rad, float width, SDL_Color c) {
    enum { N = 24 };
    for (int i = 0; i < N; i++) {
        float a0 = i * 2 * PI_F / N, a1 = (i + 1) * 2 * PI_F / N;
        thick_line(r, cx + cosf(a0) * rad, cy + sinf(a0) * rad, cx + cosf(a1) * rad, cy + sinf(a1) * rad, width, c);
    }
}

static void draw_healthy(SDL_Renderer *r, const Germs *g, const Cell *q, float a) {
    float u = g->u, rad = 4.4f * u;
    fill_circle(r, q->x, q->y, rad * 1.25f, col(0x8FE3FF, 0.18f * a));           /* soft membrane glow */
    fill_circle(r, q->x, q->y, rad, col(0x5EC4EC, 0.85f * a));
    fill_circle(r, q->x + q->spin * 0.8f * u, q->y + 0.4f * u, 1.6f * u, col(0x2C76A8, 0.9f * a));   /* nucleus */
    fill_circle(r, q->x - rad * 0.4f, q->y - rad * 0.45f, 0.9f * u, col(0xFFFFFF, 0.45f * a));
}

static void draw_germ(SDL_Renderer *r, const Germs *g, const Cell *q, float a) {
    float u = g->u, rad = 3.6f * u, rot = q->spin * 3 + q->ph * 0.25f * (q->spin < 0 ? -1 : 1);
    SDL_Color body = col(0x9BE04A, a), dark = col(0x5E9E26, a);
    for (int i = 0; i < 8; i++) {                                                 /* spikes with knobs */
        float ang = rot + i * 2 * PI_F / 8, len = rad * 1.5f;
        thick_line(r, q->x, q->y, q->x + cosf(ang) * len, q->y + sinf(ang) * len, 1.1f * u, dark);
        fill_circle(r, q->x + cosf(ang) * len, q->y + sinf(ang) * len, 0.95f * u, body);
    }
    fill_circle(r, q->x, q->y, rad, body);
    fill_circle(r, q->x + cosf(rot) * 1.4f * u, q->y + sinf(rot) * 1.4f * u, 0.9f * u, dark);
    fill_circle(r, q->x + cosf(rot + 2.4f) * 1.6f * u, q->y + sinf(rot + 2.4f) * 1.6f * u, 0.6f * u, dark);
    fill_circle(r, q->x - rad * 0.35f, q->y - rad * 0.4f, 0.8f * u, col(0xE8FFC8, 0.5f * a));
}

static void draw_helper(SDL_Renderer *r, const Germs *g, const Cell *q) {
    float u = g->u, rad = 5.6f * u;
    fill_circle(r, q->x, q->y, rad * 1.3f, col(0xFFF6E0, 0.14f));
    for (int i = 0; i < 6; i++) {                                                 /* lumpy outline */
        float ang = q->spin * 3 + i * 2 * PI_F / 6, k = 1.3f + 0.4f * sinf(i * 2.1f);
        fill_circle(r, q->x + cosf(ang) * k * u, q->y + sinf(ang) * k * u, rad * 0.82f, col(0xF4F0E4, 0.92f));
    }
    for (int i = 0; i < 3; i++) {                                                 /* lobed nucleus */
        float ang = q->spin * 3 + i * 2.1f;
        fill_circle(r, q->x + cosf(ang) * 1.5f * u, q->y + sinf(ang) * 1.5f * u, 1.5f * u, col(0xA48BD0, 0.95f));
    }
}

static void draw_cell(SDL_Renderer *r, const Germs *g, const Cell *q) {
    float f = q->flash > 0 ? q->flash : 0;
    if (q->kind == K_CELL) {
        draw_healthy(r, g, q, 1);
        if (f > 0) draw_germ(r, g, q, f);            /* just washed: the germ fades away */
    } else if (q->kind == K_HELPER) {
        draw_helper(r, g, q);
        if (f > 0) draw_germ(r, g, q, f);
    }
    else {
        draw_germ(r, g, q, 1 - f);
        if (f > 0) draw_healthy(r, g, q, f);         /* just caught: the cell fades into a germ */
    }
}

static void bubble(SDL_Renderer *r, float x, float y, float rad, float a, float u) {
    fill_circle(r, x, y, rad, col(0xD8F6FF, 0.15f * a));
    ring(r, x, y, rad, 0.7f * u, col(0xCDEFFF, 0.75f * a));
    fill_circle(r, x - rad * 0.4f, y - rad * 0.4f, rad * 0.25f, col(0xFFFFFF, 0.8f * a));
}

static void draw(void *pp, SDL_Renderer *r) {
    Germs *g = pp;
    float u = g->u, w = (float)g->w, h = (float)g->h;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    vgradient(r, g->w, g->h, hexcol(0x103848), hexcol(0x08202C));

    for (int pass = 0; pass < 3; pass++)                                          /* cells, then helpers, then germs */
        for (int i = 0; i < g->n; i++) {
            if (g->c[i].kind != (pass == 0 ? K_CELL : pass == 1 ? K_HELPER : K_GERM)) continue;
            /* near a wrapping edge, also draw the part that pokes out on the other side */
            float m = 10 * u;
            int wrap = g_config.germWrap, ex = 0, ey = 0;
            if (wrap) {
                ex = g->c[i].x < m ? 1 : g->c[i].x > w - m ? -1 : 0;
                ey = g->c[i].y < m ? 1 : g->c[i].y > h - m ? -1 : 0;
            }
            for (int k = 0; k < 4; k++) {
                int ox = k & 1, oy = k >> 1;
                if ((ox && !ex) || (oy && !ey)) continue;
                Cell q = g->c[i];
                q.x += ox * ex * w;
                q.y += oy * ey * h;
                draw_cell(r, g, &q);
            }
        }

    for (int i = 0; i < MAXSHOT; i++) {
        const Shot *s = &g->shot[i];
        if (s->life > 0) bubble(r, s->x, s->y, 2.4f * u, fminf(1, s->life * 4), u);
    }
    for (int i = 0; i < g->np; i++) {
        const Part *q = &g->part[i];
        float k = q->life / q->max;
        if (q->kind == P_RING) ring(r, q->x, q->y, q->size, 0.7f * u, col(q->col, k * 0.5f));
        else fill_circle(r, q->x, q->y, q->size * 0.7f, col(q->col, k));
    }
    if (g->wave > 0) {                                                             /* a wall of bubbles sweeps across */
        for (int i = 0; i < 24; i++) {
            float by = fmodf(i * 37.7f * u + g->t * 6 * u, h + 20 * u) - 10 * u;
            float bx = g->wave - fmodf(i * 13.3f, 30) * u;
            bubble(r, bx, by, (3 + fmodf(i * 1.7f, 3)) * u, 1, u);
        }
    }

    /* the mix: a bar of cells, helpers and germs, and a dot for each win underneath */
    int counts[3] = { 0, 0, 0 };
    for (int i = 0; i < g->n; i++) counts[g->c[i].kind]++;
    static const uint32_t BAR[3] = { 0x5EC4EC, 0xF4F0E4, 0x9BE04A };
    float bx = 10 * u, by = 9 * u, bw = fminf(w * 0.28f, 150 * u), bh = 3.2f * u, x = bx;
    SDL_Rect frame = { (int)(bx - u), (int)(by - u), (int)(bw + 2 * u), (int)(bh + 2 * u) };
    fill_rrect(r, frame, (int)(2 * u), col(0x000000, 0.35f));
    for (int k = 0; k < 3; k++) {
        float sw = g->n ? bw * counts[k] / g->n : 0;
        if (sw >= 1) {
            SDL_Rect seg = { (int)x, (int)by, (int)ceilf(sw), (int)bh };
            set_color(r, col(BAR[k], 0.95f));
            SDL_RenderFillRect(r, &seg);
        }
        x += sw;
    }
    float dy = by + bh + 4.5f * u, dr = 1.3f * u, gap = 3.6f * u;
    int maxDots = (int)(bw / 2 / gap);
    for (int i = 0; i < g->cellWins && i < maxDots; i++) fill_circle(r, bx + dr + i * gap, dy, dr, col(BAR[0], 0.9f));
    for (int i = 0; i < g->germWins && i < maxDots; i++) fill_circle(r, bx + bw - dr - i * gap, dy, dr, col(BAR[2], 0.9f));
    g->hud = (SDL_Rect){ (int)(bx - 4 * u), (int)(by - 4 * u), (int)(bw + 8 * u), (int)(dy - by + 8 * u) };

    if (g_show_info) {
        char line[96];
        int sc = g->h / 360 > 2 ? g->h / 360 : 2;
        snprintf(line, sizeof line, "CELLS %d  HELPERS %d  GERMS %d   CELL WINS %d  GERM WINS %d",
                 counts[K_CELL], counts[K_HELPER], counts[K_GERM], g->cellWins, g->germWins);
        draw_text_shadow(r, line, g->hud.x, g->hud.y + g->hud.h + 2 * sc, sc, hexcol(0xFFFFFF));
    }
}

static int keep_clear(void *pp, SDL_Rect *out, int max) {
    Germs *g = pp;
    if (max < 1) return 0;
    out[0] = g->hud;
    return 1;
}

/* ---------- lifecycle ---------- */

static void *create(SDL_Renderer *r, int w, int h) {
    (void)r;
    Germs *g = calloc(1, sizeof *g);
    g->w = w;
    g->h = h;
    g->u = h / 300.0f > 0.3f ? h / 300.0f : 0.3f;
    g->startCells = g_config.germCells;
    g->startHelpers = g_config.germHelpers;
    if (g->startCells + g->startHelpers + g_config.germSeeds < 1) g->startCells = 1;
    for (int i = 0; i < g->startCells; i++) spawn_inside(g, K_CELL);
    for (int i = 0; i < g->startHelpers; i++) spawn_inside(g, K_HELPER);
    for (int i = 0; i < g_config.germSeeds; i++) spawn_inside(g, K_GERM);
    return g;
}

static void destroy(void *pp) { free(pp); }

const Saver saver_germs = { "germs", "GERMS", create, step, draw, destroy, NULL, keep_clear };
