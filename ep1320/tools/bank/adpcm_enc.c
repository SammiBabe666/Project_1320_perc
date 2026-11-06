/* IMA ADPCM block encoder with search, for tools/build_bank.py: the same 36-byte blocks md_voice.c decodes, encoded
 * for the least error instead of greedily.
 *   cc -O2 -o adpcm_enc adpcm_enc.c && ./adpcm_enc < in.s16 > out.bin     (16-bit little-endian mono in)
 * Per block of 64 samples (+1, the next block's first sample is shared): every start step index 0..88 is tried, and
 * for each a beam search keeps the BEAM decoder states with the least squared error so far, over all 16 codes per
 * sample. The best path is written. The decoder is unchanged: any IMA decoder plays these blocks.
 * Copyright (C) 2026 Sammi (Project Medieval). GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE and
 * NOTICE). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLOCK 64
#define BEAM  48

static const int16_t step_t[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107,
    118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
    1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894,
    6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767 };
static const int8_t index_t[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };

typedef struct { int32_t pred, idx; double err; uint8_t code[BLOCK]; } state;

static void step(int32_t *pred, int32_t *idx, int n)
{
    int32_t st = step_t[*idx], d = st >> 3;
    if (n & 1) d += st >> 2;
    if (n & 2) d += st >> 1;
    if (n & 4) d += st;
    *pred = n & 8 ? *pred - d : *pred + d;
    *pred = *pred > 32767 ? 32767 : *pred < -32768 ? -32768 : *pred;
    *idx += index_t[n & 7];
    *idx = *idx < 0 ? 0 : *idx > 88 ? 88 : *idx;
}

static int cmp(const void *a, const void *b)
{
    double x = ((const state *)a)->err, y = ((const state *)b)->err;
    return x < y ? -1 : x > y;
}

static state cur[BEAM], cand[BEAM * 16];

/* the best encoding of seg[1..64] from pred seg[0] and start index idx0; returns its error */
static double search(const int16_t *seg, int idx0, state *best)
{
    int n = 1;
    cur[0].pred = seg[0]; cur[0].idx = idx0; cur[0].err = 0;
    for (int i = 0; i < BLOCK; i++) {
        int m = 0;
        for (int s = 0; s < n; s++)
            for (int c = 0; c < 16; c++) {
                state *t = &cand[m++];
                t->pred = cur[s].pred; t->idx = cur[s].idx;
                step(&t->pred, &t->idx, c);
                double e = (double)(t->pred - seg[i + 1]);
                t->err = cur[s].err + e * e;
                memcpy(t->code, cur[s].code, (size_t)i);
                t->code[i] = (uint8_t)c;
            }
        qsort(cand, (size_t)m, sizeof(state), cmp);
        n = 0;                                                /* keep the best, one per decoder state */
        for (int k = 0; k < m && n < BEAM; k++) {
            int dup = 0;
            for (int j = 0; j < n; j++) if (cur[j].pred == cand[k].pred && cur[j].idx == cand[k].idx) { dup = 1; break; }
            if (!dup) cur[n++] = cand[k];
        }
    }
    *best = cur[0];
    return cur[0].err;
}

int main(void)
{
    size_t cap = 1 << 20, len = 0;
    int16_t *x = malloc(cap * 2);
    for (size_t r; (r = fread(x + len, 2, cap - len, stdin)) > 0; ) {
        len += r;
        if (len == cap) x = realloc(x, (cap *= 2) * 2);
    }
    size_t nb = len > 1 ? (len - 1 + BLOCK - 1) / BLOCK : 1;
    int16_t *y = calloc(nb * BLOCK + 1, 2);
    memcpy(y, x, len * 2);
    double total = 0;
    for (size_t b = 0; b < nb; b++) {
        const int16_t *seg = y + b * BLOCK;
        state best, t;
        double be = -1;
        int bidx = 0;
        for (int idx0 = 0; idx0 < 89; idx0 += 2) {           /* (every other index: neighbours differ by ~10 %) */
            double e = search(seg, idx0, &t);
            if (be < 0 || e < be) { be = e; best = t; bidx = idx0; }
        }
        total += be;
        uint8_t hdr[4] = { (uint8_t)(seg[0] & 255), (uint8_t)((uint16_t)seg[0] >> 8), (uint8_t)bidx, 0 }, nib[32];
        for (int i = 0; i < BLOCK; i += 2) nib[i / 2] = (uint8_t)(best.code[i] | best.code[i + 1] << 4);
        fwrite(hdr, 1, 4, stdout); fwrite(nib, 1, 32, stdout);
    }
    (void)total;
    free(x); free(y);
    return 0;
}
