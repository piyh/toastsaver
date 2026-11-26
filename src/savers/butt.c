/* Butt: silly, kid-friendly 360p pixel-art bouncing cartoon animal butts (fluffy corgi,
   cottontail bunny, curly-tail piggy, and roly-poly panda) in a starry pastel sky
   that boing off walls and each other, wiggle, and pop little pastel puffs. */
#include "../engine.h"
#include <math.h>
#include <stdlib.h>

#define ROWS 360
#define BUTT_W 56
#define BUTT_H 52
#define MAX_BUTTS 8
#define MAX_PUFFS 48
#define STARS 45
#define CLOUDS 5

enum { B_CORGI, B_BUNNY, B_PIGGY, B_PANDA };

typedef struct {
    int kind;
    float x, y, vx, vy, ph, squish, poofT;
} Butt;

typedef struct {
    float x, y, vx, vy, life, max;
    uint32_t col;
} Puff;

typedef struct {
    int w, h, px, W, H;
    float scale, t;
    SDL_Texture *small, *bg, *tex[4][2], *cloud[2];
    int cloudW[2], cloudH[2];
    float cloudX[CLOUDS], cloudY[CLOUDS], cloudV[CLOUDS];
    Butt b[MAX_BUTTS];
    Puff p[MAX_PUFFS];
} ButtSaver;

