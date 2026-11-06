/* 1320: the drum's T1 page. Black, white lines and grey, the encoders' colours only on their four bars.
 *
 *   1320                                         5 battle kik
 *   ||||||||||||||||||||||||....                 the key's sound as this hit will play it: from its start, forwards
 *   ----------------------------------           or reversed, shaped by the decay; a line runs along it as it plays
 *    o o  o o o  o o  o o o                      the 24 keys (raised: the black ones); a ring on the key the
 *   o o o o o o o o o o o o o o                  encoders turn, a dot on every key that sounds
 *   tune      sound     decay     drive          the layer the encoders turn (shift: start, dusk, age, level)
 *   +0.0      1         natural   off
 *   ====      ====      ====      ====           blue, green, white, orange
 *
 * Everything but the moving parts is drawn once and kept (opus_page_still) until a value, the key or shift changes.
 * Copyright (C) 2026 Sammi (Project 1320 Perc). GPL-3.0-or-later (see LICENSE and NOTICE). */
#include <stdint.h>
#include "opus_plugin.h"
#include "p_page.h"
#include "p_voice.h"
#include "p_peaks.h"

#define VOICES  12
#define WX0     14              /* the sound's picture */
#define WX1     306
#define WY      54
#define WH      24
#define KY_W    101             /* key strip: white keys' row, black keys' row */
#define KY_B    93
#define CY      110             /* the four values */

extern volatile p_ui_t p_ui[VOICES];
static volatile int shift_down;

/* 10^(-n/20) x 256, n = 0..60 dB */
static const uint8_t db_amp[61] = {
    255, 228, 203, 181, 162, 144, 128, 114, 102, 91, 81, 72, 64, 57, 51, 46, 41, 36, 32, 29, 26, 23, 20, 18,
    16, 14, 13, 11, 10, 9, 8, 7, 6, 6, 5, 5, 4, 4, 3, 3, 3, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0,
    0, 0, 0 };

void p_page_init(struct opl_self *self) { (void)self; }
void p_page_shift(int down) { shift_down = down != 0; }

static int clamp15(int x) { return x < 0 ? 0 : x > 32767 ? 32767 : x; }

/* text: an integer, a sign, a decimal point, a unit */
static char *put_u(char *p, unsigned v)
{
    char t[10];
    int n = 0;
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 10);
    while (n) *p++ = t[--n];
    return p;
}
static char *put_s(char *p, const char *s) { while (*s) *p++ = *s++; return p; }

static void fmt_tune(char *b, int v)                    /* semitones, one decimal */
{
    int t = (v * 10 + (v < 0 ? -512 : 512)) / 1024;
    char *p = b;
    if (t > 0) *p++ = '+';
    if (t < 0) { *p++ = '-'; t = -t; }
    p = put_u(p, (unsigned)(t / 10));
    *p++ = '.';
    p = put_u(p, (unsigned)(t % 10));
    *p = 0;
}
static void fmt_pct(char *b, int v, const char *zero)   /* 0..32767 as 0..100 %, or a word at 0 */
{
    int pc = (clamp15(v) * 100 + 16383) / 32767;
    char *p = b;
    if (pc == 0 && zero) p = put_s(p, zero);
    else { p = put_u(p, (unsigned)pc); *p++ = '%'; }
    *p = 0;
}
static void fmt_ms(char *b, int32_t ms)
{
    char *p = b;
    if (ms <= 0) p = put_s(p, "natural");
    else if (ms < 1000) { p = put_u(p, (unsigned)ms); p = put_s(p, "ms"); }
    else { p = put_u(p, (unsigned)(ms / 1000)); *p++ = '.'; p = put_u(p, (unsigned)(ms % 1000 / 100)); *p++ = 's'; }
    *p = 0;
}

/* where a hit with these numbers starts, how far it plays (its own samples) and in which direction */
static void span(const short *par, int snd, int32_t *from, int32_t *len, int *rev)
{
    int32_t last = p_sound_len(snd) - 1;
    int st = clamp15(par[P_START]);
    int32_t off = (last * ((st & 16383) >> 4) * 3) >> 12;
    *rev = st >= 16384;
    *from = *rev ? last - 1 - off : off;
    *len = last - off;
    if (*len < 1) *len = 1;
}

static void picture(const short *par, int snd)
{
    int32_t from, len;
    int rev;
    span(par, snd, &from, &len, &rev);
    int32_t total = p_sound_len(snd);
    /* the hit's length in ms at this tune: its own samples at its own rate, sped up by the tune */
    int32_t ms = (int32_t)(((int64_t)p_sound_ms(snd) * len / (total ? total : 1)) * 65536 / p_ratio(par[P_TUNE], 65536));
    int32_t t60 = p_decay_ms(par[P_DECAY]);
    opus_page_line(WX0, WY, WX1, WY, OPUS_COLOR_DIM);
    for (int c = 0; c < P_PEAKS; c++) {
        int32_t s = (int32_t)c * len / P_PEAKS;
        int32_t src = rev ? from - s : from + s;
        if (src < 0) src = 0;
        int pk = p_peaks[snd][(src * P_PEAKS) / (total ? total : 1)];
        int amp = 255;
        if (t60 > 0) {
            int32_t db = (int32_t)((int64_t)ms * c * 60 / P_PEAKS / t60);
            amp = db >= 60 ? 0 : db_amp[db];
        }
        int x = WX0 + 3 + c * (WX1 - WX0 - 6) / (P_PEAKS - 1);
        int h = pk * amp * WH / (255 * 255);
        if (amp == 0) continue;                         /* past -60 dB: nothing to show */
        if (h < 1) h = 1;
        opus_page_line(x, WY - h, x, WY + h, OPUS_COLOR_TEXT);
    }
}

