/* Checks the DICE saver's 3D dice without opening a window: every mesh is closed with
   flat faces, faces are numbered like real dice, and a die at rest shows exactly the
   number that gets tallied, facing the viewer and the right way up.
   Run with `make check`. */
#include "../src/savers/dice.c"
#include <stdio.h>

/* engine.c refers to the saver registry, which lives in main.c. */
const Saver *const SAVERS[1] = { 0 };
const int SAVER_COUNT = 0;

static int bad;

static void fail(const char *kind, const char *what, int a, int b) {
    printf("%s: %s (%d, %d)\n", kind, what, a, b);
    bad++;
}

/* Closed: every edge is walked once each way, by the two faces that share it. */
static void check_closed(const PolyMesh *m, const char *nm) {
    for (int f = 0; f < m->nfaces; f++)
        for (int i = 0; i < m->faces[f].nverts; i++) {
            int a = m->faces[f].idx[i], b = m->faces[f].idx[(i + 1) % m->faces[f].nverts], fwd = 0, rev = 0;
            for (int g = 0; g < m->nfaces; g++)
                for (int j = 0; j < m->faces[g].nverts; j++) {
                    int c = m->faces[g].idx[j], e = m->faces[g].idx[(j + 1) % m->faces[g].nverts];
                    fwd += c == a && e == b;
                    rev += c == b && e == a;
                }
            if (fwd != 1 || rev != 1) fail(nm, "edge not shared by exactly two faces", a, b);
        }
}

static void check_flat(const PolyMesh *m, const char *nm) {
    for (int f = 0; f < m->nfaces; f++)
        for (int i = 1; i < m->faces[f].nverts; i++) {
            Vec3 v = v_sub(m->verts[m->faces[f].idx[i]], m->verts[m->faces[f].idx[0]]);
            if (fabsf(v_dot(v, m->faces[f].normal)) > 1e-3f) fail(nm, "face not flat", f, i);
        }
}

/* Each value once, and opposite faces add up to sides + 1 (7 on a d6, 21 on a d20). */
static void check_values(const PolyMesh *m, const char *nm, int sides) {
    int seen[21] = { 0 };
    for (int f = 0; f < m->nfaces; f++) {
        int v = m->faces[f].value;
        if (v < 1 || v > sides) fail(nm, "value out of range", f, v);
        else seen[v]++;
        for (int g = 0; g < m->nfaces; g++)
            if (v_dot(m->faces[f].normal, m->faces[g].normal) < -0.999f && v + m->faces[g].value != sides + 1)
                fail(nm, "opposite faces don't add up", v, m->faces[g].value);
    }
    for (int v = 1; v <= sides; v++)
        if (seen[v] != 1) fail(nm, "value not on exactly one face", v, seen[v]);
}

/* At rest the face (d4: corner) pointing most along REST is the roll; it faces the viewer
   and its number reads left to right, the right way up. Rest poses are partly random, so
   try each roll a few times. */
static void check_rest(const PolyMesh *m, int kind) {
    const char *nm = NAMES[kind];
    for (int v = 1; v <= SIDES[kind]; v++)
        for (int t = 0; t < 20; t++) {
            Die e = { 0 };
            e.kind = kind;
            e.orient = quat_to_mat3(compute_target_orientation(m, kind, v));
            Vec3 up = rest_dir();
            int topf = 0, topv = 0;
            float bf = -2, bv = -2;
            for (int f = 0; f < m->nfaces; f++) {
                float q = v_dot(mat3_mul_v(e.orient, m->faces[f].normal), up);
                if (q > bf) { bf = q; topf = f; }
            }
            for (int i = 0; i < m->nverts; i++) {
                float q = v_dot(mat3_mul_v(e.orient, v_norm(m->verts[i])), up);
                if (q > bv) { bv = q; topv = i; }
            }
            if (kind == D4) {
                if (topv + 1 != v) fail(nm, "rolled corner not on top (roll, shown)", v, topv + 1);
                continue;
            }
            const PolyFace *fc = &m->faces[topf];
            if (fc->value != v) fail(nm, "rolled face not on top (roll, shown)", v, fc->value);
            if (v_dot(mat3_mul_v(e.orient, fc->normal), v_norm(vec3(0, 0.85f, 1))) < 0.5f)
                fail(nm, "rolled face turned away from the viewer", v, t);
            float rx, ry, dx, dy;
            to_screen(&e, 1, fc->right, &rx, &ry);
            to_screen(&e, 1, v_scale(fc->up, -1), &dx, &dy);
            if (rx < 0.7f * fabsf(ry) || dy < 0.7f * fabsf(dx)) fail(nm, "number not upright", v, t);
        }
}

int main(void) {
    static PolyMesh (*const build[KINDS])(void) = { build_d4, build_d6, build_d8, build_d10, build_d12, build_d20 };
    for (int k = 0; k < KINDS; k++) {
        PolyMesh m = build[k]();
        check_closed(&m, NAMES[k]);
        check_flat(&m, NAMES[k]);
        if (k != D4) check_values(&m, NAMES[k], SIDES[k]);   /* a d4 is read at its corners */
        check_rest(&m, k);
    }
    if (bad) printf("FAIL: %d dice problems\n", bad);
    else printf("all dice OK\n");
    return bad != 0;
}
