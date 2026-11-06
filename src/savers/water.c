/* Water: the water cycle in a cutaway landscape, from sunrise to a starry night and round again.
   The sea is on the left, green hills rise to a lake, and snowy mountains stand on the right, all
   cut away at the front like a diorama to show the soil, the groundwater and the sea floor. The
   sun warms the sea and the lake and wavy wisps of vapor rise off them, more when the sun is high.
   The vapor gathers into clouds that grow, darken and drift inland on the breeze. Heavy clouds
   rain on the hills and snow on the mountains, where the snowcap grows and then melts back in the
   sunshine. Rain and meltwater run down a stream into the lake, the lake spills into a river that
   winds down to the sea, and some soaks into the ground and seeps back to the sea underneath.
   Short labels (EVAPORATION, CONDENSATION, PRECIPITATION, COLLECTION) fade in beside each stage
   while it is happening, and a ring in the corner lights up the same stages around the cycle.
   A boat bobs on the sea, a fish jumps in the lake, flowers open after rain and a rainbow comes
   out when a shower ends in sunshine. A day lasts three and a half minutes, and dawn is the place
   to cut away. Windowed: B brings a big rainstorm now, F fast-forwards to the next rain. */
#include "../engine.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DAY_SECONDS 210.0f     /* sunrise to sunrise */
#define SUNSET 0.66f           /* the part of the day the sun is up */
#define START_PHASE 0.09f      /* it opens on a fresh morning */
#define MAXCLOUD 8
#define NPUFF 7
#define MAXPART 1800
#define MAXFLOWER 16
#define MAXSTAR 90
#define DROPS 18.0f            /* raindrops per unit of cloud water (a third as many snowflakes) */
#define WISP 0.5f            /* cloud water carried up by one wisp of vapor */
#define LAKE_GAIN 0.07f        /* how far one unit of water raises the lake, in pixels at 240 high */
#define MAXRECT 4096

enum { P_WISP, P_RAIN, P_SNOW, P_SPLASH, P_SEEP };
enum { W_SEA, W_LAKE, W_HILL, W_FOOT, W_MTN };     /* where a wisp starts or a drop lands */
enum { L_EVAP, L_COND, L_PRECIP, L_COLL, NLABEL };

/* Kid-facing words, in one place for translating later. Each has a colour and an arrow
   (up, right, down, left) that match its quarter of the ring. */
static const char *const WORDS[NLABEL] = { "EVAPORATION", "CONDENSATION", "PRECIPITATION", "COLLECTION" };
static const uint32_t STAGE_COL[NLABEL] = { 0xFFC857, 0xEEF4FF, 0x8AD0FF, 0x6BE8C0 };

typedef struct { float x, y, vx, vy, life, ph, land; int kind, where; } Part;

typedef struct {
    int on, raining, storm, dying;
    float x, base, water, r, gain, got, fade, rainAcc, age, dark;
    float px[NPUFF], py[NPUFF], pr[NPUFF];
} Cloud;

typedef struct { float x, y, g; int col; } Flower;
typedef struct { float x, y, ph; int big; } Star;

typedef struct {
    int w, h, u, lw, lh, D, sc;
    float k;                                  /* lh / 240: pixel sizes and speeds scale with it */
    int coastX, beachX, outletX, lakeR, peakX, streamX;
    float seaY, rimY, bedY, condY, peakY;
    float *F;                                 /* per column: the front edge of the ground (or the sea and lake bed) */
    float *M;                                 /* per column: the top of the mountains, or a big number */
    float *snow, *wt, *br;                    /* snow depth; the water table and the bedrock under each column */
    float t, dayT, phase, sunAlt, elev, light[3], overcast, wind, humid, snowLineY;
    int dayN;
    float lake, riverQ, streamQ, groundQ, moist, flowPh, streamPh;
    float seaAcc, lakeAcc, seepAcc;
    float collectRate, rainTotal, snowTotal;
    Cloud c[MAXCLOUD];
    Part p[MAXPART];
    int np;
    Flower fl[MAXFLOWER];
    int nfl;
    Star st[MAXSTAR];
    float boatX, boatDir, fishT, fishAge, fishX, fishDir, rainbow, rainbowX;
    float act[NLABEL], alpha[NLABEL], hold[NLABEL], lx[NLABEL], ly[NLABEL];
    int labW[NLABEL], labOK[NLABEL];
    SDL_Rect lrect[NLABEL], ring, info;
    int showLabels;
    SDL_Texture *terrain, *lush, *word[NLABEL], *boat, *fish;
    int wordW[NLABEL];
    SDL_Rect rects[MAXRECT];
    int nr;
} Water;

/* ---------- small helpers ---------- */

static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static float smooth(float a, float b, float x) {
    float t = clampf((x - a) / (b - a), 0, 1);
    return t * t * (3 - 2 * t);
}
static int clampi(int v, int a, int b) { return v < a ? a : v > b ? b : v; }

static uint32_t mixc(uint32_t a, uint32_t b, float t) {
    t = clampf(t, 0, 1);
    uint32_t out = 0;
    for (int sh = 16; sh >= 0; sh -= 8) {
        float ca = (float)((a >> sh) & 255), cb = (float)((b >> sh) & 255);
        out |= (uint32_t)(ca + (cb - ca) * t + 0.5f) << sh;
    }
    return out;
}

static uint32_t hash2(int x, int y) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
static float rnd2(int x, int y) { return (hash2(x, y) & 0xFFFF) / 65536.0f; }

static float vnoise(float x, int seed) {
    int i = (int)floorf(x);
    float f = x - i;
    f = f * f * (3 - 2 * f);
    return rnd2(i, seed) * (1 - f) + rnd2(i + 1, seed) * f;
}

static const float BAYER[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };

/* Pixel-art gradient: a mix of a and b in four steps, ordered-dithered between them. */
static uint32_t dith(uint32_t a, uint32_t b, float t, int x, int y) {
    float q = floorf(clampf(t, 0, 1) * 4 + (BAYER[y & 3][x & 3] + 0.5f) / 16) / 4;
    return rgb(mixc(a, b, q));
}

/* A colour lit by the time of day. */
static SDL_Color lc(const Water *s, uint32_t hex, float a) {
    SDL_Color c = { (Uint8)(((hex >> 16) & 255) * s->light[0]), (Uint8)(((hex >> 8) & 255) * s->light[1]),
                    (Uint8)((hex & 255) * s->light[2]), (Uint8)(clampf(a, 0, 1) * 255) };
    return c;
}

/* Rectangles in landscape pixels, batched into one draw per colour. */
static void add(Water *s, int x, int y, int w, int h) {
    if (s->nr >= MAXRECT || w <= 0 || h <= 0) return;
    s->rects[s->nr++] = (SDL_Rect){ x * s->u, y * s->u, w * s->u, h * s->u };
}
static void flush(Water *s, SDL_Renderer *r, SDL_Color c) {
    if (s->nr) {
        set_color(r, c);
        SDL_RenderFillRects(r, s->rects, s->nr);
    }
    s->nr = 0;
}

/* A round blob of landscape pixels. */
static void disc(Water *s, float cx, float cy, float rad) {
    for (int y = (int)floorf(cy - rad); y <= (int)ceilf(cy + rad); y++) {
        float dy = y + 0.5f - cy;
        if (fabsf(dy) >= rad) continue;
        float half = sqrtf(rad * rad - dy * dy);
        int x0 = (int)floorf(cx - half + 0.5f), x1 = (int)floorf(cx + half + 0.5f);
        add(s, x0, y, x1 - x0, 1);
    }
}

static int col_at(const Water *s, float x) { return clampi((int)floorf(x), 0, s->lw - 1); }

/* The river winds across the strip of ground between the hills' far edge and the cut. */
static float river_y(const Water *s, int x) {
    float D = (float)s->D;
    return s->F[x] - D * (0.5f + 0.28f * sinf(x * 0.19f / s->k + 1.3f));
}

/* ---------- the landscape (drawn once) ---------- */

static float lerp_pts(const float (*pt)[2], int n, float xf) {
    if (xf <= pt[0][0]) return pt[0][1];
    for (int i = 1; i < n; i++)
        if (xf <= pt[i][0]) {
            float t = (xf - pt[i - 1][0]) / (pt[i][0] - pt[i - 1][0]);
            return pt[i - 1][1] + (pt[i][1] - pt[i - 1][1]) * t;
        }
    return pt[n - 1][1];
}

static void blur(float *a, int n, int rad) {
    float *t = malloc(sizeof(float) * (size_t)n);
    memcpy(t, a, sizeof(float) * (size_t)n);
    for (int i = 0; i < n; i++) {
        float sum = 0;
        for (int j = -rad; j <= rad; j++) sum += t[clampi(i + j, 0, n - 1)];
        a[i] = sum / (2 * rad + 1);
    }
    free(t);
}

static void tree(Canvas *c, int x, int base, float rr, int pine) {
    if (pine) {
        int hgt = (int)(rr * 2.6f);
        for (int i = 0; i < hgt; i++) {
            int half = (int)((i + 1) * rr / hgt * 0.95f);
            for (int dx = -half; dx <= half; dx++)
                put(c, x + dx, base - hgt + i, rgb(dx < 0 ? 0x3C6E3E : 0x2C5530));
        }
        put(c, x, base, rgb(0x5A3D26));
        return;
    }
    put(c, x, base, rgb(0x6B4A2F));
    put(c, x, base - 1, rgb(0x6B4A2F));
    fill_ellipse(c, x + 0.5f, base - 1 - rr, rr, rr, rgb(0x3D7A3A));
    fill_ellipse(c, x - rr * 0.25f + 0.5f, base - 1 - rr * 1.35f, rr * 0.5f, rr * 0.45f, rgb(0x5E9E4C));
}

