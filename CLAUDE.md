# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Finton is custom firmware for the M-VAVE FM-1 (JieLi AC791N, pi32v2 CPU at 240 MHz): a six-track trance machine: APEX's engine with the harmony and roller of the user's Rollwerk artifact, in a Matrix look (the dog logo excepted) (claude.ai/artifact/6wWoPZk3y1Dy2Uw3oJYSNU). `../apex/` is the predecessor, kept as is.
Siblings in `~/work/fm1/`: `dinghy-ref/` (the upstream template, read-only reference) and `fm1-emu/` (the emulator).

## Commands

```sh
tests/run_host.sh [script.txt]            # host build: firmware/src/finton.c on macOS -> build/host/finton.wav + PNGs
build/host/finton OUT script.txt 30         # a film: OUT-NNNNN.ppm every 1/30 s + OUT.wav (ffmpeg it)
FINTON_SOLO=n FINTON_SEED=n build/host/finton OUT   # one track alone / another generated song
python3 tests/stats.py build/host/finton.wav    # peak, rms, dc, stereo width, band split
../.venv-tools/bin/python tests/spectro.py in.wav out.png 4   # spectrogram (numpy lives in ../.venv-tools)
./build.sh                                # device build -> build/finton.fwsc (Docker runs the JieLi toolchain)
FM1_BENCH_KEYS="300:PLAY,400:PLAY^,6000:SCL" tools/emu.sh --shot 6500   # emulator; screenshot in ../fm1-emu/work/panel.ppm
python3 tools/gen_assets.py               # regenerate fonts (Azeret Mono), the logo, Rollwerk's harmony tables, curves
```

Host scripts (`tests/*.txt`) take one event per line: `T btn ID` (press and release), `T down ID` / `T up ID`
(hold a button, e.g. 3 = SCL), `T enc N CLICKS` (0 SELECT, 1 ALGORITHM = LIFT, 2-5 KNOB1-4, 6 PRESETS),
`T keys MASK`, `T midi ST D1 D2`, `T end`. `app_host_end()` in `ui.c` prints the song state the tests check.

The host build cannot measure CPU. The emulator can: hold SCL and the header shows the audio ISR's load. Keep it
under 50 %. It is about 33 % in normal play and 36 % at full LIFT with the BUILD lane high.

## Hard rules (the chip; `docs/RULES.md`)

- No `float`, `double` or 64-bit division: the link fails. `int64_t` multiply is fine. Constants come from
  `tools/gen_assets.py`.
- `./build.sh` refuses register access outside `firmware/hal/` and calls into ROM.
- Big buffers go in the pool with `POOL` (336 KB, about 283 KB used: framebuffer, delay, hall). RAM `.data+.bss` is 96 KB.
- In the audio ISR: no flash, no screen, no waiting. Main loop and ISR share state through bytes and volatile words
  with one writer each. Live notes go through the `live_q` ring.
- Flash: songs live at `SONG_AREA` 0xC8000 (6 x 16 KB, `platform.c` widens `FL_RANGE_OK`). An erase stalls audio
  for ~50 ms per sector, so the UI saves only while stopped.

## Architecture

`finton.c` includes everything in order, as one translation unit: `platform.c` (Dinghy: boot, USB, OTA, the audio
interrupt, the panel; on the host it includes `tests/host.c` instead) then `dsp.h`, `song.h`, `synth.c`,
`drums.c`, `fx.c`, `engine.c`, `gen.c`, `store.c`, `render.c`, `views.c`, `ui.c`.

- **Signal path (ISR, `engine.c:app_audio`):** the audio runs in control ticks of 32 frames. Each tick:
  - The sequencer clock advances `eng_pos` and fires steps (block-quantised to 0.7 ms).
  - Parameters slide toward the step's p-lock or the knob (`eff_q8`), then map to `tparam_t` and the kick/kit params.
  - LIFT is the ALGORITHM knob; BUILD follows the `song.lift` lane.
  - Each track renders into `tbuf`. The mixer applies the squared level, trance gate, kick sidechain duck and sends.
  - `fx_render` runs the ping-pong delay, the 4-line FDN hall and the riser. Then a DC blocker, drive and a soft
    clip, and the scope tap for the screen.
- **Voices:** `synth.c` has 8 voices partitioned per track (`TR_V0` and `TR_NV`), up to 7 saws each (`TR_NOSC`).
  Only the centre saw is polyBLEP'd. The TPT SVF runs in Q14 at half level so products fit 32 bits; use `mqs()`
  for filter outputs, never `mq15`.
- **Harmony (Rollwerk's):** `song.deg[8]`/`song.half[8]` index the mode's vocabulary (`finton_harmony.h`, generated
  from Rollwerk's tables in `tools/gen_assets.py`); `chord_at(step)` resolves root and quality. BASS, ROLL and LEAD
  hold MIDI notes derived by `gen.c`; `harmony_changed()` re-derives every track not flagged `edited`. PAD voices
  `chord_at()` in the ISR. `gen_roll()` is a line-by-line port of Rollwerk's `_d()`; keep it faithful.
- **BUILD vs LIFT:** the `song.lift` lane is Rollwerk's build over 8 bars (`gen_build`, smoothstep x amount): it opens
  ROLL's filter, raises its resonance and level, drives the riser. The ALGORITHM knob is LIFT, the performance
  build (also kicks/bass out, snare roll).
- **Generator (`gen.c`):** this is where the trance idioms live. Each track is seeded from `track.seed` and
  `song.seed`. ARP increments the track seed; SCL+ARP increments the song seed.
- **Screen:** `render.c` holds a full framebuffer, AA lines, convex fill, tiles, 4-bit M PLUS 1 Code text and `r_glyph` for the mirrored-katakana rain. `r_flush` sends only
  changed rows (a hash per row). `views.c` draws every view from scratch each frame (~30 ms cadence in
  `app_main`). The look is the Matrix (DESIGN.md): green ramp `K_N1..5`, white-hot `K_ACC`, red only for REC; HOME is ORBIT (`draw_home`: rings whose lobes are cutoff, a bar per revolution); the PERFORM page (`PG_PERFORM`) drives the `G_M*` macros the engine applies to the whole mix.

## Web flasher

`web/index.html` + `web/fm1ota.js` (a port of `tools/fm1_install.py`; keep the two in step, the wire format and
package checks were verified identical). `tools/make_web.sh` copies a build into `web/firmware/` with a manifest;
`.github/workflows/pages.yml` publishes `web/`. End-to-end test against a plugged-in FM-1: puppeteer-core with
`overridePermissions(origin, ["midi", "midi-sysex"])`, serve `web/` on localhost.

## Releasing

Identity `FM-1_95N` (`tools/build.py PRODUCT`). `./build.sh --release X.Y` gives `build/finton-X.Y.fwsc`. Credits to
Dinghy, Felucca and SLOOP stay in `LICENSING.md` and the file headers (GPL-3.0).
