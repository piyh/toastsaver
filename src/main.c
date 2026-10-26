/*
 * toastsaver - a collection of screensavers for Raspberry Pi OS (Wayland).
 *
 *   toastsaver                 run the saver chosen in the settings (fullscreen)
 *   toastsaver pipes           run a specific saver
 *   toastsaver --picker        open the app: the picker, with settings behind its gear
 *                              (also: toastsaver-picker, and toastsaver-settings for old menus)
 */
#include "engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <sys/stat.h>

const Saver *const SAVERS[] = {
    &saver_toasters, &saver_rainbow, &saver_pipes, &saver_books, &saver_dice, &saver_aquarium,
    &saver_birds, &saver_starfield, &saver_mystify, &saver_matrix, &saver_lava, &saver_name,
    &saver_volcano,
};
const int SAVER_COUNT = sizeof SAVERS / sizeof SAVERS[0];

const Saver *saver_find(const char *id) {
    for (int i = 0; i < SAVER_COUNT; i++)
        if (!strcmp(SAVERS[i]->id, id)) return SAVERS[i];
    return NULL;
}

/* "random" picks from the SURPRISE mix, never the same one twice in a row if it can help it. */
static const Saver *pick_random(void) {
    static const Saver *last;
    int inMix = 0, n = 0;
    for (int i = 0; i < SAVER_COUNT; i++) inMix += saver_in_mix(SAVERS[i]);
    const Saver *avoid = inMix > 1 ? last : NULL;
    for (int i = 0; i < SAVER_COUNT; i++) n += saver_in_mix(SAVERS[i]) && SAVERS[i] != avoid;
    int k = irand(n);
    for (int i = 0; i < SAVER_COUNT; i++)
        if (saver_in_mix(SAVERS[i]) && SAVERS[i] != avoid && k-- == 0) return last = SAVERS[i];
    return last = SAVERS[irand(SAVER_COUNT)];
}

const Saver *saver_resolve(const char *id) {
    if (!strcmp(id, "random")) return pick_random();
    const Saver *s = saver_find(id);
    return s ? s : &saver_toasters;
}

/* ---------- fullscreen runner ---------- */

int g_show_info;   /* windowed: I toggles a little read-out of what the saver is doing */

/* Speed slider geometry (windowed mode): the knob position is log2(speed), -2..5 = 0.25x..32x. */
#define SPEED_MIN_LOG (-2.0f)
#define SPEED_MAX_LOG 5.0f
static SDL_Rect speed_track(int w, int h) {
    int tw = w / 2 < 700 ? w / 2 : 700, th = h / 90 > 8 ? h / 90 : 8;
    return (SDL_Rect){ (w - tw) / 2, h - h / 14, tw, th };
}

static SDL_Rect reset_button(int w, int h) {
    SDL_Rect t = speed_track(w, h);
    int sc = h / 240 > 2 ? h / 240 : 2;
    return (SDL_Rect){ t.x + t.w + 36, t.y - 3 * sc, text_width("RESET", sc) + 8 * sc, 11 * sc };
}

