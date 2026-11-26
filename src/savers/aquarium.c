/* Aquarium: pixel-art fish, a passing shark or sea turtle, drifting jellyfish, a crab on
   the sand, swaying seaweed, coral and a bubbling treasure chest. */
#include "../engine.h"
#include <math.h>
#include <stdlib.h>

enum { CLOWN, TANG, PUFFER, ANGEL, SMALL_SPECIES, SHARK = SMALL_SPECIES, TURTLE, SPECIES };
#define MAX_FISH 16
#define MAX_BUBBLES 140
#define WEEDS 7
#define PEBBLES 70
#define JELLIES 2
#define SNOW 40
#define MAX_DIVERS 2
#define MAX_KRILL 56

static uint32_t rgba(uint32_t hex, uint32_t a) { return (hex << 8) | a; }

/* Paints `col` over pixels of `under` inside a rectangle (used for stripes and bellies). */
static void recolor(Canvas *c, int x0, int y0, int x1, int y1, uint32_t under, uint32_t col) {
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            if (get(c, x, y) == under) put(c, x, y, col);
}

static uint32_t tint(uint32_t p, float k) {   /* k < 1 darkens, > 1 lightens toward white */
    int ch[3];
    for (int i = 0; i < 3; i++) {
        float v = (float)((p >> (24 - 8 * i)) & 0xFF);
        v = k < 1 ? v * k : v + (255 - v) * (k - 1);
        ch[i] = v > 255 ? 255 : (int)v;
    }
    return (uint32_t)ch[0] << 24 | (uint32_t)ch[1] << 16 | (uint32_t)ch[2] << 8 | (p & 0xFF);
}

/* Rounds a sprite: per column, the top rows go a shade darker and the bottom rows lighter. */
static void shade(Canvas *c) {
    for (int x = 0; x < c->w; x++) {
        int top = -1, bot = -1;
        for (int y = 0; y < c->h; y++)
            if (get(c, x, y)) { if (top < 0) top = y; bot = y; }
        if (top < 0 || bot - top < 4) continue;
        int band = (bot - top + 1) / 4;
        for (int y = top; y <= bot; y++) {
            uint32_t p = get(c, x, y);
            if (!p) continue;
            if (y < top + band) put(c, x, y, tint(p, 0.82f));
            else if (y > bot - band) put(c, x, y, tint(p, 1.18f));
        }
    }
}

/* A round-ended stroke, for coral branches. */
static void stroke(Canvas *c, float x0, float y0, float x1, float y1, float r, uint32_t col) {
    for (int i = 0; i <= 12; i++) {
        float t = i / 12.0f;
        fill_ellipse(c, x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, r, r, col);
    }
}

static void eye(Canvas *c, int x, int y) {
    put(c, x, y, rgb(0x111111)); put(c, x + 1, y, rgb(0x111111));
    put(c, x, y + 1, rgb(0x111111)); put(c, x + 1, y + 1, rgb(0x111111));
    put(c, x, y, rgb(0xFFFFFF));
}

static void tail(Canvas *c, float x0, float cy, int frame, float spread, uint32_t col) {
    float wig = frame ? -1.5f : 1.5f;
    float xs[3] = { x0, 0, 0 }, ys[3] = { cy, cy - spread + wig, cy + spread + wig };
    fill_poly(c, xs, ys, 3, col);
}

