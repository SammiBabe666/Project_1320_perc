/* 1320: one drum voice (see p_voice.h).
 * The voice computes 64 samples at 44.1 kHz a block, then doubles them to the 128 at 88.2 kHz the Opus drum engine
 * service asks for. The bank's sounds are stored at 11.025 to 22.05 kHz, so nothing is lost below 22 kHz, and the
 * costly part (the 16-tap interpolation) runs half as often as at 88.2 kHz.
 * Per sample: the 16-tap interpolation (medieval v0.2.3's filter), then one gain: the decay envelope, the key's level
 * and the velocity folded into a ramp per block. Drive, dusk, age, a late start's fade-in, a choke and a retrigger's
 * declick go through a second, fuller loop, chosen per block, so a plain hit pays for none of them.
 * The bank is IMA ADPCM in independent 64-sample blocks; a voice decodes the block its filter reads (and the first 16
 * samples of the next) into one window, once per 64 source samples.
 * Click-free by design: every sound in the bank ends faded to zero, a late start fades in over 64 samples, a hit on a
 * sounding voice fades its last output out over 64 samples, a choke fades out over 64, the decay ramps within each
 * block. No division in the audio path.
 * Copyright (C) 2026 Sammi (Project 1320 Perc). GPL-3.0-or-later (see LICENSE and NOTICE). */
#include "p_voice.h"
#include "md_bank.h"
#include "md_names.h"
#include "p_tables.h"

static const p_tab_t tab = { P_FIR_INIT, P_IMA_DIFF_INIT, P_IMA_NEXT_INIT };
const p_tab_t *p_tab_now = &tab;
const p_tab_t *p_tab_base(void) { return &tab; }

#define LEVEL_FULL  22000       /* Q15 gain at level 32767: the default 26000 gives 17456, medieval's -5.5 dB */
#define ENV_STOP    (8 << 15)   /* Q30 envelope below which a decaying voice stops */
#define RATE_MAX    (2 << 16)   /* at most 2 source samples per output sample: a 22 kHz sound up to +2 octaves,
                                   * an 11 kHz one up to +3 (higher only aliases) */

static inline int clamp15(int x) { return x < 0 ? 0 : x > 32767 ? 32767 : x; }
static inline int16_t sat16(int32_t x) { return (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : x); }

/* the first n samples (1..64) of a 36-byte block: int16 first sample, uint8 step index, pad, 64 nibbles (the
 * 64th nibble gives the next block's first sample, which that block's header holds exactly). The step's shifts and
 * adds and the index's update come from two tables (p_tables.h), bit for bit the IMA decoder medieval's encoder
 * matched. */
#define NIBBLE(nib) do {                                                                    \
        int32_t j_ = idx8 + ((nib) & 7), d_ = t_->diff[j_], sg_ = -(((nib) >> 3) & 1);     \
        pred += (d_ ^ sg_) - sg_;                                                           \
        pred = pred > 32767 ? 32767 : (pred < -32768 ? -32768 : pred);                      \
        idx8 = t_->next[j_];                                                                 \
        *o++ = (int16_t)pred;                                                               \
    } while (0)
static void decode(int16_t *out, const uint8_t *b, int n)
{
    const p_tab_t *t_ = p_tab_now;
    int32_t pred = (int16_t)(b[0] | (b[1] << 8)), idx8 = (b[2] > 88 ? 88 : b[2]) * 8;
    int16_t *o = out;
    const uint8_t *q = b + 4;
    *o++ = (int16_t)pred;
    for (int k = 0; k < (n - 1) >> 1; k++) {
        int32_t by = q[k];
        NIBBLE(by & 15);
        NIBBLE(by >> 4);
    }
    if ((n - 1) & 1) NIBBLE(q[(n - 1) >> 1] & 15);
}

