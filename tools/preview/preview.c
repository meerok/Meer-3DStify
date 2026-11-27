/*
 * 3DStify preview - renders the real UI into PNGs (top + bottom) for a few scenes.
 * Run from a folder that contains a fake "sdmc:" directory (see make_sd.py).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "app.h"
#include "art.h"
#include "library.h"
#include "player.h"
#include "sys.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

void host_init(const char *ttf);
void host_begin(int width);
void host_save(const char *unused, unsigned char *out);
void app_debug_scene(int tab_i, int track, bool sheet, double pos);
void app_debug_push(int type, int arg);
void app_debug_cursor(int sel);
extern double preview_pos;

static Input none;
static unsigned char buf[400 * 240 * 3];

static void settle(float secs) {
    for (float t = 0; t < secs; t += 0.05f) {
        app_update(&none, 0.05f);
        host_begin(400); app_draw_top();
        host_begin(320); app_draw_bottom();
        art_update(0.05f);
        sys_sleep_ms(15);
    }
}

static void shot(const char *dir, const char *name) {
    char p[512];
    host_begin(400); app_draw_top(); host_save(NULL, buf);
    snprintf(p, sizeof p, "%s/%s_top.png", dir, name);
    stbi_write_png(p, 400, 240, 3, buf, 400 * 3);
    host_begin(320); app_draw_bottom(); host_save(NULL, buf);
    snprintf(p, sizeof p, "%s/%s_bot.png", dir, name);
    stbi_write_png(p, 320, 240, 3, buf, 320 * 3);
    printf("wrote %s\n", name);
}

static int find_title(const char *t) {
    for (int i = 0; i < lib_ntracks; i++) if (!strcmp(lib_tracks[i].title, t)) return i;
    return 0;
}

int main(int argc, char **argv) {
    const char *out = argc > 1 ? argv[1] : ".";
    const char *font = argc > 2 ? argv[2] : "/usr/share/fonts/truetype/google-fonts/ofl/firasans/FiraSans-Regular.ttf";
    host_init(font);
    app_init();
    for (int i = 0; i < 400 && (lib_scanning() || lib_ntracks == 0); i++) { app_update(&none, 0.05f); sys_sleep_ms(10); }
    printf("library: %d tracks, %d albums, %d artists\n", lib_ntracks, lib_nalbums, lib_nartists);
    if (!lib_ntracks) return 1;
    const char *liked[] = { "Night Drive", "Chrome Skyline", "Lemonade Sky", "Glass Ocean", "Polaroid", NULL };
    for (int i = 0; liked[i]; i++) lib_set_liked(find_title(liked[i]), true);

    int t = find_title("Night Drive");
    app_debug_scene(0, t, false, 83);
    app_debug_cursor(4);
    settle(2.5f);
    shot(out, "songs");

    app_debug_scene(1, -1, false, 0);
    app_debug_push(0, lib_tracks[t].album_id);
    preview_pos = 131;
    settle(1.5f);
    shot(out, "album");

    app_debug_scene(0, -1, true, 0);
    settle(1.0f);
    shot(out, "sheet");

    app_debug_scene(4, -1, false, 0);
    app_debug_cursor(3);
    settle(1.0f);
    shot(out, "settings");

    app_debug_scene(3, -1, false, 0);
    settle(1.0f);
    shot(out, "liked");

    app_debug_scene(4, -1, false, 0);
    app_debug_cursor(9);
    settle(1.0f);
    shot(out, "settings2");

    app_debug_push(2, 0);
    settle(1.0f);
    shot(out, "folders");
    return 0;
}
