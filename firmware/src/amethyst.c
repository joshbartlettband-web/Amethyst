/* SPDX-License-Identifier: GPL-3.0-only
 * Amethyst (joshbartlettband-web/Amethyst): drum and bass for the FM-1, by Josh, with the concept and the look by
 * kipatchi. Built on Melodee by Kerem Kilic (Ellic Studio), a modified version of Felucca by Leo Kuroshita, Hügelton
 * Instruments; the way it sits on Melodee follows Double Rainbow's dr.c. Amethyst's own code is this file and
 * amethyst_seq.h (the audio interrupt's part), so Melodee's updates merge easily. The plan: docs/AMETHYST.md.
 *
 * The first stage, FRIDAY: The End on West Central Street at 3 AM, ten years on, moss in the walls. A break runs on
 * track 4 (the DRUM engine's 909 kit, two bars of 16ths); the low white keys play a Reese on track 1 and the high
 * ones sevenths of A minor on track 2 (both Prophet programs of our own, below); the black keys hold slices of the
 * break. FX held builds (SLOOP's loops, 1/8 then 1/16 then 1/32, the bass out), and the drop lands on the next beat;
 * after a drop built for a bar or more the crowd calls for a rewind, and SCL answers it. Three rewinds cut the
 * stage's VIP. The moss climbs as the night goes on and glows on every kick.
 *   Held: ENV half speed, LFO the tape stopping, EDIT the break out, GLO a dub siren on the high keys.
 *   Taps: SEQ the break out or in, ARP which keys play what, HOME the sleeve notes, OCT the bass's octave.
 *   main.c: am_boot() after project_boot(); each pass am_frame() instead of Melodee's UI while it returns 1.
 *   HOME and SAVE held 3 s together: full Melodee, until the next power-on. */

#define AM_POOL __attribute__((section(".pool")))     /* Amethyst's RAM: the pool (.bss is Melodee's and nearly full) */
#define AM_HOLD_MS 3000u                               /* HOME + SAVE: to Melodee */
#define AM_BTN(b) (1u << panel.btn[b])
/* SLOOP's effects replay the last 0.37 s of the mix from a ring, and Melodee keeps that ring only while a punch key
 * is held (punch.keybit). Amethyst holds a key that is no key for as long as it is on, so the ring is always
 * recording and BUILD, REWIND, HALF and STOP have something to play. Without it they are silent. */
#define AM_PKEY (1u << 31)
#include "am_art.h"                                    /* AM_END_*, AM_MILLS_*, AM_MOSS_* (tools/amethyst/gen_am_art.py) */

static void project_boot(void);                        /* project.c (included after this file) */

static struct {
    uint8_t on, building, call, vip, half, stopping, rewinds, plate;
    uint8_t stage;                                     /* AM_STAGES[]: PRESETS */
    uint8_t pat, pat_now;                              /* the break ALGORITHM chose, and the one loaded */
    uint8_t brk_out, cut, notes, home_combo;           /* SEQ's latch, EDIT held, the sleeve notes, HOME went to the combo */
    uint8_t knob[4];                                   /* WOBBLE FOG GRIT MOSS, 0..100 */
    int8_t oct;                                        /* the bass's octave, -2..+1 */
    uint8_t knob_show;                                 /* 1 + the knob just turned, for its bar (0: none) */
    uint32_t combo_t0, build_t0, build_steps, call_until, rwd_t0, msg_t0, draw_ms, kick_ms, drop_t0, plate_t0;
    uint32_t kicks, bar0, held, knob_show_t0;          /* held: the buttons last pass (their releases) */
    uint16_t moss;                                     /* how far the moss has reached, 0..280 (amethyst reach) */
    char msg[32];
} am AM_POOL;
/* one band of each picture at a time, unpacked as it is drawn (the pool's room is the audio arena's: 49 KB of it must
 * stay free, so the pictures are never unpacked whole) */
static uint8_t am_pic[AM_BAND * 120u] AM_POOL;          /* 16 rows of the stage's photo, 4 bits a pixel */
static uint8_t am_moss[AM_BAND * 60u] AM_POOL;          /* 16 rows of the moss, 120 px wide (0: see-through) */

/* ------------------------------------------------------------ the sounds --- */
/* our own Prophet programs (Sequential's are not Amethyst's): raw fields over p5_patch_init's defaults */
static void am_p5(p5_patch_t *p, const uint8_t (*f)[2], uint32_t n, const char *name)
{
    uint32_t i, l = str_len(name);
    p5_patch_init(p);
    p->raw[P5_LEVEL_A] = 0;
    p->raw[P5_CUTOFF] = 0;
    for (i = 0; i < n; i++)
        p->raw[f[i][0]] = f[i][1];
    memset(p->raw + P5_NAME, ' ', P5_NAME_LEN);
    memcpy(p->raw + P5_NAME, name, l < P5_NAME_LEN ? l : P5_NAME_LEN);
}
/* REESE: two saws a few cents apart beating slowly (Kevin Saunderson's bass, 1988), a dark filter that opens a little
 * on each note, glide between held notes */
static const uint8_t AM_REESE[][2] = {
    {P5_FREQ_A, 24}, {P5_FREQ_B, 24}, {P5_FINE_B, 9}, {P5_SAW_A, 1}, {P5_SAW_B, 1}, {P5_KEY_B, 1},
    {P5_LEVEL_A, 124}, {P5_LEVEL_B, 124}, {P5_CUTOFF, 44}, {P5_RESONANCE, 30}, {P5_KEY_FILTER, 1}, {P5_FILTER_REV, 0},
    {P5_ENV_FILTER, 26}, {P5_DECAY_FILTER, 80}, {P5_SUSTAIN_FILTER, 60}, {P5_RELEASE_FILTER, 30},
    {P5_DECAY_AMP, 90}, {P5_SUSTAIN_AMP, 127}, {P5_RELEASE_AMP, 28}, {P5_RELEASE_ON, 1}, {P5_GLIDE, 26},
    {P5_LFO_RATE, 30}, {P5_LFO_TRI, 1}, {P5_WHEEL_FILTER, 1}, {P5_VINTAGE, 30}, {P5_BEND, 2},
    /* P5_LFO_RATE is poked live by WOBBLE (am_knobs); P5_WHEEL_FILTER lets the LFO reach the filter, its depth the
     * track's mod wheel (trk[AM_TBASS].mw, also WOBBLE), the way a player's hand would push a real wheel */
};
/* FOG PAD: saw and saw-pulse apart, a slow swell, a long tail: the chords under the break */
static const uint8_t AM_PAD[][2] = {
    {P5_FREQ_A, 24}, {P5_FREQ_B, 24}, {P5_FINE_B, 14}, {P5_SAW_A, 1}, {P5_SAW_B, 1}, {P5_PULSE_B, 1}, {P5_PW_B, 80},
    {P5_KEY_B, 1}, {P5_LEVEL_A, 110}, {P5_LEVEL_B, 96}, {P5_NOISE, 4}, {P5_CUTOFF, 62}, {P5_RESONANCE, 18},
    {P5_KEY_FILTER, 1}, {P5_FILTER_REV, 1}, {P5_ENV_FILTER, 34}, {P5_ATTACK_FILTER, 50}, {P5_DECAY_FILTER, 90},
    {P5_SUSTAIN_FILTER, 70}, {P5_RELEASE_FILTER, 90}, {P5_ATTACK_AMP, 26}, {P5_DECAY_AMP, 100},
    {P5_SUSTAIN_AMP, 118}, {P5_RELEASE_AMP, 92}, {P5_RELEASE_ON, 1}, {P5_LFO_RATE, 60}, {P5_LFO_TRI, 1},
    {P5_VINTAGE, 40}, {P5_BEND, 2},
};

