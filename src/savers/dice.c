/* DICE: a felt dice tray with true 3D polyhedral dice (d6, d8, d10, d12, d20).
   The dice tumble and bounce in 3D across the felt one at a time, each landing
   cleanly to show its roll. Then, with deliberate pauses so a six-year-old has time
   to calculate and frontrun the math, each result flies up to the chalkboard.
   Only a natural 20 on a d20 triggers the gold glow, rays, confetti and "CRIT!",
   and only a natural 1 on a d20 gets the grumpy cloud and "WOMP". */
#include "../engine.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

enum { D4, D6, D8, D10, D12, D20, KINDS };
enum { ROLL, TALLY_WAIT, TALLY_FLY, SUM_HOLD, EXIT, GAP };
enum { CALM, CRIT, FAIL };
enum { P_SPARK, P_CONFETTI, P_DUST, P_RAIN, P_CHALK };

#define MAXDICE 6
#define PARTS 260
#define POPS 8
#define NB 5
#define WOMP_COL 0xD4DCEA

static const int SIDES[KINDS] = { 4, 6, 8, 10, 12, 20 };
static const char *const NAMES[KINDS] = { "D4", "D6", "D8", "D10", "D12", "D20" };
static const uint32_t BODY[KINDS] = { 0xF0464E, 0xFF9230, 0xFFD23C, 0x26C6BE, 0x4F86F7, 0xA862F0 };
static const uint32_t CONFETTI[6] = { 0xFF5A6E, 0xFFD23C, 0x5BD46B, 0x4DA6FF, 0xC98BFF, 0xFFFFFF };
static const float BOUNCE[NB] = { 0.28f, 0.52f, 0.70f, 0.83f, 0.93f };

/* Handfuls of dice (the first is always the first roll). The d4 still has a model but
   sits out: a real d4 prints three numbers per face, two of them sideways or upside
   down, and at this pixel size those read as squiggles. */
static const int SETS[][MAXDICE + 1] = {
    { 2, D6, D8 }, { 1, D20 }, { 2, D6, D6 }, { 3, D6, D8, D10 },
    { 1, D6 }, { 2, D8, D8 }, { 2, D8, D12 }, { 1, D12 },
    { 2, D10, D10 }, { 1, D20 }, { 2, D6, D20 }, { 1, D10 },
    { 3, D6, D6, D6 }, { 3, D6, D8, D20 }, { 2, D20, D20 },
    { 5, D6, D8, D10, D12, D20 }
};
#define NSETS ((int)(sizeof SETS / sizeof SETS[0]))

/* ---------- 3D vector, quaternion and matrix math ---------- */

typedef struct { float x, y, z; } Vec3;
typedef struct { float w, x, y, z; } Quat;
typedef struct { float m[3][3]; } Mat3;

