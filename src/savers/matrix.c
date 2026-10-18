/* Matrix rain: falling green code. Now and then a column spells the NAME setting. */
#include "../engine.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SYMBOLS 30          /* made-up "alien" glyphs after the digits */
#define LETTERS_AT 10       /* atlas: 0-9, then symbols, then A-Z */
#define ALPHA_AT (LETTERS_AT + SYMBOLS)

typedef struct {
    float head, speed;
    int len, nameRow;       /* nameRow >= 0: this column spells the name from that row */
} Column;

typedef struct {
    int w, h, sc, cw, ch, cols, rows;
    Column *col;
    unsigned char *glyph;   /* atlas index per cell */
    SDL_Texture *atlas;
    float mutate;
} Matrix;

static Canvas build_atlas(void) {
    int n = ALPHA_AT + 26;
    Canvas c = canvas_new(n * (FONT_W + 1), FONT_H);
    unsigned seed = 7;
    for (int g = 0; g < n; g++) {
        int ox = g * (FONT_W + 1);
        const char *const *rows = NULL;
        if (g < LETTERS_AT) rows = font_glyph((char)('0' + g));
        else if (g >= ALPHA_AT) rows = font_glyph((char)('A' + g - ALPHA_AT));
        if (rows) {
            for (int y = 0; y < FONT_H; y++)
                for (int x = 0; x < FONT_W; x++)
                    if (rows[y][x] == '#') put(&c, ox + x, y, 0xFFFFFFFF);
        } else {
            /* Mirrored random dots look like characters from an unknown alphabet. */
            for (int y = 0; y < FONT_H; y++)
                for (int x = 0; x < 3; x++) {
                    seed = seed * 1103515245u + 12345u;
                    if ((seed >> 16) % 100 < 45) {
                        put(&c, ox + x, y, 0xFFFFFFFF);
                        if ((seed >> 8) % 3) put(&c, ox + FONT_W - 1 - x, y, 0xFFFFFFFF);
                    }
                }
        }
    }
    return c;
}

static int random_glyph(void) { return irand(LETTERS_AT + SYMBOLS); }

static void reset_column(Matrix *m, int i, int start) {
    Column *c = &m->col[i];
    c->speed = frand(7.0f, 20.0f);
    c->len = 8 + irand(m->rows);
    c->head = start ? frand(-(float)m->rows, (float)m->rows) : -frand(0, (float)m->rows * 0.5f);
    c->nameRow = -1;
    int nl = (int)strlen(g_config.name);
    if (nl > 0 && nl < m->rows - 2 && irand(26) == 0) {
        c->nameRow = irand(m->rows - nl);
        for (int k = 0; k < nl; k++) {
            char ch = g_config.name[k];
            int idx = (ch >= 'A' && ch <= 'Z') ? ALPHA_AT + ch - 'A'
                    : (ch >= '0' && ch <= '9') ? ch - '0' : random_glyph();
            m->glyph[(c->nameRow + k) * m->cols + i] = (unsigned char)idx;
        }
    }
}

static void *create(SDL_Renderer *r, int w, int h) {
    Matrix *m = calloc(1, sizeof *m);
    m->w = w;
    m->h = h;
    m->sc = h / 270 > 1 ? h / 270 : 1;
    m->cw = (FONT_W + 2) * m->sc;
    m->ch = (FONT_H + 2) * m->sc;
    m->cols = w / m->cw + 1;
    m->rows = h / m->ch + 1;
    m->col = calloc((size_t)m->cols, sizeof(Column));
    m->glyph = malloc((size_t)m->cols * m->rows);
    for (int i = 0; i < m->cols * m->rows; i++) m->glyph[i] = (unsigned char)random_glyph();
    for (int i = 0; i < m->cols; i++) reset_column(m, i, 1);
    Canvas a = build_atlas();
    m->atlas = canvas_texture(r, &a);
    return m;
}

static void step(void *p, float dt) {
    Matrix *m = p;
    for (int i = 0; i < m->cols; i++) {
        Column *c = &m->col[i];
        c->head += c->speed * dt;
        if (c->head - c->len > m->rows) reset_column(m, i, 0);
    }
    m->mutate += dt * m->cols * m->rows * 0.04f;     /* a few cells flicker each frame */
    for (; m->mutate >= 1; m->mutate -= 1) {
        int cell = irand(m->cols * m->rows), col = cell % m->cols, row = cell / m->cols;
        Column *c = &m->col[col];
        if (c->nameRow >= 0 && row >= c->nameRow && row < c->nameRow + (int)strlen(g_config.name)) continue;
        m->glyph[cell] = (unsigned char)random_glyph();
    }
}

static void draw(void *p, SDL_Renderer *r) {
    Matrix *m = p;
    SDL_SetRenderDrawColor(r, 0, 4, 0, 255);
    SDL_RenderFillRect(r, NULL);
    for (int i = 0; i < m->cols; i++) {
        Column *c = &m->col[i];
        int top = (int)floorf(c->head) - c->len, bottom = (int)floorf(c->head);
        for (int row = top < 0 ? 0 : top; row <= bottom && row < m->rows; row++) {
            int g = m->glyph[row * m->cols + i];
            float t = 1.0f - (float)(bottom - row) / c->len;  /* 1 at the head */
            int isName = c->nameRow >= 0 && g >= ALPHA_AT;
            if (row == bottom) SDL_SetTextureColorMod(m->atlas, 210, 255, 210);
            else if (isName) SDL_SetTextureColorMod(m->atlas, 150, 255, 120);
            else SDL_SetTextureColorMod(m->atlas, 0, (Uint8)(60 + 195 * t), (Uint8)(50 * t));
            SDL_SetTextureAlphaMod(m->atlas, (Uint8)(isName ? 255 : 60 + 195 * t));
            SDL_Rect src = { g * (FONT_W + 1), 0, FONT_W, FONT_H };
            SDL_Rect dst = { i * m->cw, row * m->ch, FONT_W * m->sc, FONT_H * m->sc };
            SDL_RenderCopy(r, m->atlas, &src, &dst);
        }
    }
}

static void destroy(void *p) {
    Matrix *m = p;
    SDL_DestroyTexture(m->atlas);
    free(m->col);
    free(m->glyph);
    free(m);
}

const Saver saver_matrix = { "matrix", "MATRIX", create, step, draw, destroy };