/* block b of the sound (silence outside it) into win[0..63], the next block's first 16 into win[64..79] */
static void fill_window(int16_t *win, const uint8_t *base, int32_t b, int32_t nblk)
{
    if (b >= 0 && b < nblk) decode(win, base + b * MD_BLOCK_BYTES, 64);
    else for (int k = 0; k < 64; k++) win[k] = 0;
    if (b + 1 >= 0 && b + 1 < nblk) decode(win + 64, base + (b + 1) * MD_BLOCK_BYTES, 16);
    else for (int k = 0; k < 16; k++) win[64 + k] = 0;
}

/* 16 source samples by 16 taps. The taps of a phase sum to 2^14, so the sum stays within 2^30. On the Blackfin one
 * multiply-accumulate a tap with both loads in the same instruction (medieval v0.2.3's). */
static inline int32_t fir16(const int16_t *x, const int16_t *h)
{
#ifdef __bfin__
    int32_t r, a, b;
    __asm__ volatile(
        "%1.L = W[%3++];\n\t"
        "%2.L = W[%4++];\n\t"
        "A0 = %1.L * %2.L (IS) || %1.L = W[%3++] || %2.L = W[%4++];\n\t"
        ".rept 14\n\t"
        "A0 += %1.L * %2.L (IS) || %1.L = W[%3++] || %2.L = W[%4++];\n\t"
        ".endr\n\t"
        "A0 += %1.L * %2.L (IS);\n\t"
        "%0 = A0 (IS);"
        : "=d"(r), "=&d"(a), "=&d"(b), "+b"(x), "+b"(h)
        :
        : "A0", "memory");
    return r;
#else
    int32_t acc = 0;
    for (int k = 0; k < 16; k++) acc += x[k] * h[k];
    return acc;
#endif
}

int p_sound_of(int number) { return (clamp15(number) * P_NSOUNDS) >> 15; }
short p_number_of_sound(int snd)
{
    snd = snd < 0 ? 0 : snd >= P_NSOUNDS ? P_NSOUNDS - 1 : snd;
    return (short)(((snd * 2 + 1) * 32768) / (2 * P_NSOUNDS));
}

/* the tambourines (hat, short, cap, tight, long, shakes) choke each other as hi-hats do; the tambo snare does not */
int p_choke_group(int snd)
{
    if ((unsigned)snd >= P_NSOUNDS) return 0;
    const char *n = md_name[snd];
    return n[0] == 't' && n[1] == 'a' && n[2] == 'm' && n[3] == 'b' && n[4] == 'o' && !(n[6] == 's' && n[7] == 'n');
}

int32_t p_ratio(int tune, uint32_t host)
{
    /* tune is semitones x 1024; q its octaves x 4096 (tune / 3, rounded, by a multiply: exact here), the table read between its 256 steps */
    int32_t q = tune >= 0 ? ((tune + 1) * 43691) >> 17 : -(((1 - tune) * 43691) >> 17), o = q >> 12, fr = q & 4095, k = fr >> 4;
    int32_t r = (int32_t)p_exp2[k] + ((((int32_t)p_exp2[k + 1] - (int32_t)p_exp2[k]) * (fr & 15)) >> 4);
    r = o >= 0 ? r << (o > 4 ? 4 : o) : r >> (-o > 15 ? 15 : -o);
    if (r > (4 << 16)) r = 4 << 16;
    if (host == 0) host = 65536;
    if (host > (4u << 16)) host = 4u << 16;
    int32_t t = ((r >> 3) * (int32_t)(host >> 3)) >> 10;
    return t < 1 ? 1 : t;
}

void p_voice_init(p_voice_t *v, int index)
{
    uint8_t *p = (uint8_t *)v;
    for (unsigned i = 0; i < sizeof *v; i++) p[i] = 0;
    v->wblk = -2;
    v->index = (uint8_t)index;
}

