/* Bare-metal start for the cycle bench in the GDB Blackfin simulator: enable CYCLES, run main, exit through
 * the libgloss syscall (P0 = 1, R0 -> args). sys_write is syscall 5.
 * Copyright (C) 2026 Sammi (Project 1320 Perc; from Project Medieval and Project ERD). GPL-3.0-or-later. */
	.section .text.start
	.global start
start:
	SP.H = 0xFFB0; SP.L = 0x0FF0;
	FP = 0;
	R0 = SYSCFG;
	BITSET (R0, 1);
	SYSCFG = R0;
	CSYNC;
	call _main;
	SP += -16;
	[SP] = R0;
	R0 = SP;
	P0 = 1;
	EXCPT 0;
1:	jump 1b;
	.global _sys_write
_sys_write:		/* R0 = fd, R1 = buf, R2 = len */
	SP += -16;
	[SP] = R0; [SP + 4] = R1; [SP + 8] = R2;
	R0 = SP;
	P0 = 5;
	EXCPT 0;
	SP += 16;
	RTS;
