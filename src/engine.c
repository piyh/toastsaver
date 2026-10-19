/* Shared helpers: settings, random numbers, pixel canvas, PNG output, drawing. */
#include "engine.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* ---------- settings ---------- */

Config g_config = { 1, "toasters", "HELLO", 5, 30, 0, 0, 0, "" };

const char *config_path(void) {
    static char path[1024];
    const char *xdg = getenv("XDG_CONFIG_HOME"), *home = getenv("HOME");
    if (xdg && *xdg) snprintf(path, sizeof path, "%s/toastsaver.conf", xdg);
    else snprintf(path, sizeof path, "%s/.config/toastsaver.conf", home ? home : ".");
    return path;
}

/* Only characters the font can draw and that are safe inside single quotes in sh. */
void config_set_name(const char *s) {
    size_t n = 0;
    for (; *s && n < sizeof g_config.name - 1; s++) {
        char c = (char)toupper((unsigned char)*s);
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || strchr(" !?-.*", c))
            g_config.name[n++] = c;
    }
    g_config.name[n] = 0;
}

static void read_value(const char *line, char *out, size_t size) {
    const char *v = strchr(line, '=') + 1;
    char quote = (*v == '\'' || *v == '"') ? *v++ : 0;
    size_t n = 0;
    while (*v && n < size - 1) {
        if (quote ? *v == quote : (*v == '#' || isspace((unsigned char)*v))) break;
        out[n++] = *v++;
    }
    out[n] = 0;
}

/* Applies one KEY=value line. Returns 0 if the key isn't one of ours. */
int config_apply(const char *line) {
    char v[256];
    if (!strchr(line, '=') || line[0] == '#') return 0;
    read_value(line, v, sizeof v);
    if (!strncmp(line, "ENABLED=", 8)) g_config.enabled = atoi(v) != 0;
    else if (!strncmp(line, "SAVER=", 6)) snprintf(g_config.saver, sizeof g_config.saver, "%.31s", v);
    else if (!strncmp(line, "NAME=", 5)) config_set_name(v);
    else if (!strncmp(line, "IDLE_MINUTES=", 13)) g_config.idleMinutes = atoi(v) > 0 ? atoi(v) : 5;
    else if (!strncmp(line, "SCREEN_OFF_MINUTES=", 19)) g_config.screenOffMinutes = atoi(v) > 0 ? atoi(v) : 0;
    else if (!strncmp(line, "CLOCK=", 6)) g_config.clock = atoi(v) != 0;
    else if (!strncmp(line, "CLOCK_24H=", 10)) g_config.clock24 = atoi(v) != 0;
    else if (!strncmp(line, "FPS=", 4)) g_config.fps = atoi(v) > 0 ? atoi(v) : 0;
    else if (!strncmp(line, "RANDOM_SAVERS=", 14)) snprintf(g_config.randomSavers, sizeof g_config.randomSavers, "%s", v);
    else return 0;
    return 1;
}

void config_load(void) {
    FILE *f = fopen(config_path(), "r");
    char line[512];
    if (!f) return;
    while (fgets(line, sizeof line, f)) config_apply(line);
    fclose(f);
}

/* Every setting we own, with the comment written next to it in a new file. */
#define NKEYS 9
static const char *const KEYS[NKEYS] = {
    "ENABLED=", "SAVER=", "NAME=", "IDLE_MINUTES=", "SCREEN_OFF_MINUTES=", "CLOCK=", "CLOCK_24H=",
    "FPS=", "RANDOM_SAVERS=" };
static const char *const HELP[NKEYS] = {
    "# 0 = turn the screensaver off",
    "# toasters rainbow pipes books dice aquarium birds starfield mystify matrix lava name random",
    "# shown by the name saver",
    "# minutes of no input before the screensaver appears",
    "# minutes before the display turns off (0 = never)",
    "# 1 = show a clock over the screensaver",
    "# 1 = 24-hour clock",
    "# 0 = as fast as the display; 30 saves power on a Pi",
    "# savers SAVER=random picks from, comma-separated (empty = all)" };