void p_voice_hit(p_voice_t *v, const short *par, int key, int velocity)
{
    /* The audio may render this voice while it is filled (a hit on a sounding voice): pending makes that render
     * silent and keeps finished from freeing the voice. The old sound's last sample fades out from the next render. */
    *(volatile uint8_t *)&v->pending = 1;
    int16_t prev = v->state != P_OFF ? v->last : 0;
    *(volatile uint8_t *)&v->state = P_OFF;
    int snd = p_sound_of(par[P_SOUND]);
    const md_sound_t *s = &md_sound[snd];
    int32_t last = s->len - 1;
    int st = clamp15(par[P_START]);
    int32_t off = (last * ((st & 16383) >> 4) * 3) >> 12;          /* 0 .. 3/4 of the sound */
    v->declick = prev;
    if (!prev) v->last = 0;
    v->snd = (uint8_t)snd;
    v->rev = st >= 16384;
    v->key = (uint8_t)key;
    v->vel = (uint8_t)(velocity < 1 ? 1 : velocity > 127 ? 127 : velocity);
    v->pos = (v->rev ? last - 1 - off : off) << 16;
    if (v->pos < 0) v->pos = 0;
    v->fade_in = off > 0;
    v->env = 32767 << 15;                                       /* Q30, so its Q15 ramp fits 16 bits */
    v->wblk = -2;
    v->lp = 0; v->hold = 0; v->hold_n = 0; v->fade_n = 0; v->age = 0;
    *(volatile uint8_t *)&v->state = P_PLAY;
    *(volatile uint8_t *)&v->pending = 0;
}

void p_voice_choke(p_voice_t *v)
{
    if (v->state == P_PLAY) { v->fade_n = 64; v->state = P_FADE; }
}

int p_voice_finished(const p_voice_t *v) { return v->state == P_OFF && !v->pending && v->declick == 0; }

#ifdef __bfin__
struct p_run { int16_t *y; const int16_t *wb; const int16_t (*fir)[16]; int32_t pos, step, gq, dg, n; };
void p_run_plain(struct p_run *r);
#endif

/* 64 samples at 44.1 kHz into y: the 16-tap interpolation, kept within 16 bits, times a gain ramp (gq = g0 << 6, dg
 * added each sample: g0 .. g0 + dg over 64 samples). The samples go in runs that read one window: the window's bounds
 * and the sound's end are worked out once a run (one division), so the per-sample work is the filter, the gain and the
 * store (on the Blackfin p_run.S, the same samples scheduled by hand). Returns the samples written before the sound
 * ended (64: it goes on). */
static int run(p_voice_t *v, int16_t *y, const uint8_t *base, int32_t last, int32_t nblk, int32_t rate,
               int32_t g0, int32_t dg)
{
    const int16_t (*fir)[16] = p_tab_now->fir;
    const int16_t *win = v->win;
    int32_t pos = v->pos, cur = v->wblk, gq = g0 * 64;
    const int32_t step = v->rev ? -rate : rate;
    int i = 0;
    while (i < P_HALF) {
        if (pos < 0 || (pos >> 16) >= last) break;
        int32_t idx = pos >> 16, lo = idx - 7, b0 = lo < 0 ? -1 : lo >> 6, n;
        if (b0 != cur) { fill_window(v->win, base, b0, nblk); cur = b0; }
        if (step > 0) {                                     /* until the filter leaves the window or the sound ends */
            int32_t end = (b0 + 1) * 64 + 7;
            if (end > last) end = last;
            n = ((end << 16) - pos + step - 1) / step;
        } else {                                            /* reversed: until it leaves the window or passes 0 */
            int32_t beg = b0 < 0 ? 0 : b0 * 64 + 7;
            n = (pos - (beg << 16)) / rate + 1;
        }
        if (n > P_HALF - i) n = P_HALF - i;
        const int32_t sub = 7 + b0 * 64;
#ifdef __bfin__
        struct p_run r = { y + i, (const int16_t *)(uintptr_t)((uintptr_t)win - (uintptr_t)(sub * 2)), fir, pos, step,
                           gq, dg, n };
        p_run_plain(&r);
        pos += step * n; gq += dg * n; i += n;
#else
        for (int k = 0; k < n; k++, i++) {
            int32_t x = fir16(win + ((pos >> 16) - sub), fir[(pos >> 10) & 63]) >> 14;
            x = x > 32767 ? 32767 : x < -32768 ? -32768 : x;
            y[i] = sat16((x * (gq >> 6)) >> 15);
            pos += step;
            gq += dg;
        }
#endif
    }
    v->pos = pos; v->wblk = cur;
    return i;
}