static void draw_speed_ui(SDL_Renderer *r, int w, int h, float lg, int paused) {
    SDL_Rect t = speed_track(w, h);
    int sc = h / 240 > 2 ? h / 240 : 2;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 120);
    SDL_Rect bg = { t.x - 24, t.y - 7 * sc - 18, t.w + 48, t.h + 7 * sc + 36 };
    SDL_RenderFillRect(r, &bg);
    SDL_SetRenderDrawColor(r, 255, 255, 255, 90);
    SDL_RenderFillRect(r, &t);
    float f = (lg - SPEED_MIN_LOG) / (SPEED_MAX_LOG - SPEED_MIN_LOG);
    SDL_SetRenderDrawColor(r, 255, 190, 60, 255);
    SDL_Rect fill = { t.x, t.y, (int)(t.w * f), t.h };
    SDL_RenderFillRect(r, &fill);
    SDL_Rect knob = { t.x + (int)(t.w * f) - t.h, t.y - t.h / 2, t.h * 2, t.h * 2 };
    SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    SDL_RenderFillRect(r, &knob);
    char label[64];
    float sp = powf(2.0f, lg);
    if (paused) snprintf(label, sizeof label, "PAUSED");
    else if (sp < 1.0f) snprintf(label, sizeof label, "SPEED %.2gX", sp);
    else snprintf(label, sizeof label, "SPEED %.3gX", sp);
    draw_text_shadow(r, label, t.x, t.y - 7 * sc - 10, sc, (SDL_Color){ 255, 255, 255, 255 });
    SDL_Rect rb = reset_button(w, h);
    SDL_SetRenderDrawColor(r, 255, 255, 255, 40);
    SDL_RenderFillRect(r, &rb);
    SDL_SetRenderDrawColor(r, 255, 255, 255, 200);
    SDL_RenderDrawRect(r, &rb);
    draw_text_shadow(r, "RESET", rb.x + 4 * sc, rb.y + 2 * sc, sc, (SDL_Color){ 255, 255, 255, 255 });
    draw_text_shadow(r, "[ ] SPEED   1 NORMAL SPEED   R RESTART   SPACE PAUSE   S SHOT   I INFO", t.x + t.w / 2 - text_width("[ ] SPEED   1 NORMAL SPEED   R RESTART   SPACE PAUSE   S SHOT   I INFO", 1) / 2,
                     t.y + t.h + 8, 1, (SDL_Color){ 220, 220, 220, 255 });
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

/* Saves what is on screen (call after drawing, before presenting) to ~/Pictures if it exists,
   else the home folder, and prints where it went. Returns 0 on success. */
static int save_screenshot(SDL_Renderer *r, const char *id, int w, int h) {
    uint32_t *px = malloc((size_t)w * h * 4);
    if (!px) return 1;
    if (SDL_RenderReadPixels(r, NULL, SDL_PIXELFORMAT_RGBA8888, px, w * 4) != 0) { free(px); return 1; }
    const char *home = getenv("HOME");
    char dir[600], path[800], stamp[32];
    struct stat st;
    snprintf(dir, sizeof dir, "%s/Pictures", home ? home : ".");
    if (stat(dir, &st) != 0 || !S_ISDIR(st.st_mode)) snprintf(dir, sizeof dir, "%s", home ? home : ".");
    time_t now = time(NULL);
    strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", localtime(&now));
    snprintf(path, sizeof path, "%s/toastsaver-%s-%s.png", dir, id, stamp);
    int rc = write_png(path, w, h, px);
    free(px);
    if (rc == 0) printf("screenshot: %s\n", path);
    else fprintf(stderr, "screenshot: could not write %s\n", path);
    fflush(stdout);
    return rc;
}

/* Screensaver: any key, click or mouse movement ends it. With --windowed it behaves like an
   app instead, so you can look around: only Esc/Q or closing the window quits, S (or P or F12)
   takes a screenshot, and Space pauses. */
