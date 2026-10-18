/* 3D pipes: shiny pipes growing through a 3D grid, drawn with a small software
   z-buffer so they pass correctly in front of and behind each other. Each step
   only rasterizes the new segment, so it stays cheap. */
#include "../engine.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SIDES 14
#define PIPE_R 0.22f
#define JOINT_R 0.3f
#define MAX_PIPES 14

typedef struct { float x, y, z; } V3;

static const int DIRS[6][3] = { {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1} };
static const uint32_t COLORS[] = { 0xE53935, 0x43A047, 0x1E88E5, 0xFDD835, 0x8E24AA, 0xFB8C00, 0x00ACC1, 0xD81B60 };

typedef struct {
    int w, h, rw, rh;
    uint32_t *col;
    float *depth;
    SDL_Texture *tex;
    int gx, gy, gz;
    unsigned char *occ;
    int cx, cy, cz, dir, alive, pipes, segs, maxSegs;
    V3 color;
    float acc, fade, f, zc, cY, sY, cX, sX;
    int dx0, dy0, dx1, dy1;   /* dirty rectangle since last upload */
} Pipes;

static V3 v3(float x, float y, float z) { V3 v = { x, y, z }; return v; }
static V3 add(V3 a, V3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static V3 mul(V3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static V3 cross(V3 a, V3 b) { return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static V3 norm(V3 a) { float l = sqrtf(dot(a, a)); return l > 0 ? mul(a, 1 / l) : a; }

/* World -> view: turn the grid a little so it reads as 3D, then push it away from the camera. */
static V3 view_dir(const Pipes *p, V3 v) {
    V3 a = v3(v.x * p->cY + v.z * p->sY, v.y, -v.x * p->sY + v.z * p->cY);
    return v3(a.x, a.y * p->cX - a.z * p->sX, a.y * p->sX + a.z * p->cX);
}
static V3 view_pos(const Pipes *p, V3 v) { V3 a = view_dir(p, v); a.z += p->zc; return a; }

static V3 cell_pos(const Pipes *p, int x, int y, int z) {
    return v3(x - (p->gx - 1) * 0.5f, y - (p->gy - 1) * 0.5f, z - (p->gz - 1) * 0.5f);
}

static uint32_t pack(V3 c) {
    int r = (int)(c.x * 255), g = (int)(c.y * 255), b = (int)(c.z * 255);
    r = r > 255 ? 255 : r; g = g > 255 ? 255 : g; b = b > 255 ? 255 : b;
    return ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8) | 0xFF;
}

/* Simple lighting: ambient + diffuse + a white specular highlight. */
static V3 shade(V3 n, V3 base) {
    const V3 L = { -0.45f, 0.6f, -0.66f }, H = { -0.22f, 0.3f, -0.93f };
    float d = dot(n, L), s = dot(n, H);
    d = d > 0 ? d : 0;
    s = s > 0 ? powf(s, 28) * 0.75f : 0;
    return add(mul(base, 0.22f + 0.85f * d), v3(s, s, s));
}

typedef struct { float x, y, z; V3 c; } Vert;

static int project(const Pipes *p, V3 v, V3 color, Vert *out) {
    if (v.z < 0.1f) return 0;
    out->x = p->rw * 0.5f + p->f * v.x / v.z;
    out->y = p->rh * 0.5f - p->f * v.y / v.z;
    out->z = v.z;
    out->c = color;
    return 1;
}

static void tri(Pipes *p, const Vert *a, const Vert *b, const Vert *c) {
    float area = (b->x - a->x) * (c->y - a->y) - (b->y - a->y) * (c->x - a->x);
    if (fabsf(area) < 1e-6f) return;
    int x0 = (int)floorf(fminf(a->x, fminf(b->x, c->x))), x1 = (int)ceilf(fmaxf(a->x, fmaxf(b->x, c->x)));
    int y0 = (int)floorf(fminf(a->y, fminf(b->y, c->y))), y1 = (int)ceilf(fmaxf(a->y, fmaxf(b->y, c->y)));
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > p->rw - 1) x1 = p->rw - 1;
    if (y1 > p->rh - 1) y1 = p->rh - 1;
    if (x0 > x1 || y0 > y1) return;
    if (x0 < p->dx0) p->dx0 = x0;
    if (y0 < p->dy0) p->dy0 = y0;
    if (x1 > p->dx1) p->dx1 = x1;
    if (y1 > p->dy1) p->dy1 = y1;
    float inv = 1.0f / area;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            float px = x + 0.5f, py = y + 0.5f;
            float w0 = ((b->x - px) * (c->y - py) - (b->y - py) * (c->x - px)) * inv;
            float w1 = ((c->x - px) * (a->y - py) - (c->y - py) * (a->x - px)) * inv;
            float w2 = 1 - w0 - w1;
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            float z = w0 * a->z + w1 * b->z + w2 * c->z;
            int i = y * p->rw + x;
            if (z >= p->depth[i]) continue;
            p->depth[i] = z;
            p->col[i] = pack(add(add(mul(a->c, w0), mul(b->c, w1)), mul(c->c, w2)));
        }
}