static Canvas make_fish(int species, int frame) {
    Canvas c;
    if (species == CLOWN) {
        c = canvas_new(22, 13);
        uint32_t o = rgb(0xFF8A1F);
        tail(&c, 4.5f, 6.5f, frame, 4.5f, rgb(0xF06A00));
        fill_ellipse(&c, 12, 6.5f, 8.5f, 5, o);
        fill_poly(&c, (float[]){ 9, 12, 14 }, (float[]){ 2, 0, 2.5f }, 3, rgb(0xF06A00));
        recolor(&c, 8, 0, 9, 12, o, rgb(0xFFFFFF));
        recolor(&c, 14, 0, 15, 12, o, rgb(0xFFFFFF));
        recolor(&c, 7, 0, 7, 12, o, rgb(0x2A1200));
        recolor(&c, 16, 0, 16, 12, o, rgb(0x2A1200));
        shade(&c);
        eye(&c, 17, 4);
        outline(&c, rgb(0x3A1A00));
    } else if (species == TANG) {
        c = canvas_new(24, 14);
        uint32_t b = rgb(0x2563EB);
        tail(&c, 4.5f, 7, frame, 5, rgb(0xFFD23F));
        fill_ellipse(&c, 13, 7, 9, 5.5f, b);
        fill_ellipse(&c, 13, 6, 6, 2.4f, rgb(0x0B1A40));
        fill_ellipse(&c, 14, 6.4f, 4, 1.2f, b);
        fill_poly(&c, (float[]){ 10, 15, 13 }, (float[]){ 12, 12, 14 }, 3, rgb(0xFFD23F));
        shade(&c);
        eye(&c, 18, 4);
        outline(&c, rgb(0x0A1430));
    } else if (species == PUFFER) {
        c = canvas_new(19, 16);
        uint32_t g = rgb(0xC8E05A);
        tail(&c, 3.5f, 8, frame, 3.5f, rgb(0xA7C23E));
        fill_ellipse(&c, 10, 8, 7.5f, 6.8f, g);
        recolor(&c, 0, 9, 18, 15, g, rgb(0xF2F7C8));
        static const int spots[][2] = { {7,4}, {11,3}, {6,7}, {10,6}, {13,9}, {8,9} };
        for (int i = 0; i < 6; i++)
            if (get(&c, spots[i][0], spots[i][1]) == g) put(&c, spots[i][0], spots[i][1], rgb(0x6E8A23));
        fill_poly(&c, (float[]){ 8, 11, 9 }, (float[]){ 10, 10, 13 }, 3, rgb(0xA7C23E));
        shade(&c);
        fill_rect(&c, 13, 5, 15, 7, rgb(0xFFFFFF));
        fill_rect(&c, 14, 6, 15, 7, rgb(0x111111));
        put(&c, 17, 10, rgb(0x6E3F18));
        outline(&c, rgb(0x2F3A0E));
    } else if (species == ANGEL) {               /* queen angelfish: tall, with long fins */
        c = canvas_new(21, 24);
        uint32_t y = rgb(0xFFD447), fin = rgb(0x2F7BE6);
        tail(&c, 5.5f, 12, frame, 3.5f, rgb(0xFFB020));
        fill_poly(&c, (float[]){ 15, 2, 6 }, (float[]){ 5, 0, 9 }, 3, fin);
        fill_poly(&c, (float[]){ 15, 2, 6 }, (float[]){ 19, 24, 15 }, 3, fin);
        fill_ellipse(&c, 12.5f, 12, 7, 7.5f, y);
        recolor(&c, 9, 0, 9, 23, y, rgb(0x3A7BD5));
        recolor(&c, 14, 0, 14, 23, y, rgb(0x3A7BD5));
        shade(&c);
        eye(&c, 16, 9);
        put(&c, 19, 13, rgb(0x6A3A00));
        outline(&c, rgb(0x3A2400));
    } else if (species == SHARK) {
        c = canvas_new(60, 26);
        uint32_t g = rgb(0x7D8C9C), fin = rgb(0x6A7988);
        float w = frame ? 1.5f : -1.5f;
        fill_poly(&c, (float[]){ 13, 2, 7 }, (float[]){ 11, 0 + w, 12 }, 3, fin);   /* tail lobes */
        fill_poly(&c, (float[]){ 13, 4, 8 }, (float[]){ 14, 23 + w, 13 }, 3, fin);
        fill_poly(&c, (float[]){ 9, 22, 22, 9 }, (float[]){ 11, 8, 18, 15 }, 4, g);  /* tail stalk */
        fill_ellipse(&c, 33, 13, 21, 7, g);
        fill_poly(&c, (float[]){ 46, 59, 57, 46 }, (float[]){ 7.5f, 12, 16, 18.5f }, 4, g);  /* snout */
        fill_poly(&c, (float[]){ 37, 29, 26 }, (float[]){ 7, 0, 7 }, 3, fin);        /* dorsal fin */
        fill_poly(&c, (float[]){ 18, 15, 14 }, (float[]){ 9, 6, 10 }, 3, fin);
        fill_poly(&c, (float[]){ 42, 31, 36 }, (float[]){ 17, 25, 18 }, 3, fin);     /* pectoral */
        recolor(&c, 10, 15, 59, 25, g, rgb(0xE4EAEF));                               /* white belly */
        shade(&c);
        for (int x = 41; x <= 45; x += 2) recolor(&c, x, 11, x, 15, g, rgb(0x55626F));
        fill_rect(&c, 49, 10, 50, 11, rgb(0x111111));
        put(&c, 49, 10, rgb(0xFFFFFF));
        for (int x = 51; x <= 55; x++) put(&c, x, 16 - (x == 51 || x == 55), rgb(0x3C4650));
        outline(&c, rgb(0x1E2A36));
    } else {                                     /* sea turtle */
        c = canvas_new(38, 24);
        uint32_t skin = rgb(0x9CC069);
        if (frame) fill_poly(&c, (float[]){ 25, 20, 11 }, (float[]){ 11, 8, 1 }, 3, skin);
        else fill_poly(&c, (float[]){ 25, 20, 12 }, (float[]){ 14, 16, 23 }, 3, skin);
        fill_ellipse(&c, 5, 15, 4, 2.2f, skin);
        fill_ellipse(&c, 31, 11, 5.5f, 3.6f, skin);
        fill_ellipse(&c, 17, 11, 12, 7.5f, rgb(0x6B8E3A));
        static const float plates[][2] = { {12,8}, {18,7}, {24,9}, {14,13}, {21,13} };
        for (int i = 0; i < 5; i++) fill_ellipse(&c, plates[i][0], plates[i][1], 2.6f, 2.0f, rgb(0x4E6B27));
        recolor(&c, 4, 15, 30, 19, rgb(0x6B8E3A), rgb(0xD8C77A));                    /* shell rim */
        shade(&c);
        eye(&c, 32, 9);
        put(&c, 35, 13, rgb(0x3D5220));
        outline(&c, rgb(0x223313));
    }
    return c;
}

static Canvas make_jelly(int frame) {
    Canvas c = canvas_new(15, 24);
    uint32_t bell = rgba(0xF6A6DA, 0xD8), rim = rgba(0xC2589E, 0xE0);
    float rx = frame ? 5.5f : 7, ry = frame ? 6.5f : 5, by = frame ? 7 : 5;
    fill_ellipse(&c, 7.5f, by, rx, ry, bell);
    for (int y = (int)by + 1; y < 24; y++)
        for (int x = 0; x < 15; x++) put(&c, x, y, 0);
    fill_ellipse(&c, 7.5f, by - ry * 0.4f, rx * 0.45f, ry * 0.3f, rgba(0xFFE0F4, 0xE8));
    outline(&c, rim);
    int top = (int)by + 2;
    for (int k = 0; k < 4; k++) {
        float x0 = frame ? 4.5f + k * 2 : 2.5f + k * 3.3f;
        for (int y = top; y < 24 - (k % 2) * 3; y++)
            put(&c, (int)(x0 + sinf(y * 0.6f + k + frame * 2) * (frame ? 0.6f : 1.2f)), y, rgba(0xF3B6E4, 0xA0));
    }
    return c;
}

static Canvas make_crab(int frame) {
    Canvas c = canvas_new(22, 13);
    uint32_t red = rgb(0xE5482E), dark = rgb(0x8B1E10);
    for (int i = 0; i < 3; i++) {                       /* legs, scuttling between two poses */
        int off = (i + frame) % 2;
        put(&c, 6 - i, 10 + off, dark); put(&c, 5 - i, 11 + off, dark);
        put(&c, 15 + i, 10 + off, dark); put(&c, 16 + i, 11 + off, dark);
    }
    fill_ellipse(&c, 11, 8, 7, 3.6f, red);
    fill_ellipse(&c, 2.6f, 4.5f, 2.4f, 2.2f, red);       /* claws */
    fill_ellipse(&c, 19.4f, 4.5f, 2.4f, 2.2f, red);
    put(&c, 2, 2, 0); put(&c, 19, 2, 0);
    put(&c, 4, 6, red); put(&c, 5, 7, red); put(&c, 17, 6, red); put(&c, 16, 7, red);
    put(&c, 8, 3, dark); put(&c, 8, 4, dark); put(&c, 13, 3, dark); put(&c, 13, 4, dark);
    shade(&c);
    put(&c, 8, 2, rgb(0x111111)); put(&c, 13, 2, rgb(0x111111));
    outline(&c, rgb(0x4A0E06));
    return c;
}

