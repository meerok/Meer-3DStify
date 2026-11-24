/*
 * 3DStify - playback engine
 *
 *   SD card file -> libmpg123 (decoder thread) -> NDSP wave buffers
 *
 * The decoder thread owns the mpg123 handle and the wave buffers. The UI
 * thread only posts commands (load / seek / stop) and changes volume, pause
 * and the EQ. Pause, resume, track changes and seeks fade in/out over a few
 * milliseconds so there are no clicks.
 */
#include <3ds.h>
#include <mpg123.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "player.h"

#define CH          0
#define NBUF        8
#define FRAMES      4096
#define DEC_STACK   (64 * 1024)

enum { CMD_NONE, CMD_LOAD, CMD_SEEK, CMD_STOP };

static LightLock lock;
static struct {
    int    type;
    char   path[512];
    double sec;
    bool   autoplay;
} cmd;

static Thread dec_thread;
static volatile bool g_quit = false;
static bool dsp_ok = false;

static ndspWaveBuf wbuf[NBUF];
static s64 wstart[NBUF];         /* first sample of each buffer, in track samples */
static s16 *pcm;

static volatile PlState g_state = PL_IDLE;
static volatile int    g_rate = 0, g_chans = 2, g_kbps = 0;
static volatile s64    g_len = 0;          /* total samples, 0 = unknown */
static volatile s64    g_decoded = 0;
static volatile bool   g_eof = false;
static volatile bool   g_ended = false;
static volatile bool   g_pending = false;  /* a load/seek has not produced audio yet */
static volatile double g_pending_pos = 0;
static volatile bool   g_open = false;

static float g_vol = 0.85f;
static int   g_eq = EQ_FLAT;
static float g_fade = 0.0f, g_fade_target = 0.0f;
static bool  g_pause_pending = false;
static volatile float g_gain = 0.0f;       /* what the channel mix is set to */

static bool  sus_was_playing = false;

/* --------------------------------------------------------------- DSP side */

static float eq_makeup(void) {
    switch (g_eq) {
        case EQ_BASS:   return 0.62f;
        case EQ_TREBLE: return 0.70f;
        case EQ_VOCAL:  return 0.72f;
        default:        return 1.0f;
    }
}

static void apply_mix(float gain) {
    float mix[12];
    memset(mix, 0, sizeof mix);
    mix[0] = mix[1] = gain;
    ndspChnSetMix(CH, mix);
    g_gain = gain;
}

/* RBJ audio-EQ-cookbook biquads at the DSP output rate */
static void apply_eq(void) {
    const float Fs = (float)NDSP_SAMPLE_RATE;
    float f0, gdb, q = 0.7071f;
    int kind;                       /* 0 low shelf, 1 high shelf, 2 peaking */
    switch (g_eq) {
        case EQ_BASS:      kind = 0; f0 = 110.0f;  gdb = 7.0f;  break;
        case EQ_BASS_LESS: kind = 0; f0 = 160.0f;  gdb = -8.0f; break;
        case EQ_TREBLE:    kind = 1; f0 = 5500.0f; gdb = 6.0f;  break;
        case EQ_VOCAL:     kind = 2; f0 = 2400.0f; gdb = 5.0f; q = 0.9f; break;
        default:
            ndspChnIirBiquadSetEnable(CH, false);
            return;
    }
    float A = powf(10.0f, gdb / 40.0f);
    float w0 = 2.0f * (float)M_PI * f0 / Fs;
    float cw = cosf(w0), sw = sinf(w0);
    float alpha = sw / (2.0f * q);
    float b0, b1, b2, a0, a1, a2;
    if (kind == 2) {
        b0 = 1 + alpha * A; b1 = -2 * cw; b2 = 1 - alpha * A;
        a0 = 1 + alpha / A; a1 = -2 * cw; a2 = 1 - alpha / A;
    } else {
        float sA = 2.0f * sqrtf(A) * alpha;
        if (kind == 0) {
            b0 = A * ((A + 1) - (A - 1) * cw + sA);
            b1 = 2 * A * ((A - 1) - (A + 1) * cw);
            b2 = A * ((A + 1) - (A - 1) * cw - sA);
            a0 = (A + 1) + (A - 1) * cw + sA;
            a1 = -2 * ((A - 1) + (A + 1) * cw);
            a2 = (A + 1) + (A - 1) * cw - sA;
        } else {
            b0 = A * ((A + 1) + (A - 1) * cw + sA);
            b1 = -2 * A * ((A - 1) + (A + 1) * cw);
            b2 = A * ((A + 1) + (A - 1) * cw - sA);
            a0 = (A + 1) - (A - 1) * cw + sA;
            a1 = 2 * ((A - 1) - (A + 1) * cw);
            a2 = (A + 1) - (A - 1) * cw - sA;
        }
    }
    /* the DSP takes Q14 coefficients, so keep the numerator inside +-2 */
    float peak = fmaxf(fabsf(b0), fmaxf(fabsf(b1), fabsf(b2))) / a0;
    if (peak > 1.95f) { float k = 1.95f / peak; b0 *= k; b1 *= k; b2 *= k; }
    ndspChnIirBiquadSetParamsCustomFilter(CH, a0, a1, a2, b0, b1, b2);
    ndspChnIirBiquadSetEnable(CH, true);
}