/* Unmistakable, charming cartoon animal butt silhouettes (56x52). */
static Canvas make_butt(int kind, int frame) {
    static const uint32_t COLS[4] = { 0xE69542, 0xFFF4EC, 0xFFA8C2, 0xF8F8FA };
    static const uint32_t SHADE[4] = { 0xB86A22, 0xDEC4B6, 0xD87696, 0xCBD0DC };
    static const uint32_t ACC[4] = { 0xFFF6EA, 0xFF9EC0, 0xFF7AA4, 0x2C2C38 };
    Canvas c = canvas_new(BUTT_W, BUTT_H);
    int w = frame ? 1 : -1;

    if (kind == B_BUNNY) {
        /* Tall upright bunny ears with pink inner ears */
        fill_ellipse(&c, 20.0f, 9.5f, 3.6f, 7.5f, rgb(COLS[kind]));
        fill_ellipse(&c, 35.0f, 9.5f, 3.6f, 7.5f, rgb(COLS[kind]));
        fill_ellipse(&c, 20.0f, 9.5f, 1.8f, 4.8f, rgb(ACC[kind]));
        fill_ellipse(&c, 35.0f, 9.5f, 1.8f, 4.8f, rgb(ACC[kind]));
    } else if (kind == B_PANDA) {
        /* Round black panda ears */
        fill_ellipse(&c, 17.0f, 11.5f, 4.4f, 4.2f, rgb(ACC[kind]));
        fill_ellipse(&c, 38.0f, 11.5f, 4.4f, 4.2f, rgb(ACC[kind]));
    }

    /* Rounded waist tapering smoothly out into two big round cheeks */
    fill_ellipse(&c, 27.5f, 18.0f, 14.0f, 8.0f, rgb(COLS[kind]));
    if (kind == B_PANDA)
        fill_ellipse(&c, 27.5f, 17.0f, 13.5f, 6.0f, rgb(ACC[kind]));
    fill_ellipse(&c, 19.0f, 29.0f, 12.0f, 12.5f, rgb(COLS[kind]));
    fill_ellipse(&c, 36.0f, 29.0f, 12.0f, 12.5f, rgb(COLS[kind]));
    fill_ellipse(&c, 19.0f, 36.0f, 9.5f, 4.5f, rgb(SHADE[kind]));
    fill_ellipse(&c, 36.0f, 36.0f, 9.5f, 4.5f, rgb(SHADE[kind]));
    fill_ellipse(&c, 19.0f, 29.0f, 10.5f, 10.5f, rgb(COLS[kind]));
    fill_ellipse(&c, 36.0f, 29.0f, 10.5f, 10.5f, rgb(COLS[kind]));
    /* Crisp Y-shaped cleft */
    fill_rect(&c, 27, 22, 28, 39, rgb(SHADE[kind]));
    put(&c, 26, 21, rgb(SHADE[kind])); put(&c, 29, 21, rgb(SHADE[kind]));
    fill_ellipse(&c, 15.0f, 25.0f, 2.4f, 3.6f, rgb(0xFFFFFF));
    fill_ellipse(&c, 32.0f, 25.0f, 2.4f, 3.6f, rgb(0xFFFFFF));

    if (kind == B_CORGI) {
        /* Corgi ears peeking over top, fluffy white tail nub, heart cheeks & stubby paws */
        fill_poly(&c, (float[]){ 16, 18, 22 }, (float[]){ 13, 6, 12 }, 3, rgb(COLS[kind]));
        fill_poly(&c, (float[]){ 33, 37, 39 }, (float[]){ 12, 6, 13 }, 3, rgb(COLS[kind]));
        fill_ellipse(&c, 27.5f + w, 15.0f, 4.2f, 3.6f, rgb(ACC[kind]));
        fill_ellipse(&c, 19.0f, 37.0f, 6.0f, 3.5f, rgb(ACC[kind]));
        fill_ellipse(&c, 36.0f, 37.0f, 6.0f, 3.5f, rgb(ACC[kind]));
        fill_rect(&c, 15, 41, 19, 45 + (w > 0 ? 1 : 0), rgb(ACC[kind]));
        fill_rect(&c, 36, 41, 40, 45 + (w < 0 ? 1 : 0), rgb(ACC[kind]));
    } else if (kind == B_BUNNY) {
        /* Big fluffy cotton tail & hind feet with pink toe beans */
        fill_ellipse(&c, 27.5f + w, 24.0f, 5.6f, 5.0f, rgb(0xEAD8D0));
        fill_ellipse(&c, 27.5f + w, 23.0f, 5.2f, 4.6f, rgb(0xFFFFFF));
        int ly = w > 0 ? 1 : 0, ry = w < 0 ? 1 : 0;
        fill_ellipse(&c, 17.5f, 42.5f + ly, 4.2f, 3.0f, rgb(COLS[kind]));
        fill_ellipse(&c, 37.5f, 42.5f + ry, 4.2f, 3.0f, rgb(COLS[kind]));
        fill_rect(&c, 16, 42 + ly, 18, 43 + ly, rgb(ACC[kind]));
        fill_rect(&c, 37, 42 + ry, 39, 43 + ry, rgb(ACC[kind]));
    } else if (kind == B_PIGGY) {
        /* Pointed pig ears, curly corkscrew tail, and little dark hooves */
        fill_poly(&c, (float[]){ 15, 17, 22 }, (float[]){ 13, 6, 12 }, 3, rgb(COLS[kind]));
        fill_poly(&c, (float[]){ 33, 38, 40 }, (float[]){ 12, 6, 13 }, 3, rgb(COLS[kind]));
        fill_poly(&c, (float[]){ 16, 17, 20 }, (float[]){ 12, 8, 12 }, 3, rgb(ACC[kind]));
        fill_poly(&c, (float[]){ 35, 38, 39 }, (float[]){ 12, 8, 12 }, 3, rgb(ACC[kind]));
        /* Curly tail loop */
        fill_ellipse(&c, 27.5f + w, 19.0f, 3.5f, 3.5f, rgb(ACC[kind]));
        fill_ellipse(&c, 27.5f + w, 19.0f, 1.5f, 1.5f, rgb(COLS[kind]));
        fill_rect(&c, 27, 22, 28, 24, rgb(ACC[kind]));
        fill_rect(&c, 15, 41, 19, 45 + (w > 0 ? 1 : 0), rgb(0x52343A));
        fill_rect(&c, 36, 41, 40, 45 + (w < 0 ? 1 : 0), rgb(0x52343A));
    } else if (kind == B_PANDA) {
        /* Fluffy black tail puff & chunky black hind paws with pink pads */
        fill_ellipse(&c, 27.5f + w, 21.0f, 4.2f, 3.8f, rgb(ACC[kind]));
        fill_ellipse(&c, 26.5f + w, 20.0f, 1.8f, 1.5f, rgb(0x4A4A5A));
        int ly = w > 0 ? 1 : 0, ry = w < 0 ? 1 : 0;
        fill_ellipse(&c, 17.5f, 41.5f + ly, 4.2f, 3.6f, rgb(ACC[kind]));
        fill_ellipse(&c, 37.5f, 41.5f + ry, 4.2f, 3.6f, rgb(ACC[kind]));
        fill_rect(&c, 16, 41 + ly, 18, 43 + ly, rgb(0xFF9EC0));
        fill_rect(&c, 37, 41 + ry, 39, 43 + ry, rgb(0xFF9EC0));
    }

    outline(&c, rgb(0x3A1E14));
    return c;
}

