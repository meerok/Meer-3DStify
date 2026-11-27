/*
 * 3DStify by Meer - the app
 *
 * Top screen:    Now Playing (cover, title, progress, controls)
 * Bottom screen: library (Songs / Albums / Artists / Liked / Settings),
 *                mini player, and a pull-up player sheet with the queue.
 *
 * This file only talks to gfx.h / player.h / library.h / art.h so it can
 * also be built on a PC to render previews.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include "app.h"
#include "art.h"
#include "gfx.h"
#include "icons.h"
#include "library.h"
#include "player.h"

#define APP_VERSION "1.0.0"
#define SETTINGS_FILE APP_DIR "/settings.txt"

/* ------------------------------------------------------------ palette */
#define C_BG      HEXC(0x121212)
#define C_BLACK   HEXC(0x000000)
#define C_ROW_HI  HEXC(0x2A2A2A)
#define C_CARD    HEXC(0x282828)
#define C_WHITE   HEXC(0xFFFFFF)
#define C_SUB     HEXC(0xB3B3B3)
#define C_MUTED   HEXC(0x7A7A7A)
#define C_GREEN   HEXC(0x1ED760)
#define C_TRACK   HEXC(0x4D4D4D)

/* ------------------------------------------------------------- layout */
#define TW     400
#define BW     320
#define HB     34          /* page header height */
#define TAB_Y  206
#define MINI_H 36
#define MINI_Y (TAB_Y - MINI_H - 2)
#define HERO   (HB + 72)   /* detail page header block */

enum { TAB_SONGS, TAB_ALBUMS, TAB_ARTISTS, TAB_LIKED, TAB_SETTINGS, NTABS };
enum { PG_SONGS, PG_ALBUMS, PG_ARTISTS, PG_LIKED, PG_SETTINGS, PG_ALBUM, PG_ARTIST, PG_FOLDERS };
enum { CTX_ALL, CTX_ALBUM, CTX_ARTIST, CTX_LIKED };
enum { REP_OFF, REP_ALL, REP_ONE };
enum { SLEEP_OFF, SLEEP_15, SLEEP_30, SLEEP_45, SLEEP_60, SLEEP_EOT, SLEEP_COUNT };
enum { Z_NONE, Z_CONTENT, Z_HEADER, Z_MINI, Z_TABS, Z_SLIDER_VOL, Z_SHEET_TOP, Z_SHEET_SEEK,
       Z_SHEET_CTRL, Z_SHEET_VOL, Z_SHEET_LIST };

static const char *tab_names[NTABS] = { "Songs", "Albums", "Artists", "Liked", "Settings" };
static const char *rep_names[3] = { "Off", "All", "One" };
static const char *eq_names[EQ_COUNT] = { "Flat", "Bass Boost", "Bass Reducer", "Treble Boost", "Vocal Boost" };
static const char *sleep_names[SLEEP_COUNT] = { "Off", "15 min", "30 min", "45 min", "1 hour", "End of song" };
static const char *sort_names[SORT_COUNT] = { "Title", "Artist", "Album" };

/* ----------------------------------------------------------- settings */
static struct {
    int   shuffle, repeat, eq, sort, resume, lid;
    float volume;
    char  last[512];
    double last_pos;
    int   ctx_kind;
    char  ctx_key[256];
    char  folder[512];       /* music folder, "" = the whole SD card */
} S = { 0, REP_OFF, EQ_FLAT, SORT_TITLE, 1, 1, 0.8f, "", 0, CTX_ALL, "", "" };

/* -------------------------------------------------------------- queue */
static int  *ctx;            /* tracks of what we're playing from */
static int   nctx;
static int  *order;          /* play order (shuffled or not) as indices into ctx */
static int   qpos = -1;
static int   cur = -1;       /* current track */
static int   ctx_kind = CTX_ALL;
static char  ctx_key[256];
static char  ctx_name[256] = "Your Library";

/* ---------------------------------------------------------------- UI */
typedef struct {
    int   type, arg;
    float scroll, vel, target;
    int   sel;
    bool  snap;              /* ease scroll towards target (D-pad) */
} Page;

static Page  stk[NTABS][6];
static int   dep[NTABS];
static int   tab = TAB_SONGS;
static float tab_anim = 1, push_anim = 1;
static int   push_dir = 1;
static Page  push_prev;
static bool  cursor_mode = false;

static struct {
    bool  on, moved;
    int   zone, x0, y0, x, y, ly;
    float t, scroll0, vel;
} T;

static bool  sheet_open = false;
static float sheet_t = 0;
static float sheet_scroll = 0, sheet_vel = 0;

static float tsec = 0;
static char  toast_msg[96];
static float toast_t = 0;
static float vol_flash = 0;
static bool  scrub = false;
static double scrub_pos = 0;
static float scrub_hold = 0;
static int   sleep_mode = SLEEP_OFF;
static float sleep_left = 0;
static float save_t = 0;
static float err_t = 0;
static bool  screens_off = false;
static col_t accent = HEXC(0x404040), accent_target = HEXC(0x404040);
static float anim_toggle[16];
static float sel_y = 0;      /* eased cursor position */
static bool  quit_req = false;

/* ============================================================ helpers */

static float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static float ease_out(float t) { t = clampf(t, 0, 1); return 1 - (1 - t) * (1 - t) * (1 - t); }

static void fmt_time(double s, char *out, size_t n) {
    if (s < 0) s = 0;
    int t = (int)s;
    snprintf(out, n, "%d:%02d", t / 60, t % 60);
}

static void toast(const char *msg) {
    snprintf(toast_msg, sizeof toast_msg, "%s", msg);
    toast_t = 2.0f;
}

static int cur_album(void) { return cur >= 0 && cur < lib_ntracks ? lib_tracks[cur].album_id : -1; }

/* ================================================================ settings */

static void settings_load(void) {
    FILE *f = fopen(SETTINGS_FILE, "r");
    if (!f) return;
    char line[800];
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        const char *k = line, *v = eq + 1;
        if (!strcmp(k, "shuffle")) S.shuffle = atoi(v) != 0;
        else if (!strcmp(k, "repeat")) S.repeat = atoi(v) % 3;
        else if (!strcmp(k, "eq")) S.eq = atoi(v) % EQ_COUNT;
        else if (!strcmp(k, "sort")) S.sort = atoi(v) % SORT_COUNT;
        else if (!strcmp(k, "resume")) S.resume = atoi(v) != 0;
        else if (!strcmp(k, "lid")) S.lid = atoi(v) != 0;
        else if (!strcmp(k, "volume")) S.volume = clampf((float)atof(v), 0, 1);
        else if (!strcmp(k, "last")) snprintf(S.last, sizeof S.last, "%s", v);
        else if (!strcmp(k, "pos")) S.last_pos = atof(v);
        else if (!strcmp(k, "ctx")) S.ctx_kind = atoi(v);
        else if (!strcmp(k, "ctxkey")) snprintf(S.ctx_key, sizeof S.ctx_key, "%s", v);
        else if (!strcmp(k, "folder")) snprintf(S.folder, sizeof S.folder, "%s", v);
    }
    fclose(f);
    if (S.repeat < 0) S.repeat = 0;
    if (S.eq < 0) S.eq = 0;
    if (S.sort < 0) S.sort = 0;
}

static void settings_save(void) {
    if (cur >= 0) {
        snprintf(S.last, sizeof S.last, "%s", lib_tracks[cur].path);
        S.last_pos = player_pos();
        S.ctx_kind = ctx_kind;
        snprintf(S.ctx_key, sizeof S.ctx_key, "%s", ctx_key);
    }
    FILE *f = fopen(SETTINGS_FILE ".tmp", "w");
    if (!f) return;
    fprintf(f, "shuffle=%d\nrepeat=%d\neq=%d\nsort=%d\nresume=%d\nlid=%d\nvolume=%.2f\n",
            S.shuffle, S.repeat, S.eq, S.sort, S.resume, S.lid, S.volume);
    fprintf(f, "last=%s\npos=%.1f\nctx=%d\nctxkey=%s\nfolder=%s\n", S.last, S.last_pos, S.ctx_kind, S.ctx_key, S.folder);
    fclose(f);
    remove(SETTINGS_FILE);
    rename(SETTINGS_FILE ".tmp", SETTINGS_FILE);
}

/* =================================================================== queue */

static void shuffle_order(int keep_first) {
    for (int i = 0; i < nctx; i++) order[i] = i;
    for (int i = nctx - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int t = order[i]; order[i] = order[j]; order[j] = t;
    }
    if (keep_first >= 0) {
        for (int i = 0; i < nctx; i++)
            if (order[i] == keep_first) { order[i] = order[0]; order[0] = keep_first; break; }
    }
}

static void play_track(int t, double start, bool autoplay) {
    if (t < 0 || t >= lib_ntracks) return;
    cur = t;
    err_t = 0;
    player_load(lib_tracks[t].path, start, autoplay);
}

static void set_context(const int *list, int n, int kind, const char *key, const char *name) {
    free(ctx);
    free(order);
    nctx = n;
    ctx = (int *)malloc((size_t)(n + 1) * sizeof(int));
    order = (int *)malloc((size_t)(n + 1) * sizeof(int));
    if (!ctx || !order) { nctx = 0; return; }
    memcpy(ctx, list, (size_t)n * sizeof(int));
    ctx_kind = kind;
    snprintf(ctx_key, sizeof ctx_key, "%s", key ? key : "");
    snprintf(ctx_name, sizeof ctx_name, "%s", name ? name : "");
}

/* start playing list[start] (start < 0: from the top, or a random song when shuffling) */
static void start_list(const int *list, int n, int start, int kind, const char *key, const char *name, bool force_shuffle) {
    if (n <= 0) return;
    if (force_shuffle && !S.shuffle) { S.shuffle = 1; toast("Shuffle on"); }
    set_context(list, n, kind, key, name);
    if (!nctx) return;
    if (start < 0) start = S.shuffle ? rand() % n : 0;
    if (S.shuffle) { shuffle_order(start); qpos = 0; }
    else { for (int i = 0; i < n; i++) order[i] = i; qpos = start; }
    play_track(ctx[order[qpos]], 0, true);
}

static bool ctx_is(int kind, const char *key) {
    return nctx > 0 && ctx_kind == kind && !strcmp(ctx_key, key ? key : "");
}

