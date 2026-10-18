/* Lava lamp: a glowing glass lamp on a dark table. Wax warms in a pool at the bottom,
   buds off, rises, stretches, cools near the cap and sinks back (metaballs, simulated). */
#include "../engine.h"
#include <math.h>
#include <stdlib.h>

#define BALLS 10
#define ROWS 48          /* glass rows for the textured strip and overlays */
#define RING 5           /* rings in a soft glow ellipse */
#define SEG 36           /* segments in a soft glow ellipse */
#define THR 0.5625f      /* field value at a lone ball's edge: (1 - 1/4)^2 */

typedef struct {
    uint32_t roomTop, roomBottom, liqTop, liqBottom, waxEdge, waxCore, metal, glow;
} Palette;

static const Palette PALETTES[] = {
    /* classic orange on purple */
    { 0x07040C, 0x150A1C, 0x2A0A3E, 0x9C3A8C, 0xE8461C, 0xFFB43C, 0xC9A35E, 0xFF7A3A },
    /* pink on blue */
    { 0x03050E, 0x0A1226, 0x0A1748, 0x3A72D8, 0xF0388A, 0xFF8EC4, 0xB9C0CF, 0xC0609A },
    /* green on teal */
    { 0x020A0C, 0x07181C, 0x032E36, 0x139A92, 0x4CC232, 0xD2F25C, 0xB4C2C0, 0x6AD8A0 },
    /* yellow on red */
    { 0x0A0304, 0x1C0809, 0x3A0710, 0xC42234, 0xFFA01E, 0xFFD25A, 0xC9A35E, 0xFF5A3A },
};
#define NPAL (int)(sizeof PALETTES / sizeof PALETTES[0])

/* Lamp proportions in units of total lamp height L, measured up from the base. */
#define BASE_TOP 0.30f
#define GLASS_H 0.58f
#define CAP_BOT (BASE_TOP + GLASS_H)
#define GLASS_MAXW 0.165f
#define LIQ_TOP 0.965f   /* liquid level, as a fraction of glass height */

typedef struct { float x, y, vx, vy, r, T, heat; int free; } Ball;   /* glass units: y 0..1 */

typedef struct { float r, g, b; } Col;

typedef struct {
    int w, h, gw, gh;
    float t, L, cx, by;        /* lamp height and base-centre position in pixels */
    float bud;                 /* seconds until the pool lets go of another blob */
    Ball b[BALLS];
    Palette pal;
    float *fld, *tsum;
    uint32_t *px;
    SDL_Texture *tex;
} Lava;

/* ---------- small maths ---------- */

static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static float smooth(float a, float b, float v) {
    float t = clampf((v - a) / (b - a), 0, 1);
    return t * t * (3 - 2 * t);
}
static Col col(uint32_t hex) {
    Col c = { (float)(hex >> 16 & 255), (float)(hex >> 8 & 255), (float)(hex & 255) };
    return c;
}
static Col cmix(Col a, Col b, float t) {
    Col c = { a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t };
    return c;
}
static Col cscale(Col a, float k) { Col c = { a.r * k, a.g * k, a.b * k }; return c; }
static Col cadd(Col a, Col b) { Col c = { a.r + b.r, a.g + b.g, a.b + b.b }; return c; }
static Uint8 u8(float v) { return (Uint8)clampf(v, 0, 255); }
static SDL_Color sdl(Col c, float a) { SDL_Color s = { u8(c.r), u8(c.g), u8(c.b), u8(a) }; return s; }

/* Glass half-width (lamp units) at height v in 0..1 of the glass: Catmull-Rom through a
   classic profile, narrow where it meets the base, widest a third of the way up. */
static float glass_hw(float v) {
    static const float PV[] = { 0.0f, 0.10f, 0.30f, 0.55f, 0.80f, 1.0f };
    static const float PW[] = { 0.105f, 0.140f, GLASS_MAXW, 0.140f, 0.103f, 0.072f };
    enum { N = 6 };
    v = clampf(v, 0, 1);
    int k = 0;
    while (k < N - 2 && v > PV[k + 1]) k++;
    float h = PV[k + 1] - PV[k], t = (v - PV[k]) / h;
    float m0 = k > 0 ? (PW[k + 1] - PW[k - 1]) / (PV[k + 1] - PV[k - 1]) : (PW[1] - PW[0]) / h;
    float m1 = k + 2 < N ? (PW[k + 2] - PW[k]) / (PV[k + 2] - PV[k]) : (PW[k + 1] - PW[k]) / h;
    float t2 = t * t, t3 = t2 * t;
    return (2 * t3 - 3 * t2 + 1) * PW[k] + (t3 - 2 * t2 + t) * h * m0 +
           (-2 * t3 + 3 * t2) * PW[k + 1] + (t3 - t2) * h * m1;
}
/* The same in glass units (glass height = 1), which the wax simulation uses. */
static float hw_g(float y) { return glass_hw(y) / GLASS_H; }