/* SIREN: one pulse wave, its pitch swept up and down by the LFO (the wheel's depth is set in am_setup), the dub
 * siren a sound system keeps beside the decks */
static const uint8_t AM_SIREN[][2] = {
    {P5_FREQ_A, 36}, {P5_PULSE_A, 1}, {P5_PW_A, 64}, {P5_LEVEL_A, 110}, {P5_CUTOFF, 104}, {P5_RESONANCE, 40},
    {P5_KEY_FILTER, 1}, {P5_SUSTAIN_FILTER, 127}, {P5_SUSTAIN_AMP, 127}, {P5_RELEASE_AMP, 60}, {P5_RELEASE_ON, 1},
    {P5_LFO_RATE, 58}, {P5_LFO_TRI, 1}, {P5_WHEEL_FREQ_A, 1}, {P5_BEND, 2},
};

/* The breaks: two bars of 16ths each, our own patterns, a detent of ALGORITHM apiece. Lanes: k the kick, s the snare
 * (x accented, o plain, - a rim ghost), h the closed hat (O: open), g ghosts on the rim. ROLLER is the 2000s roller
 * FRIDAY starts on; CHOPPED leans on the Amen's shape (two kicks, the snare late and doubled); HALF TIME puts the
 * kick on 1 and the snare on 3; TWO-STEP is the plain kick, snare, kick, snare. The VIP is the roller with the kick
 * doubled and the snare busier; the other three get a hat on every 16th. */
typedef struct { const char *name, *k, *s, *h, *g; } am_break_t;
#define AM_NBREAK 4u
static const am_break_t AM_BREAKS[AM_NBREAK + 1u] = {
    {"ROLLER", "x.........x.......x.......x.....", "....x.......x.......x.....o.x...",
     "..x...x...x...O...x...x...x...O.", "...-.....-...-.....-.....-...-.."},
    {"CHOPPED", "x.x.......xx....x.x.......x.....", "....x..o.o..x..o....x..o.o....x.",
     "x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.O.", "................................"},
    {"HALF TIME", "x...............x.....x.........", "........x...............x.......",
     "x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.O.", "......-.......-.......-......-.."},
    {"TWO-STEP", "x.........x.....x.........x.....", "....x.......x.......x.......x...",
     "x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.", "................................"},
    {"ROLLER VIP", "x.........x..x....x.......x..x..", "....x...-...x.......x...-...x..x",
     "..x.x.x...x.x.O...x.x.x...x.x.O.", "...-.....-...-.....-.....-...-.."},
};
static void am_break_load(void)                         /* the break ALGORITHM is on (the roller's VIP once it is cut) */
{
    const am_break_t *b = &AM_BREAKS[am.vip && !am.pat ? AM_NBREAK : am.pat];
    track_t *t = &trk[AM_TBREAK];
    uint32_t i;
    track_defaults_steps(t);
    for (i = 0; i < 32u; i++) {
        if (b->k[i] == 'x') { grid_hit(t, i, 0, 1); grid_acc(t, i, 0, 1); }
        if (b->s[i] == 'x') { grid_hit(t, i, 1, 1); grid_acc(t, i, 1, 1); }
        if (b->s[i] == 'o') grid_hit(t, i, 1, 1);
        if (b->s[i] == '-' || b->g[i] == '-') grid_hit(t, i, 6, 1);
        if (b->h[i] == 'x') grid_hit(t, i, 3, 1);
        if (b->h[i] == 'O') grid_hit(t, i, 4, 1);
    }
    if (am.vip && am.pat)                               /* the other breaks' VIP: a hat on every 16th without one */
        for (i = 1; i < 32u; i += 2u)
            if (b->h[i] == '.')
                grid_hit(t, i, 3, 1);
    t->p[P_SLEN] = 32;
    am.pat_now = am.pat;
}

/* DARK REESE: the same two saws further apart, the filter lower and ringing: DARKSIDE's bass. (The plan wants a CZ-1
 * patch here, as Kevin Saunderson made his; nobody has heard this build yet, so it stays on the engine we know.) */
static const uint8_t AM_DREESE[][2] = {
    {P5_FREQ_A, 24}, {P5_FREQ_B, 24}, {P5_FINE_B, 15}, {P5_SAW_A, 1}, {P5_SAW_B, 1}, {P5_KEY_B, 1},
    {P5_LEVEL_A, 124}, {P5_LEVEL_B, 124}, {P5_CUTOFF, 36}, {P5_RESONANCE, 48}, {P5_KEY_FILTER, 1}, {P5_FILTER_REV, 0},
    {P5_ENV_FILTER, 34}, {P5_DECAY_FILTER, 70}, {P5_SUSTAIN_FILTER, 40}, {P5_RELEASE_FILTER, 30},
    {P5_DECAY_AMP, 90}, {P5_SUSTAIN_AMP, 127}, {P5_RELEASE_AMP, 28}, {P5_RELEASE_ON, 1}, {P5_GLIDE, 34},
    {P5_LFO_RATE, 30}, {P5_LFO_TRI, 1}, {P5_WHEEL_FILTER, 1}, {P5_VINTAGE, 40}, {P5_BEND, 2},
};

/* The stages, a detent of PRESETS apiece: the night and its venue, the photo, the tempo, the break it starts on, the
 * bass, how far down the chords sit, and the sleeve notes (ten lines, the photo's credit last). */
