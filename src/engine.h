/* Shared engine for the toastsaver collection. */
#ifndef ENGINE_H
#define ENGINE_H

#include <SDL.h>
#include <stdint.h>

#define PI_F 3.14159265f
#define VERSION "1.11.1"

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
    int fps;                 /* FPS: frame-rate cap for savers, 30 by default (0 = as fast as the display) */
    char randomSavers[256];  /* RANDOM_SAVERS: ids SURPRISE picks from ("" = every saver) */
    int cycleMinutes;        /* CYCLE_MINUTES: SURPRISE moves on to another saver this often (0 = never) */
    int previewSavers;       /* PREVIEW_SAVERS: 1 shows savers still being finished (not written to new files) */
} Config;
/* Settings that belong to one saver (BOOK_*, GERM_*) are not here: each saver declares its own
   in a SaverOption table (below), and the settings file, --set, the settings page and the
   windowed-mode panel all work from those tables. */

#define CONFIG_MAX_MINUTES 9999   /* the most any minutes setting can be */
extern Config g_config;
extern const Config CONFIG_DEFAULTS;
const char *config_path(void);
void config_load(void);
int config_apply(const char *line);  /* one KEY=value line; 0 if unknown */
int config_save(void);              /* rewrites known keys, keeps comments and other lines */
void config_set_name(const char *s); /* uppercases and keeps only safe characters */

/* ---------- settings rows (per-saver settings, and the settings pages) ---------- */

enum {
    OPT_TOGGLE = 1,    /* on/off instead of a number */
    OPT_RESTART = 2,   /* only read when the saver starts: changing it in a window starts the saver over */
    OPT_LINK = 4,      /* a page row that opens another page (`link`; NULL: the list of savers) */
    OPT_WHOLE = 8,     /* the page's live preview shows the whole saver (not its demo) while this row is picked */
};

/* One row of settings. A saver lists its own in a table ending with { 0 } (written with
   designated initializers); the value lives in the saver's file, and the saver reads *value
   whenever it likes (every frame is fine). The settings file, --set, the settings pages and
   the windowed panel all work from these tables. The main settings page is a table of these
   rows too (settings.c), with no `key`: those settings are written by config_save itself. */
typedef struct SaverOption {
    const char *key;     /* the settings-file key, without '=': "GERM_CELLS" (NULL on a page-only row) */
    const char *label;   /* a few capital words for the pages and the windowed panel; NULL ends a table */
    const char *help;    /* the "# ..." comment written next to it in a new settings file */
    int *value;
    int def, lo, hi;     /* default and range */
    int step;            /* - and + move by this much (past the end of `stops`, if it has them) */
    const int *stops;    /* optional: values - and + step through first (ascending), e.g. reading speeds */
    int nstops;
    const char *unit;    /* optional: shown after the number, e.g. "WPM" */
    int flags;           /* OPT_* */
    const char *group;   /* optional: a heading the pages put above this row (and the rows after it) */
    const char *zero;    /* optional: shown instead of 0, e.g. "NEVER" */
    const char *off, *on;   /* optional: a toggle's words (OFF and ON by default) */
    int onValue;         /* a toggle's value when on (1 by default; FPS is 30) */
    const struct Saver *link;   /* OPT_LINK: the saver whose page it opens (NULL: the list of savers) */
    const char *tip;            /* optional: a few words under the live preview while this row is picked */
    int (*live)(void);          /* optional: 0 dims the row and it can't be changed */
    void (*changed)(const struct SaverOption *o, int old, int dir);   /* optional: after a change on a page;
                                                                         dir: the - or + that did it, 0 if typed */
} SaverOption;