static void quad(Pipes *p, Vert *v) { tri(p, &v[0], &v[1], &v[2]); tri(p, &v[0], &v[2], &v[3]); }

static void cylinder(Pipes *p, V3 a, V3 b) {
    V3 d = norm(v3(b.x - a.x, b.y - a.y, b.z - a.z));
    V3 u = norm(cross(d, fabsf(d.y) < 0.9f ? v3(0, 1, 0) : v3(1, 0, 0))), w = cross(d, u);
    Vert ring[SIDES + 1][2];
    for (int i = 0; i <= SIDES; i++) {
        float t = i * 2 * PI_F / SIDES;
        V3 n = add(mul(u, cosf(t)), mul(w, sinf(t)));
        V3 c = shade(view_dir(p, n), p->color);
        if (!project(p, view_pos(p, add(a, mul(n, PIPE_R))), c, &ring[i][0])) return;
        if (!project(p, view_pos(p, add(b, mul(n, PIPE_R))), c, &ring[i][1])) return;
    }
    for (int i = 0; i < SIDES; i++) {
        Vert q[4] = { ring[i][0], ring[i][1], ring[i + 1][1], ring[i + 1][0] };
        quad(p, q);
    }
}

static void sphere(Pipes *p, V3 c, float rad) {
    enum { LAT = 8, LON = 14 };
    Vert v[LAT + 1][LON + 1];
    for (int i = 0; i <= LAT; i++)
        for (int j = 0; j <= LON; j++) {
            float th = PI_F * i / LAT, ph = 2 * PI_F * j / LON;
            V3 n = v3(sinf(th) * cosf(ph), cosf(th), sinf(th) * sinf(ph));
            if (!project(p, view_pos(p, add(c, mul(n, rad))), shade(view_dir(p, n), p->color), &v[i][j])) return;
        }
    for (int i = 0; i < LAT; i++)
        for (int j = 0; j < LON; j++) {
            Vert q[4] = { v[i][j], v[i + 1][j], v[i + 1][j + 1], v[i][j + 1] };
            quad(p, q);
        }
}

static int occ_index(const Pipes *p, int x, int y, int z) { return (z * p->gy + y) * p->gx + x; }

static int free_cell(const Pipes *p, int x, int y, int z) {
    return x >= 0 && y >= 0 && z >= 0 && x < p->gx && y < p->gy && z < p->gz && !p->occ[occ_index(p, x, y, z)];
}

static void clear_scene(Pipes *p) {
    for (int i = 0; i < p->rw * p->rh; i++) { p->col[i] = 0x000000FF; p->depth[i] = 1e9f; }
    memset(p->occ, 0, (size_t)p->gx * p->gy * p->gz);
    p->alive = p->pipes = p->segs = 0;
    p->dx0 = 0; p->dy0 = 0; p->dx1 = p->rw - 1; p->dy1 = p->rh - 1;
}

static void start_pipe(Pipes *p) {
    for (int tries = 0; tries < 60; tries++) {
        int x = irand(p->gx), y = irand(p->gy), z = irand(p->gz);
        if (!free_cell(p, x, y, z)) continue;
        p->cx = x; p->cy = y; p->cz = z;
        p->dir = irand(6);
        uint32_t c = COLORS[irand(sizeof COLORS / sizeof COLORS[0])];
        p->color = v3((c >> 16 & 255) / 255.0f, (c >> 8 & 255) / 255.0f, (c & 255) / 255.0f);
        p->occ[occ_index(p, x, y, z)] = 1;
        sphere(p, cell_pos(p, x, y, z), JOINT_R);
        p->alive = 1;
        p->pipes++;
        return;
    }
    p->fade = 0.001f; /* grid is full */
}