typedef struct {
    const char *night, *venue;
    const uint8_t *pal, *lz;
    const uint16_t *at;
    const uint8_t (*bass)[2];
    uint8_t nbass, bpm, pat;
    int8_t chord_trans;
    const char *notes[10];
    uint8_t credit;                                    /* the first line of the photo's credit */
} am_stage_t;
static const am_stage_t AM_STAGES[] = {
    {"FRIDAY", "THE END", AM_END_PAL, AM_END_LZ, AM_END_AT, AM_REESE, NELEM(AM_REESE), 174, 0, 0,
     {"FABRIC OPENED IN 1999", "AND KEPT FRIDAYS FOR", "DRUM AND BASS: DJ HYPE'S", "PLAYAZ. MAY 2001: VALVE,",
      "DILLINJA AND LEMON D'S", "SOUND SYSTEM, 52 SUBS.", "THE END, WEST CENTRAL", "STREET, CLOSED IN 2009.",
      "PHOTO: steve_w, CC BY 2.0,", "CHANGED WITH GEMINI"}, 8},
    {"DARKSIDE", "TURNMILLS", AM_MILLS_PAL, AM_MILLS_LZ, AM_MILLS_AT, AM_DREESE, NELEM(AM_DREESE), 165, 1, -12,
     {"1994: RENEGADE'S", "TERRORIST TAKES ONE NOTE", "OF KEVIN SAUNDERSON'S", "1988 BASS, THE REESE.",
      "DARKSIDE HAS ITS SOUND.", "TURNMILLS, CLERKENWELL", "ROAD, CLOSED IN 2008.",
      "PHOTO: Rubberist,", "PUBLIC DOMAIN,", "CHANGED WITH GEMINI"}, 7},
};
#define AM_STAGE (&AM_STAGES[am.stage % NELEM(AM_STAGES)])

static void am_knobs(void)                              /* the four knobs onto the sounds */
{
    track_t *b = &trk[AM_TBASS], *c = &trk[AM_TCHORD], *d = &trk[AM_TBREAK];
    /* GRIT's strongest setting is about the sketch's 70 (docs/amethyst/knobs/sheet.png), not the knob's own 100 */
    uint32_t grit_eff = (uint32_t)am.knob[2] * 70u / 100u;
    p5_patch_of(b)->raw[P5_LFO_RATE] = (uint8_t)(10u + am.knob[0] * 50u / 100u);   /* WOBBLE: how fast the filter sways */
    trk[AM_TBASS].mw = (uint8_t)(am.knob[0] * 127u / 100u);                       /* WOBBLE: how far (the LFO's depth) */
    b->p[P_E1] = (int16_t)(-20 + grit_eff * 50 / 100);            /* GRIT: the Reese's resonance */
    c->p[P_E0] = (int16_t)(-10 - am.knob[1] * 40 / 100);          /* FOG: the chords further off, darker */
    d->p[P_E0 + 7] = (int16_t)(grit_eff * 127 / 100);             /* GRIT: the break's drive */
    d->p[P_E0 + 2] = (int16_t)(70 - am.knob[3] * 50 / 100);       /* MOSS: the break's tone, darker */
}

static void am_setup(void)                              /* the stage's tracks, tempo and break */
{
    const am_stage_t *st = AM_STAGE;
    track_t *b = &trk[AM_TBASS], *c = &trk[AM_TCHORD], *d = &trk[AM_TBREAK], *n = &trk[AM_TSIREN];
    undo_depth++;
    set_engine_of(n, ENGI_PROPHET);
    apply_preset_to(n, 0);
    set_engine_of(b, ENGI_PROPHET);
    apply_preset_to(b, 0);
    set_engine_of(c, ENGI_PROPHET);
    apply_preset_to(c, 0);
    if (d->eng_req != ENGI_DRUM)
        set_engine_of(d, ENGI_DRUM);
    apply_preset_to(d, 1);                              /* 909 KIT */
    undo_depth--;
    fm1_irq_off();
    am_p5(p5_patch_of(b), st->bass, st->nbass, "REESE");
    p5_track_accept(b);
    am_p5(p5_patch_of(c), AM_PAD, NELEM(AM_PAD), "FOG PAD");
    p5_track_accept(c);
    am_p5(p5_patch_of(n), AM_SIREN, NELEM(AM_SIREN), "SIREN");
    p5_track_accept(n);
    n->mw = 46;                                         /* how far the siren sweeps */
    n->p[P_VOICE] = 2;                                  /* one note at a time */
    b->p[P_VOICE] = 2;                                  /* LEG: one note, sliding */
    b->p[P_TRANS] = -12;
    c->p[P_CHRD] = 2;                                   /* DIA7: each key a seventh of the scale */
    c->p[P_ROOT] = 9;                                   /* A */
    c->p[P_SCALE] = 2;                                  /* MIN */
    c->p[P_VOIC] = 1;                                   /* OPEN */
    c->p[P_TRANS] = st->chord_trans;
    b->p[P_MUTE] = c->p[P_MUTE] = d->p[P_MUTE] = 0;
    n->p[P_MUTE] = 1;                                   /* (the siren rests until GLO) */
    fm1_irq_on();
    am.pat = st->pat;
    am.vip = am.rewinds = am.call = am.brk_out = 0;      /* each stage earns its own VIP */
    am.oct = 0;
    am.moss = 0;
    am_break_load();
    song.g[G_BPM] = st->bpm;
    am_knobs();
}

/* ------------------------------------------------------------ the night --- */
static void am_say(const char *s)
{
    str_cpy(am.msg, s, sizeof am.msg);
    am.msg_t0 = fm1_ms | 1u;
}
/* ms since t0, a time kept as fm1_ms | 1 (0: never), so maybe a millisecond ahead: never negative */
static uint32_t am_since(uint32_t t0) { int32_t d = (int32_t)(fm1_ms - t0); return d > 0 ? (uint32_t)d : 0u; }
static uint32_t am_bar_ms(void) { return 240000u / (uint32_t)(song.g[G_BPM] > 40 ? song.g[G_BPM] : 174); }