static int is_black(int k) { int n = k % 12; return n == 1 || n == 3 || n == 5 || n == 8 || n == 10; }
static int key_x(int k) { return 20 + k * 12; }
static int key_y(int k) { return is_black(k) ? KY_B : KY_W; }

static void values(struct opl_self *self, const short *par, int snd, int sh)
{
    static const char *const LAB[2][4] = { { "tune", "sound", "decay", "drive" }, { "start", "dusk", "age", "level" } };
    char v[4][16];
    const char *lab[4] = { LAB[sh][0], LAB[sh][1], LAB[sh][2], LAB[sh][3] };
    int bar[4];
    if (!sh) {
        fmt_tune(v[0], par[P_TUNE]);
        char *p = put_u(v[1], (unsigned)(snd + 1)); *p = 0;
        fmt_ms(v[2], p_decay_ms(par[P_DECAY]));
        fmt_pct(v[3], par[P_DRIVE], "off");
        bar[0] = -1; bar[1] = par[P_SOUND]; bar[2] = par[P_DECAY]; bar[3] = par[P_DRIVE];
    } else {
        int st = clamp15(par[P_START]);
        if (st >= 16384) lab[0] = "reverse";
        fmt_pct(v[0], ((st & 16383) * 3) >> 1, 0);     /* 0 .. 75 % into the sound */
        fmt_pct(v[1], par[P_DUSK], "open");
        fmt_pct(v[2], par[P_AGE], "off");
        fmt_pct(v[3], par[P_LEVEL], 0);
        bar[0] = (st & 16383) * 2; bar[1] = par[P_DUSK]; bar[2] = par[P_AGE]; bar[3] = par[P_LEVEL];
    }
    for (int j = 0; j < 4; j++) {
        int x = 14 + j * 76;
        opus_page_text(self, 3 + j, x, CY, 10, OPUS_COLOR_DIM, lab[j]);
        opus_page_text(self, 7 + j, x, CY + 12, 14, OPUS_COLOR_TEXT, v[j]);
        if (bar[j] >= 0) opus_page_bar(x, 141, x + 64, 142, (unsigned)clamp15(bar[j]), 32767, OPUS_COLOR_E(j));
        else {                                          /* tune: from the middle */
            int mid = x + 32, t = par[P_TUNE] / 768;    /* +-32 units for +-24 semitones */
            opus_page_fill(x, 141, x + 64, 142, OPUS_COLOR_OFF);
            if (t > 0) opus_page_fill(mid, 141, mid + t, 142, OPUS_COLOR_E(0));
            else if (t < 0) opus_page_fill(mid + t, 141, mid, 142, OPUS_COLOR_E(0));
            opus_page_fill(mid, 139, mid, 144, OPUS_COLOR_E(0));
        }
    }
}

void p_page_draw(struct opl_self *self)
{
    short par[8];
    int key = opus_drum_key(par);
    if (key < 0 || key >= P_KEYS) key = 0;
    int sh = shift_down, snd = p_sound_of(par[P_SOUND]);
    unsigned h = 2166136261u;
    for (int i = 0; i < 8; i++) h = (h ^ (unsigned short)par[i]) * 16777619u;
    h = (h ^ (unsigned)(key | sh << 8)) * 16777619u;

    if (!opus_page_still(self, h)) {
        char name[24], *p = put_u(name, (unsigned)(key + 1));
        *p++ = ' ';
        p = put_s(p, p_sound_name(snd));
        *p = 0;
        opus_page_text(self, 0, 14, 6, 12, OPUS_COLOR_DIM, "1320");
        opus_page_text_right(self, 1, 306, 4, 16, OPUS_COLOR_TEXT, name);
        picture(par, snd);
        for (int k = 0; k < P_KEYS; k++) opus_page_dot(key_x(k), key_y(k), 2, 0, OPUS_COLOR_DIM);
        opus_page_dot(key_x(key), key_y(key), 5, 0, OPUS_COLOR_TEXT);
        values(self, par, snd, sh);
        opus_page_still_done(self);
    }

    /* what moves: a dot on every sounding key, a line along the current key's sound where it plays */
    for (int i = 0; i < VOICES; i++) {
        volatile p_ui_t *u = &p_ui[i];
        if (!u->on) continue;
        int k = u->key < P_KEYS ? u->key : 0;
        opus_page_dot(key_x(k), key_y(k), 3, 1, OPUS_COLOR_TEXT);
        if (k != key || u->snd != snd) continue;
        int32_t gone = u->rev ? (int32_t)u->from - u->at : (int32_t)u->at - u->from;
        int32_t l = u->rev ? u->from : p_sound_len(u->snd) - 1 - u->from;
        if (l < 1 || gone < 0) continue;
        if (gone > l) gone = l;
        int x = WX0 + 3 + (int)(gone * (WX1 - WX0 - 6) / l);
        opus_page_line(x, WY - WH - 2, x, WY + WH + 2, OPUS_COLOR_TEXT);
    }
}