static void build_terrain(Water *s, SDL_Renderer *r) {
    int lw = s->lw, lh = s->lh, D = s->D;
    float k = s->k;
    /* the front edge of the ground, sea floor to mountains (x and y as parts of the screen) */
    static const float FP[][2] = {
        { 0.00f, 0.95f }, { 0.08f, 0.93f }, { 0.17f, 0.86f }, { 0.235f, 0.74f }, { 0.275f, 0.668f },
        { 0.31f, 0.656f }, { 0.37f, 0.641f }, { 0.43f, 0.621f }, { 0.49f, 0.597f }, { 0.545f, 0.574f },
        { 0.575f, 0.567f }, { 0.595f, 0.60f }, { 0.62f, 0.632f }, { 0.66f, 0.634f }, { 0.685f, 0.61f },
        { 0.705f, 0.578f }, { 0.725f, 0.556f }, { 0.76f, 0.549f }, { 0.85f, 0.545f }, { 1.0f, 0.54f } };
    s->seaY = floorf(0.668f * lh);
    for (int x = 0; x < lw; x++) s->F[x] = lerp_pts(FP, (int)(sizeof FP / sizeof FP[0]), (x + 0.5f) / lw) * lh;
    blur(s->F, lw, lw / 110 > 1 ? lw / 110 : 1);
    blur(s->F, lw, lw / 110 > 1 ? lw / 110 : 1);
    s->coastX = 0;
    while (s->coastX < lw - 1 && s->F[s->coastX] > s->seaY) s->coastX++;
    s->beachX = s->coastX + (int)(0.035f * lw);
    s->outletX = (int)(0.53f * lw);
    for (int x = (int)(0.53f * lw); x <= (int)(0.61f * lw); x++)
        if (s->F[x] < s->F[s->outletX]) s->outletX = x;
    s->rimY = s->F[s->outletX];
    s->lakeR = s->outletX + 1;
    while (s->lakeR < lw - 1 && s->F[s->lakeR] > s->rimY - 0.5f) s->lakeR++;
    s->bedY = s->rimY;
    for (int x = s->outletX; x < s->lakeR; x++) s->bedY = fmaxf(s->bedY, s->F[x]);

    /* mountains: a near range with the big peak and a hazy one behind */
    float *M1 = calloc((size_t)lw, sizeof(float)), *M2 = calloc((size_t)lw, sizeof(float));
    float *H = calloc((size_t)lw, sizeof(float));
    float aspect = (float)lw / lh;
    s->peakX = (int)(0.815f * lw);
    for (int x = 0; x < lw; x++) {
        float xf = (x + 0.5f) / lw;
        float m = 0.165f + (xf < 0.815f ? 1.95f : 1.25f) * fabsf(xf - 0.815f) * aspect / 1.78f;
        m = fminf(m, 0.318f + 1.6f * fabsf(xf - 0.73f) * aspect / 1.78f);
        m += (vnoise(x * 0.16f / k, 1) - 0.5f) * 0.03f + (vnoise(x * 0.55f / k, 2) - 0.5f) * 0.012f;
        M1[x] = m * lh;
        float m2 = 0.215f + 1.4f * fabsf(xf - 0.94f) * aspect / 1.78f;
        m2 = fminf(m2, 0.37f + 1.3f * fabsf(xf - 0.655f) * aspect / 1.78f);
        m2 += (vnoise(x * 0.2f / k, 3) - 0.5f) * 0.025f;
        M2[x] = m2 * lh;
        /* rolling hills behind the strip of meadow, bridging the lake */
        float land = (x > s->outletX && x < s->lakeR) ? s->rimY : s->F[x];
        float env = smooth(0.24f, 0.34f, xf) * (1 - smooth(0.76f, 0.84f, xf));
        float bump = 0.035f + 0.025f * sinf(xf * 23 + 0.5f) + 0.014f * sinf(xf * 51 + 2.0f);
        H[x] = env > 0.01f ? land - D - bump * env * lh : 1e6f;
    }
    s->peakY = M1[s->peakX];
    for (int x = 0; x < lw; x++) s->M[x] = fminf(M1[x], M2[x]) < s->F[x] - D ? fminf(M1[x], M2[x]) : 1e6f;

    /* the water table and the bedrock under each column */
    for (int x = 0; x < lw; x++) {
        if (x < s->coastX) s->wt[x] = s->F[x] + 3 * k;
        else if (x <= s->outletX) {
            float t = (float)(x - s->coastX) / (s->outletX - s->coastX);
            s->wt[x] = fmaxf(s->F[x] + 9 * k, s->seaY + 5 * k + (s->rimY + 11 * k - s->seaY - 5 * k) * t);
        } else if (x < s->lakeR) s->wt[x] = fmaxf(s->F[x] + 5 * k, s->rimY + 11 * k);
        else s->wt[x] = s->F[x] + 13 * k;
        s->br[x] = fmaxf(s->wt[x] + 7 * k, 0.83f * lh + sinf(x * 0.05f / k) * 0.012f * lh + (vnoise(x * 0.1f / k, 5) - 0.5f) * 4 * k);
    }

    Canvas T = canvas_new(lw, lh), L = canvas_new(lw, lh);
    for (int x = 0; x < lw; x++) {
        int sea = x < s->coastX, lakeCol = x > s->outletX && x < s->lakeR && s->F[x] > s->rimY + 1;
        int beach = !sea && x < s->beachX;
        float nearTop = sea ? s->seaY : s->F[x], far = nearTop - D;
        for (int y = 0; y < lh; y++) {
            uint32_t c = 0, l = 0;
            float fy = (float)y;
            int lit = x < s->peakX + (fy - s->peakY) * 0.45f + (vnoise(fy * 0.35f / k, 7) - 0.5f) * 5 * k;
            if (fy >= nearTop) {                                     /* the cutaway */
                float d = fy - nearTop;
                if (sea && fy < s->F[x]) {
                    float t = (fy - s->seaY) / (0.97f * lh - s->seaY);
                    c = dith(0x3E93D4, 0x174A88, t, x, y);
                } else if (sea && fy < s->F[x] + 3 * k) {
                    c = rnd2(x, y) < 0.15f ? rgb(0xB89E68) : rgb(0xD8C28A);
                } else if (!sea && d < 1) {
                    c = rgb(beach ? 0xC9B07A : lakeCol ? 0x5E4C38 : 0x4C7A2C);
                } else if (!sea && d < 5 * k) {
                    c = beach ? rgb(0xD5BE86) : rnd2(x, y) < 0.18f ? rgb(0x4A2E1A) : rgb(0x5E3D24);
                } else if (fy < s->wt[x]) {
                    float pr = rnd2(x, y);
                    c = pr < 0.03f ? rgb(0xA88A6A) : dith(0x8E6A45, 0x7B5838, (fy - nearTop) / (s->wt[x] - nearTop + 1), x, y);
                } else if (fy < s->wt[x] + 1) {
                    c = (x % 3) ? rgb(0x7FB8EA) : rgb(0x6E6A72);   /* the water table */
                } else if (fy < s->br[x]) {
                    float pr = rnd2(x, y);
                    c = pr < 0.05f ? rgb(0x6E92BE) : pr < 0.10f ? rgb(0x837A70) : dith(0x5F7392, 0x55657F, (fy - s->wt[x]) / (s->br[x] - s->wt[x] + 1), x, y);
                } else {
                    int band = (int)((fy + sinf(x * 0.07f / k) * 3 * k) / (6 * k));
                    c = rgb(band & 1 ? 0x514C5E : 0x5A5568);
                    if (rnd2(x, y) < 0.04f) c = rgb(0x6A6578);
                }
            } else if (fy >= far) {                                  /* the strip of ground seen from above */
                float t = (fy - far) / D;
                if (sea) c = rnd2(x / 3, y) < 0.12f ? rgb(0x9BD4F2) : dith(0x6CB8E6, 0x4A9AD6, t, x, y);
                else if (beach) c = dith(0xF0DDA6, 0xDCC48C, t, x, y);
                else if (lakeCol) c = dith(0x7A6A50, 0x66563F, t, x, y);
                else {
                    c = dith(0xB8C266, 0x94A64C, t, x, y);
                    l = dith(0x7ED257, 0x56AC3E, t, x, y);
                }
            } else if (fy >= H[x]) {                                 /* rolling hills */
                float t = (fy - H[x]) / (far - H[x] + 1);
                c = y < H[x] + 1 ? rgb(0xC6CF84) : dith(0xAAB873, 0x98A962, t, x, y);
                l = y < H[x] + 1 ? rgb(0x9EE07A) : dith(0x78C76A, 0x5FB056, t, x, y);
            } else if (fy >= M1[x]) {                                /* the near mountains */
                float cap = fmaxf(0, 0.275f * lh - M1[x]) * 0.55f + (vnoise(x * 0.4f, 9) - 0.5f) * 3 * k;
                float forest = 0.44f * lh + (vnoise(x * 0.12f / k, 11) - 0.5f) * 0.05f * lh;
                if (fy < M1[x] + cap) c = rgb(lit ? 0xF2F6FF : 0xC9D4E6);
                else if (fy > forest) c = dith(lit ? 0x58834A : 0x46703E, lit ? 0x4A7640 : 0x3A6236, (fy - forest) / (0.1f * lh), x, y);
                else c = dith(lit ? 0x9A90A4 : 0x6E6480, lit ? 0x857B92 : 0x5F566F, (fy - M1[x]) / (0.25f * lh), x, y);
            } else if (fy >= M2[x]) {                                /* the hazy range behind */
                int lit2 = x < (int)(0.94f * lw);
                float cap = fmaxf(0, 0.3f * lh - M2[x]) * 0.6f + (vnoise(x * 0.4f, 13) - 0.5f) * 3 * k;
                c = fy < M2[x] + cap ? rgb(lit2 ? 0xEEF3FF : 0xD3DCEE) : rgb(lit2 ? 0xA9B2CE : 0x929BBA);
            }
            if (c) T.px[y * lw + x] = c;
            if (l) L.px[y * lw + x] = l;
        }
    }
    /* trees on the hills, pines on the lower mountains, seaweed and a starfish on the sea floor */
    for (int i = 0; i < lw / (int)(9 * k + 1); i++) {
        int x = (int)(frand(0.30f, 0.80f) * lw);
        if (H[x] > 1e5f || (x > s->outletX - 3 && x < s->lakeR + 3 && irand(2))) continue;
        int base = (int)(H[x] + frand(1, (s->F[x] - D - H[x]) * 0.7f + 1));
        if (x > s->outletX && x < s->lakeR) base = (int)(H[x] + frand(1, 3 * k));
        if (base >= (int)(s->F[x] - D) && !(x > s->outletX && x < s->lakeR)) continue;
        float rr = frand(1.6f, 2.8f) * k;
        tree(&T, x, base, rr, 0);
        tree(&L, x, base, rr, 0);
    }
    for (int i = 0; i < lw / (int)(5 * k + 1); i++) {
        int x = (int)(frand(0.64f, 1.0f) * lw);
        x = clampi(x, 0, lw - 1);
        if (M1[x] > s->F[x] - D) continue;
        float forest = 0.46f * lh;
        float y = frand(fmaxf(forest, M1[x] + 4 * k), s->F[x] - D);
        if (y >= s->F[x] - D - 1 || y < H[x] - 1e5f) continue;
        if (y > H[x]) continue;
        tree(&T, x, (int)y, frand(1.4f, 2.4f) * k, 1);
    }
    for (int i = 0; i < 9; i++) {
        int x = (int)(frand(0.02f, 0.85f) * s->coastX), len = (int)(frand(4, 10) * k);
        if (s->F[x] - s->seaY < 14 * k) continue;
        for (int j = 0; j < len; j++)
            put(&T, x + (int)roundf(sinf(j * 0.7f + i) * 0.8f), (int)s->F[x] - 1 - j, rgb(j & 1 ? 0x2E8047 : 0x3FA05A));
    }
    {
        int x = (int)(0.55f * s->coastX), y = (int)s->F[x] - 1;
        uint32_t pink = rgb(0xFF7E8A);
        put(&T, x, y, pink); put(&T, x - 1, y, pink); put(&T, x + 1, y, pink); put(&T, x, y - 1, pink);
        put(&T, x - 2, y + 1, pink); put(&T, x + 2, y + 1, pink);
    }
    s->terrain = canvas_texture(r, &T);
    s->lush = canvas_texture(r, &L);

    /* where the stream leaves the snow */
    s->streamX = s->peakX;
    while (s->streamX > s->lakeR + 2 && s->M[s->streamX] < 0.36f * lh) s->streamX--;
    free(M1);
    free(M2);
    free(H);
}

