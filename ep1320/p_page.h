/* 1320: the drum's T1 page (p_page.c).
 * Copyright (C) 2026 Sammi (Project 1320 Perc). GPL-3.0-or-later (see LICENSE and NOTICE). */
#ifndef P_PAGE_H
#define P_PAGE_H

#define P_PEAKS 48              /* parts of a sound in the page's picture of it */

/* a voice as the page sees it: single bytes and halfwords, written by the hits and the audio */
typedef struct {
    unsigned char on, key, rev, snd;   /* on: 1 playing, 2 fading out (choked), 0 silent */
    unsigned short from, at;    /* where the hit started in its sound, where it plays now (its own samples) */
} p_ui_t;

struct opl_self;
void p_page_init(struct opl_self *self);
void p_page_shift(int down);
void p_page_draw(struct opl_self *self);
#endif