/* ---------- wax simulation ---------- */

static void ball_reset(Ball *b) {
    b->heat = frand(0.35f, 1.4f);
}

static void sim_step(Lava *l, float dt) {
    /* The pool buds off one blob at a time: the hottest wax waiting in it. */
    l->bud -= dt;
    if (l->bud <= 0) {
        int best = -1, seen = 0;
        for (int i = 0; i < BALLS; i++)
            if (!l->b[i].free && l->b[i].T > 0.85f && irand(++seen) == 0) best = i;
        if (best >= 0) {
            l->b[best].free = 1;
            l->bud = frand(2.5f, 7.0f);
        }
    }
    for (int i = 0; i < BALLS; i++) {
        Ball *b = &l->b[i];
        /* Heat from the bulb near the bottom, lose it faster the higher the wax gets. */
        float zone = b->free ? 0 : 1;                             /* the bulb heats the pool */
        float size = 0.065f / b->r;                               /* small blobs cool fast */
        float cool = (0.012f + 0.06f * b->y * b->y) * size;
        if (b->y > LIQ_TOP - b->r * 1.2f) cool += 0.08f;          /* touching the top */
        b->T += (0.35f * b->heat * zone * (1 - b->T) - cool * b->T) * dt;
        float ay = 0.5f * (b->T - 0.55f);
        if (!b->free) ay = -0.3f;                                  /* waiting in the pool */
        float drag = 1.6f * size;                                 /* big blobs move faster */
        float ax = 0.02f * sinf(l->t * (0.21f + 0.03f * i) + i * 1.7f);
        b->vx += (ax - drag * b->vx) * dt;
        b->vy += (ay - drag * b->vy) * dt;
    }
    /* Neighbours stick together (and share heat) until stretched too far apart. */
    for (int i = 0; i < BALLS; i++)
        for (int j = i + 1; j < BALLS; j++) {
            Ball *a = &l->b[i], *c = &l->b[j];
            float dx = c->x - a->x, dy = c->y - a->y, d = sqrtf(dx * dx + dy * dy) + 1e-5f;
            float sum = a->r + c->r;
            if (d > 1.25f * sum || a->y < 0.12f || c->y < 0.12f) continue;   /* the pool merges them anyway */
            if (fabsf(a->T - c->T) > 0.2f) continue;              /* rising and sinking wax slide past */
            float f = 0.7f * (d - 0.9f * sum) / d * dt;
            a->vx += dx * f; a->vy += dy * f;
            c->vx -= dx * f; c->vy -= dy * f;
            float dT = 0.04f * (c->T - a->T) * dt;
            a->T += dT; c->T -= dT;
        }
    for (int i = 0; i < BALLS; i++) {
        Ball *b = &l->b[i];
        b->x += b->vx * dt;
        b->y += b->vy * dt;
        float lo = b->r * 0.3f, hi = LIQ_TOP - b->r * 0.95f;
        if (b->y < lo) {
            b->y = lo;
            if (b->vy < 0) b->vy = 0;
            if (b->free && b->T < 0.6f) { b->free = 0; ball_reset(b); }   /* back in the pool */
        }
        if (b->y > hi) { b->y = hi; if (b->vy > 0) b->vy = 0; }
        float lim = hw_g(b->y) - b->r * 0.85f;
        if (b->x > lim) { b->x = lim; if (b->vx > 0) b->vx = 0; }
        if (b->x < -lim) { b->x = -lim; if (b->vx < 0) b->vx = 0; }
    }
}

static void sim_run(Lava *l, float dt) {
    while (dt > 0) {
        float s = dt > 1.0f / 60 ? 1.0f / 60 : dt;
        l->t += s;
        sim_step(l, s);
        dt -= s;
    }
}

/* ---------- wax texture (low resolution, only the glass's bounding box) ---------- */

