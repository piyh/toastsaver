/* Lava lamp: soft glowing blobs rising and sinking (metaballs). */
#include "../engine.h"
#include <math.h>
#include <stdlib.h>

#define BLOBS 8

typedef struct { uint32_t bgTop, bgBottom, outer, core; } Palette;
static const Palette PALETTES[] = {
    { 0x2A0845, 0x5B1A7A, 0xFF5E1A, 0xFFD54F },   /* classic orange on purple */
    { 0x07153D, 0x163B7A, 0xFF4FA3, 0xFFD1EC },   /* pink on blue */
    { 0x02262B, 0x075B5E, 0x8BE04E, 0xF4F871 },   /* green on teal */
};

typedef struct { float x, baseY, ampY, freq, phase, rad, xAmp, xFreq; } Blob;

typedef struct {
    int w, h, gw, gh;
    float t;
    Blob b[BLOBS];
    Palette pal;
    uint32_t *px;
    SDL_Texture *tex;
} Lava;

static float mixf(float a, float b, float t) { return a + (b - a) * t; }

static uint32_t mix(uint32_t a, uint32_t b, float t) {
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    uint32_t r = (uint32_t)mixf((float)(a >> 16 & 255), (float)(b >> 16 & 255), t);
    uint32_t g = (uint32_t)mixf((float)(a >> 8 & 255), (float)(b >> 8 & 255), t);
    uint32_t bl = (uint32_t)mixf((float)(a & 255), (float)(b & 255), t);
    return (r << 24) | (g << 16) | (bl << 8) | 0xFF;
}

static void *create(SDL_Renderer *r, int w, int h) {
    Lava *l = calloc(1, sizeof *l);
    l->w = w;
    l->h = h;
    l->gw = w / 2 < 160 ? (w / 2 > 16 ? w / 2 : 16) : 160;
    l->gh = l->gw * h / w;
    if (l->gh < 8) l->gh = 8;
    l->pal = PALETTES[irand(3)];
    for (int i = 0; i < BLOBS; i++) {
        Blob *b = &l->b[i];
        b->x = frand(0.15f, 0.85f) * l->gw;
        b->baseY = frand(0.3f, 0.7f) * l->gh;
        b->ampY = frand(0.25f, 0.5f) * l->gh;
        b->freq = frand(0.05f, 0.12f);
        b->phase = frand(0, 6.28f);
        b->rad = frand(0.06f, 0.12f) * l->gw;
        b->xAmp = frand(0.02f, 0.08f) * l->gw;
        b->xFreq = frand(0.03f, 0.08f);
    }
    l->px = malloc(sizeof(uint32_t) * l->gw * l->gh);
    l->tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, l->gw, l->gh);
    SDL_SetTextureScaleMode(l->tex, SDL_ScaleModeLinear);
    l->t = frand(0, 100);
    return l;
}

static void step(void *p, float dt) { ((Lava *)p)->t += dt; }

static void draw(void *p, SDL_Renderer *r) {
    Lava *l = p;
    float bx[BLOBS], by[BLOBS], br2[BLOBS];
    for (int i = 0; i < BLOBS; i++) {
        Blob *b = &l->b[i];
        bx[i] = b->x + b->xAmp * sinf(l->t * b->xFreq * 6.28f + b->phase);
        by[i] = b->baseY + b->ampY * sinf(l->t * b->freq * 6.28f + b->phase);
        br2[i] = b->rad * b->rad;
    }
    for (int y = 0; y < l->gh; y++) {
        uint32_t bg = mix(l->pal.bgTop, l->pal.bgBottom, (float)y / l->gh);
        for (int x = 0; x < l->gw; x++) {
            float f = 0;
            for (int i = 0; i < BLOBS; i++) {
                float dx = x - bx[i], dy = y - by[i];
                f += br2[i] / (dx * dx + dy * dy + 0.5f);
            }
            uint32_t c;
            if (f >= 1.0f) c = mix(l->pal.outer, l->pal.core, (f - 1.0f) / 2.5f);
            else if (f > 0.82f) c = mix(mix(bg, l->pal.outer, 0.35f), l->pal.outer, (f - 0.82f) / 0.18f);
            else c = mix(bg, l->pal.outer, f * 0.35f);              /* soft glow */
            l->px[y * l->gw + x] = c;
        }
    }
    SDL_UpdateTexture(l->tex, NULL, l->px, l->gw * 4);
    SDL_RenderCopy(r, l->tex, NULL, NULL);
}

static void destroy(void *p) {
    Lava *l = p;
    SDL_DestroyTexture(l->tex);
    free(l->px);
    free(l);
}

const Saver saver_lava = { "lava", "LAVA", create, step, draw, destroy };