static void configure(int rate, int chans) {
    ndspChnReset(CH);
    ndspChnSetInterp(CH, NDSP_INTERP_POLYPHASE);
    ndspChnSetRate(CH, (float)rate);
    ndspChnSetFormat(CH, chans == 2 ? NDSP_FORMAT_STEREO_PCM16 : NDSP_FORMAT_MONO_PCM16);
    apply_mix(g_gain);
    apply_eq();
}

static void clear_bufs(void) {
    ndspChnWaveBufClear(CH);
    for (int i = 0; i < NBUF; i++) {
        wbuf[i].status = NDSP_WBUF_DONE;
        wstart[i] = 0;
    }
}

static bool any_queued(void) {
    for (int i = 0; i < NBUF; i++)
        if (wbuf[i].status == NDSP_WBUF_QUEUED || wbuf[i].status == NDSP_WBUF_PLAYING) return true;
    return false;
}

/* ---------------------------------------------------------- decoder side */

static void dec_main(void *arg) {
    (void)arg;
    int err = 0;
    mpg123_handle *mh = mpg123_new(NULL, &err);
    if (!mh) return;
    mpg123_param(mh, MPG123_FLAGS, MPG123_QUIET | MPG123_GAPLESS | MPG123_FUZZY, 0.0);
    mpg123_param(mh, MPG123_INDEX_SIZE, -1000, 0.0);
    mpg123_format_none(mh);
    const long *rates;
    size_t nrates;
    mpg123_rates(&rates, &nrates);
    for (size_t i = 0; i < nrates; i++)
        mpg123_format(mh, rates[i], MPG123_MONO | MPG123_STEREO, MPG123_ENC_SIGNED_16);

    while (!g_quit) {
        int type = CMD_NONE;
        char path[512];
        double sec = 0;
        bool autoplay = false;
        LightLock_Lock(&lock);
        if (cmd.type != CMD_NONE) {
            type = cmd.type;
            memcpy(path, cmd.path, sizeof path);
            sec = cmd.sec;
            autoplay = cmd.autoplay;
            cmd.type = CMD_NONE;
        }
        LightLock_Unlock(&lock);

        if (type == CMD_STOP || type == CMD_LOAD) {
            clear_bufs();
            if (g_open) { mpg123_close(mh); g_open = false; }
            g_eof = false;
            g_decoded = 0;
            if (type == CMD_STOP) {
                g_state = PL_IDLE;
                g_pending = false;
                g_len = 0;
            }
        }
        if (type == CMD_LOAD) {
            long rate = 0;
            int ch = 0, enc = 0;
            if (mpg123_open(mh, path) != MPG123_OK || mpg123_getformat(mh, &rate, &ch, &enc) != MPG123_OK) {
                if (g_open) mpg123_close(mh);
                g_open = false;
                g_state = PL_ERROR;
                g_pending = false;
                continue;
            }
            mpg123_format_none(mh);              /* lock the format for this file */
            mpg123_format(mh, rate, ch, MPG123_ENC_SIGNED_16);
            g_open = true;
            g_rate = (int)rate;
            g_chans = ch;
            off_t len = mpg123_length(mh);
            g_len = len > 0 ? (s64)len : 0;
            struct mpg123_frameinfo fi;
            g_kbps = mpg123_info(mh, &fi) == MPG123_OK ? fi.bitrate : 0;
            g_fade = 0;
            configure((int)rate, ch);
            apply_mix(0);
            ndspChnSetPaused(CH, !autoplay);
            if (sec > 0.5) type = CMD_SEEK;      /* fall through to the seek below */
            else g_pending = true;
        }
        if (type == CMD_SEEK && g_open) {
            clear_bufs();
            if (sec < 0) sec = 0;
            off_t want = (off_t)(sec * g_rate);
            if (g_len > 0 && want >= g_len) want = (off_t)g_len - 1;
            off_t got = mpg123_seek(mh, want, SEEK_SET);
            g_decoded = got >= 0 ? (s64)got : 0;
            g_eof = false;
            g_ended = false;
            if (g_state == PL_ENDED) g_state = PL_PAUSED;
            g_fade = 0;
            apply_mix(0);
            g_pending = true;
        }

        if (!g_open || g_eof) {
            if (g_open && g_eof && !any_queued() && g_state != PL_ENDED && g_state != PL_IDLE) {
                g_state = PL_ENDED;
                g_ended = true;
            }
            svcSleepThread(8 * 1000000LL);
            continue;
        }

        int free_i = -1;
        for (int i = 0; i < NBUF; i++)
            if (wbuf[i].status == NDSP_WBUF_DONE || wbuf[i].status == NDSP_WBUF_FREE) { free_i = i; break; }
        if (free_i < 0) { svcSleepThread(6 * 1000000LL); continue; }

        s16 *out = (s16 *)wbuf[free_i].data_vaddr;
        size_t want = (size_t)FRAMES * (size_t)g_chans * 2, got = 0;
        while (got < want) {
            size_t done = 0;
            int r = mpg123_read(mh, (unsigned char *)out + got, want - got, &done);
            got += done;
            if (r == MPG123_DONE) { g_eof = true; break; }
            if (r == MPG123_NEW_FORMAT) continue;
            if (r != MPG123_OK) {
                if (done == 0) { g_eof = true; break; }   /* broken tail: treat as the end */
            }
        }
        size_t frames = got / ((size_t)g_chans * 2);
        if (frames > 0) {
            wbuf[free_i].nsamples = (u32)frames;
            wstart[free_i] = g_decoded;
            g_decoded += (s64)frames;
            DSP_FlushDataCache(out, got);
            ndspChnWaveBufAdd(CH, &wbuf[free_i]);
            g_pending = false;
        } else if (g_eof) {
            g_pending = false;
        }
    }
    if (g_open) mpg123_close(mh);
    mpg123_delete(mh);
}