int run_saver(SDL_Window *win, SDL_Renderer *r, const Saver *sv, int windowed) {
    int w, h;
    SDL_GetRendererOutputSize(r, &w, &h);
    void *st = sv->create(r, w, h);
    if (!st) return 1;
    if (!windowed) SDL_ShowCursor(SDL_DISABLE);
    clock_reset(w, h);

    const Uint64 freq = SDL_GetPerformanceFrequency();
    Uint64 last = SDL_GetPerformanceCounter();
    const Uint32 started = SDL_GetTicks();
    float moved = 0, flash = 0, lg = 0, uiT = 0;   /* lg: log2 of the speed */
    int result = -1, paused = 0, shot = 0, drag = 0, reset = 0;
    (void)win;

    while (result < 0) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            /* Ignore input for a moment: the click/keypress that started us is still arriving. */
            int armed = SDL_GetTicks() - started > 1200;
            switch (e.type) {
            case SDL_QUIT: result = 1; break;
            case SDL_KEYDOWN: {
                SDL_Keycode k = e.key.keysym.sym;
                if (!windowed) {
                    if (armed || k == SDLK_ESCAPE) result = 0;
                } else if (k == SDLK_ESCAPE || k == SDLK_q) {
                    result = 0;
                } else if (k == SDLK_s || k == SDLK_p || k == SDLK_F12) {
                    shot = 1;
                } else if (k == SDLK_SPACE && !e.key.repeat) {
                    paused = !paused;
                } else if (k == SDLK_LEFTBRACKET || k == SDLK_MINUS) {
                    lg = fmaxf(SPEED_MIN_LOG, lg - 1);
                } else if (k == SDLK_RIGHTBRACKET || k == SDLK_EQUALS || k == SDLK_PLUS) {
                    lg = fminf(SPEED_MAX_LOG, lg + 1);
                } else if (k == SDLK_1) {
                    lg = 0;
                } else if (k == SDLK_i) {
                    g_show_info = !g_show_info;
                } else if (k == SDLK_r) {
                    reset = 1;
                }
                if (windowed) uiT = 3;
                break;
            }
            case SDL_MOUSEBUTTONDOWN:
            case SDL_FINGERDOWN:
                if (armed && !windowed) result = 0;
                if (windowed && e.type == SDL_MOUSEBUTTONDOWN) {
                    int ww, wh;
                    SDL_GetWindowSize(win, &ww, &wh);
                    float mx = e.button.x * (float)w / ww, my = e.button.y * (float)h / wh;
                    SDL_Rect rb = reset_button(w, h), t = speed_track(w, h);
                    if (mx >= rb.x && mx < rb.x + rb.w && my >= rb.y && my < rb.y + rb.h) reset = 1;
                    if (my > t.y - 40 && my < t.y + t.h + 40 && mx > t.x - 20 && mx < t.x + t.w + 20) drag = 1;
                }
                if (!drag) break;
                /* a press on the slider also places the knob, like a drag: */
                /* fall through */
            case SDL_MOUSEMOTION:
                if (windowed) {
                    uiT = 3;
                    if (drag && (e.type == SDL_MOUSEMOTION ? (e.motion.state & SDL_BUTTON_LMASK) : 1)) {
                        int ww, wh;
                        SDL_GetWindowSize(win, &ww, &wh);
                        int px = e.type == SDL_MOUSEMOTION ? e.motion.x : e.button.x;
                        float mx = px * (float)w / ww;
                        SDL_Rect t = speed_track(w, h);
                        float f = (mx - t.x) / t.w;
                        f = f < 0 ? 0 : f > 1 ? 1 : f;
                        lg = SPEED_MIN_LOG + f * (SPEED_MAX_LOG - SPEED_MIN_LOG);
                        lg = floorf(lg * 4 + 0.5f) / 4;      /* snaps to quarter steps, so 1x is easy to hit */
                        if (lg > -0.12f && lg < 0.12f) lg = 0;
                    } else if (e.type == SDL_MOUSEMOTION) {
                        drag = 0;
                    }
                }
                if (armed && !windowed) {
                    moved += abs(e.motion.xrel) + abs(e.motion.yrel);
                    if (moved > 24) result = 0;
                }
                break;
            case SDL_MOUSEBUTTONUP:
                drag = 0;
                break;
            case SDL_WINDOWEVENT:
                if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                    SDL_GetRendererOutputSize(r, &w, &h);
                    sv->destroy(st);
                    st = sv->create(r, w, h);
                    if (!st) return 1;
                }
                break;
            }
        }
        Uint64 now = SDL_GetPerformanceCounter();
        float dt = (float)(now - last) / (float)freq;
        last = now;
        if (dt > 0.1f) dt = 0.1f;
        if (reset) {                              /* start the simulation over from scratch */
            reset = 0;
            sv->destroy(st);
            st = sv->create(r, w, h);
            if (!st) return 1;
            clock_reset(w, h);
        }
        if (!paused) {                            /* speed > 1 runs several steps, each small enough to stay stable */
            float left = dt * powf(2.0f, lg);
            for (int n = 0; left > 0 && n < 64; n++) {
                float d = left < 0.05f ? left : 0.05f;
                sv->step(st, d);
                left -= d;
            }
        }
        SDL_RenderSetViewport(r, NULL);
        sv->draw(st, r);
        if (g_config.clock) {
            clock_step(dt);
            clock_draw(r, w, h);
        }
        if (shot) {
            shot = 0;
            if (save_screenshot(r, sv->id, w, h) == 0) flash = 0.25f;
        }
        if (windowed) {
            if (uiT > 0) uiT -= dt;
            if (uiT > 0 || paused || drag) draw_speed_ui(r, w, h, lg, paused);
        }
        if (flash > 0) {                          /* a quick white flash so you know it worked */
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(r, 255, 255, 255, (Uint8)(flash / 0.25f * 160));
            SDL_RenderFillRect(r, NULL);
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
            flash -= dt;
        }
        SDL_RenderPresent(r);
        SDL_Delay(8); /* vsync paces us when available; this keeps CPU sane otherwise */
        if (g_config.fps > 0) {   /* FPS cap: dt above is still the real elapsed time */
            double spent = (double)(SDL_GetPerformanceCounter() - now) * 1000.0 / (double)freq;
            double wait = 1000.0 / g_config.fps - spent;
            if (wait > 0) SDL_Delay((Uint32)wait);
        }
    }
    sv->destroy(st);
    SDL_ShowCursor(SDL_ENABLE);
    return result;
}