static Canvas make_soft_cloud(int w) {
    int h = w * 2 / 5;
    Canvas c = canvas_new(w, h);
    fill_ellipse(&c, w * 0.5f, h * 0.65f, w * 0.45f, h * 0.28f, rgb(0x5E3E88));
    fill_ellipse(&c, w * 0.35f, h * 0.48f, w * 0.26f, h * 0.30f, rgb(0x6D4A98));
    fill_ellipse(&c, w * 0.64f, h * 0.50f, w * 0.24f, h * 0.26f, rgb(0x6D4A98));
    return c;
}

static void spawn_puff(ButtSaver *s, float x, float y, float dirX) {
    static const uint32_t PC[5] = { 0xFFF0A0, 0xFFB3D9, 0xA8F0D8, 0xD8B8FF, 0xFFFFFF };
    for (int k = 0; k < 5; k++) {
        for (int i = 0; i < MAX_PUFFS; i++) {
            if (s->p[i].life > 0) continue;
            float a = frand(-0.8f, 0.8f) + (dirX > 0 ? PI_F : 0);
            float sp = frand(0.06f, 0.14f) * s->H;
            s->p[i] = (Puff){ x, y, cosf(a) * sp, sinf(a) * sp - 0.04f * s->H, 0, frand(0.6f, 1.1f), PC[irand(5)] };
            s->p[i].life = s->p[i].max;
            break;
        }
    }
}

static void *create(SDL_Renderer *r, int w, int h) {
    ButtSaver *s = calloc(1, sizeof *s);
    s->w = w;
    s->h = h;
    if (h >= ROWS) {
        s->px = h / ROWS;
        s->scale = 1;
        s->W = (w + s->px - 1) / s->px;
        s->H = (h + s->px - 1) / s->px;
    } else {
        s->px = 1;
        s->scale = (float)h / ROWS;
        s->H = ROWS;
        s->W = (int)ceilf(w / s->scale);
    }
    int W = s->W, H = s->H;

    /* Starry pastel twilight pixel backdrop */
    Canvas bg = canvas_new(W, H);
    for (int y = 0; y < H; y++) {
        float t = (float)y / H;
        uint32_t col = ((uint32_t)(58 + 50 * t) << 16) | ((uint32_t)(36 + 22 * t) << 8) | (uint32_t)(102 + 34 * t);
        fill_rect(&bg, 0, y, W - 1, y, rgb(col));
    }
    for (int i = 0; i < STARS; i++) {
        int sx = irand(W), sy = irand(H);
        put(&bg, sx, sy, rgb(0xFFE8FF));
        if (i % 4 == 0) {
            put(&bg, sx - 1, sy, rgb(0xC8A8E8)); put(&bg, sx + 1, sy, rgb(0xC8A8E8));
            put(&bg, sx, sy - 1, rgb(0xC8A8E8)); put(&bg, sx, sy + 1, rgb(0xC8A8E8));
        }
    }
    s->bg = canvas_texture(r, &bg);

    for (int i = 0; i < 2; i++) {
        Canvas c = make_soft_cloud(54 + i * 24);
        s->cloudW[i] = c.w; s->cloudH[i] = c.h;
        s->cloud[i] = canvas_texture(r, &c);
    }
    for (int i = 0; i < CLOUDS; i++) {
        s->cloudX[i] = frand(0, (float)W);
        s->cloudY[i] = frand(0.08f, 0.85f) * H;
        s->cloudV[i] = frand(3.5f, 8.0f);
    }

    for (int k = 0; k < 4; k++)
        for (int fr = 0; fr < 2; fr++) {
            Canvas c = make_butt(k, fr);
            s->tex[k][fr] = canvas_texture(r, &c);
        }
    for (int i = 0; i < MAX_BUTTS; i++) {
        Butt *b = &s->b[i];
        b->kind = i % 4;
        for (int tries = 0; tries < 40; tries++) {
            float cx = frand(BUTT_W, W - BUTT_W), cy = frand(BUTT_H, H - BUTT_H);
            int ok = 1;
            for (int j = 0; j < i; j++) {
                float dx = cx - s->b[j].x, dy = cy - s->b[j].y;
                if (dx * dx + dy * dy < (BUTT_W * 1.08f) * (BUTT_W * 1.08f)) { ok = 0; break; }
            }
            b->x = cx;
            b->y = cy;
            if (ok) break;
        }
        b->vx = (irand(2) ? 1 : -1) * frand(0.045f, 0.080f) * W;
        b->vy = (irand(2) ? 1 : -1) * frand(0.045f, 0.080f) * H;
        b->ph = frand(0, 6.28f);
        b->poofT = frand(2.0f, 6.0f);
    }
    return s;
}