static void next_track(bool user) {
    if (!nctx || qpos < 0) return;
    if (!user && S.repeat == REP_ONE) { play_track(cur, 0, true); return; }
    if (!user && sleep_mode == SLEEP_EOT) {
        sleep_mode = SLEEP_OFF;
        toast("Sleep timer ended");
        if (qpos + 1 < nctx) { qpos++; play_track(ctx[order[qpos]], 0, false); }
        else player_seek(0);
        return;
    }
    if (qpos + 1 < nctx) { qpos++; play_track(ctx[order[qpos]], 0, true); return; }
    if (S.repeat == REP_ALL || (user && S.repeat != REP_OFF)) {
        if (S.shuffle) shuffle_order(-1);
        qpos = 0;
        play_track(ctx[order[qpos]], 0, true);
    } else {
        /* end of the list: back to the first song, paused (like Spotify) */
        qpos = 0;
        play_track(ctx[order[qpos]], 0, false);
    }
}

static void prev_track(void) {
    if (!nctx || qpos < 0) return;
    if (player_pos() > 3.0 || (qpos == 0 && S.repeat != REP_ALL)) { player_seek(0); player_play(); return; }
    qpos = qpos > 0 ? qpos - 1 : nctx - 1;
    play_track(ctx[order[qpos]], 0, true);
}

static void set_shuffle(bool on) {
    S.shuffle = on;
    if (nctx && qpos >= 0) {
        int c = order[qpos];
        if (on) { shuffle_order(c); qpos = 0; }
        else { for (int i = 0; i < nctx; i++) order[i] = i; qpos = c; }
    }
    toast(on ? "Shuffle on" : "Shuffle off");
}

static void cycle_repeat(void) {
    S.repeat = (S.repeat + 1) % 3;
    toast(S.repeat == REP_OFF ? "Repeat off" : S.repeat == REP_ALL ? "Repeat all" : "Repeat one");
}

static void toggle_like(int t) {
    if (t < 0) return;
    bool on = !lib_tracks[t].liked;
    lib_set_liked(t, on);
    toast(on ? "Added to Liked Songs" : "Removed from Liked Songs");
    if (ctx_kind == CTX_LIKED) {
        /* keep the queue as is; it refreshes next time Liked Songs is played */
    }
}

static void set_volume(float v) {
    S.volume = clampf(v, 0, 1);
    player_set_volume(S.volume);
    vol_flash = 1.4f;
}

static void set_sleep(int m) {
    sleep_mode = m;
    static const int mins[SLEEP_COUNT] = { 0, 15, 30, 45, 60, 0 };
    sleep_left = (float)mins[m] * 60.0f;
    if (m == SLEEP_OFF) toast("Sleep timer off");
    else if (m == SLEEP_EOT) toast("Stopping at the end of this song");
    else { char b[64]; snprintf(b, sizeof b, "Sleep timer set: %s", sleep_names[m]); toast(b); }
}

/* restore the queue from settings */
static void restore_context(void) {
    int t = lib_find_path(S.last);
    if (t < 0 || !S.resume) return;
    const int *list = lib_sorted;
    int n = lib_ntracks;
    int kind = CTX_ALL;
    const char *key = "";
    const char *name = "Your Library";
    if (S.ctx_kind == CTX_ALBUM) {
        for (int a = 0; a < lib_nalbums; a++)
            if (!strcmp(lib_albums[a].name, S.ctx_key)) { list = lib_albums[a].tracks; n = lib_albums[a].ntracks; kind = CTX_ALBUM; key = name = lib_albums[a].name; break; }
    } else if (S.ctx_kind == CTX_ARTIST) {
        for (int a = 0; a < lib_nartists; a++)
            if (!strcmp(lib_artists[a].name, S.ctx_key)) { list = lib_artists[a].tracks; n = lib_artists[a].ntracks; kind = CTX_ARTIST; key = name = lib_artists[a].name; break; }
    } else if (S.ctx_kind == CTX_LIKED && lib_nliked > 0) {
        list = lib_liked; n = lib_nliked; kind = CTX_LIKED; name = "Liked Songs";
    }
    int idx = -1;
    for (int i = 0; i < n; i++) if (list[i] == t) { idx = i; break; }
    if (idx < 0) { list = lib_sorted; n = lib_ntracks; kind = CTX_ALL; key = ""; name = "Your Library";
        for (int i = 0; i < n; i++) if (list[i] == t) { idx = i; break; } }
    if (idx < 0) return;
    set_context(list, n, kind, key, name);
    if (!nctx) return;
    if (S.shuffle) { shuffle_order(idx); qpos = 0; }
    else { for (int i = 0; i < n; i++) order[i] = i; qpos = idx; }
    play_track(t, S.last_pos > 2 ? S.last_pos : 0, false);
}

/* =================================================================== pages */

static Page *page(void) { return &stk[tab][dep[tab] - 1]; }

static bool is_detail(const Page *p) { return p->type == PG_ALBUM || p->type == PG_ARTIST || p->type == PG_LIKED; }

static float content_top(const Page *p) { return is_detail(p) ? 0 : HB; }
static float content_bottom(void) { return cur >= 0 ? MINI_Y - 2 : TAB_Y; }
static float hero_h(const Page *p) { return is_detail(p) ? HERO : 0; }
static float row_h(const Page *p) {
    if (p->type == PG_ALBUMS || p->type == PG_ARTISTS) return 48;
    if (p->type == PG_SETTINGS) return 34;
    return 40;
}

#define NSET 15
enum { SI_H_PLAY, SI_SHUFFLE, SI_REPEAT, SI_EQ, SI_VOLUME, SI_SLEEP, SI_RESUME, SI_LID,
       SI_H_LIB, SI_FOLDER, SI_SORT, SI_RESCAN, SI_H_ABOUT, SI_ABOUT, SI_CREDIT };

/* ---------------------------------------------------- folder browser */
static char   fb_path[512] = "sdmc:/";
static char **fb_dirs;
static int    fb_n;

static bool fb_is_root(void) { return !strcmp(fb_path, "sdmc:/"); }
static int  fb_first_dir(void) { return fb_is_root() ? 1 : 2; }   /* row 0 "use", row 1 "up" */

static int cmp_name(const void *a, const void *b) { return strcasecmp(*(char *const *)a, *(char *const *)b); }

static void fb_load(const char *path) {
    for (int i = 0; i < fb_n; i++) free(fb_dirs[i]);
    free(fb_dirs);
    fb_dirs = NULL;
    fb_n = 0;
    snprintf(fb_path, sizeof fb_path, "%s", path && *path ? path : "sdmc:/");
    DIR *d = opendir(fb_path);
    if (!d) { snprintf(fb_path, sizeof fb_path, "sdmc:/"); d = opendir(fb_path); }
    if (!d) return;
    struct dirent *e;
    int cap = 0;
    while ((e = readdir(d)) && fb_n < 800) {
        const char *nm = e->d_name;
        if (nm[0] == '.') continue;
        bool is_dir = false;
#ifdef DT_DIR
        if (e->d_type == DT_DIR) is_dir = true;
        else if (e->d_type == DT_UNKNOWN)
#endif
        {
            char full[800];
            struct stat st;
            snprintf(full, sizeof full, "%s%s%s", fb_path, fb_path[strlen(fb_path) - 1] == '/' ? "" : "/", nm);
            if (stat(full, &st) == 0 && S_ISDIR(st.st_mode)) is_dir = true;
        }
        if (!is_dir) continue;
        if (fb_is_root() && (!strcasecmp(nm, "Nintendo 3DS") || !strcasecmp(nm, "Nintendo DSiWare"))) continue;
        if (fb_n == cap) {
            cap = cap ? cap * 2 : 32;
            char **nd = (char **)realloc(fb_dirs, (size_t)cap * sizeof(char *));
            if (!nd) break;
            fb_dirs = nd;
        }
        fb_dirs[fb_n] = strdup(nm);
        if (fb_dirs[fb_n]) fb_n++;
    }
    closedir(d);
    if (fb_n > 1) qsort(fb_dirs, (size_t)fb_n, sizeof(char *), cmp_name);
}

static void fb_up(void) {
    char p[512];
    snprintf(p, sizeof p, "%s", fb_path);
    size_t n = strlen(p);
    if (n > 0 && p[n - 1] == '/') p[--n] = 0;
    char *sl = strrchr(p, '/');
    if (!sl) { fb_load("sdmc:/"); return; }
    if (sl > p && sl[-1] == ':') sl[1] = 0;      /* back to "sdmc:/" */
    else *sl = 0;
    fb_load(p);
}

static const char *folder_label(const char *f) {
    if (!f || !*f || !strcmp(f, "sdmc:/")) return "Whole SD card";
    const char *sl = strrchr(f, '/');
    return sl && sl[1] ? sl + 1 : f;
}

static const int *page_tracks(const Page *p, int *n) {
    switch (p->type) {
        case PG_SONGS:  *n = lib_ntracks; return lib_sorted;
        case PG_LIKED:  *n = lib_nliked; return lib_liked;
        case PG_ALBUM:  if (p->arg < lib_nalbums) { *n = lib_albums[p->arg].ntracks; return lib_albums[p->arg].tracks; } break;
        case PG_ARTIST: if (p->arg < lib_nartists) { *n = lib_artists[p->arg].ntracks; return lib_artists[p->arg].tracks; } break;
    }
    *n = 0;
    return NULL;
}

static int page_rows(const Page *p) {
    int n;
    switch (p->type) {
        case PG_ALBUMS:   return lib_nalbums;
        case PG_ARTISTS:  return lib_nartists;
        case PG_SETTINGS: return NSET;
        case PG_FOLDERS:  return fb_first_dir() + fb_n;
        default: page_tracks(p, &n); return n;
    }
}

static float max_scroll(const Page *p) {
    float h = hero_h(p) + page_rows(p) * row_h(p) + 10;
    float vis = content_bottom() - content_top(p);
    return h > vis ? h - vis : 0;
}

static bool row_selectable(const Page *p, int i) {
    if (p->type != PG_SETTINGS) return true;
    return i != SI_H_PLAY && i != SI_H_LIB && i != SI_H_ABOUT && i != SI_CREDIT && i != SI_ABOUT;
}

static void reset_tab(int t) {
    dep[t] = 1;
    memset(&stk[t][0], 0, sizeof(Page));
    stk[t][0].type = t == TAB_SONGS ? PG_SONGS : t == TAB_ALBUMS ? PG_ALBUMS : t == TAB_ARTISTS ? PG_ARTISTS :
                     t == TAB_LIKED ? PG_LIKED : PG_SETTINGS;
    if (t == TAB_SETTINGS) stk[t][0].sel = SI_SHUFFLE;
}