/* ---------- offscreen rendering for tests and the icon ---------- */

Canvas make_toaster(int frame);   /* from savers/toasters.c */
Canvas toasters_sheet(void);

static void font_test(SDL_Renderer *r, int w, int h) {
    SDL_SetRenderDrawColor(r, 20, 20, 40, 255);
    SDL_RenderClear(r);
    int sc = h / 60 > 1 ? h / 60 : 1;
    const char *lines[] = { "ABCDEFGHIJKLM", "NOPQRSTUVWXYZ", "0123456789", "HI! OK? *-.:" };
    for (int i = 0; i < 4; i++)
        draw_text_shadow(r, lines[i], sc * 4, sc * 4 + i * sc * 10, sc, hexcol(0xFFFFFF));
    (void)w;
}

/* Renders FRAMES pngs (frame0000.png ...) of a saver after warming up for `seconds`. */
static int sequence(const char *dir, const char *what, int w, int h, float seconds, int frames, int fps) {
    const Saver *sv = saver_find(what);
    if (!sv) { fprintf(stderr, "unknown saver: %s\n", what); return 2; }
    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA8888);
    SDL_Renderer *r = surf ? SDL_CreateSoftwareRenderer(surf) : NULL;
    if (!r) return 1;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    void *st = sv->create(r, w, h);
    const float dt = 1.0f / fps;
    for (float t = 0; t < seconds; t += dt) sv->step(st, dt);
    char path[1100];
    for (int i = 0; i < frames; i++) {
        sv->step(st, dt);
        sv->draw(st, r);
        SDL_RenderPresent(r);
        snprintf(path, sizeof path, "%s/frame%04d.png", dir, i);
        if (write_png(path, w, h, surf->pixels) != 0) return 1;
    }
    sv->destroy(st);
    SDL_DestroyRenderer(r);
    SDL_FreeSurface(surf);
    return 0;
}

