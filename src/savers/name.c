/* Name banner: your name bounces around like the old DVD logo.
   Hitting a corner sets off confetti. */
#include "../engine.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define CONFETTI 220
#define STARS 70

static const uint32_t COLORS[] = { 0xFF5252, 0xFFB300, 0xFFEB3B, 0x66BB6A, 0x29B6F6, 0xAB47BC, 0xFF6EC7 };
#define NCOLORS (int)(sizeof COLORS / sizeof COLORS[0])

typedef struct { float x, y, vx, vy, life, spin; SDL_Color c; } Bit;

typedef struct {
    int w, h, sc, tw, th, color;
    float x, y, vx, vy, t, lastXHit, lastYHit, party;
    char text[20];
    SDL_Texture *tex;
    Bit bits[CONFETTI];
    float star[STARS][3];
} Name;

static void *create(SDL_Renderer *r, int w, int h) {
    Name *n = calloc(1, sizeof *n);
    n->w = w;
    n->h = h;
    snprintf(n->text, sizeof n->text, "%s", g_config.name[0] ? g_config.name : "HELLO");
    Canvas c = text_canvas(n->text, 0xFFFFFFFF);
    int tw = c.w;
    n->tex = canvas_texture(r, &c);
    n->sc = (int)(w * 0.42f / tw);
    if (n->sc < 2) n->sc = 2;
    if (n->sc * FONT_H > h / 3) n->sc = h / 3 / FONT_H > 1 ? h / 3 / FONT_H : 1;
    n->tw = tw * n->sc;
    n->th = FONT_H * n->sc;
    n->x = frand(0, (float)(w - n->tw > 1 ? w - n->tw : 1));
    n->y = frand(0, (float)(h - n->th > 1 ? h - n->th : 1));
    float sp = h * 0.2f;
    n->vx = irand(2) ? sp : -sp;
    n->vy = irand(2) ? sp * 0.8f : -sp * 0.8f;
    n->color = irand(NCOLORS);
    n->lastXHit = n->lastYHit = -10;
    for (int i = 0; i < STARS; i++) {
        n->star[i][0] = frand(0, (float)w);
        n->star[i][1] = frand(0, (float)h);
        n->star[i][2] = frand(0, 6.28f);
    }
    return n;
}

static void celebrate(Name *n, float cx, float cy) {
    n->party = 2.5f;
    for (int i = 0; i < CONFETTI; i++) {
        Bit *b = &n->bits[i];
        float a = frand(0, 2 * PI_F), s = frand(0.2f, 0.9f) * n->h;
        b->x = cx;
        b->y = cy;
        b->vx = cosf(a) * s;
        b->vy = sinf(a) * s - n->h * 0.3f;
        b->life = frand(1.5f, 3.0f);
        b->spin = frand(0, 6.28f);
        b->c = hexcol(COLORS[irand(NCOLORS)]);
    }
}

static void step(void *p, float dt) {
    Name *n = p;
    n->t += dt;
    n->x += n->vx * dt;
    n->y += n->vy * dt;
    int hitX = 0, hitY = 0;
    if (n->x < 0) { n->x = 0; n->vx = fabsf(n->vx); hitX = 1; }
    if (n->x + n->tw > n->w) { n->x = (float)(n->w - n->tw); n->vx = -fabsf(n->vx); hitX = 1; }
    if (n->y < 0) { n->y = 0; n->vy = fabsf(n->vy); hitY = 1; }
    if (n->y + n->th > n->h) { n->y = (float)(n->h - n->th); n->vy = -fabsf(n->vy); hitY = 1; }
    if (hitX) n->lastXHit = n->t;
    if (hitY) n->lastYHit = n->t;
    if (hitX || hitY) n->color = (n->color + 1 + irand(NCOLORS - 1)) % NCOLORS;
    /* A corner is two walls within a moment of each other. */
    if ((hitX || hitY) && fabsf(n->lastXHit - n->lastYHit) < 0.15f && n->party <= 0)
        celebrate(n, n->x + n->tw / 2.0f, n->y + n->th / 2.0f);
    if (n->party > 0) n->party -= dt;
    for (int i = 0; i < CONFETTI; i++) {
        Bit *b = &n->bits[i];
        if (b->life <= 0) continue;
        b->life -= dt;
        b->vy += n->h * 0.9f * dt;
        b->vx *= 1 - 0.8f * dt;
        b->x += b->vx * dt;
        b->y += b->vy * dt;
        b->spin += dt * 8;
    }
}

static void draw(void *p, SDL_Renderer *r) {
    Name *n = p;
    SDL_SetRenderDrawColor(r, 8, 10, 28, 255);
    SDL_RenderFillRect(r, NULL);
    for (int i = 0; i < STARS; i++) {
        Uint8 v = (Uint8)(90 + 80 * sinf(n->t * 1.5f + n->star[i][2]));
        SDL_SetRenderDrawColor(r, v, v, (Uint8)(v + 30 > 255 ? 255 : v + 30), 255);
        SDL_Rect s = { (int)n->star[i][0], (int)n->star[i][1], n->sc / 3 + 1, n->sc / 3 + 1 };
        SDL_RenderFillRect(r, &s);
    }
    SDL_Color c = hexcol(COLORS[n->color]);
    if (n->party > 0) c = hsv(n->t * 400, 0.8f, 1);   /* rainbow while celebrating */
    int off = n->sc / 2 > 1 ? n->sc / 2 : 1;
    SDL_Rect shadow = { (int)n->x + off, (int)n->y + off, n->tw, n->th };
    SDL_Rect dst = { (int)n->x, (int)n->y, n->tw, n->th };
    SDL_SetTextureColorMod(n->tex, 0, 0, 0);
    SDL_SetTextureAlphaMod(n->tex, 150);
    SDL_RenderCopy(r, n->tex, NULL, &shadow);
    SDL_SetTextureColorMod(n->tex, c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(n->tex, 255);
    SDL_RenderCopy(r, n->tex, NULL, &dst);
    for (int i = 0; i < CONFETTI; i++) {
        Bit *b = &n->bits[i];
        if (b->life <= 0) continue;
        int sz = n->sc / 2 + 2;
        set_color(r, b->c);
        SDL_Rect q = { (int)b->x, (int)b->y, (int)(sz * (0.5f + 0.5f * fabsf(sinf(b->spin)))) + 1, sz };
        SDL_RenderFillRect(r, &q);
    }
}

static void destroy(void *p) {
    Name *n = p;
    SDL_DestroyTexture(n->tex);
    free(n);
}

const Saver saver_name = { "name", "NAME", create, step, draw, destroy };