static void push_page(int type, int arg) {
    if (dep[tab] >= 6) return;
    push_prev = *page();
    Page *p = &stk[tab][dep[tab]++];
    memset(p, 0, sizeof *p);
    p->type = type;
    p->arg = arg;
    push_anim = 0;
    push_dir = 1;
}

static void pop_page(void) {
    if (dep[tab] <= 1) return;
    push_prev = *page();
    dep[tab]--;
    push_anim = 0;
    push_dir = -1;
}

static void switch_tab(int t) {
    if (t == tab) {
        if (dep[tab] > 1) { while (dep[tab] > 1) dep[tab]--; push_anim = 1; }
        else { page()->target = 0; page()->snap = true; page()->sel = row_selectable(page(), 0) ? 0 : 1; }
        return;
    }
    tab = t;
    tab_anim = 0;
}

static void ensure_visible(Page *p) {
    float rh = row_h(p);
    float y = hero_h(p) + p->sel * rh;
    float vis = content_bottom() - content_top(p);
    float top = is_detail(p) ? HB : 0;
    float t = p->snap ? p->target : p->scroll;
    if (y - t < top + 4) t = y - top - 4;
    if (y + rh - t > vis - 4) t = y + rh - vis + 4;
    if (p->sel == 0 && is_detail(p)) t = 0;
    float ms = max_scroll(p);
    p->target = clampf(t, 0, ms);
    p->snap = true;
    p->vel = 0;
}

/* ------------------------------------------------------------- actions */

static void open_row(Page *p, int i);

static void settings_activate(int i, int dir) {
    switch (i) {
        case SI_SHUFFLE: set_shuffle(!S.shuffle); break;
        case SI_REPEAT:  S.repeat = (S.repeat + (dir < 0 ? 2 : 1)) % 3; break;
        case SI_EQ:      S.eq = (S.eq + (dir < 0 ? EQ_COUNT - 1 : 1)) % EQ_COUNT; player_set_eq(S.eq); break;
        case SI_VOLUME:  set_volume(S.volume + (dir < 0 ? -0.05f : 0.05f)); break;
        case SI_SLEEP:   set_sleep((sleep_mode + (dir < 0 ? SLEEP_COUNT - 1 : 1)) % SLEEP_COUNT); break;
        case SI_RESUME:  S.resume = !S.resume; break;
        case SI_LID:     S.lid = !S.lid; break;
        case SI_SORT:    S.sort = (S.sort + (dir < 0 ? SORT_COUNT - 1 : 1)) % SORT_COUNT; lib_sort(S.sort); break;
        case SI_FOLDER:
            fb_load(S.folder[0] ? S.folder : "sdmc:/");
            push_page(PG_FOLDERS, 0);
            return;
        case SI_RESCAN:
            if (!lib_scanning()) { lib_scan_start(); toast("Scanning your SD card..."); }
            break;
    }
    settings_save();
}

static void play_page(Page *p, bool shuffle) {
    int n;
    const int *list = page_tracks(p, &n);
    if (!list || !n) return;
    int kind = CTX_ALL;
    const char *key = "", *name = "Your Library";
    if (p->type == PG_LIKED) { kind = CTX_LIKED; name = "Liked Songs"; }
    else if (p->type == PG_ALBUM) { kind = CTX_ALBUM; key = name = lib_albums[p->arg].name; }
    else if (p->type == PG_ARTIST) { kind = CTX_ARTIST; key = name = lib_artists[p->arg].name; }
    if (ctx_is(kind, key) && cur >= 0 && !shuffle) { player_toggle(); return; }
    start_list(list, n, -1, kind, key, name, shuffle);
}

static void use_folder(void) {
    if (lib_scanning()) { toast("Wait for the current scan to finish"); return; }
    if (fb_is_root()) S.folder[0] = 0;
    else snprintf(S.folder, sizeof S.folder, "%s", fb_path);
    settings_save();
    lib_set_root(S.folder);
    lib_scan_start();
    char b[96];
    snprintf(b, sizeof b, "Music folder: %s", folder_label(S.folder));
    toast(b);
    pop_page();
}

static void open_row(Page *p, int i) {
    if (i < 0 || i >= page_rows(p)) return;
    if (p->type == PG_FOLDERS) {
        if (i == 0) { use_folder(); return; }
        if (!fb_is_root() && i == 1) fb_up();
        else {
            int k = i - fb_first_dir();
            if (k < 0 || k >= fb_n) return;
            char np[512];
            snprintf(np, sizeof np, "%s%s%s", fb_path, fb_path[strlen(fb_path) - 1] == '/' ? "" : "/", fb_dirs[k]);
            fb_load(np);
        }
        p->sel = 0;
        p->scroll = p->target = 0;
        p->vel = 0;
        return;
    }
    if (p->type == PG_ALBUMS) { push_page(PG_ALBUM, i); return; }
    if (p->type == PG_ARTISTS) { push_page(PG_ARTIST, i); return; }
    if (p->type == PG_SETTINGS) { settings_activate(i, 1); return; }
    int n;
    const int *list = page_tracks(p, &n);
    if (!list) return;
    int kind = CTX_ALL;
    const char *key = "", *name = "Your Library";
    if (p->type == PG_LIKED) { kind = CTX_LIKED; name = "Liked Songs"; }
    else if (p->type == PG_ALBUM) { kind = CTX_ALBUM; key = name = lib_albums[p->arg].name; }
    else if (p->type == PG_ARTIST) { kind = CTX_ARTIST; key = name = lib_artists[p->arg].name; }
    if (list[i] == cur && ctx_is(kind, key)) { player_toggle(); return; }
    start_list(list, n, i, kind, key, name, false);
}

/* ================================================================= update */

static void scroll_physics(float *scroll, float *vel, float ms, float dt, bool dragging) {
    if (dragging) return;
    *scroll += *vel * dt;
    *vel *= powf(0.06f, dt);                       /* friction */
    if (fabsf(*vel) < 4) *vel = 0;
    if (*scroll < 0) { *scroll += (0 - *scroll) * clampf(dt * 14, 0, 1); if (*vel < 0) *vel *= 0.5f; }
    if (*scroll > ms) { *scroll += (ms - *scroll) * clampf(dt * 14, 0, 1); if (*vel > 0) *vel *= 0.5f; }
}

static float rubber(float v, float ms) {
    if (v < 0) return -powf(-v, 0.75f);
    if (v > ms) return ms + powf(v - ms, 0.75f);
    return v;
}

/* bottom-sheet layout */
#define SH_SEEK_Y   44
#define SH_CTRL_Y   88
#define SH_VOL_Y    124
#define SH_LIST_Y   156
#define SH_ROW      36

static float sheet_max_scroll(void) {
    int n = nctx - qpos;           /* now playing + next up */
    float h = (float)(n > 0 ? n : 0) * SH_ROW + 26;
    float vis = 240 - SH_LIST_Y;
    return h > vis ? h - vis : 0;
}

static int touch_zone(int x, int y) {
    if (sheet_open) {
        if (y < 28) return Z_SHEET_TOP;
        if (y >= SH_SEEK_Y - 12 && y < SH_SEEK_Y + 18) return Z_SHEET_SEEK;
        if (y >= SH_CTRL_Y - 20 && y < SH_CTRL_Y + 20) return Z_SHEET_CTRL;
        if (y >= SH_VOL_Y - 12 && y < SH_VOL_Y + 14) return Z_SHEET_VOL;
        if (y >= SH_LIST_Y) return Z_SHEET_LIST;
        return Z_NONE;
    }
    if (y >= TAB_Y) return Z_TABS;
    if (cur >= 0 && y >= MINI_Y && y < MINI_Y + MINI_H) return Z_MINI;
    Page *p = page();
    if (y < HB && (!is_detail(p) || dep[tab] > 1 || 1)) {
        /* detail pages: header area belongs to the hero unless scrolled */
        if (!is_detail(p) || x < 40 || p->scroll > 40) return Z_HEADER;
    }
    if (y < content_bottom()) {
        if (p->type == PG_SETTINGS) {
            int row = (int)((y - content_top(p) + p->scroll) / row_h(p));
            if (row == SI_VOLUME && x >= 140) return Z_SLIDER_VOL;
        }
        return Z_CONTENT;
    }
    return Z_NONE;
}

static void seek_from_x(int x, float x0, float x1) {
    double len = player_len();
    if (len <= 0) return;
    scrub = true;
    scrub_pos = clampf((x - x0) / (x1 - x0), 0, 1) * len;
}

static void tap(int zone, int x, int y) {
    Page *p = page();
    switch (zone) {
        case Z_TABS: switch_tab(clampf(x / (BW / NTABS), 0, NTABS - 1)); break;
        case Z_MINI:
            if (x >= 238 && x < 274) toggle_like(cur);
            else if (x >= 274) player_toggle();
            else { sheet_open = true; sheet_scroll = 0; sheet_vel = 0; }
            break;
        case Z_HEADER:
            if (dep[tab] > 1 && x < 40) pop_page();
            else if (p->type == PG_SONGS && x > 270) play_page(p, true);
            break;
        case Z_CONTENT: {
            float ly = y - content_top(p) + p->scroll;
            if (ly < hero_h(p)) {
                float by = HB + 52;               /* hero buttons, in content coords */
                if (fabsf(ly - by) < 20 && x > 270) play_page(p, false);
                else if (fabsf(ly - by) < 20 && x > 232 && x <= 270) set_shuffle(!S.shuffle);
                break;
            }
            int row = (int)((ly - hero_h(p)) / row_h(p));
            if (row >= 0 && row < page_rows(p) && row_selectable(p, row)) {
                p->sel = row;
                if (p->type == PG_SETTINGS) {
                    int dir = 1;
                    if (row == SI_REPEAT || row == SI_EQ || row == SI_SLEEP || row == SI_SORT) dir = x < 200 ? -1 : 1;
                    settings_activate(row, dir);
                } else if ((p->type == PG_SONGS || p->type == PG_LIKED || p->type == PG_ARTIST || p->type == PG_ALBUM) && x > 282) {
                    int n;
                    const int *list = page_tracks(p, &n);
                    if (list) toggle_like(list[row]);
                } else {
                    open_row(p, row);
                }
            }
            break;
        }
        case Z_SHEET_TOP: sheet_open = false; break;
        case Z_SHEET_CTRL:
            if (x < 70) set_shuffle(!S.shuffle);
            else if (x < 130) prev_track();
            else if (x < 190) player_toggle();
            else if (x < 250) next_track(true);
            else cycle_repeat();
            break;
        case Z_SHEET_LIST: {
            float ly = y - SH_LIST_Y + sheet_scroll - 22;
            int k = (int)floorf(ly / SH_ROW);
            if (ly >= 0 && qpos >= 0 && k >= 0 && qpos + k < nctx) {
                if (x > 282) toggle_like(ctx[order[qpos + k]]);
                else if (k == 0) player_toggle();
                else { qpos += k; play_track(ctx[order[qpos]], 0, true); }
            }
            break;
        }
    }
}

