# Licensing

Finton is free software under the GNU General Public License, version 3 only (`GPL-3.0-only`, full text in
`LICENSE`). You may use, study, change and share it. If you give someone a firmware built from it, give them its
complete source under the same licence too.

## Who made what

| | |
| --- | --- |
| The platform: `firmware/hal/`, `firmware/loader/`, `crt0.S`, `app.ld`, `firmware/src/` (`usb.c`, `ota.c`, `recovery.c`, `lcd.c`, `gfx.c`, `libc.c`, `bootguard.h`, `uac_fir.h`), `tools/` | Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments ([Felucca](https://github.com/hugelton/Felucca)), with the changes of [SLOOP](https://github.com/isod89/sloop-fm1) (isod89) |
| The wiring `platform.c`, the host mode `tests/host.c`, `tests/run_host.sh`, `docs/API.md`, `docs/RULES.md` (Dinghy) | zvenson |
| Finton (from APEX): `firmware/src/finton.c`, `dsp.h`, `song.h`, `synth.c`, `drums.c`, `fx.c`, `engine.c`, `gen.c`, `store.c`, `render.c`, `views.c`, `ui.c`, `tools/gen_assets.py`, `tools/emu.sh`, `tests/*.txt`, `tests/stats.py`, `tests/spectro.py`, and the APEX changes to `platform.c` and `tests/host.c` | Copyright (C) 2026 FintonLabs |

## Third-party material

| What | Licence | Where |
| --- | --- | --- |
| Terminus font 8x16 (ter-u16n), rendered into `firmware/gen/felucca_font.h` (the platform's crash and update screens) | SIL OFL 1.1 | `assets/fonts/Terminus-LICENSE.txt` |
| M PLUS 1 Code (variable), rendered into `firmware/gen/apex_font.h` (type and rain glyphs) | SIL OFL 1.1 | `assets/fonts/MPLUS1Code-OFL.txt` |
| The Finton dog, `assets/logo.png`, rendered into `firmware/gen/finton_logo.h` | (C) FintonLabs, all rights reserved, not under the GPL | |
| Rollwerk's chord vocabulary, progression library and roller algorithm (ported into `tools/gen_assets.py` and `firmware/src/gen.c`) | (C) FintonLabs | the Rollwerk artifact |
| JieLi AC79 SDK: `uboot.boot`, `cfg_tool.bin`, `eq_cfg_hw.bin` are read from your SDK checkout at build time and placed in the package; no SDK files are in this tree | Apache-2.0 | <https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK> |

## Trademarks

"Felucca" and "Hügelton Instruments" are names of Hügelton Instruments; "M-VAVE" and "FM-1" belong to their
owners. Finton is not made or endorsed by them.