#define OPTION_MAX 32    /* the most settings one saver (or page) can have */
struct Saver;
int option_count(const struct Saver *sv);                 /* how many settings the saver has */
void option_set(const SaverOption *o, int v);             /* clamps to lo..hi */
int option_step(const SaverOption *o, int v, int dir);    /* the next value up (dir > 0) or down */
int option_on(const SaverOption *o);                      /* a toggle's value when on */
void option_reset(const struct Saver *sv);                /* every setting of the saver back to its default */
int option_is_default(const struct Saver *sv);            /* 1 if nothing has been changed */
const SaverOption *option_find(const char *key, const struct Saver **owner);   /* by key, NULL if none */
void option_format(const SaverOption *o, int v, char *out, size_t n);         /* "75 WPM", "ON", "6" */

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

#define CLEAR_MAX 32   /* the most keep-clear areas a saver reports */
void clock_reset(int w, int h);
void clock_step(float dt, const SDL_Rect *avoid, int n);   /* avoid: areas the clock stays off */
void clock_draw(SDL_Renderer *r, int w, int h);

/* Windowed mode: I toggles a read-out of what the saver is doing (savers may draw it). */
extern int g_show_info;
extern int g_force_egg;    /* windowed: E asks the saver to show an easter egg now (it clears this) */
extern int g_force_boom;   /* windowed: B asks the saver for its big moment now, e.g. the volcano's finale (it clears this) */
extern int g_rush;         /* windowed: F fast-forwards until the saver's big moment, then normal speed (the saver clears it) */

/* ---------- screensavers ---------- */

typedef struct Saver {
    const char *id;      /* used in the config file and on the command line */
    const char *label;   /* one short word shown to kids */
    void *(*create)(SDL_Renderer *r, int w, int h);
    void (*step)(void *s, float dt);
    void (*draw)(void *s, SDL_Renderer *r);  /* draws 0,0..w,h of the current viewport */
    void (*destroy)(void *s);
    int (*at_rest)(void *s);  /* optional: 1 at a natural place to cut away (SURPRISE rotation waits for it) */
    int (*keep_clear)(void *s, SDL_Rect *out, int max);  /* optional: screen areas showing numbers, which
                                                            the clock keeps off; returns how many */
    const char *keys;    /* optional: the windowed-mode keys it answers to beyond the usual ones, as "E EGG   B BOOM":
                            a letter, a space, a label, three spaces between keys. Other letters do nothing. */
    const SaverOption *options;   /* optional: its own settings, ending with { 0 } (see SaverOption) */
    /* optional, for the live preview on its settings page and the windowed panel: */
    void *(*demo)(SDL_Renderer *r, int w, int h);   /* a small, close-up scene that shows the settings at work */
    void (*explain)(void *s, SDL_Renderer *r, const SaverOption *focus);   /* draws over a frame what `focus`
                                                     does: how far things see, how far bubbles fly... (NULL: nothing) */
} Saver;

extern const Saver saver_toasters, saver_pipes, saver_starfield, saver_mystify,
                   saver_matrix, saver_aquarium, saver_lava, saver_name,
                   saver_rainbow, saver_books, saver_birds,
                   saver_dice, saver_volcano, saver_city, saver_germs;
extern const Saver saver_water, saver_butterfly, saver_word;
extern const Saver *SAVERS[];      /* the savers on show: see saver_list_init */
extern int SAVER_COUNT;
const Saver *const *saver_all(int *count);   /* every saver, previews too (their settings are kept even while hidden) */
void saver_list_init(void);        /* fills SAVERS from the settings (PREVIEW_SAVERS) */
const Saver *saver_find(const char *id);   /* NULL if unknown */
const Saver *saver_resolve(const char *id); /* handles "random"; falls back to toasters */
int saver_in_mix(const Saver *sv);          /* can SURPRISE ("random") pick it? */
int saver_set_in_mix(const Saver *sv, int on); /* 0 (and no change) if that would empty the mix */

/* ---------- runners ---------- */

/* Runs a saver in the window until input (after a short grace period). Returns 1 if the
   whole app should quit (window closed / SIGTERM), 0 if it was dismissed by input. */
