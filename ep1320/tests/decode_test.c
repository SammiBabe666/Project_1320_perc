/* The table decoder (p_voice.c) against the IMA ADPCM decoder written out with its shifts and adds, on every block of
 * the bank: the same samples, so the bank plays as medieval's encoder made it.
 * Copyright (C) 2026 Sammi (Project 1320 Perc). GPL-3.0-or-later. */
#include <stdio.h>
#include "../p_voice.c"
static const int16_t STEP[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107,
    118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
    1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894,
    6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767 };
static const int8_t INDEX[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };
int main(void)
{
    int nb = (int)(sizeof md_adpcm / MD_BLOCK_BYTES), bad = 0;
    int16_t got[64];
    for (int b = 0; b < nb; b++) {
        const uint8_t *p = md_adpcm + b * MD_BLOCK_BYTES;
        decode(got, p, 64);
        int32_t pred = (int16_t)(p[0] | (p[1] << 8)), idx = p[2];
        if (got[0] != pred) bad++;
        for (int i = 0; i < 63; i++) {
            int n = (p[4 + (i >> 1)] >> ((i & 1) << 2)) & 15, st = STEP[idx], d = st >> 3;
            if (n & 1) d += st >> 2;
            if (n & 2) d += st >> 1;
            if (n & 4) d += st;
            pred = n & 8 ? pred - d : pred + d;
            pred = pred > 32767 ? 32767 : pred < -32768 ? -32768 : pred;
            idx += INDEX[n & 7];
            idx = idx < 0 ? 0 : idx > 88 ? 88 : idx;
            if (got[i + 1] != pred) { bad++; break; }
        }
    }
    printf("decoder: %d blocks of the bank, %d differ from the reference decoder\n", nb, bad);
    return bad != 0;
}