/* -------------------------------------------------------------- UI side */

bool player_init(void) {
    LightLock_Init(&lock);
    mpg123_init();
    if (R_FAILED(ndspInit())) return false;
    dsp_ok = true;
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspSetOutputCount(1);
    pcm = (s16 *)linearAlloc(NBUF * FRAMES * 2 * sizeof(s16));
    if (!pcm) return false;
    memset(wbuf, 0, sizeof wbuf);
    for (int i = 0; i < NBUF; i++) {
        wbuf[i].data_vaddr = pcm + i * FRAMES * 2;
        wbuf[i].status = NDSP_WBUF_DONE;
    }
    s32 prio = 0x30;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    /* BELOW the main thread: the main thread must always be able to answer the
     * system (HOME / power button) right away, or the whole console hangs.
     * The 8-buffer queue (~0.7s) keeps audio smooth while the UI draws. */
    s32 dprio = prio + 1;
    if (dprio > 0x3F) dprio = 0x3F;
    dec_thread = threadCreate(dec_main, NULL, DEC_STACK, dprio, 2, false);    /* New 3DS core 2 */
    if (!dec_thread) dec_thread = threadCreate(dec_main, NULL, DEC_STACK, dprio, -2, false);
    return dec_thread != NULL;
}

void player_exit(void) {
    g_quit = true;
    if (dec_thread) {
        threadJoin(dec_thread, U64_MAX);
        threadFree(dec_thread);
        dec_thread = NULL;
    }
    if (dsp_ok) {
        ndspChnWaveBufClear(CH);
        ndspExit();
    }
    if (pcm) linearFree(pcm);
    mpg123_exit();
}

static void post(int type, const char *path, double sec, bool autoplay) {
    LightLock_Lock(&lock);
    cmd.type = type;
    if (path) snprintf(cmd.path, sizeof cmd.path, "%s", path);
    cmd.sec = sec;
    cmd.autoplay = autoplay;
    LightLock_Unlock(&lock);
}

void player_load(const char *path, double start_sec, bool autoplay) {
    if (!dsp_ok) { g_state = PL_ERROR; return; }
    g_pending = true;
    g_pending_pos = start_sec > 0 ? start_sec : 0;
    g_ended = false;
    g_pause_pending = false;
    g_state = autoplay ? PL_PLAYING : PL_PAUSED;
    g_fade_target = autoplay ? 1.0f : 0.0f;
    post(CMD_LOAD, path, start_sec, autoplay);
}

