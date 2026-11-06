/* SPDX-License-Identifier: GPL-3.0-only
 * Amethyst (joshbartlettband-web/Amethyst): drum and bass for the FM-1, by Josh, with the concept and the look by
 * kipatchi. Built on Melodee by Kerem Kilic (Ellic Studio), a modified version of Felucca by Leo Kuroshita, Hügelton
 * Instruments; the way it sits on Melodee follows Double Rainbow's dr.c. Amethyst's own code is this file and
 * amethyst_seq.h (the audio interrupt's part), so Melodee's updates merge easily. The plan: docs/AMETHYST.md.
 *
 * The first stage, FRIDAY: The End on West Central Street at 3 AM, ten years on, moss in the walls. A break runs on
 * track 4 (the DRUM engine's 909 kit, two bars of 16ths); the low white keys play a Reese on track 1 and the high
 * ones sevenths of A minor on track 2 (both Prophet programs of our own, below); the black keys hold slices of the
 * break. FX held builds (SLOOP's loops, 1/8 then 1/16 then 1/32, the bass out), and the drop lands on the next bar;
 * after a drop built for a bar or more the crowd calls for a rewind, and SCL answers it. Three rewinds cut the
 * stage's VIP. The moss climbs as the night goes on and glows on every kick.
 *   main.c: am_boot() after project_boot(); each pass am_frame() instead of Melodee's UI while it returns 1.
 *   HOME and SAVE held 3 s together: full Melodee, until the next power-on. */

#define AM_POOL __attribute__((section(".pool")))     /* Amethyst's RAM: the pool (.bss is Melodee's and nearly full) */
#define AM_HOLD_MS 3000u                               /* HOME + SAVE: to Melodee */
#define AM_BTN(b) (1u << panel.btn[b])
#include "am_art.h"                                    /* AM_END_*, AM_MOSS_* (tools/amethyst/gen_am_art.py) */

static void project_boot(void);                        /* project.c (included after this file) */

static struct {
    uint8_t on, building, call, vip, half, stopped, rewinds, plate;
    uint8_t knob[4];                                   /* WOBBLE FOG GRIT MOSS, 0..100 */
    int8_t oct;                                        /* the bass's octave, -2..+1 */
    uint32_t combo_t0, build_t0, build_ms, call_until, rwd_t0, stop_t0, msg_t0, draw_ms, kick_ms, drop_t0, plate_t0;
    uint32_t kicks, bar0, held;                       /* held: the buttons last pass (their releases) */
    uint16_t moss;                                     /* how far the moss has reached, 0..280 (amethyst reach) */
    char msg[32];
} am AM_POOL;
static uint8_t am_pic[240u * 120u] AM_POOL;            /* the stage's photo, 4 bits a pixel, unpacked */
static uint8_t am_moss[60u * 120u] AM_POOL;            /* the moss, 120 x 120, 4 bits a pixel (0: see-through) */
static uint8_t am_reach[120u * 120u] AM_POOL;          /* how late the moss reaches each of its pixels, 0..255 */

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
    {P5_LFO_RATE, 30}, {P5_LFO_TRI, 1}, {P5_VINTAGE, 30}, {P5_BEND, 2},
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

/* FRIDAY's break: two bars of 16ths, a roller in the style of the 2000s (our own pattern). Lanes: k the kick,
 * s the snare (x accented, o plain, - a rim ghost), h the closed hat (O: open), g ghosts on the rim. The VIP is
 * the same night with the kick doubled and the snare busier. */
typedef struct { const char *k, *s, *h, *g; } am_break_t;
static const am_break_t AM_FRIDAY[2] = {
    {"x.........x.......x.......x.....", "....x.......x.......x.....o.x...",
     "..x...x...x...O...x...x...x...O.", "...-.....-...-.....-.....-...-.."},
    {"x.........x..x....x.......x..x..", "....x...-...x.......x...-...x..x",
     "..x.x.x...x.x.O...x.x.x...x.x.O.", "...-.....-...-.....-.....-...-.."},
};
static void am_break_load(uint32_t vip)
{
    const am_break_t *b = &AM_FRIDAY[vip ? 1 : 0];
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
    t->p[P_SLEN] = 32;
}

