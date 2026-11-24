/*
 * 3DStify - album artwork
 *
 * Covers come from the MP3's embedded picture (APIC) or a cover.jpg/folder.jpg
 * next to the songs. A worker thread decodes them with stb_image and scales
 * them to 64x64 thumbnails (cached on the SD card) or a 256x256 cover for the
 * now playing screen. Textures are created on the main thread.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "art.h"
#include "library.h"
#include "sys.h"

#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_FAILURE_STRINGS
#include "stb_image.h"

#define THUMB      64
#define BIG        256
#define MAX_LOADED 360
#define QCAP       24
#define ART_UNSYNC 0x80000000u
#define MAX_SRC_BYTES (6 * 1024 * 1024)

enum { ST_NONE, ST_QUEUED, ST_READY, ST_MISSING };
enum { K_THUMB, K_BIG };

typedef struct {
    GfxImage *img;
    col_t     color;
    unsigned  last_used;
    unsigned char state;
    unsigned char has_color;
} Slot;

typedef struct {
    int      album, kind;
    unsigned gen;
    char     path[512];       /* mp3 with the embedded cover, or an image file */
    unsigned off, len;        /* len 0 = whole file */
    unsigned key;
} Job;

typedef struct {
    int      album, kind;
    unsigned gen;
    unsigned char *rgba;      /* NULL = no art */
    col_t    color;
    bool     has_color;
} Done;

static Slot *slots;
static int nslots;
static unsigned frame_no = 1;
static int nloaded = 0;

static SysLock lk;
static SysThread worker;
static volatile bool quit = false;
static Job q[QCAP];
static int qn = 0;
static Job big_job;
static bool big_job_pending = false;
static Done done[QCAP + 2];
static int dn = 0;
static unsigned gen = 1;

static int big_album = -1;          /* album the current big cover belongs to */
static int big_want = -1;
static GfxImage *big_cur, *big_prev;
static float big_fade = 1;
static GfxImage *trash[8];
static int ntrash = 0;

/* ------------------------------------------------------------ decoding */

static unsigned char *read_src(const Job *j, int *out_len) {
    FILE *f = fopen(j->path, "rb");
    if (!f) return NULL;
    unsigned len = j->len & ~ART_UNSYNC;
    if (len == 0) {
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        len = sz > 0 ? (unsigned)sz : 0;
    } else {
        fseek(f, (long)j->off, SEEK_SET);
    }
    if (len == 0 || len > MAX_SRC_BYTES) { fclose(f); return NULL; }
    unsigned char *buf = (unsigned char *)malloc(len);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, len, f);
    fclose(f);
    if (j->len & ART_UNSYNC) {               /* undo ID3 unsynchronisation: FF 00 -> FF */
        size_t o = 0;
        for (size_t i = 0; i < got; i++) {
            buf[o++] = buf[i];
            if (buf[i] == 0xFF && i + 1 < got && buf[i + 1] == 0x00) i++;
        }
        got = o;
    }
    *out_len = (int)got;
    return buf;
}

/* box-filter a centre square crop of src (sw x sh, 3 channels) into dst (n x n RGBA) */
static void scale_square(const unsigned char *src, int sw, int sh, unsigned char *dst, int n) {
    int side = sw < sh ? sw : sh;
    int ox = (sw - side) / 2, oy = (sh - side) / 2;
    for (int y = 0; y < n; y++) {
        int y0 = oy + y * side / n, y1 = oy + (y + 1) * side / n;
        if (y1 <= y0) y1 = y0 + 1;
        for (int x = 0; x < n; x++) {
            int x0 = ox + x * side / n, x1 = ox + (x + 1) * side / n;
            if (x1 <= x0) x1 = x0 + 1;
            unsigned r = 0, g = 0, b = 0, c = 0;
            int stepy = (y1 - y0) > 4 ? (y1 - y0) / 4 : 1, stepx = (x1 - x0) > 4 ? (x1 - x0) / 4 : 1;
            for (int yy = y0; yy < y1; yy += stepy)
                for (int xx = x0; xx < x1; xx += stepx) {
                    const unsigned char *p = src + ((size_t)yy * sw + xx) * 3;
                    r += p[0]; g += p[1]; b += p[2]; c++;
                }
            unsigned char *d = dst + ((size_t)y * n + x) * 4;
            d[0] = (unsigned char)(r / c); d[1] = (unsigned char)(g / c); d[2] = (unsigned char)(b / c); d[3] = 255;
        }
    }
}