static void format_key(int k, char *out, size_t n) {
    switch (k) {
    case 0: snprintf(out, n, "ENABLED=%d", g_config.enabled); break;
    case 1: snprintf(out, n, "SAVER=%s", g_config.saver); break;
    case 2: snprintf(out, n, "NAME='%s'", g_config.name); break;
    case 3: snprintf(out, n, "IDLE_MINUTES=%d", g_config.idleMinutes); break;
    case 4: snprintf(out, n, "SCREEN_OFF_MINUTES=%d", g_config.screenOffMinutes); break;
    case 5: snprintf(out, n, "CLOCK=%d", g_config.clock); break;
    case 6: snprintf(out, n, "CLOCK_24H=%d", g_config.clock24); break;
    case 7: snprintf(out, n, "FPS=%d", g_config.fps); break;
    default: snprintf(out, n, "RANDOM_SAVERS=%s", g_config.randomSavers); break;
    }
}

static void write_key(FILE *out, int k, const char *comment) {
    char kv[300];
    format_key(k, kv, sizeof kv);
    int pad = 24 - (int)strlen(kv);
    if (comment) fprintf(out, "%s%*s%s%s", kv, pad > 1 ? pad : 1, "", comment, strchr(comment, '\n') ? "" : "\n");
    else fprintf(out, "%s\n", kv);
}

int config_save(void) {
    const char *path = config_path();
    char tmp[1100], dir[1100], line[512];
    int wrote[NKEYS] = { 0 };
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    snprintf(dir, sizeof dir, "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash) { *slash = 0; mkdir(dir, 0755); }   /* make sure ~/.config exists */
    FILE *in = fopen(path, "r"), *out = fopen(tmp, "w");
    if (!out) { if (in) fclose(in); return -1; }
    if (!in) fputs("# toastsaver settings. Screensaver Settings and the Screensavers picker change these for you.\n", out);
    while (in && fgets(line, sizeof line, in)) {
        int k = 0;
        while (k < NKEYS && strncmp(line, KEYS[k], strlen(KEYS[k]))) k++;
        if (k == NKEYS) { fputs(line, out); continue; }
        write_key(out, k, strchr(line, '#'));       /* keep a trailing "# explanation" */
        wrote[k] = 1;
    }
    for (int k = 0; k < NKEYS; k++)
        if (!wrote[k]) write_key(out, k, HELP[k]);
    if (in) fclose(in);
    if (fclose(out) != 0) return -1;
    return rename(tmp, path);
}

/* ---------- the SURPRISE mix (RANDOM_SAVERS) ---------- */

/* Is `id` one of the comma- or space-separated words in RANDOM_SAVERS? */
static int mix_lists(const char *id) {
    size_t n = strlen(id);
    for (const char *s = g_config.randomSavers; *s; ) {
        while (*s == ',' || *s == ' ') s++;
        size_t len = strcspn(s, ", ");
        if (len == n && !strncmp(s, id, n)) return 1;
        s += len;
    }
    return 0;
}

/* An empty list, or one with no saver we know, means every saver is in. */
int saver_in_mix(const Saver *sv) {
    for (int i = 0; i < SAVER_COUNT; i++)
        if (mix_lists(SAVERS[i]->id)) return mix_lists(sv->id);
    return 1;
}

int saver_set_in_mix(const Saver *sv, int on) {
    char list[sizeof g_config.randomSavers] = "";
    size_t len = 0;
    int count = 0;
    for (int i = 0; i < SAVER_COUNT; i++) {
        if (!(SAVERS[i] == sv ? on : saver_in_mix(SAVERS[i]))) continue;
        len += (size_t)snprintf(list + len, sizeof list - len, "%s%s", count ? "," : "", SAVERS[i]->id);
        if (len >= sizeof list) return 0;
        count++;
    }
    if (count == 0) return 0;
    /* Everything on is written as empty, so savers added later join the mix too. */
    snprintf(g_config.randomSavers, sizeof g_config.randomSavers, "%s", count == SAVER_COUNT ? "" : list);
    return 1;
}

/* ---------- random ---------- */

float frand(float a, float b) { return a + (b - a) * (float)rand() / (float)RAND_MAX; }
int irand(int n) { return n > 0 ? rand() % n : 0; }

/* ---------- canvas ---------- */

uint32_t rgb(uint32_t hex) { return (hex << 8) | 0xFF; }

Canvas canvas_new(int w, int h) {
    Canvas c = { w, h, calloc((size_t)w * h, sizeof(uint32_t)) };
    return c;
}

void put(Canvas *c, int x, int y, uint32_t col) {
    if (x >= 0 && y >= 0 && x < c->w && y < c->h) c->px[y * c->w + x] = col;
}

uint32_t get(const Canvas *c, int x, int y) {
    if (x < 0 || y < 0 || x >= c->w || y >= c->h) return 0;
    return c->px[y * c->w + x];
}

void fill_rect(Canvas *c, int x0, int y0, int x1, int y1, uint32_t col) {
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) put(c, x, y, col);
}