static void handle_touch(const Input *in, float dt) {
    if (in->touch && !T.on) {
        T.on = true;
        T.moved = false;
        T.x0 = T.x = in->tx;
        T.y0 = T.y = T.ly = in->ty;
        T.t = 0;
        T.vel = 0;
        T.zone = touch_zone(in->tx, in->ty);
        cursor_mode = false;
        if (T.zone == Z_CONTENT || T.zone == Z_HEADER) { T.scroll0 = page()->scroll; page()->vel = 0; page()->snap = false; }
        if (T.zone == Z_SHEET_LIST) { T.scroll0 = sheet_scroll; sheet_vel = 0; }
        if (T.zone == Z_SHEET_SEEK) seek_from_x(in->tx, 16, 304);
        if (T.zone == Z_SHEET_VOL) set_volume((in->tx - 44.0f) / (292.0f - 44.0f));
        if (T.zone == Z_SLIDER_VOL) set_volume((in->tx - 150.0f) / (300.0f - 150.0f));
        return;
    }
    if (in->touch && T.on) {
        T.t += dt;
        T.x = in->tx;
        T.y = in->ty;
        if (abs(T.y - T.y0) > 6 || abs(T.x - T.x0) > 10) T.moved = true;
        float inst = dt > 0 ? (float)(T.ly - T.y) / dt : 0;
        T.vel = T.vel * 0.6f + inst * 0.4f;
        T.ly = T.y;
        if (T.zone == Z_CONTENT && T.moved) {
            Page *p = page();
            p->scroll = rubber(T.scroll0 - (T.y - T.y0), max_scroll(p));
        } else if (T.zone == Z_SHEET_LIST && T.moved) {
            sheet_scroll = rubber(T.scroll0 - (T.y - T.y0), sheet_max_scroll());
        } else if (T.zone == Z_SHEET_SEEK) {
            seek_from_x(in->tx, 16, 304);
        } else if (T.zone == Z_SHEET_VOL) {
            set_volume((in->tx - 44.0f) / (292.0f - 44.0f));
        } else if (T.zone == Z_SLIDER_VOL) {
            set_volume((in->tx - 150.0f) / (300.0f - 150.0f));
        } else if (T.zone == Z_SHEET_TOP && T.y - T.y0 > 40) {
            sheet_open = false;                      /* swipe down to close */
            T.zone = Z_NONE;
        } else if (T.zone == Z_MINI && T.y0 - T.y > 24) {
            sheet_open = true;                       /* swipe up to open */
            sheet_scroll = 0;
            T.zone = Z_NONE;
        }
        return;
    }
    if (!in->touch && T.on) {
        T.on = false;
        if (T.zone == Z_SHEET_SEEK) {
            if (scrub) { player_seek(scrub_pos); scrub = false; }
            return;
        }
        if (T.zone == Z_SLIDER_VOL || T.zone == Z_SHEET_VOL) { settings_save(); return; }
        if (!T.moved) tap(T.zone, T.x0, T.y0);
        else if (T.zone == Z_CONTENT) page()->vel = clampf(T.vel, -2400, 2400);
        else if (T.zone == Z_SHEET_LIST) sheet_vel = clampf(T.vel, -2400, 2400);
    }
}

static void handle_buttons(const Input *in, float dt) {
    uint32_t kd = in->down, kr = in->repeat;
    if (kd & BTN_START) { quit_req = true; return; }
    if (kd & BTN_SELECT) { screens_off = !screens_off; return; }
    if (screens_off && kd) { screens_off = false; return; }

    if (kd & BTN_Y) player_toggle();
    if (kd & BTN_X) toggle_like(cur);
    if (kd & BTN_R) next_track(true);
    if (kd & BTN_L) prev_track();
    if (kr & BTN_ZR) set_volume(S.volume + 0.05f);
    if (kr & BTN_ZL) set_volume(S.volume - 0.05f);

    /* circle pad: up/down volume, left/right scrub */
    static float vol_acc = 0;
    if (fabsf(in->cy) > 0.5f) {
        vol_acc += dt;
        if (vol_acc > 0.09f) { vol_acc = 0; set_volume(S.volume + (in->cy > 0 ? 0.02f : -0.02f)); }
    } else vol_acc = 0.09f;
    if (fabsf(in->cx) > 0.45f && player_len() > 0) {
        if (!scrub) { scrub = true; scrub_pos = player_pos(); scrub_hold = 0; }
        scrub_hold += dt;
        float speed = 6.0f + scrub_hold * 18.0f;           /* seconds per second, accelerating */
        if (speed > 60) speed = 60;
        scrub_pos += (in->cx > 0 ? 1 : -1) * speed * dt * fabsf(in->cx);
        scrub_pos = clampf((float)scrub_pos, 0, (float)player_len());
    } else if (scrub && !(T.on && T.zone == Z_SHEET_SEEK)) {
        player_seek(scrub_pos);
        player_play();
        scrub = false;
    }

    if (sheet_open) {
        if (kd & BTN_B) sheet_open = false;
        if (kd & BTN_A) player_toggle();
        if (kr & BTN_DOWN) sheet_vel = 600;
        if (kr & BTN_UP) sheet_vel = -600;
        return;
    }

    Page *p = page();
    int n = page_rows(p);
    if (kd & BTN_B) {
        if (p->type == PG_FOLDERS && !fb_is_root()) { fb_up(); p->sel = 0; p->scroll = p->target = 0; }
        else if (dep[tab] > 1) pop_page();
    }
    if (kr & (BTN_UP | BTN_DOWN)) {
        if (!cursor_mode) { cursor_mode = true; }
        else {
            int d = (kr & BTN_DOWN) ? 1 : -1;
            int s = p->sel;
            do { s += d; } while (s >= 0 && s < n && !row_selectable(p, s));
            if (s >= 0 && s < n) p->sel = s;
        }
        if (n > 0) ensure_visible(p);
    }
    if (kr & (BTN_LEFT | BTN_RIGHT)) {
        int d = (kr & BTN_RIGHT) ? 1 : -1;
        if (p->type == PG_SETTINGS && cursor_mode &&
            (p->sel == SI_REPEAT || p->sel == SI_EQ || p->sel == SI_VOLUME || p->sel == SI_SLEEP || p->sel == SI_SORT)) {
            settings_activate(p->sel, d);
        } else if (kd & (BTN_LEFT | BTN_RIGHT)) {
            switch_tab((tab + d + NTABS) % NTABS);
        }
    }
    if ((kd & BTN_A) && n > 0) {
        cursor_mode = true;
        if (p->sel >= n) p->sel = n - 1;
        open_row(p, p->sel);
    }
}

static void on_library_changed(void) {
    char path[512] = "";
    if (cur >= 0 && cur < lib_ntracks) snprintf(path, sizeof path, "%s", lib_tracks[cur].path);
    (void)path;
}

bool app_update(const Input *in, float dt) {
    tsec += dt;

    /* a finished background scan */
    if (lib_scanning()) {
        char keep[512] = "";
        if (cur >= 0) snprintf(keep, sizeof keep, "%s", lib_tracks[cur].path);
        if (lib_scan_poll()) {
            art_reset();
            for (int t = 0; t < NTABS; t++) reset_tab(t);
            int t = lib_find_path(keep);
            if (t >= 0) {
                cur = t;
                int idx = 0;
                for (int i = 0; i < lib_ntracks; i++) if (lib_sorted[i] == t) { idx = i; break; }
                set_context(lib_sorted, lib_ntracks, CTX_ALL, "", "Your Library");
                if (nctx) {
                    if (S.shuffle) { shuffle_order(idx); qpos = 0; }
                    else { for (int i = 0; i < nctx; i++) order[i] = i; qpos = idx; }
                }
            } else if (keep[0]) {
                player_stop();
                cur = -1; qpos = -1; nctx = 0;
            } else if (lib_ntracks > 0 && cur < 0) {
                S.last[0] = 0;
            }
            char b[64];
            snprintf(b, sizeof b, "Found %d song%s", lib_ntracks, lib_ntracks == 1 ? "" : "s");
            toast(b);
            on_library_changed();
        }
    }

    handle_touch(in, dt);
    handle_buttons(in, dt);
    if (quit_req) return false;

    /* track finished / failed */
    if (player_take_ended()) next_track(false);
    if (player_state() == PL_ERROR && cur >= 0) {
        if (err_t == 0) toast("Can't play this file - skipping");
        err_t += dt;
        if (err_t > 0.8f) { err_t = 0; if (nctx > 1) next_track(true); else { player_stop(); cur = -1; } }
    }

    if (sleep_mode != SLEEP_OFF && sleep_mode != SLEEP_EOT && player_is_playing()) {
        sleep_left -= dt;
        if (sleep_left <= 0) { player_pause(); sleep_mode = SLEEP_OFF; toast("Sleep timer ended"); }
    }

    player_tick(dt);

    /* animations */
    if (tab_anim < 1) tab_anim = clampf(tab_anim + dt / 0.22f, 0, 1);
    if (push_anim < 1) push_anim = clampf(push_anim + dt / 0.28f, 0, 1);
    sheet_t += ((sheet_open ? 1.0f : 0.0f) - sheet_t) * clampf(dt * 12, 0, 1);
    if (fabsf(sheet_t - (sheet_open ? 1 : 0)) < 0.002f) sheet_t = sheet_open ? 1 : 0;
    if (toast_t > 0) toast_t -= dt;
    if (vol_flash > 0) vol_flash -= dt;
    for (int i = 0; i < 16; i++) {
        int v = 0;
        if (i == SI_SHUFFLE) v = S.shuffle; else if (i == SI_RESUME) v = S.resume; else if (i == SI_LID) v = S.lid;
        anim_toggle[i] += ((float)v - anim_toggle[i]) * clampf(dt * 14, 0, 1);
    }

    Page *p = page();
    bool dragging = T.on && T.zone == Z_CONTENT && T.moved;
    if (p->snap && !dragging) {
        p->scroll += (p->target - p->scroll) * clampf(dt * 16, 0, 1);
        if (fabsf(p->target - p->scroll) < 0.3f) { p->scroll = p->target; p->snap = false; }
    } else {
        scroll_physics(&p->scroll, &p->vel, max_scroll(p), dt, dragging);
    }
    scroll_physics(&sheet_scroll, &sheet_vel, sheet_max_scroll(), dt, T.on && T.zone == Z_SHEET_LIST && T.moved);
    float want_sel = hero_h(p) + p->sel * row_h(p);
    sel_y += (want_sel - sel_y) * clampf(dt * 22, 0, 1);

    /* accent colour follows the cover */
    int al = cur_album();
    if (al >= 0) { art_thumb(al); if (art_has_color(al)) accent_target = art_color(al); }
    else accent_target = HEXC(0x404040);
    accent = gfx_mix(accent, accent_target, clampf(dt * 5, 0, 1));

    save_t += dt;
    if (save_t > 15 && cur >= 0) { save_t = 0; settings_save(); }
    return true;
}