/* the effects' settings for a block */
typedef struct { int32_t G, drive_g, dusk_a, age_len, age_mask, dc; } fx_t;

/* the effects on n decayed samples, in order: drive, dusk, age, a late start's fade-in, the gain, a choke's fade, a
 * retrigger's declick. Each its own short loop, run only when it is on. Returns the samples left (a choke ends). */
static int effects(p_voice_t *v, int16_t *y, int n, const fx_t *f)
{
    int32_t t[P_HALF];
    for (int i = 0; i < n; i++) t[i] = y[i];
    if (f->drive_g)                                         /* drive: gain into a cubic soft clip */
        for (int i = 0; i < n; i++) {
            int32_t x = (t[i] * f->drive_g) >> 12;
            x = x > 32767 ? 32767 : (x < -32767 ? -32767 : x);
            int32_t x2 = (x * x) >> 15;
            t[i] = ((3 * x) >> 1) - (((x * x2) >> 15) >> 1);
        }
    if (f->dusk_a < 32767) {                                /* dusk: one-pole low pass */
        int32_t lp = v->lp, a = f->dusk_a;
        for (int i = 0; i < n; i++) { lp += ((t[i] - lp) * a) >> 15; t[i] = lp; }
        v->lp = (int16_t)lp;
    }
    if (f->age_len) {                                       /* age: hold, fewer bits */
        int32_t hold = v->hold, hn = v->hold_n;
        for (int i = 0; i < n; i++) {
            if (hn == 0) { hold = t[i] & f->age_mask; hn = f->age_len; }
            hn--;
            t[i] = hold;
        }
        v->hold = (int16_t)hold; v->hold_n = (uint8_t)hn;
    }
    if (v->fade_in && v->age < 64)                          /* a late start fades in */
        for (int i = 0; i < n && v->age + i < 64; i++) t[i] = (t[i] * (v->age + i)) >> 6;
    v->age = (uint16_t)(v->age + n > 0xFFFF ? 0xFFFF : v->age + n);
    for (int i = 0; i < n; i++) t[i] = (t[i] * f->G) >> 15;
    if (v->state == P_FADE)                                 /* choked: out over 64 samples */
        for (int i = 0; i < n; i++) {
            if (!v->fade_n) { n = i; break; }
            t[i] = (t[i] * v->fade_n) >> 6; v->fade_n--;
        }
    if (f->dc) for (int i = 0; i < n; i++) t[i] += (f->dc * (64 - i)) >> 6;
    for (int i = 0; i < n; i++) y[i] = sat16(t[i]);
    return n;
}

