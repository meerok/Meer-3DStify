#pragma once
#include <stdbool.h>
#include <stdint.h>

#define APP_DIR    "sdmc:/3ds/3DStify"
#define MAX_TRACKS 6000

typedef struct {
    char    *path;
    char    *title;
    char    *artist;
    char    *album;
    uint32_t art_off, art_len;   /* embedded cover (APIC) inside the file, 0 = none */
    uint16_t trackno;
    uint16_t album_id;
    uint16_t artist_id;
    uint8_t  liked;
} Track;

typedef struct {
    char *name;
    char *artist;      /* most common artist on the album */
    int  *tracks;      /* sorted by track number */
    int   ntracks;
    int   art_track;   /* track with an embedded cover, or -1 */
    char *art_file;    /* cover.jpg / folder.jpg next to the songs, or NULL */
    uint32_t key;      /* hash used for the thumbnail cache */
} Album;

typedef struct {
    char *name;
    int  *tracks;
    int   ntracks;
    int   album;       /* an album to borrow artwork from */
} Artist;

enum { SORT_TITLE, SORT_ARTIST, SORT_ALBUM, SORT_COUNT };

extern Track  *lib_tracks;
extern int     lib_ntracks;
extern Album  *lib_albums;
extern int     lib_nalbums;
extern Artist *lib_artists;
extern int     lib_nartists;
extern int    *lib_sorted;      /* all tracks in the current sort order */
extern int    *lib_liked;       /* liked tracks, most recent first */
extern int     lib_nliked;

void lib_init(void);                 /* makes the app folder */
bool lib_load_cache(void);           /* fast path: library.txt from the last scan */
void lib_scan_start(void);           /* scan the SD card on a thread */
bool lib_scanning(void);
bool lib_scan_poll(void);            /* true once when a finished scan was swapped in */
int  lib_scan_count(void);
void lib_scan_folder(char *out, int n);
void lib_sort(int mode);
int  lib_find_path(const char *path);
void lib_set_liked(int t, bool on);
void lib_save_liked(void);
void lib_free(void);