static void render_wax(Lava *l) {
    int gw = l->gw, gh = l->gh;
    float half = GLASS_MAXW / GLASS_H, sx = 2 * half / gw, sy = 1.0f / gh;
    const Palette *p = &l->pal;
    Col liqT = col(p->liqTop), liqB = col(p->liqBottom), edge = col(p->waxEdge), core = col(p->waxCore);

    /* The pool: a hot layer whose wobbling surface is where its field crosses THR. */
    float colf[160], rowf[300];
    for (int i = 0; i < gw; i++) {
        float x = -half + (i + 0.5f) * sx;
        float top = 0.08f + 0.010f * sinf(x * 17 + l->t * 0.6f) + 0.005f * sinf(x * 29 - l->t * 0.9f);
        colf[i] = THR * expf(top * 40);
    }
    for (int j = 0; j < gh; j++) rowf[j] = expf(-(1 - (j + 0.5f) * sy) * 40);
    for (int j = 0; j < gh; j++)
        for (int i = 0; i < gw; i++) {
            float f = colf[i] * rowf[j];
            l->fld[j * gw + i] = f;
            l->tsum[j * gw + i] = f * 0.95f;
        }

    for (int k = 0; k < BALLS; k++) {
        const Ball *b = &l->b[k];
        /* Moving wax stretches along its motion, keeping its area. */
        float st = 1 + clampf(fabsf(b->vy) * 3.5f, 0, 0.7f);
        float ex = 2 * b->r / sqrtf(st), ey = 2 * b->r * st;
        int i0 = (int)((b->x - ex + half) / sx), i1 = (int)((b->x + ex + half) / sx) + 1;
        int j0 = (int)((1 - b->y - ey) / sy), j1 = (int)((1 - b->y + ey) / sy) + 1;
        if (i0 < 0) i0 = 0;
        if (j0 < 0) j0 = 0;
        if (i1 > gw) i1 = gw;
        if (j1 > gh) j1 = gh;
        float iex = 1 / (ex * ex), iey = 1 / (ey * ey);
        for (int j = j0; j < j1; j++) {
            float dy = (1 - (j + 0.5f) * sy) - b->y, qy = dy * dy * iey;
            for (int i = i0; i < i1; i++) {
                float dx = -half + (i + 0.5f) * sx - b->x, q = dx * dx * iex + qy;
                if (q >= 1) continue;
                float f = (1 - q) * (1 - q);
                l->fld[j * gw + i] += f;
                l->tsum[j * gw + i] += f * b->T;
            }
        }
    }

    for (int j = 0; j < gh; j++) {
        float y = 1 - (j + 0.5f) * sy;
        float down = 1 - y;
        /* Liquid lit from the bulb below: bright at the bottom, dim towards the cap. */
        Col liq = cmix(liqT, liqB, down * down * down);
        liq = cadd(liq, cscale(col(p->glow), 0.25f * smooth(0.25f, 0.0f, y)));
        float light = 0.85f + 0.25f * down * down, edgeX = hw_g(y);
        for (int i = 0; i < gw; i++) {
            int n = j * gw + i;
            float f = l->fld[n];
            float T = f > 1e-4f ? l->tsum[n] / f : 0;
            Col out;
            if (y > LIQ_TOP) {                                  /* air gap under the cap */
                out = cscale(liqT, 0.55f);
                if (y < LIQ_TOP + sy * 1.5f) out = cmix(out, cscale(liqB, 1.2f), 0.6f);
            } else {
                float g = clampf(f / THR, 0, 1);
                Col halo = cmix(liq, edge, g * g * g * 0.3f);
                float above = j > 0 ? l->fld[n - gw] : f, below = j < gh - 1 ? l->fld[n + gw] : f;
                float inner = clampf((f - THR) / 2.5f, 0, 1);
                float cov = smooth(THR - 0.06f, THR + 0.05f, f);
                Col wax = cmix(cscale(edge, 0.8f), edge, smooth(THR, THR + 0.25f, f));
                wax = cmix(wax, core, 0.75f * sqrtf(inner));
                wax = cscale(wax, light * (0.8f + 0.3f * T));
                /* Lit from the bulb below: undersides glow, tops fall into shade. */
                float ny = clampf((above - below) * gh / 16, -1, 1) * (1 - smooth(THR, THR + 1.2f, f));
                if (ny > 0) wax = cmix(wax, core, ny * 0.6f);
                else wax = cscale(wax, 1 + ny * 0.3f);
                out = cmix(halo, wax, cov);
            }
            float a = clampf((edgeX - fabsf(-half + (i + 0.5f) * sx)) / sx + 0.5f, 0, 1) * 255;
            l->px[n] = (uint32_t)u8(out.r) << 24 | (uint32_t)u8(out.g) << 16 | (uint32_t)u8(out.b) << 8 | u8(a);
        }
    }
    SDL_UpdateTexture(l->tex, NULL, l->px, gw * 4);
}

