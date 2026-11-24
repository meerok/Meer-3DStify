/*
 * 3DStify - music library
 *
 * Walks the SD card for .mp3 files, reads their ID3v2/ID3v1 tags (title,
 * artist, album, track number and where the embedded cover art sits), groups
 * them into albums and artists and caches the result in library.txt so the
 * next launch is instant.
 */
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include "library.h"
#include "sys.h"

#define CACHE_FILE APP_DIR "/library.txt"
#define LIKED_FILE APP_DIR "/liked.txt"
#define CACHE_MAGIC "3DSTIFY-LIB 1"
#define ART_UNSYNC 0x80000000u

typedef struct {
    Track  *t;
    int     nt, cap;
    Album  *al;
    int     nal;
    Artist *ar;
    int     nar;
    int    *sorted;
} Lib;

Track  *lib_tracks;
int     lib_ntracks;
Album  *lib_albums;
int     lib_nalbums;
Artist *lib_artists;
int     lib_nartists;
int    *lib_sorted;
int    *lib_liked;
int     lib_nliked;

static Lib cur;
static int g_sort = SORT_TITLE;

/* scan thread state */
static Lib scan_lib;
static volatile bool scan_running = false, scan_done = false, scan_abort = false;
static volatile int  scan_count = 0;
static char scan_dir[256];
static SysLock scan_lock;
static SysThread scan_thread;

/* ---------------------------------------------------------------- utils */

static char *xstrdup(const char *s) {
    if (!s) s = "";
    size_t n = strlen(s);
    char *d = (char *)malloc(n + 1);
    if (d) memcpy(d, s, n + 1);
    return d;
}

static void clean(char *s) {           /* no tabs/newlines, trimmed */
    for (char *p = s; *p; p++)
        if (*p == '\t' || *p == '\n' || *p == '\r') *p = ' ';
    size_t n = strlen(s);
    while (n && s[n - 1] == ' ') s[--n] = 0;
    size_t k = 0;
    while (s[k] == ' ') k++;
    if (k) memmove(s, s + k, strlen(s + k) + 1);
}

static int put_utf8(char *o, int cap, int n, unsigned cp) {
    if (cp < 0x80) { if (n + 1 < cap) o[n++] = (char)cp; }
    else if (cp < 0x800) { if (n + 2 < cap) { o[n++] = (char)(0xC0 | (cp >> 6)); o[n++] = (char)(0x80 | (cp & 0x3F)); } }
    else if (cp < 0x10000) { if (n + 3 < cap) { o[n++] = (char)(0xE0 | (cp >> 12)); o[n++] = (char)(0x80 | ((cp >> 6) & 0x3F)); o[n++] = (char)(0x80 | (cp & 0x3F)); } }
    else if (n + 4 < cap) {
        o[n++] = (char)(0xF0 | (cp >> 18)); o[n++] = (char)(0x80 | ((cp >> 12) & 0x3F));
        o[n++] = (char)(0x80 | ((cp >> 6) & 0x3F)); o[n++] = (char)(0x80 | (cp & 0x3F));
    }
    return n;
}

/* ID3 text (with its leading encoding byte) -> UTF-8 */
static void id3_text(const unsigned char *d, int len, char *out, int cap) {
    out[0] = 0;
    if (len < 1) return;
    int enc = d[0], n = 0;
    d++; len--;
    if (enc == 0 || enc == 3) {
        for (int i = 0; i < len && d[i]; i++) {
            if (enc == 3) { if (n + 1 < cap) out[n++] = (char)d[i]; }
            else n = put_utf8(out, cap, n, d[i]);
        }
    } else {
        bool be = enc == 2;
        int i = 0;
        if (len >= 2 && d[0] == 0xFF && d[1] == 0xFE) { be = false; i = 2; }
        else if (len >= 2 && d[0] == 0xFE && d[1] == 0xFF) { be = true; i = 2; }
        for (; i + 1 < len; i += 2) {
            unsigned u = be ? (unsigned)(d[i] << 8 | d[i + 1]) : (unsigned)(d[i + 1] << 8 | d[i]);
            if (u == 0) break;
            if (u >= 0xD800 && u < 0xDC00 && i + 3 < len) {
                unsigned u2 = be ? (unsigned)(d[i + 2] << 8 | d[i + 3]) : (unsigned)(d[i + 3] << 8 | d[i + 2]);
                u = 0x10000 + ((u - 0xD800) << 10) + (u2 - 0xDC00);
                i += 2;
            }
            n = put_utf8(out, cap, n, u);
        }
    }
    out[n < cap ? n : cap - 1] = 0;
    clean(out);
}

