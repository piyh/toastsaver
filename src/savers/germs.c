/* Germs: cells, germs and soap bubbles on a plain blue background. Soft blue cells drift about; spiky green germs chase
   them, and a cell a germ touches turns into a germ. Big white helper cells (the white blood cells)
   blow soap bubbles; a bubble pops with a little splash, and every germ in the splash pops too.
   With GERM_POP off it is the way it first was: a bubble washes the one germ it hits back into
   a healthy cell. Healthy cells divide to fill the
   slide back up. Inspired by the Android live wallpaper Pixel Zombies. Its settings are in
   GERM_OPTIONS at the end: the settings page has them, and in a window O opens them live.
   When the germs run out a few cells catch them again; if every cell turns green, a wave of
   bubbles washes across the slide. A bar in the corner shows the mix, with a dot under it for
   each side's wins. With GERM_WRAP on, the slide has no edges: off one side and back on the
   other, like Pac-Man. */
#include "../engine.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXP 400
#define MAXSHOT 48
#define MAXPART 600
#define MAXTRAIL 4096       /* trail dots: a germ drops one every TRAIL_EVERY seconds */
#define TRAIL_EVERY 0.2f

enum { K_CELL, K_HELPER, K_GERM };
enum { P_RING, P_DOT, P_SPLASH };

/* Settings (GERM_OPTIONS below): read live, except the ones marked OPT_RESTART. */
static int o_cells = 60, o_helpers = 2, o_seeds = 5, o_max = 70, o_helperSpeed = 5, o_speed = 6, o_sight = 5,
           o_range = 4, o_reload = 5, o_splash = 3, o_pop = 1, o_trail = 5, o_growth = 4, o_wrap = 1;

typedef struct {
    int kind, home;             /* what it is now, and what it goes back to when washed */
    float x, y, vx, vy, ph, spin;
    float jx, jy;               /* Brownian jiggle: a small drift that wanders smoothly */
    float tx, ty, think, rest; /* wander target, when to pick another, and a pause */
    float cool, immune, flash; /* bubble reload, can't be caught for a while, fade after a change */
    float age;                  /* seconds since it appeared: a new cell from a division grows in */
    float drop;                 /* a germ: when it drops its next trail dot */
    float sniff, sx, sy;        /* a helper: when it next looks for a trail, and the dot it is heading for */
    int onTrail;
    int dead;                   /* popped: taken out at the end of the step */
    float coolMax;              /* a helper: how long this reload takes, for the preview's countdown ring */
} Cell;

/* Something the settings preview points out for a moment: a bubble's splash, a germ washed
   clean, a cell dividing. */
enum { EV_SPLASH, EV_WASH, EV_BORN };
typedef struct { float x, y, rad, t; int kind; } Event;
#define MAXEV 16

typedef struct { float x, y, vx, vy, life; } Shot;
typedef struct { float x, y, t; } Dot;      /* a faint dot of germ trail, dropped at time t */
typedef struct { float x, y, vx, vy, life, max, size; int kind; uint32_t col; } Part;

typedef struct {
    int w, h;
    float u;                    /* one unit of drawing: a cell is about 10 units across */
    int cellWins, germWins;     /* how often the cells cleared the slide, and how often the germs took it */
    int popped, born;           /* germs popped and cells born from a division, for the read-out */
    int demo, cap;              /* the settings preview's close-up scene, which keeps to `cap` critters */
    Event ev[MAXEV];
    int nextEv;
    float t, noGerms, noCells, wave;
    int startCells, startHelpers;
    Cell c[MAXP];
    int n;
    Shot shot[MAXSHOT];
    Part part[MAXPART];
    int np;
    Dot trail[MAXTRAIL];        /* a ring: the newest dot replaces the oldest */
    int trailNext;
    SDL_Rect hud;
} Germs;

/* ---------- tunables ---------- */

/* Speeds: helper speed 5 is how the saver always moved. A dashing cell is about as fast as a germ at 5. */
static float cell_drift(const Germs *g) { return 12.0f * g->u; }
static float cell_dash(const Germs *g) { return 26.0f * g->u; }
static float helper_drift(const Germs *g) { return 2.4f * o_helperSpeed * g->u; }
static float helper_chase(const Germs *g) { return 3.36f * o_helperSpeed * g->u; }
static float helper_dash(const Germs *g) { return 5.2f * o_helperSpeed * g->u; }
/* How far each kind notices the other side, in units (a cell is about 10 across). GERM_SIGHT 5:
   germs 100, helpers 70, cells 60; the setting scales all three together. */
