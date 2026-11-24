#pragma once
#include <stdbool.h>
#include <stdint.h>

enum {
    BTN_A = 1 << 0, BTN_B = 1 << 1, BTN_X = 1 << 2, BTN_Y = 1 << 3,
    BTN_L = 1 << 4, BTN_R = 1 << 5, BTN_UP = 1 << 6, BTN_DOWN = 1 << 7,
    BTN_LEFT = 1 << 8, BTN_RIGHT = 1 << 9, BTN_START = 1 << 10, BTN_SELECT = 1 << 11,
    BTN_ZL = 1 << 12, BTN_ZR = 1 << 13
};

typedef struct {
    uint32_t down, held, up, repeat;
    bool  touch;              /* finger on the screen */
    int   tx, ty;
    float cx, cy;             /* circle pad, -1..1 (up is +) */
} Input;

void app_init(void);
bool app_update(const Input *in, float dt);   /* false = quit */
void app_draw_top(void);
void app_draw_bottom(void);
void app_exit(void);
bool app_keep_awake(void);       /* playing and "keep playing with the lid closed" is on */
bool app_screens_off(void);      /* SELECT toggled the backlight off */
void app_on_suspend(void);
void app_on_resume(void);