static unsigned syncsafe(const unsigned char *b) {
    return (unsigned)((b[0] & 0x7F) << 21 | (b[1] & 0x7F) << 14 | (b[2] & 0x7F) << 7 | (b[3] & 0x7F));
}

/* skip the APIC header (encoding, mime / format, picture type, description) */
static int apic_header_len(const unsigned char *d, int len, bool v22) {
    if (len < 4) return -1;
    int enc = d[0], i = 1;
    if (v22) i += 3;
    else { while (i < len && d[i]) i++; i++; }
    i++;                                    /* picture type */
    if (enc == 1 || enc == 2) {
        while (i + 1 < len && (d[i] || d[i + 1])) i += 2;
        i += 2;
    } else {
        while (i < len && d[i]) i++;
        i++;
    }
    return i < len ? i : -1;
}

typedef struct {
    char title[256], artist[256], album[256];
    int trackno;
    unsigned art_off, art_len;
} Tags;

static void read_tags(const char *path, Tags *tg) {
    memset(tg, 0, sizeof *tg);
    FILE *f = fopen(path, "rb");
    if (!f) return;
    unsigned char h[10];
    if (fread(h, 1, 10, f) == 10 && !memcmp(h, "ID3", 3) && h[3] >= 2 && h[3] <= 4) {
        int ver = h[3];
        bool unsync = (h[5] & 0x80) != 0;
        unsigned tag_size = syncsafe(h + 6);
        unsigned pos = 10, end = 10 + tag_size;
        if (ver == 3 && (h[5] & 0x40)) {               /* extended header */
            unsigned char e[4];
            if (fread(e, 1, 4, f) == 4) pos += 4 + (unsigned)(e[0] << 24 | e[1] << 16 | e[2] << 8 | e[3]);
        } else if (ver == 4 && (h[5] & 0x40)) {
            unsigned char e[4];
            if (fread(e, 1, 4, f) == 4) pos += syncsafe(e);
        }
        bool v22 = ver == 2;
        int fh_len = v22 ? 6 : 10;
        unsigned char body[1024];
        while (pos + (unsigned)fh_len < end) {
            if (fseek(f, (long)pos, SEEK_SET)) break;
            unsigned char fh[10];
            if (fread(fh, 1, (size_t)fh_len, f) != (size_t)fh_len) break;
            if (fh[0] == 0) break;                      /* padding */
            unsigned fsz;
            char id[5] = { 0 };
            if (v22) { memcpy(id, fh, 3); fsz = (unsigned)(fh[3] << 16 | fh[4] << 8 | fh[5]); }
            else {
                memcpy(id, fh, 4);
                fsz = ver == 4 ? syncsafe(fh + 4) : (unsigned)(fh[4] << 24 | fh[5] << 16 | fh[6] << 8 | fh[7]);
            }
            unsigned body_pos = pos + (unsigned)fh_len;
            if (fsz == 0 || body_pos + fsz > end) break;
            bool compressed = !v22 && ((ver == 3 && (fh[9] & 0xC0)) || (ver == 4 && (fh[9] & 0x0C)));
            char *dst = NULL;
            if (!strcmp(id, "TIT2") || !strcmp(id, "TT2")) dst = tg->title;
            else if (!strcmp(id, "TPE1") || !strcmp(id, "TP1")) dst = tg->artist;
            else if (!strcmp(id, "TALB") || !strcmp(id, "TAL")) dst = tg->album;
            if ((dst || !strcmp(id, "TRCK") || !strcmp(id, "TRK")) && !compressed) {
                int n = fsz < sizeof body ? (int)fsz : (int)sizeof body;
                if (fread(body, 1, (size_t)n, f) == (size_t)n) {
                    if (dst) { if (!dst[0]) id3_text(body, n, dst, 256); }
                    else { char tmp[32]; id3_text(body, n, tmp, sizeof tmp); tg->trackno = atoi(tmp); }
                }
            } else if ((!strcmp(id, "APIC") || !strcmp(id, "PIC")) && !compressed && tg->art_len == 0) {
                int n = fsz < sizeof body ? (int)fsz : (int)sizeof body;
                if (fread(body, 1, (size_t)n, f) == (size_t)n) {
                    int hl = apic_header_len(body, n, v22);
                    if (hl > 0 && fsz > (unsigned)hl + 16) {
                        tg->art_off = body_pos + (unsigned)hl;
                        tg->art_len = (fsz - (unsigned)hl) | (unsync ? ART_UNSYNC : 0);
                    }
                }
            }
            pos = body_pos + fsz;
        }
    }
    if (!tg->title[0] || !tg->artist[0]) {          /* ID3v1 fallback */
        unsigned char v1[128];
        if (!fseek(f, -128, SEEK_END) && fread(v1, 1, 128, f) == 128 && !memcmp(v1, "TAG", 3)) {
            char tmp[31];
            struct { int off; char *dst; } fl[3] = { { 3, tg->title }, { 33, tg->artist }, { 63, tg->album } };
            for (int k = 0; k < 3; k++) {
                if (fl[k].dst[0]) continue;
                int n = 0;
                for (int i = 0; i < 30 && v1[fl[k].off + i]; i++) n = put_utf8(tmp, 31, n, v1[fl[k].off + i]);
                tmp[n] = 0;
                snprintf(fl[k].dst, 256, "%s", tmp);
                clean(fl[k].dst);
            }
            if (!tg->trackno && v1[125] == 0 && v1[126]) tg->trackno = v1[126];
        }
    }
    fclose(f);
}