void player_play(void) {
    if (g_state == PL_ENDED) {
        g_state = PL_PLAYING;
        player_seek(0);
    }
    if (g_state != PL_PAUSED && g_state != PL_PLAYING) return;
    g_state = PL_PLAYING;
    g_pause_pending = false;
    if (dsp_ok) ndspChnSetPaused(CH, false);
    g_fade_target = 1.0f;
}

void player_pause(void) {
    if (g_state != PL_PLAYING) return;
    g_state = PL_PAUSED;
    g_fade_target = 0.0f;
    g_pause_pending = true;
}

void player_toggle(void) {
    if (g_state == PL_PLAYING) player_pause();
    else player_play();
}

void player_stop(void) {
    g_state = PL_IDLE;
    g_fade_target = 0;
    post(CMD_STOP, NULL, 0, false);
}

void player_seek(double sec) {
    if (g_state == PL_IDLE || g_state == PL_ERROR) return;
    if (sec < 0) sec = 0;
    double len = player_len();
    if (len > 0 && sec > len - 0.25) sec = len - 0.25;
    if (sec < 0) sec = 0;
    g_pending = true;
    g_pending_pos = sec;
    g_ended = false;
    post(CMD_SEEK, NULL, sec, false);
}

double player_pos(void) {
    if (g_pending) return g_pending_pos;
    int rate = g_rate;
    if (rate <= 0) return 0;
    if (g_state == PL_ENDED) return g_len > 0 ? (double)g_len / rate : (double)g_decoded / rate;
    for (int i = 0; i < NBUF; i++)
        if (wbuf[i].status == NDSP_WBUF_PLAYING)
            return (double)(wstart[i] + (s64)ndspChnGetSamplePos(CH)) / rate;
    s64 best = -1;
    for (int i = 0; i < NBUF; i++)
        if (wbuf[i].status == NDSP_WBUF_QUEUED && (best < 0 || wstart[i] < best)) best = wstart[i];
    if (best >= 0) return (double)best / rate;
    return (double)g_decoded / rate;
}

double player_len(void) {
    if (g_rate <= 0 || g_len <= 0) return 0;
    return (double)g_len / g_rate;
}

PlState player_state(void) { return g_state; }
bool player_is_playing(void) { return g_state == PL_PLAYING; }
int player_samplerate(void) { return g_rate; }
int player_bitrate(void) { return g_kbps; }

bool player_take_ended(void) {
    if (!g_ended) return false;
    g_ended = false;
    return true;
}

void player_set_volume(float v) {
    if (v < 0) v = 0;
    if (v > 1) v = 1;
    g_vol = v;
}

void player_set_eq(int preset) {
    if (preset < 0 || preset >= EQ_COUNT) preset = EQ_FLAT;
    g_eq = preset;
    if (dsp_ok) apply_eq();
}

void player_tick(float dt) {
    if (!dsp_ok) return;
    if (g_pending) {
        g_fade = 0;           /* stay silent until fresh audio is queued */
    } else {
        float speed = dt / 0.12f;
        if (g_fade < g_fade_target) { g_fade += speed; if (g_fade > g_fade_target) g_fade = g_fade_target; }
        else if (g_fade > g_fade_target) { g_fade -= speed * 1.4f; if (g_fade < g_fade_target) g_fade = g_fade_target; }
    }
    /* perceptual volume curve */
    float v = g_vol * g_vol;
    float gain = v * eq_makeup() * g_fade * g_fade;
    if (fabsf(gain - g_gain) > 0.0005f) apply_mix(gain);
    if (g_pause_pending && g_fade <= 0.001f) {
        ndspChnSetPaused(CH, true);
        g_pause_pending = false;
    }
}

void player_suspend(void) {
    sus_was_playing = g_state == PL_PLAYING;
    if (sus_was_playing && dsp_ok) {
        g_state = PL_PAUSED;
        g_fade_target = 0;
        g_fade = 0;
        g_pause_pending = false;
        apply_mix(0);
        ndspChnSetPaused(CH, true);
    }
}

void player_resume_after_suspend(void) {
    if (g_state == PL_PAUSED || g_state == PL_PLAYING) {
        /* the DSP may have dropped its queue while we were away: re-queue from here */
        double p = player_pos();
        player_seek(p);
    }
    if (sus_was_playing) player_play();
    sus_was_playing = false;
}
