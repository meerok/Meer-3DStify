/* 3DStify - citro2d backend for gfx.h */
#include <3ds.h>
#include <citro2d.h>
#include <stdlib.h>
#include <string.h>
#include "gfx.h"
#include "gfx_3ds.h"

struct GfxImage {
    C3D_Tex tex;
    Tex3DS_SubTexture sub;
    int w, h;
};

static C2D_TextBuf tbuf;
static float g_ox, g_oy;
static int   g_screen_w = 400;
static float g_line = 30.0f;

static inline u32 cv(col_t c) {     /* 0xAARRGGBB -> citro2d 0xAABBGGRR */
    return (c & 0xFF00FF00u) | ((c >> 16) & 0xFFu) | ((c & 0xFFu) << 16);
}

void gfx3ds_init(void) {
    fontEnsureMapped();
    tbuf = C2D_TextBufNew(8192);
    FINF_s *fi = fontGetInfo(NULL);
    if (fi) g_line = (float)fi->lineFeed;
}

void gfx3ds_exit(void) {
    C2D_TextBufDelete(tbuf);
}

void gfx3ds_frame(void) {
    C2D_TextBufClear(tbuf);
}

void gfx3ds_screen(int width) {
    g_screen_w = width;
    g_ox = g_oy = 0;
}

void gfx_offset(float ox, float oy) { g_ox = ox; g_oy = oy; }
void gfx_get_offset(float *ox, float *oy) { *ox = g_ox; *oy = g_oy; }

void gfx_clip(int x, int y, int w, int h) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > g_screen_w) w = g_screen_w - x;
    if (y + h > 240) h = 240 - y;
    if (w <= 0 || h <= 0) { x = y = 0; w = h = 1; }
    C2D_Flush();
    /* the framebuffers are rotated: 240 wide, 400/320 tall */
    C3D_SetScissor(GPU_SCISSOR_NORMAL, (u32)(240 - (y + h)), (u32)(g_screen_w - (x + w)),
                   (u32)(240 - y), (u32)(g_screen_w - x));
}

void gfx_unclip(void) {
    C2D_Flush();
    C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
}

void gfx_rect(float x, float y, float w, float h, col_t c) {
    if ((c >> 24) == 0 || w <= 0 || h <= 0) return;
    C2D_DrawRectSolid(x + g_ox, y + g_oy, 0.5f, w, h, cv(c));
}

void gfx_rect_v(float x, float y, float w, float h, col_t top, col_t bottom) {
    if (w <= 0 || h <= 0) return;
    C2D_DrawRectangle(x + g_ox, y + g_oy, 0.5f, w, h, cv(top), cv(top), cv(bottom), cv(bottom));
}

void gfx_rect_h(float x, float y, float w, float h, col_t left, col_t right) {
    if (w <= 0 || h <= 0) return;
    C2D_DrawRectangle(x + g_ox, y + g_oy, 0.5f, w, h, cv(left), cv(right), cv(left), cv(right));
}

void gfx_tri(float x0, float y0, float x1, float y1, float x2, float y2, col_t c) {
    if ((c >> 24) == 0) return;
    u32 k = cv(c);
    C2D_DrawTriangle(x0 + g_ox, y0 + g_oy, k, x1 + g_ox, y1 + g_oy, k, x2 + g_ox, y2 + g_oy, k, 0.5f);
}

void gfx_line(float x0, float y0, float x1, float y1, float th, col_t c) {
    if ((c >> 24) == 0) return;
    u32 k = cv(c);
    C2D_DrawLine(x0 + g_ox, y0 + g_oy, k, x1 + g_ox, y1 + g_oy, k, th, 0.5f);
}

void gfx_circle(float cx, float cy, float r, col_t c) {
    if ((c >> 24) == 0 || r <= 0) return;
    C2D_DrawCircleSolid(cx + g_ox, cy + g_oy, 0.5f, r, cv(c));
}