static Canvas make_starfish(void) {
    Canvas c = canvas_new(13, 13);
    float xs[10], ys[10];
    for (int i = 0; i < 10; i++) {
        float a = -PI_F / 2 + i * PI_F / 5, r = i % 2 ? 2.6f : 6.2f;
        xs[i] = 6.5f + cosf(a) * r;
        ys[i] = 7 + sinf(a) * r;
    }
    fill_poly(&c, xs, ys, 10, rgb(0xFF8A3D));
    shade(&c);
    static const int dots[][2] = { {6,3}, {3,6}, {9,6}, {5,9}, {8,9}, {6,6} };
    for (int i = 0; i < 6; i++) put(&c, dots[i][0], dots[i][1], rgb(0xFFD6A8));
    outline(&c, rgb(0x7A2E0A));
    return c;
}

static Canvas make_coral(void) {
    Canvas c = canvas_new(34, 30);
    uint32_t p = rgb(0xFF6F91);
    static const float br[][4] = {
        {17,29,17,18}, {17,21,9,11}, {9,11,6,3}, {9,11,13,4}, {17,18,25,9},
        {25,9,23,2}, {25,9,30,5}, {17,18,17,6}, {12,24,4,17},
    };
    for (int i = 0; i < 9; i++) stroke(&c, br[i][0], br[i][1], br[i][2], br[i][3], 1.7f, p);
    shade(&c);
    static const int tips[][2] = { {6,3}, {13,4}, {23,2}, {30,5}, {17,6}, {4,17} };
    for (int i = 0; i < 6; i++) put(&c, tips[i][0], tips[i][1] - 1, rgb(0xFFC2D1));
    outline(&c, rgb(0x7A1F3D));
    return c;
}

static Canvas make_brain_coral(void) {
    Canvas c = canvas_new(30, 15);
    uint32_t o = rgb(0xE9A24E);
    fill_ellipse(&c, 15, 15, 14, 14, o);
    for (int k = 0; k < 2; k++)
        for (int x = 0; x < 30; x++) {
            int y = 6 + k * 4 + (int)lroundf(sinf(x * 0.5f + k * 2.0f) * 1.2f);
            if (get(&c, x, y) == o) put(&c, x, y, rgb(0xC9853A));
        }
    shade(&c);
    outline(&c, rgb(0x5C3510));
    return c;
}

static Canvas make_bubble(void) {
    Canvas c = canvas_new(7, 7);
    fill_ellipse(&c, 3.5f, 3.5f, 3.5f, 3.5f, rgb(0xBDEFFF));
    fill_ellipse(&c, 3.5f, 3.5f, 2.4f, 2.4f, 0);
    put(&c, 2, 2, rgb(0xFFFFFF));
    return c;
}

static Canvas make_chest(void) {
    Canvas c = canvas_new(26, 18);
    fill_rect(&c, 2, 8, 23, 17, rgb(0x8B5A2B));
    fill_round_rect(&c, 2, 2, 23, 9, 3, rgb(0xA0692F));
    fill_rect(&c, 2, 8, 23, 8, rgb(0x5E3A17));
    fill_rect(&c, 6, 2, 7, 17, rgb(0xFFC93C));
    fill_rect(&c, 18, 2, 19, 17, rgb(0xFFC93C));
    fill_rect(&c, 12, 7, 13, 10, rgb(0xFFE07A));
    put(&c, 9, 5, rgb(0xFFE07A)); put(&c, 15, 13, rgb(0xFFE07A));
    outline(&c, rgb(0x2E1A08));
    return c;
}

/* Scuba diver facing right: wetsuit, air tank, mask, flippers. */
static Canvas make_diver(int frame) {
    Canvas c = canvas_new(34, 16);
    int kick = frame ? 2 : -2;
    /* Flippers */
    fill_poly(&c, (float[]){ 0, 6, 6 }, (float[]){ 6 + kick, 5, 8 }, 3, rgb(0xFFD23F));
    fill_poly(&c, (float[]){ 1, 7, 7 }, (float[]){ 11 - kick, 9, 12 }, 3, rgb(0xE0A81E));
    /* Legs & wetsuit body */
    fill_rect(&c, 6, 6, 12, 8, rgb(0x2A3244));
    fill_rect(&c, 7, 9, 12, 11, rgb(0x1E2534));
    fill_ellipse(&c, 17, 8, 6.5f, 3.8f, rgb(0x2A3244));
    fill_rect(&c, 14, 7, 20, 8, rgb(0xFF7A28)); /* wetsuit stripe */
    /* Air tank */
    fill_round_rect(&c, 12, 2, 21, 5, 1, rgb(0xF5B82E));
    fill_rect(&c, 21, 3, 22, 4, rgb(0x9098A8));
    /* Arm reaching forward */
    fill_rect(&c, 19, 10, 26, 11, rgb(0x2A3244));
    fill_rect(&c, 26, 10, 27, 11, rgb(0xF2C29B));
    /* Head & diving mask */
    fill_ellipse(&c, 26, 6.5f, 4, 3.6f, rgb(0xF2C29B));
    fill_ellipse(&c, 25, 5, 3.5f, 2.2f, rgb(0x2A3244)); /* hood */
    fill_rect(&c, 27, 5, 30, 7, rgb(0x7EE8FF));        /* mask glass */
    fill_rect(&c, 27, 4, 30, 4, rgb(0x181C24));        /* mask rim */
    fill_rect(&c, 29, 8, 31, 9, rgb(0x3A4254));        /* regulator */
    outline(&c, rgb(0x10141C));
    return c;
}