static float sight(const Germs *g, int kind) {
    return (kind == K_GERM ? 100.0f : kind == K_HELPER ? 70.0f : 60.0f) * o_sight / 5.0f * g->u;
}
static float trail_life(void) { return 1.2f * o_trail; }   /* seconds a trail dot lasts: 6 at GERM_TRAIL 5 */
static float germ_speed(const Germs *g) { return 5.1f * o_speed * g->u; }
static float bubble_range(const Germs *g) { return 13.0f * o_range * g->u; }
/* GERM_SPLASH 0 pops only the germ a bubble touches; 3 reaches about a germ's width past it; 10, four cells */
static float splash_radius(const Germs *g) { return o_splash ? (5.0f + 2.5f * o_splash) * g->u : 0; }
static float bubble_speed(const Germs *g) { return 150.0f * g->u; }
#define HIT_RADIUS 6.0f   /* units: how close a bubble must come to a germ to pop */

/* With wrapping on, the shortest way from one point to another may go off an edge. */
static float wrapd(float d, float size) {
    return o_wrap ? d - size * floorf(d / size + 0.5f) : d;
}

static float wrapp(float x, float size) {
    return o_wrap ? x - size * floorf(x / size) : x;
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
        q->size = (kind == P_RING ? 5.0f : kind == P_SPLASH ? spread : frand(0.6f, 1.4f)) * g->u;
        if (kind == P_SPLASH) q->vx = q->vy = 0;
        q->kind = kind;
        q->col = col;
    }
}

/* ---------- cells ---------- */

static void note(Germs *g, int kind, float x, float y, float rad) {
    g->ev[g->nextEv] = (Event){ x, y, rad, g->t > 0 ? g->t : 0.001f, kind };
    g->nextEv = (g->nextEv + 1) % MAXEV;
}

/* The most on the slide at once: GERM_MAX, or the preview scene's own few. */
static int slide_room(const Germs *g) {
    int m = g->demo ? g->cap : o_max;
    return m < MAXP ? m : MAXP;
}

