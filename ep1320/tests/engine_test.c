/* The engine as the OS drives it (a model of Opus's drum voices, not the OS's code): ep1320.c's opl_init and its
 * engine struct, twelve voice states, hits on the 24 keys from a pseudo-random pattern, every busy voice rendered each
 * block, a voice freed when finished says so, a choke group stopping the other keys of the group.
 * Checks: the engine registers as a drum engine with every call the core requires; no more voices sound at once than
 * the "voices" setting allows (a hit past it chokes the oldest, faded out within its next block); every voice ends; the tambourines
 * choke each other; the 24 default keys hold the intended sounds.
 * Copyright (C) 2026 Sammi (Project 1320 Perc). GPL-3.0-or-later. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "opus_plugin.h"
#include "p_voice.h"
#include "p_page.h"

static const struct opus_engine *eng;
static int setting_voices = 8;
static void (*setting_fn)(const char *, int);
void *opus_alloc_audio(struct opl_self *s, unsigned n) { (void)s; return calloc(1, n); }
struct opus_fast_lease *opus_fast_lease(struct opl_self *s, unsigned n, int k, void *slow) { (void)s; (void)n; (void)k; (void)slow; return 0; }
void *opus_fast_use(struct opus_fast_lease *l) { (void)l; return 0; }
void opus_log(struct opl_self *s, const char *t) { (void)s; printf("log: %s\n", t); }
int opus_setting(struct opl_self *s, const char *n) { (void)s; return n[0] == 'v' ? setting_voices : 0; }
void opus_on_setting(struct opl_self *s, void (*fn)(const char *, int)) { (void)s; setting_fn = fn; }
int opus_engine_add(struct opl_self *s, const struct opus_engine *e) { (void)s; eng = e; return 0x1320; }
void p_page_init(struct opl_self *s) { (void)s; }
void p_page_shift(int d) { (void)d; }
void p_page_draw(struct opl_self *s) { (void)s; }

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static unsigned char *state[12];
static int busy[12], vkey[12];
static unsigned since[12], rnd = 1320;
static unsigned next(void) { rnd = rnd * 1103515245u + 12345u; return rnd >> 16; }

static int sounding(void) { int n = 0; for (int v = 0; v < 12; v++) n += busy[v] && !eng->finished(state[v]); return n; }
/* playing: sounding and not fading out (a choke's fade takes at most two blocks) */
static int playing(void) { int n = 0; for (int v = 0; v < 12; v++) n += busy[v] && ((p_voice_t *)state[v])->state == P_PLAY; return n; }

static void hit(int key, unsigned t)
{
    const short *kn = eng->keys[key];
    int g = eng->group ? eng->group(kn, key) : 0;
    if (g)
        for (int v = 0; v < 12; v++)
            if (busy[v] && vkey[v] != key && eng->group(eng->keys[vkey[v]], vkey[v]) == g) eng->choke(state[v]);
    int f = -1;
    for (int v = 0; v < 12 && f < 0; v++) if (!busy[v]) f = v;
    if (f < 0) { f = 0; for (int v = 1; v < 12; v++) if (since[v] < since[f]) f = v; }   /* the OS steals the oldest */
    eng->note_key(state[f], kn, key, 1 + (int)(next() % 127));
    busy[f] = 1; vkey[f] = key; since[f] = t;
}

static int run(int limit, int blocks)
{
    setting_voices = limit;
    setting_fn("voices", limit);
    int over = 0, most = 0;
    short out[128];
    unsigned pitch = 65536;
    for (int b = 0; b < blocks; b++) {
        int hits = next() % 4 == 0 ? 1 + (int)(next() % 4) : 0;            /* bursts of up to four hits */
        for (int h = 0; h < hits; h++) hit((int)(next() % 24), (unsigned)b);
        for (int v = 0; v < 12; v++) {
            if (!busy[v]) continue;
            if (eng->finished(state[v])) { busy[v] = 0; continue; }
            eng->render(state[v], out, eng->keys[vkey[v]], &pitch);
        }
        int s = sounding();
        if (s > most) most = s;
        if (playing() > limit) over++;                                      /* past the limit */
    }
    for (int b = 0; b < 2000; b++)                                          /* let everything end */
        for (int v = 0; v < 12; v++) {
            if (!busy[v]) continue;
            if (eng->finished(state[v])) { busy[v] = 0; continue; }
            eng->render(state[v], out, eng->keys[vkey[v]], &pitch);
        }
    int left = 0;
    for (int v = 0; v < 12; v++) left += busy[v];
    printf("voices %2d: %d blocks of bursts, most sounding at once %d (fades included), blocks playing past the limit %d, left sounding %d\n",
           limit, blocks, most, over, left);
    CHECK(left == 0, "voices still sounding after 2000 blocks");
    return over;
}

int main(void)
{
    struct opl_self self = { sizeof self, "sammi.ep1320", "/tmp", 0 };
    CHECK(opl_init(&self) == 0 && eng, "opl_init");
    CHECK(eng->flags & 16, "not a drum engine");
    CHECK(eng->finished && eng->note_key && eng->render && eng->keys && eng->group && eng->choke, "missing calls");
    CHECK(!(eng->flags & (4 | 8)) && eng->mark == 0 && eng->type == 0, "flags the core refuses for a drum engine");
    CHECK(strlen(eng->name) <= 15, "name too long");
    printf("engine: \"%s\", %u bytes a voice, flags 0x%x\n", eng->name, eng->voice, eng->flags);
    static const int want[24] = { 0, 39, 2, 41, 18, 46, 5, 36, 25, 40, 29, 75, 4, 43, 28, 32, 76, 57, 20, 22, 17, 21, 84, 72 };
    for (int k = 0; k < 24; k++) CHECK(p_sound_of(eng->keys[k][P_SOUND]) == want[k], "key %d sound", k);
    printf("default kit:");
    for (int k = 0; k < 24; k++) printf(" %s%s", p_sound_name(want[k]), k < 23 ? "," : "\n");
    for (int v = 0; v < 12; v++) { state[v] = calloc(1, eng->voice); eng->init(state[v], v); }
    CHECK(run(8, 6000) == 0, "more than 8 voices sounded");
    CHECK(run(4, 6000) == 0, "more than 4 voices sounded");
    run(12, 6000);
    /* choke: tambo long (key 6, group 1) is stopped by tambo hat (key 2) */
    hit(5, 0);
    short out[128]; unsigned pitch = 65536;
    for (int b = 0; b < 5; b++) eng->render(state[0], out, eng->keys[5], &pitch);
    int lv = -1; for (int v = 0; v < 12; v++) if (busy[v] && vkey[v] == 5) lv = v;
    hit(1, 1);
    for (int b = 0; b < 1; b++) for (int v = 0; v < 12; v++) if (busy[v]) eng->render(state[v], out, eng->keys[vkey[v]], &pitch);
    CHECK(lv >= 0 && eng->finished(state[lv]), "tambo hat did not choke tambo long");
    printf("choke: tambo hat stops tambo long within 1 block: %s\n", lv >= 0 && eng->finished(state[lv]) ? "yes" : "no");
    printf(fails ? "FAILED: %d\n" : "OK: engine checks passed\n", fails);
    return fails != 0;
}