/* ---------- geometry helpers ---------- */

static float SX(const Lava *l, float x) { return l->cx + x * l->L; }
static float SY(const Lava *l, float y) { return l->by - y * l->L; }

/* Soft elliptical glow: alpha falls off smoothly from the centre. */
static void glow_ellipse(SDL_Renderer *r, float cx, float cy, float rx, float ry, Col c, float a) {
    static const float RF[RING] = { 0.18f, 0.38f, 0.6f, 0.8f, 1.0f };
    static const float AF[RING] = { 0.82f, 0.52f, 0.24f, 0.08f, 0.0f };
    SDL_Vertex v[1 + RING * SEG];
    int idx[SEG * 3 + (RING - 1) * SEG * 6], n = 0;
    v[0] = (SDL_Vertex){ { cx, cy }, sdl(c, a), { 0, 0 } };
    for (int k = 0; k < RING; k++)
        for (int s = 0; s < SEG; s++) {
            float an = s * 2 * PI_F / SEG;
            v[1 + k * SEG + s] = (SDL_Vertex){ { cx + cosf(an) * rx * RF[k], cy + sinf(an) * ry * RF[k] },
                                               sdl(c, a * AF[k]), { 0, 0 } };
        }
    for (int s = 0; s < SEG; s++) {
        idx[n++] = 0; idx[n++] = 1 + s; idx[n++] = 1 + (s + 1) % SEG;
    }
    for (int k = 0; k + 1 < RING; k++)
        for (int s = 0; s < SEG; s++) {
            int a0 = 1 + k * SEG + s, a1 = 1 + k * SEG + (s + 1) % SEG;
            int b0 = a0 + SEG, b1 = a1 + SEG;
            idx[n++] = a0; idx[n++] = b0; idx[n++] = b1;
            idx[n++] = a0; idx[n++] = b1; idx[n++] = a1;
        }
    SDL_RenderGeometry(r, NULL, v, 1 + RING * SEG, idx, n);
}

/* Metal shading across a turned part, lit from the front left. */
#define MCOLS 9
static const float MF[MCOLS] = { -1, -0.9f, -0.66f, -0.44f, -0.2f, 0.15f, 0.5f, 0.8f, 1 };
static const float MB[MCOLS] = { 0.22f, 0.38f, 0.78f, 1.12f, 0.72f, 0.46f, 0.30f, 0.42f, 0.18f };

/* A turned metal part given as rows of (height, half-width) in lamp units. `tint` is how
   much of the lamp's glow each row picks up. */
static void metal_part(SDL_Renderer *r, const Lava *l, const float (*rows)[2], const float *tint,
                       const float *shade, int nrows) {
    SDL_Vertex v[12 * MCOLS];
    int idx[11 * (MCOLS - 1) * 6], n = 0;
    Col m = col(l->pal.metal), g = col(l->pal.glow);
    for (int k = 0; k < nrows; k++)
        for (int c = 0; c < MCOLS; c++) {
            float b = MB[c] * shade[k];
            Col o = cscale(m, b);
            if (b > 0.95f) o = cadd(o, cscale(col(0xFFFFFF), (b - 0.95f) * 2.2f));
            o = cadd(o, cscale(g, tint[k] * (0.35f + 0.65f * MB[c])));
            v[k * MCOLS + c] = (SDL_Vertex){ { SX(l, MF[c] * rows[k][1]), SY(l, rows[k][0]) }, sdl(o, 255), { 0, 0 } };
        }
    for (int k = 0; k + 1 < nrows; k++)
        for (int c = 0; c + 1 < MCOLS; c++) {
            int a = k * MCOLS + c, b = a + MCOLS;
            idx[n++] = a; idx[n++] = a + 1; idx[n++] = b + 1;
            idx[n++] = a; idx[n++] = b + 1; idx[n++] = b;
        }
    SDL_RenderGeometry(r, NULL, v, nrows * MCOLS, idx, n);
}

/* A band across the glass between fractions f0..f1 of its half-width, with alpha a0..a1,
   faded towards the top and bottom of the glass when `fade` is set. */
