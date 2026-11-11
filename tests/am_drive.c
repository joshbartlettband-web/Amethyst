/* SPDX-License-Identifier: GPL-3.0-only */
/* Amethyst played on the computer: the browser build's code (web/emu/melodee_web.c) natively, driven along a script
 * of keys and buttons like a person would, the sound into a WAV and the screen into PPMs at chosen moments.
 *   cc -O1 -std=gnu99 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/am_drive tests/am_drive.c -lm
 *   build/host/am_drive OUTDIR      -> OUTDIR/amethyst.wav, OUTDIR/shot-*.ppm, a line per second of levels */
#include "../web/emu/melodee_web.c"
#include <stdio.h>
#include <math.h>

static FILE *wav;
static uint32_t wav_frames;
static double sec_sum, sec_peak;

static void wav_header(FILE *f, uint32_t n)
{
    uint32_t rate = FS, bytes = n * 4u, v;
    fseek(f, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, f); v = 36u + bytes; fwrite(&v, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f); v = 16; fwrite(&v, 4, 1, f);
    uint16_t h[2] = {1, 2}; fwrite(h, 2, 2, f);
    fwrite(&rate, 4, 1, f); v = rate * 4u; fwrite(&v, 4, 1, f);
    uint16_t a[2] = {4, 16}; fwrite(a, 2, 2, f);
    fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
}

static void run_ms(uint32_t ms)                        /* the device for ms milliseconds, its sound into the WAV */
{
    uint32_t n = (uint32_t)((uint64_t)ms * FS / 1000u), k;
    while (n) {
        uint32_t c = n > 512u ? 512u : n;
        web_render(c);
        for (k = 0; k < c; k++) {
            float l = out_l[k], r = out_r[k];
            int16_t s[2] = {(int16_t)fmaxf(-32767, fminf(32767, l * 32767.0f)), (int16_t)fmaxf(-32767, fminf(32767, r * 32767.0f))};
            fwrite(s, 2, 2, wav);
            sec_sum += (double)l * l;
            if (fabs(l) > sec_peak) sec_peak = fabs(l);
            if (++wav_frames % FS == 0u) {
                printf("%3us  rms %5.1f dB  peak %5.1f dB  %s%s%s\n", wav_frames / FS, 10 * log10(sec_sum / FS + 1e-12),
                       20 * log10(sec_peak + 1e-12), am.building ? "BUILD " : "", am.call ? "CALL " : "", am.msg);
                sec_sum = sec_peak = 0;
            }
        }
        n -= c;
    }
}

static void shot(const char *dir, const char *name)
{
    char p[512];
    FILE *f;
    uint32_t i;
    snprintf(p, sizeof p, "%s/shot-%s.ppm", dir, name);
    f = fopen(p, "wb");
    fprintf(f, "P6\n240 240\n255\n");
    for (i = 0; i < 240u * 240u; i++) {
        uint16_t c = host_screen[i];
        c = (uint16_t)((c >> 8) | (c << 8));            /* (the panel's byte order) */
        uint8_t rgb[3] = {(uint8_t)(((c >> 11) & 31u) * 255u / 31u), (uint8_t)(((c >> 5) & 63u) * 255u / 63u),
                          (uint8_t)((c & 31u) * 255u / 31u)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

static void key(uint32_t k, int on) { web_keys(on ? fm1_in.notes | 1u << k : fm1_in.notes & ~(1u << k)); }
static void btn(uint32_t b, int on)
{
    uint32_t m = 1u << panel.btn[b];
    web_buttons(on ? fm1_in.buttons | m : fm1_in.buttons & ~m);
}
static void tap(uint32_t b) { btn(b, 1); run_ms(60); btn(b, 0); }

/* hold FX for n beats at the current tempo, then let go, watching how long the drop takes: step 1's check */
static uint32_t beat_ms(void) { return (uint32_t)(60000.0 / (song.g[G_BPM] > 40 ? song.g[G_BPM] : 174)); }
static void build_beats(const char *dir, const char *tag, uint32_t beats)
{
    uint32_t prev_drop, waited = 0;
    char mid[64];
    btn(B_FX, 1);
    run_ms(beats * beat_ms() / 2u);
    snprintf(mid, sizeof mid, "%s-mid", tag);
    shot(dir, mid);                                     /* the meter, partway through the hold */
    run_ms(beats * beat_ms() / 2u + beat_ms() / 2u);    /* land safely past the beat boundary */
    prev_drop = am.drop_t0;
    btn(B_FX, 0);
    while (am.drop_t0 == prev_drop && waited < 2u * beat_ms()) { run_ms(5); waited += 5; }
    printf("build %2u beats (%s): drop %u ms after release (a beat is %u ms), call %u, rewinds %u\n",
           beats, tag, waited, beat_ms(), am.call, am.rewinds);
    shot(dir, tag);
    run_ms(8u * beat_ms());                              /* let any rewind call time out before the next build */
}

__attribute__((constructor)) static void am_drive(int argc, char **argv)   /* (before ui_test.c's main: it exits) */
{
    const char *dir = argc > 1 ? argv[1] : ".";
    char p[512];
    uint32_t round;
    snprintf(p, sizeof p, "%s/amethyst.wav", dir);
    wav = fopen(p, "wb");
    wav_header(wav, 0);
    web_boot(800);
    run_ms(1500); shot(dir, "0-boot");
    key(4, 1); run_ms(500);                            /* A3: the Reese */
    key(16, 1); run_ms(700); shot(dir, "1-bass-chord");   /* A4: a chord (Am7) */
    key(16, 0); key(4, 0); key(2, 1); run_ms(300); key(2, 0); key(7, 1); run_ms(400); key(7, 0);   /* G3, C4 */
    key(1, 1); run_ms(220); shot(dir, "2-chop"); run_ms(400); key(1, 0);   /* F#3: slice 1 held */
    key(8, 1); run_ms(350); key(8, 0); run_ms(500);    /* C#4: slice 4 */
    build_beats(dir, "1beat", 1);                      /* step 1: short builds don't earn a rewind */
    build_beats(dir, "5beats", 5);                     /* long ones (4+) do */
    build_beats(dir, "9beats", 9);                     /* and reach LOOP32 (beats 8+) */
    for (round = 0; round < 3u; round++) {             /* three more builds, three rewinds: the VIP */
        btn(B_FX, 1); run_ms(2900);
        if (!round) shot(dir, "3-build");
        btn(B_FX, 0); run_ms(1600);
        if (!round) shot(dir, "4-call");
        tap(B_SCL); run_ms(250);
        if (!round) shot(dir, "5-rewind");
        run_ms(900);
    }
    shot(dir, "6-vip");
    key(4, 1); run_ms(1500); key(4, 0);
    tap(B_LFO); run_ms(1400); shot(dir, "7-stopped");
    tap(B_LFO); run_ms(1500);
    { uint32_t i; for (i = 0; i < 30u; i++) { host_enc[panel.enc[EN_K4]] += 1; run_ms(30); } }   /* MOSS up */
    run_ms(4000); shot(dir, "8-moss");
    wav_header(wav, wav_frames);
    fclose(wav);
    printf("rewinds %u, vip %u, moss %u, playing %u\n", am.rewinds, am.vip, am.moss, song.playing);
    exit(0);
}