/* ---------------------------------------------------------------- build */

static void lib_free_one(Lib *L) {
    for (int i = 0; i < L->nt; i++) {
        free(L->t[i].path); free(L->t[i].title); free(L->t[i].artist); free(L->t[i].album);
    }
    for (int i = 0; i < L->nal; i++) { free(L->al[i].name); free(L->al[i].artist); free(L->al[i].tracks); free(L->al[i].art_file); }
    for (int i = 0; i < L->nar; i++) { free(L->ar[i].name); free(L->ar[i].tracks); }
    free(L->t); free(L->al); free(L->ar); free(L->sorted);
    memset(L, 0, sizeof *L);
}

static bool add_track(Lib *L, const char *path, const char *title, const char *artist, const char *album,
                      int trackno, unsigned art_off, unsigned art_len) {
    if (L->nt >= MAX_TRACKS) return false;
    if (L->nt == L->cap) {
        int nc = L->cap ? L->cap * 2 : 256;
        Track *nt = (Track *)realloc(L->t, (size_t)nc * sizeof(Track));
        if (!nt) return false;
        L->t = nt;
        L->cap = nc;
    }
    Track *t = &L->t[L->nt];
    memset(t, 0, sizeof *t);
    t->path = xstrdup(path);
    t->title = xstrdup(title);
    t->artist = xstrdup(artist);
    t->album = xstrdup(album);
    t->trackno = (uint16_t)(trackno > 0 && trackno < 65535 ? trackno : 0);
    t->art_off = art_off;
    t->art_len = art_len;
    if (!t->path || !t->title || !t->artist || !t->album) return false;
    L->nt++;
    return true;
}

static uint32_t fnv(const char *s, uint32_t h) {
    for (; *s; s++) { h ^= (uint8_t)tolower((unsigned char)*s); h *= 16777619u; }
    return h;
}

