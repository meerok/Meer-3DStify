#pragma once
/* 3DS-only setup calls for the citro2d backend */
void  gfx3ds_init(void);     /* after C2D_Init */
void  gfx3ds_exit(void);
void  gfx3ds_frame(void);    /* once per frame, before drawing */
void  gfx3ds_screen(int width);  /* call after C2D_SceneBegin: 400 top, 320 bottom */
float gfx3ds_line_height(void);