static void glass_band(SDL_Renderer *r, const Lava *l, float f0, float f1, float a0, float a1,
                       Col c, int fade, float grow) {
    SDL_Vertex v[(ROWS + 1) * 2];
    int idx[ROWS * 6], n = 0;
    for (int k = 0; k <= ROWS; k++) {
        float t = (float)k / ROWS, hw = glass_hw(t) + grow, y = BASE_TOP + t * GLASS_H;
        float fa = fade ? powf(sinf(PI_F * clampf((t - 0.03f) / 0.9f, 0, 1)), 0.6f) : 1;
        v[k * 2] = (SDL_Vertex){ { SX(l, f0 * hw), SY(l, y) }, sdl(c, a0 * fa), { 0, 0 } };
        v[k * 2 + 1] = (SDL_Vertex){ { SX(l, f1 * hw), SY(l, y) }, sdl(c, a1 * fa), { 0, 0 } };
    }
    for (int k = 0; k < ROWS; k++) {
        int a = k * 2;
        idx[n++] = a; idx[n++] = a + 1; idx[n++] = a + 3;
        idx[n++] = a; idx[n++] = a + 3; idx[n++] = a + 2;
    }
    SDL_RenderGeometry(r, NULL, v, (ROWS + 1) * 2, idx, n);
}

/* The wax texture (clear outside the glass) stretched over the glass, then shaded
   darker towards the sides so it reads as a round vessel. */
static void glass_body(SDL_Renderer *r, const Lava *l) {
    SDL_FRect dst = { SX(l, -GLASS_MAXW), SY(l, CAP_BOT), 2 * GLASS_MAXW * l->L, GLASS_H * l->L };
    SDL_RenderCopyF(r, l->tex, NULL, &dst);
    Col black = col(0);
    glass_band(r, l, -1, -0.82f, 140, 50, black, 0, 0);
    glass_band(r, l, -0.82f, -0.4f, 50, 0, black, 0, 0);
    glass_band(r, l, 0.4f, 0.82f, 0, 60, black, 0, 0);
    glass_band(r, l, 0.82f, 1, 60, 150, black, 0, 0);
}

/* ---------- saver ---------- */

static void *create(SDL_Renderer *r, int w, int h) {
    Lava *l = calloc(1, sizeof *l);
    l->w = w;
    l->h = h;
    l->pal = PALETTES[irand(NPAL)];
    l->by = h * 0.94f;
    l->L = h * 0.87f;
    if (l->L * 0.42f > w * 0.9f) l->L = w * 0.9f / 0.42f;
    l->cx = w * 0.5f;

    float glassPx = 2 * GLASS_MAXW * l->L;
    l->gw = (int)clampf(glassPx / 3.5f, 28, 120);
    l->gh = (int)(l->gw * GLASS_H / (2 * GLASS_MAXW) + 0.5f);
    l->fld = malloc(sizeof(float) * l->gw * l->gh);
    l->tsum = malloc(sizeof(float) * l->gw * l->gh);
    l->px = malloc(sizeof(uint32_t) * l->gw * l->gh);
    l->tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, l->gw, l->gh);
    SDL_SetTextureScaleMode(l->tex, SDL_ScaleModeLinear);
    SDL_SetTextureBlendMode(l->tex, SDL_BLENDMODE_BLEND);

    for (int i = 0; i < BALLS; i++) {
        Ball *b = &l->b[i];
        b->r = frand(0.05f, 0.088f);
        b->y = frand(0.0f, 1.0f);
        b->free = 1;
        b->x = frand(-0.6f, 0.6f) * (hw_g(b->y) - b->r);
        b->T = frand(0.2f, 0.9f);
        ball_reset(b);
    }
    l->t = frand(0, 100);
    sim_run(l, 60);    /* start mid-flow, not with a cold lamp */
    return l;
}

static void step(void *p, float dt) {
    if (dt > 0.1f) dt = 0.1f;
    sim_run(p, dt);
}

