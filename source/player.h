#pragma once
#include <stdbool.h>

typedef enum {
    PL_IDLE,      /* nothing loaded */
    PL_PLAYING,
    PL_PAUSED,
    PL_ENDED,     /* reached the end of the file */
    PL_ERROR
} PlState;

enum { EQ_FLAT, EQ_BASS, EQ_BASS_LESS, EQ_TREBLE, EQ_VOCAL, EQ_COUNT };

bool    player_init(void);
void    player_exit(void);
void    player_load(const char *path, double start_sec, bool autoplay);
void    player_play(void);
void    player_pause(void);
void    player_toggle(void);
void    player_stop(void);
void    player_seek(double sec);
double  player_pos(void);
double  player_len(void);
PlState player_state(void);
bool    player_is_playing(void);   /* playing, or about to be */
bool    player_take_ended(void);   /* true once when a track finishes */
void    player_set_volume(float v);/* 0..1 */
void    player_set_eq(int preset);
void    player_tick(float dt);     /* call every frame: fades */
void    player_suspend(void);      /* entering HOME Menu / sleep */
void    player_resume_after_suspend(void);
int     player_samplerate(void);
int     player_bitrate(void);