/* a Spotify-ish accent: the average colour, pushed towards saturated and mid-dark */
static col_t pick_color(const unsigned char *rgba, int n) {
    double r = 0, g = 0, b = 0, w = 0;
    for (int i = 0; i < n * n; i++) {
        const unsigned char *p = rgba + i * 4;
        int mx = p[0] > p[1] ? (p[0] > p[2] ? p[0] : p[2]) : (p[1] > p[2] ? p[1] : p[2]);
        int mn = p[0] < p[1] ? (p[0] < p[2] ? p[0] : p[2]) : (p[1] < p[2] ? p[1] : p[2]);
        double sat = mx ? (double)(mx - mn) / mx : 0;
        double wt = 0.15 + sat * sat * 2.0;             /* colourful pixels count more */
        if (mx < 30) wt *= 0.3;
        r += p[0] * wt; g += p[1] * wt; b += p[2] * wt; w += wt;
    }
    if (w <= 0) return HEXC(0x535353);
    r /= w; g /= w; b /= w;
    double mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    double mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    double l = (mx + mn) / 2;
    double s = 1.25;                                     /* saturate */
    r = l + (r - l) * s; g = l + (g - l) * s; b = l + (b - l) * s;
    mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    double target = mx < 70 ? 70 : (mx > 165 ? 165 : mx);
    double k = mx > 1 ? target / mx : 1;
    r *= k; g *= k; b *= k;
    r = r < 0 ? 0 : (r > 255 ? 255 : r);
    g = g < 0 ? 0 : (g > 255 ? 255 : g);
    b = b < 0 ? 0 : (b > 255 ? 255 : b);
    return RGB((int)r, (int)g, (int)b);
}

static unsigned char *decode(const Job *j, int n) {
    int len = 0;
    unsigned char *src = read_src(j, &len);
    if (!src) return NULL;
    int w = 0, h = 0, c = 0;
    unsigned char *out = NULL;
    if (stbi_info_from_memory(src, len, &w, &h, &c) && w > 0 && h > 0 && (long)w * h <= 2400L * 2400L) {
        unsigned char *px = stbi_load_from_memory(src, len, &w, &h, &c, 3);
        if (px) {
            out = (unsigned char *)malloc((size_t)n * n * 4);
            if (out) scale_square(px, w, h, out, n);
            stbi_image_free(px);
        }
    }
    free(src);
    return out;
}

static void cache_path(unsigned key, char *out, size_t n) {
    snprintf(out, n, APP_DIR "/cache/%08x.t64", key);
}

static unsigned char *thumb_from_cache(unsigned key) {
    char p[96];
    cache_path(key, p, sizeof p);
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    unsigned char *rgb = (unsigned char *)malloc(THUMB * THUMB * 3 + 1);
    unsigned char *out = (unsigned char *)malloc(THUMB * THUMB * 4);
    size_t got = rgb ? fread(rgb, 1, THUMB * THUMB * 3 + 1, f) : 0;
    fclose(f);
    if (!rgb || !out || (got != THUMB * THUMB * 3 && got != 1)) { free(rgb); free(out); return NULL; }
    if (got == 1) {                                     /* remembered "no art" */
        free(rgb); free(out);
        return (unsigned char *)1;
    }
    for (int i = 0; i < THUMB * THUMB; i++) {
        out[i * 4] = rgb[i * 3]; out[i * 4 + 1] = rgb[i * 3 + 1]; out[i * 4 + 2] = rgb[i * 3 + 2]; out[i * 4 + 3] = 255;
    }
    free(rgb);
    return out;
}

