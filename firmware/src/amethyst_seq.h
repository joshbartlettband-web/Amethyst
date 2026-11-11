/* SPDX-License-Identifier: GPL-3.0-only
 * Amethyst (joshbartlettband-web/Amethyst), built on Melodee. The part of Amethyst the audio interrupt runs, included
 * by seq.c: which track each key plays, and the break's chop. The rest is amethyst.c.
 *
 * While Amethyst is on, keyboard_block sends F3..E4 to the bass (track 1) and F4..G5 to the chords (track 2, whose
 * chord keys make each key a seventh of A minor); a black key holds a slice of the break (track 4) instead of a note.
 * am_seq_step runs as the break's sequencer moves a step: it keeps the bar where it would have been (ghost), plays the
 * held slice in its place, two 16ths over and over, and lands the drop on the first step of a bar. */
#define AM_TBASS 0u
#define AM_TCHORD 1u
#define AM_TBREAK 3u
#define AM_SLICES 11u
static struct {
    volatile uint8_t on;          /* the keys are Amethyst's */
    volatile uint8_t chop;        /* 1 + the slice a black key holds; 0 none */
    volatile uint8_t drop;        /* 1: the build was let go: the drop lands on the next beat's first step */
    volatile uint8_t dropped;     /* the drop landed (amethyst.c clears it) */
    volatile uint8_t building;    /* amethyst.c: FX is held, so bstep counts 16ths since it was pressed */
    uint8_t cn;                   /* the held slice's 16ths so far */
    uint16_t ghost;               /* the break's step without the chop */
    volatile uint16_t bstep;      /* 16ths since BUILD started (amethyst.c reads it for the loop length and meter) */
    volatile uint32_t kicks;      /* the break's kicks so far (the screen's pulse) */
} am_seq;
/* where each black key's slice starts, in 16ths of the break's two bars */
static const uint8_t AM_SLICE[AM_SLICES] = {0, 2, 4, 6, 8, 10, 12, 14, 16, 20, 24};
static void am_seq_step(track_t *t, uint32_t starting, uint32_t len);   /* amethyst.c */