/* SpongeBob facing right: yellow porous sponge, big eyes, buck teeth, red tie, brown square pants. */
static Canvas make_spongebob(int frame) {
    Canvas c = canvas_new(22, 26);
    int step = frame ? 1 : -1;
    /* Little legs & shoes */
    fill_rect(&c, 7 + step, 20, 8 + step, 23, rgb(0xF7E33B));
    fill_rect(&c, 13 - step, 20, 14 - step, 23, rgb(0xF7E33B));
    fill_rect(&c, 6 + step, 23, 9 + step, 24, rgb(0x181818));
    fill_rect(&c, 13 - step, 23, 16 - step, 24, rgb(0x181818));
    /* Brown square pants & white shirt */
    fill_rect(&c, 5, 17, 16, 20, rgb(0x8B5A2B));
    fill_rect(&c, 6, 17, 7, 17, rgb(0x181818)); fill_rect(&c, 10, 17, 11, 17, rgb(0x181818)); fill_rect(&c, 14, 17, 15, 17, rgb(0x181818));
    fill_rect(&c, 5, 14, 16, 16, rgb(0xFFFFFF));
    /* Red tie */
    fill_rect(&c, 10, 15, 11, 18, rgb(0xE03030));
    /* Yellow sponge body */
    fill_round_rect(&c, 4, 2, 17, 14, 2, rgb(0xFCE83A));
    put(&c, 6, 4, rgb(0xD4B81C)); put(&c, 15, 5, rgb(0xD4B81C)); put(&c, 6, 11, rgb(0xD4B81C));
    /* Big blue eyes + rosy cheeks */
    fill_ellipse(&c, 8.5f, 7, 2.3f, 2.5f, rgb(0xFFFFFF));
    fill_ellipse(&c, 13.5f, 7, 2.3f, 2.5f, rgb(0xFFFFFF));
    put(&c, 8, 7, rgb(0x3898FF)); put(&c, 13, 7, rgb(0x3898FF));
    put(&c, 9, 7, rgb(0x111111)); put(&c, 14, 7, rgb(0x111111));
    put(&c, 6, 10, rgb(0xFF7A8A)); put(&c, 15, 10, rgb(0xFF7A8A));
    /* Smile & buck teeth */
    fill_rect(&c, 8, 11, 13, 11, rgb(0x3A1A08));
    put(&c, 9, 12, rgb(0xFFFFFF)); put(&c, 12, 12, rgb(0xFFFFFF));
    /* Arms */
    fill_rect(&c, 2, 12 - step, 4, 14 - step, rgb(0xFCE83A));
    fill_rect(&c, 17, 12 + step, 19, 14 + step, rgb(0xFCE83A));
    outline(&c, rgb(0x4A3A08));
    return c;
}

/* Colossal 224x60 pixel-art Blue Whale facing right (mouth_open = 0 or 1). */
static Canvas make_whale(int mouth_open) {
    Canvas c = canvas_new(224, 60);
    /* Tail fluke + tapered caudal peduncle */
    fill_poly(&c, (float[]){ 26, 4, 10, 28 }, (float[]){ 26, 8, 26, 30 }, 4, rgb(0x285B8E));
    fill_poly(&c, (float[]){ 26, 4, 10, 28 }, (float[]){ 30, 50, 31, 28 }, 4, rgb(0x1E4670));
    fill_poly(&c, (float[]){ 22, 60, 60, 22 }, (float[]){ 26, 18, 39, 32 }, 4, rgb(0x285B8E));
    /* Small characteristic dorsal fin set far back */
    fill_poly(&c, (float[]){ 74, 59, 56 }, (float[]){ 15, 4, 17 }, 3, rgb(0x245380));
    /* Streamlined gigantic body + subtle mottled skin patches */
    fill_ellipse(&c, 118, 28, 86, 18, rgb(0x2F6CA6));
    fill_ellipse(&c, 82, 22, 12, 4.5f, rgb(0x4482BA));
    fill_ellipse(&c, 116, 20, 15, 5.0f, rgb(0x4482BA));
    fill_ellipse(&c, 146, 24, 10, 3.8f, rgb(0x4482BA));
    /* Lighter pleated ventral throat grooves */
    fill_ellipse(&c, 134, 38, 66, 8.2f, rgb(0x7FB0D8));
    for (int x = 86; x <= 184; x += 5) fill_rect(&c, x, 35, x + 3, 36, rgb(0x4E86B8));
    /* Friendly mouth strictly inside snout outline */
    if (mouth_open) {
        /* Gentle rounded lower-jaw gulp: smooth navy interior, soft baleen rim, and cute tongue */
        fill_ellipse(&c, 178, 35, 22, 6.0f, rgb(0x1B385C));
        fill_ellipse(&c, 179, 42.0f, 22, 4.0f, rgb(0x7FB0D8));
        fill_ellipse(&c, 179, 37.5f, 11, 2.0f, rgb(0xC86E88));
        fill_ellipse(&c, 181, 30.0f, 19, 1.5f, rgb(0x9BBBD4));
        fill_ellipse(&c, 179, 28.5f, 23, 2.5f, rgb(0x2F6CA6));
    } else {
        fill_rect(&c, 162, 32, 200, 33, rgb(0x142438));
        put(&c, 161, 31, rgb(0x142438));
    }
    /* Small eye relative to colossal body + sweeping pectoral flipper */
    fill_rect(&c, 168, 23, 170, 25, rgb(0x111111));
    put(&c, 168, 23, rgb(0xFFFFFF));
    fill_poly(&c, (float[]){ 140, 114, 108, 128 }, (float[]){ 34, 54, 56, 38 }, 4, rgb(0x245380));
    shade(&c);
    outline(&c, rgb(0x0A1A30));
    return c;
}

typedef struct { float x, y, vx, phase, bubT; int frame; } Diver;
typedef struct { float x, y, vx, ph; int alive; } Krill;
typedef struct { int on, dir; float x, y, vx, wait, mouth; } BlueWhale;

typedef struct { int species, frame, far; float x, baseY, vx, bobAmp, bobF, phase, flick, scare, dodge; } Fish;
typedef struct { float x, y, vy, phase; int alive; } Bubble;
typedef struct { float x, y, vx, phase; } Jelly;

typedef struct {
    int w, h, sc, farSc, sandY, nfish, nfar;
    float t, chestTimer, sandTimer;
    SDL_Texture *fish[SPECIES][2], *jelly[2], *crab[2], *diver[2], *sponge[2], *whaleTex[2], *bubble, *chest, *star, *coral, *brain;
    int fw[SPECIES], fh[SPECIES];
    Fish f[MAX_FISH];
    Fish vis[2];                       /* the shark and the turtle, each now and then */
    int visOn[2];
    float visWait[2];
    Jelly j[JELLIES];
    Diver divers[MAX_DIVERS];
    Krill krill[MAX_KRILL];
    int krillDir;
    float krillWait;
    BlueWhale whale;
    int spongeOn;
    float spongeX, spongeVx, spongeStep, spongeWait;
    float crabX, crabVx, crabWait, crabStep;
    Bubble b[MAX_BUBBLES];
    float weedX[WEEDS], weedH[WEEDS], weedP[WEEDS];
    int pebble[PEBBLES][3];
    float snow[SNOW][3];
    SDL_Rect chestRect, starRect[2], coralRect, brainRect;
} Aquarium;