static void am_knobs(void)                              /* the four knobs onto the sounds */
{
    track_t *b = &trk[AM_TBASS], *c = &trk[AM_TCHORD], *d = &trk[AM_TBREAK];
    b->p[P_E0] = (int16_t)(-40 + am.knob[0] * 90 / 100);          /* WOBBLE: the Reese's filter */
    b->p[P_E1] = (int16_t)(-20 + am.knob[2] * 50 / 100);          /* GRIT: its resonance */
    c->p[P_E0] = (int16_t)(-10 - am.knob[1] * 40 / 100);          /* FOG: the chords further off, darker */
    d->p[P_E0 + 7] = (int16_t)(am.knob[2] * 127 / 100);           /* GRIT: the break's drive */
    d->p[P_E0 + 2] = (int16_t)(70 - am.knob[3] * 50 / 100);       /* MOSS: the break's tone, darker */
}

static void am_setup(void)                              /* the stage's tracks, tempo and break */
{
    track_t *b = &trk[AM_TBASS], *c = &trk[AM_TCHORD], *d = &trk[AM_TBREAK];
    undo_depth++;
    set_engine_of(b, ENGI_PROPHET);
    apply_preset_to(b, 0);
    set_engine_of(c, ENGI_PROPHET);
    apply_preset_to(c, 0);
    if (d->eng_req != ENGI_DRUM)
        set_engine_of(d, ENGI_DRUM);
    apply_preset_to(d, 1);                              /* 909 KIT */
    undo_depth--;
    fm1_irq_off();
    am_p5(p5_patch_of(b), AM_REESE, NELEM(AM_REESE), "REESE");
    p5_track_accept(b);
    am_p5(p5_patch_of(c), AM_PAD, NELEM(AM_PAD), "FOG PAD");
    p5_track_accept(c);
    b->p[P_VOICE] = 2;                                  /* LEG: one note, sliding */
    b->p[P_TRANS] = -12;
    c->p[P_CHRD] = 2;                                   /* DIA7: each key a seventh of the scale */
    c->p[P_ROOT] = 9;                                   /* A */
    c->p[P_SCALE] = 2;                                  /* MIN */
    c->p[P_VOIC] = 1;                                   /* OPEN */
    b->p[P_MUTE] = c->p[P_MUTE] = d->p[P_MUTE] = 0;
    trk[2].p[P_MUTE] = 1;                               /* (track 3 rests) */
    fm1_irq_on();
    am_break_load(0);
    song.g[G_BPM] = 174;
    am_knobs();
}

/* ------------------------------------------------------------ the night --- */
static void am_say(const char *s)
{
    str_cpy(am.msg, s, sizeof am.msg);
    am.msg_t0 = fm1_ms | 1u;
}
static uint32_t am_bar_ms(void) { return 240000u / (uint32_t)(song.g[G_BPM] > 40 ? song.g[G_BPM] : 174); }