static Canvas make_boat(void) {
    Canvas c = canvas_new(14, 13);
    for (int i = 0; i < 8; i++)                                /* main sail */
        for (int x = 7 - (i * 6) / 8; x <= 6; x++) put(&c, x, 1 + i, rgb(x == 6 ? 0xDDE3EE : 0xFFFFFF));
    for (int i = 0; i < 5; i++)                                /* jib */
        for (int x = 8; x <= 8 + (i * 3) / 4; x++) put(&c, x, 4 + i, rgb(0xFFE8A8));
    for (int y = 0; y <= 9; y++) put(&c, 7, y, rgb(0x6B4A2F));
    put(&c, 8, 0, rgb(0xE84A4A));
    put(&c, 9, 0, rgb(0xE84A4A));
    put(&c, 8, 1, rgb(0xE84A4A));
    fill_rect(&c, 0, 10, 13, 10, rgb(0xFFFFFF));
    fill_rect(&c, 1, 11, 12, 11, rgb(0xD9473F));
    fill_rect(&c, 3, 12, 10, 12, rgb(0xA8352F));
    return c;
}

static Canvas make_fish(void) {
    static const char *const art[5] = { "...ooo..", "o.ooooo.", "ooooooeo", "o.bbbbb.", "...bbb.." };
    Canvas c = canvas_new(8, 5);
    for (int y = 0; y < 5; y++)
        for (int x = 0; x < 8; x++) {
            char ch = art[y][x];
            if (ch != '.') put(&c, x, y, rgb(ch == 'o' ? 0xFF8A3D : ch == 'b' ? 0xFFC27A : 0x202020));
        }
    return c;
}

/* ---------- clouds ---------- */

static float cloud_r(const Water *s, float water) { return (4.0f + 2.3f * sqrtf(fmaxf(water, 0))) * s->k; }

static Cloud *new_cloud(Water *s, float x, float base, float water) {
    static const float PX[NPUFF] = { -0.95f, -0.55f, -0.12f, 0.3f, 0.68f, 0.98f, 0.05f };
    static const float PY[NPUFF] = { 0.28f, 0.52f, 0.72f, 0.66f, 0.48f, 0.26f, 0.98f };
    static const float PR[NPUFF] = { 0.40f, 0.52f, 0.60f, 0.56f, 0.46f, 0.36f, 0.44f };
    for (int i = 0; i < MAXCLOUD; i++) {
        Cloud *c = &s->c[i];
        if (c->on) continue;
        memset(c, 0, sizeof *c);
        c->on = 1;
        c->x = x;
        c->base = base;
        c->water = water;
        c->r = cloud_r(s, water);
        for (int j = 0; j < NPUFF; j++) {
            c->px[j] = PX[j] + frand(-0.1f, 0.1f);
            c->py[j] = PY[j] * frand(0.85f, 1.15f);
            c->pr[j] = PR[j] * frand(0.85f, 1.15f);
        }
        return c;
    }
    return NULL;
}

static float cloud_top(const Cloud *c) {
    float top = c->base;
    for (int j = 0; j < NPUFF; j++) top = fminf(top, c->base - (c->py[j] + c->pr[j]) * c->r);
    return top;
}

/* The highest ground under a cloud: clouds ride up over the hills and snag on the peaks. */
static float ground_under(const Water *s, float x, float r) {
    float g = 1e9f;
    for (int xi = col_at(s, x - r * 0.6f); xi <= col_at(s, x + r * 0.6f); xi++) {
        float land = xi < s->coastX ? s->seaY - s->D : (xi > s->outletX && xi < s->lakeR) ? s->rimY - s->D : s->F[xi] - s->D;
        g = fminf(g, fminf(land, s->M[xi]));
    }
    return g;
}

/* A big storm cloud over the hills (B in windowed mode, and something a kid could ask for later). */
static void make_storm(Water *s) {
    Cloud *c = NULL;
    for (int i = 0; i < MAXCLOUD && !c; i++)
        if (!s->c[i].on) c = &s->c[i];
    if (!c) {                                                   /* every slot taken: the smallest cloud makes way */
        c = &s->c[0];
        for (int i = 1; i < MAXCLOUD; i++)
            if (s->c[i].water < c->water) c = &s->c[i];
        c->on = 0;
    }
    c = new_cloud(s, frand(0.34f, 0.42f) * s->lw, s->condY, 380);
    if (!c) return;
    c->storm = 1;
    c->fade = 0.2f;
    c->base = fmaxf(s->condY, c->r * 1.5f + 0.03f * s->lh);
    s->humid = fmaxf(s->humid, 1.2f);
}

/* ---------- particles ---------- */

static Part *spawn(Water *s, int kind, float x, float y) {
    if (s->np >= MAXPART) return NULL;
    Part *q = &s->p[s->np++];
    memset(q, 0, sizeof *q);
    q->kind = kind;
    q->x = x;
    q->y = y;
    q->life = 1;
    q->ph = frand(0, 6.28f);
    return q;
}

/* Where a drop falling at x lands, and on what. z: 0 = the far side of the strip, 1 = the cut. */
static float land_at(const Water *s, float x, float z, int *where) {
    int xi = col_at(s, x);
    float D = (float)s->D;
    if (xi < s->coastX) { *where = W_SEA; return s->seaY - D + z * D; }
    if (xi > s->outletX && xi < s->lakeR && s->F[xi] > s->lake) { *where = W_LAKE; return s->lake - D + z * D; }
    float far = s->F[xi] - D;
    if (s->M[xi] < far - 2 && z < 0.6f) { *where = W_MTN; return s->M[xi] + z / 0.6f * fminf(10 * s->k, far - s->M[xi]); }
    *where = xi > s->lakeR ? W_FOOT : W_HILL;
    return far + z * D;
}

static void splash(Water *s, float x, float y, int n) {
    for (int i = 0; i < n; i++) {
        Part *q = spawn(s, P_SPLASH, x, y - 0.5f);
        if (!q) return;
        q->vx = frand(-10, 10) * s->k;
        q->vy = frand(-22, -10) * s->k;
        q->life = frand(0.2f, 0.35f);
    }
}

static void seep(Water *s, float x, float y) {
    int n = 0;
    for (int i = 0; i < s->np; i++) n += s->p[i].kind == P_SEEP;
    if (n > 90) return;
    Part *q = spawn(s, P_SEEP, x, y);
    if (!q) return;
    q->vy = frand(3, 5) * s->k;
    q->life = 1;
    q->land = frand(-0.3f, 0.3f);
}

static void rain_from(Water *s, Cloud *c, float units) {
    c->rainAcc += units * DROPS * (c->storm ? 1.5f : 1);
    while (c->rainAcc >= 1) {
        c->rainAcc -= 1;
        float x = c->x + frand(-0.85f, 0.85f) * c->r;
        int xi = col_at(s, x);
        int snowy = s->M[xi] < s->snowLineY && s->M[xi] < s->F[xi] - s->D - 2;
        if (snowy && irand(3)) continue;                        /* a third as many flakes, each worth more */
        Part *q = spawn(s, snowy ? P_SNOW : P_RAIN, x, c->base - frand(0, 2) * s->k);
        if (!q) return;
        float z = snowy ? frand(0, 0.25f) : frand(0, 1);
        q->land = land_at(s, x, z, &q->where);
        q->vy = (snowy ? frand(0.07f, 0.1f) : frand(0.5f, 0.62f)) * s->lh;
        q->vx = snowy ? 0 : s->wind * 0.15f;
        if (snowy) s->snowTotal += 1.0f / 6; else s->rainTotal += 1.0f / DROPS;
    }
}

