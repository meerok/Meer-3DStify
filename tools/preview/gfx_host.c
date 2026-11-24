/*
 * 3DStify preview - software version of gfx.h
 *
 * Renders the real UI code into a PNG on a PC, for README screenshots.
 * Draws at SS x resolution and box-filters down to the 3DS's native size.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gfx.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#define SS 4
#define MAXW 400
#define MAXH 240

struct GfxImage { int w, h; unsigned char *px; };

static float fb[MAXH * SS][MAXW * SS][3];
static int   scr_w = 400;
static float ox, oy;
static int   clip_on = 0, cx0, cy0, cx1, cy1;
static stbtt_fontinfo font;
static unsigned char *font_buf;
static float em_per_scale = 25.0f;     /* px of em at text scale 1.0 (3DS system font ~ 30px line) */

void host_init(const char *ttf) {
    FILE *f = fopen(ttf, "rb");
    if (!f) { fprintf(stderr, "no font %s\n", ttf); exit(1); }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    font_buf = malloc((size_t)n);
    if (fread(font_buf, 1, (size_t)n, f) != (size_t)n) exit(1);
    fclose(f);
    stbtt_InitFont(&font, font_buf, 0);
}

void host_begin(int width) {
    scr_w = width;
    ox = oy = 0;
    clip_on = 0;
    for (int y = 0; y < MAXH * SS; y++)
        for (int x = 0; x < MAXW * SS; x++) fb[y][x][0] = fb[y][x][1] = fb[y][x][2] = 0x12 / 255.0f;
}

void host_save(const char *path_rgb_out, unsigned char *out) {   /* out: w*240*3 */
    (void)path_rgb_out;
    for (int y = 0; y < 240; y++)
        for (int x = 0; x < scr_w; x++) {
            float r = 0, g = 0, b = 0;
            for (int j = 0; j < SS; j++)
                for (int i = 0; i < SS; i++) { r += fb[y * SS + j][x * SS + i][0]; g += fb[y * SS + j][x * SS + i][1]; b += fb[y * SS + j][x * SS + i][2]; }
            float k = 1.0f / (SS * SS);
            unsigned char *o = out + (y * scr_w + x) * 3;
            o[0] = (unsigned char)fminf(255, r * k * 255 + 0.5f);
            o[1] = (unsigned char)fminf(255, g * k * 255 + 0.5f);
            o[2] = (unsigned char)fminf(255, b * k * 255 + 0.5f);
        }
}

static inline void blend(int X, int Y, float r, float g, float b, float a) {
    if (X < 0 || Y < 0 || X >= scr_w * SS || Y >= 240 * SS || a <= 0) return;
    if (clip_on && (X < cx0 * SS || X >= cx1 * SS || Y < cy0 * SS || Y >= cy1 * SS)) return;
    float *p = fb[Y][X];
    p[0] += (r - p[0]) * a; p[1] += (g - p[1]) * a; p[2] += (b - p[2]) * a;
}

static inline void unpack(col_t c, float *r, float *g, float *b, float *a) {
    *a = ((c >> 24) & 0xFF) / 255.0f; *r = ((c >> 16) & 0xFF) / 255.0f; *g = ((c >> 8) & 0xFF) / 255.0f; *b = (c & 0xFF) / 255.0f;
}

void gfx_offset(float x, float y) { ox = x; oy = y; }
void gfx_get_offset(float *x, float *y) { *x = ox; *y = oy; }
void gfx_clip(int x, int y, int w, int h) { clip_on = 1; cx0 = x; cy0 = y; cx1 = x + w; cy1 = y + h; }
void gfx_unclip(void) { clip_on = 0; }

