/* The bench's and the host test's shared cases: hits through p_voice_render, a hash of every sample.
 * Copyright (C) 2026 Sammi (Project 1320 Perc). GPL-3.0-or-later. */
#ifndef SCENARIOS_H
#define SCENARIOS_H
#include "p_voice.h"

typedef struct { const char *name; int voices, heavy, tune, retrig; uint32_t host; } scen_t;

/* plain: decay natural, no drive, dusk or age. heaviest: decay 600 ms, drive, dusk and age full, reversed from a
 * late start (the full loop every sample). retrig: every voice struck again every 8 blocks (declicks, new windows). 8 voices: the engine's
 * limit to start (the voices setting); 12: the setting at its top. */
static const scen_t SCEN[] = {
    { "1 voice,   plain,    tune  0      ", 1, 0, 0, 0, 65536 },
    { "1 voice,   plain,    tune +12     ", 1, 0, 12 * 1024, 0, 65536 },
    { "1 voice,   plain,    tune +24 x2  ", 1, 0, 24 * 1024, 0, 131072 },
    { "1 voice,   heaviest, tune  0      ", 1, 1, 0, 0, 65536 },
    { "1 voice,   heaviest, tune +24 x2  ", 1, 1, 24 * 1024, 0, 131072 },
    { "8 voices,  plain,    tune  0      ", 8, 0, 0, 0, 65536 },
    { "8 voices,  plain,    tune +24 x2  ", 8, 0, 24 * 1024, 0, 131072 },
    { "8 voices,  heaviest, tune +24 x2  ", 8, 1, 24 * 1024, 0, 131072 },
    { "8 voices,  heaviest, retriggered  ", 8, 1, 24 * 1024, 1, 131072 },
    { "12 voices, plain,    tune  0      ", 12, 0, 0, 0, 65536 },
    { "12 voices, plain,    tune +24 x2  ", 12, 0, 24 * 1024, 0, 131072 },
    { "12 voices, heaviest, tune  0      ", 12, 1, 0, 0, 65536 },
    { "12 voices, heaviest, tune +24 x2  ", 12, 1, 24 * 1024, 0, 131072 },
    { "12 voices, heaviest, retriggered  ", 12, 1, 24 * 1024, 1, 131072 },
};
#define NSCEN ((int)(sizeof SCEN / sizeof SCEN[0]))

static void scen_par(const scen_t *s, int v, short *par)
{
    /* the longest sounds of the bank (22.05 kHz, 0.8 s) on every voice, so all twelve sound throughout */
    static const int longest[12] = { 5, 22, 46, 47, 67, 70, 71, 84, 90, 17, 20, 8 };
    par[P_TUNE] = (short)s->tune;
    par[P_SOUND] = p_number_of_sound(longest[v % 12]);
    par[P_DECAY] = s->heavy ? 21 << 9 : 32767;
    par[P_DRIVE] = s->heavy ? 32767 : 0;
    par[P_START] = s->heavy ? 16384 + 3000 : 0;
    par[P_DUSK] = s->heavy ? 32767 : 0;
    par[P_AGE] = s->heavy ? 32767 : 0;
    par[P_LEVEL] = 26000;
}
#endif
