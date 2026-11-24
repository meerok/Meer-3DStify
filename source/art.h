#pragma once
#include "gfx.h"

void      art_init(void);
void      art_exit(void);
void      art_reset(void);                 /* the library changed: forget everything */
void      art_update(float dt);            /* main thread, after C3D_FrameBegin: uploads + frees */
GfxImage *art_thumb(int album);            /* 64x64, NULL while loading / when there is none */
col_t     art_color(int album);            /* accent colour picked from the cover */
bool      art_has_color(int album);
/* big cover for the now playing view, with a crossfade from the previous one */
void      art_big(int album, GfxImage **cur, GfxImage **prev, float *fade);