static const Lib *sort_lib;
static int cmp_str(const char *a, const char *b) {
    /* letters before symbols/numbers feel more natural in a music list */
    bool la = isalpha((unsigned char)a[0]) != 0, lb = isalpha((unsigned char)b[0]) != 0;
    if (la != lb) return la ? -1 : 1;
    return strcasecmp(a, b);
}
static int cmp_title(const void *x, const void *y) {
    const Track *a = &sort_lib->t[*(const int *)x], *b = &sort_lib->t[*(const int *)y];
    int c = cmp_str(a->title, b->title);
    return c ? c : cmp_str(a->artist, b->artist);
}
static int cmp_artist(const void *x, const void *y) {
    const Track *a = &sort_lib->t[*(const int *)x], *b = &sort_lib->t[*(const int *)y];
    int c = cmp_str(a->artist, b->artist);
    if (c) return c;
    c = cmp_str(a->album, b->album);
    if (c) return c;
    if (a->trackno != b->trackno) return (int)a->trackno - (int)b->trackno;
    return cmp_str(a->title, b->title);
}
static int cmp_album(const void *x, const void *y) {
    const Track *a = &sort_lib->t[*(const int *)x], *b = &sort_lib->t[*(const int *)y];
    int c = cmp_str(a->album, b->album);
    if (c) return c;
    if (a->trackno != b->trackno) return (int)a->trackno - (int)b->trackno;
    return cmp_str(a->title, b->title);
}
static int cmp_album_tracks(const void *x, const void *y) {
    const Track *a = &sort_lib->t[*(const int *)x], *b = &sort_lib->t[*(const int *)y];
    int ta = a->trackno ? a->trackno : 9999, tb = b->trackno ? b->trackno : 9999;
    if (ta != tb) return ta - tb;
    return strcasecmp(a->path, b->path);
}
static int cmp_albums(const void *x, const void *y) {
    return cmp_str(((const Album *)x)->name, ((const Album *)y)->name);
}
static int cmp_artists(const void *x, const void *y) {
    return cmp_str(((const Artist *)x)->name, ((const Artist *)y)->name);
}

static const char *cover_names[] = { "cover.jpg", "folder.jpg", "front.jpg", "album.jpg", "cover.png",
                                     "folder.png", "front.png", "cover.jpeg", "folder.jpeg", NULL };

static char *find_dir_cover(const char *track_path) {
    char dir[512];
    snprintf(dir, sizeof dir, "%s", track_path);
    char *sl = strrchr(dir, '/');
    if (!sl) return NULL;
    sl[1] = 0;
    char p[600];
    struct stat st;
    for (int i = 0; cover_names[i]; i++) {
        snprintf(p, sizeof p, "%s%s", dir, cover_names[i]);
        if (stat(p, &st) == 0 && st.st_size > 64) return xstrdup(p);
    }
    /* any jpg/png in the folder */
    DIR *d = opendir(dir);
    if (!d) return NULL;
    struct dirent *e;
    char *found = NULL;
    while ((e = readdir(d))) {
        size_t n = strlen(e->d_name);
        if (n > 4 && e->d_name[0] != '.' &&
            (!strcasecmp(e->d_name + n - 4, ".jpg") || !strcasecmp(e->d_name + n - 4, ".png") ||
             (n > 5 && !strcasecmp(e->d_name + n - 5, ".jpeg")))) {
            snprintf(p, sizeof p, "%s%s", dir, e->d_name);
            found = xstrdup(p);
            break;
        }
    }
    closedir(d);
    return found;
}

