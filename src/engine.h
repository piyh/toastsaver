/* Shared engine for the toastsaver collection. */
#ifndef ENGINE_H
#define ENGINE_H

#include <SDL.h>
#include <stdint.h>

#define PI_F 3.14159265f
#define VERSION "1.0.0"

/* Install locations, passed in by the Makefile. */
#ifndef TOASTSAVER_BINDIR
#define TOASTSAVER_BINDIR "/usr/local/bin"
#endif
#ifndef TOASTSAVER_DATADIR
#define TOASTSAVER_DATADIR "/usr/local/share"
#endif

/* ---------- settings (~/.config/toastsaver.conf) ---------- */

typedef struct {
    int enabled;             /* ENABLED: 0 turns the screensaver off */
    char saver[32];          /* saver id or "random" */
    char name[16];           /* shown by the NAME saver */
    int idleMinutes;         /* IDLE_MINUTES: wait before the screensaver */
    int screenOffMinutes;    /* SCREEN_OFF_MINUTES: display off after (0 = never) */
    int clock;               /* CLOCK: 1 shows a drifting clock over any saver */
    int clock24;             /* CLOCK_24H: 1 for 24-hour time */
} Config;

extern Config g_config;
const char *config_path(void);
void config_load(void);
int config_apply(const char *line);  /* one KEY=value line; 0 if unknown */
int config_save(void);              /* rewrites known keys, keeps comments and other lines */
void config_set_name(const char *s); /* uppercases and keeps only safe characters */

/* ---------- random ---------- */

float frand(float a, float b);
int irand(int n);

/* ---------- pixel-art canvas (0xRRGGBBAA, 0 = transparent) ---------- */

typedef struct {
    int w, h;
    uint32_t *px;
} Canvas;

uint32_t rgb(uint32_t hex);
Canvas canvas_new(int w, int h);
void put(Canvas *c, int x, int y, uint32_t col);
uint32_t get(const Canvas *c, int x, int y);
void fill_rect(Canvas *c, int x0, int y0, int x1, int y1, uint32_t col);
void fill_ellipse(Canvas *c, float cx, float cy, float rx, float ry, uint32_t col);
void fill_round_rect(Canvas *c, int x0, int y0, int x1, int y1, int r, uint32_t col);
void fill_poly(Canvas *c, const float *xs, const float *ys, int n, uint32_t col);
void outline(Canvas *c, uint32_t col);
SDL_Texture *canvas_texture(SDL_Renderer *r, Canvas *c); /* frees the canvas pixels */
int write_png(const char *path, int w, int h, const uint32_t *px);

/* ---------- renderer helpers ---------- */

SDL_Color hsv(float h, float s, float v);
SDL_Color hexcol(uint32_t hex);
void set_color(SDL_Renderer *r, SDL_Color c);
void thick_line(SDL_Renderer *r, float x0, float y0, float x1, float y1, float width, SDL_Color c);
void vgradient(SDL_Renderer *r, int w, int h, SDL_Color top, SDL_Color bottom);
void fill_circle(SDL_Renderer *r, float cx, float cy, float rad, SDL_Color c);
void fill_rrect(SDL_Renderer *r, SDL_Rect rc, int rad, SDL_Color c);
void fill_triangle(SDL_Renderer *r, float x0, float y0, float x1, float y1, float x2, float y2, SDL_Color c);

/* ---------- 5x7 pixel font ---------- */

#define FONT_W 5
#define FONT_H 7
const char *const *font_glyph(char ch);                 /* 7 rows of 5 chars, or NULL */
int text_width(const char *s, int scale);               /* in pixels */
void draw_text(SDL_Renderer *r, const char *s, int x, int y, int scale, SDL_Color c);
void draw_text_shadow(SDL_Renderer *r, const char *s, int x, int y, int scale, SDL_Color c);
Canvas text_canvas(const char *s, uint32_t col);         /* 1 canvas pixel per font dot */

/* ---------- clock overlay ---------- */

void clock_reset(int w, int h);
void clock_step(float dt);
void clock_draw(SDL_Renderer *r, int w, int h);

/* ---------- screensavers ---------- */

typedef struct Saver {
    const char *id;      /* used in the config file and on the command line */
    const char *label;   /* one short word shown to kids */
    void *(*create)(SDL_Renderer *r, int w, int h);
    void (*step)(void *s, float dt);
    void (*draw)(void *s, SDL_Renderer *r);  /* draws 0,0..w,h of the current viewport */
    void (*destroy)(void *s);
} Saver;

extern const Saver saver_toasters, saver_pipes, saver_starfield, saver_mystify,
                   saver_matrix, saver_aquarium, saver_lava, saver_name,
                   saver_rainbow, saver_books;
extern const Saver *const SAVERS[];
extern const int SAVER_COUNT;
const Saver *saver_find(const char *id);   /* NULL if unknown */
const Saver *saver_resolve(const char *id); /* handles "random"; falls back to toasters */

/* ---------- runners ---------- */

/* Runs a saver in the window until input (after a short grace period). Returns 1 if the
   whole app should quit (window closed / SIGTERM), 0 if it was dismissed by input. */
int run_saver(SDL_Window *win, SDL_Renderer *r, const Saver *sv, int windowed);
int run_picker(SDL_Window *win, SDL_Renderer *r);
int run_settings(SDL_Window *win, SDL_Renderer *r);
void *settings_create(SDL_Renderer *r, int w, int h);
void settings_step(void *p, float dt);
void settings_draw(void *p, SDL_Renderer *r);
void settings_destroy(void *p);

/* For tests: render the picker or a saver offscreen. */
void *picker_create(SDL_Renderer *r, int w, int h);
void picker_step(void *p, float dt);
void picker_draw(void *p, SDL_Renderer *r);
void picker_destroy(void *p);

#endif