/* ================================================================== drawing */

static void draw_cover(int album, float x, float y, float s, float r, col_t bg) {
    GfxImage *img = album >= 0 ? art_thumb(album) : NULL;
    if (img) gfx_image(img, x, y, s, s, C_WHITE);
    else {
        gfx_rect(x, y, s, s, C_CARD);
        icon_note(x + s / 2, y + s / 2, s * 0.42f, HEXC(0x7F7F7F));
    }
    if (r > 0) gfx_corners(x, y, s, s, r, bg);
}

static void draw_toggle(float x, float y, float t) {
    col_t track = gfx_mix(HEXC(0x535353), C_GREEN, t);
    gfx_rrect(x, y, 30, 17, 8.5f, track);
    gfx_circle(x + 8.5f + t * 13, y + 8.5f, 6.5f, t > 0.5f ? C_BLACK : C_WHITE);
    gfx_circle(x + 8.5f + t * 13, y + 8.5f, 6.0f, gfx_mix(HEXC(0xE0E0E0), C_WHITE, t));
}

static void draw_slider(float x0, float x1, float y, float v, bool active) {
    gfx_rrect(x0, y - 2, x1 - x0, 4, 2, gfx_alpha(C_WHITE, 0.25f));
    gfx_rrect(x0, y - 2, (x1 - x0) * v, 4, 2, active ? C_GREEN : C_WHITE);
    gfx_circle(x0 + (x1 - x0) * v, y, active ? 7 : 6, C_WHITE);
}

static void draw_track_row(const Page *p, int i, int t, float y, col_t bg) {
    const Track *tr = &lib_tracks[t];
    bool is_cur = t == cur;
    float x = 50;
    if (p->type == PG_ALBUM) {
        x = 42;
        if (is_cur) icon_eqbars(21, y + 20, 13, C_GREEN, tsec, player_is_playing());
        else {
            char num[12];
            snprintf(num, sizeof num, "%d", tr->trackno ? tr->trackno : i + 1);
            gfx_text(21, y + 12, 0.46f, C_SUB, ALIGN_CENTER, num);
        }
    } else {
        draw_cover(tr->album_id, 10, y + 4, 32, 3, bg);
    }
    float tw = 284 - x - (is_cur && p->type != PG_ALBUM ? 22 : 0);
    gfx_text_fit(x, y + 6, 0.5f, is_cur ? C_GREEN : C_WHITE, ALIGN_LEFT, tw, tr->title, false);
    const char *sub = p->type == PG_ARTIST ? tr->album : tr->artist;
    gfx_text_fit(x, y + 22, 0.4f, C_SUB, ALIGN_LEFT, tw, sub, false);
    if (is_cur && p->type != PG_ALBUM) icon_eqbars(274, y + 20, 12, C_GREEN, tsec, player_is_playing());
    if (tr->liked) icon_heart(298, y + 20, 12, C_GREEN);
    else if (cursor_mode && p->sel == i) icon_heart_outline(298, y + 20, 12, C_MUTED, bg);
}

static void draw_settings_row(int i, float y, col_t bg) {
    float ty = y + 10;
    char b[64];
    switch (i) {
        case SI_H_PLAY: case SI_H_LIB: case SI_H_ABOUT: {
            const char *h = i == SI_H_PLAY ? "Playback" : i == SI_H_LIB ? "Library" : "About";
            gfx_text_bold(14, y + 15, 0.55f, C_WHITE, ALIGN_LEFT, h);
            return;
        }
        default: break;
    }
    const char *label = "";
    switch (i) {
        case SI_SHUFFLE: label = "Shuffle"; break;
        case SI_REPEAT:  label = "Repeat"; break;
        case SI_EQ:      label = "Equalizer"; break;
        case SI_VOLUME:  label = "Volume"; break;
        case SI_SLEEP:   label = "Sleep timer"; break;
        case SI_RESUME:  label = "Resume where you left off"; break;
        case SI_LID:     label = "Keep playing with lid closed"; break;
        case SI_SORT:    label = "Sort songs by"; break;
        case SI_FOLDER:  label = "Music folder"; break;
        case SI_RESCAN:  label = "Rescan music"; break;
        case SI_ABOUT:   label = "3DStify by Meer"; break;
        case SI_CREDIT:  label = "Plays .mp3 files from your SD card or a folder you pick"; break;
    }
    if (i == SI_CREDIT) { gfx_text_fit(14, ty + 1, 0.4f, C_MUTED, ALIGN_LEFT, 292, label, false); return; }
    gfx_text(14, ty, 0.48f, C_WHITE, ALIGN_LEFT, label);
    (void)bg;
    switch (i) {
        case SI_SHUFFLE: case SI_RESUME: case SI_LID:
            draw_toggle(276, y + 8, anim_toggle[i]);
            break;
        case SI_REPEAT: case SI_EQ: case SI_SLEEP: case SI_SORT: {
            const char *v = i == SI_REPEAT ? rep_names[S.repeat] : i == SI_EQ ? eq_names[S.eq] :
                            i == SI_SORT ? sort_names[S.sort] : sleep_names[sleep_mode];
            if (i == SI_SLEEP && sleep_mode != SLEEP_OFF && sleep_mode != SLEEP_EOT) {
                char t[16];
                fmt_time(sleep_left, t, sizeof t);
                snprintf(b, sizeof b, "%s left", t);
                v = b;
            }
            bool on = (i == SI_REPEAT && S.repeat) || (i == SI_EQ && S.eq) || (i == SI_SLEEP && sleep_mode);
            float w = gfx_text_width(v, 0.44f);
            gfx_text(296, ty + 1, 0.44f, on ? C_GREEN : C_SUB, ALIGN_RIGHT, v);
            icon_back(296 - w - 9, y + 17, 7, C_MUTED);
            gfx_line(301, y + 14, 304, y + 17, 1.4f, C_MUTED);
            gfx_line(304, y + 17, 301, y + 20, 1.4f, C_MUTED);
            break;
        }
        case SI_FOLDER: {
            const char *v = folder_label(S.folder);
            gfx_text_fit(296, ty + 1, 0.44f, S.folder[0] ? C_GREEN : C_SUB, ALIGN_RIGHT, 150, v, false);
            gfx_line(301, y + 14, 304, y + 17, 1.4f, C_MUTED);
            gfx_line(304, y + 17, 301, y + 20, 1.4f, C_MUTED);
            break;
        }
        case SI_VOLUME: {
            draw_slider(150, 300, y + 17, S.volume, T.on && T.zone == Z_SLIDER_VOL);
            break;
        }
        case SI_RESCAN:
            if (lib_scanning()) snprintf(b, sizeof b, "Scanning... %d", lib_scan_count());
            else snprintf(b, sizeof b, "%d songs", lib_ntracks);
            gfx_text(306, ty + 1, 0.44f, lib_scanning() ? C_GREEN : C_SUB, ALIGN_RIGHT, b);
            break;
        case SI_ABOUT:
            gfx_text(306, ty + 1, 0.44f, C_SUB, ALIGN_RIGHT, "v" APP_VERSION);
            break;
    }
}

static void draw_hero(const Page *p, float y0, col_t bg) {
    col_t a = bg;
    const char *title = "", *kind = "";
    char sub[300], sub2[64];
    sub[0] = sub2[0] = 0;
    int album = -1;
    int n = 0;
    page_tracks(p, &n);
    if (p->type == PG_ALBUM) {
        const Album *al = &lib_albums[p->arg];
        album = p->arg;
        a = art_color(album);
        title = al->name; kind = "Album";
        snprintf(sub, sizeof sub, "%s", al->artist);
    } else if (p->type == PG_ARTIST) {
        const Artist *ar = &lib_artists[p->arg];
        album = ar->album;
        a = art_color(album);
        title = ar->name; kind = "Artist";
        snprintf(sub, sizeof sub, "Artist");
    } else {
        a = HEXC(0x5038A0);
        title = "Liked Songs"; kind = "Playlist";
        snprintf(sub, sizeof sub, "Your favourites");
    }
    snprintf(sub2, sizeof sub2, "%s  \xE2\x80\xA2  %d song%s", kind, n, n == 1 ? "" : "s");
    gfx_rect_v(0, y0, BW, HERO + 20, gfx_mix(a, C_BLACK, 0.15f), bg);
    float ay = y0 + 30;
    const float as = 60;
    if (p->type == PG_LIKED) {
        gfx_rect_h(14, ay, as, as, HEXC(0x450AF5), HEXC(0x8E8EE5));
        gfx_rect_v(14, ay, as, as, gfx_alpha(C_WHITE, 0), gfx_alpha(HEXC(0xC4EFD9), 0.55f));
        icon_heart(14 + as / 2, ay + as / 2 + 2, 24, C_WHITE);
    } else {
        draw_cover(album, 14, ay, as, p->type == PG_ARTIST ? as / 2 : 0, gfx_mix(gfx_mix(a, C_BLACK, 0.15f), bg, 60.0f / (HERO + 20)));
    }
    gfx_text_fit(86, ay + 2, 0.64f, C_WHITE, ALIGN_LEFT, 220, title, true);
    gfx_text_fit(86, ay + 25, 0.44f, C_SUB, ALIGN_LEFT, 142, sub, false);
    gfx_text_fit(86, ay + 41, 0.4f, C_SUB, ALIGN_LEFT, 142, sub2, false);
    float by = y0 + HB + 52;
    icon_shuffle(251, by, 16, S.shuffle ? C_GREEN : C_SUB);
    if (S.shuffle) gfx_circle(251, by + 12, 1.6f, C_GREEN);
    int kindc = p->type == PG_LIKED ? CTX_LIKED : p->type == PG_ALBUM ? CTX_ALBUM : CTX_ARTIST;
    bool playing_this = ctx_is(kindc, p->type == PG_LIKED ? "" : title) && player_is_playing();
    gfx_circle(290, by, 18, C_GREEN);
    if (playing_this) icon_pause(290, by, 14, C_BLACK);
    else icon_play(291.5f, by, 14, C_BLACK);
}

