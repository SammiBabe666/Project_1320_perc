/* Cycle bench for the 1320 voice in GDB's Blackfin simulator (bf524 model): each case's worst and average block (all
 * its voices together, 128 samples at 88.2 kHz each), and a hash of every sample, which the host test (tests/) must
 * match: the Blackfin build (its assembly filter) plays the same samples as the C the host tests check.
 * The simulator counts instructions and branch delays, not the device's memory waits: compare versions with it; the
 * device figure comes from the OP-1 (developer mode, the mods list's details). Build and run: run.sh.
 * Copyright (C) 2026 Sammi (Project 1320 Perc). GPL-3.0-or-later. */
#include "scenarios.h"
extern void sys_write(int fd, const char *buf, int len);
static void puts_(const char *s) { int n = 0; while (s[n]) n++; sys_write(1, s, n); }
static void putu(uint32_t v) { char b[12]; int i = 11; b[i] = 0; do { b[--i] = (char)('0' + v % 10); v /= 10; } while (v); puts_(b + i); }
static void puth(uint32_t v) { char b[9]; for (int i = 7; i >= 0; i--, v >>= 4) b[i] = "0123456789abcdef"[v & 15]; b[8] = 0; puts_(b); }
static inline uint32_t cyc(void) { uint32_t c; __asm__ volatile("%0 = CYCLES;" : "=d"(c)); return c; }

static p_voice_t vs[12];
static int16_t out[P_OUT];
static short par[12][8];

int main(void)
{
    uint32_t overall = 0;
    for (int c = 0; c < NSCEN; c++) {
        const scen_t *s = &SCEN[c];
        for (int v = 0; v < s->voices; v++) {
            p_voice_init(&vs[v], v);
            scen_par(s, v, par[v]);
            p_voice_hit(&vs[v], par[v], v, 127);
        }
        uint32_t worst = 0, total = 0, h = 2166136261u;
        const int nb = 60;
        for (int b = 0; b < nb; b++) {
            uint32_t d = 0;
            for (int v = 0; v < s->voices; v++) {
                if (s->retrig && b % 8 == 7) p_voice_hit(&vs[v], par[v], v, 100);
                uint32_t c0 = cyc();
                p_voice_render(&vs[v], out, par[v], s->host);
                d += cyc() - c0;
                for (int i = 0; i < P_OUT; i++) h = (h ^ (uint16_t)out[i]) * 16777619u;
            }
            total += d;
            if (d > worst) worst = d;
        }
        if (worst > overall) overall = worst;
        puts_(s->name); puts_(" worst "); putu(worst); puts_(" avg "); putu(total / nb); puts_(" hash "); puth(h); puts_("\n");
    }
    puts_("worst block of all cases: "); putu(overall); puts_("\n");
    return 0;
}