static Vec3 vec3(float x, float y, float z) { return (Vec3){ x, y, z }; }
static Vec3 v_add(Vec3 a, Vec3 b) { return (Vec3){ a.x + b.x, a.y + b.y, a.z + b.z }; }
static Vec3 v_sub(Vec3 a, Vec3 b) { return (Vec3){ a.x - b.x, a.y - b.y, a.z - b.z }; }
static Vec3 v_scale(Vec3 a, float s) { return (Vec3){ a.x * s, a.y * s, a.z * s }; }
static float v_dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static Vec3 v_cross(Vec3 a, Vec3 b) {
    return (Vec3){
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}
static float v_len(Vec3 a) { return sqrtf(a.x * a.x + a.y * a.y + a.z * a.z); }
static Vec3 v_norm(Vec3 a) {
    float l = v_len(a);
    return l > 1e-5f ? v_scale(a, 1.0f / l) : a;
}

static Quat quat_ident(void) { return (Quat){ 1, 0, 0, 0 }; }

static Quat quat_axis_angle(Vec3 axis, float angle) {
    axis = v_norm(axis);
    float half = angle * 0.5f;
    float s = sinf(half);
    return (Quat){ cosf(half), axis.x * s, axis.y * s, axis.z * s };
}

static Quat quat_mul(Quat a, Quat b) {
    return (Quat){
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w
    };
}

static Quat quat_norm(Quat q) {
    float l = sqrtf(q.w*q.w + q.x*q.x + q.y*q.y + q.z*q.z);
    return l > 1e-5f ? (Quat){ q.w/l, q.x/l, q.y/l, q.z/l } : quat_ident();
}

static Quat quat_slerp(Quat a, Quat b, float t) {
    float dot = a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
    if (dot < 0) { b = (Quat){ -b.w, -b.x, -b.y, -b.z }; dot = -dot; }
    if (dot > 0.9995f) {
        return quat_norm((Quat){
            a.w + t * (b.w - a.w),
            a.x + t * (b.x - a.x),
            a.y + t * (b.y - a.y),
            a.z + t * (b.z - a.z)
        });
    }
    float theta = acosf(dot < -1 ? -1 : (dot > 1 ? 1 : dot));
    float s1 = sinf((1 - t) * theta) / sinf(theta);
    float s2 = sinf(t * theta) / sinf(theta);
    return (Quat){
        s1 * a.w + s2 * b.w,
        s1 * a.x + s2 * b.x,
        s1 * a.y + s2 * b.y,
        s1 * a.z + s2 * b.z
    };
}

static Mat3 quat_to_mat3(Quat q) {
    float w = q.w, x = q.x, y = q.y, z = q.z;
    return (Mat3){{
        { 1 - 2*(y*y + z*z),     2*(x*y - w*z),     2*(x*z + w*y) },
        {     2*(x*y + w*z), 1 - 2*(x*x + z*z),     2*(y*z - w*x) },
        {     2*(x*z - w*y),     2*(y*z + w*x), 1 - 2*(x*x + y*y) }
    }};
}

static Vec3 mat3_mul_v(Mat3 m, Vec3 v) {
    return (Vec3){
        m.m[0][0] * v.x + m.m[0][1] * v.y + m.m[0][2] * v.z,
        m.m[1][0] * v.x + m.m[1][1] * v.y + m.m[1][2] * v.z,
        m.m[2][0] * v.x + m.m[2][1] * v.y + m.m[2][2] * v.z
    };
}

/* Aligns model vector src with world target vector. */
static Quat orient_face_to(Vec3 src, Vec3 target) {
    src = v_norm(src);
    target = v_norm(target);
    float dot = v_dot(src, target);
    if (dot > 0.9999f) return quat_ident();
    if (dot < -0.9999f) {
        Vec3 perp = fabsf(src.x) < 0.8f ? vec3(1, 0, 0) : vec3(0, 1, 0);
        Vec3 ax = v_norm(v_cross(src, perp));
        return quat_axis_angle(ax, PI_F);
    }
    Vec3 ax = v_cross(src, target);
    float angle = acosf(dot < -1 ? -1 : (dot > 1 ? 1 : dot));
    return quat_axis_angle(ax, angle);
}

/* ---------- polyhedral 3D mesh definitions ---------- */

#define MAX_POLY_VERTS 24
#define MAX_POLY_FACES 20
#define MAX_FACE_VERTS 5

typedef struct {
    int nverts;
    int idx[MAX_FACE_VERTS];
    int value;
    Vec3 normal;
    Vec3 c, up, right;   /* centre and in-plane axes the face's number or pips are printed on */
    float ri;            /* distance from the centre to the nearest edge */
} PolyFace;

typedef struct {
    int nverts;
    int nfaces;
    Vec3 verts[MAX_POLY_VERTS];
    PolyFace faces[MAX_POLY_FACES];
} PolyMesh;

static void init_poly_mesh(PolyMesh *m) {
    for (int f = 0; f < m->nfaces; f++) {
        PolyFace *fc = &m->faces[f];
        Vec3 v0 = m->verts[fc->idx[0]];
        Vec3 v1 = m->verts[fc->idx[1]];
        Vec3 v2 = m->verts[fc->idx[2]];
        Vec3 n = v_norm(v_cross(v_sub(v1, v0), v_sub(v2, v0)));
        Vec3 c = vec3(0, 0, 0);
        for (int i = 0; i < fc->nverts; i++) c = v_add(c, m->verts[fc->idx[i]]);
        c = v_scale(c, 1.0f / fc->nverts);
        if (v_dot(n, c) < 0) {
            for (int i = 0; i < fc->nverts / 2; i++) {
                int tmp = fc->idx[i];
                fc->idx[i] = fc->idx[fc->nverts - 1 - i];
                fc->idx[fc->nverts - 1 - i] = tmp;
            }
            n = v_scale(n, -1.0f);
        }
        fc->normal = n;
        fc->c = c;
        fc->up = v_norm(v_sub(m->verts[fc->idx[0]], c));
        fc->right = v_cross(n, fc->up);
        fc->ri = 1e9f;
        for (int i = 0; i < fc->nverts; i++) {
            Vec3 a = m->verts[fc->idx[i]], b = m->verts[fc->idx[(i + 1) % fc->nverts]];
            float dist = v_len(v_cross(v_norm(v_sub(b, a)), v_sub(c, a)));
            if (dist < fc->ri) fc->ri = dist;
        }
    }
}

/* Numbers the faces like real dice: opposite faces add up to sides + 1 (7 on a d6,
   21 on a d20). Called after init_poly_mesh, which knows the face normals. */
static void number_opposites(PolyMesh *m) {
    int next = 1, done[MAX_POLY_FACES] = { 0 };
    for (int f = 0; f < m->nfaces; f++) {
        if (done[f]) continue;
        int o = f;
        for (int g = 0; g < m->nfaces; g++)
            if (v_dot(m->faces[g].normal, m->faces[f].normal) < v_dot(m->faces[o].normal, m->faces[f].normal)) o = g;
        m->faces[f].value = next;
        m->faces[o].value = m->nfaces + 1 - next++;
        done[f] = done[o] = 1;
    }
}

static PolyMesh build_d4(void) {
    PolyMesh m = {0};
    m.nverts = 4;
    m.verts[0] = v_norm(vec3( 1,  1,  1));
    m.verts[1] = v_norm(vec3(-1, -1,  1));
    m.verts[2] = v_norm(vec3(-1,  1, -1));
    m.verts[3] = v_norm(vec3( 1, -1, -1));
    m.nfaces = 4;
    int faces[4][3] = { {0, 1, 2}, {0, 3, 1}, {0, 2, 3}, {1, 3, 2} };
    for (int i = 0; i < 4; i++) {
        m.faces[i].nverts = 3;
        m.faces[i].value = i + 1;
        for (int j = 0; j < 3; j++) m.faces[i].idx[j] = faces[i][j];
    }
    init_poly_mesh(&m);
    return m;
}

static PolyMesh build_d6(void) {
    PolyMesh m = {0};
    m.nverts = 8;
    for (int i = 0; i < 8; i++) {
        m.verts[i] = v_norm(vec3(
            (i & 1) ? 1.0f : -1.0f,
            (i & 2) ? 1.0f : -1.0f,
            (i & 4) ? 1.0f : -1.0f
        ));
    }
    m.nfaces = 6;
    /* Opposite faces sum to 7 */
    int fidx[6][4] = {
        { 4, 5, 7, 6 }, // 1: +Z
        { 6, 7, 3, 2 }, // 2: +Y
        { 5, 1, 3, 7 }, // 3: +X
        { 0, 4, 6, 2 }, // 4: -X
        { 0, 1, 5, 4 }, // 5: -Y
        { 0, 2, 3, 1 }, // 6: -Z
    };
    for (int i = 0; i < 6; i++) {
        m.faces[i].nverts = 4;
        m.faces[i].value = i + 1;
        for (int j = 0; j < 4; j++) m.faces[i].idx[j] = fidx[i][j];
    }
    init_poly_mesh(&m);
    /* Square the pip grid to the edges (not the corners) so pips sit like a real die's
       and the die comes to rest square on, not balanced on a corner. */
    for (int i = 0; i < 6; i++) {
        PolyFace *fc = &m.faces[i];
        Vec3 mid = v_scale(v_add(m.verts[fc->idx[0]], m.verts[fc->idx[1]]), 0.5f);
        fc->up = v_norm(v_sub(mid, fc->c));
        fc->right = v_cross(fc->normal, fc->up);
    }
    return m;
}

static PolyMesh build_d8(void) {
    PolyMesh m = {0};
    m.nverts = 6;
    m.verts[0] = vec3( 1,  0,  0);
    m.verts[1] = vec3(-1,  0,  0);
    m.verts[2] = vec3( 0,  1,  0);
    m.verts[3] = vec3( 0, -1,  0);
    m.verts[4] = vec3( 0,  0,  1);
    m.verts[5] = vec3( 0,  0, -1);
    m.nfaces = 8;
    int fidx[8][3] = {
        {4, 0, 2}, {4, 2, 1}, {4, 1, 3}, {4, 3, 0},
        {5, 1, 2}, {5, 2, 0}, {5, 0, 3}, {5, 3, 1}
    };
    for (int i = 0; i < 8; i++) {
        m.faces[i].nverts = 3;
        m.faces[i].value = i + 1;
        for (int j = 0; j < 3; j++) m.faces[i].idx[j] = fidx[i][j];
    }
    init_poly_mesh(&m);
    number_opposites(&m);
    return m;
}

static PolyMesh build_d10(void) {
    /* Pentagonal trapezohedron: two apexes and a zig-zag ring. The ring height is
       chosen so every kite is flat. */
    PolyMesh m = {0};
    const float A = 1.05f, rr = 0.95f, c36 = cosf(PI_F / 5), h = A * (1 - c36) / (1 + c36);
    m.nverts = 12;
    m.verts[0] = vec3(0, 0, A);
    m.verts[1] = vec3(0, 0, -A);
    for (int k = 0; k < 5; k++) {
        float a = k * 2.0f * PI_F / 5.0f, b = a + PI_F / 5.0f;
        m.verts[2 + k] = vec3(rr * cosf(a), rr * sinf(a), h);    /* upper ring */
        m.verts[7 + k] = vec3(rr * cosf(b), rr * sinf(b), -h);   /* lower ring */
    }
    m.nfaces = 10;
    for (int k = 0; k < 5; k++) {
        int k1 = (k + 1) % 5;
        PolyFace *top = &m.faces[k * 2], *bot = &m.faces[k * 2 + 1];
        *top = (PolyFace){ .nverts = 4, .idx = { 0, 2 + k, 7 + k, 2 + k1 }, .value = 2 * k + 1 };
        *bot = (PolyFace){ .nverts = 4, .idx = { 1, 7 + k, 2 + k1, 7 + k1 }, .value = 2 * k + 2 };
    }
    init_poly_mesh(&m);
    number_opposites(&m);
    return m;
}

static PolyMesh build_d12(void) {
    PolyMesh m = {0};
    m.nverts = 20;
    float phi = 1.6180339887f, ip = 1.0f / phi;
    int v = 0;
    for (int x = -1; x <= 1; x += 2)
        for (int y = -1; y <= 1; y += 2)
            for (int z = -1; z <= 1; z += 2)
                m.verts[v++] = vec3((float)x, (float)y, (float)z);
    for (int s1 = -1; s1 <= 1; s1 += 2)
        for (int s2 = -1; s2 <= 1; s2 += 2) {
            m.verts[v++] = vec3(0, (float)s1 * ip, (float)s2 * phi);
            m.verts[v++] = vec3((float)s1 * ip, (float)s2 * phi, 0);
            m.verts[v++] = vec3((float)s1 * phi, 0, (float)s2 * ip);
        }
    for (int i = 0; i < 20; i++) m.verts[i] = v_norm(m.verts[i]);

    Vec3 fnorm[12] = {
        /* face centres point at the vertices of the dual icosahedron */
        vec3(0,  phi,  1), vec3(0,  phi, -1), vec3(0, -phi,  1), vec3(0, -phi, -1),
        vec3( 1, 0,  phi), vec3(-1, 0,  phi), vec3( 1, 0, -phi), vec3(-1, 0, -phi),
        vec3( phi,  1, 0), vec3( phi, -1, 0), vec3(-phi,  1, 0), vec3(-phi, -1, 0)
    };
    for (int i = 0; i < 12; i++) fnorm[i] = v_norm(fnorm[i]);

    m.nfaces = 12;
    for (int f = 0; f < 12; f++) {
        Vec3 fn = fnorm[f];
        int cverts[5];
        float cdots[5] = {-99,-99,-99,-99,-99};
        for (int i = 0; i < 20; i++) {
            float d = v_dot(m.verts[i], fn);
            for (int j = 0; j < 5; j++) {
                if (d > cdots[j]) {
                    for (int k = 4; k > j; k--) {
                        cdots[k] = cdots[k-1];
                        cverts[k] = cverts[k-1];
                    }
                    cdots[j] = d;
                    cverts[j] = i;
                    break;
                }
            }
        }
        Vec3 ref = v_norm(v_sub(m.verts[cverts[0]], v_scale(fn, v_dot(m.verts[cverts[0]], fn))));
        Vec3 ref2 = v_cross(fn, ref);
        float angles[5];
        for (int i = 0; i < 5; i++) {
            Vec3 p = m.verts[cverts[i]];
            angles[i] = atan2f(v_dot(p, ref2), v_dot(p, ref));
        }
        for (int i = 0; i < 5; i++)
            for (int j = i + 1; j < 5; j++)
                if (angles[j] < angles[i]) {
                    float ta = angles[i]; angles[i] = angles[j]; angles[j] = ta;
                    int tv = cverts[i]; cverts[i] = cverts[j]; cverts[j] = tv;
                }
        m.faces[f].nverts = 5;
        m.faces[f].value = f + 1;
        for (int i = 0; i < 5; i++) m.faces[f].idx[i] = cverts[i];
    }
    init_poly_mesh(&m);
    number_opposites(&m);
    return m;
}

static PolyMesh build_d20(void) {
    PolyMesh m = {0};
    m.nverts = 12;
    float phi = 1.6180339887f;
    float f = 1.0f / sqrtf(1.0f + phi * phi);
    float pf = phi * f;
    m.verts[0]  = vec3(-f,  0,  pf);
    m.verts[1]  = vec3( f,  0,  pf);
    m.verts[2]  = vec3(-f,  0, -pf);
    m.verts[3]  = vec3( f,  0, -pf);
    m.verts[4]  = vec3( 0,  pf,  f);
    m.verts[5]  = vec3( 0,  pf, -f);
    m.verts[6]  = vec3( 0, -pf,  f);
    m.verts[7]  = vec3( 0, -pf, -f);
    m.verts[8]  = vec3( pf,  f,  0);
    m.verts[9]  = vec3(-pf,  f,  0);
    m.verts[10] = vec3( pf, -f,  0);
    m.verts[11] = vec3(-pf, -f,  0);
    for (int i = 0; i < 12; i++) m.verts[i] = v_norm(m.verts[i]);

    m.nfaces = 20;
    int fidx[20][3] = {
        {0, 4, 1}, {0, 9, 4}, {0, 11, 9}, {0, 6, 11}, {0, 1, 6},
        {1, 4, 8}, {4, 9, 5}, {9, 11, 2}, {11, 6, 7}, {6, 1, 10},
        {3, 2, 5}, {3, 5, 8}, {3, 8, 10}, {3, 10, 7}, {3, 7, 2},
        {2, 5, 9}, {5, 8, 4}, {8, 10, 1}, {10, 7, 6}, {7, 2, 11}
    };
    int fvals[20] = {
        20, 2, 3, 4, 5,
        6, 7, 8, 9, 10,
        1, 19, 18, 17, 16,
        15, 14, 13, 12, 11
    };
    for (int i = 0; i < 20; i++) {
        m.faces[i].nverts = 3;
        m.faces[i].value = fvals[i];
        for (int j = 0; j < 3; j++) m.faces[i].idx[j] = fidx[i][j];
    }
    init_poly_mesh(&m);
    number_opposites(&m);
    return m;
}

/* ---------- die and particle state ---------- */

typedef struct {
    int kind, value, react, settled, tallied, counted;
    float sx, sy, tx, ty, t0, dur, z0, u;
    float x, y, z, tint, impact, impactT, st;
    uint32_t tintCol;
    Quat q_tumble, q_target, q_snap;
    Vec3 tumble_axis;
    float tumble_speed;
    Mat3 orient;
} Die;

typedef struct { float x, y, vx, vy, life, max, ph; int kind; uint32_t col; } Part;
typedef struct { float x0, y0, t; int value, on; } Fly;
typedef struct { char txt[8]; float x, y, t, max; int sc; uint32_t col; } Pop;

typedef struct {
    int w, h, S, W, H, R, F, small;
    int bx, by, bw, bh;
    float t, stateT, tick, hold, tallyWait;
    int state, n, rows, set, fromLeft, total, shown, hasCrit;
    int nextTally;
    char expr[48];
    Die die[MAXDICE];
    Fly fly[MAXDICE];
    Pop pop[POPS];
    Part parts[PARTS];
    PolyMesh mesh[KINDS];
    Canvas under, top;
    unsigned char *mark;
    SDL_Texture *felt, *underTex, *topTex;
} Dice;

/* ---------- drawing helpers ---------- */

static float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static float lerp(float a, float b, float t) { return a + (b - a) * t; }
static float ease(float k) { k = clampf(k, 0, 1); return k * k * (3 - 2 * k); }

static uint32_t shade(uint32_t hex, float f) {
    int r = (int)(((hex >> 16) & 255) * f), g = (int)(((hex >> 8) & 255) * f), b = (int)((hex & 255) * f);
    r = r > 255 ? 255 : (r < 0 ? 0 : r);
    g = g > 255 ? 255 : (g < 0 ? 0 : g);
    b = b > 255 ? 255 : (b < 0 ? 0 : b);
    return (uint32_t)(r << 16 | g << 8 | b);
}

static uint32_t mixc(uint32_t a, uint32_t b, float t) {
    int r = (int)lerp((float)((a >> 16) & 255), (float)((b >> 16) & 255), t);
    int g = (int)lerp((float)((a >> 8) & 255), (float)((b >> 8) & 255), t);
    int bl = (int)lerp((float)(a & 255), (float)(b & 255), t);
    return (uint32_t)(r << 16 | g << 8 | bl);
}

static uint32_t lit(uint32_t hex, float f) { return f >= 1 ? mixc(hex, 0xFFFFFF, f - 1) : shade(hex, f); }
static uint32_t rgba(uint32_t hex, int a) { return (hex << 8) | (uint32_t)(a < 0 ? 0 : (a > 255 ? 255 : a)); }
static void hline(Canvas *c, int x0, int x1, int y, uint32_t hex) { fill_rect(c, x0, y, x1, y, rgb(hex)); }

static void mul_px(Canvas *c, int x, int y, float f) {
    uint32_t p = get(c, x, y);
    if (p) put(c, x, y, rgb(shade(p >> 8, f)));
}

static void draw_line(Canvas *c, int x0, int y0, int x1, int y1, uint32_t col) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (1) {
        put(c, x0, y0, col);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static const char *const PLUS[FONT_H] = { ".....", "..#..", "..#..", "#####", "..#..", "..#..", "....." };
static const char *const EQUALS[FONT_H] = { ".....", ".....", "#####", ".....", "#####", ".....", "....." };

static void glyphs(Canvas *c, const char *s, int x, int y, int sc, uint32_t col) {
    for (; *s; s++, x += (FONT_W + 1) * sc) {
        const char *const *g = *s == '+' ? PLUS : (*s == '=' ? EQUALS : font_glyph(*s));
        for (int j = 0; g && j < FONT_H; j++)
            for (int i = 0; i < FONT_W; i++)
                if (g[j][i] == '#') fill_rect(c, x + i * sc, y + j * sc, x + i * sc + sc - 1, y + j * sc + sc - 1, col);
    }
}

static int glyphs_w(const char *s, int sc) { int n = (int)strlen(s); return n ? n * (FONT_W + 1) * sc - sc : 0; }

/* ---------- 3D die rendering ---------- */

/* The dice are seen by a camera looking down at about 50 degrees: a die's own depth
   shrinks by CAM_S and its height by CAM_C on screen (the felt positions and the bounce
   keep their old 1 : 0.85 slant), so faces whose normal points along (0, 0.85, 1) face
   the viewer. Dice come to rest with the rolled face pointing at REST: mostly up, tipped
   a little toward the viewer so the top reads clearly, turned so its number is upright.
   Numbers and pips are printed on the faces themselves: each face maps a little bitmap
   through its own tilted axes, so they foreshorten and tumble with the die. */
#define CAM_S 0.762f   /* sin and cos of the camera's 49.6 degree look-down (tan = 1 / 0.85) */
#define CAM_C 0.648f

static Vec3 rest_dir(void) { return v_norm(vec3(0, 0.38f, 1.0f)); }

static const float PIPS[7][6][2] = {
    {{0}},
    {{0, 0}},
    {{-0.55f, -0.55f}, {0.55f, 0.55f}},
    {{-0.55f, -0.55f}, {0, 0}, {0.55f, 0.55f}},
    {{-0.55f, -0.55f}, {0.55f, -0.55f}, {-0.55f, 0.55f}, {0.55f, 0.55f}},
    {{-0.55f, -0.55f}, {0.55f, -0.55f}, {0, 0}, {-0.55f, 0.55f}, {0.55f, 0.55f}},
    {{-0.55f, -0.55f}, {0.55f, -0.55f}, {-0.55f, 0}, {0.55f, 0}, {-0.55f, 0.55f}, {0.55f, 0.55f}},
};

/* Where a model-space direction lands on screen (in canvas pixels per unit). */
static void to_screen(const Die *e, float R, Vec3 v, float *x, float *y) {
    Vec3 w = mat3_mul_v(e->orient, v);
    *x = w.x * R;
    *y = (w.y * CAM_S - w.z * CAM_C) * R;
}

/* Paints a mask (0 nothing, 1 rim, 2 ink) centred on (ox, oy). One mask pixel steps by
   (ax, ay) across and (bx, by) down, so the bitmap lies flat on a tilted face. */
static void paint_on_face(Canvas *c, const unsigned char *mask, int mw, int mh, float ox, float oy,
                          float ax, float ay, float bx, float by, uint32_t rim, uint32_t ink) {
    float det = ax * by - ay * bx;
    if (fabsf(det) < 0.05f) return;  /* seen edge-on */
    float hw = mw * 0.5f, hh = mh * 0.5f, x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (int k = 0; k < 4; k++) {
        float u = k & 1 ? hw : -hw, v = k & 2 ? hh : -hh, px = ox + ax * u + bx * v, py = oy + ay * u + by * v;
        x0 = fminf(x0, px); x1 = fmaxf(x1, px); y0 = fminf(y0, py); y1 = fmaxf(y1, py);
    }
    for (int y = (int)floorf(y0); y <= (int)ceilf(y1); y++)
        for (int x = (int)floorf(x0); x <= (int)ceilf(x1); x++) {
            float dx = x + 0.5f - ox, dy = y + 0.5f - oy;
            int i = (int)floorf((dx * by - dy * bx) / det + hw), j = (int)floorf((ax * dy - ay * dx) / det + hh);
            if (i < 0 || j < 0 || i >= mw || j >= mh || !mask[j * mw + i]) continue;
            put(c, x, y, rgb(mask[j * mw + i] == 2 ? ink : rim));
        }
}

/* A number printed on a face: centre p, text axes right/up (model space), mask pixel size px. */
static void face_number(Dice *d, const Die *e, float ox, float oy, Vec3 right, Vec3 up, float px,
                        int value, uint32_t face, uint32_t ink, float a) {
    uint32_t rim = mixc(face, shade(face, 0.42f), a);
    ink = mixc(face, ink, a);
    char s[4];
    snprintf(s, sizeof s, "%d", value);
    int bar = value == 6 || value == 9, tw = glyphs_w(s, 1), th = FONT_H + (bar ? 2 : 0);
    int mw = tw + 2, mh = th + 2;
    unsigned char mask[16 * 13] = { 0 };
    for (int k = 0; s[k]; k++) {
        const char *const *g = font_glyph(s[k]);
        for (int j = 0; g && j < FONT_H; j++)
            for (int i = 0; i < FONT_W; i++)
                if (g[j][i] == '#') mask[(j + 1) * mw + k * (FONT_W + 1) + i + 1] = 2;
    }
    if (bar) for (int i = 0; i < tw; i++) mask[th * mw + i + 1] = 2;   /* 6. and 9. */
    for (int j = 0; j < mh; j++)
        for (int i = 0; i < mw; i++)
            if (!mask[j * mw + i] && ((i > 0 && mask[j * mw + i - 1] == 2) || (i < mw - 1 && mask[j * mw + i + 1] == 2) ||
                                      (j > 0 && mask[(j - 1) * mw + i] == 2) || (j < mh - 1 && mask[(j + 1) * mw + i] == 2)))
                mask[j * mw + i] = 1;
    float ax, ay, bx, by, R = (float)d->R;
    to_screen(e, R, v_scale(right, px), &ax, &ay);
    to_screen(e, R, v_scale(up, -px), &bx, &by);
    if (ax > 0 && by > 0 && fabsf(ay) < 0.1f * ax && fabsf(bx) < 0.1f * by) {
        /* Square on to the viewer (a die at rest): draw it as crisp whole pixels. */
        int sc = (int)lroundf(fminf(ax, by)), x0, y0;
        if (sc < 1) sc = 1;
        x0 = (int)lroundf(ox - mw * sc / 2.0f);
        y0 = (int)lroundf(oy - mh * sc / 2.0f);
        for (int j = 0; j < mh; j++)
            for (int i = 0; i < mw; i++)
                if (mask[j * mw + i])
                    fill_rect(&d->top, x0 + i * sc, y0 + j * sc, x0 + i * sc + sc - 1, y0 + j * sc + sc - 1,
                              rgb(mask[j * mw + i] == 2 ? ink : rim));
        return;
    }
    paint_on_face(&d->top, mask, mw, mh, ox, oy, ax, ay, bx, by, rim, ink);
}

/* A pip: a disc on the face at (u, v) in the face's own units, with a dark rim. */
static void face_pip(Dice *d, const Die *e, float ox, float oy, const PolyFace *fc, float u, float v, uint32_t face, float a) {
    float R = (float)d->R, Ax, Ay, Bx, By;
    to_screen(e, R, v_scale(fc->right, fc->ri), &Ax, &Ay);
    to_screen(e, R, v_scale(fc->up, -fc->ri), &Bx, &By);
    float det = Ax * By - Ay * Bx;
    if (fabsf(det) < 0.5f) return;
    float pcx = ox + Ax * u + Bx * v, pcy = oy + Ay * u + By * v, ext = 0.3f * (fabsf(Ax) + fabsf(Bx) + fabsf(Ay) + fabsf(By));
    for (int y = (int)floorf(pcy - ext); y <= (int)ceilf(pcy + ext); y++)
        for (int x = (int)floorf(pcx - ext); x <= (int)ceilf(pcx + ext); x++) {
            float dx = x + 0.5f - pcx, dy = y + 0.5f - pcy;
            float lu = (dx * By - dy * Bx) / det, lv = (Ax * dy - Ay * dx) / det, q = lu * lu + lv * lv;
            if (q < 0.19f * 0.19f) put(&d->top, x, y, rgb(mixc(face, 0xFFFFFF, a)));
            else if (q < 0.26f * 0.26f) put(&d->top, x, y, rgb(mixc(face, shade(face, 0.45f), a)));
        }
}

static void draw_3d_die(Dice *d, const Die *e, float cx, float cy) {
    const PolyMesh *m = &d->mesh[e->kind];
    float R = (float)d->R;
    Vec3 center = vec3(cx, cy, e->z);

    /* Transform vertices */
    float sx[MAX_POLY_VERTS], sy[MAX_POLY_VERTS], depth[MAX_POLY_VERTS];
    for (int i = 0; i < m->nverts; i++) {
        Vec3 vrot = mat3_mul_v(e->orient, m->verts[i]);
        Vec3 pw = v_add(center, v_scale(vrot, R));
        sx[i] = pw.x;
        sy[i] = cy - e->z * 0.85f + (vrot.y * CAM_S - vrot.z * CAM_C) * R;
        depth[i] = pw.y * 0.85f + pw.z;
    }

    /* Sort visible faces by depth back to front */
    typedef struct { int f; float d; float view; } VisFace;
    VisFace vis[MAX_POLY_FACES];
    int nvis = 0;
    for (int f = 0; f < m->nfaces; f++) {
        const PolyFace *fc = &m->faces[f];
        float fx[MAX_FACE_VERTS], fy[MAX_FACE_VERTS];
        for (int i = 0; i < fc->nverts; i++) {
            fx[i] = sx[fc->idx[i]];
            fy[i] = sy[fc->idx[i]];
        }
        float a2d = 0;
        for (int i = 0; i < fc->nverts; i++) {
            int j = (i + 1) % fc->nverts;
            a2d += fx[i] * fy[j] - fx[j] * fy[i];
        }
        if (a2d <= 0.1f) continue; /* faces turned away from the viewer wind clockwise on screen */

        float davg = 0;
        for (int i = 0; i < fc->nverts; i++) davg += depth[fc->idx[i]];
        davg /= fc->nverts;
        vis[nvis++] = (VisFace){ f, davg, a2d / (R * R) };
    }
    for (int i = 0; i < nvis; i++)
        for (int j = i + 1; j < nvis; j++)
            if (vis[j].d < vis[i].d) { VisFace t = vis[i]; vis[i] = vis[j]; vis[j] = t; }

    /* The face pointing most along REST is the roll (it gets the gold ink on a natural 20). */
    Vec3 up = rest_dir();
    int topf = 0;
    float bf = -2;
    for (int f = 0; f < m->nfaces; f++) {
        float k = v_dot(mat3_mul_v(e->orient, m->faces[f].normal), up);
        if (k > bf) { bf = k; topf = f; }
    }
    float ocx = cx, ocy = cy - e->z * 0.85f;

    Vec3 light = v_norm(vec3(-0.5f, -0.6f, 0.8f));
    uint32_t base = BODY[e->kind];

    for (int vi = 0; vi < nvis; vi++) {
        int f = vis[vi].f;
        const PolyFace *fc = &m->faces[f];
        Vec3 nrot = mat3_mul_v(e->orient, fc->normal);
        float diff = v_dot(nrot, light);
        float lfactor = 0.68f + 0.36f * diff;
        uint32_t col = lit(base, lfactor);
        if (e->tint > 0.01f) col = mixc(col, e->tintCol, e->tint);

        float fx[MAX_FACE_VERTS], fy[MAX_FACE_VERTS];
        for (int i = 0; i < fc->nverts; i++) {
            fx[i] = sx[fc->idx[i]];
            fy[i] = sy[fc->idx[i]];
        }
        fill_poly(&d->top, fx, fy, fc->nverts, rgb(col));

        /* Crisp bevelled edge outlines between faces */
        uint32_t edge_col = diff > 0 ? lit(col, 1.25f) : shade(col, 0.75f);
        for (int i = 0; i < fc->nverts; i++) {
            int j = (i + 1) % fc->nverts;
            draw_line(&d->top, (int)fx[i], (int)fy[i], (int)fx[j], (int)fy[j], rgb(edge_col));
        }

        /* Print the face: pips on the d6, a number by each corner on the d4 (the top
           corner's number is the roll, as on a real d4), one number in the middle on the
           rest. Every face always carries the same print; as a face turns away its print
           fades into the face colour (squashed pixel digits read as noise), so nothing
           pops in or out. */
        float facing = v_dot(nrot, v_norm(vec3(0, 0.85f, 1)));
        float lo = e->kind == D6 ? 0.2f : e->kind == D4 ? 0.15f : 0.45f;
        float ink_a = ease((facing - lo) / 0.3f);
        if (ink_a < 0.04f) continue;
        float fcx, fcy;
        to_screen(e, R, fc->c, &fcx, &fcy);
        fcx += ocx; fcy += ocy;
        if (e->kind == D6) {
            for (int p = 0; p < fc->value; p++)
                face_pip(d, e, fcx, fcy, fc, PIPS[fc->value][p][0], PIPS[fc->value][p][1], col, ink_a);
        } else if (e->kind == D4) {
            for (int k = 0; k < 3; k++) {
                Vec3 vtx = m->verts[fc->idx[k]], toward = v_norm(v_sub(vtx, fc->c));
                Vec3 at = v_add(fc->c, v_scale(v_sub(vtx, fc->c), 0.5f));
                float px, py;
                to_screen(e, R, at, &px, &py);
                face_number(d, e, px + ocx, py + ocy, v_cross(fc->normal, toward), toward,
                            fc->ri * 0.95f / FONT_H, fc->idx[k] + 1, col, 0xFFFFFF, ink_a);
            }
        } else {
            char s[4];
            snprintf(s, sizeof s, "%d", fc->value);
            float tw = (float)glyphs_w(s, 1), px = fminf(1.3f * fc->ri / FONT_H, 1.75f * fc->ri / tw);
            uint32_t ink = (e->kind == D20 && fc->value == 20 && e->settled && f == topf) ? 0xFFE04A : 0xFFFFFF;
            face_number(d, e, fcx, fcy, fc->right, fc->up, px, fc->value, col, ink, ink_a);
        }
    }
}

/* ---------- tray and chalkboard ---------- */

static void wood(Canvas *c, int x0, int y0, int x1, int y1, uint32_t base, int vertical) {
    fill_rect(c, x0, y0, x1, y1, rgb(base));
    int n = vertical ? x1 - x0 + 1 : y1 - y0 + 1, len = vertical ? y1 - y0 + 1 : x1 - x0 + 1;
    for (int k = 0; k < n; k++)
        for (int p = irand(12); p < len; p += 6 + irand(24)) {
            int run = 4 + irand(16);
            uint32_t col = shade(base, irand(3) ? 0.86f : 1.1f);
            for (int q = p; q < p + run && q < len; q++)
                if (vertical) put(c, x0 + k, y0 + q, rgb(col));
                else put(c, x0 + q, y0 + k, rgb(col));
            p += run;
        }
}

static void build_tray(Dice *d, Canvas *c) {
    int W = d->W, H = d->H, F = d->F;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            float dx = (x - W * 0.5f) / (W * 0.62f), dy = (y - H * 0.52f) / (H * 0.7f);
            float f = 1.12f - 0.38f * (dx * dx + dy * dy);
            int n = irand(16);
            if (n == 0) f *= 0.9f;
            else if (n == 1) f *= 1.08f;
            uint32_t col = shade(0x2A7A4A, f);
            if ((x + y) % 2 == 0 && irand(5) == 0) col = shade(col, 0.95f);
            put(c, x, y, rgb(col));
        }
    int s0 = F + 3;
    for (int x = s0; x <= W - 1 - s0; x++)
        if ((x - s0) % 4 < 2) { put(c, x, s0, rgb(0x5EAE76)); put(c, x, H - 1 - s0, rgb(0x4E9A66)); }
    for (int y = s0; y <= H - 1 - s0; y++)
        if ((y - s0) % 4 < 2) { put(c, s0, y, rgb(0x5EAE76)); put(c, W - 1 - s0, y, rgb(0x4E9A66)); }
    static const float sh[4] = { 0.55f, 0.7f, 0.83f, 0.93f };
    for (int i = 0; i < 4; i++) {
        for (int x = F; x < W - F; x++) mul_px(c, x, F + i, sh[i]);
        for (int y = F + i; y < H - F; y++) mul_px(c, F + i, y, sh[i]);
    }
    for (int x = F; x < W - F; x++) mul_px(c, x, H - F - 1, 0.82f);
    for (int y = F; y < H - F; y++) mul_px(c, W - F - 1, y, 0.82f);
    wood(c, 0, 0, W - 1, F - 1, 0x9A6236, 0);
    wood(c, 0, H - F, W - 1, H - 1, 0x9A6236, 0);
    wood(c, 0, F, F - 1, H - F - 1, 0x9A6236, 1);
    wood(c, W - F, F, W - 1, H - F - 1, 0x9A6236, 1);
    hline(c, F, W - F - 1, F - 1, 0xC6864C);
    hline(c, F, W - F - 1, H - F, 0x56341C);
    for (int y = F; y < H - F; y++) { put(c, F - 1, y, rgb(0xC6864C)); put(c, W - F, y, rgb(0x56341C)); }

    int bx = d->bx, by = d->by, bw = d->bw, bh = d->bh, fr = d->small ? 2 : 3;
    wood(c, bx, by, bx + bw - 1, by + bh - 1, 0x6E4220, 0);
    int ix0 = bx + fr, iy0 = by + fr, ix1 = bx + bw - 1 - fr, iy1 = by + bh - 1 - fr;
    for (int y = iy0; y <= iy1; y++)
        for (int x = ix0; x <= ix1; x++) {
            float g = 1.0f + 0.1f * sinf(x * 0.15f + y * 0.08f);
            uint32_t col = shade(0x283630, g);
            if (irand(12) == 0) col = lit(col, 1.15f);
            put(c, x, y, rgb(col));
        }
    fill_rect(c, ix0, iy0, ix1, iy0, rgb(0x1C2622));
    fill_rect(c, ix0, iy0, ix0, iy1, rgb(0x1C2622));
    if (!d->small) {
        fill_rect(c, ix1 - 8, iy1 - 1, ix1 - 3, iy1, rgb(0xF2F0E6));
        hline(c, ix1 - 8, ix1 - 3, iy1, 0xC8C6BC);
    }
    for (int i = 0; i < 2; i++) {
        int nx = i ? bx + bw - 3 : bx + 2, ny = by + 1;
        put(c, nx, ny, rgb(0xE0E4EA)); put(c, nx, ny + 1, rgb(0x707882));
    }
}

/* ---------- roll sequencing and physics ---------- */

static Quat compute_target_orientation(const PolyMesh *m, int kind, int value) {
    Vec3 n = vec3(0, 0, 1);
    if (kind == D4) n = v_norm(m->verts[value - 1]);     /* d4: the rolled corner points up */
    else
        for (int f = 0; f < m->nfaces; f++)
            if (m->faces[f].value == value) { n = m->faces[f].normal; break; }
    Vec3 up = rest_dir();
    if (kind == D4) {
        /* A d4 sits flat on the felt with the rolled corner straight up, one face turned
           toward the viewer so the number near that corner reads upright. */
        Vec3 z = vec3(0, 0, 1);
        Quat align = orient_face_to(n, z);
        Mat3 a = quat_to_mat3(align);
        const PolyFace *front = &m->faces[0];
        for (int f = 0; f < m->nfaces; f++) {
            int has = 0;
            for (int i = 0; i < 3; i++) has |= m->faces[f].idx[i] == value - 1;
            if (has) { front = &m->faces[f]; break; }
        }
        Vec3 fn = mat3_mul_v(a, front->normal);
        float turn = atan2f(fn.x, fn.y) + frand(-0.3f, 0.3f);   /* swing its normal round to +y */
        return quat_norm(quat_mul(quat_axis_angle(z, turn), align));
    }
    if (kind == D6) up = vec3(0, 0, 1);   /* a cube sits flat, so its front face shows too */
    Quat align = orient_face_to(n, up);
    float turn;
    {
        /* Turn the rolled face's number exactly upright (its "up" pointing away from the
           viewer): even a slight tilt makes pixel digits jagged. */
        const PolyFace *fc = &m->faces[0];
        for (int f = 0; f < m->nfaces; f++) if (m->faces[f].value == value) fc = &m->faces[f];
        Vec3 u1 = mat3_mul_v(quat_to_mat3(align), fc->up), away = vec3(0, -1, 0);
        away = v_norm(v_sub(away, v_scale(up, v_dot(away, up))));
        turn = atan2f(v_dot(v_cross(u1, away), up), v_dot(u1, away));
    }
    return quat_norm(quat_mul(quat_axis_angle(up, turn), align));
}

static void new_roll(Dice *d) {
    d->set = d->t < 0.5f ? 0 : 1 + irand(NSETS - 1);
    const int *set = SETS[d->set];
    int n = set[0], R = d->R;
    d->n = n;
    d->fromLeft = irand(2);
    d->total = d->shown = d->hasCrit = 0;
    d->nextTally = 0;
    d->expr[0] = 0;
    if (n == 1) snprintf(d->expr, sizeof d->expr, "%s", NAMES[set[1]]);
    memset(d->fly, 0, sizeof d->fly);

    float x0 = d->F + 8, x1 = d->W - d->F - 8, y0 = d->by + d->bh + 4, y1 = d->H - d->F - 12;
    float px = 2.0f * R + 18, py = 2.0f * R + 20;
    int cols = n;
    while (cols > 1 && cols * px > x1 - x0) cols--;
    int rows = (n + cols - 1) / cols;
    cols = (n + rows - 1) / rows;
    d->rows = rows;
    if (rows > 1 || d->small) y1 = d->H - d->F - (d->small ? 3 : 6);
    if (rows * py > y1 - y0) py = (y1 - y0) / rows;
    if (cols * px > x1 - x0) px = (x1 - x0) / cols;
    float dir = d->fromLeft ? 1.0f : -1.0f;

    /* Pacing: roll 1 die at a time so each roll has its own individual spotlight */
    float roll_interval = 2.2f;
    for (int i = 0; i < n; i++) {
        Die *e = &d->die[i];
        memset(e, 0, sizeof *e);
        int row = i / cols, col = i % cols, m = row == rows - 1 ? n - row * cols : cols;
        e->kind = set[i + 1];
        e->value = 1 + irand(SIDES[e->kind]);   /* fair dice: a natural 20 or 1 is 1 in 20 */
        e->tx = (x0 + x1) * 0.5f + (col - (m - 1) * 0.5f) * px + frand(-3, 3);
        e->ty = (y0 + y1) * 0.5f + (row - (rows - 1) * 0.5f) * py + frand(-3, 3);
        e->sx = d->fromLeft ? -R * 1.4f - frand(0, 16) : d->W + R * 1.4f + frand(0, 16);
        e->sy = e->ty + frand(-14, 14);

        /* Slots stay left to right (that's the tally order); the slot farthest from the
           entry side fills first so a new die never flies through a settled one. */
        int along = d->fromLeft ? m - 1 - col : col;
        e->t0 = (row * cols + along) * roll_interval;
        e->dur = 1.7f;
        e->z0 = d->H * frand(0.28f, 0.40f);
        e->x = e->sx; e->y = e->sy; e->z = e->z0;
        e->u = -1;

        /* Tumbling physics setup */
        e->tumble_speed = frand(18.0f, 26.0f);
        float tdx = e->tx - e->sx, tdy = e->ty - e->sy;
        Vec3 roll_ax = v_norm(vec3(-tdy, tdx, 0));
        e->tumble_axis = v_norm(v_add(roll_ax, vec3(frand(-0.35f, 0.35f), frand(-0.35f, 0.35f), -dir * frand(0.4f, 0.8f))));
        e->q_tumble = quat_axis_angle(vec3(frand(-1, 1), frand(-1, 1), frand(-1, 1)), frand(0, 6.28f));
        e->q_target = compute_target_orientation(&d->mesh[e->kind], e->kind, e->value);
        e->orient = quat_to_mat3(e->q_tumble);
    }
    d->state = ROLL;
    d->stateT = 0;
}

static Part *add_part(Dice *d, float x, float y, float vx, float vy, int kind, uint32_t col, float life) {
    for (int i = 0; i < PARTS; i++) {
        Part *q = &d->parts[i];
        if (q->life > 0) continue;
        *q = (Part){ x, y, vx, vy, life, life, frand(0, 6.28f), kind, col };
        return q;
    }
    return NULL;
}

static void add_pop(Dice *d, const char *txt, float x, float y, int sc, uint32_t col, float life) {
    for (int i = 0; i < POPS; i++) {
        Pop *p = &d->pop[i];
        if (p->t < p->max) continue;
        *p = (Pop){ { 0 }, x, y, 0, life, sc, col };
        snprintf(p->txt, sizeof p->txt, "%s", txt);
        return;
    }
}

static void burst(Dice *d, float x, float y, int n, float speed) {
    for (int i = 0; i < n; i++) {
        float a = frand(0, 2 * PI_F), v = d->H * speed * frand(0.4f, 1.0f);
        add_part(d, x, y, cosf(a) * v, sinf(a) * v - d->H * 0.15f, P_SPARK, irand(3) ? 0xFFE27A : 0xFFFFFF, frand(0.6f, 1.1f));
    }
}

/* Settle reactions: keep celebrations and failures rare and special.
   Only a natural 20 on a d20 gets CRIT!, and only a 1 on a d20 gets WOMP. */
static void settle(Dice *d, Die *e) {
    e->settled = 1;
    e->st = 0;
    float x = e->x, y = e->y - d->R;
    if (e->kind == D20 && e->value == 20) {
        e->react = CRIT;
        d->hasCrit = 1;
        burst(d, x, y, 34, 0.55f);
        for (int i = 0; i < 36; i++)
            add_part(d, x + frand(-1, 1) * d->R, y, frand(-1, 1) * d->H * 0.3f, -d->H * frand(0.5f, 0.9f),
                     P_CONFETTI, CONFETTI[irand(6)], frand(1.6f, 2.4f));
        add_pop(d, "CRIT!", x, y - d->R * 1.6f, 2, 0xFFD84A, 2.6f);
    } else if (e->kind == D20 && e->value == 1) {
        e->react = FAIL;
        add_pop(d, "WOMP", x, y - d->R * 2.35f, 1, WOMP_COL, 2.2f);
    } else {
        e->react = CALM;
    }
}

static void board_target(const Dice *d, float *x, float *y) {
    *x = d->bx + d->bw * 0.5f;
    *y = d->by + d->bh * 0.62f;
}

static void update_die(Dice *d, Die *e, float T, float dt) {
    int R = d->R;
    if (T < e->t0) return;
    float u = clampf((T - e->t0) / e->dur, 0, 1);
    float prev = e->u;
    e->u = u;

    float g = 1 - powf(1 - u, 2.2f);
    e->x = lerp(e->sx, e->tx, g);
    e->y = lerp(e->sy, e->ty, g);

    if (u < BOUNCE[0]) {
        float s = u / BOUNCE[0];
        e->z = e->z0 * (1 - s * s);
    } else {
        float hh = e->z0 * 0.32f;
        e->z = 0;
        for (int k = 0; k < NB - 1; k++, hh *= 0.4f)
            if (u < BOUNCE[k + 1]) {
                float s = (u - BOUNCE[k]) / (BOUNCE[k + 1] - BOUNCE[k]);
                e->z = 4 * hh * s * (1 - s);
                break;
            }
    }

    for (int k = 0; k < NB; k++)
        if (prev < BOUNCE[k] && u >= BOUNCE[k]) {
            e->impact = 1 - k * 0.2f;
            e->impactT = T;
            for (int i = 0; i < 4 - k; i++)
                add_part(d, e->x + frand(-1, 1) * R * 0.8f, e->y + R * 0.7f, frand(-1, 1) * d->H * 0.12f,
                         -frand(0.02f, 0.12f) * d->H, P_DUST, 0x8FCB9E, frand(0.3f, 0.6f));
            e->tumble_axis = v_norm(v_add(e->tumble_axis, vec3(frand(-0.4f, 0.4f), frand(-0.4f, 0.4f), frand(-0.4f, 0.4f))));
        }

    /* 3D rotation update */
    if (u < 0.76f) {
        float spd = e->tumble_speed * (1.0f - u * 0.7f);
        Quat dq = quat_axis_angle(e->tumble_axis, spd * dt);
        e->q_tumble = quat_norm(quat_mul(dq, e->q_tumble));
        e->q_snap = e->q_tumble;
        e->orient = quat_to_mat3(e->q_tumble);
    } else if (u < 1.0f) {
        float blend = ease((u - 0.76f) / 0.24f);
        Quat q = quat_slerp(e->q_snap, e->q_target, blend);
        e->orient = quat_to_mat3(q);
    } else {
        e->orient = quat_to_mat3(e->q_target);
    }

    e->tint = 0;
    if (u >= 1 && !e->settled) settle(d, e);
    if (!e->settled) return;

    e->st += dt;
    float st = e->st;

    if (e->react == CRIT) {
        for (int h = 0; h < 2; h++) {
            float s = (st - 0.05f - h * 0.42f) / 0.38f;
            if (s > 0 && s < 1) {
                e->z = R * (h ? 0.35f : 0.60f) * 4 * s * (1 - s);
                Quat hop_tilt = quat_axis_angle(vec3(0, 1, 0), 0.15f * sinf(s * 2 * PI_F));
                e->orient = quat_to_mat3(quat_mul(hop_tilt, e->q_target));
            }
        }
        e->tintCol = 0xFFFFFF;
        e->tint = 0.7f * expf(-st * 5) + 0.12f * (0.5f + 0.5f * sinf(st * 5));
    } else if (e->react == FAIL) {
        float dr = ease(st / 0.6f) * (1 - ease((st - 2.6f) / 0.6f));
        e->tintCol = 0x8A929E;
        e->tint = 0.5f * dr;
        e->x += sinf(st * 46) * R * 0.14f * clampf(1 - st / 0.8f, 0, 1);
        if (st > 0.5f && st < 2.6f && irand(100) < 45)
            add_part(d, e->x + frand(-0.6f, 0.6f) * R, e->y - R * 1.9f, 0, d->H * 0.5f, P_RAIN, 0x9DB8D8, (R * 0.85f) / (d->H * 0.5f));
    }
}

static void step(void *p, float dt) {
    Dice *d = p;
    if (dt > 0.1f) dt = 0.1f;
    d->t += dt;
    d->stateT += dt;

    switch (d->state) {
    case ROLL: {
        int allSettled = 1;
        for (int i = 0; i < d->n; i++) {
            update_die(d, &d->die[i], d->stateT, dt);
            if (!d->die[i].settled) allSettled = 0;
        }
        /* Pacing: once all dice have settled on the felt, pause so a 6-year-old
           can look at the numbers and frontrun the calculation before addition starts */
        if (allSettled) {
            d->state = TALLY_WAIT;
            d->stateT = 0;
            d->tallyWait = (d->n > 1) ? 2.0f : 1.2f;
            d->nextTally = 0;
        }
        break;
    }
    case TALLY_WAIT:
        for (int i = 0; i < d->n; i++) update_die(d, &d->die[i], 1e6f, dt);
        if (d->stateT >= d->tallyWait) {
            if (d->nextTally < d->n) {
                int idx = d->nextTally;
                d->die[idx].tallied = 1;
                d->fly[idx] = (Fly){ d->die[idx].x, d->die[idx].y - d->R, 0, d->die[idx].value, 1 };
                d->state = TALLY_FLY;
                d->stateT = 0;
            } else {
                d->state = SUM_HOLD;
                d->stateT = 0;
                d->hold = d->hasCrit ? 4.0f : 3.2f;
                float x, y;
                board_target(d, &x, &y);
                for (int i = 0; i < 8; i++)
                    add_part(d, x + frand(-1, 1) * d->bw * 0.3f, y, frand(-1, 1) * d->H * 0.1f, -d->H * frand(0.05f, 0.2f),
                             P_SPARK, 0xFFFFFF, frand(0.4f, 0.8f));
            }
        }
        break;
    case TALLY_FLY:
        for (int i = 0; i < d->n; i++) update_die(d, &d->die[i], 1e6f, dt);
        /* Handled in fly animation loop below */
        break;
    case SUM_HOLD:
        for (int i = 0; i < d->n; i++) update_die(d, &d->die[i], 1e6f, dt);
        if (d->stateT > d->hold) { d->state = EXIT; d->stateT = 0; }
        break;
    case EXIT: {
        float dir = d->fromLeft ? 1.0f : -1.0f;
        for (int i = 0; i < d->n; i++) {
            Die *e = &d->die[i];
            int order = d->fromLeft ? d->n - 1 - i : i;
            float k = clampf((d->stateT - order * 0.07f) / 0.6f, 0, 1);
            e->st += dt;
            e->x = e->tx + dir * k * k * (d->W + d->R * 4.0f);
            e->y = e->ty;
            e->z = d->R * 0.5f * fabsf(sinf(k * PI_F * 2)) * (1 - k);
            Quat roll_q = quat_axis_angle(vec3(0, 0, 1), dir * 2.2f * k * k);
            e->orient = quat_to_mat3(quat_mul(roll_q, e->q_target));
            e->tint *= 1 - dt * 4;
        }
        if (d->stateT > 0.6f + d->n * 0.07f) { d->state = GAP; d->stateT = 0; }
        break;
    }
    default:
        if (d->stateT > 0.3f) new_roll(d);
        break;
    }

    /* Tallies flying to chalkboard (leisurely pace for kids to track) */
    for (int i = 0; i < d->n; i++) {
        Fly *f = &d->fly[i];
        if (!f->on) continue;
        f->t += dt / 0.85f;
        if (f->t >= 1) {
            f->on = 0;
            d->total += f->value;
            d->die[i].counted = 1;
            if (d->n > 1) {
                d->expr[0] = 0;
                for (int j = 0; j < d->n; j++) {
                    if (!d->die[j].counted) continue;
                    char add[8];
                    snprintf(add, sizeof add, "%s%d", d->expr[0] ? "+" : "", d->die[j].value);
                    strncat(d->expr, add, sizeof d->expr - strlen(d->expr) - 1);
                }
            }
            float x, y;
            board_target(d, &x, &y);
            for (int k = 0; k < 6; k++)
                add_part(d, x + frand(-8, 8), y + frand(-4, 4), frand(-1, 1) * d->H * 0.08f, -frand(0, 0.08f) * d->H,
                         P_CHALK, 0xE8E6DC, frand(0.3f, 0.6f));

            /* Generous pause after each tally lands before starting the next */
            d->nextTally++;
            d->state = TALLY_WAIT;
            d->stateT = 0;
            d->tallyWait = (d->nextTally < d->n) ? 2.2f : 0.6f;
        }
    }

    d->tick += dt;
    while (d->shown < d->total && d->tick > 0.06f) { d->shown++; d->tick -= 0.06f; }
    if (d->shown >= d->total) d->tick = 0;

    for (int i = 0; i < POPS; i++) d->pop[i].t += dt;
    for (int i = 0; i < PARTS; i++) {
        Part *q = &d->parts[i];
        if (q->life <= 0) continue;
        q->life -= dt;
        q->x += q->vx * dt;
        q->y += q->vy * dt;
        switch (q->kind) {
        case P_SPARK: q->vx *= 1 - 2.5f * dt; q->vy = q->vy * (1 - 2.5f * dt) + d->H * 0.15f * dt; break;
        case P_CONFETTI:
            q->vx *= 1 - 1.8f * dt;
            q->vy = q->vy * (1 - 1.8f * dt) + d->H * 0.9f * dt;
            q->x += sinf(d->t * 7 + q->ph) * d->H * 0.08f * dt;
            break;
        case P_DUST: case P_CHALK: q->vx *= 1 - 4 * dt; q->vy *= 1 - 4 * dt; break;
        default: break;
        }
    }
}

/* ---------- drawing ---------- */

static void glow(Canvas *c, float cx, float cy, float rx, float ry, float a) {
    for (int y = (int)(cy - ry) - 1; y <= (int)(cy + ry) + 1; y++)
        for (int x = (int)(cx - rx) - 1; x <= (int)(cx + rx) + 1; x++) {
            float dx = (x + 0.5f - cx) / rx, dy = (y + 0.5f - cy) / ry, q = dx * dx + dy * dy;
            if (q > 1) continue;
            if (q > 0.62f && (x + y) % 2) continue;
            if (q > 0.85f && (x % 2 || y % 2)) continue;
            put(c, x, y, rgba(q < 0.4f ? 0xFFF0A0 : 0xFFD95A, (int)((q < 0.4f ? 245 : 230) * a)));
        }
}

static void cloud(Canvas *c, float x, float y, float s) {
    if (s <= 0.05f) return;
    fill_ellipse(c, x - 6 * s, y + 1 * s, 5 * s, 3.5f * s, rgb(0x8A929E));
    fill_ellipse(c, x + 6 * s, y + 1 * s, 5 * s, 3.5f * s, rgb(0x8A929E));
    fill_ellipse(c, x, y - 1 * s, 6.5f * s, 5 * s, rgb(0x9EA6B2));
    fill_ellipse(c, x - 5 * s, y, 4.5f * s, 3.5f * s, rgb(0xA8B0BC));
    fill_ellipse(c, x + 5 * s, y, 4.5f * s, 3.5f * s, rgb(0xA8B0BC));
    fill_ellipse(c, x - 1 * s, y - 3 * s, 4 * s, 2.5f * s, rgb(0xC4CAD4));
    if (s > 0.7f) {
        put(c, (int)(x - 2 * s), (int)(y), rgb(0x4A505A));
        put(c, (int)(x + 2 * s), (int)(y), rgb(0x4A505A));
        put(c, (int)x - 1, (int)(y + 2 * s), rgb(0x4A505A));
        put(c, (int)x, (int)(y + 2 * s) - 1, rgb(0x4A505A));
        put(c, (int)x + 1, (int)(y + 2 * s), rgb(0x4A505A));
    }
}

static void outline_top(Dice *d) {
    Canvas *c = &d->top;
    int W = c->w, H = c->h;
    uint32_t *px = c->px;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            int i = y * W + x;
            d->mark[i] = !px[i] && ((x > 0 && px[i - 1]) || (x < W - 1 && px[i + 1]) ||
                                    (y > 0 && px[i - W]) || (y < H - 1 && px[i + W]));
        }
    for (int i = 0; i < W * H; i++)
        if (d->mark[i]) px[i] = rgb(0x1A1410);
}

