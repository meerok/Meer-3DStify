/* 3DStify preview - pretend player */
#include "player.h"
static PlState st = PL_IDLE;
double preview_pos = 83, preview_len = 214;
bool player_init(void) { return true; }
void player_exit(void) {}
void player_load(const char *p, double s, bool a) { (void)p; (void)s; st = a ? PL_PLAYING : PL_PAUSED; }
void player_play(void) { st = PL_PLAYING; }
void player_pause(void) { st = PL_PAUSED; }
void player_toggle(void) { st = st == PL_PLAYING ? PL_PAUSED : PL_PLAYING; }
void player_stop(void) { st = PL_IDLE; }
void player_seek(double s) { preview_pos = s; }
double player_pos(void) { return preview_pos; }
double player_len(void) { return preview_len; }
PlState player_state(void) { return st; }
bool player_is_playing(void) { return st == PL_PLAYING; }
bool player_take_ended(void) { return false; }
void player_set_volume(float v) { (void)v; }
void player_set_eq(int p) { (void)p; }
void player_tick(float dt) { (void)dt; }
void player_suspend(void) {}
void player_resume_after_suspend(void) {}
int player_samplerate(void) { return 44100; }
int player_bitrate(void) { return 320; }