/* A drop arrives: the water goes into the lake, the river, the stream, the ground or the snowcap. */
static void landed(Water *s, Part *q) {
    float unit = 1.0f / DROPS;
    int xi = col_at(s, q->x);
    if (q->kind == P_SNOW) {
        float d = 0.12f * s->k;
        s->snow[xi] = fminf(s->snow[xi] + d, 7 * s->k);
        if (xi > 0) s->snow[xi - 1] = fminf(s->snow[xi - 1] + d * 0.5f, 7 * s->k);
        if (xi < s->lw - 1) s->snow[xi + 1] = fminf(s->snow[xi + 1] + d * 0.5f, 7 * s->k);
        return;
    }
    if (irand(3) == 0) splash(s, q->x, q->land, 1 + irand(2));
    switch (q->where) {
    case W_LAKE: s->lake -= unit * LAKE_GAIN * s->k; s->collectRate += unit; break;
    case W_HILL:
        s->riverQ += unit * 0.6f;
        s->groundQ += unit * 0.4f;
        s->moist = fminf(1, s->moist + unit * 0.02f);
        s->collectRate += unit;
        if (irand(45) == 0) seep(s, q->x, s->F[xi] + 2 * s->k);
        break;
    case W_FOOT:
    case W_MTN:
        s->streamQ += unit * 0.7f;
        s->groundQ += unit * 0.3f;
        s->collectRate += unit;
        if (irand(60) == 0 && q->where == W_FOOT) seep(s, q->x, s->F[xi] + 2 * s->k);
        break;
    default: break;
    }
}

/* ---------- simulation ---------- */

/* The sun climbs from behind the mountains and sets into the sea (rainbows come out on the other side). */
static float sun_x(const Water *s) { return s->lw * (1.04f - 1.08f * s->phase / SUNSET); }

static void sky_update(Water *s) {
    float p = s->phase;
    if (p < SUNSET) s->sunAlt = s->elev = sinf(PI_F * p / SUNSET);
    else { s->sunAlt = 0; s->elev = -0.7f * sinf(PI_F * (p - SUNSET) / (1 - SUNSET)); }
    float day = smooth(-0.14f, 0.32f, s->elev);
    float warm = 1 - smooth(0.0f, 0.4f, fabsf(s->elev + 0.03f));
    static const float night[3] = { 0.34f, 0.40f, 0.66f }, dusk[3] = { 1.0f, 0.80f, 0.66f };
    for (int i = 0; i < 3; i++) {
        float v = night[i] + (1 - night[i]) * day;
        v = v + (dusk[i] * v - v) * warm * day;
        s->light[i] = v * (1 - 0.22f * s->overcast);
    }
}

static void evaporate(Water *s, float dt) {
    float lh = (float)s->lh, D = (float)s->D;
    s->seaAcc += (0.5f + 10.0f * s->sunAlt) * s->humid * dt;
    while (s->seaAcc >= 1) {
        s->seaAcc -= 1;
        Part *q = spawn(s, P_WISP, frand(2, s->coastX - 2), s->seaY - D * frand(0.15f, 0.95f));
        if (!q) break;
        q->vy = -frand(0.075f, 0.105f) * lh;
        q->where = W_SEA;
    }
    s->lakeAcc += (0.15f + 2.6f * s->sunAlt) * s->humid * dt;
    while (s->lakeAcc >= 1) {
        s->lakeAcc -= 1;
        int x = s->outletX + 2 + irand(s->lakeR - s->outletX - 3 > 1 ? s->lakeR - s->outletX - 3 : 1);
        if (s->F[x] <= s->lake) continue;
        Part *q = spawn(s, P_WISP, x + frand(0, 1), s->lake - D * frand(0.15f, 0.95f));
        if (!q) break;
        q->vy = -frand(0.075f, 0.105f) * lh;
        q->where = W_LAKE;
        s->lake += 0.08f * LAKE_GAIN * s->k;
    }
}

static void clouds_step(Water *s, float dt) {
    float lh = (float)s->lh;
    float decay = expf(-dt / 3.0f);
    s->overcast = 0;
    for (int i = 0; i < MAXCLOUD; i++) {
        Cloud *c = &s->c[i];
        if (!c->on) continue;
        c->age += dt;
        c->gain = c->gain * decay + c->got / 3.0f;
        c->got = 0;
        float rt = cloud_r(s, c->water);
        c->r += (rt - c->r) * fminf(1, dt * (c->storm ? 3.0f : 1.2f));
        float speed = s->wind * (c->x > s->peakX - c->r * 0.5f ? 0.2f : c->x > s->lakeR ? 0.45f : c->x > s->outletX ? 0.75f : 1);
        if (c->raining) speed *= 0.6f;
        c->x += speed * dt;
        float height = c->base - cloud_top(c);
        float want = fminf(s->condY, ground_under(s, c->x, c->r) - 0.06f * lh);
        want = fmaxf(want, height + 0.025f * lh);
        c->base += (want - c->base) * fminf(1, dt * 0.6f);
        c->dark += (clampf((c->water - 22) / 110, 0, 1) * 0.85f + c->storm * 0.15f - c->dark) * fminf(1, dt);

        int land = c->x > s->coastX + c->r * 0.3f, mtn = c->x > s->lakeR;
        float thr = (mtn ? 34 : 56) * (s->sunAlt > 0 ? 1 : 1.25f);
        if (!c->raining && !c->dying && ((land && c->water > thr) || c->water > thr * 2.4f || (c->storm && land))) {
            c->raining = 1;
            g_rush = 0;                                         /* F: here is the rain, back to normal speed */
        }
        if (c->raining) {
            float rate = (1.0f + c->water * 0.04f) * (c->storm ? 1.4f : 1);
            float u = fminf(c->water, rate * dt);
            c->water -= u;
            rain_from(s, c, u);
            if (c->water < 7) {
                c->raining = 0;
                c->dying = 1;
                if (s->sunAlt > 0.12f && s->rainbow <= 0 && fabsf(c->x - sun_x(s)) > 0.3f * s->lw) { s->rainbow = 24; s->rainbowX = c->x; }
            }
        }
        if (!c->dying && !c->raining && c->water < 5 && c->age > 45) c->dying = 1;
        if (c->dying) {
            c->fade -= dt * 0.35f;
            c->water = fmaxf(0, c->water - dt * 0.5f);
            if (c->fade <= 0) c->on = 0;
        } else c->fade = fminf(1, c->fade + dt * 0.7f);
        if (c->x - c->r * 1.2f > s->lw) c->on = 0;
        s->overcast += c->dark * c->dark * c->r * c->fade / (s->lw * (c->storm ? 0.12f : 0.3f));
    }
    s->overcast = clampf(s->overcast, 0, 1);
    /* clouds that bump together join up */
    for (int i = 0; i < MAXCLOUD; i++)
        for (int j = i + 1; j < MAXCLOUD; j++) {
            Cloud *a = &s->c[i], *b = &s->c[j];
            if (!a->on || !b->on || a->dying || b->dying) continue;
            if (fabsf(a->x - b->x) > (a->r + b->r) * 0.5f || fabsf(a->base - b->base) > 12 * s->k) continue;
            if (b->water > a->water) { Cloud t = *a; *a = *b; *b = t; }
            a->x = (a->x * a->water + b->x * b->water) / (a->water + b->water + 0.01f);
            a->water += b->water;
            a->gain += b->gain;
            a->storm |= b->storm;
            b->on = 0;
        }
}

static void parts_step(Water *s, float dt) {
    float lh = (float)s->lh, k = s->k;
    for (int i = 0; i < s->np; i++) {
        Part *q = &s->p[i];
        int dead = 0;
        switch (q->kind) {
        case P_WISP: {
            q->y += q->vy * dt;
            q->x += s->wind * 0.25f * dt;
            q->ph += dt;
            Cloud *best = NULL;
            float bd = 0.12f * s->lw;
            for (int j = 0; j < MAXCLOUD; j++) {
                Cloud *c = &s->c[j];
                if (!c->on || c->dying) continue;
                float d = fabsf(c->x - q->x);
                if (d < c->r * 1.1f && q->y <= c->base - c->r * 0.15f) { best = c; bd = -1; break; }
                if (d < bd + c->r) { bd = d - c->r; best = c; }
            }
            if (bd < 0 && best) { best->water += WISP; best->got += WISP; dead = 1; }
            else if (q->y <= s->condY) {
                if (best) { best->water += WISP; best->got += WISP; }
                else {
                    Cloud *c = new_cloud(s, q->x, s->condY, WISP);
                    if (c) c->got = WISP;
                }
                dead = 1;
            }
            break;
        }
        case P_RAIN:
        case P_SNOW:
            q->y += q->vy * dt;
            if (q->kind == P_SNOW) q->x += (sinf(s->t * 2 + q->ph) * 4 * k + s->wind * 0.1f) * dt;
            else q->x += q->vx * dt;
            if (q->y >= q->land) { landed(s, q); dead = 1; }
            break;
        case P_SPLASH:
            q->vy += 90 * k * dt;
            q->x += q->vx * dt;
            q->y += q->vy * dt;
            q->life -= dt;
            dead = q->life <= 0;
            break;
        case P_SEEP: {
            int xi = col_at(s, q->x);
            float mid = (s->wt[xi] + s->br[xi]) * 0.5f + q->land * (s->br[xi] - s->wt[xi]);
            if (q->vy > 0) {                                   /* soaking down to the water table */
                q->y += q->vy * dt;
                if (q->y >= mid) q->vy = 0;
            } else {                                           /* then creeping downhill to the sea */
                q->x -= frand(2.5f, 4.0f) * k * dt;
                q->y += (mid - q->y) * fminf(1, dt * 1.5f);
            }
            if (q->x < s->coastX - 0.04f * s->lw) q->life -= dt * 0.8f;
            dead = q->life <= 0 || q->x < 0;
            break;
        }
        }
        if (dead) s->p[i--] = s->p[--s->np];
    }
    (void)lh;
}