/* group tracks into albums and artists; also fills in missing tags */
static void finalize(Lib *L, bool look_for_covers) {
    int n = L->nt;
    for (int i = 0; i < n; i++) {
        Track *t = &L->t[i];
        if (!t->title[0]) {
            const char *b = strrchr(t->path, '/');
            b = b ? b + 1 : t->path;
            char tmp[256];
            snprintf(tmp, sizeof tmp, "%s", b);
            char *dot = strrchr(tmp, '.');
            if (dot) *dot = 0;
            for (char *p = tmp; *p; p++) if (*p == '_') *p = ' ';
            free(t->title);
            t->title = xstrdup(tmp);
        }
        if (!t->artist[0]) { free(t->artist); t->artist = xstrdup("Unknown Artist"); }
        if (!t->album[0]) {
            char tmp[256];
            snprintf(tmp, sizeof tmp, "%s", t->path);
            char *sl = strrchr(tmp, '/');
            if (sl) *sl = 0;
            sl = strrchr(tmp, '/');
            const char *dn = sl ? sl + 1 : tmp;
            if (!*dn || strchr(dn, ':')) dn = "Unknown Album";
            free(t->album);
            t->album = xstrdup(dn);
        }
    }

    /* hash tables: album name / artist name -> index */
    int hcap = 1;
    while (hcap < n * 2 + 16) hcap <<= 1;
    int *ha = (int *)malloc((size_t)hcap * sizeof(int));
    int *hr = (int *)malloc((size_t)hcap * sizeof(int));
    L->al = (Album *)calloc((size_t)n + 1, sizeof(Album));
    L->ar = (Artist *)calloc((size_t)n + 1, sizeof(Artist));
    if (!ha || !hr || !L->al || !L->ar) { free(ha); free(hr); return; }
    memset(ha, -1, (size_t)hcap * sizeof(int));
    memset(hr, -1, (size_t)hcap * sizeof(int));
    int *alc = (int *)calloc((size_t)n + 1, sizeof(int)), *arc = (int *)calloc((size_t)n + 1, sizeof(int));

    for (int i = 0; i < n; i++) {
        Track *t = &L->t[i];
        uint32_t h = fnv(t->album, 2166136261u) & (uint32_t)(hcap - 1);
        while (ha[h] >= 0 && strcasecmp(L->al[ha[h]].name, t->album)) h = (h + 1) & (uint32_t)(hcap - 1);
        if (ha[h] < 0) {
            ha[h] = L->nal;
            L->al[L->nal].name = xstrdup(t->album);
            L->al[L->nal].art_track = -1;
            L->nal++;
        }
        t->album_id = (uint16_t)ha[h];
        alc[ha[h]]++;

        h = fnv(t->artist, 2166136261u) & (uint32_t)(hcap - 1);
        while (hr[h] >= 0 && strcasecmp(L->ar[hr[h]].name, t->artist)) h = (h + 1) & (uint32_t)(hcap - 1);
        if (hr[h] < 0) {
            hr[h] = L->nar;
            L->ar[L->nar].name = xstrdup(t->artist);
            L->ar[L->nar].album = t->album_id;
            L->nar++;
        }
        t->artist_id = (uint16_t)hr[h];
        arc[hr[h]]++;
    }
    free(ha); free(hr);

    for (int a = 0; a < L->nal; a++) L->al[a].tracks = (int *)malloc((size_t)(alc[a] ? alc[a] : 1) * sizeof(int));
    for (int a = 0; a < L->nar; a++) L->ar[a].tracks = (int *)malloc((size_t)(arc[a] ? arc[a] : 1) * sizeof(int));
    free(alc); free(arc);
    for (int i = 0; i < n; i++) {
        Album *al = &L->al[L->t[i].album_id];
        al->tracks[al->ntracks++] = i;
        Artist *ar = &L->ar[L->t[i].artist_id];
        ar->tracks[ar->ntracks++] = i;
    }

    sort_lib = L;
    for (int a = 0; a < L->nal; a++) {
        Album *al = &L->al[a];
        qsort(al->tracks, (size_t)al->ntracks, sizeof(int), cmp_album_tracks);
        const char *first = L->t[al->tracks[0]].artist;
        bool various = false;
        for (int k = 0; k < al->ntracks; k++) {
            Track *t = &L->t[al->tracks[k]];
            if (strcasecmp(t->artist, first)) various = true;
            if (al->art_track < 0 && (t->art_len & ~ART_UNSYNC)) al->art_track = al->tracks[k];
        }
        al->artist = xstrdup(various ? "Various Artists" : first);
        if (look_for_covers) al->art_file = find_dir_cover(L->t[al->tracks[0]].path);
        uint32_t key = fnv(L->t[al->tracks[0]].path, 2166136261u);
        if (al->art_track >= 0) key ^= L->t[al->art_track].art_off * 2654435761u;
        al->key = key ? key : 1;
    }
    for (int a = 0; a < L->nar; a++) {
        Artist *ar = &L->ar[a];
        qsort(ar->tracks, (size_t)ar->ntracks, sizeof(int), cmp_artist);
        /* prefer an album with artwork */
        for (int k = 0; k < ar->ntracks; k++) {
            int al = L->t[ar->tracks[k]].album_id;
            if (L->al[al].art_track >= 0 || L->al[al].art_file) { ar->album = al; break; }
        }
    }

    /* sorting albums/artists alphabetically reorders their indices; remap */
    int *amap = (int *)malloc((size_t)(L->nal + 1) * sizeof(int));
    int *rmap = (int *)malloc((size_t)(L->nar + 1) * sizeof(int));
    for (int a = 0; a < L->nal; a++) L->al[a].ntracks |= a << 16;   /* stash the old index */
    for (int a = 0; a < L->nar; a++) L->ar[a].ntracks |= a << 16;
    qsort(L->al, (size_t)L->nal, sizeof(Album), cmp_albums);
    qsort(L->ar, (size_t)L->nar, sizeof(Artist), cmp_artists);
    for (int a = 0; a < L->nal; a++) { amap[L->al[a].ntracks >> 16] = a; L->al[a].ntracks &= 0xFFFF; }
    for (int a = 0; a < L->nar; a++) { rmap[L->ar[a].ntracks >> 16] = a; L->ar[a].ntracks &= 0xFFFF; }
    for (int i = 0; i < n; i++) {
        L->t[i].album_id = (uint16_t)amap[L->t[i].album_id];
        L->t[i].artist_id = (uint16_t)rmap[L->t[i].artist_id];
    }
    for (int a = 0; a < L->nar; a++) L->ar[a].album = amap[L->ar[a].album];
    free(amap); free(rmap);

    L->sorted = (int *)malloc((size_t)(n + 1) * sizeof(int));
    for (int i = 0; i < n; i++) L->sorted[i] = i;
}

