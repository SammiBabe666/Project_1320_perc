/* Host preview of the T1 page: p_page.c's drawing calls written out as lines of text for preview.py to draw. The
 * OP-1's own font is not used (preview.py draws a plain font), so text widths are close, not exact.
 * Copyright (C) 2026 Sammi (Project 1320 Perc). GPL-3.0-or-later. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "opus_plugin.h"
#include "p_page.h"
#include "p_voice.h"

volatile p_ui_t p_ui[12];
volatile unsigned p_hits;
static short cur[8];
static int cur_key;

int opus_drum_key(short par[8]) { memcpy(par, cur, sizeof cur); return cur_key; }
int opus_page_still(struct opl_self *self, unsigned key) { (void)self; (void)key; return 0; }
void opus_page_still_done(struct opl_self *self) { (void)self; }
void opus_page_text(struct opl_self *s, int slot, int x, int y, int size, int c, const char *t) { (void)s; printf("text %d L %d %d %d %d %s\n", slot, x, y, size, c, t); }
void opus_page_text_right(struct opl_self *s, int slot, int x, int y, int size, int c, const char *t) { (void)s; printf("text %d R %d %d %d %d %s\n", slot, x, y, size, c, t); }
void opus_page_fill(int x0, int y0, int x1, int y1, int c) { printf("fill %d %d %d %d %d\n", x0, y0, x1, y1, c); }
void opus_page_bar(int x0, int y0, int x1, int y1, unsigned v, unsigned m, int c) { printf("bar %d %d %d %d %u %u %d\n", x0, y0, x1, y1, v, m, c); }
void opus_page_line(int x0, int y0, int x1, int y1, int c) { printf("line %d %d %d %d %d\n", x0, y0, x1, y1, c); }
void opus_page_dot(int x, int y, int r, int f, int c) { printf("dot %d %d %d %d %d\n", x, y, r, f, c); }

int main(int argc, char **argv)
{
    /* argv: key shift then the eight numbers, then voices as key:fraction(0-100) */
    if (argc < 11) { fprintf(stderr, "mock KEY SHIFT N0..N7 [key:pct ...]\n"); return 1; }
    cur_key = atoi(argv[1]);
    p_page_shift(atoi(argv[2]));
    for (int i = 0; i < 8; i++) cur[i] = (short)atoi(argv[3 + i]);
    for (int a = 11, v = 0; a < argc && v < 12; a++, v++) {
        int k, pct; sscanf(argv[a], "%d:%d", &k, &pct);
        int snd = p_sound_of(k == cur_key ? cur[P_SOUND] : 0), len = p_sound_len(snd) - 1;
        int st = cur[P_START] < 0 ? 0 : cur[P_START];
        int rev = k == cur_key && st >= 16384;
        int off = k == cur_key ? (len * ((st & 16383) >> 4) * 3) >> 12 : 0;
        int from = rev ? len - 1 - off : off, l = rev ? from : len - from;
        p_ui[v].on = 1; p_ui[v].key = (unsigned char)k; p_ui[v].snd = (unsigned char)snd; p_ui[v].rev = (unsigned char)rev;
        p_ui[v].from = (unsigned short)from;
        p_ui[v].at = (unsigned short)(rev ? from - l * pct / 100 : from + l * pct / 100);
    }
    p_page_draw(0);
    return 0;
}