static void water_step(Water *s, float dt) {
    float k = s->k;
    s->snowLineY = (0.37f + 0.04f * (1 - smooth(-0.1f, 0.3f, s->elev))) * s->lh;
    /* sunshine melts the snow, faster lower down */
    for (int x = s->lakeR; x < s->lw; x++) {
        if (s->snow[x] <= 0) continue;
        float low = clampf((s->M[x] - s->peakY) / (s->snowLineY - s->peakY + 1), 0, 1.5f);
        float m = fminf(s->snow[x], 0.036f * k * s->sunAlt * (0.4f + low) * dt);
        s->snow[x] -= m;
        s->streamQ += m / k * 1.5f;
        s->collectRate += m / k * 1.5f;
    }
    /* the stream runs into the lake, the lake spills into the river */
    float sf = s->streamQ * fminf(1, 0.18f * dt);
    s->streamQ -= sf;
    s->lake -= sf * LAKE_GAIN * k;
    float over = s->rimY + 1.5f * k - s->lake;
    if (over > 0) {
        float out = over / k * 1.6f * dt;
        s->lake += out * LAKE_GAIN * k;
        s->riverQ += out;
    }
    s->lake = clampf(s->lake, s->rimY - 4 * k, s->bedY - 3 * k);
    /* groundwater feeds the river slowly; the river runs out to sea */
    float gb = s->groundQ * fminf(1, 0.03f * dt);
    s->groundQ -= gb;
    s->riverQ += gb * 0.5f;
    s->riverQ -= s->riverQ * fminf(1, 0.1f * dt);
    s->moist = clampf(s->moist - 0.005f * dt, 0.1f, 1);
    s->collectRate *= expf(-dt / 4.0f);
    s->flowPh += (5 + s->riverQ * 0.4f) * k * dt;
    s->streamPh += (6 + s->streamQ * 0.5f) * k * dt;
    /* the ground keeps a few drops creeping toward the sea */
    s->seepAcc += (0.15f + fminf(s->groundQ, 80) * 0.015f) * dt;
    while (s->seepAcc >= 1) {
        s->seepAcc -= 1;
        int x = s->coastX + irand(s->lw - s->coastX);
        Part *q = spawn(s, P_SEEP, (float)x, (s->wt[x] + s->br[x]) * 0.5f);
        if (q) q->land = frand(-0.3f, 0.3f);
    }
}

static void life_step(Water *s, float dt) {
    float k = s->k;
    /* the boat sails back and forth */
    s->boatX += s->boatDir * 1.6f * k * dt;
    if (s->boatX < 0.05f * s->lw) s->boatDir = 1;
    if (s->boatX > s->coastX - 0.07f * s->lw) s->boatDir = -1;
    /* a fish jumps in the lake now and then */
    if (s->fishAge > 0) {
        s->fishAge += dt;
        if (s->fishAge > 1.1f) {
            s->fishAge = 0;
            splash(s, s->fishX + s->fishDir * 12 * k, s->lake - s->D * 0.5f, 4);
        }
    } else if ((s->fishT -= dt) <= 0) {
        s->fishT = frand(9, 22);
        int span = s->lakeR - s->outletX;
        if (span > 20 * k && s->elev > -0.1f) {
            s->fishDir = irand(2) ? 1 : -1;
            s->fishX = s->outletX + span * 0.5f - s->fishDir * span * frand(0.05f, 0.2f);
            s->fishAge = 0.001f;
            splash(s, s->fishX, s->lake - s->D * 0.5f, 3);
        }
    }
    /* flowers open after rain and close up in a dry spell */
    for (int i = 0; i < s->nfl; i++) {
        Flower *f = &s->fl[i];
        f->g = clampf(f->g + (s->moist > 0.4f ? 0.22f : -0.012f) * dt, 0.15f, 1);
    }
    if (s->rainbow > 0) s->rainbow -= dt;
}

/* ---------- labels ---------- */

static int label_h(const Water *s) { return FONT_H * s->sc; }

static void labels_step(Water *s, float dt) {
    if (!s->showLabels) return;
    int sc = s->sc, u = s->u, W = s->w, Hh = s->h, lhPx = label_h(s), pad = sc * 3;
    float tx[NLABEL], ty[NLABEL];
    int ok[NLABEL] = { 0 };
    /* evaporation: wisps rising off the sea */
    int n = 0;
    float sum = 0;
    for (int i = 0; i < s->np; i++)
        if (s->p[i].kind == P_WISP && s->p[i].where == W_SEA) { n++; sum += s->p[i].x; }
    s->act[L_EVAP] = n >= 5;
    tx[L_EVAP] = clampf((n ? sum / n : s->coastX * 0.5f) * u - s->labW[L_EVAP] * 0.5f, pad, s->coastX * u - s->labW[L_EVAP]);
    ty[L_EVAP] = (s->seaY - s->D) * u - 0.075f * Hh - lhPx;
    ok[L_EVAP] = 1;
    /* condensation: the cloud gathering the most vapor that isn't raining yet */
    const Cloud *cg = NULL, *cp = NULL;
    float best = 0.6f;
    for (int i = 0; i < MAXCLOUD; i++) {
        const Cloud *c = &s->c[i];
        if (c->on && !c->raining && !c->dying && c->fade > 0.5f && c->gain > best && c->water > 6) { best = c->gain; cg = c; }
    }
    s->act[L_COND] = cg != NULL;
    if (cg) {
        tx[L_COND] = cg->x * u - s->labW[L_COND] * 0.5f;
        ty[L_COND] = cloud_top(cg) * u - pad - lhPx;
        if (ty[L_COND] < pad * 2) ty[L_COND] = cg->base * u + pad;
        ok[L_COND] = 1;
    }
    /* precipitation: beside the heaviest rain */
    best = 0;
    for (int i = 0; i < MAXCLOUD; i++) {
        const Cloud *c = &s->c[i];
        if (c->on && c->raining && c->water > best) { best = c->water; cp = c; }
    }
    s->act[L_PRECIP] = cp != NULL;
    if (cp) {
        tx[L_PRECIP] = (cp->x + cp->r * 1.05f) * u + pad;
        if (tx[L_PRECIP] + s->labW[L_PRECIP] > W - pad) tx[L_PRECIP] = (cp->x - cp->r * 1.05f) * u - pad - s->labW[L_PRECIP];
        ty[L_PRECIP] = cp->base * u + 0.05f * Hh;
        ok[L_PRECIP] = 1;
    }
    /* collection: in the ground under the river, while water is running in */
    s->act[L_COLL] = s->collectRate > 2.0f;
    {
        int mx = (s->beachX + s->outletX) / 2;
        tx[L_COLL] = mx * u - s->labW[L_COLL] * 0.5f;
        ty[L_COLL] = (s->F[mx] + 7 * s->k) * u;
        ok[L_COLL] = 1;
    }
    /* keep them on screen, off the ring and off each other (rain first, it moves most) */
    static const int order[NLABEL] = { L_PRECIP, L_COND, L_EVAP, L_COLL };
    SDL_Rect placed[NLABEL + 2];
    int np = 0;
    placed[np++] = s->ring;
    if (s->phase < SUNSET) {                                    /* and off the sun */
        float hz = s->seaY - s->D, f = s->phase / SUNSET, sr = 0.07f * s->lh * u;
        float sy = (hz - (hz - 0.1f * s->lh) * sinf(PI_F * f)) * u;
        placed[np++] = (SDL_Rect){ (int)(sun_x(s) * u - sr), (int)(sy - sr), (int)(2 * sr), (int)(2 * sr) };
    }
    for (int oi = 0; oi < NLABEL; oi++) {
        int i = order[oi];
        if (!ok[i]) continue;
        tx[i] = clampf(tx[i], 2 * pad, W - 2 * pad - s->labW[i]);
        ty[i] = clampf(ty[i], 2 * pad, Hh - 2 * pad - lhPx);
        if (s->alpha[i] < 0.02f && !s->act[i]) continue;
        for (int pass = 0; pass < 2; pass++)
            for (int j = 0; j < np; j++) {
                SDL_Rect a = { (int)tx[i] - pad, (int)ty[i] - pad, s->labW[i] + 2 * pad, lhPx + 2 * pad }, b = placed[j];
                if (!SDL_HasIntersection(&a, &b)) continue;
                if (b.x + b.w + pad + s->labW[i] < W - pad && b.y < (int)ty[i] + lhPx) tx[i] = (float)(b.x + b.w + pad);
                else ty[i] = (float)(b.y + b.h + pad);
            }
        placed[np++] = (SDL_Rect){ (int)tx[i] - pad, (int)ty[i] - pad, s->labW[i] + 2 * pad, lhPx + 2 * pad };
    }
    for (int i = 0; i < NLABEL; i++) {
        if (s->act[i]) s->hold[i] = 4.5f;
        else s->hold[i] -= dt;
        float want = s->hold[i] > 0 ? 1.0f : 0.0f;
        s->alpha[i] += clampf(want - s->alpha[i], -dt * 0.7f, dt * 1.2f);
        if (!ok[i]) continue;
        if (s->alpha[i] < 0.02f || !s->labOK[i]) { s->lx[i] = tx[i]; s->ly[i] = ty[i]; s->labOK[i] = 1; }
        else {
            s->lx[i] += (tx[i] - s->lx[i]) * fminf(1, dt * 2.0f);
            s->ly[i] += (ty[i] - s->ly[i]) * fminf(1, dt * 2.0f);
        }
    }
    for (int i = 0; i < NLABEL; i++)
        s->lrect[i] = (SDL_Rect){ (int)s->lx[i] - pad, (int)s->ly[i] - pad, s->labW[i] + 2 * pad, lhPx + 2 * pad };
}

static void step(void *p, float dt) {
    Water *s = p;
    if (dt > 0.1f) dt = 0.1f;
    s->t += dt;
    s->dayT += dt;
    if (s->dayT >= DAY_SECONDS) {
        s->dayT -= DAY_SECONDS;
        s->dayN++;
        s->humid = frand(0.75f, 1.3f);                          /* some days are muggier than others */
    }
    s->phase = s->dayT / DAY_SECONDS;
    sky_update(s);
    if (g_force_boom) {
        g_force_boom = 0;
        make_storm(s);
    }
    s->wind = s->lw * 0.011f * (0.8f + 0.4f * sinf(s->t * 0.05f)) * (s->sunAlt > 0 ? 1 : 0.6f);
    evaporate(s, dt);
    clouds_step(s, dt);
    parts_step(s, dt);
    water_step(s, dt);
    life_step(s, dt);
    labels_step(s, dt);
}

/* ---------- drawing ---------- */

static void sky_colors(const Water *s, uint32_t *top, uint32_t *hor) {
    float day = smooth(-0.14f, 0.32f, s->elev);
    float warm = 1 - smooth(0.0f, 0.4f, fabsf(s->elev + 0.03f));
    *top = mixc(0x0A0F2E, 0x3C8CE0, day);
    *hor = mixc(0x1E2858, 0xB8E2FA, day);
    *hor = mixc(*hor, 0xFFA36B, warm * 0.85f);
    *top = mixc(*top, 0x5A5FA8, warm * 0.4f);
    *top = mixc(*top, mixc(0x1A1E2A, 0x77839A, day), s->overcast * 0.6f);
    *hor = mixc(*hor, mixc(0x2A2E3A, 0xA3ADBC, day), s->overcast * 0.6f);
}

