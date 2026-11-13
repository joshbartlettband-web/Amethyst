# Amethyst: build hand-off

For the next session (any model). Read this top to bottom before touching code. Then read `WRITING.md` (how to
write for Josh) and skim `docs/AMETHYST.md` (the plan) and `firmware/src/amethyst.c` (all of Amethyst, about 530 lines).

Do the steps in section 6 in order, one commit each. After every step: run the native check (section 3), look at the
screens, push, and see CI go green (section 4). Do not batch steps.

---

## 1. What exists

Amethyst is a drum and bass toy for the M-VAVE FM-1, by Josh, concept and look by kipatchi (always "kipatchi", no
other name). It sits on Melodee 1.6.1 (Kerem Kilic) the way Double Rainbow's `dr.c` does.

- Repo: `joshbartlettband-web/Amethyst`, branch `main`. Local clone: `/home/user/amethyst`. Push straight to `main`.
- Test page: https://joshbartlettband-web.github.io/Amethyst/webapp/try/ (CI publishes it on every push, about 3 min).
- Do not link Amethyst from Josh's other projects. Josh hasn't cleared that yet.

Two stages are built, FRIDAY at The End and DARKSIDE at Turnmills (`AM_STAGES[]`). On either:

| Control | Now |
| --- | --- |
| White keys F3..E4 (key index 0..11) | Reese bass, track 1 (`AM_TBASS` 0), Prophet program REESE, legato, -12 |
| White keys F4..G5 (index 12..26) | Sevenths of A minor, track 2 (`AM_TCHORD` 1), Prophet FOG PAD, Melodee chord keys DIA7 |
| Black keys (11) | Hold a slice of the break (two 16ths repeating); release returns to where the bar would be |
| FX held | BUILD: SLOOP loops 1/8, 1/16, 1/32, four beats each, bass muted, a meter on screen; release: drop on the next beat |
| SCL | REWIND when the crowd calls (after a build of a bar or more); 3 rewinds cut the VIP break |
| ENV held | SLOOP half speed |
| LFO held | Tape stop (`PX_STOP`); let go and the tune carries on |
| EDIT held | Break muted while held |
| GLO held | Dub siren on the high white keys, track 3 (`AM_TSIREN` 2), Prophet program SIREN |
| SEQ | Break mute toggle |
| ARP | Key layout: bass and chords / all bass / all chords (`am_seq.layout`) |
| HOME tap | Sleeve notes on and off |
| REC tap | Four bars of the side last played (bass or chords) into its track, by Melodee's recorder (`rec_set`): waits for the track's 64-step loop to come round, one pass, then loops. Again: a layer. During a take: that side wiped |
| REC held | The tape pack: stages played, the clock, rewinds, VIPs cut |
| PLAY | Start / stop |
| OCT- / OCT+ | Bass octave |
| SELECT | Tempo 140..180 |
| ALGORITHM | The break: ROLLER, CHOPPED, HALF TIME, TWO-STEP (`AM_BREAKS[]`), loaded on the next bar |
| PRESETS | The stage: its photo, tempo, starting break, bass and sleeve notes; VIP and moss start over |
| K1..K4 | WOBBLE, FOG, GRIT, MOSS (0..100): each changes the sound and the picture, a bar on screen while it turns |
| HOME + SAVE 3 s | Leave to full Melodee (`am_leave`) |

