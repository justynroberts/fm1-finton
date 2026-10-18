# Finton

**A six-track trance machine for the M-VAVE FM-1, after Rollwerk.**

KICK, 909, BASS, ROLL, LEAD and PAD play over Rollwerk's harmony: its 66-progression library in five moods
(Epic, Emotive, Melodic, Dark, Major), eight bars that can split at the half, sus and 7th chords, and its 16th
roller with figures, top notes, the bar-8 sus turn, the register across the phrase and the build over 8 bars. Every knob can be p-locked per step or
recorded as motion, and one knob, **LIFT**, runs the build and the drop. The screen is ORBIT: a bar is a revolution, the six tracks are sinusoidal rings whose ripples are their filter cutoff and whose breathing is their level, and a hand sweeps the notes; HOME's knobs perform the whole mix (FILTER, RES, SPACE, PUMP).

Everything is synthesised in fixed point on the FM-1's JieLi AC791N (no FPU): supersaws, the 909 kick and kit, a
ping-pong delay, a hall and a riser. Measured in the emulator, the whole mix uses about 30 % of the audio budget,
and 34 % at the top of a build.

Free and open source (GPL-3.0). Unofficial: this is not M-VAVE's firmware, and you install it at your own risk.

## The tracks

| Track | Sound | What the generator writes |
| --- | --- | --- |
| KICK | 909-style kick: swept sine, click, drive, boom tail, tone | four on the floor, sometimes a breath before the turnaround |
| 909 | closed and open hat, clap, snare, ride (metallic oscillator bank and noise) | offbeat open hats, 16th closed-hat grooves, claps on 2 and 4, a fill |
| BASS | saw/square with a sub, mono, slides | Rollwerk's ROLL, OFFBEAT or DRAG on the chord root |
| ROLL | 7-saw supersaw or pluck (Rollwerk VOICE) | Rollwerk's roller: figure AUTO, LIFT, 1-5-8, CLIMB or CYCLE over the chord tones, voice-led; ARP rerolls |
| LEAD | 7-saw supersaw, mono with slides | four bars of call and response that climb and land on the root |
| PAD | 7-saw supersaw triads, trance gate | a chord a bar, gated |

BASS, ROLL and LEAD are derived from the chords: change a chord, the key, the mode or the roller settings and they
are written again, unless you have edited that track by hand (ARP regenerates it either way). PAD voices the
chord sounding as it plays.

## Controls