static void spawn(Germs *g, int kind, float x, float y) {
    if (g->n >= slide_room(g)) return;                     /* GERM_MAX: the most on the slide at once */
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
    q->age = 10;
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

/* A bubble pops a germ: it bursts into sparkles and is gone. A helper that caught a germ is too
   big to pop, so it is washed back instead. */
static void pop(Germs *g, Cell *q) {
    if (q->home == K_HELPER) { cure(g, q); return; }
    q->dead = 1;
    g->popped++;
    burst(g, q->x, q->y, P_DOT, 10, 30, 0.7f, 0xB8F070);
    burst(g, q->x, q->y, P_DOT, 4, 18, 0.5f, 0xFFFFFF);
}

/* Where a bubble bursts: the germ it touched, and every germ within the splash. With GERM_POP
   off, the germ it touched is washed back into a cell, and that's all. */
static void splash(Germs *g, float x, float y, Cell *hit) {
    if (!o_pop) { cure(g, hit); note(g, EV_WASH, hit->x, hit->y, HIT_RADIUS * g->u); return; }
    float rad = splash_radius(g);
    note(g, EV_SPLASH, x, y, rad > 0 ? rad : HIT_RADIUS * g->u);
    pop(g, hit);
    if (rad > 0) {
        burst(g, x, y, P_SPLASH, 1, rad / g->u, 0.35f, 0xD8F6FF);
        for (int j = 0; j < g->n; j++) {
            Cell *q = &g->c[j];
            if (q == hit || q->kind != K_GERM || q->dead) continue;
            float dx = wrapd(q->x - x, (float)g->w), dy = wrapd(q->y - y, (float)g->h);
            if (dx * dx + dy * dy < rad * rad) pop(g, q);
        }
    }
}

/* A healthy cell splits in two: the new one buds off to the side and grows in. */
static void divide(Germs *g, Cell *q) {
    if (g->n >= slide_room(g)) return;
    float a = frand(0, 2 * PI_F), u = g->u;
    float x = wrapp(q->x + cosf(a) * 3 * u, (float)g->w), y = wrapp(q->y + sinf(a) * 3 * u, (float)g->h);
    spawn(g, K_CELL, x, y);
    Cell *b = &g->c[g->n - 1];
    b->age = 0;
    b->immune = 1.5f;
    b->vx = cosf(a) * 14 * u;
    b->vy = sinf(a) * 14 * u;
    q->vx -= cosf(a) * 14 * u;
    q->vy -= sinf(a) * 14 * u;
    g->born++;
    note(g, EV_BORN, q->x, q->y, 6 * u);
}

static void fire(Germs *g, Cell *from, const Cell *to) {
    for (int i = 0; i < MAXSHOT; i++) {
        Shot *s = &g->shot[i];
        if (s->life > 0) continue;
        float sp = bubble_speed(g);
        float dx = to->x - from->x, dy = to->y - from->y, d = sqrtf(dx * dx + dy * dy) + 0.001f;
        float lead = d / sp, a = atan2f(dy + to->vy * lead, dx + to->vx * lead);
        if (irand(100) < 45) a += frand(-0.35f, 0.35f);   /* sometimes it just misses */
        s->x = from->x + cosf(a) * 7 * g->u;
        s->y = from->y + sinf(a) * 7 * g->u;
        s->vx = cosf(a) * sp;
        s->vy = sinf(a) * sp;
        s->life = (bubble_range(g) + 6 * g->u) / sp;
        from->cool = from->coolMax = (2.0f + frand(0, 0.8f)) * 5.0f / o_reload;   /* GERM_RELOAD 5: one every 2-3 seconds */
        return;
    }
}

/* ---------- trails ---------- */

static void drop_dot(Germs *g, const Cell *q) {
    Dot *d = &g->trail[g->trailNext];
    g->trailNext = (g->trailNext + 1) % MAXTRAIL;
    d->x = q->x;
    d->y = q->y;
    d->t = g->t;
}

/* A helper with no germ in sight sniffs for a trail now and then: the freshest dot it can see
   that isn't right under it. Newer dots lie further along, toward the germ, so going from
   freshest to freshest walks it up the trail. Returns 1 while it has a dot to head for. */
static int follow_trail(Germs *g, Cell *q, float dt) {
    if (!o_trail) return q->onTrail = 0;
    float u = g->u, life = trail_life();
    q->sniff -= dt;
    if (q->onTrail) {                                    /* reached the dot, or it faded: look again */
        float dx = wrapd(q->sx - q->x, (float)g->w), dy = wrapd(q->sy - q->y, (float)g->h);
        if (dx * dx + dy * dy < 4 * u * 4 * u) q->sniff = 0;
    }
    if (q->sniff > 0) return q->onTrail;
    q->sniff = 0.3f;
    float see = sight(g, K_HELPER), best = -1;
    for (int i = 0; i < MAXTRAIL; i++) {
        const Dot *d = &g->trail[i];
        if (d->t <= 0 || g->t - d->t > life || d->t <= best) continue;
        float dx = wrapd(d->x - q->x, (float)g->w), dy = wrapd(d->y - q->y, (float)g->h), d2 = dx * dx + dy * dy;
        if (d2 > see * see || d2 < 5 * u * 5 * u) continue;
        best = d->t;
        q->sx = d->x;
        q->sy = d->y;
    }
    return q->onTrail = best > 0;
}

/* Where a cell would like to go: away from, toward or just around. Writes a unit vector and speed. */
static void steer(Germs *g, Cell *q, const Cell *near, float ndx, float ndy, float nd, float *dx, float *dy,
                  float *speed, float dt) {
    float w = (float)g->w, h = (float)g->h, u = g->u;
    *dx = *dy = 0;
    *speed = 0;
    int sees = near && nd < sight(g, q->kind);
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
        if (nd > bubble_range(g) * 0.85f) *speed = helper_chase(g);
        else if (nd < 22 * u) { *dx = -*dx; *dy = -*dy; *speed = helper_dash(g); }
        else *speed = 0;
        if (q->cool <= 0 && nd <= bubble_range(g)) fire(g, q, near);
        q->onTrail = 0;
    } else if (q->kind == K_HELPER && follow_trail(g, q, dt)) {   /* no germ in sight: follow its trail */
        *dx = wrapd(q->sx - q->x, w);
        *dy = wrapd(q->sy - q->y, h);
        *speed = helper_dash(g) * 1.2f;                    /* hurrying: a fresh trail means a germ is near */
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
            *speed = q->kind == K_GERM ? germ_speed(g) * 0.45f : q->kind == K_HELPER ? helper_drift(g) : cell_drift(g);
        }
    }
    /* stay on the slide: lean away from the edges, unless they wrap around */
    float m = 10 * u;
    float len = sqrtf(*dx * *dx + *dy * *dy);
    if (len > 0.001f) { *dx /= len; *dy /= len; }
    if (o_wrap) return;
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

    int ng = 0, nc = 0, healthy = 0;
    for (int i = 0; i < g->n; i++) {
        Cell *q = &g->c[i];
        q->age += dt;
        Cell *near = NULL;
        float best = 1e9f, ndx = 0, ndy = 0;
        for (int j = 0; j < g->n; j++) {
            Cell *o = &g->c[j];
            if (i == j || (q->kind == K_GERM) == (o->kind == K_GERM)) continue;
            float dx = wrapd(o->x - q->x, (float)g->w), dy = wrapd(o->y - q->y, (float)g->h), d = sqrtf(dx * dx + dy * dy);
            if (d < best) { best = d; near = o; ndx = dx; ndy = dy; }
        }
        if (q->kind == K_GERM) ng++; else nc++;
        if (q->kind == K_CELL) healthy++;
        q->cool -= dt;
        q->immune -= dt;
        if (q->kind == K_GERM && o_trail && (q->drop -= dt) <= 0) { drop_dot(g, q); q->drop = TRAIL_EVERY; }
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
            if (q->kind == K_GERM && !q->dead && dx * dx + dy * dy < HIT_RADIUS * u * HIT_RADIUS * u) { splash(g, s->x, s->y, q); hit = 1; }
        }
        if (hit || s->life <= 0) {
            s->life = 0;
        }
    }
    /* popped germs leave; healthy cells divide to refill the slide, faster the emptier it is */
    for (int i = 0; i < g->n; i++)
        if (g->c[i].dead) g->c[i--] = g->c[--g->n];
    int room = slide_room(g);                                      /* GERM_MAX: divisions fill the slide up to it */
    if (o_growth > 0 && g->n < room && healthy > 0) {
        float want = 0.012f * o_growth * healthy * (1 - (float)g->n / room) * dt;   /* divisions this step */
        while (want > 0 && g->n < room) {
            if (frand(0, 1) < want) {
                Cell *q = &g->c[irand(g->n)];
                if (q->kind == K_CELL && q->age > 3) divide(g, q);
            }
            want -= 1;
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
        if (q->kind == P_RING) q->size += 9 * u * dt;   /* (a splash keeps its size: draw grows it) */
        if (q->life <= 0) g->part[i--] = g->part[--g->np];
    }
    /* keep the story going */
    g->noGerms = ng == 0 ? g->noGerms + dt : 0;
    g->noCells = healthy == 0 && ng > 0 ? g->noCells + dt : 0;   /* the germs win when no healthy cell is left */
    (void)nc;
    int seeds = g->demo ? 2 : o_seeds;
    float wait = g->demo ? 1.5f : 4;         /* the preview keeps something happening */
    if (g->noGerms > wait && seeds > 0) {
        for (int i = 0; i < seeds && healthy > 0; i++) {   /* a few healthy cells catch the germs again */
            Cell *q = &g->c[irand(g->n)];
            if (q->kind != K_CELL || q->dead) { i--; continue; }
            infect(q);
            healthy--;
        }
        g->noGerms = 0;
        g->cellWins++;
    } else if (g->noGerms > wait) {
        g->noGerms = 0;   /* zero seeds means the germs are not coming back */
    }
    if (g->noCells > (g->demo ? 2 : 5) && g->wave <= 0) { g->wave = 1; g->germWins++; }   /* all germs: a wave of bubbles */
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
    float grow = q->age < 1 ? 0.55f + 0.45f * q->age : 1;     /* a new cell from a division grows in */
    float u = g->u * grow, rad = 4.4f * u;
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

    if (o_trail) {                                                                 /* faint green germ trails */
        float life = trail_life();
        for (int i = 0; i < MAXTRAIL; i++) {
            const Dot *d = &g->trail[i];
            float age = g->t - d->t;
            if (d->t <= 0 || age > life) continue;
            float k = 1 - age / life;
            fill_circle(r, d->x, d->y, (0.7f + 0.5f * k) * u, col(0x9BE04A, 0.14f * k));
        }
    }

    for (int pass = 0; pass < 3; pass++)                                          /* cells, then helpers, then germs */
        for (int i = 0; i < g->n; i++) {
            if (g->c[i].kind != (pass == 0 ? K_CELL : pass == 1 ? K_HELPER : K_GERM)) continue;
            /* near a wrapping edge, also draw the part that pokes out on the other side */
            float m = 10 * u;
            int wrap = o_wrap, ex = 0, ey = 0;
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
        else if (q->kind == P_SPLASH) {                                            /* a quick ring out to the splash's edge */
            float rad = q->size * (0.35f + 0.65f * sqrtf(1 - k));
            fill_circle(r, q->x, q->y, rad, col(q->col, 0.12f * k));
            ring(r, q->x, q->y, rad, 0.8f * u, col(q->col, 0.8f * k));
        }
        else fill_circle(r, q->x, q->y, q->size * 0.7f, col(q->col, k));
    }
    if (g->wave > 0) {                                                             /* a wall of bubbles sweeps across */
        for (int i = 0; i < 24; i++) {
            float by = fmodf(i * 37.7f * u + g->t * 6 * u, h + 20 * u) - 10 * u;
            float bx = g->wave - fmodf(i * 13.3f, 30) * u;
            bubble(r, bx, by, (3 + fmodf(i * 1.7f, 3)) * u, 1, u);
        }
    }

    if (g->demo) return;                     /* the preview's close-up has no bar */
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
        char line[128];
        int sc = g->h / 360 > 2 ? g->h / 360 : 2;
        snprintf(line, sizeof line, "CELLS %d  HELPERS %d  GERMS %d   POPPED %d  BORN %d   CELL WINS %d  GERM WINS %d",
                 counts[K_CELL], counts[K_HELPER], counts[K_GERM], g->popped, g->born, g->cellWins, g->germWins);
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
    g->startCells = o_cells;
    g->startHelpers = o_helpers;
    if (g->startCells + g->startHelpers + o_seeds < 1) g->startCells = 1;
    /* helpers and germs first, so that with a low GERM_MAX it is the healthy cells that are fewer */
    for (int i = 0; i < g->startHelpers; i++) spawn_inside(g, K_HELPER);
    for (int i = 0; i < o_seeds; i++) spawn_inside(g, K_GERM);
    for (int i = 0; i < g->startCells; i++) spawn_inside(g, K_CELL);
    return g;
}