void gfx_image(GfxImage *img, float x, float y, float w, float h, col_t tint_alpha) {
    if (!img) return;
    C2D_Image ci = { &img->tex, &img->sub };
    float a = (float)(tint_alpha >> 24) / 255.0f;
    if (a <= 0.0f) return;
    if (a < 0.999f) {
        C2D_ImageTint t;
        C2D_AlphaImageTint(&t, a);
        C2D_DrawImageAt(ci, x + g_ox, y + g_oy, 0.5f, &t, w / img->w, h / img->h);
    } else {
        C2D_DrawImageAt(ci, x + g_ox, y + g_oy, 0.5f, NULL, w / img->w, h / img->h);
    }
}

float gfx_text_width(const char *s, float scale) {
    if (!s) return 0;
    const u8 *p = (const u8 *)s;
    float w = 0;
    while (*p) {
        u32 cp;
        ssize_t n = decode_utf8(&cp, p);
        if (n <= 0) { p++; continue; }
        p += n;
        if (cp == '\n') break;
        int gi = fontGlyphIndexFromCodePoint(NULL, cp);
        charWidthInfo_s *wi = fontGetCharWidthInfo(NULL, gi);
        if (wi) w += wi->charWidth;
    }
    return w * scale;
}

static float draw_text(float x, float y, float scale, col_t c, int align, const char *s) {
    if (!s || !*s || (c >> 24) == 0) return 0;
    C2D_Text t;
    if (!C2D_TextParse(&t, tbuf, s)) return 0;
    C2D_TextOptimize(&t);
    u32 flags = C2D_WithColor;
    if (align == ALIGN_CENTER) flags |= C2D_AlignCenter;
    else if (align == ALIGN_RIGHT) flags |= C2D_AlignRight;
    /* the system font sits a little low; nudge so y is the visual top */
    C2D_DrawText(&t, flags, x + g_ox, y + g_oy - scale * 2.0f, 0.5f, scale, scale, cv(c));
    return t.width * scale;
}

float gfx_text(float x, float y, float scale, col_t c, int align, const char *s) {
    return draw_text(x, y, scale, c, align, s);
}

float gfx_text_bold(float x, float y, float scale, col_t c, int align, const char *s) {
    draw_text(x + 0.6f, y, scale, c, align, s);
    return draw_text(x, y, scale, c, align, s) + 0.6f;
}

static inline u32 swz(u32 x, u32 y, u32 w) {
    return ((((y >> 3) * (w >> 3) + (x >> 3)) << 6) +
            ((x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3)));
}

GfxImage *gfx_image_new(const uint8_t *rgba, int w, int h) {
    GfxImage *img = (GfxImage *)calloc(1, sizeof(GfxImage));
    if (!img) return NULL;
    if (!C3D_TexInit(&img->tex, (u16)w, (u16)h, GPU_RGBA8)) { free(img); return NULL; }
    C3D_TexSetFilter(&img->tex, GPU_LINEAR, GPU_LINEAR);
    C3D_TexSetWrap(&img->tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    u8 *dst = (u8 *)img->tex.data;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const u8 *s = rgba + (y * w + x) * 4;
            u8 *d = dst + swz((u32)x, (u32)y, (u32)w) * 4;
            d[0] = s[3]; d[1] = s[2]; d[2] = s[1]; d[3] = s[0];
        }
    }
    GSPGPU_FlushDataCache(img->tex.data, img->tex.size);
    img->sub.width = (u16)w;
    img->sub.height = (u16)h;
    img->sub.left = 0.0f;
    img->sub.top = 1.0f;
    img->sub.right = 1.0f;
    img->sub.bottom = 0.0f;
    img->w = w;
    img->h = h;
    return img;
}

void gfx_image_free(GfxImage *img) {
    if (!img) return;
    C3D_TexDelete(&img->tex);
    free(img);
}

float gfx3ds_line_height(void) { return g_line; }