static void frect(SDL_Renderer *r, float x, float y, float w, float h, SDL_Color c) {
    SDL_FRect rc = { x, y, w, h };
    set_color(r, c);
    SDL_RenderFillRectF(r, &rc);
}

static SDL_Color col_a(uint32_t hex, float a) {
    SDL_Color c = hexcol(hex);
    c.a = (Uint8)(255 * clampf(a, 0, 1));
    return c;
}

static void draw(void *p, SDL_Renderer *r) {
    Dice *d = p;
    int S = d->S, R = d->R;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    set_color(r, hexcol(0x1E5A36));
    SDL_RenderFillRect(r, NULL);
    SDL_Rect full = { 0, 0, d->W * S, d->H * S };
    SDL_RenderCopy(r, d->felt, NULL, &full);

    Canvas *un = &d->under, *top = &d->top;
    memset(un->px, 0, sizeof(uint32_t) * un->w * un->h);
    memset(top->px, 0, sizeof(uint32_t) * top->w * top->h);
    int active = d->state != GAP;
    float fade = d->state == EXIT ? clampf(1 - d->stateT / 0.5f, 0, 1) : (d->state == GAP ? 0 : 1);

    /* Chalkboard text */
    {
        uint32_t chalk = 0xF2F0E6;
        int a = (int)(225 * fade);
        if (d->expr[0] && a > 0 && !d->small) {
            glyphs(un, d->expr, d->bx + (d->bw - glyphs_w(d->expr, 1)) / 2, d->by + 5, 1, rgba(chalk, a * 4 / 5));
        }
        if ((d->total > 0 || d->state == SUM_HOLD) && a > 0) {
            char s[8];
            snprintf(s, sizeof s, "%d", d->shown);
            int sc = d->small ? 1 : 2, w = glyphs_w(s, sc), x = d->bx + (d->bw - w) / 2, y = d->by + (d->small ? 4 : 15);
            uint32_t col = d->state == SUM_HOLD ? mixc(chalk, 0xFFE27A, 0.5f + 0.5f * sinf(d->stateT * 6)) : chalk;
            if (d->state == SUM_HOLD || d->state == EXIT) {
                int k = (int)((w + 8) * clampf(d->stateT * 3, 0, 1));
                if (d->state == EXIT) k = w + 8;
                if (!d->small) fill_rect(un, x - 4, y + 16, x - 4 + k, y + 16, rgba(chalk, a * 3 / 4));
                if (d->n > 1) glyphs(un, "=", x - (d->small ? 8 : 12), y + (d->small ? 0 : 3), 1, rgba(chalk, a));
            }
            glyphs(un, s, x, y, sc, rgba(col, a));
        }
    }

    if (active) {
        int order[MAXDICE];
        for (int i = 0; i < d->n; i++) order[i] = i;
        for (int i = 1; i < d->n; i++)
            for (int j = i; j > 0 && d->die[order[j]].y < d->die[order[j - 1]].y; j--) {
                int t = order[j]; order[j] = order[j - 1]; order[j - 1] = t;
            }

        for (int oi = 0; oi < d->n; oi++) {
            Die *e = &d->die[order[oi]];
            if (e->u < 0) continue;

            /* Natural 20 rays and gold glow */
            if (e->settled && e->react == CRIT && d->state != EXIT) {
                float st = e->st, a = clampf(st * 3, 0, 1);
                if (a > 0.02f) {
                    float gx = e->x, gy = e->y;
                    for (int k = 0; k < 10; k++) {
                        float an = d->t * 0.8f + k * PI_F / 5, an2 = an + 0.13f, L = R * (2.4f + 0.3f * sinf(d->t * 3 + k));
                        float xs[3] = { gx, gx + cosf(an) * L, gx + cosf(an2) * L };
                        float ys[3] = { gy, gy + sinf(an) * L * 0.8f, gy + sinf(an2) * L * 0.8f };
                        fill_poly(un, xs, ys, 3, rgba(k % 2 ? 0xFFD84A : 0xFFF0A0, (int)(215 * a)));
                    }
                    float gr = R * (1.45f + 0.08f * sinf(d->t * 5));
                    glow(un, gx, gy, gr * 1.2f, gr, a);
                }
            }

            /* Felt shadow */
            float zf = clampf(1 - e->z / (d->H * 0.5f), 0.3f, 1);
            fill_ellipse(un, e->x + 2 + e->z * 0.15f, e->y + R * 0.42f + e->z * 0.1f, R * 0.98f * zf, R * 0.7f * zf,
                         rgba(0x06180C, (int)(110 * zf)));

            /* Render the true 3D die */
            draw_3d_die(d, e, e->x, e->y);

            /* Chalk label on felt */
            if (e->settled && d->state != EXIT && !d->small && d->rows == 1) {
                const char *nm = NAMES[e->kind];
                int a = (int)(140 * clampf(e->st * 3, 0, 1));
                glyphs(un, nm, (int)(e->tx - glyphs_w(nm, 1) / 2.0f + 0.5f), (int)(e->ty + R * 1.25f + 2), 1, rgba(0xBFE6C8, a));
            }

            /* Natural 1 raincloud */
            if (e->settled && e->react == FAIL && d->state != EXIT) {
                float st = e->st, s = ease((st - 0.2f) / 0.35f) * (1 - ease((st - 2.7f) / 0.4f)) * 1.25f * (R / 14.0f);
                cloud(top, e->x, e->y - R * 2.1f + sinf(d->t * 2.5f) * 1.2f, s);
            }
        }
    }

    /* Flying tallies to chalkboard */
    for (int i = 0; i < d->n; i++) {
        Fly *f = &d->fly[i];
        if (!f->on) continue;
        float tx, ty, k = ease(f->t);
        board_target(d, &tx, &ty);
        float x = lerp(f->x0, tx, k), y = lerp(f->y0, ty, k) - sinf(PI_F * f->t) * d->H * 0.16f;
        char s[8];
        snprintf(s, sizeof s, "+%d", f->value);
        int sc = f->t < 0.75f ? 2 : 1;
        glyphs(top, s, (int)(x - glyphs_w(s, sc) / 2.0f), (int)(y - FONT_H * sc / 2.0f), sc, rgb(0xFFFFFF));
    }

    /* CRIT! and WOMP popups */
    for (int i = 0; i < POPS; i++) {
        Pop *q = &d->pop[i];
        if (q->t >= q->max) continue;
        float k = q->t / q->max;
        if (d->state == EXIT || d->state == GAP) continue;
        float y = q->y - d->R * 0.9f - (q->col == WOMP_COL ? -k * 3 : k * 8);
        float in = clampf(q->t / 0.15f, 0, 1);
        if (k > 0.85f && (int)(q->t * 16) % 2) continue;
        int sc = q->sc, w = glyphs_w(q->txt, sc);
        float bob = (1 - in) * 6 + (q->sc == 2 ? sinf(q->t * 9) * 1.2f : 0);
        if (sc == 2) glyphs(top, q->txt, (int)(q->x - w / 2.0f), (int)(y + bob) + 1, sc, rgb(0xC08A20));
        glyphs(top, q->txt, (int)(q->x - w / 2.0f), (int)(y + bob), sc, rgb(q->col));
    }
    outline_top(d);

    SDL_UpdateTexture(d->underTex, NULL, un->px, un->w * 4);
    SDL_UpdateTexture(d->topTex, NULL, top->px, top->w * 4);
    SDL_RenderCopy(r, d->underTex, NULL, &full);
    SDL_RenderCopy(r, d->topTex, NULL, &full);

    for (int i = 0; i < PARTS; i++) {
        Part *q = &d->parts[i];
        if (q->life <= 0) continue;
        float f = q->life / q->max, x = floorf(q->x) * S, y = floorf(q->y) * S;
        switch (q->kind) {
        case P_SPARK: {
            SDL_Color c = col_a(q->col, f < 0.3f ? f / 0.3f : 1);
            frect(r, x, y, (float)S, (float)S, c);
            if (f > 0.45f) {
                frect(r, x - S, y, (float)S, (float)S, c); frect(r, x + S, y, (float)S, (float)S, c);
                frect(r, x, y - S, (float)S, (float)S, c); frect(r, x, y + S, (float)S, (float)S, c);
            }
            break;
        }
        case P_CONFETTI: {
            int flip = (int)(d->t * 10 + q->ph * 3) % 3;
            frect(r, x, y, (float)S * (flip ? 2 : 1), (float)S * (flip == 1 ? 1 : 2), col_a(q->col, f < 0.25f ? f / 0.25f : 1));
            break;
        }
        case P_RAIN: frect(r, x, y, (float)S, 2.0f * S, col_a(q->col, 0.85f)); break;
        default: frect(r, x, y, (float)S, (float)S, col_a(q->col, f * 0.8f)); break;
        }
    }
}

