/* 3DStify - vector icons drawn from primitives (crisp at any size) */
#include <math.h>
#include "icons.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void icon_play(float x, float y, float s, col_t c) {
    float h = s * 0.5f;
    gfx_tri(x - h * 0.62f, y - h, x - h * 0.62f, y + h, x + h * 0.95f, y, c);
}

void icon_pause(float x, float y, float s, col_t c) {
    float h = s * 0.5f, w = s * 0.24f;
    gfx_rrect(x - s * 0.36f, y - h, w, s, w * 0.25f, c);
    gfx_rrect(x + s * 0.12f, y - h, w, s, w * 0.25f, c);
}

void icon_next(float x, float y, float s, col_t c) {
    float h = s * 0.42f;
    gfx_tri(x - h, y - h, x - h, y + h, x + h * 0.7f, y, c);
    gfx_rrect(x + h * 0.62f, y - h, s * 0.13f, h * 2, 1, c);
}

void icon_prev(float x, float y, float s, col_t c) {
    float h = s * 0.42f;
    gfx_tri(x + h, y - h, x + h, y + h, x - h * 0.7f, y, c);
    gfx_rrect(x - h * 0.62f - s * 0.13f, y - h, s * 0.13f, h * 2, 1, c);
}

void icon_shuffle(float x, float y, float s, col_t c) {
    float h = s * 0.32f, w = s * 0.5f, t = s * 0.085f + 0.6f;
    gfx_line(x - w, y - h, x - w * 0.35f, y - h, t, c);
    gfx_line(x - w * 0.35f, y - h, x + w * 0.35f, y + h, t, c);
    gfx_line(x + w * 0.35f, y + h, x + w * 0.62f, y + h, t, c);
    gfx_line(x - w, y + h, x - w * 0.35f, y + h, t, c);
    gfx_line(x - w * 0.35f, y + h, x + w * 0.35f, y - h, t, c);
    gfx_line(x + w * 0.35f, y - h, x + w * 0.62f, y - h, t, c);
    float a = s * 0.2f;
    gfx_tri(x + w, y - h, x + w - a, y - h - a, x + w - a, y - h + a, c);
    gfx_tri(x + w, y + h, x + w - a, y + h - a, x + w - a, y + h + a, c);
}

void icon_repeat(float x, float y, float s, col_t c) {
    float w = s * 0.46f, h = s * 0.3f, t = s * 0.085f + 0.6f, r = h * 0.75f;
    /* rounded loop */
    gfx_line(x - w + r, y - h, x + w - r * 0.4f, y - h, t, c);
    gfx_line(x + w - r, y + h, x - w + r * 0.4f, y + h, t, c);
    gfx_arc(x - w + r, y - h + r, r, (float)M_PI, 1.5f * (float)M_PI, t, c);
    gfx_arc(x - w + r, y + h - r, r, 0.5f * (float)M_PI, (float)M_PI, t, c);
    gfx_line(x - w, y - h + r, x - w, y + h - r, t, c);
    gfx_arc(x + w - r, y + h - r, r, 0, 0.5f * (float)M_PI, t, c);
    gfx_arc(x + w - r, y - h + r, r, 1.5f * (float)M_PI, 2 * (float)M_PI, t, c);
    gfx_line(x + w, y - h + r, x + w, y + h - r * 0.2f, t, c);
    float a = s * 0.19f;
    gfx_tri(x + w * 0.35f + a, y - h, x + w * 0.35f - a * 0.4f, y - h - a * 1.1f, x + w * 0.35f - a * 0.4f, y - h + a * 1.1f, c);
}

void icon_heart(float x, float y, float s, col_t c) {
    float r = s * 0.27f;
    gfx_circle(x - s * 0.235f, y - s * 0.12f, r, c);
    gfx_circle(x + s * 0.235f, y - s * 0.12f, r, c);
    gfx_tri(x - s * 0.49f, y - s * 0.04f, x + s * 0.49f, y - s * 0.04f, x, y + s * 0.44f, c);
    gfx_rect(x - s * 0.1f, y - s * 0.2f, s * 0.2f, s * 0.25f, c);
}

void icon_heart_outline(float x, float y, float s, col_t c, col_t bg) {
    icon_heart(x, y, s, c);
    icon_heart(x, y + s * 0.02f, s * 0.68f, bg);
}

void icon_note(float x, float y, float s, col_t c) {
    float r = s * 0.2f;
    gfx_circle(x - s * 0.12f, y + s * 0.26f, r, c);
    gfx_rect(x - s * 0.12f + r - s * 0.08f, y - s * 0.42f, s * 0.09f, s * 0.68f, c);
    gfx_tri(x + r - s * 0.1f, y - s * 0.44f, x + s * 0.36f, y - s * 0.24f, x + r - s * 0.1f, y - s * 0.16f, c);
}

