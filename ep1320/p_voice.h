/* 1320: one drum voice of the 1320 engine, playing a sound of the EP-1320 bank (md_bank.h) with its key's eight
 * numbers. Pure C, integer only, no firmware addresses, no allocation: ep1320.c calls it from the Opus drum engine
 * service, and the host tests and the simulator bench call it directly.
 * Copyright (C) 2026 Sammi (Project 1320 Perc). GPL-3.0-or-later (see LICENSE and NOTICE). */
#ifndef P_VOICE_H
#define P_VOICE_H
#include <stdint.h>

#define P_KEYS      24
#define P_NSOUNDS   96
#define P_OUT       128         /* samples a render writes (88.2 kHz) */
#define P_HALF      64          /* samples the voice computes a render (44.1 kHz) */

/* The eight numbers of a key, as the OS stores and the LFO moves them (number 0 is signed, the others 0..32767). */
enum {
    P_TUNE = 0,                 /* blue: semitones x 1024, -24320..24832 */
    P_SOUND,                    /* green: which of the 96 sounds */
    P_DECAY,                    /* white: 30 ms .. 3 s to -60 dB; the top step plays the sound to its natural end */
    P_DRIVE,                    /* orange: a soft clip, its level kept */
    P_START,                    /* shift blue: start 0..3/4 into the sound; its upper half plays it reversed */
    P_DUSK,                     /* shift green: a one-pole low pass, open .. about 250 Hz */
    P_AGE,                      /* shift white: sample-and-hold and fewer bits */
    P_LEVEL,                    /* shift orange: the key's level */
};

enum { P_OFF = 0, P_PLAY, P_FADE };

typedef struct {
    int32_t pos;                /* Q16 position in the sound, in its own samples */
    int32_t env;                /* decay envelope, Q30 */
    int32_t wblk;               /* block decoded into win (-2: none, -1: the silence before the sound) */
    int16_t win[64 + 16];       /* that block and the start of the next, one piece for the 16-tap filter */
    int16_t last;               /* last 44.1 kHz sample (the 2x upsampler's memory) */
    int16_t declick;            /* the output when a hit cut a sounding voice, faded out over 64 samples */
    int16_t lp;                 /* dusk's state */
    int16_t hold;               /* age's held sample */
    uint16_t age;               /* samples since the hit (saturating), for a late start's fade-in */
    uint8_t state, snd, rev, key, vel, hold_n, fade_n, pending, fade_in, index;
} p_voice_t;

/* The tables every sample reads: the interpolation filter (64 phases x 16 taps, Q14) and the ADPCM decoder's two
 * tables. One block of 4,896 bytes, so the engine can keep it on one lease of fast memory: p_tab_now is where the
 * voices read it in this block (the fast copy, or the engine's own); p_tab_base() the original. */
typedef struct {
    int16_t fir[64][16];
    uint16_t diff[712];
    uint16_t next[712];
} p_tab_t;
extern const p_tab_t *p_tab_now;
const p_tab_t *p_tab_base(void);

int p_sound_of(int number);                 /* the sound (0..95) a green value picks */
short p_number_of_sound(int snd);           /* the green value at the middle of a sound's range */
int p_choke_group(int snd);                 /* 1 for the tambourines that choke each other, else 0 */
int32_t p_ratio(int tune, uint32_t host);   /* Q16 playback ratio: number 0 and the OS's pitch (Q16) */

void p_voice_init(p_voice_t *v, int index);
void p_voice_hit(p_voice_t *v, const short *par, int key, int velocity);
void p_voice_choke(p_voice_t *v);
/* one block: 128 samples at 88.2 kHz into out; par the playing key's numbers (LFO in), host the OS's pitch ratio Q16.
 * Returns 1 while the voice sounds, 0 once it has ended (out is complete either way). */
int p_voice_render(p_voice_t *v, int16_t *out, const short *par, uint32_t host);
int p_voice_finished(const p_voice_t *v);

/* for the page */
int32_t p_decay_ms(int number);             /* time to -60 dB, 0: natural */
int32_t p_sound_ms(int snd);                /* the sound's length as recorded */
int32_t p_sound_len(int snd);               /* in its own samples */
const char *p_sound_name(int snd);
/* the peak of each of n equal parts of the sound (0..255), decoding it: start-up work, not for the audio */
void p_sound_peaks(int snd, uint8_t *peaks, int n);
#endif