static void thumb_to_cache(unsigned key, const unsigned char *rgba) {
    char p[96];
    cache_path(key, p, sizeof p);
    FILE *f = fopen(p, "wb");
    if (!f) return;
    if (!rgba) { fputc(0, f); fclose(f); return; }
    unsigned char *rgb = (unsigned char *)malloc(THUMB * THUMB * 3);
    if (rgb) {
        for (int i = 0; i < THUMB * THUMB; i++) {
            rgb[i * 3] = rgba[i * 4]; rgb[i * 3 + 1] = rgba[i * 4 + 1]; rgb[i * 3 + 2] = rgba[i * 4 + 2];
        }
        fwrite(rgb, 1, THUMB * THUMB * 3, f);
        free(rgb);
    }
    fclose(f);
}

static void worker_main(void *arg) {
    (void)arg;
    while (!quit) {
        Job j;
        bool have = false;
        sys_lock(&lk);
        if (dn < QCAP) {
            if (big_job_pending) { j = big_job; big_job_pending = false; have = true; }
            else if (qn > 0) { j = q[--qn]; have = true; }      /* newest request first */
        }
        sys_unlock(&lk);
        if (!have) { sys_sleep_ms(12); continue; }

        Done d;
        memset(&d, 0, sizeof d);
        d.album = j.album;
        d.kind = j.kind;
        d.gen = j.gen;
        if (j.kind == K_THUMB) {
            unsigned char *px = thumb_from_cache(j.key);
            if (px == (unsigned char *)1) px = NULL;
            else if (!px) {
                px = j.path[0] ? decode(&j, THUMB) : NULL;
                thumb_to_cache(j.key, px);
            }
            d.rgba = px;
        } else {
            d.rgba = j.path[0] ? decode(&j, BIG) : NULL;
        }
        if (d.rgba) {
            d.color = pick_color(d.rgba, j.kind == K_THUMB ? THUMB : BIG);
            d.has_color = true;
        }
        sys_lock(&lk);
        if (j.gen == gen && dn < QCAP + 2) done[dn++] = d;
        else free(d.rgba);
        sys_unlock(&lk);
    }
}

/* ---------------------------------------------------------------- API */

void art_init(void) {
    sys_lock_init(&lk);
    worker = sys_thread_start(worker_main, NULL, 64 * 1024);
}

static void free_slots(void) {
    for (int i = 0; i < nslots; i++) gfx_image_free(slots[i].img);
    free(slots);
    slots = NULL;
    nslots = 0;
    nloaded = 0;
}

void art_exit(void) {
    quit = true;
    sys_thread_join(worker);
    free_slots();
    for (int i = 0; i < dn; i++) free(done[i].rgba);
    dn = 0;
    gfx_image_free(big_cur);
    gfx_image_free(big_prev);
    for (int i = 0; i < ntrash; i++) gfx_image_free(trash[i]);
    big_cur = big_prev = NULL;
    ntrash = 0;
}

void art_reset(void) {
    sys_lock(&lk);
    gen++;
    qn = 0;
    big_job_pending = false;
    for (int i = 0; i < dn; i++) free(done[i].rgba);
    dn = 0;
    sys_unlock(&lk);
    free_slots();
    nslots = lib_nalbums;
    slots = (Slot *)calloc((size_t)(nslots + 1), sizeof(Slot));
    if (!slots) nslots = 0;
    big_album = big_want = -1;
    if (big_cur && ntrash < 8) trash[ntrash++] = big_cur;
    if (big_prev && ntrash < 8) trash[ntrash++] = big_prev;
    big_cur = big_prev = NULL;
    big_fade = 1;
}

static bool fill_job(Job *j, int album, int kind) {
    if (album < 0 || album >= lib_nalbums) return false;
    const Album *al = &lib_albums[album];
    memset(j, 0, sizeof *j);
    j->album = album;
    j->kind = kind;
    j->gen = gen;
    j->key = al->key;
    if (al->art_track >= 0) {
        const Track *t = &lib_tracks[al->art_track];
        snprintf(j->path, sizeof j->path, "%s", t->path);
        j->off = t->art_off;
        j->len = t->art_len;
        if ((j->len & ~ART_UNSYNC) == 0) j->path[0] = 0;
    } else if (al->art_file) {
        snprintf(j->path, sizeof j->path, "%s", al->art_file);
    }
    return true;
}