static int snapshot(const char *path, const char *what, int w, int h, float seconds) {
    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA8888);
    SDL_Renderer *r = surf ? SDL_CreateSoftwareRenderer(surf) : NULL;
    if (!r) { fprintf(stderr, "snapshot: %s\n", SDL_GetError()); return 1; }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    g_show_info = getenv("TOASTSAVER_INFO") != NULL;   /* for snapshots: draws the saver's read-out */
    const float dt = 1.0f / 30;
    if (!strcmp(what, "sprites")) {                       /* scaled-up sprite sheet, own size */
        Canvas sh = toasters_sheet();
        int z = 4, rc;
        Canvas big = canvas_new(sh.w * z, sh.h * z);
        for (int i = 0; i < big.w * big.h; i++) {
            uint32_t p = get(&sh, (i % big.w) / z, (i / big.w) / z);
            big.px[i] = p ? p : 0x101030FF;
        }
        rc = write_png(path, big.w, big.h, big.px);
        free(sh.px); free(big.px);
        SDL_DestroyRenderer(r);
        SDL_FreeSurface(surf);
        return rc == 0 ? 0 : 1;
    }
    if (!strcmp(what, "fonttest")) {
        font_test(r, w, h);
    } else if (!strcmp(what, "settings")) {
        void *p = settings_create(r, w, h);
        for (float t = 0; t < seconds; t += dt) settings_step(p, dt);
        settings_draw(p, r);
        settings_destroy(p);
    } else if (!strcmp(what, "picker")) {
        void *p = picker_create(r, w, h);
        for (float t = 0; t < seconds; t += dt) picker_step(p, dt);
        picker_draw(p, r);
        picker_destroy(p);
    } else {
        const Saver *sv = strcmp(what, "random") ? saver_find(what) : saver_resolve(what);
        if (!sv) { fprintf(stderr, "unknown saver: %s\n", what); return 2; }
        if (!strcmp(what, "random")) printf("random: %s\n", sv->id);
        void *st = sv->create(r, w, h);
        clock_reset(w, h);
        for (float t = 0; t < seconds; t += dt) { sv->step(st, dt); clock_step(dt); }
        sv->draw(st, r);
        if (g_config.clock) clock_draw(r, w, h);
        sv->destroy(st);
    }
    SDL_RenderPresent(r);
    int rc = write_png(path, w, h, surf->pixels);
    SDL_DestroyRenderer(r);
    SDL_FreeSurface(surf);
    return rc == 0 ? 0 : 1;
}