int p_voice_render(p_voice_t *v, int16_t *out, const short *par, uint32_t host)
{
    int16_t y[P_HALF];
    int n = 0, sounding = 1;
    int32_t dc = v->declick;
    if (v->pending) {                                               /* being filled by a hit: silence this block */
        for (int i = 0; i < P_OUT; i++) out[i] = 0;
        return 1;
    }
    if (v->state != P_OFF) {
        const md_sound_t *s = &md_sound[v->snd];
        const int32_t last = s->len - 1, nblk = (last + MD_BLOCK - 1) / MD_BLOCK;
        int32_t rate = (s->inc * (p_ratio(par[P_TUNE], host) >> 4)) >> 12;
        rate = rate < 1 ? 1 : rate > RATE_MAX ? RATE_MAX : rate;
        int di = clamp15(par[P_DECAY]) >> 9;
        int32_t e0 = v->env, e1 = di == 63 ? e0 : (e0 >> 15) * p_decay[di];
        int drive = clamp15(par[P_DRIVE]), dusk = clamp15(par[P_DUSK]), age = clamp15(par[P_AGE]);
        int32_t vg = v->vel * v->vel * 2;                           /* Q15, velocity squared */
        int32_t comp = 32767 - ((drive * 13000) >> 15);             /* the clip's level compensation */
        int32_t G = (((clamp15(par[P_LEVEL]) * LEVEL_FULL) >> 15) * vg) >> 15;
        G = (G * comp) >> 15;
        const uint8_t *base = md_adpcm + s->block * MD_BLOCK_BYTES;
        int plain = !drive && !dusk && !age && !dc && v->state == P_PLAY && !(v->fade_in && v->age < 64);
        if (plain) {                                                /* the gain folded into the decay's ramp */
            int32_t g0 = ((e0 >> 15) * G) >> 15, g1 = ((e1 >> 15) * G) >> 15;
            n = run(v, y, base, last, nblk, rate, g0, g1 - g0);
            v->age = (uint16_t)(v->age + n > 0xFFFF ? 0xFFFF : v->age + n);
        } else {
            fx_t f;
            f.G = G; f.dc = dc;
            f.drive_g = drive ? 4096 + ((drive * 7) >> 3) : 0;     /* Q12 gain 1 .. 8 */
            f.dusk_a = 32767 - ((dusk * 31000) >> 15);
            f.age_len = age ? 1 + ((age * 7) >> 15) : 0;
            f.age_mask = ~((1 << (age >> 12)) - 1);
            n = run(v, y, base, last, nblk, rate, e0 >> 15, (e1 - e0) >> 15);
            n = effects(v, y, n, &f);
        }
        v->env = e1;
        if (n < P_HALF || e1 < ENV_STOP || (v->state == P_FADE && !v->fade_n)) { v->state = P_OFF; sounding = 0; }
    } else sounding = 0;
    for (int i = n; i < P_HALF; i++)                                /* ended (or never sounding): the declick alone */
        y[i] = (int16_t)(dc ? (dc * (64 - i)) >> 6 : 0);
    v->declick = 0;
    /* 2x up: each 44.1 kHz sample, and the midpoint before it */
    int32_t prev = v->last;
    for (int i = 0; i < P_HALF; i++) {
        int32_t x = y[i];
        out[2 * i] = (int16_t)((prev + x) >> 1);
        out[2 * i + 1] = (int16_t)x;
        prev = x;
    }
    v->last = (int16_t)prev;
    return sounding;
}

int32_t p_decay_ms(int number) { int di = clamp15(number) >> 9; return di == 63 ? 0 : p_t60_ms[di]; }
int32_t p_sound_len(int snd) { return (unsigned)snd < P_NSOUNDS ? md_sound[snd].len : 0; }
int32_t p_sound_ms(int snd)
{
    if ((unsigned)snd >= P_NSOUNDS) return 0;
    return (int32_t)(((uint32_t)md_sound[snd].len * 1000u) / ((44100u * md_sound[snd].inc) >> 16));
}
const char *p_sound_name(int snd) { return md_name[(unsigned)snd < P_NSOUNDS ? snd : 0]; }

void p_sound_peaks(int snd, uint8_t *peaks, int n)
{
    int16_t buf[64];
    const md_sound_t *s = &md_sound[(unsigned)snd < P_NSOUNDS ? snd : 0];
    const uint8_t *base = md_adpcm + s->block * MD_BLOCK_BYTES;
    int32_t len = s->len, nblk = (len + 63) / 64;
    for (int k = 0; k < n; k++) peaks[k] = 0;
    for (int32_t b = 0; b < nblk; b++) {
        decode(buf, base + b * MD_BLOCK_BYTES, 64);
        for (int i = 0; i < 64 && b * 64 + i < len; i++) {
            int32_t a = buf[i] < 0 ? -buf[i] : buf[i];
            int k = (int)(((b * 64 + i) * n) / len);
            int32_t p = a >> 7;
            if (p > 255) p = 255;
            if (p > peaks[k]) peaks[k] = (uint8_t)p;
        }
    }
}