void fill_ellipse(Canvas *c, float cx, float cy, float rx, float ry, uint32_t col) {
    for (int y = (int)floorf(cy - ry); y <= (int)ceilf(cy + ry); y++)
        for (int x = (int)floorf(cx - rx); x <= (int)ceilf(cx + rx); x++) {
            float dx = (x + 0.5f - cx) / rx, dy = (y + 0.5f - cy) / ry;
            if (dx * dx + dy * dy <= 1.0f) put(c, x, y, col);
        }
}

void fill_round_rect(Canvas *c, int x0, int y0, int x1, int y1, int r, uint32_t col) {
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            int cx = x < x0 + r ? x0 + r : (x > x1 - r ? x1 - r : x);
            int cy = y < y0 + r ? y0 + r : (y > y1 - r ? y1 - r : y);
            float dx = (float)(x - cx), dy = (float)(y - cy);
            if (dx * dx + dy * dy <= (r + 0.3f) * (r + 0.3f)) put(c, x, y, col);
        }
}

void fill_poly(Canvas *c, const float *xs, const float *ys, int n, uint32_t col) {
    float miny = ys[0], maxy = ys[0];
    for (int i = 1; i < n; i++) {
        if (ys[i] < miny) miny = ys[i];
        if (ys[i] > maxy) maxy = ys[i];
    }
    for (int y = (int)floorf(miny); y <= (int)ceilf(maxy); y++) {
        float sy = y + 0.5f, hits[16];
        int nh = 0;
        for (int i = 0, j = n - 1; i < n && nh < 16; j = i++)
            if ((ys[i] > sy) != (ys[j] > sy))
                hits[nh++] = xs[i] + (sy - ys[i]) * (xs[j] - xs[i]) / (ys[j] - ys[i]);
        for (int a = 0; a < nh; a++)
            for (int b = a + 1; b < nh; b++)
                if (hits[b] < hits[a]) { float t = hits[a]; hits[a] = hits[b]; hits[b] = t; }
        for (int k = 0; k + 1 < nh; k += 2)
            for (int x = (int)ceilf(hits[k] - 0.5f); x <= (int)floorf(hits[k + 1] - 0.5f); x++)
                put(c, x, y, col);
    }
}

void outline(Canvas *c, uint32_t col) {
    Canvas o = canvas_new(c->w, c->h);
    for (int y = 0; y < c->h; y++)
        for (int x = 0; x < c->w; x++)
            if (!get(c, x, y) && (get(c, x - 1, y) || get(c, x + 1, y) || get(c, x, y - 1) || get(c, x, y + 1)))
                o.px[y * c->w + x] = col;
    for (int i = 0; i < c->w * c->h; i++)
        if (o.px[i]) c->px[i] = o.px[i];
    free(o.px);
}

SDL_Texture *canvas_texture(SDL_Renderer *r, Canvas *c) {
    SDL_Texture *t = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STATIC, c->w, c->h);
    if (t) {
        SDL_UpdateTexture(t, NULL, c->px, c->w * 4);
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(t, SDL_ScaleModeNearest);
    }
    free(c->px);
    c->px = NULL;
    return t;
}

