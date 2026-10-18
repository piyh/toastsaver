/* BOOKS: placeholder until the bookshelf saver lands. */
#include "../engine.h"
#include <stdlib.h>

typedef struct { int w, h; } Books;

static void *create(SDL_Renderer *r, int w, int h) {
    (void)r;
    Books *b = calloc(1, sizeof *b);
    b->w = w;
    b->h = h;
    return b;
}
static void step(void *p, float dt) { (void)p; (void)dt; }
static void draw(void *p, SDL_Renderer *r) {
    (void)p;
    SDL_SetRenderDrawColor(r, 0x3B, 0x24, 0x16, 255);
    SDL_RenderFillRect(r, NULL);
}
static void destroy(void *p) { free(p); }

const Saver saver_books = { "books", "BOOKS", create, step, draw, destroy };