static void draw_page(const Page *p, float ox) {
    gfx_offset(ox, 0);
    float top = content_top(p), bottom = content_bottom();
    col_t bg = C_BG;
    gfx_rect(0, 0, BW, bottom, bg);

    float y0 = top - p->scroll;
    if (is_detail(p)) draw_hero(p, y0, bg);

    int n = page_rows(p);
    float rh = row_h(p);
    float base = y0 + hero_h(p);
    int first = (int)((top - base) / rh);
    if (first < 0) first = 0;
    int ntr = 0;
    const int *tracks = page_tracks(p, &ntr);
    bool pressed_ok = T.on && !T.moved && T.zone == Z_CONTENT && T.t > 0.05f && p == page();
    int pressed_row = pressed_ok ? (int)((T.y0 - base) / rh) : -1;

    if (cursor_mode && p == page() && n > 0 && row_selectable(p, p->sel)) {
        float hy = y0 + sel_y;
        gfx_rrect(4, hy + 1, BW - 8, rh - 2, 5, C_ROW_HI);
    }
    for (int i = first; i < n; i++) {
        float y = base + i * rh;
        if (y > bottom) break;
        bool hi = (cursor_mode && p == page() && i == p->sel && row_selectable(p, i)) || i == pressed_row;
        if (i == pressed_row && !(cursor_mode && i == p->sel)) gfx_rrect(4, y + 1, BW - 8, rh - 2, 5, C_ROW_HI);
        col_t rbg = hi ? C_ROW_HI : bg;
        if (tracks) draw_track_row(p, i, tracks[i], y, rbg);
        else if (p->type == PG_ALBUMS) {
            const Album *al = &lib_albums[i];
            draw_cover(i, 10, y + 4, 40, 3, rbg);
            gfx_text_fit(60, y + 9, 0.5f, C_WHITE, ALIGN_LEFT, 244, al->name, false);
            char sub[300];
            snprintf(sub, sizeof sub, "Album \xE2\x80\xA2 %s", al->artist);
            gfx_text_fit(60, y + 26, 0.4f, C_SUB, ALIGN_LEFT, 244, sub, false);
        } else if (p->type == PG_ARTISTS) {
            const Artist *ar = &lib_artists[i];
            draw_cover(ar->album, 10, y + 4, 40, 20, rbg);
            gfx_text_fit(60, y + 9, 0.5f, C_WHITE, ALIGN_LEFT, 244, ar->name, false);
            char sub[64];
            snprintf(sub, sizeof sub, "Artist \xE2\x80\xA2 %d song%s", ar->ntracks, ar->ntracks == 1 ? "" : "s");
            gfx_text(60, y + 26, 0.4f, C_SUB, ALIGN_LEFT, sub);
        } else if (p->type == PG_SETTINGS) {
            draw_settings_row(i, y, rbg);
        } else if (p->type == PG_FOLDERS) {
            if (i == 0) {
                gfx_circle(26, y + 20, 12, C_GREEN);
                gfx_line(20, y + 20, 24.5f, y + 24.5f, 2.4f, C_BLACK);
                gfx_line(24.5f, y + 24.5f, 32, y + 15, 2.4f, C_BLACK);
                gfx_text_bold(48, y + 5, 0.5f, C_GREEN, ALIGN_LEFT, fb_is_root() ? "Use the whole SD card" : "Use this folder");
                gfx_text_fit(48, y + 22, 0.4f, C_SUB, ALIGN_LEFT, 256, fb_path, false);
            } else if (!fb_is_root() && i == 1) {
                icon_back(26, y + 20, 14, C_SUB);
                gfx_text(48, y + 12, 0.5f, C_WHITE, ALIGN_LEFT, "Up one level");
            } else {
                int k = i - fb_first_dir();
                gfx_rrect(15, y + 12, 9, 5, 1.5f, HEXC(0xC9A34A));
                gfx_rrect(15, y + 15, 23, 15, 2, HEXC(0xE5BE5C));
                gfx_text_fit(48, y + 12, 0.5f, C_WHITE, ALIGN_LEFT, 240, fb_dirs[k], false);
                gfx_line(301, y + 17, 304, y + 20, 1.4f, C_MUTED);
                gfx_line(304, y + 20, 301, y + 23, 1.4f, C_MUTED);
            }
        }
    }

    /* empty states */
    if (n == 0 && !is_detail(p)) {
        if (p->type == PG_SONGS || p->type == PG_ALBUMS || p->type == PG_ARTISTS) {
            gfx_text(BW / 2, 80, 0.55f, C_WHITE, ALIGN_CENTER, lib_scanning() ? "Looking for music..." : "No music yet");
            gfx_text(BW / 2, 102, 0.42f, C_SUB, ALIGN_CENTER, "Copy .mp3 files anywhere on your SD card");
            gfx_text(BW / 2, 117, 0.42f, C_SUB, ALIGN_CENTER, "(for example sdmc:/Music) and rescan.");
        }
    } else if (n == 0 && p->type == PG_LIKED) {
        gfx_text(BW / 2, HERO + 14 - p->scroll, 0.5f, C_WHITE, ALIGN_CENTER, "Songs you like will appear here");
        gfx_text(BW / 2, HERO + 34 - p->scroll, 0.42f, C_SUB, ALIGN_CENTER, "Tap the heart or press X to save a song.");
    }

    /* cover rows that run under the mini player (no scissor needed) */
    gfx_rect(0, bottom, BW, 240 - bottom, bg);

    /* header bar */
    if (is_detail(p)) {
        float a = clampf((p->scroll - 50) / 40, 0, 1);
        col_t hc = p->type == PG_LIKED ? HEXC(0x3A2A80) : art_color(p->type == PG_ALBUM ? p->arg : lib_artists[p->arg].album);
        hc = gfx_mix(hc, C_BLACK, 0.35f);
        gfx_rect(0, 0, BW, HB, gfx_alpha(hc, a));
        const char *t = p->type == PG_LIKED ? "Liked Songs" : p->type == PG_ALBUM ? lib_albums[p->arg].name : lib_artists[p->arg].name;
        gfx_text_fit(dep[tab] > 1 ? 40 : 14, 9, 0.52f, gfx_alpha(C_WHITE, a), ALIGN_LEFT, 230, t, true);
        if (dep[tab] > 1) {
            gfx_circle(18, 17, 12, gfx_alpha(C_BLACK, 0.45f * (1 - a)));
            icon_back(17, 17, 13, C_WHITE);
        }
    } else {
        float sh = clampf(p->scroll / 20, 0, 1);
        gfx_rect(0, 0, BW, HB, bg);
        if (sh > 0) gfx_rect_v(0, HB, BW, 6, gfx_alpha(C_BLACK, 0.5f * sh), gfx_alpha(C_BLACK, 0));
        if (p->type == PG_FOLDERS) {
            icon_back(17, 17, 13, C_WHITE);
            gfx_text_bold(36, 7, 0.7f, C_WHITE, ALIGN_LEFT, "Music folder");
        } else
        gfx_text_bold(14, 7, 0.7f, C_WHITE, ALIGN_LEFT, tab_names[tab]);
        char cnt[32] = "";
        if (p->type == PG_SONGS) snprintf(cnt, sizeof cnt, "%d", lib_ntracks);
        else if (p->type == PG_ALBUMS) snprintf(cnt, sizeof cnt, "%d", lib_nalbums);
        else if (p->type == PG_ARTISTS) snprintf(cnt, sizeof cnt, "%d", lib_nartists);
        float tw = p->type == PG_FOLDERS ? 400 : gfx_text_width(tab_names[tab], 0.7f);
        if (cnt[0]) gfx_text(22 + tw, 13, 0.42f, C_MUTED, ALIGN_LEFT, cnt);
        if (p->type == PG_SONGS && lib_ntracks > 0) {
            gfx_circle(296, 17, 13, C_GREEN);
            icon_shuffle(296, 17, 13, C_BLACK);
        }
    }
    gfx_offset(0, 0);
}

static void draw_mini(void) {
    if (cur < 0) return;
    gfx_rect(0, MINI_Y - 2, BW, TAB_Y - MINI_Y + 2, C_BG);
    const Track *t = &lib_tracks[cur];
    col_t bg = gfx_mix(accent, C_BLACK, 0.45f);
    float y = MINI_Y;
    gfx_rrect(6, y, BW - 12, MINI_H, 6, bg);
    draw_cover(t->album_id, 11, y + 4, 28, 3, bg);
    gfx_text_fit(47, y + 4, 0.46f, C_WHITE, ALIGN_LEFT, 182, t->title, false);
    gfx_text_fit(47, y + 18, 0.4f, gfx_mix(C_SUB, C_WHITE, 0.15f), ALIGN_LEFT, 182, t->artist, false);
    if (t->liked) icon_heart(256, y + 17, 15, C_GREEN);
    else icon_heart_outline(256, y + 17, 15, C_WHITE, bg);
    if (player_is_playing()) icon_pause(292, y + 17, 14, C_WHITE);
    else icon_play(293, y + 17, 14, C_WHITE);
    double len = player_len(), pos = scrub ? scrub_pos : player_pos();
    float f = len > 0 ? clampf((float)(pos / len), 0, 1) : 0;
    gfx_rect(12, y + MINI_H - 3, BW - 24, 2, gfx_alpha(C_WHITE, 0.25f));
    gfx_rect(12, y + MINI_H - 3, (BW - 24) * f, 2, C_WHITE);
}

