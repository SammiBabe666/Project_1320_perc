/* 1320: the plain loop's samples on the Blackfin (p_voice.c's run, FX 0, holds the same in C):
 *   x = fir16(window at pos, phase of pos) >> 14, kept within 16 bits; x = x * (gq >> 6) >> 15, kept within 16 bits;
 *   pos += step; gq += dg
 * for n samples (n > 0, pos >= 0 throughout). The addresses of a sample are worked out before its 16 multiply-
 * accumulates, the gain ramp beside them, so no load waits on a pointer just written. No hardware loop (the shipped
 * medieval filter used none either): a counted branch.
 * void p_run_plain(struct p_run *r): r = { y, wb, fir, pos, step, gq, dg, n }, wb the window less 7 + 64 b0 samples.
 * Copyright (C) 2026 Sammi (Project 1320 Perc). GPL-3.0-or-later (see LICENSE and NOTICE). */
	.text
	.align 4
	.global _p_run_plain
	.type _p_run_plain, STT_FUNC
_p_run_plain:
	[--SP] = (R7:4, P5:3);
	P5 = R0;
	P2 = [P5 + 0];			/* y */
	P3 = [P5 + 4];			/* wb */
	P4 = [P5 + 8];			/* fir: 64 phases of 16 taps, 32 bytes a phase */
	R4 = [P5 + 12];			/* pos, Q16 */
	R5 = [P5 + 16];			/* step */
	R6 = [P5 + 20];			/* gq: the gain << 6 */
	R7 = [P5 + 24];			/* dg */
	P0 = [P5 + 28];			/* n */
	R2 = 0x7E0 (Z);			/* the phase's byte offset: ((pos >> 10) & 63) << 5 */
1:	R0 = R4 >>> 15;
	BITCLR (R0, 0);			/* 2 * (pos >> 16) */
	R1 = R4 >>> 5;
	R1 = R1 & R2;
	P1 = R0;
	P5 = R1;
	R3 = R6 >>> 6;			/* this sample's gain */
	R4 = R4 + R5;
	R6 = R6 + R7;
	P1 = P1 + P3;
	P5 = P5 + P4;
	I0 = P1;
	I1 = P5;
	R0.L = W[I0++];
	R1.L = W[I1++];
	A0 = R0.L * R1.L (IS) || R0.L = W[I0++] || R1.L = W[I1++];
	.rept 14
	A0 += R0.L * R1.L (IS) || R0.L = W[I0++] || R1.L = W[I1++];
	.endr
	A0 += R0.L * R1.L (IS);
	R0 = A0 (IS);
	R0 >>>= 14;
	R0 = R0 << 16 (S);		/* within 16 bits */
	R0 >>>= 16;
	R0 = R0.L * R3.L (IS);
	R0 >>>= 15;
	R0 = R0 << 16 (S);
	R0 >>>= 16;
	W[P2++] = R0;
	P0 += -1;
	CC = P0 == 0;
	if !CC jump 1b (bp);
	(R7:4, P5:3) = [SP++];
	RTS;
	.size _p_run_plain, .-_p_run_plain
