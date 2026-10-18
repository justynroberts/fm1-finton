#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Run the device build in fm1-emu (https://github.com/joakimeriksson/fm1-emu), the AC791N emulated at 240 MHz.
#   tools/emu.sh                  a window: play it with the computer keyboard / mouse / MIDI
#   tools/emu.sh --shot MS        headless: a screenshot of the panel at MS (FM1_BENCH_KEYS scripts input)
#   tools/emu.sh --bench MS       headless: how fast, and FM1_BENCH_WAV=out.wav records the audio
# FM1_EMU: the fm1-emu checkout (default ../fm1-emu next to this project)
set -e
cd "$(dirname "$0")/.."
EMU="${FM1_EMU:-$(cd .. && pwd)/fm1-emu}"
python3 -c "import sys; d=open('build/finton.bin','rb').read(); open('build/finton.emu.bin','wb').write(b'\xff'*0x120+d)"
cd "$EMU"
exec ./target/release/fm1-live --bin "$OLDPWD/build/finton.emu.bin" "$@"
