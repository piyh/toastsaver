/*
 * toastsaver - a collection of screensavers for Raspberry Pi OS (Wayland).
 *
 *   toastsaver                 run the saver chosen in the settings (fullscreen)
 *   toastsaver pipes           run a specific saver
 *   toastsaver --picker        open the kid-friendly picker (also: toastsaver-picker)
 */
#include "engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

const Saver *const SAVERS[] = {
    &saver_toasters, &saver_pipes, &saver_starfield, &saver_mystify,
    &saver_matrix, &saver_aquarium, &saver_lava, &saver_name,
};
const int SAVER_COUNT = sizeof SAVERS / sizeof SAVERS[0];

const Saver *saver_find(const char *id) {
    for (int i = 0; i < SAVER_COUNT; i++)
        if (!strcmp(SAVERS[i]->id, id)) return SAVERS[i];
    return NULL;
}

const Saver *saver_resolve(const char *id) {
    if (!strcmp(id, "random")) return SAVERS[irand(SAVER_COUNT)];
    const Saver *s = saver_find(id);
    return s ? s : &saver_toasters;
}

/* ---------- fullscreen runner ---------- */

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
    float moved = 0;
    int result = -1;
    (void)win;

    while (result < 0) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            /* Ignore input for a moment: the click/keypress that started us is still arriving. */
            int armed = SDL_GetTicks() - started > 1200;
            switch (e.type) {
            case SDL_QUIT: result = 1; break;
            case SDL_KEYDOWN:
                if (armed || e.key.keysym.sym == SDLK_ESCAPE) result = 0;
                break;
            case SDL_MOUSEBUTTONDOWN:
            case SDL_FINGERDOWN:
                if (armed) result = 0;
                break;
            case SDL_MOUSEMOTION:
                if (armed && !windowed) {
                    moved += abs(e.motion.xrel) + abs(e.motion.yrel);
                    if (moved > 24) result = 0;
                }
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
        sv->step(st, dt);
        SDL_RenderSetViewport(r, NULL);
        sv->draw(st, r);
        if (g_config.clock) {
            clock_step(dt);
            clock_draw(r, w, h);
        }
        SDL_RenderPresent(r);
        SDL_Delay(8); /* vsync paces us when available; this keeps CPU sane otherwise */
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
        const Saver *sv = saver_find(what);
        if (!sv) { fprintf(stderr, "unknown saver: %s\n", what); return 2; }
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
           "  toastsaver --picker         open the picker (big tiles, for kids)\n"
           "  toastsaver --settings       open the settings window (for grown-ups)\n"
           "  toastsaver --set KEY=VALUE  change a setting, e.g. --set CLOCK=1\n"
           "  toastsaver --desktop-icon   put a Screensavers icon on the desktop\n"
           "  toastsaver --list           list savers\n"
           "  toastsaver --windowed ...   use a window instead of fullscreen\n\n"
           "Savers: random", VERSION, config_path());
    for (int i = 0; i < SAVER_COUNT; i++) printf(", %s", SAVERS[i]->id);
    printf("\n");
}

int main(int argc, char **argv) {
    int windowed = 0, picker = 0, settings = 0, inhibit = 0, sw = 1280, sh = 720;
    float seconds = 4;
    const char *snap = NULL, *which = NULL, *icon = NULL, *frameDir = NULL;
    int frames = 0, fps = 15;
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
        else if (!strcmp(a, "--size") && i + 1 < argc) sscanf(argv[++i], "%dx%d", &sw, &sh);
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
    const char *title = picker ? "Screensavers" : settings ? "Screensaver Settings" : "toastsaver";
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

    if (settings) run_settings(win, r);
    else if (picker) run_picker(win, r);
    else run_saver(win, r, saver_resolve(which ? which : g_config.saver), windowed);

    SDL_DestroyRenderer(r);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
