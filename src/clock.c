/* Optional clock drawn over any saver. It drifts to a new spot about once a
   minute (fading out and back in) so nothing sits in one place on the screen.
   It stays off the areas a saver says are showing numbers (the dice and their
   total, say), and moves on early if one of them comes to it. */
#include "engine.h"
#include <stdio.h>
#include <time.h>

static struct {
    float x, y;        /* top-left, as a fraction of the free space */
    float alpha, hold; /* fade 0..1, seconds left before the next move */
    int moving;
    int w, h;          /* the screen */
} C;

static void clock_text(char *text, size_t n) {
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    if (g_config.clock24) snprintf(text, n, "%02d:%02d", tm->tm_hour, tm->tm_min);
    else snprintf(text, n, "%d:%02d %s", tm->tm_hour % 12 ? tm->tm_hour % 12 : 12,
                  tm->tm_min, tm->tm_hour < 12 ? "AM" : "PM");
}

static int clock_scale(void) { return C.h / 120 > 2 ? C.h / 120 : 2; }

/* Where the clock is drawn at spot (fx, fy), with a little room around it. */
static SDL_Rect clock_rect(float fx, float fy) {
    char text[16];
    clock_text(text, sizeof text);
    int sc = clock_scale(), tw = text_width(text, sc), th = FONT_H * sc, m = sc * 2;
    return (SDL_Rect){ (int)(fx * (C.w - tw)) - m, (int)(fy * (C.h - th)) - m, tw + 2 * m, th + 2 * m };
}

static int overlap(SDL_Rect a, SDL_Rect b) {
    SDL_Rect o;
    return SDL_IntersectRect(&a, &b, &o) ? o.w * o.h : 0;
}

static int covered(SDL_Rect c, const SDL_Rect *avoid, int n) {
    int area = 0;
    for (int i = 0; i < n; i++) area += overlap(c, avoid[i]);
    return area;
}

/* A random spot clear of `avoid`, or the least covered of the ones tried. */
static void pick_spot(const SDL_Rect *avoid, int n) {
    float bx = 0, by = 0;
    int best = -1;
    for (int k = 0; k < 40; k++) {
        float x = frand(0.05f, 0.95f), y = frand(0.05f, 0.95f);
        int a = covered(clock_rect(x, y), avoid, n);
        if (best < 0 || a < best) { best = a; bx = x; by = y; }
        if (a == 0) break;
    }
    C.x = bx;
    C.y = by;
}

void clock_reset(int w, int h) {
    C.w = w;
    C.h = h;
    pick_spot(NULL, 0);
    C.alpha = 0;
    C.hold = frand(45, 75);
    C.moving = 0;
}

void clock_step(float dt, const SDL_Rect *avoid, int n) {
    if (!C.moving && covered(clock_rect(C.x, C.y), avoid, n)) C.moving = 1;   /* numbers came to it */
    if (C.moving) {                        /* fade out, jump, fade back in */
        C.alpha -= dt * 2;
        if (C.alpha <= 0) {
            C.alpha = 0;
            pick_spot(avoid, n);
            C.moving = 0;
            C.hold = frand(45, 75);
        }
    } else {
        C.alpha = C.alpha + dt * 2 > 1 ? 1 : C.alpha + dt * 2;
        C.hold -= dt;
        if (C.hold <= 0) C.moving = 1;
    }
}

void clock_draw(SDL_Renderer *r, int w, int h) {
    char text[16];
    clock_text(text, sizeof text);
    C.w = w;
    C.h = h;
    int sc = clock_scale();
    int tw = text_width(text, sc), th = FONT_H * sc;
    int x = (int)(C.x * (w - tw)), y = (int)(C.y * (h - th));
    SDL_Color c = { 255, 255, 255, (Uint8)(215 * C.alpha) };
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_Color shadow = { 0, 0, 0, (Uint8)(170 * C.alpha) };
    int off = sc / 3 > 1 ? sc / 3 : 1;
    draw_text(r, text, x + off, y + off, sc, shadow);
    draw_text(r, text, x, y, sc, c);
}