/* The settings preview: a close-up with a few of each, big enough to see what a setting does. */
static void *demo(SDL_Renderer *r, int w, int h) {
    (void)r;
    Germs *g = calloc(1, sizeof *g);
    g->w = w;
    g->h = h;
    g->u = h / 170.0f > 0.3f ? h / 170.0f : 0.3f;
    g->demo = 1;
    g->cap = 12;
    spawn_inside(g, K_HELPER);
    for (int i = 0; i < 2; i++) spawn_inside(g, K_GERM);
    for (int i = 0; i < 9; i++) spawn_inside(g, K_CELL);
    g->startCells = 9;
    g->startHelpers = 1;
    return g;
}

static void destroy(void *pp) { free(pp); }

/* ---------- showing what a setting does (the settings preview and the windowed panel) ---------- */

static const uint32_t KIND_COL[3] = { 0x8FE3FF, 0xFFF6E0, 0xB8F070 };

static void arrow(SDL_Renderer *r, float x, float y, float dx, float dy, float u, SDL_Color c) {
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 2 * u) return;
    float ux = dx / len, uy = dy / len, hx = x + dx, hy = y + dy, k = 2.6f * u;
    thick_line(r, x, y, hx, hy, 0.9f * u, c);
    fill_triangle(r, hx + ux * k, hy + uy * k, hx - uy * k * 0.7f, hy + ux * k * 0.7f, hx + uy * k * 0.7f, hy - ux * k * 0.7f, c);
}