static void step(void *p, float dt) {
    ButtSaver *s = p;
    s->t += dt;
    for (int i = 0; i < CLOUDS; i++) {
        s->cloudX[i] += s->cloudV[i] * dt;
        if (s->cloudX[i] > s->W + 20) s->cloudX[i] = -80.0f;
    }
    float hw = BUTT_W * 0.5f, hh = BUTT_H * 0.5f;
    for (int i = 0; i < MAX_BUTTS; i++) {
        Butt *b = &s->b[i];
        b->x += b->vx * dt;
        b->y += b->vy * dt;
        b->ph += dt * 3.8f;
        if (b->squish > 0) b->squish = fmaxf(0, b->squish - dt * 2.5f);
        b->poofT -= dt;
        if (b->poofT <= 0) {
            b->poofT = frand(3.0f, 7.0f);
            b->squish = 0.8f;
            spawn_puff(s, b->x, b->y + hh * 0.4f, b->vx);
        }
        if (b->x < hw) { b->x = hw; b->vx = fabsf(b->vx); b->squish = 1.0f; }
        if (b->x > s->W - hw) { b->x = s->W - hw; b->vx = -fabsf(b->vx); b->squish = 1.0f; }
        if (b->y < hh) { b->y = hh; b->vy = fabsf(b->vy); b->squish = 1.0f; }
        if (b->y > s->H - hh) { b->y = s->H - hh; b->vy = -fabsf(b->vy); b->squish = 1.0f; }
    }
    /* Elastic butt-to-butt collisions with multi-pass separation so sprites don't overlap */
    float minD = BUTT_W * 1.04f;
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < MAX_BUTTS; i++)
            for (int j = i + 1; j < MAX_BUTTS; j++) {
                Butt *a = &s->b[i], *b = &s->b[j];
                float dx = b->x - a->x, dy = b->y - a->y, d2 = dx * dx + dy * dy;
                if (d2 < minD * minD && d2 > 0.01f) {
                    float d = sqrtf(d2), nx = dx / d, ny = dy / d, sep = (minD - d) * 0.5f;
                    a->x -= nx * sep; a->y -= ny * sep;
                    b->x += nx * sep; b->y += ny * sep;
                    if (pass == 0) {
                        float rvn = (b->vx - a->vx) * nx + (b->vy - a->vy) * ny;
                        if (rvn < 0) {
                            a->vx += nx * rvn; a->vy += ny * rvn;
                            b->vx -= nx * rvn; b->vy -= ny * rvn;
                            a->squish = b->squish = 0.9f;
                        }
                    }
                }
            }
        for (int i = 0; i < MAX_BUTTS; i++) {
            Butt *b = &s->b[i];
            if (b->x < hw) { b->x = hw; b->vx = fabsf(b->vx); }
            if (b->x > s->W - hw) { b->x = s->W - hw; b->vx = -fabsf(b->vx); }
            if (b->y < hh) { b->y = hh; b->vy = fabsf(b->vy); }
            if (b->y > s->H - hh) { b->y = s->H - hh; b->vy = -fabsf(b->vy); }
        }
    }
    for (int i = 0; i < MAX_PUFFS; i++) {
        Puff *pf = &s->p[i];
        if (pf->life <= 0) continue;
        pf->life -= dt;
        pf->x += pf->vx * dt;
        pf->y += pf->vy * dt;
    }
}