static void draw(void *p, SDL_Renderer *r) {
    Lava *l = p;
    const Palette *pal = &l->pal;
    Col glow = col(pal->glow), roomB = col(pal->roomBottom);
    float L = l->L, breathe = 0.92f + 0.08f * sinf(l->t * 0.37f);

    /* Room and table. */
    set_color(r, hexcol(pal->roomTop));
    SDL_RenderFillRect(r, NULL);
    vgradient(r, l->w, l->h, hexcol(pal->roomTop), hexcol(pal->roomBottom));
    float tableY = SY(l, 0.05f);
    SDL_Vertex tv[4] = {
        { { 0, tableY }, sdl(cadd(cscale(roomB, 1.5f), cscale(glow, 0.06f)), 255), { 0, 0 } },
        { { (float)l->w, tableY }, sdl(cadd(cscale(roomB, 1.5f), cscale(glow, 0.06f)), 255), { 0, 0 } },
        { { (float)l->w, (float)l->h }, sdl(cscale(roomB, 0.7f), 255), { 0, 0 } },
        { { 0, (float)l->h }, sdl(cscale(roomB, 0.7f), 255), { 0, 0 } },
    };
    int qi[6] = { 0, 1, 2, 0, 2, 3 };
    SDL_RenderGeometry(r, NULL, tv, 4, qi, 6);

    /* Light the lamp throws onto the wall and the table. */
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_ADD);
    float gy = SY(l, BASE_TOP + GLASS_H * 0.4f);
    glow_ellipse(r, l->cx, gy, L * 0.85f, L * 0.75f, glow, 70 * breathe);
    glow_ellipse(r, l->cx, gy, L * 0.36f, L * 0.42f, glow, 60 * breathe);
    glow_ellipse(r, l->cx, l->by, L * 0.75f, L * 0.09f, glow, 80 * breathe);
    thick_line(r, 0, tableY, (float)l->w, tableY, fmaxf(1, L * 0.003f), sdl(cscale(glow, 0.5f), 60));
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    glow_ellipse(r, l->cx, l->by, L * 0.27f, L * 0.03f, col(0), 200);

    /* Base: a lip, a long cone and a collar the glass sits in. */
    static const float BR[][2] = { { 0, 0.198f }, { 0.016f, 0.2f }, { 0.024f, 0.19f }, { 0.07f, 0.1754f },
                                   { 0.12f, 0.1596f }, { 0.17f, 0.1437f }, { 0.22f, 0.1278f },
                                   { 0.27f, 0.112f }, { 0.274f, 0.12f }, { BASE_TOP, 0.12f } };
    static const float BT[] = { 0, 0, 0.02f, 0.03f, 0.05f, 0.09f, 0.16f, 0.3f, 0.45f, 0.55f };
    static const float BS[] = { 0.55f, 0.8f, 0.9f, 0.95f, 1, 1.03f, 1.05f, 1.05f, 0.95f, 0.85f };
    metal_part(r, l, BR, BT, BS, 10);

    /* Glass: dark outline, wax inside, then rim light and a specular streak on top. */
    render_wax(l);
    float px = fmaxf(1.0f, L * 0.004f) / L;
    glass_band(r, l, -1, 1, 255, 255, cscale(col(pal->liqTop), 0.5f), 0, px);
    glass_body(r, l);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_ADD);
    Col white = col(0xFFFFFF), rim = cmix(glow, white, 0.4f);
    glass_band(r, l, -1, -0.84f, 120, 0, rim, 1, 0);
    glass_band(r, l, 0.84f, 1, 0, 150, rim, 1, 0);
    glass_band(r, l, -0.68f, -0.56f, 0, 110, white, 1, 0);
    glass_band(r, l, -0.56f, -0.44f, 110, 0, white, 1, 0);
    glass_band(r, l, 0.56f, 0.66f, 0, 35, white, 1, 0);
    glass_band(r, l, 0.66f, 0.74f, 35, 0, white, 1, 0);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

    /* Cap. */
    static const float CR[][2] = { { CAP_BOT, 0.08f }, { CAP_BOT + 0.012f, 0.08f }, { 0.931f, 0.065f },
                                   { 0.97f, 0.05f }, { 0.985f, 0.045f }, { 0.995f, 0.034f }, { 1.0f, 0.018f } };
    static const float CT[] = { 0.5f, 0.3f, 0.12f, 0.06f, 0.02f, 0, 0 };
    static const float CS[] = { 0.8f, 1.0f, 1.0f, 1.0f, 1.05f, 1.1f, 1.0f };
    metal_part(r, l, CR, CT, CS, 7);
}

static void destroy(void *p) {
    Lava *l = p;
    SDL_DestroyTexture(l->tex);
    free(l->fld);
    free(l->tsum);
    free(l->px);
    free(l);
}

const Saver saver_lava = { "lava", "LAVA", create, step, draw, destroy };
