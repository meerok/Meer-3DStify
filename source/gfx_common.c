/* 3DStify - drawing helpers shared by the 3DS and preview backends */
#include <math.h>
#include <string.h>
#include "gfx.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static inline int ch(col_t c, int s) { return (int)((c >> s) & 0xFF); }

col_t gfx_mix(col_t a, col_t b, float t) {
    if (t <= 0) return a;
    if (t >= 1) return b;
    int A = (int)(ch(a, 24) + (ch(b, 24) - ch(a, 24)) * t);
    int R = (int)(ch(a, 16) + (ch(b, 16) - ch(a, 16)) * t);
    int G = (int)(ch(a, 8) + (ch(b, 8) - ch(a, 8)) * t);
    int B = (int)(ch(a, 0) + (ch(b, 0) - ch(a, 0)) * t);
    return RGBA(R, G, B, A);
}

col_t gfx_alpha(col_t c, float a) {
    if (a >= 1) return c;
    if (a < 0) a = 0;
    int A = (int)(ch(c, 24) * a);
    return (c & 0x00FFFFFFu) | ((col_t)A << 24);
}

void gfx_rrect(float x, float y, float w, float h, float r, col_t c) {
    if (r > h * 0.5f) r = h * 0.5f;
    if (r > w * 0.5f) r = w * 0.5f;
    if (r < 0.5f) { gfx_rect(x, y, w, h, c); return; }
    gfx_rect(x + r, y, w - 2 * r, h, c);
    gfx_rect(x, y + r, r, h - 2 * r, c);
    gfx_rect(x + w - r, y + r, r, h - 2 * r, c);
    /* quarter circles as fans so translucent colours don't double up */
    int seg = r < 4 ? 4 : (r < 10 ? 6 : 8);
    const float cx[4] = { x + r, x + w - r, x + w - r, x + r };
    const float cy[4] = { y + r, y + r, y + h - r, y + h - r };
    const float a0[4] = { (float)M_PI, 1.5f * (float)M_PI, 0, 0.5f * (float)M_PI };
    for (int k = 0; k < 4; k++) {
        for (int i = 0; i < seg; i++) {
            float t0 = a0[k] + (float)i / seg * 0.5f * (float)M_PI;
            float t1 = a0[k] + (float)(i + 1) / seg * 0.5f * (float)M_PI;
            gfx_tri(cx[k], cy[k], cx[k] + cosf(t0) * r, cy[k] + sinf(t0) * r,
                    cx[k] + cosf(t1) * r, cy[k] + sinf(t1) * r, c);
        }
    }
}

void gfx_arc(float cx, float cy, float r, float a0, float a1, float th, col_t c) {
    int seg = (int)(fabsf(a1 - a0) * r / 3.0f);
    if (seg < 6) seg = 6;
    if (seg > 64) seg = 64;
    float ri = r - th * 0.5f, ro = r + th * 0.5f;
    for (int i = 0; i < seg; i++) {
        float t0 = a0 + (a1 - a0) * i / seg, t1 = a0 + (a1 - a0) * (i + 1) / seg;
        float c0 = cosf(t0), s0 = sinf(t0), c1 = cosf(t1), s1 = sinf(t1);
        gfx_tri(cx + c0 * ri, cy + s0 * ri, cx + c0 * ro, cy + s0 * ro, cx + c1 * ro, cy + s1 * ro, c);
        gfx_tri(cx + c0 * ri, cy + s0 * ri, cx + c1 * ro, cy + s1 * ro, cx + c1 * ri, cy + s1 * ri, c);
    }
}

void gfx_ring(float cx, float cy, float r, float th, col_t c) {
    gfx_arc(cx, cy, r, 0, 2 * (float)M_PI, th, c);
}

void gfx_corners(float x, float y, float w, float h, float r, col_t bg) {
    if (r < 0.5f) return;
    int seg = r < 6 ? 3 : (r < 16 ? 6 : 12);
    const float px[4] = { x, x + w, x + w, x };          /* square corners */
    const float py[4] = { y, y, y + h, y + h };
    const float cx[4] = { x + r, x + w - r, x + w - r, x + r };
    const float cy[4] = { y + r, y + r, y + h - r, y + h - r };
    const float a0[4] = { (float)M_PI, 1.5f * (float)M_PI, 0, 0.5f * (float)M_PI };
    for (int k = 0; k < 4; k++) {
        for (int i = 0; i < seg; i++) {
            float t0 = a0[k] + (float)i / seg * 0.5f * (float)M_PI;
            float t1 = a0[k] + (float)(i + 1) / seg * 0.5f * (float)M_PI;
            gfx_tri(px[k], py[k], cx[k] + cosf(t0) * r, cy[k] + sinf(t0) * r,
                    cx[k] + cosf(t1) * r, cy[k] + sinf(t1) * r, bg);
        }
    }
}

/* step back to the start of a UTF-8 character */
static size_t utf8_back(const char *s, size_t n) {
    while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80) n--;
    return n;
}

float gfx_text_fit(float x, float y, float scale, col_t c, int align, float maxw, const char *s, bool bold) {
    if (!s) s = "";
    float w = gfx_text_width(s, scale);
    if (w <= maxw) {
        return bold ? gfx_text_bold(x, y, scale, c, align, s) : gfx_text(x, y, scale, c, align, s);
    }
    char buf[256];
    size_t len = strlen(s);
    if (len > sizeof buf - 4) len = sizeof buf - 4;
    size_t lo = 0, hi = len;
    float ell = gfx_text_width("...", scale);
    while (lo < hi) {                         /* longest prefix that fits */
        size_t mid = utf8_back(s, (lo + hi + 1) / 2);
        if (mid <= lo) mid = lo + 1;
        memcpy(buf, s, mid);
        buf[mid] = 0;
        if (gfx_text_width(buf, scale) + ell <= maxw) lo = mid;
        else hi = mid - 1;
    }
    lo = utf8_back(s, lo);
    while (lo > 0 && s[lo - 1] == ' ') lo--;
    memcpy(buf, s, lo);
    memcpy(buf + lo, "...", 4);
    return bold ? gfx_text_bold(x, y, scale, c, align, buf) : gfx_text(x, y, scale, c, align, buf);
}