/* Lines from each critter to the nearest one on the other side it can see. */
static void sight_lines(const Germs *g, SDL_Renderer *r) {
    for (int i = 0; i < g->n; i++) {
        const Cell *q = &g->c[i];
        float best = sight(g, q->kind), bx = 0, by = 0;
        int found = 0;
        for (int j = 0; j < g->n; j++) {
            const Cell *o = &g->c[j];
            if ((q->kind == K_GERM) == (o->kind == K_GERM)) continue;
            float dx = wrapd(o->x - q->x, (float)g->w), dy = wrapd(o->y - q->y, (float)g->h), d = sqrtf(dx * dx + dy * dy);
            if (d < best) { best = d; bx = dx; by = dy; found = 1; }
        }
        if (found) thick_line(r, q->x, q->y, q->x + bx, q->y + by, 0.5f * g->u, col(KIND_COL[q->kind], 0.75f));
    }
}

static void explain(void *pp, SDL_Renderer *r, const SaverOption *focus) {
    Germs *g = pp;
    if (!focus || !focus->key) return;
    const char *k = focus->key + 5;            /* after "GERM_" */
    float u = g->u;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    if (!strcmp(k, "SIGHT")) {                 /* a circle of how far each one sees, and a line to what it saw */
        for (int i = 0; i < g->n; i++) {
            const Cell *q = &g->c[i];
            float rad = sight(g, q->kind);
            fill_circle(r, q->x, q->y, rad, col(KIND_COL[q->kind], 0.04f));
            ring(r, q->x, q->y, rad, 0.5f * u, col(KIND_COL[q->kind], 0.35f));
        }
        sight_lines(g, r);
    } else if (!strcmp(k, "SPEED") || !strcmp(k, "HELPER_SPEED")) {   /* arrows: where they're going, and how fast */
        int kind = !strcmp(k, "SPEED") ? K_GERM : K_HELPER;
        for (int i = 0; i < g->n; i++)
            if (g->c[i].kind == kind) arrow(r, g->c[i].x, g->c[i].y, g->c[i].vx * 1.2f, g->c[i].vy * 1.2f, u, col(0xFFE45C, 0.9f));
    } else if (!strcmp(k, "RANGE")) {          /* how far a bubble flies */
        for (int i = 0; i < g->n; i++)
            if (g->c[i].kind == K_HELPER) {
                fill_circle(r, g->c[i].x, g->c[i].y, bubble_range(g), col(0xCDEFFF, 0.06f));
                ring(r, g->c[i].x, g->c[i].y, bubble_range(g), 0.7f * u, col(0xCDEFFF, 0.6f));
            }
    } else if (!strcmp(k, "RELOAD")) {         /* a ring that fills up until the next bubble */
        for (int i = 0; i < g->n; i++) {
            const Cell *q = &g->c[i];
            if (q->kind != K_HELPER) continue;
            float f = q->coolMax > 0 ? 1 - fmaxf(q->cool, 0) / q->coolMax : 1, rad = 8.5f * u;
            ring(r, q->x, q->y, rad, 1.2f * u, col(0xFFFFFF, 0.2f));
            int segs = (int)(f * 32);
            for (int s2 = 0; s2 < segs; s2++) {
                float a0 = -PI_F / 2 + s2 * 2 * PI_F / 32, a1 = a0 + 2 * PI_F / 32;
                thick_line(r, q->x + cosf(a0) * rad, q->y + sinf(a0) * rad, q->x + cosf(a1) * rad, q->y + sinf(a1) * rad,
                           1.4f * u, col(0xFFE45C, 0.95f));
            }
        }
    } else if (!strcmp(k, "SPLASH") || !strcmp(k, "POP")) {   /* each burst's reach stays up for a moment */
        for (int i = 0; i < MAXEV; i++) {
            const Event *e = &g->ev[i];
            float age = g->t - e->t;
            if (e->t <= 0 || age > 2.5f || e->kind == EV_BORN) continue;
            float a = 1 - age / 2.5f;
            fill_circle(r, e->x, e->y, e->rad, col(e->kind == EV_WASH ? 0x8FE3FF : 0xFFE45C, 0.12f * a));
            ring(r, e->x, e->y, e->rad, 0.8f * u, col(e->kind == EV_WASH ? 0x8FE3FF : 0xFFE45C, 0.9f * a));
        }
    } else if (!strcmp(k, "TRAIL")) {          /* the trail, brighter, and where each helper is heading on it */
        float life = trail_life();
        for (int i = 0; o_trail && i < MAXTRAIL; i++) {
            const Dot *d = &g->trail[i];
            float age = g->t - d->t;
            if (d->t > 0 && age <= life) fill_circle(r, d->x, d->y, 1.1f * u, col(0xB8F070, 0.5f * (1 - age / life)));
        }
        for (int i = 0; i < g->n; i++) {
            const Cell *q = &g->c[i];
            if (q->kind != K_HELPER || !q->onTrail) continue;
            thick_line(r, q->x, q->y, q->x + wrapd(q->sx - q->x, (float)g->w), q->y + wrapd(q->sy - q->y, (float)g->h),
                       0.6f * u, col(0xFFF6E0, 0.8f));
            ring(r, q->sx, q->sy, 2.2f * u, 0.6f * u, col(0xFFF6E0, 0.9f));
        }
    } else if (!strcmp(k, "GROWTH")) {         /* a flash where a cell splits in two */
        for (int i = 0; i < MAXEV; i++) {
            const Event *e = &g->ev[i];
            float age = g->t - e->t;
            if (e->t <= 0 || age > 1.5f || e->kind != EV_BORN) continue;
            ring(r, e->x, e->y, e->rad + age * 10 * u, 1.0f * u, col(0xFFE45C, 1 - age / 1.5f));
        }
    } else if (!strcmp(k, "WRAP")) {           /* edges: open and glowing, or walls */
        SDL_Color c = o_wrap ? col(0x8FE3FF, 0.35f + 0.25f * sinf(g->t * 4)) : col(0xE57373, 0.8f);
        float t = 1.2f * u;
        for (float x = 0; x < g->w; x += 8 * u) {
            float len = o_wrap ? 4 * u : 8 * u;
            thick_line(r, x, t / 2, x + len, t / 2, t, c);
            thick_line(r, x, g->h - t / 2, x + len, g->h - t / 2, t, c);
        }
        for (float y = 0; y < g->h; y += 8 * u) {
            float len = o_wrap ? 4 * u : 8 * u;
            thick_line(r, t / 2, y, t / 2, y + len, t, c);
            thick_line(r, g->w - t / 2, y, g->w - t / 2, y + len, t, c);
        }
    } else if (!strcmp(k, "CELLS") || !strcmp(k, "HELPERS") || !strcmp(k, "SEEDS")) {   /* rings round that kind */
        int kind = !strcmp(k, "CELLS") ? K_CELL : !strcmp(k, "HELPERS") ? K_HELPER : K_GERM;
        for (int i = 0; i < g->n; i++)
            if (g->c[i].kind == kind) ring(r, g->c[i].x, g->c[i].y, 8 * u, 0.8f * u, col(0xFFE45C, 0.9f));
    } else if (!strcmp(k, "MAX")) {            /* how full the slide is */
        char line[32];
        int sc = g->h / 120 > 2 ? g->h / 120 : 2;
        snprintf(line, sizeof line, "%d OF %d", g->n, slide_room(g));
        draw_text_shadow(r, line, (g->w - text_width(line, sc)) / 2, g->h / 2 - FONT_H * sc / 2, sc, hexcol(0xFFE45C));
    }
}