The break is track 4 (`AM_TBREAK` 3): DRUM engine, 909 kit, 32 steps, patterns in `AM_BREAKS[]` (four and the roller's VIP).

## 2. How it is wired (the only Melodee files touched)

- `firmware/src/amethyst.c`: everything. Included after `ui_layer.c` in `melodee.c` and in `tests/ui_test.c`.
- `firmware/src/amethyst_seq.h`: the `am_seq` struct shared with the audio interrupt. Included in `seq.c`.
- `seq.c`, three hooks: the include; in `keyboard_block` (key press: black key sets `am_seq.chop`, white keys set
  `kb_trk[k]`; key release clears the chop); in the step advance, `am_seq_step(t, starting, len)`.
- `main.c`: `am_boot()` after `project_boot()`; `am_frame()` replaces `ui_input/ui_leds/ui_draw` while `am.on`.
- `web/emu/melodee_web.c`: the same two calls for the browser build.
- `tools/build.py`: runs `tools/amethyst/gen_am_art.py` to make `build/gen/am_art.h` (the pictures).

Keep it that way. New Amethyst code goes in `amethyst.c` / `amethyst_seq.h`. New hooks in Melodee files must be
tiny and commented "Amethyst". Never copy Kerem's or Leo's copyright lines into new files; never remove theirs.

Two contexts, don't mix them up:
- **Main loop** (`am_frame`, `am_poll`, `am_draw`): buttons, encoders, screen, timers in ms (`fm1_ms`).
- **Audio interrupt** (`am_seq_step`, the `seq.c` hooks): must be short, no drawing, no loads. Talk between the
  two with single bytes/words in `am_seq` (declared `volatile`).

## 3. Build and check on this machine (fast, do this constantly)

```sh
cd /home/user/amethyst
pip install -q fonttools Pillow                       # once
python3 -c "import sys; sys.path.insert(0,'tools'); import build; build.generate()"   # once, and after art changes
python3 tools/gen_909_tables.py build/gen/x0x_drum_tables.h                            # once
mkdir -p build/host
cc -O1 -std=gnu99 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/am_drive tests/am_drive.c -lm
OUT=<your scratchpad>/am; mkdir -p $OUT; build/host/am_drive $OUT
```

`tests/am_drive.c` boots Amethyst and plays a script (keys, FX holds, SCL, LFO, a knob), prints a line per second
(level, state, message) and a last line `rewinds 3, vip 1, ...`, and writes `shot-*.ppm` screens and `amethyst.wav`.
Turn the PPMs into one PNG with PIL and **look at it** with the Read tool. Extend the script when you add a feature
(helpers: `key(k,on)`, `btn(B_X,on)`, `tap(B_X)`, `run_ms(ms)`, `shot(dir,name)`, `host_enc[panel.enc[EN_K1]] += n`).

To see warnings in your own code, compile once with `-Wall` instead of `-w` and grep the output for `amethyst`.

Do **not** install or run Emscripten or the JieLi toolchain here (blocked, and CI does it).

## 4. CI and limits

Every push to `main` runs `.github/workflows/amethyst.yml`: `firmware` (real FM-1 build), `browser`, `publish`.
Check with the GitHub MCP tools (`mcp__github__actions_list` `list_workflow_runs`, then `list_workflow_jobs`; logs
with `mcp__github__get_job_logs`, `return_content: true`). **The firmware job must be green**, not only browser.
The line to read in its log:

`ok    image 565628 B; RAM .data+.bss 97208 B of 98304; pool 136016 B of 357888`

and `resources: N B at 0xcb020 (X of 21 sectors)`.

Hard limits (the native build won't warn you; only CI's link does):

- **Flash:** image must stay under 581,564 B. About 16 KB free. Code is expensive; keep it small.
- **.bss:** about 1 KB free. Put **all** Amethyst state in the pool: `static ... AM_POOL;`. No plain `static` buffers.
- **Pool:** Melodee needs 49,152 B of it free for audio ("audio working arena below worst-case budget" is the
  link error). Keep Amethyst's pool use to a few KB. Never unpack a whole picture; unpack 16-row bands as drawn
  (see `am_draw`).
- **Resource region (pictures):** 21 sectors of 4 KB = 86 KB, shared with Melodee's data. FRIDAY's photo is 20.8 KB
  and the moss 2.4 KB. Before adding a second photo, read the `resources:` line; if it won't fit, do step 6 first.

## 5. Traps already hit (don't repeat)

1. **SLOOP effects need the ring.** `punch.req = PX_...` does nothing unless `punch.keybit` is set. Amethyst keeps
   `punch.keybit = AM_PKEY` while on (`am_poll` re-asserts it). Never set `punch.keybit = 0` except in `am_leave`.
   Effects replay only the last 0.37 s, so they are momentary: latching one (HALF) goes quiet.
2. **Times.** Start times are stored as `fm1_ms | 1` (0 means never). Always read elapsed time with
   `am_since(t0)`. A bare `fm1_ms - t0` can underflow and fire at once.
3. **Releases.** The host's `fm1_input_edges(0)` returns presses only. Releases come from
   `am.held & ~fm1_in.buttons` (already in `am_frame`). Use `pressed` / `released` / `fm1_in.buttons`.
4. **Chord and bass tracks** are set up in `am_setup` under `fm1_irq_off()`. Loading engines/presets
   (`set_engine_of`, `apply_preset_to`) happens in the main loop, wrapped in `undo_depth++ / --`.
5. **Prophet programs** are our own (`AM_REESE`, `AM_PAD` field lists over `p5_patch_init`, then
   `p5_track_accept`). Do not use Sequential's or Casio's factory presets for Amethyst's sounds.
6. **Drawing:** the screen is drawn in bands (`cv_begin_band(240,240,0,band)` loop, `cv_blit_band(0,0)`), pixels are
   byte-swapped RGB565 (`swap16(RGB(r,g,b))` when writing `cv_px` directly; `cv_rect/cv_text*/cv_line/cv_disc` take
   plain `RGB`). Text faces: `TY_LB` 16, `TY_TX` 20, `TY_VL` 28, `TY_HR` 48 px; center with `cv_text_c`, vertical
   with `ty_y(center_y, face)`.
7. In test `printf`s, don't put a call that advances time and a state read in the same argument list.

## 6. The work, in order

Decided by Josh: top row buttons are **held** (let go and the music is back; SCL is the one tap), bottom row are
**taps**; the drop lands on the **next beat**. Knob looks follow `docs/amethyst/knobs/sheet.png`
(`tools/amethyst/knob_looks.py` shows the math). Josh hasn't said whether GRIT at 100 is too much: build it so the
strongest setting is about the sketch's 70, and say so.

### Step 1. Build and drop
- Count the build in beats, in the interrupt: add to `am_seq` a step counter that `am_seq_step` increments while
  building (16ths). Loop length by beats held: under 4 beats `PX_LOOP8`, under 8 `PX_LOOP16`, then `PX_LOOP32`.
  Change `punch.req` only on a beat (`ghost % 4 == 0`).
- Drop on the next beat: in `am_seq_step`, when `am_seq.drop` and `am_seq.ghost % 4 == 0`: clear the punch, unmute
  the bass, set `am_seq.ghost = 0` and `t->seq_idx = 0` so the break starts from its first step, set `dropped`.
- "Built long enough for a rewind" becomes 4 beats or more (use the counter, not ms).
- Screen: while building, a meter across the bottom (three segments: 1/8, 1/16, 1/32, filling by beat) and
  `LET GO TO DROP`. If FX is pressed while stopped, say `PRESS PLAY FIRST`.
- Check: script a 1-beat, a 5-beat and a 9-beat build; assert the stage reached, that `call` follows only the
  long ones, and that the drop comes within one beat (about 345 ms at 174) of release.

### Step 2. Knobs: look and sound
In `am_draw`, per row, all cheap:
- **WOBBLE** (K1): shift each row sideways, `dx = knob/100 * 12 * sin(y/13 + phase) * (0.4 + 0.6*y/240)`, phase moving
  with the beat. Use a small sine table (uint8/int8, in flash), not `sinf`. Sound: make the bass filter really
  move. Look in `eng_prophet_test.c` for how `P5_LFO_RATE`, `P5_LFO_TRI`, `P5_WHEEL_FILTER`, `P5_LFO_INITIAL` drive
  the filter; set them in the patch and scale the amount with the knob. If the LFO can't reach the filter that
  way, modulate `trk[AM_TBASS].p[P_E0]` from `am_poll` with a triangle in time with the beat (cheap, works).
- **FOG** (K2): per row, mix the 16 palette colors toward (176,166,204), strongest at the bottom, a slow drifting
  band. Build the row's palette once per row or per 4 rows. Sound: find Melodee's shared reverb (grep `fx.c`,
  `mix.c`, `ui_layer.c` for the rack and track sends) and raise the chord and break sends with the knob. If that
  takes more than a small change, keep today's darkening and write down what you found.
- **GRIT** (K3): draw in blocks (repeat a pixel 1, 2 or 3 times in x and y), drop color bits, a few torn rows
  (row shifted by a hashed offset), sparse inverted specks (hash of x, y, frame). Sound: keep drive, add what the
  DRUM engine offers.
- **MOSS** (K4): moss reach = `max(knob-driven reach, the night's slow growth)`, palette tinted green by the knob.
  `am.moss` runs 0..280 against `am_reach()`.
- While a knob turns, show its name and a bar for 1.5 s (the bottom line).
- Check: shots at 0 and 100 for each knob; they must be plainly different from each other.

### Step 3. The rest of the top row and bottom row
Done, as are steps 1 and 2 (step 2 twice: Josh found the first knob looks overcooked, so GRIT is grain, blocks
and a few torn rows now, FOG is per row, WOBBLE sways 4 px at most). One trap found: Melodee's `kb_mask` and
`punch_panel_mask` make a key pressed with a button held silent; `am_poll` clears them every pass.

Steps 4 and 6 are done too. Step 6 came first, because the second photo needed its room: `AMETHYST_NO_FACTORY`
in `core.h` empties both banks. DARKSIDE's bass is a darker Prophet Reese (`AM_DREESE`), not the CZ-1 patch the
plan asks for; voice that once Josh has listened. Melodee's tests that count 200 programs and 64 tones fail with
the flag on (CI doesn't run them).

Step 5 is built except saving: REC, the tape pack, and the shout-outs (a phone in the corner every 16 bars and
after a rewind, `AM_SHOUTS`). SAVE does nothing yet. Nothing persists: the knobs, the loops and the VIPs are gone
at power-off. The settings record (`settings_persist.c`) has no spare words left, the browser build has no flash,
and nobody has a device to check a flash write on, so ask Josh what should be kept and wait for hardware.
- **LFO held**: `PX_STOP` while held; on release clear it and carry on (no transport stop). Remove the tap logic.
- **EDIT held**: break muted while held (`trk[AM_TBREAK].p[P_MUTE]`), restore on release. SEQ stays the latch.
- **GLO held**: dub siren. Simplest: track 3 (index 2, currently muted) gets its own Prophet program (pulse,
  LFO on pitch); while GLO is held the high white keys play track 3 instead of chords (one more flag in `am_seq`,
  one more line in the `keyboard_block` hook). Unmute/mute track 3 with the button.
- **ARP tap**: key layout cycles BASS+CHORDS / ALL BASS / ALL CHORDS (a mode byte in `am_seq` read by the hook).
- **HOME tap**: sleeve notes on/off: a dark panel with 4 to 6 short lines (`TY_LB`). FRIDAY's text: Fabric opened
  in Farringdon in 1999 and kept Fridays for drum and bass; DJ Hype's Playaz; May 2001, Dillinja and Lemon D's
  Valve sound system, 52 subs; The End, West Central Street, closed 2009. Photo: steve_w, CC BY 2.0, changed with
  Gemini. Keep HOME+SAVE 3 s as the way out.
- LEDs: every button that does something glows dim (`USED[]` in `am_leds`), lit while active.
- Update the browser page text in `web/emu/index.html` and the README's control list to match.

### Step 4. Break patterns and a second stage
- **ALGORITHM**: break pattern, a detent each: ROLLER, CHOPPED, HALF TIME, TWO-STEP (new `am_break_t` rows,
  our own patterns; HALF TIME = kick on 1, snare on 3 of the bar). Load on the next bar.
- **PRESETS**: stages. Make a `am_stage_t` table (name, sub-label, picture data pointers, patterns, bpm, bass/chord
  programs). Second stage: DARKSIDE at TURNMILLS (photo `docs/amethyst/gemini/turnmills-moss.jpg`, public domain
  source, 165 BPM, CZ-1 Reese if you can voice one, else Prophet). Add it to `STAGES` in `gen_am_art.py`.
  Check the resource region first (section 4).

### Step 5. REC, shout-outs, saving
See `docs/AMETHYST.md` section 3. Ask Josh before starting this step; it is the least settled.

### Step 6. Factory sounds out
Amethyst must not ship Sequential's 200 Prophet programs or Casio's 64 CZ tones (`P5_FPACK`,
`CZ_LZ`; `tools/gen_prophet_factory.py`, `tools/gen_cz1_factory.py`). Add a build flag (say `AMETHYST_NO_FACTORY`)
that makes both banks empty (`P5_FACTORY_N` and `CZ_FACTORY_N` small or zero with INIT only), check every user
(`grep -n "P5_FACTORY_N\|p5_factory(\|CZ_FACTORY_N\|cz_factory(" firmware/src/*.c`), keep Melodee's tests
compiling, and note the bytes freed from the CI log. `build.py` runs `gen_prophet_factory.py --web check`; keep
that passing or skip it under the flag.

## 7. Not yet known

- Nobody has run this on a real FM-1. The whole screen is redrawn every 40 ms; on the device that may be too
  slow. If Josh reports stutter: redraw only when something changed, and pulse fewer rows.
- How it sounds is Josh's call. He said the first build "sounds alright". Ask him to listen after steps 1 and 2.
- The 32 KB SLOOP ring is now held all the time; on the device the audio arena may run short with 3 tracks.
  If effects go silent on hardware, that's the first suspect (`punch_memory_failed`).

## 8. Reporting to Josh

Short, plain, per `WRITING.md`. Say what was built, what you checked and how, what you didn't, and the link. Report
failures as failures. Commit messages in the same voice, ending with the attribution lines your session gives you.