static void am_build(uint32_t on)
{
    if (on) {
        if (am.rwd_t0 || am.stopping)
            return;
        if (!song.playing) {                            /* nothing is building if nothing is playing */
            am_say("PRESS PLAY FIRST");
            return;
        }
        am.building = 1;
        am.build_t0 = fm1_ms | 1u;
        am.call = 0;
        am_seq.drop = 0;
        am_seq.bstep = 0;
        am_seq.building = 1;
        punch.req = PX_LOOP8;
        trk[AM_TBASS].p[P_MUTE] = 1;                    /* the bass out while it builds */
        am_say("BUILDING...");
    } else if (am.building) {
        am.building = 0;
        am_seq.building = 0;
        am.build_steps = am_seq.bstep;                  /* 16ths it was held: the drop's rewind test reads this */
        am_seq.drop = 1;                                /* (amethyst_seq.h: on the next beat's first step) */
    }
}
static void am_rewind(void)
{
    if (!am.call) {
        am_say(am.rwd_t0 ? "" : "EARN IT: HOLD FX, LET IT DROP");
        return;
    }
    am.call = 0;
    punch.req = PX_RWD;                                 /* the record dragged back under the needle */
    am.rwd_t0 = fm1_ms | 1u;
    am_say("PULL UP!");
}
static void am_poll(void)
{
    uint32_t bar = am_bar_ms();
    if (punch.keybit != AM_PKEY)                        /* (also after Melodee resets its effects) */
        punch.keybit = AM_PKEY;
    kb_mask = kb_lock = 0;                              /* a held button never takes the keys for one of Melodee's */
    punch_panel_mask = punch_panel_lock = 0;            /* layers: with GLO held they play the siren */
    if (am.building) {                                  /* the loops get shorter every 4 beats: 1/8, 1/16, 1/32 */
        uint32_t beats = am_seq.bstep / 4u;
        int8_t want = beats < 4u ? PX_LOOP8 : beats < 8u ? PX_LOOP16 : PX_LOOP32;
        if (punch.req != want && am_seq.ghost % 4u == 0u)   /* changed on a beat: never mid-loop */
            punch.req = want;
    }
    if (am_seq.dropped) {                               /* the drop landed (the ISR let go of the loop) */
        am_seq.dropped = 0;
        am.drop_t0 = fm1_ms | 1u;
        if (am.build_steps >= 16u) {                    /* held 4 beats (a bar) or more: the crowd wants it again */
            am.call = 1;
            am.call_until = fm1_ms + 8u * bar;
            am_say("");
        } else
            am_say("BUILD IT LONGER FOR A REWIND");
    }
    if (am.call && (int32_t)(fm1_ms - am.call_until) >= 0)
        am.call = 0;
    if (am.rwd_t0 && am_since(am.rwd_t0) >= 600u) {      /* the rewind done: the tune again from the top */
        am.rwd_t0 = 0;
        punch.req = -1;
        transport_req = 3;
        am.drop_t0 = fm1_ms | 1u;
        if (++am.rewinds == 3u && !am.vip) {            /* three: the stage's VIP is cut */
            am.vip = 1;
            am_break_load();
            am.plate_t0 = fm1_ms | 1u;
            {
                char m[32] = "DUBPLATE CUT: ";
                str_cpy(m + 14, AM_STAGE->night, sizeof m - 14u);
                str_cpy(m + str_len(m), " VIP", 5);
                am_say(m);
            }
        } else
            am_say("COME AGAIN");
    }
    if (am.pat != am.pat_now && (!song.playing || am_seq.ghost % 16u == 0u))   /* a new break: on the next bar */
        am_break_load();
    if (am_seq.kicks != am.kicks) {                     /* a kick: the screen's pulse */
        am.kicks = am_seq.kicks;
        am.kick_ms = fm1_ms | 1u;
    }
    if (song.playing && fm1_ms - am.bar0 >= bar) {      /* the moss climbs a little each bar (MOSS: faster) */
        am.bar0 = fm1_ms;
        if (am.moss < 280u)
            am.moss = (uint16_t)(am.moss + 2u + am.knob[3] / 20u);
    }
    if (am.msg[0] && !am.combo_t0 && am_since(am.msg_t0) > 2500u)
        am.msg[0] = 0;
}

/* the break's step on the sequencer (amethyst_seq.h): the chop, the build's beat count, the drop, the kicks. In
 * the audio interrupt. */
static void am_seq_step(track_t *t, uint32_t starting, uint32_t len)
{
    uint32_t idx;
    if (!am_seq.on || t != &trk[AM_TBREAK] || !len)
        return;
    am_seq.ghost = (uint16_t)(starting ? 0u : (am_seq.ghost + 1u) % len);
    if (am_seq.building && am_seq.bstep < 0xFFFFu)      /* 16ths since BUILD was pressed (amethyst.c: the loop length */
        am_seq.bstep++;                                 /* and whether it earns a rewind) */
    if (am_seq.drop && am_seq.ghost % 4u == 0u) {       /* the drop: on the next beat, the break from its first step */
        punch.req = -1;
        trk[AM_TBASS].p[P_MUTE] = 0;
        am_seq.ghost = 0;
        am_seq.drop = 0;
        am_seq.dropped = 1;
    }
    idx = am_seq.chop ? (AM_SLICE[(am_seq.chop - 1u) % AM_SLICES] + (am_seq.cn++ & 1u)) % len : am_seq.ghost;
    t->seq_idx = (uint16_t)idx;
    if (seq_steps(t)[idx].hit & 1u)
        am_seq.kicks++;
}

