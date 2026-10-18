/* Optional clock drawn over any saver. It drifts to a new spot about once a
   minute (fading out and back in) so nothing sits in one place on the screen. */
#include "engine.h"
#include <stdio.h>
#include <time.h>

static struct {
    float x, y;        /* top-left, as a fraction of the free space */
    float alpha, hold; /* fade 0..1, seconds left before the next move */
    int moving;
} C;

void clock_reset(int w, int h) {
    (void)w; (void)h;
    C.x = frand(0.05f, 0.95f);
    C.y = frand(0.05f, 0.95f);
    C.alpha = 0;
    C.hold = frand(45, 75);
    C.moving = 0;
}

void clock_step(float dt) {
    if (C.moving) {                        /* fade out, jump, fade back in */
        C.alpha -= dt * 2;
        if (C.alpha <= 0) {
            C.alpha = 0;
            C.x = frand(0.05f, 0.95f);
            C.y = frand(0.05f, 0.95f);
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
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    char text[16];
    if (g_config.clock24) snprintf(text, sizeof text, "%02d:%02d", tm->tm_hour, tm->tm_min);
    else snprintf(text, sizeof text, "%d:%02d %s", tm->tm_hour % 12 ? tm->tm_hour % 12 : 12,
                  tm->tm_min, tm->tm_hour < 12 ? "AM" : "PM");
    int sc = h / 120 > 2 ? h / 120 : 2;
    int tw = text_width(text, sc), th = FONT_H * sc;
    int x = (int)(C.x * (w - tw)), y = (int)(C.y * (h - th));
    SDL_Color c = { 255, 255, 255, (Uint8)(215 * C.alpha) };
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_Color shadow = { 0, 0, 0, (Uint8)(170 * C.alpha) };
    int off = sc / 3 > 1 ? sc / 3 : 1;
    draw_text(r, text, x + off, y + off, sc, shadow);
    draw_text(r, text, x, y, sc, c);
}
