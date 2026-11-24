/*
 * 3DStify - tiny drawing layer
 *
 * Everything the UI draws goes through these calls. On the 3DS they map to
 * citro2d (gfx_3ds.c); tools/preview has a software version so the exact same
 * UI code can render README screenshots on a PC.
 *
 * Colours are 0xAARRGGBB.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef uint32_t col_t;

#define RGB(r, g, b)     ((col_t)(0xFF000000u | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b)))
#define RGBA(r, g, b, a) ((col_t)(((uint32_t)(a) << 24) | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b)))
#define HEXC(h)          ((col_t)(0xFF000000u | (uint32_t)(h)))

enum { ALIGN_LEFT = 0, ALIGN_CENTER = 1, ALIGN_RIGHT = 2 };

typedef struct GfxImage GfxImage;   /* a texture; opaque */

void  gfx_offset(float ox, float oy);                 /* translate everything drawn after this */
void  gfx_get_offset(float *ox, float *oy);
void  gfx_clip(int x, int y, int w, int h);           /* screen coords, ignores the offset */
void  gfx_unclip(void);

void  gfx_rect(float x, float y, float w, float h, col_t c);
void  gfx_rect_v(float x, float y, float w, float h, col_t top, col_t bottom);
void  gfx_rect_h(float x, float y, float w, float h, col_t left, col_t right);
void  gfx_tri(float x0, float y0, float x1, float y1, float x2, float y2, col_t c);
void  gfx_line(float x0, float y0, float x1, float y1, float th, col_t c);
void  gfx_circle(float cx, float cy, float r, col_t c);
void  gfx_image(GfxImage *img, float x, float y, float w, float h, col_t tint_alpha);

/* text: scale 1.0 is roughly 30px tall (3DS system font) */
float gfx_text(float x, float y, float scale, col_t c, int align, const char *s);
float gfx_text_bold(float x, float y, float scale, col_t c, int align, const char *s);
float gfx_text_width(const char *s, float scale);

/* images from 8-bit RGBA pixels, w/h powers of two (64, 256) */
GfxImage *gfx_image_new(const uint8_t *rgba, int w, int h);
void      gfx_image_free(GfxImage *img);

/* ---- helpers built on the primitives (gfx_common.c) ---- */
col_t gfx_mix(col_t a, col_t b, float t);
col_t gfx_alpha(col_t c, float a);           /* multiply alpha */
void  gfx_rrect(float x, float y, float w, float h, float r, col_t c);
void  gfx_ring(float cx, float cy, float r, float th, col_t c);
void  gfx_arc(float cx, float cy, float r, float a0, float a1, float th, col_t c);
void  gfx_corners(float x, float y, float w, float h, float r, col_t bg);  /* paint rounded corners in bg colour */
/* draws s cut to maxw with an ellipsis */
float gfx_text_fit(float x, float y, float scale, col_t c, int align, float maxw, const char *s, bool bold);