static void draw_sky(Water *s, SDL_Renderer *r) {
    uint32_t top, hor;
    sky_colors(s, &top, &hor);
    float W = (float)s->w, H = (float)s->h, hy = (s->seaY - s->D) * s->u;
    SDL_Color a = hexcol(top), b = hexcol(hor);
    SDL_Vertex v[6] = { { { 0, 0 }, a, { 0, 0 } }, { { W, 0 }, a, { 0, 0 } }, { { W, hy }, b, { 0, 0 } },
                        { { 0, hy }, b, { 0, 0 } }, { { W, H }, b, { 0, 0 } }, { { 0, H }, b, { 0, 0 } } };
    int idx[12] = { 0, 1, 2, 0, 2, 3, 3, 2, 4, 3, 4, 5 };
    SDL_RenderGeometry(r, NULL, v, 6, idx, 12);

    /* stars and the moon at night */
    float starA = 1 - smooth(-0.32f, -0.06f, s->elev);
    if (starA > 0.01f) {
        for (int pass = 0; pass < 2; pass++) {
            for (int i = 0; i < MAXSTAR; i++) {
                const Star *st = &s->st[i];
                float tw = 0.5f + 0.5f * sinf(s->t * 1.7f + st->ph);
                if ((tw > 0.5f) != pass) continue;
                add(s, (int)st->x, (int)st->y, 1, 1);
                if (st->big && tw > 0.7f) { add(s, (int)st->x - 1, (int)st->y, 3, 1); add(s, (int)st->x, (int)st->y - 1, 1, 3); }
            }
            flush(s, r, (SDL_Color){ 255, 250, 225, (Uint8)(starA * (pass ? 255 : 130)) });
        }
        float f = (s->phase - SUNSET) / (1 - SUNSET);
        if (f > 0 && f < 1) {
            float hz = s->seaY - s->D;
            float mx = s->lw * (1.04f - 1.08f * f), my = hz - (hz - 0.13f * s->lh) * sinf(PI_F * f), mr = 0.032f * s->lh;
            fill_circle(r, mx * s->u, my * s->u, mr * 2.2f * s->u, (SDL_Color){ 200, 215, 255, (Uint8)(starA * 30) });
            disc(s, mx, my, mr);
            flush(s, r, (SDL_Color){ 240, 240, 220, (Uint8)(starA * 255) });
            disc(s, mx + mr * 0.3f, my - mr * 0.2f, mr * 0.25f);
            disc(s, mx - mr * 0.35f, my + mr * 0.3f, mr * 0.18f);
            flush(s, r, (SDL_Color){ 205, 205, 190, (Uint8)(starA * 255) });
        }
    }
    /* the sun: rises behind the mountains and sets into the sea */
    if (s->phase < SUNSET) {
        float f = s->phase / SUNSET, hz = s->seaY - s->D;
        float sx = sun_x(s), sy = hz - (hz - 0.1f * s->lh) * sinf(PI_F * f), sr = 0.045f * s->lh;
        float warm = 1 - smooth(0.05f, 0.45f, s->elev);
        SDL_Color glow = hexcol(mixc(0xFFF4C0, 0xFFB070, warm));
        glow.a = 40;
        fill_circle(r, sx * s->u, sy * s->u, sr * 3.2f * s->u, glow);
        glow.a = 60;
        fill_circle(r, sx * s->u, sy * s->u, sr * 1.9f * s->u, glow);
        disc(s, sx, sy, sr);
        flush(s, r, hexcol(mixc(0xFFE680, 0xFF9A50, warm)));
        disc(s, sx - sr * 0.25f, sy - sr * 0.25f, sr * 0.55f);
        flush(s, r, hexcol(mixc(0xFFF6C8, 0xFFC890, warm)));
    }
}

static void draw_waters(Water *s, SDL_Renderer *r) {
    int D = s->D, lake = (int)roundf(s->lake);
    float k = s->k;
    /* the lake: its surface from above, and its depth in the cut */
    for (int band = 0; band < 4; band++) {
        for (int x = s->outletX + 1; x < s->lakeR; x++) {
            int bed = (int)roundf(s->F[x]);
            if (bed <= lake) continue;
            if (band == 0) add(s, x, lake - D, 1, D / 2);
            else if (band == 1) add(s, x, lake - D + D / 2, 1, D - D / 2);
            else if (band == 2) add(s, x, lake, 1, bed - lake < (int)(3 * k) ? bed - lake : (int)(3 * k));
            else add(s, x, lake + (int)(3 * k), 1, bed - lake - (int)(3 * k));
        }
        static const uint32_t LC[4] = { 0x72BEEA, 0x55A6DE, 0x4392D2, 0x2C6EB2 };
        flush(s, r, lc(s, LC[band], 1));
    }
    for (int x = s->outletX + 2; x < s->lakeR - 1; x++)       /* glints */
        if (s->F[x] > lake + 1 && ((x * 7 + (int)(s->t * 3)) % 11) == 0) add(s, x, lake - D + 1 + (x % (D > 2 ? D - 1 : 1)), 2, 1);
    flush(s, r, lc(s, 0xD8F2FF, 0.8f));

    /* the river winds down from the lake to the sea */
    int wr = 1 + (s->riverQ > 5) + (s->riverQ > 16);
    for (int x = s->coastX - 2; x <= s->outletX + 1; x++) {
        float a = river_y(s, x), b = river_y(s, x + 1);
        int w = wr + (x < s->coastX + 4 * k);
        int y0 = (int)floorf(fminf(a, b) - w * 0.5f + 0.5f), y1 = (int)floorf(fmaxf(a, b) + w * 0.5f + 0.5f);
        add(s, x, y0, 1, y1 - y0 > w ? y1 - y0 : w);
    }
    flush(s, r, lc(s, 0x3A98DE, 1));
    for (int x = s->coastX; x <= s->outletX; x++)
        if (((x + (int)s->flowPh) % (int)(7 * k + 1)) == 0) add(s, x, (int)floorf(river_y(s, x)), 1, 1);
    flush(s, r, lc(s, 0xCFF2FF, 1));

    /* the stream from the snow down to the lake, when water is running */
    float sa = clampf(s->streamQ / 1.5f, 0, 1);
    if (sa > 0.05f) {
        float x0 = (float)s->streamX, y0 = s->M[s->streamX] + 3 * k, x1 = s->lakeR - 1.0f, y1 = s->lake - D * 0.5f;
        int N = 22;
        float px = x0, py = y0, wid = (1 + (s->streamQ > 8)) * s->u;
        SDL_Color c = lc(s, 0x6CC4F2, sa);
        for (int i = 1; i <= N; i++) {
            float t = (float)i / N, wig = i < N ? sinf(i * 0.9f) * 1.6f * k : 0;
            float nx = x0 + (x1 - x0) * t + wig, ny = y0 + (y1 - y0) * (t * 0.85f + 0.15f * t * t);
            thick_line(r, px * s->u, py * s->u, nx * s->u, ny * s->u, wid, c);
            px = nx;
            py = ny;
        }
        for (int i = 0; i < 4; i++) {                          /* sparkles running down it */
            float t = fmodf(s->streamPh / (60 * k) + i * 0.25f, 1.0f);
            int seg = (int)(t * N);
            float tt = (float)seg / N, wig = seg > 0 && seg < N ? sinf(seg * 0.9f) * 1.6f * k : 0;
            add(s, (int)(x0 + (x1 - x0) * tt + wig), (int)(y0 + (y1 - y0) * (tt * 0.85f + 0.15f * tt * tt)), 1, 1);
        }
        flush(s, r, lc(s, 0xE0F6FF, sa));
    }

    /* the sea: little waves along the cut and glints on top */
    for (int x = 0; x < s->coastX; x++) {
        float w = sinf(x * 0.35f / k - s->t * 2.2f) + 0.5f * sinf(x * 0.13f / k + s->t * 1.3f);
        if (w > 0.6f) add(s, x, (int)s->seaY - 1, 1, 1);
    }
    flush(s, r, lc(s, 0x8FD0F4, 1));
    for (int i = 0; i < 14; i++) {
        float x = fmodf(i * 37.7f * k + s->t * (2 + (i % 3)) * k, (float)s->coastX - 3);
        int y = (int)(s->seaY - D + 1 + (i * 5) % (D > 2 ? D - 1 : 1));
        add(s, (int)x, y, 2 + (i & 1), 1);
    }
    flush(s, r, lc(s, 0xD4F0FF, 0.75f));
}

static void draw_snow(Water *s, SDL_Renderer *r) {
    for (int pass = 0; pass < 2; pass++) {
        for (int x = s->lakeR; x < s->lw; x++) {
            if (s->snow[x] < 0.15f * s->k || (x < s->peakX) != (pass == 0)) continue;
            int top = (int)floorf(s->M[x]);
            add(s, x, top, 1, 1 + (int)(s->snow[x] * 2.5f));
        }
        flush(s, r, lc(s, pass ? 0xD6E0EE : 0xFAFCFF, 1));
    }
}

static void draw_cloud(Water *s, SDL_Renderer *r, const Cloud *c) {
    float rr = c->r, top = cloud_top(c);
    int y0 = (int)floorf(top), y1 = (int)floorf(c->base);
    float d = c->dark;
    uint32_t cols[3] = { mixc(0xFFFFFF, 0x9DA6B8, d), mixc(0xEDF1F7, 0x7A8395, d), mixc(0xC6D0E0, 0x586072, d) };
    for (int b = 0; b < 3; b++) {
        for (int y = y0; y <= y1; y++) {
            float t = (y - top) / (c->base - top + 0.01f);
            if ((t < 0.36f ? 0 : t < 0.76f ? 1 : 2) != b) continue;
            float lo[NPUFF], hi[NPUFF];
            int n = 0;
            for (int j = 0; j < NPUFF; j++) {
                float cy = c->base - c->py[j] * rr, pr = c->pr[j] * rr, dy = y + 0.5f - cy;
                if (fabsf(dy) >= pr) continue;
                float half = sqrtf(pr * pr - dy * dy), cx = c->x + c->px[j] * rr;
                int at = n++;
                while (at > 0 && lo[at - 1] > cx - half) { lo[at] = lo[at - 1]; hi[at] = hi[at - 1]; at--; }
                lo[at] = cx - half;
                hi[at] = cx + half;
            }
            for (int j = 0; j < n;) {
                float a = lo[j], e = hi[j];
                for (j++; j < n && lo[j] <= e + 0.5f; j++) e = fmaxf(e, hi[j]);
                int xa = (int)floorf(a + 0.5f), xb = (int)floorf(e + 0.5f);
                add(s, xa, y, xb - xa, 1);
            }
        }
        flush(s, r, lc(s, cols[b], c->fade));
    }
}