static void evict_one(void) {
    int best = -1;
    unsigned oldest = 0xFFFFFFFFu;
    for (int i = 0; i < nslots; i++)
        if (slots[i].img && slots[i].last_used < oldest && slots[i].last_used + 2 < frame_no) {
            oldest = slots[i].last_used;
            best = i;
        }
    if (best < 0) return;
    gfx_image_free(slots[best].img);    /* safe: called after the previous frame finished */
    slots[best].img = NULL;
    slots[best].state = ST_NONE;
    nloaded--;
}

void art_update(float dt) {
    frame_no++;
    for (int i = 0; i < ntrash; i++) gfx_image_free(trash[i]);
    ntrash = 0;

    Done local[4];
    int nl = 0;
    sys_lock(&lk);
    while (dn > 0 && nl < 4) local[nl++] = done[--dn];
    sys_unlock(&lk);
    for (int i = 0; i < nl; i++) {
        Done *d = &local[i];
        if (d->kind == K_THUMB) {
            if (d->album < nslots) {
                Slot *s = &slots[d->album];
                if (d->rgba) {
                    if (nloaded >= MAX_LOADED) evict_one();
                    s->img = gfx_image_new(d->rgba, THUMB, THUMB);
                    s->state = s->img ? ST_READY : ST_MISSING;
                    if (s->img) nloaded++;
                    s->color = d->color;
                    s->has_color = d->has_color;
                } else {
                    s->state = ST_MISSING;
                }
            }
        } else if (d->album == big_want) {
            if (big_prev && ntrash < 8) trash[ntrash++] = big_prev;
            big_prev = big_cur;
            big_cur = d->rgba ? gfx_image_new(d->rgba, BIG, BIG) : NULL;
            big_album = d->album;
            big_fade = 0;
            if (d->album < nslots && d->has_color && !slots[d->album].has_color) {
                slots[d->album].color = d->color;
                slots[d->album].has_color = true;
            }
        }
        free(d->rgba);
    }
    if (big_fade < 1) {
        big_fade += dt / 0.35f;
        if (big_fade >= 1) {
            big_fade = 1;
            if (big_prev && ntrash < 8) { trash[ntrash++] = big_prev; big_prev = NULL; }
        }
    }
}

GfxImage *art_thumb(int album) {
    if (album < 0 || album >= nslots) return NULL;
    Slot *s = &slots[album];
    s->last_used = frame_no;
    if (s->state == ST_NONE) {
        Job j;
        if (!fill_job(&j, album, K_THUMB)) return NULL;
        if (!j.path[0] && !lib_albums[album].art_file) { s->state = ST_MISSING; return NULL; }
        sys_lock(&lk);
        if (qn == QCAP) {                    /* drop the oldest request; it will be asked again */
            if (q[0].album < nslots) slots[q[0].album].state = ST_NONE;
            memmove(q, q + 1, sizeof(Job) * (QCAP - 1));
            qn--;
        }
        q[qn++] = j;
        sys_unlock(&lk);
        s->state = ST_QUEUED;
    }
    return s->img;
}

col_t art_color(int album) {
    if (album >= 0 && album < nslots && slots[album].has_color) return slots[album].color;
    return HEXC(0x404040);
}

bool art_has_color(int album) {
    return album >= 0 && album < nslots && slots[album].has_color;
}

void art_big(int album, GfxImage **cur, GfxImage **prev, float *fade) {
    if (album != big_want) {
        big_want = album;
        Job j;
        if (fill_job(&j, album, K_BIG)) {
            sys_lock(&lk);
            big_job = j;
            big_job_pending = true;
            sys_unlock(&lk);
        } else {
            if (big_prev && ntrash < 8) trash[ntrash++] = big_prev;
            big_prev = big_cur;
            big_cur = NULL;
            big_fade = 0;
            big_album = -1;
        }
    }
    *cur = big_cur;
    *prev = big_prev;
    *fade = big_fade;
}