/* ---------- life cycle ---------- */

static void *create(SDL_Renderer *r, int w, int h) {
    Dice *d = calloc(1, sizeof *d);
    d->w = w;
    d->h = h;
    d->S = (int)(h / 180.0f + 0.5f);
    if (d->S < 1) d->S = 1;
    d->W = (w + d->S - 1) / d->S;
    d->H = (h + d->S - 1) / d->S;
    d->small = d->H < 130;
    d->R = (int)(d->H * (d->small ? 0.15f : 0.115f) + 0.5f);
    if (d->R < 8) d->R = 8;
    d->F = d->H * 55 / 1000 > 5 ? d->H * 55 / 1000 : 5;
    d->bw = d->W - 2 * d->F - 16 < 116 ? d->W - 2 * d->F - 16 : 116;
    d->bh = 38;
    if (d->small) { d->bw = 40; d->bh = 15; }
    d->bx = (d->W - d->bw) / 2;
    d->by = d->F / 2 - 1 > 1 ? d->F / 2 - 1 : 1;

    /* Build the 3D meshes for all 6 dice kinds */
    d->mesh[D4]  = build_d4();
    d->mesh[D6]  = build_d6();
    d->mesh[D8]  = build_d8();
    d->mesh[D10] = build_d10();
    d->mesh[D12] = build_d12();
    d->mesh[D20] = build_d20();

    Canvas tray = canvas_new(d->W, d->H);
    build_tray(d, &tray);
    d->felt = canvas_texture(r, &tray);
    d->under = canvas_new(d->W, d->H);
    d->top = canvas_new(d->W, d->H);
    d->mark = calloc((size_t)d->W * d->H, 1);
    d->underTex = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, d->W, d->H);
    d->topTex = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, d->W, d->H);
    SDL_Texture *ts[2] = { d->underTex, d->topTex };
    for (int i = 0; i < 2; i++) {
        SDL_SetTextureBlendMode(ts[i], SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(ts[i], SDL_ScaleModeNearest);
    }
    new_roll(d);
    return d;
}

static void destroy(void *p) {
    Dice *d = p;
    SDL_DestroyTexture(d->felt);
    SDL_DestroyTexture(d->underTex);
    SDL_DestroyTexture(d->topTex);
    free(d->under.px);
    free(d->top.px);
    free(d->mark);
    free(d);
}

const Saver saver_dice = { "dice", "DICE", create, step, draw, destroy };