int run_saver(SDL_Window *win, SDL_Renderer *r, const Saver *sv, int windowed, int cycle);
int run_picker(SDL_Window *win, SDL_Renderer *r);   /* the app window */
int run_settings(SDL_Window *win, SDL_Renderer *r);
struct PageSpec;
const struct PageSpec *settings_spec(void);   /* the main settings page's rows (settings.c) */

/* A settings page drawn from a table of rows (options.c): headings, - and + or a switch on
   each row, a number you can tap to type in, rows that open other pages, scrolling when
   they don't fit, and TEST IT, RESET (tap twice) and BACK along the bottom. */
typedef struct PageSpec {
    const char *title;
    const SaverOption *rows;               /* ending with { 0 } */
    int buttons;                           /* 1: BACK only; else TEST IT, RESET and BACK */
    const Saver *test;                     /* what TEST IT runs (NULL: the screensaver as chosen) */
    void (*reset)(void);                   /* what RESET does (then it saves) */
    const char *resetNote;                 /* the line at the bottom while RESET waits for SURE? */
    const char *(*note)(void);             /* the line at the bottom otherwise (NULL: the usual one) */
} PageSpec;
int run_page(SDL_Window *win, SDL_Renderer *r, const PageSpec *spec);   /* returns 1 to quit the app */
void *page_create(SDL_Renderer *r, int w, int h, const PageSpec *spec);  /* for snapshots */
void page_draw(void *p, SDL_Renderer *r);
void page_step(void *p, float dt);
void page_destroy(void *p);
/* A saver's own settings page (sv = NULL: the list of savers that have settings). Returns 1 to quit the app. */
int run_saver_options(SDL_Window *win, SDL_Renderer *r, const Saver *sv);
void *options_page_create(SDL_Renderer *r, int w, int h, const Saver *sv);   /* for snapshots */

/* Windowed mode: a panel of the saver's own settings, opened with O. Changes apply at once;
   SAVE writes them to the settings file, DEFAULTS puts them back. */
typedef struct {
    int open, sel;
    int saved[OPTION_MAX];      /* the values as they are in the settings file, to mark what changed */
    float savedFlash;           /* "SAVED" shows for a moment */
    SDL_Rect row[OPTION_MAX], minus[OPTION_MAX], plus[OPTION_MAX], box, save, defaults;
} OptionPanel;
void option_panel_init(OptionPanel *p, const Saver *sv);
/* Both return -1 if the key or click wasn't the panel's, 1 if the saver must start over
   (an OPT_RESTART setting changed), else 0. */
int option_panel_key(OptionPanel *p, const Saver *sv, SDL_Keycode k);
int option_panel_click(OptionPanel *p, const Saver *sv, int x, int y);
void option_panel_draw(OptionPanel *p, const Saver *sv, SDL_Renderer *r, int w, int h, float dt);

/* Shared look of the settings pages. */
void ui_text_at(SDL_Renderer *r, const char *t, int x, int cy, int sc, SDL_Color c);   /* cy: vertical middle */
void ui_round_button(SDL_Renderer *r, SDL_Rect b, int plus, int enabled);
void ui_toggle(SDL_Renderer *r, SDL_Rect t, int on, int enabled);
void ui_button(SDL_Renderer *r, SDL_Rect b, const char *label, uint32_t col, uint32_t dark);
int ui_hit(SDL_Rect rc, int x, int y);

/* For tests: render the picker or a saver offscreen. */
void *picker_create(SDL_Renderer *r, int w, int h);
void picker_step(void *p, float dt);
int picker_hit_heart(void *p, int x, int y);     /* tile whose heart is at x,y, or -1 */
int picker_hit_picture(void *p, int x, int y);   /* tile whose picture (tap to watch) is at x,y, or -1 */
void picker_draw(void *p, SDL_Renderer *r);
void picker_destroy(void *p);

#endif