static void apply_sort(Lib *L, int mode) {
    if (!L->sorted) return;
    for (int i = 0; i < L->nt; i++) L->sorted[i] = i;
    sort_lib = L;
    qsort(L->sorted, (size_t)L->nt, sizeof(int),
          mode == SORT_ARTIST ? cmp_artist : mode == SORT_ALBUM ? cmp_album : cmp_title);
}

/* ---------------------------------------------------------------- liked */

static void load_liked(void) {
    free(lib_liked);
    lib_liked = (int *)malloc((size_t)(lib_ntracks + 1) * sizeof(int));
    lib_nliked = 0;
    for (int i = 0; i < lib_ntracks; i++) lib_tracks[i].liked = 0;
    FILE *f = fopen(LIKED_FILE, "r");
    if (!f || !lib_liked) { if (f) fclose(f); return; }
    char line[600];
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        int t = lib_find_path(line);
        if (t >= 0 && !lib_tracks[t].liked) {
            lib_tracks[t].liked = 1;
            lib_liked[lib_nliked++] = t;
        }
    }
    fclose(f);
}

void lib_save_liked(void) {
    FILE *f = fopen(LIKED_FILE, "w");
    if (!f) return;
    for (int i = 0; i < lib_nliked; i++) fprintf(f, "%s\n", lib_tracks[lib_liked[i]].path);
    fclose(f);
}

void lib_set_liked(int t, bool on) {
    if (t < 0 || t >= lib_ntracks || !lib_liked) return;
    if (on == (lib_tracks[t].liked != 0)) return;
    lib_tracks[t].liked = on;
    if (on) {
        memmove(lib_liked + 1, lib_liked, (size_t)lib_nliked * sizeof(int));
        lib_liked[0] = t;
        lib_nliked++;
    } else {
        for (int i = 0; i < lib_nliked; i++)
            if (lib_liked[i] == t) {
                memmove(lib_liked + i, lib_liked + i + 1, (size_t)(lib_nliked - i - 1) * sizeof(int));
                lib_nliked--;
                break;
            }
    }
    lib_save_liked();
}

/* ---------------------------------------------------------------- public */

static void publish(Lib *L) {
    cur = *L;
    lib_tracks = cur.t;  lib_ntracks = cur.nt;
    lib_albums = cur.al; lib_nalbums = cur.nal;
    lib_artists = cur.ar; lib_nartists = cur.nar;
    lib_sorted = cur.sorted;
    apply_sort(&cur, g_sort);
    load_liked();
}