static void draw_rainbow(Water *s, SDL_Renderer *r) {
    if (s->rainbow <= 0) return;
    static const uint32_t RB[6] = { 0xFF5A5A, 0xFFA040, 0xFFE84A, 0x5EDC6A, 0x4BA8FF, 0xA070FF };
    float a = fminf(1, fminf((24 - s->rainbow) / 3, s->rainbow / 5)) * 0.42f * (1 - 0.5f * s->overcast);
    int xi = col_at(s, s->rainbowX);
    float cx = s->rainbowX * s->u, cy = (s->F[xi] - s->D + 4 * s->k) * s->u, R = 0.3f * s->lh * s->u;
    float bw = 1.6f * s->k * s->u;
    for (int b = 0; b < 6; b++) {
        SDL_Color c = lc(s, RB[b], a);
        float rad = R - b * bw;
        for (int i = 0; i < 28; i++) {
            float a0 = PI_F + PI_F * i / 28, a1 = PI_F + PI_F * (i + 1) / 28;
            thick_line(r, cx + cosf(a0) * rad, cy + sinf(a0) * rad, cx + cosf(a1) * rad, cy + sinf(a1) * rad, bw + 1, c);
        }
    }
}

/* A chunky arrow, 7 font dots square, pointing up, right, down or left. */
static void arrow(SDL_Renderer *r, int x, int y, int sc, int dir, SDL_Color c) {
    static const char *const art[7] = { "...#...", "..###..", ".#####.", "#######", "..###..", "..###..", "..###.." };
    set_color(r, c);
    for (int row = 0; row < 7; row++)
        for (int col = 0; col < 7; col++) {
            if (art[row][col] != '#') continue;
            int X = dir == 0 ? col : dir == 1 ? 6 - row : dir == 2 ? col : row;
            int Y = dir == 0 ? row : dir == 1 ? col : dir == 2 ? 6 - row : col;
            SDL_Rect d = { x + X * sc, y + Y * sc, sc, sc };
            SDL_RenderFillRect(r, &d);
        }
}

static void draw_labels(Water *s, SDL_Renderer *r) {
    if (!s->showLabels) return;
    int sc = s->sc, lhPx = label_h(s);
    /* the ring: the four stages going round, each quarter lit while it happens */
    {
        SDL_Rect g = s->ring;
        float cx = g.x + g.w * 0.5f, cy = g.y + g.h * 0.5f, R = g.w * 0.5f - sc * 2.5f, wd = sc * 1.6f;
        fill_circle(r, cx, cy, g.w * 0.5f, (SDL_Color){ 10, 22, 48, 90 });
        static const float start[NLABEL] = { 135, 225, 315, 45 };
        for (int i = 0; i < NLABEL; i++) {
            float lit = 0.3f + 0.7f * s->alpha[i];
            SDL_Color c = hexcol(STAGE_COL[i]);
            c.a = (Uint8)(lit * 255);
            float a0 = (start[i] + 10) * PI_F / 180, a1 = (start[i] + 70) * PI_F / 180;
            for (int j = 0; j < 6; j++) {
                float b0 = a0 + (a1 - a0) * j / 6, b1 = a0 + (a1 - a0) * (j + 1) / 6;
                thick_line(r, cx + cosf(b0) * R, cy + sinf(b0) * R, cx + cosf(b1) * R, cy + sinf(b1) * R, wd, c);
            }
            float tip = (start[i] + 82) * PI_F / 180, back = a1;
            float tx = cx + cosf(tip) * R, ty = cy + sinf(tip) * R, bx = cx + cosf(back) * R, by = cy + sinf(back) * R;
            float nx = cosf(back) * sc * 1.9f, ny = sinf(back) * sc * 1.9f;
            fill_triangle(r, tx, ty, bx + nx, by + ny, bx - nx, by - ny, c);
        }
        /* a drop in the middle */
        float dr = R * 0.38f;
        SDL_Color drop = hexcol(0x7CC8FF);
        fill_circle(r, cx, cy + dr * 0.35f, dr, drop);
        fill_triangle(r, cx, cy - dr * 1.45f, cx - dr * 0.93f, cy + dr * 0.05f, cx + dr * 0.93f, cy + dr * 0.05f, drop);
        fill_circle(r, cx - dr * 0.35f, cy + dr * 0.15f, dr * 0.25f, (SDL_Color){ 230, 246, 255, 255 });
    }
    for (int i = 0; i < NLABEL; i++) {
        float a = s->alpha[i];
        if (a < 0.02f) continue;
        a = a * a * (3 - 2 * a);
        int x = (int)s->lx[i], y = (int)s->ly[i], pad = sc * 3;
        fill_rrect(r, (SDL_Rect){ x - pad, y - pad, s->labW[i] + 2 * pad, lhPx + 2 * pad }, sc * 3, (SDL_Color){ 8, 18, 40, (Uint8)(a * 120) });
        int off = sc / 3 > 1 ? sc / 3 : 1;
        SDL_Rect d = { x + off, y + off, s->wordW[i] * sc, lhPx };
        SDL_SetTextureColorMod(s->word[i], 0, 0, 0);
        SDL_SetTextureAlphaMod(s->word[i], (Uint8)(a * 170));
        SDL_RenderCopy(r, s->word[i], NULL, &d);
        SDL_Color c = hexcol(STAGE_COL[i]);
        d.x = x;
        d.y = y;
        SDL_SetTextureColorMod(s->word[i], c.r, c.g, c.b);
        SDL_SetTextureAlphaMod(s->word[i], (Uint8)(a * 255));
        SDL_RenderCopy(r, s->word[i], NULL, &d);
        /* its arrow, nudging the way the water goes */
        static const int dir[NLABEL] = { 0, 1, 2, 3 };
        int bob = (int)roundf(sinf(s->t * 3.2f) * sc * 0.8f);
        int axp = x + s->wordW[i] * sc + sc * 3, ayp = y;
        if (dir[i] == 0) ayp -= bob; else if (dir[i] == 1) axp += bob; else if (dir[i] == 2) ayp += bob; else axp -= bob;
        c.a = (Uint8)(a * 255);
        arrow(r, axp + off, ayp + off, sc, dir[i], (SDL_Color){ 0, 0, 0, (Uint8)(a * 170) });
        arrow(r, axp, ayp, sc, dir[i], c);
    }
}

static void draw_info(Water *s, SDL_Renderer *r) {
    char line[5][96];
    float hrs = s->phase < SUNSET ? 6 + 14 * s->phase / SUNSET : 20 + 10 * (s->phase - SUNSET) / (1 - SUNSET);
    int hh = (int)hrs % 24, mm = (int)((hrs - floorf(hrs)) * 60);
    int clouds = 0, vapor = 0;
    float big = 0;
    for (int i = 0; i < MAXCLOUD; i++)
        if (s->c[i].on) { clouds++; big = fmaxf(big, s->c[i].water); }
    for (int i = 0; i < s->np; i++) vapor += s->p[i].kind == P_WISP;
    float cap = 0;
    for (int x = s->lakeR; x < s->lw; x++) cap += s->snow[x];
    float lakeFill = clampf((s->bedY - s->lake) / (s->bedY - s->rimY + 4 * s->k), 0, 1);
    snprintf(line[0], sizeof line[0], "DAY %d  TIME %d:%02d  SUN %d", s->dayN, hh, mm, (int)(s->sunAlt * 100));
    snprintf(line[1], sizeof line[1], "VAPOR %d  CLOUDS %d  BIGGEST %d  WIND %d", vapor, clouds, (int)big, (int)(s->wind / s->k));
    snprintf(line[2], sizeof line[2], "RAIN %d  SNOW %d  SNOWCAP %d", (int)s->rainTotal, (int)s->snowTotal, (int)(cap / s->k));
    snprintf(line[3], sizeof line[3], "LAKE %d  RIVER %d  STREAM %d  GROUND %d", (int)(lakeFill * 100), (int)s->riverQ,
             (int)s->streamQ, (int)s->groundQ);
    snprintf(line[4], sizeof line[4], "NEXT DAWN IN %d S", (int)(DAY_SECONDS - s->dayT));
    int sc = s->h / 360 > 2 ? s->h / 360 : 2, step = 9 * sc;
    for (int i = 0; i < 5; i++) draw_text_shadow(r, line[i], s->info.x + 6, s->info.y + 6 + i * step, sc, hexcol(0xFFFFFF));
}

