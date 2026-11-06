/* 1320: the EP-1320 medieval percussion bank as a native Opus 2 drum engine (core 2.26, Opus 2.0 beta 4).
 * A row of the drum's engine list (shift + T1 in drum mode). Every key of the kit holds eight numbers the OS saves,
 * loads, copies and lets the drum LFO move: blue tune, green sound (any of the 96), white decay, orange drive; with
 * shift: start (its upper half reversed), dusk, age and level. Twelve voices; the tambourines choke each other.
 * Its T1 page: the key's sound as it will play (start, direction, decay), the 24 keys with what sounds, and the four
 * values of the layer the encoders turn.
 * Copyright (C) 2026 Sammi (Project 1320 Perc). GPL-3.0-or-later (see LICENSE and NOTICE). */
#include "opus_plugin.h"
#include "p_voice.h"
#include "p_page.h"

#define VOICES 12

/* The default kit: kicks and snares on the white keys low, tambourines and clappers between, toms, metal and the
 * court's stabs high. Each row: tune, sound, decay, drive, start, dusk, age, level. */
#define SND(s) ((short)((((s) * 2 + 1) * 32768) / (2 * P_NSOUNDS)))       /* p_number_of_sound */
#define K(s)   { 0, SND(s), 32767, 0, 0, 0, 0, 26000 }
/* F..E low: battle kik, tambo hat, medium evil kik, tambo short, fair snare, tambo long, big clap, market snare,
 * clapper a, jester snare, coconut a, tight snare; F..E high: war tom a, tambo cap, frame tom a, coconut b, deep tom a,
 * clank a, anvil hit, sword clang, chain hit, hell bell, stab a, royal kik */
static const short KIT[P_KEYS][8] = {
    K(0), K(39), K(2), K(41), K(18), K(46), K(5), K(36), K(25), K(40), K(29), K(75),
    K(4), K(43), K(28), K(32), K(76), K(57), K(20), K(22), K(17), K(21), K(84), K(72),
};

static struct opl_self *me;
static const p_tab_t *tab_slow;
static struct opus_fast_lease *tab_lease;

/* what the page reads: single words written by the voices (audio) and the hits (key thread) */
volatile p_ui_t p_ui[VOICES];
volatile unsigned p_hits;

/* The voice limit (the "voices" setting, 4-12, 8 to start): a hit past it chokes the voice struck longest ago. The
 * key thread cannot reach another voice's state (the core may move it), so it leaves that voice a flag its next
 * render reads: single bytes, each written by one side. */
static volatile unsigned char kill[VOICES];
static unsigned stamp[VOICES];
static int limit = 8;

static void e_init(void *voice, int index) { p_voice_init((p_voice_t *)voice, index); }

static void e_note_key(void *voice, const short *knobs, int key, int velocity)
{
    p_voice_t *v = (p_voice_t *)voice;
    int me_i = v->index < VOICES ? v->index : 0, busy = 0, oldest = -1;
    for (int i = 0; i < VOICES; i++) {
        if (i == me_i || p_ui[i].on != 1 || kill[i]) continue;      /* playing: not fading, not ending */
        busy++;
        if (oldest < 0 || (int)(stamp[i] - stamp[oldest]) < 0) oldest = i;
    }
    if (busy >= limit && oldest >= 0) kill[oldest] = 1;
    kill[me_i] = 0;
    stamp[me_i] = p_hits;
    p_voice_hit(v, knobs, key, velocity);
    volatile p_ui_t *u = &p_ui[me_i];
    u->on = 0;
    u->key = (unsigned char)key;
    u->rev = v->rev;
    u->from = (unsigned short)(v->pos >> 16);
    u->at = u->from;
    u->snd = v->snd;
    u->on = 1;
    p_hits++;
}

static void e_render(void *voice, short *out, const short *par, const unsigned *pitch)
{
    p_voice_t *v = (p_voice_t *)voice;
    int me_i = v->index < VOICES ? v->index : 0;
    if (kill[me_i]) { kill[me_i] = 0; p_voice_choke(v); }
    p_tab_now = tab_lease ? (const p_tab_t *)opus_fast_use(tab_lease) : tab_slow;
    int on = p_voice_render(v, out, par, pitch ? *pitch : 65536);
    volatile p_ui_t *u = &p_ui[me_i];
    u->at = (unsigned short)(v->pos < 0 ? 0 : v->pos >> 16);
    u->on = (unsigned char)(on ? (v->state == P_FADE ? 2 : 1) : 0);   /* 2: fading out (a choke) */
}

static int e_finished(void *voice) { return p_voice_finished((const p_voice_t *)voice); }
static void e_choke(void *voice) { p_voice_choke((p_voice_t *)voice); }
static int e_group(const short *knobs, int key) { (void)key; return p_choke_group(p_sound_of(knobs[P_SOUND])); }

static int e_key(int key, int pressed, int page)
{
    (void)page;
    if (key == 0x26) p_page_shift(pressed);                     /* shift: the page shows the shift layer */
    return 0;                                                   /* the OS keeps every key */
}

static void e_page(void) { p_page_draw(me); }

static void on_setting(const char *name, int value)
{
    if (name[0] == 'v') limit = value < 4 ? 4 : value > VOICES ? VOICES : value;
}

static const struct opus_engine ENGINE = {
    .size = sizeof (struct opus_engine),
    .name = "1320",
    .knobs = { 0, 0, 32767, 0, 0, 0, 0, 26000 },
    .voice = sizeof (p_voice_t),
    .init = e_init,
    .render = e_render,
    .page = e_page,
    .key = e_key,
    .flags = OPUS_ENGINE_DRUM | OPUS_ENGINE_STATE_MOVES,
    .finished = e_finished,
    .keys = KIT,
    .note_key = e_note_key,
    .group = e_group,
    .choke = e_choke,
};

int opl_init(struct opl_self *self)
{
    me = self;
    /* the tables every sample reads (the filter, the decoder: 4.9 KB) on a lease of fast memory, from a copy of the
     * engine's own in the audio pool: fast while it plays, by the plugin's priority, and given back when it rests */
    p_tab_t *t = (p_tab_t *)opus_alloc_audio(self, sizeof (p_tab_t));
    if (t) {
        const unsigned char *src = (const unsigned char *)p_tab_base();
        unsigned char *dst = (unsigned char *)t;
        for (unsigned i = 0; i < sizeof (p_tab_t); i++) dst[i] = src[i];
        tab_slow = t;
        tab_lease = opus_fast_lease(self, sizeof (p_tab_t), OPUS_FAST_CONST, t);
    } else tab_slow = p_tab_base();
    p_tab_now = tab_slow;
    on_setting("voices", opus_setting(self, "voices"));
    opus_on_setting(self, on_setting);
    p_page_init(self);
    int r = opus_engine_add(self, &ENGINE);
    if (r < 0) { opus_log(self, "1320: engine refused"); return -1; }
    return 0;
}