void lib_init(void) {
    mkdir("sdmc:/3ds", 0777);
    mkdir(APP_DIR, 0777);
    mkdir(APP_DIR "/cache", 0777);
    sys_lock_init(&scan_lock);
}

static void save_cache(Lib *L) {
    FILE *f = fopen(CACHE_FILE ".tmp", "w");
    if (!f) return;
    fprintf(f, "%s\n", CACHE_MAGIC);
    for (int i = 0; i < L->nt; i++) {
        Track *t = &L->t[i];
        fprintf(f, "%s\t%s\t%s\t%s\t%u\t%lu\t%lu\n", t->path, t->title, t->artist, t->album,
                (unsigned)t->trackno, (unsigned long)t->art_off, (unsigned long)t->art_len);
    }
    for (int a = 0; a < L->nal; a++)
        if (L->al[a].art_file) fprintf(f, "#cover\t%s\t%s\n", L->al[a].name, L->al[a].art_file);
    fclose(f);
    remove(CACHE_FILE);
    rename(CACHE_FILE ".tmp", CACHE_FILE);
}

bool lib_load_cache(void) {
    FILE *f = fopen(CACHE_FILE, "r");
    if (!f) return false;
    char *line = (char *)malloc(2048);
    if (!line || !fgets(line, 2048, f) || strncmp(line, CACHE_MAGIC, strlen(CACHE_MAGIC))) {
        free(line); fclose(f); return false;
    }
    Lib L;
    memset(&L, 0, sizeof L);
    char **cov_name = NULL, **cov_file = NULL;
    int ncov = 0, capcov = 0;
    while (fgets(line, 2048, f)) {
        line[strcspn(line, "\r\n")] = 0;
        char *fld[7] = { 0 };
        int nf = 0;
        char *p = line;
        while (nf < 7) {
            fld[nf++] = p;
            char *tab = strchr(p, '\t');
            if (!tab) break;
            *tab = 0;
            p = tab + 1;
        }
        if (!strcmp(fld[0], "#cover") && nf >= 3) {
            if (ncov == capcov) {
                capcov = capcov ? capcov * 2 : 64;
                cov_name = (char **)realloc(cov_name, (size_t)capcov * sizeof(char *));
                cov_file = (char **)realloc(cov_file, (size_t)capcov * sizeof(char *));
            }
            cov_name[ncov] = xstrdup(fld[1]);
            cov_file[ncov] = xstrdup(fld[2]);
            ncov++;
            continue;
        }
        if (nf < 7) continue;
        add_track(&L, fld[0], fld[1], fld[2], fld[3], atoi(fld[4]),
                  (unsigned)strtoul(fld[5], NULL, 10), (unsigned)strtoul(fld[6], NULL, 10));
    }
    free(line);
    fclose(f);
    if (L.nt == 0) { lib_free_one(&L); return false; }
    finalize(&L, false);
    for (int c = 0; c < ncov; c++) {
        for (int a = 0; a < L.nal; a++)
            if (!L.al[a].art_file && !strcasecmp(L.al[a].name, cov_name[c])) { L.al[a].art_file = cov_file[c]; cov_file[c] = NULL; break; }
        free(cov_name[c]);
        free(cov_file[c]);
    }
    free(cov_name);
    free(cov_file);
    lib_free_one(&cur);
    publish(&L);
    return true;
}

/* ----------------------------------------------------------------- scan */

static bool skip_dir(const char *name, int depth) {
    if (name[0] == '.') return true;
    if (depth == 0) {
        static const char *skip[] = { "Nintendo 3DS", "Nintendo DSiWare", "luma", "gm9", "boot9strap",
                                      "System Volume Information", "private", NULL };
        for (int i = 0; skip[i]; i++) if (!strcasecmp(name, skip[i])) return true;
    }
    return false;
}

