# Finton: design record

Read this before changing the look.

## The brief

The music is Rollwerk's (the user's artifact, claude.ai/artifact/6wWoPZk3y1Dy2Uw3oJYSNU). The look is **the
Matrix**, by the user's direction, with one exception: the dog logo keeps its own colours. That direction
overrides the `distinctive-ui` defaults. The house identity rules still apply: the FintonLabs credit on the
ABOUT screen, and reduced motion through MOVE CALM.

## The look

| Element | Choice |
| --- | --- |
| Ground | black with the faintest green, `K_BG` 0 6 2 |
| Ramp | phosphor green: `K_N1` 0 40 14 for hairlines, through to `K_N5` 0 255 65 (#00FF41) for values |
| Accent | the white-hot head of the code, `K_ACC` 200 255 210: what is selected, and what sounds now |
| Red pill | `K_RED`, used only for the REC light |
| Type | M PLUS 1 Code (SIL OFL), one family, in 4-bit antialiased cuts on 6x12, 8x16 and 14x28 cells |
| Rain | M PLUS 1 Code's katakana with numerals, mirrored as the film's code is, in 8x12 cells (`F_RAIN`, 62 glyphs): the boot splash |
| Panels | thin green borders on the chord tiles and slot tiles; the knob strip is a terminal row with the value under its label |

## HOME: ORBIT, a living cell

A bar is a turn. The six tracks are **membranes**, KICK inside and PAD outside, and none of it is mechanical:

- **Shape:** each membrane is a blend of slowly drifting waves at uneven speeds. Its ripples run bigger on one side
  than the other, and its centre wanders a couple of pixels, so the rings float like nested cells or tree rings.
- **Cutoff (and the FILTER macro):** sets the ripple count, and morphs smoothly between counts rather than snapping.
  Resonance sharpens the ripples, and the live level plus LIFT make the membrane breathe.
- **Colour:** each membrane has its own tint, teal inside to lime outside, and heats toward white with its level.
- **The beat:** no hand or dial. It is a swell of light travelling round every membrane, lifting it as it passes.
- **Notes:** they ride the moving membrane at their step, and the one playing blooms outward as a fading ripple.
- **Spores:** particles drift and curl out of the nucleus on every kick and from the selected track's notes. The
  drop bursts them.
- **Noise:** a slowly evolving value-noise field (`vnoise`) roughens every membrane and patches its brightness, so
  no two rings and no two moments match. The spacing between rings drifts too.
- **Strokes:** thin where a membrane is quiet, thick where it swells.
- **Cytoplasm:** translucent fills on the outer and middle membranes give a soft depth.
- **Cilia:** hairs bristle round the outer membrane, waving in a travelling wave and growing with the level and LIFT.
- **Organelles:** five small bodies drift between the membranes.
- **The nucleus:** three soft layers, brighter inward, fed by LIFT (more lobes, white past 96). It collapses at the
  drop.

**Rendering quality:**
- **Phosphor afterglow:** the scene fades by 150/256 a frame instead of clearing, so motion leaves CRT-like trails.
  CALM gives clean frames.
- **Strokes:** every ring and tick is a glowing stroke (a quarter-strength halo either side of a crisp
  antialiased core) on 96 points.
- **Selected ring:** filled as a translucent flower with a seamless even-odd scanline fill (`r_shape`).
- **Colour:** heat from the level, dark green through phosphor to white-hot.
- **Notes:** round antialiased discs; the sounding one gets a halo.
- **Text:** a one-pixel shadow instead of a box.
- **Speed:** about 31 fps at full LIFT in the emulator (SCL shows CPU and FPS).

HOME's knobs are **PERFORM**: FILTER, RES, SPACE and PUMP, macros over the whole mix (SCL+turn resets one), and
the touched macro shows large at the bottom left. Track pages stay on EDIT, ENV, LFO and FX; HOME brings PERFORM
back.

## The other screens

- **STEPS, GLOBAL and ABOUT:** they keep the Rollwerk-derived layouts (chord tiles, pattern bars, register boxes,
  build curve, pump curve), now in phosphor green.
- **The splash (kept):** a curtain of code falls behind the dog while `wake up, finton...` types itself out with a
  blinking cursor. It shows for 2.6 s, or until any press, and the press still counts.