static void am_build(uint32_t on)
{
    if (on) {
        if (!song.playing || am.rwd_t0 || am.stop_t0)
            return;
        am.building = 1;
        am.build_t0 = fm1_ms | 1u;
        am.call = 0;
        am_seq.drop = 0;
        punch.keybit = 0;
        punch.req = PX_LOOP8;
        trk[AM_TBASS].p[P_MUTE] = 1;                    /* the bass out while it builds */
        am_say("BUILDING...");
    } else if (am.building) {
        am.building = 0;
        am.build_ms = fm1_ms - am.build_t0;
        am_seq.drop = 1;                                /* (amethyst_seq.h: on the next bar's first step) */
    }
}
static void am_rewind(void)
{
    if (!am.call) {
        am_say(am.rwd_t0 ? "" : "EARN IT: HOLD FX, LET IT DROP");
        return;
    }
    am.call = 0;
    punch.keybit = 0;
    punch.req = PX_RWD;                                 /* the record dragged back under the needle */
    am.rwd_t0 = fm1_ms | 1u;
    am_say("PULL UP!");
}
static void am_stop(void)
{
    if (am.stopped || !song.playing) {                  /* stopped: again from the top */
        am.stopped = 0;
        punch.req = -1;
        transport_req = 1;
        am_say("FROM THE TOP");
        return;
    }
    punch.keybit = 0;
    punch.req = PX_STOP;                                /* the tape slows to nothing */
    am.stop_t0 = fm1_ms | 1u;
}
static void am_poll(void)
{
    uint32_t bar = am_bar_ms();
    if (am.building) {                                  /* the loops get shorter: 1/8, 1/16, 1/32 */
        uint32_t held = fm1_ms - am.build_t0;
        int8_t want = held < bar ? PX_LOOP8 : held < 2u * bar ? PX_LOOP16 : PX_LOOP32;
        if (punch.req != want)
            punch.req = want;
    }
    if (am_seq.dropped) {                               /* the drop landed (the ISR let go of the loop) */
        am_seq.dropped = 0;
        am.drop_t0 = fm1_ms | 1u;
        if (am.build_ms >= bar) {                       /* built for a bar or more: the crowd wants it again */
            am.call = 1;
            am.call_until = fm1_ms + 8u * bar;
            am_say("");
        } else
            am_say("BUILD IT LONGER FOR A REWIND");
    }
    if (am.call && (int32_t)(fm1_ms - am.call_until) >= 0)
        am.call = 0;
    if (am.rwd_t0 && fm1_ms - am.rwd_t0 >= 700u) {      /* the rewind done: the tune again from the top */
        am.rwd_t0 = 0;
        punch.req = -1;
        transport_req = 3;
        am.drop_t0 = fm1_ms | 1u;
        if (++am.rewinds == 3u && !am.vip) {            /* three: the stage's VIP is cut */
            am.vip = 1;
            am_break_load(1);
            am.plate_t0 = fm1_ms | 1u;
            am_say("DUBPLATE CUT: FRIDAY VIP");
        } else
            am_say("COME AGAIN");
    }
    if (am.stop_t0 && fm1_ms - am.stop_t0 >= 900u) {    /* the tape stopped: the transport too */
        am.stop_t0 = 0;
        punch.req = -1;
        transport_req = 2;
        am.stopped = 1;
        am_say("STOPPED. LFO: FROM THE TOP");
    }
    if (am_seq.kicks != am.kicks) {                     /* a kick: the screen's pulse */
        am.kicks = am_seq.kicks;
        am.kick_ms = fm1_ms | 1u;
    }
    if (song.playing && fm1_ms - am.bar0 >= bar) {      /* the moss climbs a little each bar (MOSS: faster) */
        am.bar0 = fm1_ms;
        if (am.moss < 280u)
            am.moss = (uint16_t)(am.moss + 2u + am.knob[3] / 20u);
    }
    if (am.msg[0] && !am.combo_t0 && fm1_ms - am.msg_t0 > 2500u)
        am.msg[0] = 0;
}