static void draw_tabs(void) {
    gfx_rect_v(0, TAB_Y - 14, BW, 14, gfx_alpha(C_BLACK, 0), gfx_alpha(C_BLACK, 0.85f));
    gfx_rect(0, TAB_Y, BW, 240 - TAB_Y, gfx_alpha(C_BLACK, 0.92f));
    for (int i = 0; i < NTABS; i++) {
        float cx = (i + 0.5f) * (BW / NTABS);
        col_t c = i == tab ? C_WHITE : HEXC(0xA7A7A7);
        float iy = TAB_Y + 12;
        switch (i) {
            case TAB_SONGS:    icon_note(cx, iy, 15, c); break;
            case TAB_ALBUMS:   icon_album(cx, iy, 15, c, C_BLACK); break;
            case TAB_ARTISTS:  icon_artist(cx, iy, 15, c); break;
            case TAB_LIKED:    if (i == tab) icon_heart(cx, iy, 15, c); else icon_heart_outline(cx, iy, 15, c, C_BLACK); break;
            case TAB_SETTINGS: icon_gear(cx, iy, 15, c, C_BLACK); break;
        }
        gfx_text(cx, TAB_Y + 23, 0.34f, c, ALIGN_CENTER, tab_names[i]);
    }
}

static void draw_sheet(void) {
    float oy = (1 - ease_out(sheet_t)) * 240;
    gfx_offset(0, oy);
    col_t top = gfx_mix(accent, C_BLACK, 0.3f);
    gfx_rect_v(0, 0, BW, 150, top, C_BG);
    gfx_rect(0, 150, BW, 90, C_BG);
    /* grab handle + title */
    gfx_rrect(BW / 2 - 18, 5, 36, 4, 2, gfx_alpha(C_WHITE, 0.4f));
    icon_down(18, 18, 13, C_WHITE);
    gfx_text(BW / 2, 13, 0.4f, gfx_alpha(C_WHITE, 0.8f), ALIGN_CENTER, "PLAYING FROM");
    gfx_text_fit(BW / 2, 25, 0.0f + 0.42f, C_WHITE, ALIGN_CENTER, 200, ctx_name, true);
    icon_dots(300, 18, 13, C_WHITE);

    double len = player_len(), pos = scrub ? scrub_pos : player_pos();
    float f = len > 0 ? clampf((float)(pos / len), 0, 1) : 0;
    bool seek_active = T.on && T.zone == Z_SHEET_SEEK;
    gfx_rrect(16, SH_SEEK_Y - 2, 288, 4, 2, gfx_alpha(C_WHITE, 0.25f));
    gfx_rrect(16, SH_SEEK_Y - 2, 288 * f, 4, 2, seek_active || scrub ? C_GREEN : C_WHITE);
    gfx_circle(16 + 288 * f, SH_SEEK_Y, seek_active ? 7 : 5.5f, C_WHITE);
    char a[16], b[16];
    fmt_time(pos, a, sizeof a);
    fmt_time(len, b, sizeof b);
    gfx_text(16, SH_SEEK_Y + 7, 0.36f, C_SUB, ALIGN_LEFT, a);
    gfx_text(304, SH_SEEK_Y + 7, 0.36f, C_SUB, ALIGN_RIGHT, len > 0 ? b : "--:--");

    float cy = SH_CTRL_Y;
    icon_shuffle(40, cy, 18, S.shuffle ? C_GREEN : C_WHITE);
    if (S.shuffle) gfx_circle(40, cy + 13, 1.7f, C_GREEN);
    icon_prev(100, cy, 20, C_WHITE);
    gfx_circle(160, cy, 22, C_WHITE);
    if (player_is_playing()) icon_pause(160, cy, 17, C_BLACK);
    else icon_play(162, cy, 18, C_BLACK);
    icon_next(220, cy, 20, C_WHITE);
    icon_repeat(280, cy, 19, S.repeat ? C_GREEN : C_WHITE);
    if (S.repeat) gfx_circle(280, cy + 13, 1.7f, C_GREEN);
    if (S.repeat == REP_ONE) { gfx_circle(289, cy - 8, 6, C_GREEN); gfx_text(289, cy - 14, 0.32f, C_BLACK, ALIGN_CENTER, "1"); }

    icon_speaker(26, SH_VOL_Y, 13, C_SUB);
    draw_slider(44, 292, SH_VOL_Y, S.volume, T.on && T.zone == Z_SHEET_VOL);

    /* queue */
    gfx_clip(0, (int)(SH_LIST_Y + oy), BW, (int)(240 - SH_LIST_Y));
    float ly = SH_LIST_Y - sheet_scroll;
    gfx_text_bold(14, ly + 4, 0.48f, C_WHITE, ALIGN_LEFT, "Queue");
    if (sleep_mode != SLEEP_OFF) {
        char sb[32];
        if (sleep_mode == SLEEP_EOT) snprintf(sb, sizeof sb, "Sleep: end of song");
        else { char t[16]; fmt_time(sleep_left, t, sizeof t); snprintf(sb, sizeof sb, "Sleep in %s", t); }
        icon_moon(220, ly + 12, 10, C_GREEN, C_BG);
        gfx_text(306, ly + 6, 0.38f, C_GREEN, ALIGN_RIGHT, sb);
    }
    for (int k = 0; qpos >= 0 && qpos + k < nctx; k++) {
        float y = ly + 22 + k * SH_ROW;
        if (y > 240) break;
        if (y + SH_ROW < SH_LIST_Y) continue;
        int t = ctx[order[qpos + k]];
        const Track *tr = &lib_tracks[t];
        draw_cover(tr->album_id, 14, y + 3, 30, 3, C_BG);
        bool now = k == 0;
        gfx_text_fit(52, y + 4, 0.46f, now ? C_GREEN : C_WHITE, ALIGN_LEFT, 222, tr->title, false);
        gfx_text_fit(52, y + 19, 0.38f, C_SUB, ALIGN_LEFT, 222, tr->artist, false);
        if (now) icon_eqbars(280, y + 18, 11, C_GREEN, tsec, player_is_playing());
        if (tr->liked) icon_heart(300, y + 18, 11, C_GREEN);
        if (k == 0 && nctx - qpos > 1)
            gfx_text(14, y + SH_ROW + 2, 0.38f, C_MUTED, ALIGN_LEFT, "");
    }
    gfx_unclip();
    gfx_offset(0, 0);
}

static void draw_toast(float y) {
    if (toast_t <= 0) return;
    float a = clampf(toast_t / 0.25f, 0, 1) * clampf((2.0f - toast_t) / 0.15f, 0, 1);
    float w = gfx_text_width(toast_msg, 0.44f) + 28;
    float x = (BW - w) / 2;
    float yy = y + (1 - a) * 8;
    gfx_rrect(x, yy, w, 24, 6, gfx_alpha(HEXC(0xF2F2F2), a));
    gfx_text(BW / 2, yy + 6, 0.44f, gfx_alpha(C_BLACK, a), ALIGN_CENTER, toast_msg);
}

static void draw_scanning_bottom(void) {
    gfx_rect(0, 0, BW, 240, C_BG);
    float cx = BW / 2, cy = 92;
    gfx_ring(cx, cy, 22, 4, HEXC(0x2A2A2A));
    float a0 = tsec * 5.0f;
    gfx_arc(cx, cy, 22, a0, a0 + 1.8f + 0.8f * sinf(tsec * 2), 4, C_GREEN);
    gfx_text_bold(cx, 132, 0.58f, C_WHITE, ALIGN_CENTER, "Scanning your SD card");
    char b[64];
    snprintf(b, sizeof b, "%d song%s found", lib_scan_count(), lib_scan_count() == 1 ? "" : "s");
    gfx_text(cx, 156, 0.45f, C_GREEN, ALIGN_CENTER, b);
    char dir[256];
    lib_scan_folder(dir, sizeof dir);
    gfx_text_fit(cx, 178, 0.38f, C_MUTED, ALIGN_CENTER, 290, dir, false);
}

void app_draw_bottom(void) {
    if (screens_off) { gfx_rect(0, 0, BW, 240, C_BLACK); return; }
    if (lib_ntracks == 0 && lib_scanning()) { draw_scanning_bottom(); return; }
    gfx_rect(0, 0, BW, 240, C_BG);

    Page *p = page();
    if (push_anim < 1) {
        float e = ease_out(push_anim);
        if (push_dir > 0) {
            draw_page(&push_prev, -e * 70);
            gfx_rect(0, 0, BW, content_bottom(), gfx_alpha(C_BLACK, 0.5f * e));
            draw_page(p, (1 - e) * BW);
        } else {
            draw_page(p, -(1 - e) * 70);
            gfx_rect(0, 0, BW, content_bottom(), gfx_alpha(C_BLACK, 0.5f * (1 - e)));
            draw_page(&push_prev, e * BW);
        }
    } else {
        draw_page(p, 0);
    }
    if (tab_anim < 1) gfx_rect(0, 0, BW, content_bottom(), gfx_alpha(C_BG, 1 - ease_out(tab_anim)));

    draw_mini();
    draw_tabs();
    draw_toast(cur >= 0 ? MINI_Y - 30 : TAB_Y - 30);
    if (sheet_t > 0.001f) {
        gfx_rect(0, 0, BW, 240, gfx_alpha(C_BLACK, 0.6f * sheet_t));
        draw_sheet();
        if (sheet_open) draw_toast(206);
    }
}

/* ------------------------------------------------------------ top screen */

static void draw_top_empty(void) {
    gfx_rect_v(0, 0, TW, 240, HEXC(0x1F3A2A), C_BG);
    icon_logo(TW / 2, 86, 34, C_GREEN, C_BLACK);
    gfx_text_bold(TW / 2, 132, 0.9f, C_WHITE, ALIGN_CENTER, "3DStify");
    gfx_text(TW / 2, 162, 0.45f, C_SUB, ALIGN_CENTER, "by Meer");
    const char *msg = lib_scanning() ? "Finding your music..." :
                      lib_ntracks ? "Pick a song on the touch screen" : "No .mp3 files found on your SD card";
    gfx_text(TW / 2, 196, 0.48f, C_WHITE, ALIGN_CENTER, msg);
}