/* ---------- settings ---------- */

static int popping(void) { return o_pop; }   /* BUBBLE SPLASH only matters while bubbles pop */
#define NUM(k, lbl, hlp, v, d, l, h, st) .key = k, .label = lbl, .help = hlp, .value = &v, .def = d, .lo = l, .hi = h, .step = st
static const SaverOption GERM_OPTIONS[] = {
    { NUM("GERM_CELLS", "CELLS", "healthy cells the germs saver starts with", o_cells, 60, 0, 300, 10),
      .flags = OPT_RESTART | OPT_WHOLE, .tip = "HOW MANY BLUE CELLS THERE ARE AT THE START", .group = "AT THE START" },
    { NUM("GERM_HELPERS", "HELPERS", "helper cells (white blood cells) that blow soap bubbles in the germs saver",
          o_helpers, 2, 0, 100, 1), .flags = OPT_RESTART | OPT_WHOLE, .tip = "WHITE HELPERS BLOW BUBBLES AT GERMS" },
    { NUM("GERM_SEEDS", "GERMS", "germs at the start, and how many come back when none are left", o_seeds, 5, 0, 50, 1),
      .flags = OPT_RESTART | OPT_WHOLE, .tip = "GREEN GERMS TURN CELLS INTO GERMS" },
    { NUM("GERM_MAX", "MOST AT ONCE", "the most cells, helpers and germs on the slide at once; cells divide up to it",
          o_max, 70, 1, MAXP, 10), .tip = "THE MOST THAT FIT ON THE SLIDE", .flags = OPT_WHOLE },
    { NUM("GERM_SPEED", "GERM SPEED", "how fast germs chase, 1 to 10 (a dashing cell is about 5)", o_speed, 6, 1, 10, 1),
      .group = "HOW THEY MOVE", .tip = "ARROWS SHOW HOW FAST GERMS GO" },
    { NUM("GERM_HELPER_SPEED", "HELPER SPEED", "how fast helpers move, 1 to 10", o_helperSpeed, 5, 1, 10, 1), .tip = "ARROWS SHOW HOW FAST HELPERS GO" },
    { NUM("GERM_SIGHT", "HOW FAR THEY SEE", "how far cells, helpers and germs notice each other, 1 to 10", o_sight, 5, 1, 10, 1), .tip = "CIRCLES SHOW HOW FAR EACH ONE SEES" },
    { NUM("GERM_RELOAD", "BUBBLES HOW OFTEN", "how often a helper blows a bubble, 1 (slow) to 10 (fast)", o_reload, 5, 1, 10, 1),
      .group = "SOAP BUBBLES", .tip = "THE RING FILLS UP UNTIL THE NEXT BUBBLE" },
    { NUM("GERM_RANGE", "BUBBLE RANGE", "how far a soap bubble flies, 1 to 10", o_range, 4, 1, 10, 1), .tip = "THE RING SHOWS HOW FAR A BUBBLE FLIES" },
    { NUM("GERM_POP", "POP WITH A SPLASH", "1 = a bubble pops germs with a splash; 0 = it washes the one germ it hits back into a cell",
          o_pop, 1, 0, 1, 1), .flags = OPT_TOGGLE, .tip = "ON: BUBBLES POP GERMS. OFF: THEY WASH ONE CLEAN" },
    { NUM("GERM_SPLASH", "BUBBLE SPLASH", "how far a bursting bubble reaches, 0 (only the germ it hits) to 10",
          o_splash, 3, 0, 10, 1), .live = popping, .tip = "THE CIRCLE SHOWS WHAT EACH BUBBLE POPS" },
    { NUM("GERM_TRAIL", "GERM TRAILS", "how long germs' faint trails last for helpers to follow, 0 (none) to 10",
          o_trail, 5, 0, 10, 1), .group = "THE SLIDE", .tip = "HELPERS FOLLOW THE DOTS GERMS LEAVE" },
    { NUM("GERM_GROWTH", "CELLS GROW BACK", "how fast healthy cells divide to fill the slide back up, 0 (never) to 10",
          o_growth, 4, 0, 10, 1), .tip = "A FLASH SHOWS A CELL SPLITTING IN TWO" },
    { NUM("GERM_WRAP", "WRAP AROUND", "1 = germs and cells go off one edge and come back on the other, like Pac-Man",
          o_wrap, 1, 0, 1, 1), .flags = OPT_TOGGLE, .tip = "ON: OFF ONE EDGE, BACK ON THE OTHER" },
    { 0 },
};
#undef NUM

const Saver saver_germs = { "germs", "GERMS", create, step, draw, destroy, NULL, keep_clear, "I INFO", GERM_OPTIONS,
                            demo, explain };