static void rect4(float x, float y, float w, float h, col_t c00, col_t c10, col_t c01, col_t c11) {
    x += ox; y += oy;
    int X0 = (int)floorf(x * SS + 0.5f), Y0 = (int)floorf(y * SS + 0.5f);
    int X1 = (int)floorf((x + w) * SS + 0.5f), Y1 = (int)floorf((y + h) * SS + 0.5f);
    float a[4][4];
    col_t cs[4] = { c00, c10, c01, c11 };
    for (int i = 0; i < 4; i++) unpack(cs[i], &a[i][0], &a[i][1], &a[i][2], &a[i][3]);
    for (int Y = Y0; Y < Y1; Y++) {
        float v = (Y1 - Y0) > 1 ? (float)(Y - Y0) / (Y1 - Y0 - 1) : 0;
        for (int X = X0; X < X1; X++) {
            float u = (X1 - X0) > 1 ? (float)(X - X0) / (X1 - X0 - 1) : 0;
            float k[4];
            for (int ch = 0; ch < 4; ch++)
                k[ch] = (a[0][ch] * (1 - u) + a[1][ch] * u) * (1 - v) + (a[2][ch] * (1 - u) + a[3][ch] * u) * v;
            blend(X, Y, k[0], k[1], k[2], k[3]);
        }
    }
}

void gfx_rect(float x, float y, float w, float h, col_t c) { rect4(x, y, w, h, c, c, c, c); }
void gfx_rect_v(float x, float y, float w, float h, col_t t, col_t b) { rect4(x, y, w, h, t, t, b, b); }
void gfx_rect_h(float x, float y, float w, float h, col_t l, col_t r) { rect4(x, y, w, h, l, r, l, r); }

void gfx_tri(float x0, float y0, float x1, float y1, float x2, float y2, col_t c) {
    float r, g, b, a;
    unpack(c, &r, &g, &b, &a);
    if (a <= 0) return;
    x0 = (x0 + ox) * SS; y0 = (y0 + oy) * SS; x1 = (x1 + ox) * SS; y1 = (y1 + oy) * SS; x2 = (x2 + ox) * SS; y2 = (y2 + oy) * SS;
    float area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
    if (fabsf(area) < 1e-6f) return;
    int minx = (int)floorf(fminf(x0, fminf(x1, x2))), maxx = (int)ceilf(fmaxf(x0, fmaxf(x1, x2)));
    int miny = (int)floorf(fminf(y0, fminf(y1, y2))), maxy = (int)ceilf(fmaxf(y0, fmaxf(y1, y2)));
    for (int Y = miny; Y <= maxy; Y++)
        for (int X = minx; X <= maxx; X++) {
            float px = X + 0.5f, py = Y + 0.5f;
            float w0 = (x1 - px) * (y2 - py) - (x2 - px) * (y1 - py);
            float w1 = (x2 - px) * (y0 - py) - (x0 - px) * (y2 - py);
            float w2 = (x0 - px) * (y1 - py) - (x1 - px) * (y0 - py);
            if (area > 0 ? (w0 >= 0 && w1 >= 0 && w2 >= 0) : (w0 <= 0 && w1 <= 0 && w2 <= 0)) blend(X, Y, r, g, b, a);
        }
}

void gfx_line(float x0, float y0, float x1, float y1, float th, col_t c) {
    float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy);
    if (l < 1e-4f) return;
    float nx = -dy / l * th * 0.5f, ny = dx / l * th * 0.5f;
    gfx_tri(x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, c);
    gfx_tri(x0 + nx, y0 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, c);
}

void gfx_circle(float cx, float cy, float rad, col_t c) {
    float r, g, b, a;
    unpack(c, &r, &g, &b, &a);
    if (a <= 0) return;
    cx = (cx + ox) * SS; cy = (cy + oy) * SS; rad *= SS;
    for (int Y = (int)(cy - rad - 1); Y <= (int)(cy + rad + 1); Y++)
        for (int X = (int)(cx - rad - 1); X <= (int)(cx + rad + 1); X++) {
            float dx = X + 0.5f - cx, dy = Y + 0.5f - cy;
            if (dx * dx + dy * dy <= rad * rad) blend(X, Y, r, g, b, a);
        }
}

