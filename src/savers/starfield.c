/* Starfield: flying forward through space at warp speed, with rocket ships cruising on 3D vectors. */
#include "../engine.h"
#include <math.h>
#include <stdlib.h>

typedef struct {
    float x, y, z, px, py;
    int hasPrev;
} Star;

#define MAX_ROCKETS 1
#define Z_STAR_FAR 2.4f
#define Z_ROCKET_FAR 3.2f
#define WARP_SPEED 0.65f

typedef struct {
    float x, y, z, vx, vy, vz, ph, wait;
    uint32_t accent;
} Rocket;

typedef struct {
    int w, h, n;
    float line, t;
    Star *s;
    Rocket r[MAX_ROCKETS];
} Starfield;

typedef struct { float x, y, z; } Vec3;

static Vec3 v_add(Vec3 a, Vec3 b) { return (Vec3){ a.x + b.x, a.y + b.y, a.z + b.z }; }
static Vec3 v_scale(Vec3 a, float s) { return (Vec3){ a.x * s, a.y * s, a.z * s }; }
static float v_dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static Vec3 v_cross(Vec3 a, Vec3 b) {
    return (Vec3){ a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}
static Vec3 v_norm(Vec3 a) {
    float l = sqrtf(v_dot(a, a));
    return l > 1e-6f ? v_scale(a, 1.0f / l) : (Vec3){ 0, 0, -1 };
}

static void reset_star(Star *s, int far) {
    s->x = frand(-1.1f, 1.1f);
    s->y = frand(-1.1f, 1.1f);
    s->z = far ? Z_STAR_FAR : frand(0.15f, Z_STAR_FAR);
    s->hasPrev = 0;
}

static void reset_rocket(Rocket *rk, int initial) {
    static const uint32_t ACCENTS[4] = { 0xFF5252, 0xFFB020, 0x4FC3F7, 0x69F0AE };
    float z0 = Z_ROCKET_FAR + frand(0.0f, 0.3f);
    float zEnd = 0.5f;
    float aEnd = frand(0, 2 * PI_F), rEnd = frand(0.24f, 0.72f);
    float tx = cosf(aEnd) * rEnd, ty = sinf(aEnd) * rEnd;
    float ang0 = aEnd + frand(-1.5f, 1.5f), r0 = frand(0.25f, 0.95f);
    float x0 = cosf(ang0) * r0, y0 = sinf(ang0) * r0;

    /* 70% oncoming/crossing rockets moving toward us; 30% slower cruising ships we overtake */
    float travelT = irand(10) < 7 ? frand(2.6f, 4.0f) : frand(5.2f, 6.8f);
    float dzDt = -(z0 - zEnd) / travelT;
    rk->vx = (tx - x0) / travelT;
    rk->vy = (ty - y0) / travelT;
    rk->vz = dzDt + WARP_SPEED;

    if (initial) {
        float tElap = travelT * 0.25f;
        rk->x = x0 + rk->vx * tElap;
        rk->y = y0 + rk->vy * tElap;
        rk->z = z0 + dzDt * tElap;
        rk->wait = 0.0f;
    } else {
        rk->x = x0;
        rk->y = y0;
        rk->z = z0;
        rk->wait = frand(5.0f, 11.0f);
    }
    rk->ph = frand(0, 6.28f);
    rk->accent = ACCENTS[irand(4)];
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
    for (int i = 0; i < MAX_ROCKETS; i++) reset_rocket(&f->r[i], 1);
    return f;
}

static void step(void *p, float dt) {
    Starfield *f = p;
    f->t += dt;
    for (int i = 0; i < f->n; i++) {
        f->s[i].z -= WARP_SPEED * dt;
        if (f->s[i].z <= 0.03f) reset_star(&f->s[i], 1);
    }
    if (g_force_egg) {
        g_force_egg = 0;
        if (f->r[0].wait > 0) f->r[0].wait = 0;
        else reset_rocket(&f->r[0], 1);
    }
    for (int i = 0; i < MAX_ROCKETS; i++) {
        Rocket *rk = &f->r[i];
        if (rk->wait > 0) { rk->wait -= dt; continue; }
        rk->x += rk->vx * dt;
        rk->y += rk->vy * dt;
        rk->z += (rk->vz - WARP_SPEED) * dt;
        rk->ph += dt * 16.0f;
        float sx = f->w * 0.5f + rk->x * (f->w * 0.5f) / rk->z;
        float sy = f->h * 0.5f + rk->y * (f->w * 0.5f) / rk->z;
        if (rk->z <= 0.08f || sx < -f->w * 0.25f || sx > f->w * 1.25f || sy < -f->h * 0.25f || sy > f->h * 1.25f)
            reset_rocket(rk, 0);
    }
}

static SDL_Color shade_col(uint32_t hex, float k) {
    SDL_Color c = hexcol(hex);
    float r = c.r * k, g = c.g * k, b = c.b * k;
    c.r = (Uint8)(r > 255 ? 255 : r < 0 ? 0 : r);
    c.g = (Uint8)(g > 255 ? 255 : g < 0 ? 0 : g);
    c.b = (Uint8)(b > 255 ? 255 : b < 0 ? 0 : b);
    return c;
}

typedef struct {
    float x0, y0, x1, y1, x2, y2, depth;
    SDL_Color col;
} Tri;

static void push_tri(Tri *tris, int *nt, int cap, Vec3 p0, Vec3 p1, Vec3 p2,
                     float cx, float cy, float fov, SDL_Color col) {
    if (*nt >= cap || p0.z < 0.03f || p1.z < 0.03f || p2.z < 0.03f) return;
    tris[(*nt)++] = (Tri){
        cx + p0.x * fov / p0.z, cy + p0.y * fov / p0.z,
        cx + p1.x * fov / p1.z, cy + p1.y * fov / p1.z,
        cx + p2.x * fov / p2.z, cy + p2.y * fov / p2.z,
        (p0.z + p1.z + p2.z) / 3.0f, col
    };
}

static void draw_rocket(SDL_Renderer *ren, const Starfield *f, const Rocket *rk) {
    if (rk->z < 0.09f) return;
    float fade = (Z_ROCKET_FAR - rk->z) / 0.9f;
    if (fade <= 0.02f) return;
    if (fade > 1.0f) fade = 1.0f;
    fade = fade * fade * (3.0f - 2.0f * fade);

    float cx = f->w * 0.5f, cy = f->h * 0.5f, fov = f->w * 0.5f;
    Vec3 center = { rk->x, rk->y, rk->z };
    Vec3 dir = v_norm((Vec3){ rk->vx, rk->vy, rk->vz });
    Vec3 ref = fabsf(dir.y) < 0.9f ? (Vec3){ 0, 1, 0 } : (Vec3){ 1, 0, 0 };
    Vec3 right = v_norm(v_cross(dir, ref));
    Vec3 up = v_norm(v_cross(right, dir));

    enum { SEG = 12, CAP = 160 };
    float rad = 0.0055f, radNoz = 0.0036f;
    float flick = 1.0f + 0.25f * sinf(rk->ph);
    Vec3 tip = v_add(center, v_scale(dir, 0.045f));
    Vec3 flTip = v_add(center, v_scale(dir, -0.025f - 0.019f * flick));
    Vec3 nP[SEG], tP[SEG], bP[SEG], zP[SEG];
    for (int i = 0; i < SEG; i++) {
        float a = i * 2.0f * PI_F / SEG;
        Vec3 radial = v_add(v_scale(right, cosf(a)), v_scale(up, sinf(a)));
        Vec3 rim = v_scale(radial, rad);
        nP[i] = v_add(v_add(center, v_scale(dir, 0.024f)), rim);
        bP[i] = v_add(v_add(center, v_scale(dir, -0.011f)), rim);
        tP[i] = v_add(v_add(center, v_scale(dir, -0.019f)), rim);
        zP[i] = v_add(v_add(center, v_scale(dir, -0.025f)), v_scale(radial, radNoz));
    }

    Tri tris[CAP];
    int nt = 0;
    Vec3 light = v_norm((Vec3){ -0.5f, -0.6f, -0.62f });
    Vec3 camDir = v_norm(v_scale(center, -1.0f));

    for (int i = 0; i < SEG; i++) {
        int j = (i + 1) % SEG;
        float am = (i + 0.5f) * 2.0f * PI_F / SEG;
        Vec3 normRad = v_add(v_scale(right, cosf(am)), v_scale(up, sinf(am)));
        float lit = 0.72f + 0.34f * v_dot(normRad, light);

        /* Thruster flame cone (front facets visible from any angle) */
        Vec3 flMid = v_scale(v_add(v_add(zP[i], zP[j]), flTip), 1.0f / 3.0f);
        Vec3 flNorm = v_norm(v_add(v_scale(normRad, 0.019f * flick), v_scale(dir, -radNoz)));
        if (v_dot(flNorm, v_scale(flMid, -1.0f)) > -0.15f)
            push_tri(tris, &nt, CAP, zP[i], zP[j], flTip, cx, cy, fov, shade_col(0xFF7A18, fade));

        /* Rear nozzle bulkhead (visible when viewing ship from behind) */
        Vec3 tailCtr = v_add(center, v_scale(dir, -0.019f));
        if (v_dot(v_scale(dir, -1.0f), camDir) > 0.0f) {
            push_tri(tris, &nt, CAP, tP[i], tP[j], tailCtr, cx, cy, fov, shade_col(0x363E4E, fade));
            Vec3 nozCtr = v_add(center, v_scale(dir, -0.025f));
            push_tri(tris, &nt, CAP, zP[i], zP[j], nozCtr, cx, cy, fov, shade_col(0xFFE066, fade));
        }

        /* 12-sided fuselage + accent tail band */
        Vec3 cylMid = v_scale(v_add(bP[i], nP[j]), 0.5f);
        if (v_dot(normRad, v_scale(cylMid, -1.0f)) > 0.0f) {
            SDL_Color bodyC = shade_col(0xE8ECF4, lit * fade);
            SDL_Color bandC = shade_col(rk->accent, lit * fade);
            push_tri(tris, &nt, CAP, bP[i], bP[j], nP[j], cx, cy, fov, bodyC);
            push_tri(tris, &nt, CAP, bP[i], nP[j], nP[i], cx, cy, fov, bodyC);
            push_tri(tris, &nt, CAP, tP[i], tP[j], bP[j], cx, cy, fov, bandC);
            push_tri(tris, &nt, CAP, tP[i], bP[j], bP[i], cx, cy, fov, bandC);
        }

        /* Nose cone */
        Vec3 noseNorm = v_norm(v_add(v_scale(normRad, 0.021f), v_scale(dir, rad)));
        Vec3 noseMid = v_scale(v_add(v_add(nP[i], nP[j]), tip), 1.0f / 3.0f);
        if (v_dot(noseNorm, v_scale(noseMid, -1.0f)) > 0.0f) {
            float nLit = 0.78f + 0.32f * v_dot(noseNorm, light);
            push_tri(tris, &nt, CAP, nP[i], nP[j], tip, cx, cy, fov, shade_col(rk->accent, nLit * fade));
        }
    }

    /* 4 radial fins */
    for (int k = 0; k < 4; k++) {
        float fa = 0.7854f + k * (PI_F * 0.5f);
        Vec3 finDir = v_add(v_scale(right, cosf(fa)), v_scale(up, sinf(fa)));
        Vec3 f0 = v_add(v_add(center, v_scale(dir, -0.019f)), v_scale(finDir, rad * 0.9f));
        Vec3 f1 = v_add(v_add(center, v_scale(dir, -0.002f)), v_scale(finDir, rad * 0.9f));
        Vec3 f2 = v_add(v_add(center, v_scale(dir, -0.024f)), v_scale(finDir, rad * 2.25f));
        Vec3 fMid = v_scale(v_add(v_add(f0, f1), f2), 1.0f / 3.0f);
        Vec3 fNorm = v_norm(v_cross(v_add(f1, v_scale(f0, -1.0f)), v_add(f2, v_scale(f0, -1.0f))));
        float side = fabsf(v_dot(fNorm, v_scale(fMid, -1.0f)));
        SDL_Color fCol = shade_col(rk->accent, (0.72f + 0.35f * side) * fade);
        push_tri(tris, &nt, CAP, f0, f1, f2, cx, cy, fov, fCol);
    }

    /* Depth-sort all triangles far-to-near (painter's order, zero Z-fighting) */
    for (int i = 0; i < nt; i++)
        for (int j = i + 1; j < nt; j++)
            if (tris[j].depth > tris[i].depth) { Tri t = tris[i]; tris[i] = tris[j]; tris[j] = t; }
    for (int i = 0; i < nt; i++) {
        const Tri *t = &tris[i];
        fill_triangle(ren, t->x0, t->y0, t->x1, t->y1, t->x2, t->y2, t->col);
    }

    /* Small circular porthole on camera-facing flank when side profile is visible */
    float viewAlong = v_dot(camDir, dir);
    if (fabsf(viewAlong) < 0.85f) {
        Vec3 radialCam = v_norm(v_add(camDir, v_scale(dir, -viewAlong)));
        Vec3 port = v_add(v_add(center, v_scale(dir, 0.006f)), v_scale(radialCam, rad * 0.95f));
        if (port.z > 0.05f) {
            float pR = 0.0018f * fov / port.z;
            if (pR >= 1.0f) {
                float pX = cx + port.x * fov / port.z, pY = cy + port.y * fov / port.z;
                fill_circle(ren, pX, pY, pR, shade_col(0x2B3A4A, fade));
                fill_circle(ren, pX, pY, pR * 0.68f, shade_col(0x64D2FF, fade));
            }
        }
    }
}

static void draw(void *p, SDL_Renderer *r) {
    Starfield *f = p;
    float cx = f->w * 0.5f, cy = f->h * 0.5f, fov = f->w * 0.5f;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 2, 2, 10, 255);
    SDL_RenderFillRect(r, NULL);
    for (int i = 0; i < f->n; i++) {
        Star *s = &f->s[i];
        float sx = cx + s->x * fov / s->z, sy = cy + s->y * fov / s->z;
        if (sx < -10 || sy < -10 || sx > f->w + 10 || sy > f->h + 10) {
            reset_star(s, 1);
            continue;
        }
        float b = 1.0f - s->z / Z_STAR_FAR;
        if (b < 0) b = 0;
        float lum = b * (0.4f + 0.6f * b);
        Uint8 v = (Uint8)(255.0f * lum);
        if (v >= 3) {
            SDL_Color c = { (Uint8)(v * 0.88f), (Uint8)(v * 0.92f), v, 255 };
            float rad = f->line * (0.28f + 1.25f * b * b);
            if (s->hasPrev) {
                float dx = sx - s->px, dy = sy - s->py, len = sqrtf(dx * dx + dy * dy);
                if (len > 0.5f) {
                    float nx = -dy / len * rad, ny = dx / len * rad;
                    SDL_Color tail = { c.r, c.g, c.b, 0 };
                    SDL_Vertex tri[3] = {
                        { { s->px, s->py }, tail, { 0, 0 } },
                        { { sx + nx, sy + ny }, c, { 0, 0 } },
                        { { sx - nx, sy - ny }, c, { 0, 0 } },
                    };
                    SDL_RenderGeometry(r, NULL, tri, 3, NULL, 0);
                }
            }
            fill_circle(r, sx, sy, rad, c);
        }
        s->px = sx;
        s->py = sy;
        s->hasPrev = 1;
    }
    /* Draw rockets back-to-front by z */
    int ord[MAX_ROCKETS];
    for (int i = 0; i < MAX_ROCKETS; i++) ord[i] = i;
    for (int i = 0; i < MAX_ROCKETS; i++)
        for (int j = i + 1; j < MAX_ROCKETS; j++)
            if (f->r[ord[j]].z > f->r[ord[i]].z) { int t = ord[i]; ord[i] = ord[j]; ord[j] = t; }
    for (int i = 0; i < MAX_ROCKETS; i++)
        if (f->r[ord[i]].wait <= 0) draw_rocket(r, f, &f->r[ord[i]]);
}

static void destroy(void *p) {
    Starfield *f = p;
    free(f->s);
    free(f);
}

const Saver saver_starfield = { "starfield", "STARS", create, step, draw, destroy, NULL, NULL, "E SHIP", NULL, NULL, NULL };
