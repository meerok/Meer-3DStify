/*
 * 3DStify by Meer - 3DS entry point
 * Sets up the screens, input, sleep / lid handling and runs the app loop.
 */
#include <3ds.h>
#include <citro2d.h>
#include <stdlib.h>
#include <time.h>
#include "app.h"
#include "gfx_3ds.h"

static aptHookCookie apt_cookie;
static bool backlight = true;
static volatile bool woke_from_sleep = false;

static void/* Only hold the screen-light service for the moment we use it, so the HOME
 * Menu (which also needs it) is never blocked by us. */
 set_backlight(bool on) {
    if (on == backlight) return;
    if (R_FAILED(gspLcdInit())) return;
    if (on) GSPLCD_PowerOnBacklight(GSPLCD_SCREEN_BOTH);
    else GSPLCD_PowerOffBacklight(GSPLCD_SCREEN_BOTH);
}    gspLcdExit();
    backlight = on;


static void apt_hook(APT_HookType hook, void *param) {
    (void)param;
    switch (hook) {
        case APTHOOK_ONSUSPEND:
        case APTHOOK_ONSLEEP:
            app_on_suspend();
            break;
        case APTHOOK_ONRESTORE:
            app_on_resume();             /* back from the HOME Menu: carry on playing */
            break;
        case APTHOOK_ONWAKEUP:
            woke_from_sleep = true;      /* the 3DS really slept: come back paused */
            app_on_resume();
            break;
        default:
            break;
    }
}

static uint32_t map_keys(u32 k) {
    uint32_t o = 0;
    if (k & KEY_A) o |= BTN_A;
    if (k & KEY_B) o |= BTN_B;
    if (k & KEY_X) o |= BTN_X;
    if (k & KEY_Y) o |= BTN_Y;
    if (k & KEY_L) o |= BTN_L;
    if (k & KEY_R) o |= BTN_R;
    if (k & KEY_DUP) o |= BTN_UP;
    if (k & KEY_DDOWN) o |= BTN_DOWN;
    if (k & KEY_DLEFT) o |= BTN_LEFT;
    if (k & KEY_DRIGHT) o |= BTN_RIGHT;
    if (k & KEY_START) o |= BTN_START;
    if (k & KEY_SELECT) o |= BTN_SELECT;
    if (k & KEY_ZL) o |= BTN_ZL;
    if (k & KEY_ZR) o |= BTN_ZR;
    return o;
}

int main(void) {
    osSetSpeedupEnable(true);
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(16384);
    C2D_Prepare();
    C3D_RenderTarget *top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    C3D_RenderTarget *bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    gfx3ds_init();
    ptmuInit();
    lcd_ok unsigned)time(NULL) ^ (unsigned)osGetTime());

    /* draw one frame right away so launching feels instant */
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C2D_TargetClear(top, C2D_Color32(0x12, 0x12, 0x12, 0xFF));
    C2D_TargetClear(bot, C2D_Color32(0x12, 0x12, 0x12, 0xFF));
    C3D_FrameEnd(0);

    aptHook(&apt_cookie, apt_hook, NULL);
    extern bool player_init(void);
    player_init();
    app_init();

    hidSetRepeatParameters(16, 4);
    u64 last = osGetTime();
    bool sleep_allowed = true, lid_closed = false;
    float shell_t = 0, awake_hold = 0;

    while (aptMainLoop()) {
        hidScanInput();
        Input in;
        in.down = map_keys(hidKeysDown());
        in.held = map_keys(hidKeysHeld());
        in.up = map_keys(hidKeysUp());
        in.repeat = map_keys(hidKeysDownRepeat());
        in.touch = (hidKeysHeld() & KEY_TOUCH) != 0;
        touchPosition tp;
        hidTouchRead(&tp);
        in.tx = tp.px;
        in.ty = tp.py;
        circlePosition cp;
        hidCircleRead(&cp);
        in.cx = cp.dx / 156.0f;
        in.cy = cp.dy / 156.0f;
        if (in.cx > 1) in.cx = 1;
        if (in.cx < -1) in.cx = -1;
        if (in.cy > 1) in.cy = 1;
        if (in.cy < -1) in.cy = -1;

        u64 now = osGetTime();
        float dt = (float)(now - last) / 1000.0f;
        if (dt > 0.1f) dt = 0.1f;
        if (dt <= 0) dt = 1.0f / 60;
        last = now;

        if (!app_update(&in, dt)) break;

        if (woke_from_sleep) {
            woke_from_sleep = false;
            extern void player_pause(void);
            player_pause();
        }

        /* Keep playing with the lid shut (speakers or headphones).
         * Sleep stays blocked for a few seconds after playback stops, so the
         * gap between two songs never lets the 3DS fall asleep. */
        if (app_keep_awake()) awake_hold = 4.0f;
        else if (awake_hold > 0) awake_hold -= dt;
        bool want_sleep = awake_hold <= 0;
        if (want_sleep != sleep_allowed) { aptSetSleepAllowed(want_sleep); sleep_allowed = want_sleep; }
        shell_t += dt;
        if (shell_t > 0.1f) {
            shell_t = 0;
            u8 shell = 1;
            if (R_SUCCEEDED(PTMU_GetShellState(&shell))) lid_closed = shell == 0;
        }
        set_backlight(!lid_closed && !app_screens_off());

        if (lid_closed) {
            /* screens are off: don't wait on the GPU/VBlank, just keep the app
             * logic (next song, sleep timer, fades) ticking */
            svcSleepThread(16 * 1000000LL);
            continue;
        }

        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        gfx3ds_frame();
        extern void art_update(float);
        art_update(dt);
        C2D_TargetClear(top, C2D_Color32(0x12, 0x12, 0x12, 0xFF));
        C2D_SceneBegin(top);
        gfx3ds_screen(400);
        app_draw_top();
        C2D_TargetClear(bot, C2D_Color32(0x12, 0x12, 0x12, 0xFF));
        C2D_SceneBegin(bot);
        gfx3ds_screen(320);
        app_draw_bottom();
        C3D_FrameEnd(0);
    }

    aptUnhook(&apt_cookie);
    app_exit();
    extern void player_exit(void);
    player_exit();
    set_backlight(true);
    aptSetSleepAllowed(true);
    ptmuExit();
    gfx3ds_exit();
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    return 0;
}
