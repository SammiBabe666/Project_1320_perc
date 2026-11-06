/* Host tests for the 1320 voice (run under ASan and UBSan by tests/run.sh). What each check holds:
 *  - every sound, forwards and reversed, at three tunes, plays and ends on its own, its output bounded;
 *  - no click: the largest step between neighbouring output samples where a hit starts, at a retrigger of a sounding
 *    voice, and at a choke stays small; a choke ends the voice within its block;
 *  - a hit filled while the audio renders the voice (pending) is silent and is never reported finished;
 *  - tune: +12 semitones doubles the rate, -12 halves it, +7 is a fifth; the OS's pitch multiplies it;
 *  - velocity (squared), level and drive keep their expected levels; the shortest decay shortens a long sound;
 *  - the scenarios' hashes, which the Blackfin simulator bench must match (bench/run.sh prints its own).
 * Copyright (C) 2026 Sammi (Project 1320 Perc). GPL-3.0-or-later. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "p_voice.h"
#include "../bench/scenarios.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void defaults(short *par, int snd)
{
    static const short d[8] = { 0, 0, 32767, 0, 0, 0, 0, 26000 };
    memcpy(par, d, sizeof d);
    par[P_SOUND] = p_number_of_sound(snd);
}

/* play a hit to its end; returns blocks rendered, the peak, the largest step in the first 8 samples */
static int play(p_voice_t *v, const short *par, uint32_t host, int *peak, int *first_step, int max_blocks)
{
    int16_t out[P_OUT];
    int prev = 0, pk = 0, fs = 0, b;
    for (b = 0; b < max_blocks; b++) {
        if (p_voice_finished(v)) break;
        p_voice_render(v, out, par, host);
        for (int i = 0; i < P_OUT; i++) {
            int a = abs(out[i]);
            if (a > pk) pk = a;
            if (b == 0 && i < 8 && abs(out[i] - prev) > fs) fs = abs(out[i] - prev);
            prev = out[i];
        }
    }
    *peak = pk; *first_step = fs;
    return b;
}

static int max_step(const int16_t *a, int n, int prev)
{
    int m = 0;
    for (int i = 0; i < n; i++) { int d = abs(a[i] - prev); if (d > m) m = d; prev = a[i]; }
    return m;
}