void icon_album(float x, float y, float s, col_t c, col_t bg) {
    gfx_circle(x, y, s * 0.46f, c);
    gfx_circle(x, y, s * 0.36f, bg);
    gfx_circle(x, y, s * 0.12f, c);
}

void icon_artist(float x, float y, float s, col_t c) {
    gfx_circle(x, y - s * 0.2f, s * 0.21f, c);
    gfx_rrect(x - s * 0.38f, y + s * 0.08f, s * 0.76f, s * 0.38f, s * 0.17f, c);
}

void icon_gear(float x, float y, float s, col_t c, col_t bg) {
    for (int i = 0; i < 8; i++) {
        float a = (float)i * (float)M_PI / 4;
        gfx_line(x, y, x + cosf(a) * s * 0.48f, y + sinf(a) * s * 0.48f, s * 0.2f, c);
    }
    gfx_circle(x, y, s * 0.34f, c);
    gfx_circle(x, y, s * 0.15f, bg);
}

void icon_back(float x, float y, float s, col_t c) {
    float t = s * 0.13f + 0.5f;
    gfx_line(x + s * 0.2f, y - s * 0.4f, x - s * 0.2f, y, t, c);
    gfx_line(x - s * 0.2f, y, x + s * 0.2f, y + s * 0.4f, t, c);
    gfx_circle(x - s * 0.2f, y, t * 0.5f, c);
}

void icon_down(float x, float y, float s, col_t c) {
    float t = s * 0.13f + 0.5f;
    gfx_line(x - s * 0.4f, y - s * 0.18f, x, y + s * 0.2f, t, c);
    gfx_line(x, y + s * 0.2f, x + s * 0.4f, y - s * 0.18f, t, c);
    gfx_circle(x, y + s * 0.2f, t * 0.5f, c);
}

void icon_dots(float x, float y, float s, col_t c) {
    for (int i = -1; i <= 1; i++) gfx_circle(x + i * s * 0.36f, y, s * 0.1f + 0.4f, c);
}

void icon_speaker(float x, float y, float s, col_t c) {
    gfx_rect(x - s * 0.45f, y - s * 0.16f, s * 0.22f, s * 0.32f, c);
    gfx_tri(x - s * 0.3f, y - s * 0.16f, x + s * 0.02f, y - s * 0.42f, x + s * 0.02f, y + s * 0.42f, c);
    gfx_tri(x - s * 0.3f, y - s * 0.16f, x + s * 0.02f, y + s * 0.42f, x - s * 0.3f, y + s * 0.16f, c);
    gfx_arc(x + s * 0.06f, y, s * 0.2f, -0.8f, 0.8f, s * 0.08f + 0.5f, c);
    gfx_arc(x + s * 0.06f, y, s * 0.38f, -0.8f, 0.8f, s * 0.08f + 0.5f, c);
}

void icon_moon(float x, float y, float s, col_t c, col_t bg) {
    gfx_circle(x, y, s * 0.42f, c);
    gfx_circle(x + s * 0.2f, y - s * 0.16f, s * 0.34f, bg);
}

void icon_queue(float x, float y, float s, col_t c) {
    float t = s * 0.1f + 0.5f;
    for (int i = 0; i < 3; i++) gfx_line(x - s * 0.45f, y - s * 0.3f + i * s * 0.3f, x + s * 0.2f, y - s * 0.3f + i * s * 0.3f, t, c);
    gfx_tri(x + s * 0.28f, y + s * 0.05f, x + s * 0.28f, y + s * 0.45f, x + s * 0.5f, y + s * 0.25f, c);
}

void icon_eqbars(float x, float y, float s, col_t c, float t, bool moving) {
    float w = s * 0.2f;
    for (int i = 0; i < 4; i++) {
        float h;
        if (moving) h = 0.25f + 0.75f * fabsf(sinf(t * (5.1f + i * 1.7f) + i * 1.3f));
        else h = 0.25f + 0.1f * (float)(i % 2);
        float bh = s * h;
        gfx_rect(x - s * 0.5f + i * s * 0.27f, y + s * 0.5f - bh, w, bh, c);
    }
}

/* 3DStify mark: a green disc with a play-wave glyph */
void icon_logo(float x, float y, float r, col_t green, col_t ink) {
    gfx_circle(x, y, r, green);
    float s = r * 0.9f;
    gfx_tri(x - s * 0.34f, y - s * 0.46f, x - s * 0.34f, y + s * 0.46f, x + s * 0.18f, y, ink);
    gfx_arc(x + s * 0.12f, y, s * 0.42f, -0.85f, 0.85f, s * 0.11f, ink);
    gfx_arc(x + s * 0.12f, y, s * 0.66f, -0.75f, 0.75f, s * 0.11f, ink);
}