/* the break's step on the sequencer (amethyst_seq.h): the chop, the drop, the kicks. In the audio interrupt. */
static void am_seq_step(track_t *t, uint32_t starting, uint32_t len)
{
    uint32_t idx;
    if (!am_seq.on || t != &trk[AM_TBREAK] || !len)
        return;
    am_seq.ghost = (uint16_t)(starting ? 0u : (am_seq.ghost + 1u) % len);
    idx = am_seq.chop ? (AM_SLICE[(am_seq.chop - 1u) % AM_SLICES] + (am_seq.cn++ & 1u)) % len : am_seq.ghost;
    t->seq_idx = (uint16_t)idx;
    if (am_seq.drop && am_seq.ghost % 16u == 0u) {     /* the drop: on the one */
        punch.req = -1;
        punch.keybit = 0;
        trk[AM_TBASS].p[P_MUTE] = 0;
        am_seq.drop = 0;
        am_seq.dropped = 1;
    }
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
static void am_unpack(void)                             /* the pictures into the pool, once */
{
    uint32_t k, x, y;
    for (k = 0; k + 1u < NELEM(AM_END_AT); k++)
        lz_unpack(am_pic + k * AM_BAND * 120u, AM_END_LZ + AM_END_AT[k], AM_BAND * 120u);
    for (k = 0, y = 0; k + 1u < NELEM(AM_MOSS_AT); k++, y += AM_BAND) {
        uint32_t rows = 120u - y < AM_BAND ? 120u - y : AM_BAND;
        lz_unpack(am_moss + y * 60u, AM_MOSS_LZ + AM_MOSS_AT[k], rows * 60u);
    }
    for (y = 0; y < 120u; y++)                          /* up from the bottom, in from the sides, ragged */
        for (x = 0; x < 120u; x++) {
            uint32_t h = (x / 4u) * 374761393u + (y / 4u) * 668265263u, side = x < 60u ? x : 119u - x;
            h = (h ^ (h >> 13)) * 1274126177u;
            am_reach[y * 120u + x] = (uint8_t)(y < 120u ? (119u - y) * 150u / 120u + side * 70u / 60u + ((h >> 24) & 63u) : 255u);
        }
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
    uint16_t pal[16], mpal[16];
    uint32_t i, band, x, y, kick = 0, white = 0, glow;
    char sub[40];
    if (am.kick_ms && fm1_ms - am.kick_ms < 220u)
        kick = 220u - (fm1_ms - am.kick_ms);           /* 220 .. 0 ms after the kick */
    if (am.building) {
        uint32_t held = fm1_ms - am.build_t0, two = 2u * am_bar_ms();
        white = (held > two ? two : held) * 150u / two;
    }
    glow = kick * 14u / 220u;
    for (i = 0; i < 16u; i++) {
        pal[i] = swap16(am_rgb(AM_END_PAL + 3u * i, kick * 3u / 220u, white));
        mpal[i] = swap16(am_rgb(AM_MOSS_PAL + 3u * i, (AM_MOSS_PAL[3u * i] + AM_MOSS_PAL[3u * i + 1u] > 260u) ? glow : 0u, white / 2u));
    }
    mpal[0] = 0;
    str_cpy(sub, "FRIDAY  -  ", sizeof sub);
    fmt_int(sub + str_len(sub), song.g[G_BPM]);
    str_cpy(sub + str_len(sub), " BPM", 5);
    cv_oy = 0;
    for (band = 0; band < 240u; band += CV_MAX / 240u) {
        cv_begin_band(240, 240, 0, band);
        for (y = cv_top; y < cv_top + cv_rows && y < 240u; y++) {
            uint16_t *o = cv_px + (y - cv_top) * 240u;
            const uint8_t *p = am_pic + y * 120u, *m = am_moss + (y / 2u) * 60u, *r = am_reach + (y / 2u) * 120u;
            for (x = 0; x < 240u; x += 2u) {
                uint32_t mi = (x / 2u) & 1u ? m[x / 4u] & 15u : m[x / 4u] >> 4;
                if (mi && r[x / 2u] < am.moss) {        /* the moss has reached here */
                    o[x] = o[x + 1u] = mpal[mi];
                } else {
                    o[x] = pal[p[x / 2u] >> 4];
                    o[x + 1u] = pal[p[x / 2u] & 15u];
                }
            }
        }
        if (am.drop_t0 && fm1_ms - am.drop_t0 < 8u * am_bar_ms()) {   /* lasers after the drop */
            uint32_t f = (fm1_ms / 90u) & 1u;
            for (i = 0; i < 9u; i++) {
                int32_t ex = -60 + (int32_t)i * 45, ey = i == 4u ? 240 : 150 + (int32_t)((i > 4u ? 8u - i : i) * 10u);
                cv_line(120, 96, ex, ey, (i + f) & 1u ? RGB(206, 172, 248) : RGB(150, 210, 130));
            }
        }
        am_label(10, am.vip ? "THE END  VIP" : "THE END", sub);
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
        if (am.plate_t0 && fm1_ms - am.plate_t0 < 3500u) {   /* the dubplate, just cut */
            cv_rect(0, 64, 240, 176, RGB(11, 9, 15));
            cv_disc(120, 150, 150, RGB(24, 22, 28), 0);
            cv_ring(120, 150, 120, RGB(44, 40, 52));
            cv_ring(120, 150, 96, RGB(44, 40, 52));
            cv_disc(120, 150, 64, RGB(236, 228, 210), 0);
            cv_text_c(120, ty_y(140, TY_LB), TY_LB, "FRIDAY", RGB(40, 30, 110), RGB(236, 228, 210));
            cv_text_c(120, ty_y(162, TY_TX), TY_TX, "VIP", RGB(216, 48, 60), RGB(236, 228, 210));
        }
        if (am.msg[0]) {                                /* the line at the bottom */
            cv_rect(0, 212, 240, 28, RGB(12, 10, 10));
            cv_text_c(120, ty_y(226, TY_LB), TY_LB, am.msg, RGB(226, 140, 48), RGB(12, 10, 10));
        }
        cv_blit_band(0, 0);
    }
}

static void am_leds(void)
{
    static const uint8_t USED[] = {B_FX, B_SCL, B_ENV, B_LFO, B_HOME, B_SAVE, B_OCTDN, B_OCTUP, B_PLAY, B_SEQ};
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
    am_unpack();
    am.knob[0] = 40;
    am.knob[1] = 35;
    am.knob[2] = 15;
    am.knob[3] = 30;
    am_setup();
    am.on = 1;
    am_seq.on = 1;
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
    am_seq.chop = am_seq.drop = 0;
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
    if ((pressed & AM_BTN(B_ENV)) && !am.building && !am.rwd_t0) {
        am.half ^= 1u;                                  /* HALF: the whole night at half speed, and back */
        punch.keybit = 0;
        punch.req = am.half ? PX_HALF : -1;
        am_say(am.half ? "HALF TIME" : "FULL TIME");
    }
    if (pressed & AM_BTN(B_LFO))
        am_stop();
    if (pressed & AM_BTN(B_PLAY)) {
        transport_req = song.playing ? 2u : 1u;
        am.stopped = 0;
    }
    if (pressed & AM_BTN(B_SEQ)) {                      /* the break out and back */
        trk[AM_TBREAK].p[P_MUTE] ^= 1;
        am_say(trk[AM_TBREAK].p[P_MUTE] ? "BREAK OUT" : "BREAK IN");
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
    if (fm1_enc_take(panel.enc[EN_PRESET]) != 0)
        am_say("ONE STAGE SO FAR: FRIDAY");
    (void)fm1_enc_take(panel.enc[EN_ALGO]);
    for (i = 0; i < 4u; i++)                            /* the four knobs, 2 % a detent */
        if ((s = fm1_enc_take(panel.enc[EN_K1 + i]) * panel.dir[EN_K1 + i]) != 0) {
            static const char *const N[4] = {"WOBBLE ", "FOG ", "GRIT ", "MOSS "};
            char m[16];
            am.knob[i] = (uint8_t)clamp((int32_t)am.knob[i] + s * 2, 0, 100);
            am_knobs();
            str_cpy(m, N[i], sizeof m);
            fmt_int(m + str_len(m), am.knob[i]);
            am_say(m);
        }
    am_poll();
    am_leds();
    if (fm1_ms - am.draw_ms >= 40u) {                    /* ~25 pictures a second */
        am.draw_ms = fm1_ms;
        am_draw();
    }
    return 1;
}