int main(void)
{
    p_voice_t v;
    short par[8];
    int16_t out[P_OUT];

    /* every sound ends on its own; bounded; a clean first sample */
    int hits = 0, worst_first = 0, loudest = 0;
    for (int s = 0; s < P_NSOUNDS; s++)
        for (int rev = 0; rev < 2; rev++)
            for (int t = -1; t <= 1; t++) {
                defaults(par, s);
                par[P_TUNE] = (short)(t * 12 * 1024);
                par[P_START] = rev ? 16384 : 0;
                p_voice_init(&v, 0);
                p_voice_hit(&v, par, 0, 127);
                int pk, fs, b = play(&v, par, 65536, &pk, &fs, 4000);
                CHECK(b < 4000, "sound %d rev %d tune %d never ended", s, rev, t);
                if (fs > worst_first) worst_first = fs;
                if (pk > loudest) loudest = pk;
                hits++;
            }
    printf("all sounds: %d hits, all ended; largest step in a hit's first 8 samples %d; loudest peak %d\n", hits,
           worst_first, loudest);
    CHECK(worst_first < 2500, "a hit starts with a step of %d", worst_first);
    CHECK(loudest < 32000, "a single hit peaks at %d", loudest);

    /* retrigger: a sounding voice struck again fades its old sound out */
    int worst_retrig = 0;
    for (int s = 0; s < P_NSOUNDS; s += 5) {
        defaults(par, s);
        p_voice_init(&v, 0);
        p_voice_hit(&v, par, 0, 127);
        for (int b = 0; b < 30; b++) {
            p_voice_render(&v, out, par, 65536);
            if (b % 7 == 3) {
                int16_t prev = out[P_OUT - 1];
                short p2[8]; defaults(p2, (s + 37) % P_NSOUNDS); p2[P_START] = 9000;
                p_voice_hit(&v, p2, 1, 127);
                p_voice_render(&v, out, p2, 65536);
                int m = max_step(out, 8, prev);
                if (m > worst_retrig) worst_retrig = m;
            }
        }
    }
    printf("retrigger: largest step at a retrigger %d\n", worst_retrig);
    CHECK(worst_retrig < 4000, "a retrigger steps by %d", worst_retrig);

    /* choke: fades out within a block, then finished */
    defaults(par, 46);                                  /* tambo long */
    CHECK(p_choke_group(46) == 1 && p_choke_group(38) == 0 && p_choke_group(0) == 0, "choke groups");
    p_voice_init(&v, 0);
    p_voice_hit(&v, par, 5, 127);
    for (int b = 0; b < 10; b++) p_voice_render(&v, out, par, 65536);
    int16_t before = out[P_OUT - 1];
    p_voice_choke(&v);
    p_voice_render(&v, out, par, 65536);
    int cs = max_step(out, P_OUT, before);
    CHECK(p_voice_finished(&v), "a choked voice did not finish within its block");
    printf("choke: largest step %d, finished within its block\n", cs);
    CHECK(cs < 2500, "a choke steps by %d", cs);

    /* pending: silent, never finished */
    p_voice_init(&v, 0);
    v.pending = 1;
    CHECK(p_voice_render(&v, out, par, 65536) == 1 && max_step(out, P_OUT, 0) == 0, "pending render not silent");
    CHECK(!p_voice_finished(&v), "pending voice reported finished");

    /* tune and the OS's pitch */
    CHECK(p_ratio(12 * 1024, 65536) == 131072, "+12 semitones: ratio %d", p_ratio(12 * 1024, 65536));
    CHECK(p_ratio(-12 * 1024, 65536) == 32768, "-12 semitones: ratio %d", p_ratio(-12 * 1024, 65536));
    CHECK(p_ratio(0, 131072) == 131072, "the OS's octave up: ratio %d", p_ratio(0, 131072));
    int r7 = p_ratio(7 * 1024, 65536);
    CHECK(abs(r7 - 98193) < 40, "+7 semitones: ratio %d (98193)", r7);

    /* levels: velocity squared, level, drive keeping its level */
    int pk127, pk64, pkhalf, pkdrive, fs;
    defaults(par, 0);
    p_voice_init(&v, 0); p_voice_hit(&v, par, 0, 127); play(&v, par, 65536, &pk127, &fs, 2000);
    p_voice_init(&v, 0); p_voice_hit(&v, par, 0, 64); play(&v, par, 65536, &pk64, &fs, 2000);
    par[P_LEVEL] = 13000;
    p_voice_init(&v, 0); p_voice_hit(&v, par, 0, 127); play(&v, par, 65536, &pkhalf, &fs, 2000);
    par[P_LEVEL] = 26000; par[P_DRIVE] = 32767;
    p_voice_init(&v, 0); p_voice_hit(&v, par, 0, 127); play(&v, par, 65536, &pkdrive, &fs, 2000);
    printf("levels (battle kik): velocity 127 peak %d, velocity 64 peak %d, half level peak %d, full drive peak %d\n",
           pk127, pk64, pkhalf, pkdrive);
    CHECK(pk64 * 3 < pk127 && pk64 * 5 > pk127, "velocity 64 is not about a quarter");
    CHECK(pkhalf * 2 > pk127 - 600 && pkhalf * 2 < pk127 + 600, "half level is not half");
    CHECK(pkdrive < pk127 * 2 && pkdrive > pk127 / 2, "drive changes the level too much (%d)", pkdrive);

    /* decay: a short decay ends a long sound early */
    defaults(par, 5);
    int nat = 0, shortd = 0;
    p_voice_init(&v, 0); p_voice_hit(&v, par, 0, 127); nat = play(&v, par, 65536, &pk127, &fs, 4000);
    par[P_DECAY] = 0;
    p_voice_init(&v, 0); p_voice_hit(&v, par, 0, 127); shortd = play(&v, par, 65536, &pk127, &fs, 4000);
    printf("decay (big clap): natural %d blocks, shortest %d blocks (%d ms to -60 dB)\n", nat, shortd,
           (int)p_decay_ms(0));
    CHECK(shortd < nat / 4, "the shortest decay does not shorten the sound");

    /* the scenarios' hashes (the bench prints the same from the Blackfin) */
    static p_voice_t vs[12];
    static short sp[12][8];
    for (int c = 0; c < NSCEN; c++) {
        const scen_t *s = &SCEN[c];
        for (int k = 0; k < s->voices; k++) { p_voice_init(&vs[k], k); scen_par(s, k, sp[k]); p_voice_hit(&vs[k], sp[k], k, 127); }
        uint32_t h = 2166136261u;
        for (int b = 0; b < 60; b++)
            for (int k = 0; k < s->voices; k++) {
                if (s->retrig && b % 8 == 7) p_voice_hit(&vs[k], sp[k], k, 100);
                p_voice_render(&vs[k], out, sp[k], s->host);
                for (int i = 0; i < P_OUT; i++) h = (h ^ (uint16_t)out[i]) * 16777619u;
            }
        printf("%s hash %08x\n", s->name, h);
    }

    printf(fails ? "FAILED: %d\n" : "OK: all checks passed\n", fails);
    return fails != 0;
}