/* Diagnostic (not in --help): average step/draw time per saver on a hidden window. */
static int bench(const char *which, int w, int h, int frames) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "bench: %s\n", SDL_GetError()); return 1; }
    SDL_Window *win = SDL_CreateWindow("bench", 0, 0, 64, 64, SDL_WINDOW_HIDDEN);
    SDL_Renderer *r = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE) : NULL;
    if (win && !r) r = SDL_CreateRenderer(win, -1, 0);
    SDL_Texture *target = r ? SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, w, h) : NULL;
    if (!target || SDL_SetRenderTarget(r, target) != 0) {
        fprintf(stderr, "bench: no render target: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

    const double msPerTick = 1000.0 / (double)SDL_GetPerformanceFrequency();
    for (int k = 0; k < SAVER_COUNT; k++) {
        const Saver *sv = SAVERS[k];
        if (strcmp(which, "all") && strcmp(which, sv->id)) continue;
        srand(1);
        void *st = sv->create(r, w, h);
        double stepMs = 0, drawMs = 0, worstMs = 0;
        for (int f = 0; f < frames; f++) {
            Uint64 t0 = SDL_GetPerformanceCounter();
            sv->step(st, 1 / 60.0f);
            Uint64 t1 = SDL_GetPerformanceCounter();
            sv->draw(st, r);
            SDL_RenderFlush(r);
            Uint64 t2 = SDL_GetPerformanceCounter();
            stepMs += (t1 - t0) * msPerTick;
            drawMs += (t2 - t1) * msPerTick;
            if ((t2 - t0) * msPerTick > worstMs) worstMs = (t2 - t0) * msPerTick;
        }
        sv->destroy(st);
        printf("%-10s step %6.3f  draw %6.3f  worst %6.2f ms/frame\n",
               sv->id, stepMs / frames, drawMs / frames, worstMs);
    }
    SDL_DestroyTexture(target);
    SDL_DestroyRenderer(r);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

/* Copies the Screensavers menu entry onto the user's desktop (packages can't do this). */
static int desktop_icon(void) {
    const char *home = getenv("HOME");
    char dst[1024], line[512];
    FILE *in = fopen(TOASTSAVER_DATADIR "/applications/toastsaver-picker.desktop", "r"), *out;
    if (!in || !home) { fprintf(stderr, "toastsaver: menu entry not found; is toastsaver installed?\n"); return 1; }
    snprintf(dst, sizeof dst, "%s/Desktop", home);
    mkdir(dst, 0755);
    snprintf(dst, sizeof dst, "%s/Desktop/Screensavers.desktop", home);
    if (!(out = fopen(dst, "w"))) { fclose(in); fprintf(stderr, "toastsaver: cannot write %s\n", dst); return 1; }
    while (fgets(line, sizeof line, in)) fputs(line, out);
    fclose(in);
    fclose(out);
    chmod(dst, 0755);   /* file managers only launch executable .desktop files */
    printf("added %s\n", dst);
    return 0;
}

static int write_icon(const char *path) {
    const int S = 128;
    Canvas icon = canvas_new(S, S), t = make_toaster(1);
    fill_round_rect(&icon, 4, 4, S - 5, S - 5, 24, rgb(0x2B3A8C));
    fill_round_rect(&icon, 4, 4, S - 5, S / 2, 24, rgb(0x3B4FB0));
    fill_rect(&icon, 4, S / 2 - 10, S - 5, S / 2, rgb(0x3B4FB0));
    int ox = (S - t.w * 2) / 2, oy = (S - t.h * 2) / 2 + 4;
    for (int y = 0; y < t.h * 2; y++)
        for (int x = 0; x < t.w * 2; x++) {
            uint32_t p = get(&t, x / 2, y / 2);
            if (p) put(&icon, ox + x, oy + y, p);
        }
    int rc = write_png(path, S, S, icon.px);
    free(icon.px);
    free(t.px);
    return rc;
}

/* ---------- main ---------- */

static void usage(void) {
    printf("toastsaver %s - screensavers\n\n"
           "  toastsaver [SAVER]          run fullscreen (default: the one in %s)\n"
           "  toastsaver --picker         open the app: big tiles to pick a saver; the gear opens settings\n"
           "  toastsaver --set KEY=VALUE  change a setting, e.g. --set CLOCK=1\n"
           "  toastsaver --desktop-icon   put a Screensavers icon on the desktop\n"
           "  toastsaver --list           list savers\n"
           "  toastsaver --windowed ...   use a window: clicks and keys no longer quit it. S = screenshot\n"
           "                              (saved to ~/Pictures), Space = pause, [ ] = slower/faster (or drag the\n"
           "                              slider), R = restart, I = info, Esc or Q = quit\n\n"
           "Savers: random", VERSION, config_path());
    for (int i = 0; i < SAVER_COUNT; i++) printf(", %s", SAVERS[i]->id);
    printf("\n\nrandom picks a different saver each time from RANDOM_SAVERS, e.g.\n"
           "  toastsaver --set RANDOM_SAVERS=toasters,pipes,lava   (empty = all savers)\n");
}

int main(int argc, char **argv) {
    int windowed = 0, picker = 0, settings = 0, inhibit = 0, sw = 1280, sh = 720;
    float seconds = 4;
    const char *snap = NULL, *which = NULL, *icon = NULL, *frameDir = NULL;
    int frames = 0, fps = 15, bench_mode = 0, benchFrames = 600, sizeSet = 0;
    const char *base = strrchr(argv[0], '/');
    base = base ? base + 1 : argv[0];
    if (!strcmp(base, "toastsaver-picker")) picker = 1;
    if (!strcmp(base, "toastsaver-settings")) settings = 1;

    srand((unsigned)time(NULL));
    config_load();

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--windowed")) windowed = 1;
        else if (!strcmp(a, "--picker")) picker = 1;
        else if (!strcmp(a, "--settings")) settings = 1;
        else if (!strcmp(a, "--clock")) g_config.clock = 1;   /* for snapshots */
        else if (!strcmp(a, "--set") && i + 1 < argc) {
            if (!config_apply(argv[++i])) { fprintf(stderr, "toastsaver: unknown setting '%s'\n", argv[i]); return 2; }
            if (config_save() != 0) { fprintf(stderr, "toastsaver: cannot write %s\n", config_path()); return 1; }
            printf("saved %s\n", argv[i]);
            return 0;
        }
        else if (!strcmp(a, "--inhibit")) inhibit = 1;   /* test only: behave like a video player */
        else if (!strcmp(a, "--list")) {
            for (int k = 0; k < SAVER_COUNT; k++) printf("%s\n", SAVERS[k]->id);
            return 0;
        }
        else if (!strcmp(a, "--version")) { puts(VERSION); return 0; }
        else if (!strcmp(a, "--desktop-icon")) return desktop_icon();
        else if (!strcmp(a, "--help") || !strcmp(a, "-h")) { usage(); return 0; }
        else if (!strcmp(a, "--snapshot") && i + 1 < argc) snap = argv[++i];
        else if (!strcmp(a, "--frames") && i + 2 < argc) { frameDir = argv[++i]; frames = atoi(argv[++i]); }
        else if (!strcmp(a, "--fps") && i + 1 < argc) fps = atoi(argv[++i]);
        else if (!strcmp(a, "--size") && i + 1 < argc) { sscanf(argv[++i], "%dx%d", &sw, &sh); sizeSet = 1; }
        else if (!strcmp(a, "--bench")) {   /* --bench [SAVER|all] [FRAMES] */
            bench_mode = 1;
            if (i + 1 < argc && argv[i + 1][0] != '-') which = argv[++i];
            if (i + 1 < argc && argv[i + 1][0] != '-') benchFrames = atoi(argv[++i]);
        }
        else if (!strcmp(a, "--seconds") && i + 1 < argc) seconds = (float)atof(argv[++i]);
        else if (!strcmp(a, "--name") && i + 1 < argc) config_set_name(argv[++i]);
        else if (!strcmp(a, "--write-icon") && i + 1 < argc) icon = argv[++i];
        else if (a[0] != '-') which = a;
        else { usage(); return 2; }
    }

    if (icon) return write_icon(icon) == 0 ? 0 : 1;
    if (frameDir) {
        if (SDL_Init(0) != 0) return 1;
        int rc = sequence(frameDir, which ? which : "toasters", sw, sh, seconds, frames, fps > 0 ? fps : 15);
        SDL_Quit();
        return rc;
    }
    if (snap) {
        if (SDL_Init(0) != 0) return 1;
        int rc = snapshot(snap, picker ? "picker" : settings ? "settings" : (which ? which : "toasters"), sw, sh, seconds);
        SDL_Quit();
        return rc;
    }
    if (bench_mode) {
        if (which && strcmp(which, "all") && !saver_find(which)) {
            fprintf(stderr, "toastsaver: unknown saver '%s' (try --list)\n", which);
            return 2;
        }
        setvbuf(stdout, NULL, _IONBF, 0);
        if (benchFrames < 1) benchFrames = 1;
        return bench(which ? which : "all", sizeSet ? sw : 1920, sizeSet ? sh : 1080, benchFrames);
    }
    if (which && strcmp(which, "random") && !saver_find(which)) {
        fprintf(stderr, "toastsaver: unknown saver '%s' (try --list)\n", which);
        return 2;
    }

    /* We are the idle state: don't stop the screen from blanking later.
       (--inhibit flips this, so tests can play the part of a video player.) */
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, inhibit ? "0" : "1");
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "toastsaver: SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    if (inhibit) SDL_DisableScreenSaver();

    Uint32 flags = SDL_WINDOW_SHOWN | (windowed ? SDL_WINDOW_RESIZABLE : SDL_WINDOW_FULLSCREEN_DESKTOP);
    const char *title = picker || settings ? "Screensavers" : "toastsaver";
    SDL_Window *win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 800, flags);
    if (!win) {
        fprintf(stderr, "toastsaver: cannot open window: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_Renderer *r = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!r) r = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!r) {
        fprintf(stderr, "toastsaver: no renderer: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

    if (picker || settings) run_picker(win, r);   /* both start on the tiles */
    else run_saver(win, r, saver_resolve(which ? which : g_config.saver), windowed);

    SDL_DestroyRenderer(r);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