/* ------------------------------------------------------------ the screen --- */
static uint16_t am_rgb(const uint8_t *c, uint32_t lift, uint32_t white)   /* lift: x1/16 brighter; white: 0..255 */
{
    uint32_t r = c[0] * (16u + lift) / 16u, g = c[1] * (16u + lift) / 16u, b = c[2] * (16u + lift) / 16u;
    r = r > 255u ? 255u : r; g = g > 255u ? 255u : g; b = b > 255u ? 255u : b;
    r += (255u - r) * white / 255u; g += (255u - g) * white / 255u; b += (255u - b) * white / 255u;
    return RGB(r, g, b);
}
static uint32_t am_reach(uint32_t x, uint32_t y)        /* how late the moss reaches its pixel (x, y), 0..~280 */
{
    uint32_t h = (x / 4u) * 374761393u + (y / 4u) * 668265263u, side = x < 60u ? x : 119u - x;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (119u - y) * 150u / 120u + side * 70u / 60u + ((h >> 24) & 63u);   /* up from the bottom, in from the sides */
}
/* a cheap hash for GRIT's specks and torn rows: no table, no float */
static uint32_t am_hash(uint32_t x)
{
    uint32_t h = x * 374761393u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
/* one period of sin(), -100..100, so WOBBLE's sway and FOG's drift need no float sinf() */
static const int8_t AM_SIN[64] = {
    0, 10, 20, 29, 38, 47, 56, 63, 71, 77, 83, 88, 92, 96, 98, 100, 100, 100, 98, 96, 92, 88, 83, 77,
    71, 63, 56, 47, 38, 29, 20, 10, 0, -10, -20, -29, -38, -47, -56, -63, -71, -77, -83, -88, -92, -96, -98, -100,
    -100, -100, -98, -96, -92, -88, -83, -77, -71, -63, -56, -47, -38, -29, -20, -10,
};
/* the photo's palette, with WOBBLE and FOG's filter, as plain RGB bytes (am_draw mixes FOG's fog into these once a
 * band; swap16(RGB(...)) at amount 0 is exactly the old pal[], so FOG off looks the same as before) */
static void am_pal_rgb(uint8_t out[3], const uint8_t *c, uint32_t lift, uint32_t white, uint32_t tr, uint32_t tg, uint32_t tb)
{
    uint32_t r = c[0] * (16u + lift) / 16u, g = c[1] * (16u + lift) / 16u, b = c[2] * (16u + lift) / 16u;
    r = r > 255u ? 255u : r; g = g > 255u ? 255u : g; b = b > 255u ? 255u : b;
    r = r * tr / 100u; g = g * tg / 100u; b = b * tb / 100u;     /* MOSS: a green tint across the whole photo */
    r = r > 255u ? 255u : r; g = g > 255u ? 255u : g; b = b > 255u ? 255u : b;
    r += (255u - r) * white / 255u; g += (255u - g) * white / 255u; b += (255u - b) * white / 255u;
    out[0] = (uint8_t)r; out[1] = (uint8_t)g; out[2] = (uint8_t)b;
}
static uint16_t am_fog_mix(const uint8_t rgb[3], int32_t al)   /* al: 0..97, toward kipatchi's fog (176, 166, 204) */
{
    int32_t r = rgb[0] + ((176 - (int32_t)rgb[0]) * al) / 100;
    int32_t g = rgb[1] + ((166 - (int32_t)rgb[1]) * al) / 100;
    int32_t b = rgb[2] + ((204 - (int32_t)rgb[2]) * al) / 100;
    return RGB((uint32_t)clamp(r, 0, 255), (uint32_t)clamp(g, 0, 255), (uint32_t)clamp(b, 0, 255));
}

static void am_label(int32_t y, const char *title, const char *sub)
{
    int32_t w1 = text_w(TY_VL, title), w2 = text_w(TY_LB, sub), w = (w1 > w2 ? w1 : w2) + 16, x = (240 - w) / 2;
    uint16_t bg = RGB(12, 10, 10);
    cv_rect(x, y, w, 52, RGB(110, 96, 70));
    cv_rect(x + 1, y + 1, w - 2, 50, bg);
    cv_text_on(x + 8, ty_y(y + 18, TY_VL), TY_VL, title, RGB(236, 228, 210), bg);
    cv_text_on(x + 8, ty_y(y + 40, TY_LB), TY_LB, sub, RGB(214, 196, 160), bg);
}

static void am_draw(void)
{
    uint8_t pal_rgb[16][3];
    uint16_t mpal[16], row_pal[16], row_dark[16], row_light[16];
    uint32_t i, band, x, y, kick = 0, white = 0, glow, bar = am_bar_ms();
    uint32_t grit = am.knob[2];
    uint32_t grit_and = grit >= 45u ? ~1u : ~0u;                   /* GRIT past 45: the photo in 2 x 2 blocks */
    uint32_t grit_dark = grit * 40u / 100u, grit_light = grit * 10u / 100u;   /* grain: of 256 pixels, darker / lighter */
    uint32_t grit_tear = grit * 8u / 100u;                         /* of 256 row pairs, torn sideways */
    uint32_t moss_reach = am.knob[3] * 280u / 100u;                /* MOSS: pushes the reach on, besides the night's own */
    uint32_t moss_thresh = am.moss > moss_reach ? am.moss : moss_reach;
    uint32_t mk = am.knob[3] * 22u / 100u;                         /* MOSS: a little green over the whole photo */
    uint32_t tr = 100u - mk + 55u * mk / 100u, tg = 100u - mk + 105u * mk / 100u, tb = 100u - mk + 50u * mk / 100u;
    uint32_t wobble_cycle = 2u * bar - am.knob[0] * bar / 100u;    /* WOBBLE: one sway, 2 bars slow .. 1 bar fast */
    uint32_t wobble_phase = (fm1_ms * 64u) / wobble_cycle;
    uint32_t fog_phase = (fm1_ms * 64u) / 3200u;                    /* FOG: drifts, about 3.2 s a cycle */
    uint32_t frame_hash = fm1_ms / 120u;                            /* the grain and the tears change 8 times a second */
    char sub[40], title[24];
    const am_stage_t *st = AM_STAGE;
    if (am.kick_ms && am_since(am.kick_ms) < 220u)
        kick = 220u - am_since(am.kick_ms);           /* 220 .. 0 ms after the kick */
    if (am.building) {
        uint32_t held = am_since(am.build_t0), two = 2u * bar;
        white = (held > two ? two : held) * 150u / two;
    }
    glow = kick * 14u / 220u;
    for (i = 0; i < 16u; i++) {
        am_pal_rgb(pal_rgb[i], st->pal + 3u * i, kick * 3u / 220u, white, tr, tg, tb);
        mpal[i] = swap16(am_rgb(AM_MOSS_PAL + 3u * i, (AM_MOSS_PAL[3u * i] + AM_MOSS_PAL[3u * i + 1u] > 260u) ? glow : 0u, white / 2u));
    }
    mpal[0] = 0;
    str_cpy(sub, st->night, sizeof sub);
    str_cpy(sub + str_len(sub), "  -  ", 6);
    fmt_int(sub + str_len(sub), song.g[G_BPM]);
    str_cpy(sub + str_len(sub), " BPM", 5);
    cv_oy = 0;
    for (band = 0; band < 240u; band += CV_MAX / 240u) {
        cv_begin_band(240, 240, 0, band);
        for (y = cv_top; y < cv_top + cv_rows && y < 240u; y++) {
            uint16_t *o = cv_px + (y - cv_top) * 240u;
            const uint8_t *p, *m;
            int32_t dx = 0, al;
            uint32_t rng = am_hash(y + frame_hash * 977u), sy = y & grit_and;
            if (y % AM_BAND == 0u || y == cv_top)        /* the photo's band, unpacked */
                lz_unpack(am_pic, st->lz + st->at[y / AM_BAND], AM_BAND * 120u);
            /* FOG: this row's palette toward the fog, thicker lower down, drifting a little */
            al = y > 40u ? (int32_t)((y - 40u) * am.knob[1] * 70u / 20000u) : 0;
            al += (8 * (int32_t)am.knob[1] * AM_SIN[(((y * 290u) >> 8) + fog_phase) & 63u]) / 10000;
            al = clamp(al, 0, 80);
            for (i = 0; i < 16u; i++) {
                uint16_t u = am_fog_mix(pal_rgb[i], al);
                row_pal[i] = swap16(u);
                row_dark[i] = swap16((uint16_t)(((u >> 1) & 0x7BEFu) + ((u >> 2) & 0x39E7u)));   /* GRIT's grain: 3/4 */
                row_light[i] = swap16((uint16_t)(u + (((uint16_t)~u >> 3) & 0x18E3u)));         /* and an eighth to white */
            }
            if ((y / 2u) % AM_BAND == 0u && y % 2u == 0u) {   /* the moss's band (its rows are 2 screen rows each) */
                uint32_t my = y / 2u, rows = 120u - my < AM_BAND ? 120u - my : AM_BAND;
                lz_unpack(am_moss, AM_MOSS_LZ + AM_MOSS_AT[my / AM_BAND], rows * 60u);
            } else if (y == cv_top) {
                uint32_t my = (y / 2u) / AM_BAND * AM_BAND, rows = 120u - my < AM_BAND ? 120u - my : AM_BAND;
                lz_unpack(am_moss, AM_MOSS_LZ + AM_MOSS_AT[my / AM_BAND], rows * 60u);
            }
            p = am_pic + (sy % AM_BAND) * 120u;
            m = am_moss + ((y / 2u) % AM_BAND) * 60u;
            if (am.knob[0])                              /* WOBBLE: the row sways, up to 4 px at the bottom, less higher up */
                dx = ((int32_t)am.knob[0] * AM_SIN[(((sy * 107u) >> 8) + wobble_phase) & 63u] * (40 + (int32_t)sy / 4)) / 250000;
            if (grit_tear) {                             /* GRIT: now and then a pair of rows torn sideways */
                uint32_t th = am_hash((y / 2u) * 131u + frame_hash);
                if ((th & 0xFFu) < grit_tear)
                    dx += (int32_t)((th >> 8) % 9u) - 4;
            }
            for (x = 0; x < 240u; x += 2u) {
                uint32_t mi = (x / 2u) & 1u ? m[x / 4u] & 15u : m[x / 4u] >> 4, k;
                if (mi && moss_thresh && am_reach(x / 2u, y / 2u) < moss_thresh) {   /* the moss has reached here */
                    o[x] = o[x + 1u] = mpal[mi];
                    continue;
                }
                for (k = 0; k < 2u; k++) {
                    int32_t sx = clamp((int32_t)((x + k) & grit_and) - dx, 0, 239);
                    uint32_t ni = sx & 1 ? p[sx >> 1] & 15u : p[sx >> 1] >> 4, r;
                    uint16_t c = row_pal[ni];
                    if (grit_dark) {
                        rng = rng * 1664525u + 1013904223u;
                        r = rng >> 24;
                        if (r < grit_dark)
                            c = row_dark[ni];
                        else if (r >= 256u - grit_light)
                            c = row_light[ni];
                    }
                    o[x + k] = c;
                }
            }
        }
        if (am.drop_t0 && am_since(am.drop_t0) < 8u * bar) {   /* lasers after the drop */
            uint32_t f = (fm1_ms / 90u) & 1u;
            for (i = 0; i < 9u; i++) {
                int32_t ex = -60 + (int32_t)i * 45, ey = i == 4u ? 240 : 150 + (int32_t)((i > 4u ? 8u - i : i) * 10u);
                cv_line(120, 96, ex, ey, (i + f) & 1u ? RGB(206, 172, 248) : RGB(150, 210, 130));
            }
        }
        str_cpy(title, st->venue, sizeof title);
        if (am.vip)
            str_cpy(title + str_len(title), "  VIP", 6);
        am_label(10, title, sub);
        if (am.call) {                                  /* the crowd: lighters up, the call */
            uint32_t blink = (fm1_ms / 300u) & 1u;
            cv_rect(0, 76, 240, 78, RGB(12, 10, 14));
            cv_text_c(120, ty_y(106, TY_HR), TY_HR, "REWIND", blink ? RGB(255, 214, 150) : RGB(226, 140, 48), RGB(12, 10, 14));
            cv_text_c(120, ty_y(142, TY_TX), TY_TX, "COME AGAIN  -  SCL", RGB(206, 172, 248), RGB(12, 10, 14));
            for (i = 0; i < 9u; i++) {
                int32_t lx = 16 + (int32_t)i * 26, ly = 176 + (int32_t)((i * 53u) % 18u);
                uint32_t fl = 3u + ((fm1_ms / 70u + i * 2u) % 3u);
                cv_rect(lx - 5, ly, 10, 26, RGB(11, 9, 15));
                cv_rect(lx - 3, ly - 8, 6, 8, RGB(60, 60, 70));
                cv_disc(lx, ly - 10 - (int32_t)fl, 6 + (int32_t)fl, RGB(226, 140, 48), 0);
            }
        }
        if (am.plate_t0 && am_since(am.plate_t0) < 3500u) {   /* the dubplate, just cut */
            cv_rect(0, 64, 240, 176, RGB(11, 9, 15));
            cv_disc(120, 150, 150, RGB(24, 22, 28), 0);
            cv_ring(120, 150, 120, RGB(44, 40, 52));
            cv_ring(120, 150, 96, RGB(44, 40, 52));
            cv_disc(120, 150, 64, RGB(236, 228, 210), 0);
            cv_text_c(120, ty_y(140, TY_LB), TY_LB, st->night, RGB(40, 30, 110), RGB(236, 228, 210));
            cv_text_c(120, ty_y(162, TY_TX), TY_TX, "VIP", RGB(216, 48, 60), RGB(236, 228, 210));
        }
        if (am.notes) {                                 /* the sleeve notes: where the night comes from */
            cv_rect(0, 66, 240, 174, RGB(12, 10, 10));
            for (i = 0; i < 10u; i++)
                cv_text_c(120, ty_y(76 + (int32_t)i * 17, TY_LB), TY_LB, st->notes[i], i >= st->credit ? RGB(150, 140, 160) : RGB(214, 196, 160),
                          RGB(12, 10, 10));
        } else if (am.building) {                       /* the build meter: three beats a segment, 1/8 1/16 1/32 */
            uint32_t seg = am_seq.bstep / 16u, fill;
            if (seg > 2u)
                seg = 2u;
            fill = am_seq.bstep - seg * 16u;
            if (fill > 16u)
                fill = 16u;
            cv_rect(0, 206, 240, 34, RGB(12, 10, 10));
            for (i = 0; i < 3u; i++) {
                int32_t x0 = 4 + (int32_t)i * 79;
                cv_rect(x0, 208, 75, 10, RGB(44, 40, 52));
                if (i < seg)
                    cv_rect(x0, 208, 75, 10, RGB(226, 140, 48));
                else if (i == seg && fill)
                    cv_rect(x0, 208, (int32_t)(75u * fill / 16u), 10, RGB(226, 140, 48));
            }
            cv_text_c(120, ty_y(232, TY_LB), TY_LB, "LET GO TO DROP", RGB(226, 140, 48), RGB(12, 10, 10));
        } else if (am.knob_show && am_since(am.knob_show_t0) < 1500u) {   /* a knob's bar, for 1.5 s after it turns */
            uint32_t v = am.knob[am.knob_show - 1u];
            cv_rect(0, 206, 240, 34, RGB(12, 10, 10));
            cv_rect(10, 208, 220, 10, RGB(44, 40, 52));
            cv_rect(10, 208, (int32_t)(220u * v / 100u), 10, RGB(226, 140, 48));
            cv_text_c(120, ty_y(232, TY_LB), TY_LB, am.msg, RGB(226, 140, 48), RGB(12, 10, 10));
        } else if (am.msg[0]) {                         /* the line at the bottom */
            cv_rect(0, 212, 240, 28, RGB(12, 10, 10));
            cv_text_c(120, ty_y(226, TY_LB), TY_LB, am.msg, RGB(226, 140, 48), RGB(12, 10, 10));
        }
        cv_blit_band(0, 0);
    }
}

static void am_leds(void)
{
    static const uint8_t USED[] = {B_FX, B_SCL, B_ENV, B_LFO, B_EDIT, B_GLO, B_HOME, B_SAVE, B_ARP, B_OCTDN, B_OCTUP,
                                   B_PLAY, B_SEQ};
    uint8_t nl[FM1_NCOL] = {0}, nd[FM1_NCOL] = {0}, nb[FM1_NCOL] = {0};
    uint32_t k, c, blink = (fm1_ms / 160u) & 1u;
    for (k = 0; k < 27u; k++) {
        led_put(nl, 14u + k, (int)((fm1_in.notes >> k) & 1u));
        led_put(nd, 14u + k, 1);
    }
    for (k = 0; k < NELEM(USED); k++)
        led_put(nb, panel.btn[USED[k]], 1);
    led_put(nl, panel.btn[B_PLAY], song.playing);
    led_put(nl, panel.btn[B_FX], am.building);
    led_put(nl, panel.btn[B_SCL], am.call && blink);
    led_put(nl, panel.btn[B_ENV], am.half);
    led_put(nl, panel.btn[B_LFO], am.stopping);
    led_put(nl, panel.btn[B_EDIT], am.cut);
    led_put(nl, panel.btn[B_GLO], am_seq.siren);
    led_put(nl, panel.btn[B_ARP], am_seq.layout != 0u);
    led_put(nl, panel.btn[B_HOME], am.notes);
    led_put(nl, panel.btn[B_SEQ], !trk[AM_TBREAK].p[P_MUTE]);
    led_put(nl, panel.btn[B_OCTDN], am.oct < 0);
    led_put(nl, panel.btn[B_OCTUP], am.oct > 0);
    fm1_led_dim_mask[0] = LIGHTS_MASK[settings_lights % LIGHTS_N];
    fm1_led_dim_mask[1] = BTN_DIM_MASK;
    for (c = 0; c < FM1_NCOL; c++) {
        fm1_led_dim[0][c] = (uint8_t)(nd[c] | nl[c]);
        fm1_led_dim[1][c] = nb[c];
        fm1_led[c] = nl[c];
    }
}

/* ------------------------------------------------------------ the mode --- */
static void am_boot(void)
{
    led_pos_init();
    am.knob[0] = 40;
    am.knob[1] = 35;
    am.knob[2] = 15;
    am.knob[3] = 30;
    am_setup();
    am.on = 1;
    am_seq.on = 1;
    punch.keybit = AM_PKEY;
    transport_req = 1;                                  /* the break from the start */
    am_say("AMETHYST  -  JOSH & KIPATCHI");
}

/* to Melodee: its own start again (the tracks' default sounds, then the BOOT project or the template); nothing is
 * saved. Amethyst comes back at the next power-on. */
static void am_leave(void)
{
    uint32_t i;
    am.on = 0;
    am_seq.on = 0;
    am_seq.chop = am_seq.drop = am_seq.building = am_seq.siren = am_seq.layout = 0;
    punch.req = -1;
    punch.keybit = 0;
    transport_req = 2;
    undo_depth++;
    for (i = 0; i < NPART; i++) {
        set_engine_of(&trk[i], TRK_DEF[i][0]);
        apply_preset_to(&trk[i], TRK_DEF[i][1]);
        trk[i].p[P_MUTE] = 0;
    }
    undo_depth--;
    project_boot();
    lcd_fill(0, 0, 240, 240, T_BG);
    ui.home = 1;
    ui.force = 1;
}

/* one main-loop pass while Amethyst is on: the panel, the night, the LEDs, the screen. 0: Melodee's UI */
static int am_frame(void)
{
    uint32_t released, pressed, both, i;
    int32_t s;
    if (!am.on)
        return 0;
    pressed = fm1_input_edges(0);
    released = am.held & ~fm1_in.buttons;               /* (from what was held: the same on the device and the host) */
    am.held = fm1_in.buttons;
    (void)fm1_input_note_edges();                        /* (the keys play in the audio interrupt: seq.c) */
    both = AM_BTN(B_HOME) | AM_BTN(B_SAVE);
    if ((fm1_in.buttons & both) == both) {              /* HOME + SAVE held: the countdown, then Melodee */
        int32_t held = (int32_t)(fm1_ms - am.combo_t0);
        am.home_combo = 1;
        if (!am.combo_t0)
            am.combo_t0 = fm1_ms | 1u;
        else if (held >= (int32_t)AM_HOLD_MS) {
            am.combo_t0 = 0;
            am_leave();
            return 1;
        } else {
            char m[20] = "MELODEE IN ";
            m[11] = (char)('0' + (AM_HOLD_MS - (uint32_t)(held > 0 ? held : 0) + 999u) / 1000u);
            m[12] = 0;
            am_say(m);
        }
    } else if (am.combo_t0) {
        am.combo_t0 = 0;
        am.msg[0] = 0;
    }
    if (pressed & AM_BTN(B_FX))
        am_build(1);
    if (released & AM_BTN(B_FX))
        am_build(0);
    if (pressed & AM_BTN(B_SCL))
        am_rewind();
    if ((pressed & AM_BTN(B_ENV)) && !am.building && !am.rwd_t0 && !am.stopping) {
        am.half = 1;                                    /* HALF: SLOOP's half speed, while ENV is held (kept on, */
        punch.req = PX_HALF;                            /* it runs out of the 0.37 s it replays and falls quiet) */
        am_say("HALF SPEED");
    }
    if ((released & AM_BTN(B_ENV)) && am.half) {
        am.half = 0;
        if (punch.req == PX_HALF)
            punch.req = -1;
    }
    if ((pressed & AM_BTN(B_LFO)) && !am.building && !am.rwd_t0 && !am.half) {
        am.stopping = 1;                                /* STOP: the tape slows to nothing while LFO is held */
        punch.req = PX_STOP;
        am_say("TAPE STOP");
    }
    if ((released & AM_BTN(B_LFO)) && am.stopping) {    /* let go: the tune carries on where it would have been */
        am.stopping = 0;
        if (punch.req == PX_STOP)
            punch.req = -1;
    }
    if (pressed & AM_BTN(B_PLAY))
        transport_req = song.playing ? 2u : 1u;
    if (pressed & AM_BTN(B_SEQ)) {                      /* the break out and back: SEQ latches, EDIT while held */
        am.brk_out ^= 1;
        am_say(am.brk_out ? "BREAK OUT" : "BREAK IN");
    }
    if (pressed & AM_BTN(B_EDIT)) {
        am.cut = 1;
        am_say("BREAK OUT");
    }
    if (released & AM_BTN(B_EDIT))
        am.cut = 0;
    trk[AM_TBREAK].p[P_MUTE] = (int16_t)(am.brk_out || am.cut);
    if (pressed & AM_BTN(B_GLO)) {                      /* the siren on the high keys while GLO is held */
        am_seq.siren = 1;
        trk[AM_TSIREN].p[P_MUTE] = 0;
        am_say("SIREN: THE HIGH KEYS");
    }
    if (released & AM_BTN(B_GLO)) {
        am_seq.siren = 0;
        trk[AM_TSIREN].p[P_MUTE] = 1;
    }
    if (pressed & AM_BTN(B_ARP)) {                      /* which keys play what */
        static const char *const L[3] = {"KEYS: BASS AND CHORDS", "KEYS: ALL BASS", "KEYS: ALL CHORDS"};
        am_seq.layout = (uint8_t)((am_seq.layout + 1u) % 3u);
        am_say(L[am_seq.layout]);
    }
    if (released & AM_BTN(B_HOME)) {                    /* HOME tapped (not held with SAVE): the sleeve notes */
        if (!am.home_combo)
            am.notes ^= 1;
        am.home_combo = 0;
    }
    for (i = 0; i < 2u; i++)                            /* OCT- / OCT+: the bass an octave down or up */
        if (pressed & AM_BTN(i ? B_OCTUP : B_OCTDN)) {
            am.oct = (int8_t)clamp(am.oct + (i ? 1 : -1), -2, 1);
            trk[AM_TBASS].p[P_TRANS] = (int16_t)(-12 + am.oct * 12);
            am_say(am.oct < 0 ? (am.oct == -2 ? "BASS  -2" : "BASS  -1") : am.oct ? "BASS  +1" : "BASS  0");
        }
    if ((s = fm1_enc_take(panel.enc[EN_SELECT]) * panel.dir[EN_SELECT]) != 0) {   /* SELECT: the tempo */
        char m[12];
        song.g[G_BPM] = (int16_t)clamp(song.g[G_BPM] + (s > 0 ? 1 : -1), 140, 180);
        fmt_int(m, song.g[G_BPM]);
        str_cpy(m + str_len(m), " BPM", 5);
        am_say(m);
    }
    if ((s = fm1_enc_take(panel.enc[EN_PRESET]) * panel.dir[EN_PRESET]) != 0 && !am.building && !am.rwd_t0) {
        char m[32];                                     /* PRESETS: the next stage, its own sounds, tempo and break */
        am.stage = (uint8_t)((am.stage + (s > 0 ? 1u : NELEM(AM_STAGES) - 1u)) % NELEM(AM_STAGES));
        am_setup();
        str_cpy(m, AM_STAGE->night, sizeof m);
        str_cpy(m + str_len(m), " AT ", 5);
        str_cpy(m + str_len(m), AM_STAGE->venue, sizeof m - str_len(m));
        am_say(m);
    }
    if ((s = fm1_enc_take(panel.enc[EN_ALGO]) * panel.dir[EN_ALGO]) != 0) {   /* ALGORITHM: the break, a detent each */
        char m[24] = "BREAK: ";
        am.pat = (uint8_t)((am.pat + (s > 0 ? 1u : AM_NBREAK - 1u)) % AM_NBREAK);
        str_cpy(m + 7, AM_BREAKS[am.vip && !am.pat ? AM_NBREAK : am.pat].name, sizeof m - 7u);
        am_say(m);
    }
    for (i = 0; i < 4u; i++)                            /* the four knobs, 2 % a detent */
        if ((s = fm1_enc_take(panel.enc[EN_K1 + i]) * panel.dir[EN_K1 + i]) != 0) {
            static const char *const N[4] = {"WOBBLE ", "FOG ", "GRIT ", "MOSS "};
            char m[16];
            am.knob[i] = (uint8_t)clamp((int32_t)am.knob[i] + s * 2, 0, 100);
            am_knobs();
            str_cpy(m, N[i], sizeof m);
            fmt_int(m + str_len(m), am.knob[i]);
            am_say(m);
            am.knob_show = (uint8_t)(i + 1u);
            am.knob_show_t0 = fm1_ms | 1u;
        }
    am_poll();
    am_leds();
    if (fm1_ms - am.draw_ms >= 40u) {                    /* ~25 pictures a second */
        am.draw_ms = fm1_ms;
        am_draw();
    }
    return 1;
}