/* ---------- minimal PNG writer (stored deflate, no compression) ---------- */

static uint32_t crc_table[256];

static uint32_t crc32_update(uint32_t crc, const uint8_t *buf, size_t len) {
    if (!crc_table[1])
        for (uint32_t n = 0; n < 256; n++) {
            uint32_t c = n;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            crc_table[n] = c;
        }
    crc = ~crc;
    while (len--) crc = crc_table[(crc ^ *buf++) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

static void be32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = (uint8_t)v; }

static void chunk(FILE *f, const char *type, const uint8_t *data, uint32_t len) {
    uint8_t hdr[8];
    be32(hdr, len);
    memcpy(hdr + 4, type, 4);
    fwrite(hdr, 1, 8, f);
    if (len) fwrite(data, 1, len, f);
    uint32_t crc = crc32_update(0, hdr + 4, 4);
    if (len) {
        /* continue CRC over data: crc32_update expects the running value */
        crc = crc32_update(crc, data, len);
    }
    be32(hdr, crc);
    fwrite(hdr, 1, 4, f);
}

int write_png(const char *path, int w, int h, const uint32_t *px) {
    size_t raw_len = (size_t)h * (1 + (size_t)w * 4);
    uint8_t *raw = malloc(raw_len);
    if (!raw) return -1;
    for (int y = 0; y < h; y++) {
        uint8_t *row = raw + (size_t)y * (1 + (size_t)w * 4);
        row[0] = 0; /* filter: none */
        for (int x = 0; x < w; x++) {
            uint32_t p = px[y * w + x];
            row[1 + x * 4 + 0] = p >> 24;
            row[1 + x * 4 + 1] = p >> 16;
            row[1 + x * 4 + 2] = p >> 8;
            row[1 + x * 4 + 3] = (uint8_t)p;
        }
    }
    size_t blocks = (raw_len + 65534) / 65535;
    size_t z_len = 2 + raw_len + blocks * 5 + 4;
    uint8_t *z = malloc(z_len), *o = z;
    if (!z) { free(raw); return -1; }
    *o++ = 0x78; *o++ = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t off = 0; off < raw_len; off += 65535) {
        uint16_t n = (uint16_t)(raw_len - off > 65535 ? 65535 : raw_len - off);
        *o++ = off + n >= raw_len ? 1 : 0;
        *o++ = n & 0xFF; *o++ = n >> 8; *o++ = ~n & 0xFF; *o++ = (uint8_t)(~n >> 8);
        memcpy(o, raw + off, n);
        o += n;
    }
    for (size_t i = 0; i < raw_len; i++) { a = (a + raw[i]) % 65521; b = (b + a) % 65521; }
    be32(o, (b << 16) | a);
    o += 4;

    FILE *f = fopen(path, "wb");
    if (!f) { free(raw); free(z); return -1; }
    static const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    uint8_t ihdr[13];
    be32(ihdr, (uint32_t)w);
    be32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8; ihdr[9] = 6; ihdr[10] = ihdr[11] = ihdr[12] = 0;
    fwrite(sig, 1, 8, f);
    chunk(f, "IHDR", ihdr, 13);
    chunk(f, "IDAT", z, (uint32_t)(o - z));
    chunk(f, "IEND", NULL, 0);
    free(raw);
    free(z);
    return fclose(f) == 0 ? 0 : -1;
}

/* ---------- renderer helpers ---------- */

SDL_Color hsv(float h, float s, float v) {
    h = fmodf(h, 360.0f);
    if (h < 0) h += 360.0f;
    float c = v * s, x = c * (1 - fabsf(fmodf(h / 60.0f, 2) - 1)), m = v - c, r, g, b;
    if (h < 60) { r = c; g = x; b = 0; }
    else if (h < 120) { r = x; g = c; b = 0; }
    else if (h < 180) { r = 0; g = c; b = x; }
    else if (h < 240) { r = 0; g = x; b = c; }
    else if (h < 300) { r = x; g = 0; b = c; }
    else { r = c; g = 0; b = x; }
    SDL_Color out = { (Uint8)((r + m) * 255), (Uint8)((g + m) * 255), (Uint8)((b + m) * 255), 255 };
    return out;
}