static int fish_scale(const Aquarium *a, const Fish *f) { return f->far ? a->farSc : a->sc; }
static float fish_y(const Aquarium *a, const Fish *f) {
    return f->baseY + sinf(a->t * f->bobF * 6.28f + f->phase) * f->bobAmp;
}

static void spawn(Aquarium *a, Fish *f, int species, float speed, int anywhere) {
    f->species = species;
    int right = irand(2);
    f->vx = right ? speed : -speed;
    float fw = (float)a->fw[species] * fish_scale(a, f);
    f->x = anywhere ? frand(0, (float)a->w) : (right ? -fw : (float)a->w);
    f->baseY = frand(0.08f, 0.85f) * a->sandY;
    f->bobAmp = frand(0.01f, 0.03f) * a->h;
    f->bobF = frand(0.3f, 0.8f);
    f->phase = frand(0, 6.28f);
    f->flick = frand(0, 1);
    f->scare = 0;
}

static void spawn_fish(Aquarium *a, Fish *f, int anywhere) {
    spawn(a, f, irand(SMALL_SPECIES), frand(0.06f, 0.16f) * a->w * (f->far ? 0.6f : 1), anywhere);
    if (f->far) f->baseY = frand(0.1f, 0.75f) * a->sandY;
}

static void spawn_visitor(Aquarium *a, int k) {
    Fish *v = &a->vis[k];
    spawn(a, v, SHARK + k, (k ? 0.045f : 0.075f) * a->w, 0);
    v->baseY = frand(0.15f, 0.55f) * a->sandY;
    v->bobAmp = 0.012f * a->h;
    v->bobF = 0.2f;
    a->visOn[k] = 1;
}

static void spawn_jelly(Aquarium *a, Jelly *j, int anywhere) {
    int right = irand(2);
    j->vx = (right ? 1 : -1) * frand(0.008f, 0.018f) * a->w;
    j->x = anywhere ? frand(0, (float)a->w) : (right ? -15.0f * a->sc : (float)a->w);
    j->y = frand(0.1f, 0.5f) * a->sandY;
    j->phase = frand(0, 6.28f);
}

static void add_bubble(Aquarium *a, float x, float y) {
    for (int i = 0; i < MAX_BUBBLES; i++)
        if (!a->b[i].alive) {
            a->b[i] = (Bubble){ x, y, -frand(0.10f, 0.2f) * a->h, frand(0, 6.28f), 1 };
            return;
        }
}

static void *create(SDL_Renderer *r, int w, int h) {
    Aquarium *a = calloc(1, sizeof *a);
    a->w = w;
    a->h = h;
    a->sc = (h + 110) / 220 > 1 ? (h + 110) / 220 : 1;
    a->farSc = (a->sc * 2 + 1) / 3 > 1 ? (a->sc * 2 + 1) / 3 : 1;
    a->sandY = h - h / 9;
    for (int s = 0; s < SPECIES; s++)
        for (int fr = 0; fr < 2; fr++) {
            Canvas c = make_fish(s, fr);
            a->fw[s] = c.w;
            a->fh[s] = c.h;
            a->fish[s][fr] = canvas_texture(r, &c);
        }
    for (int fr = 0; fr < 2; fr++) {
        Canvas jc = make_jelly(fr), cc = make_crab(fr), dc = make_diver(fr), sc2 = make_spongebob(fr), wc = make_whale(fr);
        a->jelly[fr] = canvas_texture(r, &jc);
        a->crab[fr] = canvas_texture(r, &cc);
        a->diver[fr] = canvas_texture(r, &dc);
        a->sponge[fr] = canvas_texture(r, &sc2);
        a->whaleTex[fr] = canvas_texture(r, &wc);
    }
    for (int i = 0; i < MAX_DIVERS; i++) {
        int dir = i % 2 ? -1 : 1;
        a->divers[i].vx = dir * frand(0.045f, 0.07f) * w;
        a->divers[i].x = dir > 0 ? -40.0f * a->sc - i * w * 0.4f : w + 40.0f * a->sc;
        a->divers[i].y = frand(0.18f, 0.65f) * a->sandY;
        a->divers[i].phase = frand(0, 6.28f);
    }
    a->krillWait = 2.0f;
    a->whale.wait = 5.0f;
    a->spongeOn = 0;
    a->spongeWait = frand(300, 720);
    Canvas bc = make_bubble(), cc = make_chest(), sc = make_starfish(), co = make_coral(), br = make_brain_coral();
    a->bubble = canvas_texture(r, &bc);
    a->chest = canvas_texture(r, &cc);
    a->star = canvas_texture(r, &sc);
    a->coral = canvas_texture(r, &co);
    a->brain = canvas_texture(r, &br);
    int s = a->sc;
    a->chestRect = (SDL_Rect){ (int)(w * 0.7f), a->sandY - 14 * s, 26 * s, 18 * s };
    a->coralRect = (SDL_Rect){ (int)(w * 0.12f), a->sandY - 27 * s, 34 * s, 30 * s };
    a->brainRect = (SDL_Rect){ (int)(w * 0.45f), a->sandY - 12 * s, 30 * s, 15 * s };
    a->starRect[0] = (SDL_Rect){ (int)(w * 0.3f), a->sandY + (h - a->sandY) / 3, 13 * s, 13 * s };
    a->starRect[1] = (SDL_Rect){ (int)(w * 0.88f), a->sandY + (h - a->sandY) / 2, 13 * s, 13 * s };
    a->nfish = 6 + w * h / 300000;
    if (a->nfish > MAX_FISH - 4) a->nfish = MAX_FISH - 4;
    a->nfar = a->farSc < a->sc ? 4 : 0;
    a->nfish += a->nfar;
    for (int i = 0; i < a->nfish; i++) {
        a->f[i].far = i < a->nfar;
        spawn_fish(a, &a->f[i], 1);
    }
    a->visWait[0] = frand(2, 6);      /* the shark shows up soon: it's the favourite */
    a->visWait[1] = frand(10, 20);
    for (int i = 0; i < JELLIES; i++) spawn_jelly(a, &a->j[i], 1);
    a->crabX = frand(0.2f, 0.8f) * w;
    a->crabVx = 0.03f * w;
    for (int i = 0; i < WEEDS; i++) {
        a->weedX[i] = frand(0.03f, 0.97f) * w;
        a->weedH[i] = frand(0.15f, 0.4f) * h;
        a->weedP[i] = frand(0, 6.28f);
    }
    for (int i = 0; i < PEBBLES; i++) {
        a->pebble[i][0] = irand(w);
        a->pebble[i][1] = a->sandY + irand(h - a->sandY);
        a->pebble[i][2] = irand(3);
    }
    for (int i = 0; i < SNOW; i++) {
        a->snow[i][0] = frand(0, (float)w);
        a->snow[i][1] = frand(0, (float)a->sandY);
        a->snow[i][2] = frand(0, 6.28f);
    }
    return a;
}

