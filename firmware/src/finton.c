/* SPDX-License-Identifier: GPL-3.0-only */
/* Finton: a six-track trance machine for the M-VAVE FM-1, after Rollwerk.
 *
 *   KICK  909   BASS  ROLL  LEAD  PAD
 *
 * Six tracks over Rollwerk's harmony (its progression library by mood, split bars, sus and 7th chords)
 * and its roller, bass styles and eight-bar build; every knob p-lockable and recordable as motion; one
 * knob, LIFT, for the build and the drop. The screen is APEX's horizon in Rollwerk's colours.
 *
 * Built on Dinghy's platform (platform.c: boot, USB, updates, the audio interrupt, the panel), itself
 * Felucca's and SLOOP's. Everything above it is Finton's (from APEX):
 *   dsp.h     fixed-point primitives          song.h    the song and its harmony
 *   synth.c   the supersaw voice              drums.c   the 909 kick and kit
 *   fx.c      delay, hall, riser              engine.c  the sequencer and app_audio()
 *   gen.c     the trance generator            store.c   song slots in flash
 *   presets.c the sound banks, a bank a track
 *   render.c  the framebuffer                 views.c   the screens
 *   ui.c      the controls and app_main()
 * Integers only: the chip has no FPU and no 64-bit division (docs/RULES.md). */
#define APP_NAME "FINTON"
#define LCD_BAUD 1u                             /* 30 MHz SPI (Felucca 12; X0X runs 60 on hardware) */
#include "platform.c"
#include "apex_tables.h"
#include "apex_font.h"
#include "finton_logo.h"
#include "dsp.h"
#include "song.h"
#include "synth.c"
#include "drums.c"
#include "fx.c"
#include "engine.c"
#include "presets.c"
#include "gen.c"
#include "store.c"
#include "render.c"
#include "views.c"
#include "ui.c"