static void grow(Pipes *p) {
    if (!p->alive) { start_pipe(p); return; }
    int d = p->dir;
    int nx = p->cx + DIRS[d][0], ny = p->cy + DIRS[d][1], nz = p->cz + DIRS[d][2];
    if (irand(1000) < 15) {             /* now and then a pipe just stops and a new colour starts */
        sphere(p, cell_pos(p, p->cx, p->cy, p->cz), JOINT_R);
        p->alive = 0;
        if (p->pipes >= MAX_PIPES) p->fade = 0.001f;
        return;
    }
    if (irand(100) < 22 || !free_cell(p, nx, ny, nz)) {
        int options[6], n = 0;
        for (int k = 0; k < 6; k++) {
            if ((k ^ 1) == d) continue; /* no reversing */
            if (free_cell(p, p->cx + DIRS[k][0], p->cy + DIRS[k][1], p->cz + DIRS[k][2])) options[n++] = k;
        }
        if (n == 0) {                    /* boxed in: end this pipe */
            p->alive = 0;
            if (p->pipes >= MAX_PIPES) p->fade = 0.001f;
            return;
        }
        int nd = options[irand(n)];
        if (nd != d) sphere(p, cell_pos(p, p->cx, p->cy, p->cz), JOINT_R);
        p->dir = d = nd;
        nx = p->cx + DIRS[d][0]; ny = p->cy + DIRS[d][1]; nz = p->cz + DIRS[d][2];
    }
    cylinder(p, cell_pos(p, p->cx, p->cy, p->cz), cell_pos(p, nx, ny, nz));
    p->occ[occ_index(p, nx, ny, nz)] = 1;
    p->cx = nx; p->cy = ny; p->cz = nz;
    if (++p->segs > p->maxSegs) p->fade = 0.001f;
}

static void *create(SDL_Renderer *r, int w, int h) {
    Pipes *p = calloc(1, sizeof *p);
    p->w = w;
    p->h = h;
    p->rw = w > 960 ? w / 2 : w;          /* render at half size on big screens */
    p->rh = w > 960 ? h / 2 : h;
    p->gx = 14;
    p->gy = (int)(14.0f * h / w + 0.5f);
    if (p->gy < 6) p->gy = 6;
    p->gz = 10;
    p->maxSegs = p->gx * p->gy * p->gz * 35 / 100;
    p->col = malloc(sizeof(uint32_t) * p->rw * p->rh);
    p->depth = malloc(sizeof(float) * p->rw * p->rh);
    p->occ = malloc((size_t)p->gx * p->gy * p->gz);
    float ay = 0.42f, ax = 0.28f;
    p->cY = cosf(ay); p->sY = sinf(ay); p->cX = cosf(ax); p->sX = sinf(ax);
    p->zc = p->gx * 1.25f;
    p->f = p->rw * 0.5f * p->zc / (p->gx * 0.78f);
    p->tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, p->rw, p->rh);
    SDL_SetTextureScaleMode(p->tex, SDL_ScaleModeLinear);
    clear_scene(p);
    return p;
}

static void step(void *v, float dt) {
    Pipes *p = v;
    if (p->fade > 0) {                    /* fade to black, then start over */
        p->fade += dt;
        if (p->fade > 1.2f) { p->fade = 0; clear_scene(p); }
        return;
    }
    p->acc += dt;
    int n = 0;
    while (p->acc > 0.045f && n++ < 4 && p->fade == 0) { p->acc -= 0.045f; grow(p); }
    if (p->acc > 0.2f) p->acc = 0;
}

static void draw(void *v, SDL_Renderer *r) {
    Pipes *p = v;
    if (p->dx0 <= p->dx1 && p->dy0 <= p->dy1) {
        SDL_Rect d = { p->dx0, p->dy0, p->dx1 - p->dx0 + 1, p->dy1 - p->dy0 + 1 };
        SDL_UpdateTexture(p->tex, &d, p->col + p->dy0 * p->rw + p->dx0, p->rw * 4);
        p->dx0 = p->rw; p->dy0 = p->rh; p->dx1 = -1; p->dy1 = -1;
    }
    SDL_RenderCopy(r, p->tex, NULL, NULL);
    if (p->fade > 0) {
        float a = p->fade > 1 ? 1 : p->fade;
        SDL_SetRenderDrawColor(r, 0, 0, 0, (Uint8)(a * 255));
        SDL_RenderFillRect(r, NULL);
    }
}

static void destroy(void *v) {
    Pipes *p = v;
    SDL_DestroyTexture(p->tex);
    free(p->col);
    free(p->depth);
    free(p->occ);
    free(p);
}

const Saver saver_pipes = { "pipes", "PIPES", create, step, draw, destroy };