static void walk(Lib *L, const char *dir, int depth) {
    if (depth > 10 || L->nt >= MAX_TRACKS || scan_abort) return;
    DIR *d = opendir(dir);
    if (!d) return;
    sys_lock(&scan_lock);
    snprintf(scan_dir, sizeof scan_dir, "%s", dir);
    sys_unlock(&scan_lock);
    struct dirent *e;
    char path[600];
    /* collect subfolders first and recurse after closing, so few handles stay open */
    char **subs = NULL;
    int nsub = 0, capsub = 0;
    while ((e = readdir(d)) && !scan_abort) {
        const char *nm = e->d_name;
        if (!strcmp(nm, ".") || !strcmp(nm, "..")) continue;
        snprintf(path, sizeof path, "%s%s%s", dir, dir[strlen(dir) - 1] == '/' ? "" : "/", nm);
        bool is_dir = false;
#ifdef DT_DIR
        if (e->d_type == DT_DIR) is_dir = true;
        else if (e->d_type == DT_UNKNOWN)
#endif
        {
            struct stat st;
            if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) is_dir = true;
        }
        if (is_dir) {
            if (skip_dir(nm, depth)) continue;
            if (depth == 1 && !strcasecmp(dir, "sdmc:/3ds") && !strcasecmp(nm, "3DStify")) continue;
            if (nsub == capsub) {
                capsub = capsub ? capsub * 2 : 16;
                char **ns = (char **)realloc(subs, (size_t)capsub * sizeof(char *));
                if (!ns) break;
                subs = ns;
            }
            subs[nsub++] = xstrdup(path);
            continue;
        }
        size_t n = strlen(nm);
        if (n < 5 || nm[0] == '.' || strcasecmp(nm + n - 4, ".mp3")) continue;
        Tags tg;
        read_tags(path, &tg);
        if (add_track(L, path, tg.title, tg.artist, tg.album, tg.trackno, tg.art_off, tg.art_len))
            scan_count = L->nt;
    }
    closedir(d);
    for (int i = 0; i < nsub; i++) {
        walk(L, subs[i], depth + 1);
        free(subs[i]);
    }
    free(subs);
}

static void scan_main(void *arg) {
    (void)arg;
    Lib L;
    memset(&L, 0, sizeof L);
    walk(&L, "sdmc:/", 0);
    if (scan_abort) { lib_free_one(&L); scan_running = false; return; }
    finalize(&L, true);
    if (L.nt) save_cache(&L);
    else remove(CACHE_FILE);
    scan_lib = L;
    scan_done = true;
    scan_running = false;
}

void lib_scan_start(void) {
    if (scan_running) return;
    if (scan_thread) { sys_thread_join(scan_thread); scan_thread = NULL; }
    scan_done = false;
    scan_count = 0;
    scan_dir[0] = 0;
    scan_running = true;
    scan_thread = sys_thread_start(scan_main, NULL, 96 * 1024);
    if (!scan_thread) { scan_running = false; scan_main(NULL); }
}

bool lib_scanning(void) { return scan_running || scan_done; }
int  lib_scan_count(void) { return scan_count; }

void lib_scan_folder(char *out, int n) {
    sys_lock(&scan_lock);
    snprintf(out, (size_t)n, "%s", scan_dir);
    sys_unlock(&scan_lock);
}

bool lib_scan_poll(void) {
    if (!scan_done) return false;
    if (scan_thread) { sys_thread_join(scan_thread); scan_thread = NULL; }
    scan_done = false;
    lib_save_liked();
    lib_free_one(&cur);
    free(lib_liked);
    lib_liked = NULL;
    publish(&scan_lib);
    memset(&scan_lib, 0, sizeof scan_lib);
    return true;
}

void lib_sort(int mode) {
    g_sort = mode;
    apply_sort(&cur, mode);
}

int lib_find_path(const char *path) {
    if (!path || !*path) return -1;
    for (int i = 0; i < lib_ntracks; i++)
        if (!strcmp(lib_tracks[i].path, path)) return i;
    return -1;
}

void lib_free(void) {
    scan_abort = true;
    if (scan_thread) { sys_thread_join(scan_thread); scan_thread = NULL; }
    lib_free_one(&cur);
    free(lib_liked);
    lib_liked = NULL;
    lib_nliked = 0;
}