static void draw(void *p, SDL_Renderer *r) {
    Water *s = p;
    int u = s->u, lw = s->lw, lh = s->lh;
    float k = s->k;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    draw_sky(s, r);

    SDL_Rect full = { 0, 0, lw * u, lh * u };
    Uint8 lr = (Uint8)(s->light[0] * 255), lg = (Uint8)(s->light[1] * 255), lb = (Uint8)(s->light[2] * 255);
    SDL_SetTextureColorMod(s->terrain, lr, lg, lb);
    SDL_RenderCopy(r, s->terrain, NULL, &full);
    SDL_SetTextureColorMod(s->lush, lr, lg, lb);
    SDL_SetTextureAlphaMod(s->lush, (Uint8)(smooth(0.1f, 0.8f, s->moist) * 255));
    SDL_RenderCopy(r, s->lush, NULL, &full);
    draw_snow(s, r);
    draw_waters(s, r);

    /* flowers on the meadow */
    for (int pass = 0; pass < 6; pass++) {
        static const uint32_t FC[5] = { 0xFF6FA8, 0xFFE14D, 0xFFFFFF, 0xFF7A4D, 0xB48CFF };
        for (int i = 0; i < s->nfl; i++) {
            const Flower *f = &s->fl[i];
            int stem = 1 + (int)(f->g * 3 * k);
            if (pass == 0) add(s, (int)f->x, (int)f->y - stem + 1, 1, stem);
            else if (f->col == pass - 1 && f->g > 0.5f) {
                add(s, (int)f->x, (int)f->y - stem, 1, 1);
                if (f->g > 0.8f) { add(s, (int)f->x - 1, (int)f->y - stem, 3, 1); add(s, (int)f->x, (int)f->y - stem - 1, 1, 1); }
            }
        }
        flush(s, r, lc(s, pass == 0 ? 0x3E8A2E : FC[pass - 1], 1));
    }

    /* groundwater creeping through the ground */
    for (int i = 0; i < s->np; i++)
        if (s->p[i].kind == P_SEEP) add(s, (int)s->p[i].x, (int)s->p[i].y, 2, 1);
    flush(s, r, lc(s, 0xCDEEFF, 0.85f));

    /* the boat, with a lantern at night */
    {
        float bx = s->boatX, by = s->seaY - s->D * 0.35f + sinf(s->t * 1.7f) * 0.6f * k;
        int bw = (int)(14 * k + 0.5f), bh = (int)(13 * k + 0.5f);
        if (bw < 7) { bw = 7; bh = 7; }
        SDL_Rect d = { (int)((bx - bw * 0.5f) * u), (int)((by - bh) * u), bw * u, bh * u };
        SDL_SetTextureColorMod(s->boat, lr, lg, lb);
        SDL_RenderCopyEx(r, s->boat, NULL, &d, sinf(s->t * 1.3f) * 4, NULL, s->boatDir < 0 ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
        float night = 1 - smooth(-0.15f, 0.1f, s->elev);
        if (night > 0.05f) {
            float lx = bx + (s->boatDir < 0 ? -1 : 1) * 1.5f * k, ly = by - bh * 0.55f;
            fill_circle(r, lx * u, ly * u, 3 * k * u, (SDL_Color){ 255, 210, 110, (Uint8)(night * 60) });
            add(s, (int)lx, (int)ly, 1, 1);
            flush(s, r, (SDL_Color){ 255, 225, 140, (Uint8)(night * 255) });
        }
    }
    /* the fish */
    if (s->fishAge > 0) {
        float t = s->fishAge / 1.1f, fx = s->fishX + s->fishDir * 12 * k * t;
        float fy = s->lake - s->D * 0.5f - 4 * t * (1 - t) * 14 * k;
        float ang = atan2f(-4 * (1 - 2 * t) * 14 * k, s->fishDir * 12 * k) * 180 / PI_F;
        int fw = (int)(8 * k + 0.5f), fh = (int)(5 * k + 0.5f);
        if (fw < 4) { fw = 4; fh = 3; }
        SDL_Rect d = { (int)((fx - fw * 0.5f) * u), (int)((fy - fh * 0.5f) * u), fw * u, fh * u };
        SDL_SetTextureColorMod(s->fish, lr, lg, lb);
        SDL_RenderCopyEx(r, s->fish, NULL, &d, s->fishDir > 0 ? ang : -ang, NULL, s->fishDir < 0 ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
    }

    /* vapor: short wavy wisps, faint at first and fading into the clouds */
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < s->np; i++) {
            const Part *q = &s->p[i];
            if (q->kind != P_WISP) continue;
            float hgt = clampf(((q->where == W_SEA ? s->seaY : s->lake) - q->y) / (0.05f * lh), 0, 1);
            if ((hgt > 0.6f) != pass) continue;
            int len = (int)(9 * k + 1);
            for (int j = 0; j < len; j++) {
                float yy = q->y + j, xx = q->x + sinf(q->ph * 3.0f + (q->y + j) * 0.45f / k) * 1.8f * k;
                add(s, (int)xx, (int)yy, 1, 1);
            }
        }
        flush(s, r, lc(s, 0xFFFFFF, pass ? 0.7f : 0.35f));
    }

    draw_rainbow(s, r);
    for (int i = 0; i < MAXCLOUD; i++)
        if (s->c[i].on) draw_cloud(s, r, &s->c[i]);

    /* rain, snow and splashes */
    for (int i = 0; i < s->np; i++)
        if (s->p[i].kind == P_RAIN) add(s, (int)s->p[i].x, (int)s->p[i].y, 1, (int)(4 * k + 0.5f) > 1 ? (int)(4 * k + 0.5f) : 2);
    flush(s, r, lc(s, 0x3F86D8, 0.9f));
    for (int i = 0; i < s->np; i++)
        if (s->p[i].kind == P_SNOW) add(s, (int)s->p[i].x, (int)s->p[i].y, 1, 1);
        else if (s->p[i].kind == P_SPLASH) add(s, (int)s->p[i].x, (int)s->p[i].y, 1, 1);
    flush(s, r, lc(s, 0xFFFFFF, 0.95f));

    draw_labels(s, r);
    if (g_show_info) draw_info(s, r);
}

/* ---------- lifecycle ---------- */

static void destroy(void *p) {
    Water *s = p;
    if (s->terrain) SDL_DestroyTexture(s->terrain);
    if (s->lush) SDL_DestroyTexture(s->lush);
    if (s->boat) SDL_DestroyTexture(s->boat);
    if (s->fish) SDL_DestroyTexture(s->fish);
    for (int i = 0; i < NLABEL; i++)
        if (s->word[i]) SDL_DestroyTexture(s->word[i]);
    free(s->F);
    free(s->M);
    free(s->snow);
    free(s->wt);
    free(s->br);
    free(s);
}

static void *create(SDL_Renderer *r, int w, int h) {
    Water *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    s->w = w;
    s->h = h;
    s->u = (h + 120) / 240 > 1 ? (h + 120) / 240 : 1;
    s->lw = (w + s->u - 1) / s->u;
    s->lh = (h + s->u - 1) / s->u;
    s->k = s->lh / 240.0f;
    s->D = (int)(0.04f * s->lh + 0.5f) > 2 ? (int)(0.04f * s->lh + 0.5f) : 2;
    s->sc = s->u;
    s->showLabels = h >= 300;
    size_t n = (size_t)s->lw;
    s->F = calloc(n, sizeof(float));
    s->M = calloc(n, sizeof(float));
    s->snow = calloc(n, sizeof(float));
    s->wt = calloc(n, sizeof(float));
    s->br = calloc(n, sizeof(float));
    if (!s->F || !s->M || !s->snow || !s->wt || !s->br) { destroy(s); return NULL; }
    build_terrain(s, r);
    s->condY = 0.33f * s->lh;
    s->lake = s->rimY + 0.5f * s->k;
    s->moist = 0.45f;
    s->riverQ = 4;
    s->groundQ = 20;
    s->humid = 1;
    s->dayN = 1;
    s->dayT = START_PHASE * DAY_SECONDS;
    s->boatX = 0.12f * s->lw;
    s->boatDir = 1;
    s->fishT = frand(3, 8);
    for (int x = s->lakeR; x < s->lw; x++)                       /* a little snow left from the night */
        if (s->M[x] < 0.33f * s->lh) s->snow[x] = 1.2f * s->k;
    for (int i = 0; i < MAXSTAR; i++)
        s->st[i] = (Star){ frand(0, (float)s->lw), frand(0, 0.58f * s->lh), frand(0, 6.28f), irand(7) == 0 };
    for (int i = 0; i < MAXFLOWER; i++) {
        int x = s->beachX + 3 + irand(s->outletX - s->beachX - 6 > 1 ? s->outletX - s->beachX - 6 : 1);
        float ry = river_y(s, x), far = s->F[x] - s->D;
        float y = ry - far > s->D * 0.5f ? far + frand(0.5f, 1.2f) : s->F[x] - frand(0.2f, 0.9f);
        s->fl[s->nfl++] = (Flower){ (float)x, y, frand(0.2f, 0.5f), irand(5) };
    }
    Canvas b = make_boat(), f = make_fish();
    s->boat = canvas_texture(r, &b);
    s->fish = canvas_texture(r, &f);
    for (int i = 0; i < NLABEL; i++) {
        Canvas c = text_canvas(WORDS[i], 0xFFFFFFFF);
        s->wordW[i] = c.w;
        s->word[i] = canvas_texture(r, &c);
        s->labW[i] = (c.w + 3 + 7) * s->sc;
    }
    {
        int sc = h / 360 > 2 ? h / 360 : 2, step = 9 * sc, wd = text_width("LAKE 100  RIVER 99  STREAM 99  GROUND 99", sc);
        s->info = (SDL_Rect){ 6, h - 18 - 5 * step, wd + 12, 5 * step + 12 };
    }
    int R = 11 * s->sc;
    s->ring = (SDL_Rect){ s->sc * 4, s->sc * 4, 2 * R, 2 * R };
    new_cloud(s, 0.14f * s->lw, s->condY, 18);
    new_cloud(s, 0.47f * s->lw, s->condY - 0.04f * s->lh, 30);
    for (int i = 0; i < MAXCLOUD; i++) s->c[i].fade = 1;
    for (int i = 0; i < 150; i++) step(s, 1.0f / 30);          /* warm up: vapor already rising */
    return s;
}

/* Dawn: the stars are going and the sky is about to brighten; a natural place to cut away. */
static int at_rest(void *p) {
    const Water *s = p;
    return s->phase > 0.985f || s->phase < 0.004f;
}

/* The labels, the ring and the read-out: the clock keeps off the words. */
static int keep_clear(void *p, SDL_Rect *out, int max) {
    const Water *s = p;
    int n = 0;
    if (s->showLabels && n < max) out[n++] = s->ring;
    for (int i = 0; i < NLABEL && n < max; i++)
        if (s->showLabels && s->alpha[i] > 0.05f) out[n++] = s->lrect[i];
    if (g_show_info && n < max) out[n++] = s->info;
    return n;
}

const Saver saver_water = { "water", "WATER", create, step, draw, destroy, at_rest, keep_clear, "B STORM   F TO RAIN   I INFO", NULL, NULL, NULL };