SDL_Color hexcol(uint32_t hex) {
    SDL_Color c = { (Uint8)(hex >> 16), (Uint8)(hex >> 8), (Uint8)hex, 255 };
    return c;
}

void set_color(SDL_Renderer *r, SDL_Color c) { SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a); }

void fill_triangle(SDL_Renderer *r, float x0, float y0, float x1, float y1, float x2, float y2, SDL_Color c) {
    SDL_Vertex v[3] = { { { x0, y0 }, c, { 0, 0 } }, { { x1, y1 }, c, { 0, 0 } }, { { x2, y2 }, c, { 0, 0 } } };
    SDL_RenderGeometry(r, NULL, v, 3, NULL, 0);
}

void thick_line(SDL_Renderer *r, float x0, float y0, float x1, float y1, float width, SDL_Color c) {
    float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
    if (len < 0.01f) return;
    float nx = -dy / len * width * 0.5f, ny = dx / len * width * 0.5f;
    SDL_Vertex v[4] = {
        { { x0 + nx, y0 + ny }, c, { 0, 0 } }, { { x1 + nx, y1 + ny }, c, { 0, 0 } },
        { { x1 - nx, y1 - ny }, c, { 0, 0 } }, { { x0 - nx, y0 - ny }, c, { 0, 0 } },
    };
    int idx[6] = { 0, 1, 2, 0, 2, 3 };
    SDL_RenderGeometry(r, NULL, v, 4, idx, 6);
}

void vgradient(SDL_Renderer *r, int w, int h, SDL_Color top, SDL_Color bottom) {
    SDL_Vertex v[4] = {
        { { 0, 0 }, top, { 0, 0 } }, { { (float)w, 0 }, top, { 0, 0 } },
        { { (float)w, (float)h }, bottom, { 0, 0 } }, { { 0, (float)h }, bottom, { 0, 0 } },
    };
    int idx[6] = { 0, 1, 2, 0, 2, 3 };
    SDL_RenderGeometry(r, NULL, v, 4, idx, 6);
}

void fill_circle(SDL_Renderer *r, float cx, float cy, float rad, SDL_Color c) {
    enum { N = 28 };
    SDL_Vertex v[N + 1];
    int idx[N * 3];
    v[0] = (SDL_Vertex){ { cx, cy }, c, { 0, 0 } };
    for (int i = 0; i < N; i++) {
        float a = i * 2 * PI_F / N;
        v[i + 1] = (SDL_Vertex){ { cx + cosf(a) * rad, cy + sinf(a) * rad }, c, { 0, 0 } };
        idx[i * 3] = 0; idx[i * 3 + 1] = i + 1; idx[i * 3 + 2] = (i + 1) % N + 1;
    }
    SDL_RenderGeometry(r, NULL, v, N + 1, idx, N * 3);
}

void fill_rrect(SDL_Renderer *r, SDL_Rect rc, int rad, SDL_Color c) {
    if (rad * 2 > rc.w) rad = rc.w / 2;
    if (rad * 2 > rc.h) rad = rc.h / 2;
    set_color(r, c);
    SDL_Rect mid = { rc.x + rad, rc.y, rc.w - 2 * rad, rc.h };
    SDL_Rect side = { rc.x, rc.y + rad, rc.w, rc.h - 2 * rad };
    SDL_RenderFillRect(r, &mid);
    SDL_RenderFillRect(r, &side);
    fill_circle(r, rc.x + rad, rc.y + rad, rad, c);
    fill_circle(r, rc.x + rc.w - rad, rc.y + rad, rad, c);
    fill_circle(r, rc.x + rad, rc.y + rc.h - rad, rad, c);
    fill_circle(r, rc.x + rc.w - rad, rc.y + rc.h - rad, rad, c);
}