/* Small fish near the shark turn and dart away from it for a moment. */
static void flee(Aquarium *a, Fish *f, float dt) {
    if (f->scare > 0) {
        f->scare -= dt;
        f->baseY += f->dodge * dt * f->scare;
        float lo = 0.05f * a->sandY, hi = 0.88f * a->sandY;
        f->baseY = f->baseY < lo ? lo : (f->baseY > hi ? hi : f->baseY);
        return;
    }
    if (f->far || !a->visOn[0]) return;
    const Fish *s = &a->vis[0];
    float sx = s->x + a->fw[SHARK] * a->sc * 0.5f, sy = fish_y(a, s) + a->fh[SHARK] * a->sc * 0.5f;
    float dx = f->x + a->fw[f->species] * a->sc * 0.5f - sx, dy = fish_y(a, f) - sy;
    if (fabsf(dx) < 0.2f * a->w && fabsf(dy) < 0.16f * a->h) {
        f->scare = 1.5f;
        f->vx = dx < 0 ? -fabsf(f->vx) : fabsf(f->vx);
        f->dodge = (dy < 0 ? -1 : 1) * 0.1f * a->h;
    }
}

static void step(void *p, float dt) {
    Aquarium *a = p;
    a->t += dt;
    for (int i = 0; i < a->nfish; i++) {
        Fish *f = &a->f[i];
        flee(a, f, dt);
        float boost = f->scare > 0 ? 1 + 1.6f * f->scare : 1;
        f->x += f->vx * dt * boost;
        f->flick += dt * 4 * boost;
        f->frame = (int)f->flick % 2;
        float fw = (float)a->fw[f->species] * fish_scale(a, f);
        if (f->x < -fw - 5 || f->x > a->w + 5) spawn_fish(a, f, 0);
    }
    for (int k = 0; k < 2; k++) {
        Fish *v = &a->vis[k];
        if (!a->visOn[k]) {
            if ((a->visWait[k] -= dt) <= 0) spawn_visitor(a, k);
            continue;
        }
        v->x += v->vx * dt;
        v->flick += dt * (k ? 1.2f : 2);
        v->frame = (int)v->flick % 2;
        float fw = (float)a->fw[v->species] * a->sc;
        if (v->x < -fw - 5 || v->x > a->w + 5) {
            a->visOn[k] = 0;
            a->visWait[k] = k ? frand(20, 40) : frand(8, 20);
        }
    }
    for (int i = 0; i < JELLIES; i++) {                 /* pulse up, then sink slowly */
        Jelly *j = &a->j[i];
        j->phase += dt * 1.7f;
        float push = sinf(j->phase);
        j->y += (0.018f - (push > 0 ? push : 0) * 0.06f) * a->h * dt;
        if (j->y < 0.06f * a->sandY) j->y += 0.04f * a->h * dt;
        if (j->y > 0.6f * a->sandY) j->y -= 0.04f * a->h * dt;
        j->x += j->vx * dt;
        if (j->x < -20.0f * a->sc || j->x > a->w + 5) spawn_jelly(a, j, 0);
    }
    if (a->crabWait > 0) a->crabWait -= dt;             /* the crab scuttles, pauses, turns */
    else {
        a->crabX += a->crabVx * dt;
        a->crabStep += dt * 8;
        float cw = 22.0f * a->sc;
        if (a->crabX < 0) a->crabVx = fabsf(a->crabVx);
        if (a->crabX > a->w - cw) a->crabVx = -fabsf(a->crabVx);
        if (frand(0, 1) < dt * 0.25f) {
            a->crabWait = frand(1, 3);
            if (irand(2)) a->crabVx = -a->crabVx;
        }
    }
    for (int i = 0; i < SNOW; i++) {
        a->snow[i][1] += 0.008f * a->h * dt;
        if (a->snow[i][1] > a->sandY) { a->snow[i][1] = 0; a->snow[i][0] = frand(0, (float)a->w); }
    }
    a->chestTimer -= dt;
    if (a->chestTimer <= 0) {
        a->chestTimer = frand(0.15f, 0.5f);
        add_bubble(a, a->chestRect.x + a->chestRect.w * frand(0.3f, 0.7f), (float)a->chestRect.y);
    }
    a->sandTimer -= dt;
    if (a->sandTimer <= 0) {
        a->sandTimer = frand(0.4f, 1.2f);
        add_bubble(a, frand(0, (float)a->w), (float)a->sandY);
    }
    for (int i = 0; i < MAX_BUBBLES; i++) {
        Bubble *b = &a->b[i];
        if (!b->alive) continue;
        b->y += b->vy * dt;
        b->phase += dt * 3;
        if (b->y < -10) b->alive = 0;
    }
    /* Divers swimming through */
    for (int i = 0; i < MAX_DIVERS; i++) {
        Diver *dv = &a->divers[i];
        dv->x += dv->vx * dt;
        dv->phase += dt * 3.2f;
        dv->frame = (int)(dv->phase * 1.5f) % 2;
        dv->bubT -= dt;
        if (dv->bubT <= 0) {
            dv->bubT = frand(0.18f, 0.45f);
            float bx = dv->vx > 0 ? dv->x + 28 * a->sc : dv->x + 6 * a->sc;
            add_bubble(a, bx, dv->y + 6 * a->sc + sinf(dv->phase) * 3 * a->sc);
        }
        float dw = 34.0f * a->sc;
        if (dv->x < -dw - 20 || dv->x > a->w + 20) {
            int dir = irand(2) ? 1 : -1;
            dv->vx = dir * frand(0.045f, 0.07f) * a->w;
            dv->x = dir > 0 ? -dw : (float)a->w + 10;
            dv->y = frand(0.15f, 0.68f) * a->sandY;
        }
    }
    /* SpongeBob strolling rarely across the sand */
    float sw = 22.0f * a->sc;
    if (!a->spongeOn) {
        if ((a->spongeWait -= dt) <= 0) {
            int dir = irand(2) ? 1 : -1;
            a->spongeOn = 1;
            a->spongeVx = dir * 0.028f * a->w;
            a->spongeX = dir > 0 ? -sw : (float)a->w;
            a->spongeStep = 0;
        }
    } else {
        a->spongeX += a->spongeVx * dt;
        a->spongeStep += dt * 5.0f;
        if ((a->spongeVx > 0 && a->spongeX > a->w + 10) || (a->spongeVx < 0 && a->spongeX < -sw - 10)) {
            a->spongeOn = 0;
            a->spongeWait = frand(480, 1080);
        }
    }
    /* Shadowy background krill swarm & Blue Whale following behind to gulp them */
    int anyKrill = 0;
    for (int i = 0; i < MAX_KRILL; i++) {
        Krill *kr = &a->krill[i];
        if (!kr->alive) continue;
        kr->x += kr->vx * dt;
        kr->ph += dt * 8.0f;
        if ((kr->vx > 0 && kr->x < a->w + 20) || (kr->vx < 0 && kr->x > -20)) anyKrill = 1;
        else kr->alive = 0;
    }
    if (g_force_egg) {
        a->krillWait = 0;
        a->whale.on = 0;
        for (int i = 0; i < MAX_KRILL; i++) a->krill[i].alive = 0;
        anyKrill = 0;
        g_force_egg = 0;
    }
    if (!anyKrill && !a->whale.on) {
        a->krillWait -= dt;
        if (a->krillWait <= 0) {
            int dir = irand(2) ? 1 : -1;
            a->krillDir = dir;
            float baseY = frand(0.32f, 0.50f) * a->sandY;
            float sp = 0.045f * a->w * dir;
            for (int i = 0; i < MAX_KRILL; i++) {
                a->krill[i].alive = 1;
                a->krill[i].x = dir > 0 ? frand(-0.22f, -0.02f) * a->w : frand(1.02f, 1.22f) * a->w;
                a->krill[i].y = baseY + frand(-0.07f, 0.07f) * a->sandY;
                a->krill[i].vx = sp * frand(0.92f, 1.08f);
                a->krill[i].ph = frand(0, 6.28f);
            }
            a->whale.on = 1;
            a->whale.dir = dir;
            float ww = a->w * 0.56f;
            a->whale.x = dir > 0 ? -ww - a->w * 0.28f : a->w + a->w * 0.28f;
            a->whale.y = baseY;
            a->whale.vx = dir * a->w * 0.11f;
            a->whale.mouth = 0;
            a->krillWait = frand(14, 24);
        }
    }
    if (a->whale.on) {
        BlueWhale *wh = &a->whale;
        wh->x += wh->vx * dt;
        float ww = a->w * 0.56f, wht = ww * (60.0f / 224.0f);
        float mouthX = wh->dir > 0 ? wh->x + ww * 0.80f : wh->x + ww * 0.20f;
        float mouthY = wh->y + wht * 0.08f;
        int nearKrill = 0;
        for (int i = 0; i < MAX_KRILL; i++) {
            Krill *kr = &a->krill[i];
            if (!kr->alive) continue;
            float dx = kr->x - mouthX, dy = kr->y - mouthY;
            float ahead = wh->dir > 0 ? dx : -dx;
            if (ahead > -ww * 0.04f && ahead < ww * 0.24f && fabsf(dy) < wht * 0.70f) {
                nearKrill = 1;
                /* Funnel krill smoothly into the open mouth instead of vanishing out front */
                if (ahead < ww * 0.16f && fabsf(dy) < wht * 0.45f) {
                    kr->x += (mouthX - kr->x) * fminf(1.0f, dt * 6.5f);
                    kr->y += (mouthY - kr->y) * fminf(1.0f, dt * 6.5f);
                }
            }
            if (fabsf(dx) < ww * 0.03f && fabsf(dy) < wht * 0.16f && wh->mouth > 0.35f) {
                kr->alive = 0;
                if (irand(3) == 0) add_bubble(a, mouthX, mouthY);
            }
        }
        float targetMouth = nearKrill ? 1.0f : 0.0f;
        wh->mouth += (targetMouth - wh->mouth) * fminf(1.0f, dt * 8.0f);
        if ((wh->dir > 0 && wh->x > a->w + 40) || (wh->dir < 0 && wh->x + ww < -40)) wh->on = 0;
    }
}