static void draw_scene(const ButtSaver *s, SDL_Renderer *r) {
    int u = s->px;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_RenderCopy(r, s->bg, NULL, &(SDL_Rect){ 0, 0, s->W * u, s->H * u });

    for (int i = 0; i < CLOUDS; i++) {
        int k = i % 2;
        SDL_RenderCopy(r, s->cloud[k], NULL,
                       &(SDL_Rect){ (int)s->cloudX[i] * u, (int)s->cloudY[i] * u, s->cloudW[k] * u, s->cloudH[k] * u });
    }
    for (int i = 0; i < MAX_PUFFS; i++) {
        const Puff *pf = &s->p[i];
        if (pf->life <= 0) continue;
        SDL_Color c = hexcol(pf->col);
        c.a = (Uint8)(255 * (pf->life / pf->max));
        set_color(r, c);
        SDL_RenderFillRect(r, &(SDL_Rect){ (int)pf->x * u, (int)pf->y * u, 4 * u, 4 * u });
    }
    int ord[MAX_BUTTS];
    for (int i = 0; i < MAX_BUTTS; i++) ord[i] = i;
    for (int i = 0; i < MAX_BUTTS; i++)
        for (int j = i + 1; j < MAX_BUTTS; j++) {
            float yi = s->b[ord[i]].y, yj = s->b[ord[j]].y;
            if (yj < yi || (yj == yi && ord[j] < ord[i])) { int t = ord[i]; ord[i] = ord[j]; ord[j] = t; }
        }
    for (int i = 0; i < MAX_BUTTS; i++) {
        const Butt *b = &s->b[ord[i]];
        float sq = 1.0f + b->squish * 0.14f;
        int dw = (int)(BUTT_W * sq), dh = (int)(BUTT_H / sq);
        int wig = (int)(sinf(b->ph) * 2);
        SDL_Rect d = { ((int)b->x - dw / 2 + wig) * u, ((int)b->y - dh / 2) * u, dw * u, dh * u };
        SDL_RenderCopy(r, s->tex[b->kind][(int)b->ph % 2], NULL, &d);
    }
}

static void draw(void *p, SDL_Renderer *r) {
    ButtSaver *s = p;
    if (s->scale == 1) { draw_scene(s, r); return; }
    if (!s->small) {
        s->small = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, s->W, s->H);
        if (!s->small) return;
        SDL_SetTextureScaleMode(s->small, SDL_ScaleModeLinear);
    }
    SDL_Texture *old = SDL_GetRenderTarget(r);
    SDL_Rect vp;
    SDL_RenderGetViewport(r, &vp);
    SDL_SetRenderTarget(r, s->small);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);
    draw_scene(s, r);
    SDL_SetRenderTarget(r, old);
    SDL_RenderSetViewport(r, &vp);
    SDL_RenderCopy(r, s->small, NULL, &(SDL_Rect){ 0, 0, s->w, s->h });
}

static void destroy(void *p) {
    ButtSaver *s = p;
    SDL_DestroyTexture(s->bg);
    if (s->small) SDL_DestroyTexture(s->small);
    for (int i = 0; i < 2; i++) SDL_DestroyTexture(s->cloud[i]);
    for (int k = 0; k < 4; k++)
        for (int fr = 0; fr < 2; fr++) SDL_DestroyTexture(s->tex[k][fr]);
    free(s);
}

const Saver saver_butt = { "butt", "BUTT", create, step, draw, destroy, NULL, NULL, NULL, NULL, NULL, NULL };