void app_draw_top(void) {
    if (screens_off) { gfx_rect(0, 0, TW, 240, C_BLACK); return; }
    if (cur < 0) { draw_top_empty(); return; }
    const Track *t = &lib_tracks[cur];

    col_t top = gfx_mix(accent, C_BLACK, 0.2f);
    gfx_rect_v(0, 0, TW, 240, top, gfx_mix(accent, C_BG, 0.82f));
    gfx_rect_v(0, 150, TW, 90, gfx_alpha(C_BG, 0), gfx_alpha(C_BG, 0.75f));

    /* header */
    icon_down(20, 16, 12, C_WHITE);
    gfx_text(TW / 2, 5, 0.36f, gfx_alpha(C_WHITE, 0.75f), ALIGN_CENTER, "PLAYING FROM");
    gfx_text_fit(TW / 2, 16, 0.44f, C_WHITE, ALIGN_CENTER, 250, ctx_name, true);
    icon_dots(380, 16, 12, C_WHITE);

    /* cover with a soft shadow */
    float ax = 20, ay = 38, as = 150;
    for (int i = 4; i >= 1; i--) gfx_rrect(ax - i + 2, ay - i + 4, as + 2 * i, as + 2 * i, (float)i + 2, gfx_alpha(C_BLACK, 0.07f));
    GfxImage *ci, *pi;
    float fade;
    art_big(t->album_id, &ci, &pi, &fade);
    if (!ci && !pi) {
        gfx_rect(ax, ay, as, as, C_CARD);
        icon_note(ax + as / 2, ay + as / 2, 56, HEXC(0x7F7F7F));
    } else {
        if (fade < 1) {
            if (pi) gfx_image(pi, ax, ay, as, as, C_WHITE);
            else { gfx_rect(ax, ay, as, as, C_CARD); icon_note(ax + as / 2, ay + as / 2, 56, HEXC(0x7F7F7F)); }
        }
        if (ci) gfx_image(ci, ax, ay, as, as, gfx_alpha(C_WHITE, ease_out(fade)));
        else if (fade >= 0) {
            gfx_rect(ax, ay, as, as, gfx_alpha(C_CARD, ease_out(fade)));
            icon_note(ax + as / 2, ay + as / 2, 56, gfx_alpha(HEXC(0x7F7F7F), ease_out(fade)));
        }
    }

    /* title (scrolls when too long), artist, album */
    float cx0 = 190, cw = 190;
    float title_w = gfx_text_width(t->title, 0.78f);
    float tmax = cw - 30;
    if (title_w <= tmax) {
        gfx_text_bold(cx0, 50, 0.78f, C_WHITE, ALIGN_LEFT, t->title);
    } else {
        float gap = 40, period = (title_w + gap) / 28.0f + 2.5f;
        float ph = fmodf(tsec, period);
        float off = ph < 2.5f ? 0 : (ph - 2.5f) * 28.0f;
        gfx_clip((int)cx0, 44, (int)tmax, 32);
        gfx_text_bold(cx0 - off, 50, 0.78f, C_WHITE, ALIGN_LEFT, t->title);
        gfx_text_bold(cx0 - off + title_w + gap, 50, 0.78f, C_WHITE, ALIGN_LEFT, t->title);
        gfx_unclip();
        gfx_rect_h(cx0 + tmax - 10, 44, 10, 32, gfx_alpha(top, 0), gfx_alpha(top, 0.0f));
    }
    if (t->liked) icon_heart(cx0 + cw - 10, 62, 17, C_GREEN);
    else icon_heart_outline(cx0 + cw - 10, 62, 17, gfx_alpha(C_WHITE, 0.85f), gfx_mix(top, gfx_mix(accent, C_BG, 0.82f), 62.0f / 240));
    gfx_text_fit(cx0, 80, 0.52f, C_SUB, ALIGN_LEFT, cw, t->artist, false);
    gfx_text_fit(cx0, 98, 0.42f, gfx_alpha(C_SUB, 0.7f), ALIGN_LEFT, cw, t->album, false);

    /* progress */
    double len = player_len(), pos = scrub ? scrub_pos : player_pos();
    float f = len > 0 ? clampf((float)(pos / len), 0, 1) : 0;
    float py = 136;
    gfx_rrect(cx0, py - 2, cw, 4, 2, gfx_alpha(C_WHITE, 0.25f));
    gfx_rrect(cx0, py - 2, cw * f, 4, 2, scrub ? C_GREEN : C_WHITE);
    gfx_circle(cx0 + cw * f, py, scrub ? 7 : 5.5f, C_WHITE);
    char a[16], b[16];
    fmt_time(pos, a, sizeof a);
    if (len > 0) { b[0] = '-'; fmt_time(len - pos, b + 1, sizeof b - 1); }
    else snprintf(b, sizeof b, "--:--");
    gfx_text(cx0, py + 8, 0.38f, C_SUB, ALIGN_LEFT, a);
    gfx_text(cx0 + cw, py + 8, 0.38f, C_SUB, ALIGN_RIGHT, b);

    /* controls */
    float cy = 186, mid = cx0 + cw / 2;
    icon_shuffle(mid - 84, cy, 17, S.shuffle ? C_GREEN : C_WHITE);
    if (S.shuffle) gfx_circle(mid - 84, cy + 13, 1.7f, C_GREEN);
    icon_prev(mid - 44, cy, 20, C_WHITE);
    gfx_circle(mid, cy, 24, C_WHITE);
    if (player_is_playing()) icon_pause(mid, cy, 18, C_BLACK);
    else icon_play(mid + 2, cy, 19, C_BLACK);
    icon_next(mid + 44, cy, 20, C_WHITE);
    icon_repeat(mid + 84, cy, 18, S.repeat ? C_GREEN : C_WHITE);
    if (S.repeat) gfx_circle(mid + 84, cy + 13, 1.7f, C_GREEN);
    if (S.repeat == REP_ONE) { gfx_circle(mid + 93, cy - 8, 6, C_GREEN); gfx_text(mid + 93, cy - 14, 0.32f, C_BLACK, ALIGN_CENTER, "1"); }

    /* bottom line: volume / EQ / sleep / next up */
    float by = 222;
    icon_speaker(26, by, 12, C_SUB);
    gfx_rrect(38, by - 1.5f, 100, 3, 1.5f, gfx_alpha(C_WHITE, 0.25f));
    gfx_rrect(38, by - 1.5f, 100 * S.volume, 3, 1.5f, vol_flash > 0 ? C_GREEN : C_SUB);
    float bx = 150;
    if (S.eq != EQ_FLAT) {
        float w = gfx_text_width(eq_names[S.eq], 0.34f) + 12;
        gfx_rrect(bx, by - 8, w, 16, 8, gfx_alpha(C_GREEN, 0.18f));
        gfx_text(bx + 6, by - 5, 0.34f, C_GREEN, ALIGN_LEFT, eq_names[S.eq]);
        bx += w + 6;
    }
    if (sleep_mode != SLEEP_OFF) {
        char sb[24];
        if (sleep_mode == SLEEP_EOT) snprintf(sb, sizeof sb, "End of song");
        else fmt_time(sleep_left, sb, sizeof sb);
        float w = gfx_text_width(sb, 0.34f) + 22;
        gfx_rrect(bx, by - 8, w, 16, 8, gfx_alpha(C_GREEN, 0.18f));
        icon_moon(bx + 9, by, 9, C_GREEN, gfx_mix(gfx_mix(accent, C_BG, 0.82f), C_GREEN, 0.18f));
        gfx_text(bx + 17, by - 5, 0.34f, C_GREEN, ALIGN_LEFT, sb);
        bx += w + 6;
    }
    if (nctx > 0 && qpos >= 0) {
        int nx = -1;
        if (S.repeat == REP_ONE) nx = cur;
        else if (qpos + 1 < nctx) nx = ctx[order[qpos + 1]];
        else if (S.repeat == REP_ALL) nx = ctx[order[0]];
        if (nx >= 0) {
            char nb[300];
            snprintf(nb, sizeof nb, "Next: %s", lib_tracks[nx].title);
            float x0 = bx > cx0 ? bx : cx0;
            gfx_text_fit(380, by - 6, 0.38f, C_SUB, ALIGN_RIGHT, 380 - x0, nb, false);
        }
    }

    /* volume pop-up */
    if (vol_flash > 0) {
        float va = clampf(vol_flash / 0.3f, 0, 1);
        gfx_rrect(TW / 2 - 70, 8, 140, 26, 13, gfx_alpha(HEXC(0x181818), 0.92f * va));
        icon_speaker(TW / 2 - 52, 21, 12, gfx_alpha(C_WHITE, va));
        gfx_rrect(TW / 2 - 38, 19.5f, 96, 3, 1.5f, gfx_alpha(C_WHITE, 0.25f * va));
        gfx_rrect(TW / 2 - 38, 19.5f, 96 * S.volume, 3, 1.5f, gfx_alpha(C_GREEN, va));
    }
}

/* ===================================================================== init */

void app_init(void) {
    lib_init();
    settings_load();
    for (int t = 0; t < NTABS; t++) reset_tab(t);
    art_init();
    lib_set_root(S.folder);
    bool cached = lib_load_cache();
    if (cached) lib_sort(S.sort);
    else { lib_sort(S.sort); lib_scan_start(); }
    art_reset();
    player_set_volume(S.volume);
    player_set_eq(S.eq);
    if (cached) restore_context();
}

void app_exit(void) {
    settings_save();
    lib_save_liked();
    art_exit();
    lib_free();
    free(ctx);
    free(order);
}

bool app_keep_awake(void) { return S.lid && player_is_playing(); }
bool app_screens_off(void) { return screens_off; }

void app_on_suspend(void) {
    player_suspend();
    settings_save();
}

void app_on_resume(void) {
    player_resume_after_suspend();
}

/* used by the PC preview to set up a scene */
void app_debug_scene(int tab_i, int track, bool sheet, double pos);
void app_debug_scene(int tab_i, int track, bool sheet, double pos) {
    (void)pos;
    tab = tab_i;
    if (track >= 0) {
        start_list(lib_sorted, lib_ntracks, 0, CTX_ALL, "", "Your Library", false);
        for (int i = 0; i < lib_ntracks; i++) if (lib_sorted[i] == track) { qpos = i; cur = track; }
    }
    sheet_open = sheet;
    sheet_t = sheet ? 1 : 0;
    accent = accent_target = art_color(cur_album());
}

void app_debug_push(int type, int arg);
void app_debug_push(int type, int arg) {
    if (type == 2) { fb_load("sdmc:/"); push_page(PG_FOLDERS, 0); push_anim = 1; return; }
    push_page(type == 0 ? PG_ALBUM : PG_ARTIST, arg);
    push_anim = 1;
}

void app_debug_cursor(int sel);
void app_debug_cursor(int sel) { cursor_mode = true; page()->sel = sel; sel_y = hero_h(page()) + sel * row_h(page()); }