static void draw_fish(Aquarium *a, SDL_Renderer *r, const Fish *f) {
    int s = fish_scale(a, f);
    SDL_Rect d = { (int)f->x, (int)fish_y(a, f), a->fw[f->species] * s, a->fh[f->species] * s };
    SDL_RenderCopyEx(r, a->fish[f->species][f->frame], NULL, &d, 0, NULL,
                     f->vx < 0 ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
}

static void draw(void *p, SDL_Renderer *r) {
    Aquarium *a = p;
    vgradient(r, a->w, a->h, hexcol(0x1B6CA8), hexcol(0x0B2E59));
    for (int i = 0; i < a->nfar; i++) {                 /* faraway fish, hazy blue */
        SDL_Texture *t = a->fish[a->f[i].species][a->f[i].frame];
        SDL_SetTextureColorMod(t, 80, 130, 175);
        SDL_SetTextureAlphaMod(t, 190);
        draw_fish(a, r, &a->f[i]);
        SDL_SetTextureColorMod(t, 255, 255, 255);
        SDL_SetTextureAlphaMod(t, 255);
    }
    /* Shadowy background Blue Whale & krill swarm */
    if (a->whale.on) {
        const BlueWhale *wh = &a->whale;
        SDL_Texture *wt = a->whaleTex[wh->mouth > 0.35f];
        float ww = a->w * 0.56f, wht = ww * (60.0f / 224.0f);
        float cy = wh->y + sinf(a->t * 1.4f) * a->h * 0.015f;
        SDL_Rect wd = { (int)wh->x, (int)(cy - wht * 0.5f), (int)ww, (int)wht };
        SDL_SetTextureColorMod(wt, 55, 100, 150);
        SDL_SetTextureAlphaMod(wt, 155);
        SDL_RenderCopyEx(r, wt, NULL, &wd, 0, NULL, wh->dir < 0 ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
        SDL_SetTextureColorMod(wt, 255, 255, 255);
        SDL_SetTextureAlphaMod(wt, 255);
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    for (int i = 0; i < MAX_KRILL; i++) {
        const Krill *kr = &a->krill[i];
        if (!kr->alive) continue;
        int ky = (int)(kr->y + sinf(kr->ph) * 2.0f * a->sc);
        int ks = a->sc;
        int kd = kr->vx > 0 ? 1 : -1;
        set_color(r, (SDL_Color){ 85, 95, 130, 150 });
        SDL_RenderFillRect(r, &(SDL_Rect){ (int)kr->x - 2 * ks, ky - ks / 2, 4 * ks, ks });
        set_color(r, (SDL_Color){ 105, 115, 150, 150 });
        SDL_RenderFillRect(r, &(SDL_Rect){ (int)kr->x - kd * 3 * ks, ky, 2 * ks, ks });
        set_color(r, (SDL_Color){ 20, 40, 75, 150 });
        SDL_RenderFillRect(r, &(SDL_Rect){ (int)kr->x + kd * 2 * ks, ky - ks / 2, ks, ks });
    }
    for (int i = 0; i < WEEDS; i++) {                   /* seaweed */
        float x = a->weedX[i], y = (float)a->sandY + a->sc * 2, seg = a->weedH[i] / 8;
        for (int k = 0; k < 8; k++) {
            float sway = sinf(a->t * 0.9f + a->weedP[i] + k * 0.45f) * a->sc * 1.6f * k / 4;
            float nx = x + sway, ny = y - seg;
            thick_line(r, x, y, nx, ny, a->sc * (4.5f - k * 0.35f), k % 2 ? hexcol(0x3CB371) : hexcol(0x2E8B57));
            x = nx;
            y = ny;
        }
    }
    SDL_Color st = hexcol(0xE4C88E), sb = hexcol(0xC9A766);   /* sand, lit from above */
    SDL_Vertex v[4] = {
        { { 0, (float)a->sandY }, st, { 0, 0 } }, { { (float)a->w, (float)a->sandY }, st, { 0, 0 } },
        { { (float)a->w, (float)a->h }, sb, { 0, 0 } }, { { 0, (float)a->h }, sb, { 0, 0 } },
    };
    SDL_RenderGeometry(r, NULL, v, 4, (int[]){ 0, 1, 2, 0, 2, 3 }, 6);
    set_color(r, hexcol(0xF0DCA8));
    SDL_RenderFillRect(r, &(SDL_Rect){ 0, a->sandY, a->w, a->sc });
    for (int i = 0; i < PEBBLES; i++) {
        static const uint32_t pc[3] = { 0xB89A5E, 0xC8A96B, 0xEAD3A2 };
        set_color(r, hexcol(pc[a->pebble[i][2]]));
        SDL_Rect q = { a->pebble[i][0], a->pebble[i][1], a->sc * 2, a->sc };
        SDL_RenderFillRect(r, &q);
    }
    SDL_RenderCopy(r, a->coral, NULL, &a->coralRect);
    SDL_RenderCopy(r, a->brain, NULL, &a->brainRect);
    SDL_RenderCopy(r, a->chest, NULL, &a->chestRect);
    for (int i = 0; i < 2; i++) SDL_RenderCopy(r, a->star, NULL, &a->starRect[i]);
    SDL_Rect cr = { (int)a->crabX, a->sandY + (a->h - a->sandY) / 4 - 9 * a->sc, 22 * a->sc, 13 * a->sc };
    SDL_RenderCopy(r, a->crab[(int)a->crabStep % 2], NULL, &cr);
    for (int i = 0; i < JELLIES; i++) {
        const Jelly *j = &a->j[i];
        SDL_Rect d = { (int)j->x, (int)j->y, 15 * a->sc, 24 * a->sc };
        SDL_RenderCopy(r, a->jelly[sinf(j->phase) > 0], NULL, &d);
    }
    if (a->visOn[1]) draw_fish(a, r, &a->vis[1]);
    for (int i = a->nfar; i < a->nfish; i++) draw_fish(a, r, &a->f[i]);
    /* Divers */
    for (int i = 0; i < MAX_DIVERS; i++) {
        const Diver *dv = &a->divers[i];
        SDL_Rect d = { (int)dv->x, (int)(dv->y + sinf(dv->phase) * 4 * a->sc), 34 * a->sc, 16 * a->sc };
        SDL_RenderCopyEx(r, a->diver[dv->frame], NULL, &d, 0, NULL, dv->vx < 0 ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
    }
    /* SpongeBob on the ocean floor (extra rare visitor) */
    if (a->spongeOn) {
        SDL_Rect sb = { (int)a->spongeX, a->sandY - 22 * a->sc, 22 * a->sc, 26 * a->sc };
        SDL_RenderCopyEx(r, a->sponge[(int)a->spongeStep % 2], NULL, &sb, 0, NULL,
                         a->spongeVx < 0 ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
    }
    if (a->visOn[0]) draw_fish(a, r, &a->vis[0]);
    for (int i = 0; i < MAX_BUBBLES; i++) {
        Bubble *b = &a->b[i];
        if (!b->alive) continue;
        int s = 7 * a->sc / 2 + 2;
        SDL_Rect d = { (int)(b->x + sinf(b->phase) * a->sc * 2), (int)b->y, s, s };
        SDL_RenderCopy(r, a->bubble, NULL, &d);
    }
    int ss = a->sc / 2 > 1 ? a->sc / 2 : 1;             /* drifting specks */
    set_color(r, (SDL_Color){ 220, 240, 255, 50 });
    for (int i = 0; i < SNOW; i++) {
        SDL_Rect q = { (int)(a->snow[i][0] + sinf(a->t * 0.5f + a->snow[i][2]) * a->sc * 3), (int)a->snow[i][1], ss, ss };
        SDL_RenderFillRect(r, &q);
    }
}

static void destroy(void *p) {
    Aquarium *a = p;
    for (int s = 0; s < SPECIES; s++)
        for (int fr = 0; fr < 2; fr++) SDL_DestroyTexture(a->fish[s][fr]);
    for (int fr = 0; fr < 2; fr++) {
        SDL_DestroyTexture(a->jelly[fr]);
        SDL_DestroyTexture(a->crab[fr]);
        SDL_DestroyTexture(a->diver[fr]);
        SDL_DestroyTexture(a->sponge[fr]);
        SDL_DestroyTexture(a->whaleTex[fr]);
    }
    SDL_DestroyTexture(a->bubble);
    SDL_DestroyTexture(a->chest);
    SDL_DestroyTexture(a->star);
    SDL_DestroyTexture(a->coral);
    SDL_DestroyTexture(a->brain);
    free(a);
}

const Saver saver_aquarium = { "aquarium", "FISH", create, step, draw, destroy, NULL, NULL, "E WHALE", NULL, NULL, NULL };