| Control | Does |
| --- | --- |
| PLAY | start / stop (SCL+PLAY: from the top) |
| REC | arm: knob moves record as motion, keys record steps, LIFT writes its lane |
| HOME | ORBIT, with the PERFORM knobs; hold for ABOUT |
| SEQ | steps: white keys are the 16 steps, black keys pick tracks 1-6, then bar pages 1-4 |
| GLO | global, five pages: HARMONY (key, mode, mood, progression; the chord tiles), ROLLER (figure, top, sus turn, voice), BUILD (build, amount, register, register amount), GROOVE (bass, pump, drive, hall), ECHO+HALL (delay, feedback, tone, damp), SYSTEM (tempo, slot, motion) |
| EDIT / ENV / LFO / FX | the track's knob page: SOUND / SHAPE / FILTER / SEND (HOME returns to PERFORM) |
| ARP | generate this track again; on ROLL it is Rollwerk's reroll; on GLOBAL HARMONY a progression from any mood (SCL+ARP: a whole new song) |
| SAVE | save the song to the slot (while stopped); SCL+SAVE loads the slot |
| SCL | shift; held, it shows CPU load. SCL + a black track key mutes the track |
| SELECT | track; with a step held, its note; in GLOBAL, the selected bar's chord (white keys 1-8 pick the bar) |
| ALGORITHM | **LIFT**, the live build: filters open, a riser climbs, the snare rolls, the kick and bass fall away. Take it back to 0 from the top and the drop lands. The BUILD lane (Rollwerk's build over 8 bars) runs underneath on its own |
| PRESETS | **CUTOFF** of the selected track (ROLL when a drum track is selected), on every screen, recordable as motion with REC. **SCL+PRESETS: the track's sound presets** (below). With a step held: its length; in GLOBAL HARMONY: the bar's second-half chord (a split) |
| KNOB 1-4 | on HOME: **PERFORM** macros FILTER, RES, SPACE, PUMP over the whole mix (SCL+turn: neutral). On a track page: its four parameters; with a step held, a p-lock; SCL+turn clears that parameter's motion |
| keys (HOME) | play the selected track (909: the five voices on the first white keys) |
| OCT- / OCT+ | octave of the keys; in SEQ, the bar page 1-8 (on 909, black keys 7-11 pick the voice) |

In SEQ, tap a step to toggle it. Hold it to edit: SELECT changes the note, PRESETS the length, and the knobs set
p-locks. The bottom strip marks a locked value in the accent colour.

## Sound presets

SCL+PRESETS steps through the selected track's bank, and the strip shows the preset's name. A preset sets the
track's sixteen knobs; p-locks and recorded motion stay.

| Track | Presets |
| --- | --- |
| KICK | 909, HARD, LONG, PUNCH, DEEP |
| 909 | CLASSIC, TIGHT, OPEN, LOFI |
| BASS | ROLLER, SUB, ACID, REESE, PLUCKY, SQUARE |
| ROLL | SUPERSAW, PLUCK (Rollwerk's two voices), GLASS, HOOVER, ACID, CHIME, DEEP, SHIMMER |
| LEAD | SOAR, HOOVER, SAW, PIPE, BRIGHT, DARK |
| PAD | WARM, STRINGS, GLASS, DARK, GATED, AIR |

## Build

You need Python 3 with Pillow, Docker (on macOS the JieLi toolchain runs in a `linux/amd64` container), JieLi's
toolchain and three files from the AC79 SDK:

```sh
tools/get_toolchain.sh                    # ~/.jieli/toolchain
# the SDK: gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK at AC79NN_SDK_V1.2.1_2023-12-13 in ~/fw-AC79_AIoT_SDK
# (build.py checks the SHA-256 of uboot.boot, cfg_tool.bin and cfg/eq_cfg_hw.bin)
./build.sh                                # -> build/finton.fwsc (identity FM-1_950)
./build.sh --release 0.1                  # -> build/finton-0.1.fwsc
```

`python3 tools/gen_assets.py` regenerates `firmware/gen/apex_font.h` (Azeret Mono), `finton_logo.h` (the dog,
from `assets/logo.png`), `finton_harmony.h` (Rollwerk's vocabulary and progressions) and `apex_tables.h`.

## Try it without an FM-1

```sh
tests/run_host.sh                         # on this computer: build/host/finton.wav and two screens as PNG
tests/run_host.sh tests/demo.txt          # a scripted run (keys, buttons, encoders over time)
build/host/finton build/demo/f tests/demo.txt 30   # a film: a frame every 1/30 s, plus the WAV
FINTON_SOLO=4 build/host/finton build/host/lead      # one track alone
tools/emu.sh                              # the device build in fm1-emu: the AC791N emulated at 240 MHz
FM1_BENCH_KEYS="300:PLAY,400:PLAY^,6000:SCL" tools/emu.sh --shot 6500    # headless, a screenshot with the CPU load
```

`tools/emu.sh` expects [fm1-emu](https://github.com/joakimeriksson/fm1-emu) built next to this project
(`cargo build --release -p fm1-live`). In its window, A to ' play the keys, Space is PLAY, R is REC, 1-0 are the
other buttons, and the mouse turns the knobs.

## Web flasher

`web/` is a one-page installer in the Finton look (digital rain, the dog, a terminal log): connect, check, flash,
in Chrome or Edge over Web MIDI with SysEx. It speaks the same USB-MIDI update as `tools/fm1_install.py`
(`web/fm1ota.js` is a port of it, checked byte for byte against it), installs the firmware bundled next to it, or
any `.fwsc` you drop on it, and finishes an update that was cut off.

```sh
./build.sh --release 0.1 && tools/make_web.sh build/finton-0.1.fwsc   # web/firmware/finton.fwsc + manifest.json
python3 -m http.server -d web 8913                                    # http://localhost:8913
```

On GitHub, `.github/workflows/pages.yml` publishes `web/` to Pages on every push that touches it (Settings > Pages >
Source: GitHub Actions). Web MIDI needs https or localhost, which both are.

## Install

```sh
pip3 install mido python-rtmidi
python3 tools/fm1_install.py --info               # what the connected FM-1 runs now
python3 tools/fm1_install.py build/finton.fwsc      # asks before writing
```

To go back, use M-VAVE's updater or any FM-1 web installer's "return to official" option. If a firmware ever fails
to start, hold OCT- while powering on: that is the USB rescue mode, and the installer still works from there.

Finton keeps six song slots of 16 KB at flash 0xC8000-0xDFFFF, inside the area Felucca uses for its own projects. Going
back to Felucca after APEX may show that space as damaged projects there.

## Credits

Finton is APEX's engine and six tracks with Rollwerk's harmony and roller, in the Matrix's green. The platform under it (`platform.c`, `hal/`, `loader/`, `usb.c`, `ota.c`, `lcd.c`, `gfx.c`, `tools/` except
`gen_assets.py` and `emu.sh`) is [Dinghy](https://github.com/zvenson/dinghy) by zvenson, which is
[Felucca](https://github.com/hugelton/Felucca)'s platform by Leo Kuroshita (Hügelton Instruments) as
[SLOOP](https://github.com/isod89/sloop-fm1) (isod89) carries it. The type and the rain are M PLUS 1 Code (SIL OFL 1.1). The supersaw's
detune and mix curves follow Adam Szabo's measurements of the JP-8000. See `LICENSING.md`.

Made by [FintonLabs](https://fintonlabs.com).