void gfx_image(GfxImage *img, float x, float y, float w, float h, col_t tint) {
    if (!img) return;
    float ta = ((tint >> 24) & 0xFF) / 255.0f;
    x += ox; y += oy;
    int X0 = (int)(x * SS), Y0 = (int)(y * SS), X1 = (int)((x + w) * SS), Y1 = (int)((y + h) * SS);
    for (int Y = Y0; Y < Y1; Y++)
        for (int X = X0; X < X1; X++) {
            float u = (X - X0 + 0.5f) / (X1 - X0) * img->w - 0.5f, v = (Y - Y0 + 0.5f) / (Y1 - Y0) * img->h - 0.5f;
            int iu = (int)floorf(u), iv = (int)floorf(v);
            float fu = u - iu, fv = v - iv;
            float acc[4] = { 0 };
            for (int j = 0; j < 2; j++)
                for (int i = 0; i < 2; i++) {
                    int sx = iu + i < 0 ? 0 : (iu + i >= img->w ? img->w - 1 : iu + i);
                    int sy = iv + j < 0 ? 0 : (iv + j >= img->h ? img->h - 1 : iv + j);
                    float wt = (i ? fu : 1 - fu) * (j ? fv : 1 - fv);
                    unsigned char *p = img->px + (sy * img->w + sx) * 4;
                    for (int ch = 0; ch < 4; ch++) acc[ch] += p[ch] * wt;
                }
            blend(X, Y, acc[0] / 255, acc[1] / 255, acc[2] / 255, acc[3] / 255 * ta);
        }
}

static int next_cp(const char **s) {
    const unsigned char *p = (const unsigned char *)*s;
    int c = *p;
    if (c < 0x80) { *s += 1; return c; }
    if ((c & 0xE0) == 0xC0) { *s += 2; return ((c & 0x1F) << 6) | (p[1] & 0x3F); }
    if ((c & 0xF0) == 0xE0) { *s += 3; return ((c & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F); }
    *s += 4;
    return ((c & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
}

float gfx_text_width(const char *s, float scale) {
    if (!s) return 0;
    float k = stbtt_ScaleForMappingEmToPixels(&font, em_per_scale * scale);
    float w = 0;
    while (*s) {
        int cp = next_cp(&s);
        int adv, lsb;
        stbtt_GetCodepointHMetrics(&font, cp, &adv, &lsb);
        w += adv * k;
    }
    return w;
}

static float draw_text(float x, float y, float scale, col_t c, int align, const char *s) {
    if (!s || !*s) return 0;
    float w = gfx_text_width(s, scale);
    if (align == ALIGN_CENTER) x -= w / 2;
    else if (align == ALIGN_RIGHT) x -= w;
    float r, g, b, a;
    unpack(c, &r, &g, &b, &a);
    float k = stbtt_ScaleForMappingEmToPixels(&font, em_per_scale * scale * SS);
    int asc, desc, gap;
    stbtt_GetFontVMetrics(&font, &asc, &desc, &gap);
    float pen = (x + ox) * SS;
    float base = (y + oy) * SS + asc * k * 0.92f;
    while (*s) {
        int cp = next_cp(&s);
        int adv, lsb, gw, gh, gx, gy;
        stbtt_GetCodepointHMetrics(&font, cp, &adv, &lsb);
        unsigned char *bm = stbtt_GetCodepointBitmapSubpixel(&font, k, k, pen - floorf(pen), 0, cp, &gw, &gh, &gx, &gy);
        if (bm) {
            for (int j = 0; j < gh; j++)
                for (int i = 0; i < gw; i++)
                    blend((int)floorf(pen) + gx + i, (int)floorf(base) + gy + j, r, g, b, a * bm[j * gw + i] / 255.0f);
            stbtt_FreeBitmap(bm, NULL);
        }
        pen += adv * k;
    }
    return w;
}

float gfx_text(float x, float y, float scale, col_t c, int align, const char *s) { return draw_text(x, y, scale, c, align, s); }
float gfx_text_bold(float x, float y, float scale, col_t c, int align, const char *s) {
    draw_text(x + 0.6f, y, scale, c, align, s);
    return draw_text(x, y, scale, c, align, s) + 0.6f;
}

GfxImage *gfx_image_new(const uint8_t *rgba, int w, int h) {
    GfxImage *g = malloc(sizeof *g);
    g->w = w; g->h = h;
    g->px = malloc((size_t)w * h * 4);
    memcpy(g->px, rgba, (size_t)w * h * 4);
    return g;
}

void gfx_image_free(GfxImage *img) {
    if (!img) return;
    free(img->px);
    free(img);
}
