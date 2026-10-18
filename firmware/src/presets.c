/* SPDX-License-Identifier: GPL-3.0-only */
/* Finton sound presets: a bank of named patches for each track, the sixteen knobs at once.
 * SCL + PRESETS browses the selected track's bank; GLOBAL ROLLER VOICE picks ROLL's first two
 * (Rollwerk's SUPERSAW and PLUCK). A preset sets the knobs; p-locks and recorded motion stay. */

typedef struct { const char *name; uint8_t p[NPAR]; } preset_t;

/*                                  sound            shape              filter             send
 * melodic:                         WAVE WIDTH SUB OCT  ATK DEC SUS REL   CUT RES ENV GATE    DLY VERB PUMP LVL
 * kick:                            TUNE PITCH SWEEP DECAY CLICK DRIVE TONE BOOM PUMP.T - - -  DLY VERB - LVL
 * 909:                             CH.D OH.D HAT.T RIDE.D CLP.D CLP.T SNR.T SNAP CH OH CLAP SNR DLY VERB RIDE LVL */
static const preset_t PRE_KICK[] = {
    {"909", {60, 70, 50, 64, 60, 40, 90, 30, 60, 0, 0, 0, 0, 6, 0, 120}},
    {"HARD", {70, 90, 30, 50, 100, 90, 110, 20, 50, 0, 0, 0, 0, 0, 0, 120}},
    {"LONG", {45, 60, 60, 110, 40, 40, 80, 90, 80, 0, 0, 0, 0, 10, 0, 116}},
    {"PUNCH", {80, 100, 25, 40, 90, 60, 100, 10, 40, 0, 0, 0, 0, 0, 0, 120}},
    {"DEEP", {30, 50, 70, 90, 20, 30, 60, 80, 70, 0, 0, 0, 0, 0, 0, 124}},
};
static const preset_t PRE_909[] = {
    {"CLASSIC", {30, 60, 64, 70, 50, 60, 64, 70, 100, 90, 100, 90, 0, 30, 60, 100}},
    {"TIGHT", {15, 35, 80, 50, 35, 70, 70, 90, 110, 80, 100, 80, 0, 20, 50, 100}},
    {"OPEN", {45, 90, 60, 90, 70, 50, 60, 60, 90, 110, 100, 90, 20, 50, 70, 100}},
    {"LOFI", {30, 60, 20, 60, 60, 30, 40, 50, 100, 90, 110, 100, 0, 40, 40, 100}},
};
static const preset_t PRE_BASS[] = {
    {"ROLLER", {20, 0, 60, 64, 0, 50, 70, 20, 60, 40, 40, 0, 0, 0, 110, 112}},
    {"SUB", {100, 0, 110, 64, 0, 60, 90, 20, 30, 10, 10, 0, 0, 0, 110, 112}},
    {"ACID", {0, 0, 30, 64, 0, 40, 30, 20, 30, 115, 110, 0, 20, 0, 100, 108}},
    {"REESE", {0, 70, 60, 64, 0, 70, 100, 30, 45, 30, 20, 0, 0, 0, 110, 108}},
    {"PLUCKY", {20, 10, 60, 64, 0, 30, 0, 20, 40, 50, 90, 0, 0, 0, 110, 112}},
    {"SQUARE", {127, 0, 80, 64, 0, 50, 80, 20, 50, 20, 30, 0, 0, 0, 110, 110}},
};
static const preset_t PRE_ROLL[] = {
    {"SUPERSAW", {0, 89, 40, 64, 0, 50, 80, 30, 58, 36, 45, 0, 64, 44, 110, 96}},   /* Rollwerk's */
    {"PLUCK", {0, 34, 40, 64, 0, 34, 0, 26, 40, 36, 96, 0, 64, 44, 110, 116}},      /* Rollwerk's */
    {"GLASS", {40, 20, 0, 77, 0, 30, 0, 40, 70, 90, 60, 0, 80, 70, 100, 106}},
    {"HOOVER", {90, 110, 70, 64, 4, 60, 90, 30, 72, 30, 30, 0, 40, 40, 100, 90}},
    {"ACID", {0, 0, 30, 64, 0, 40, 20, 20, 25, 110, 100, 0, 50, 20, 100, 96}},
    {"CHIME", {120, 10, 0, 77, 0, 26, 0, 50, 90, 40, 40, 0, 90, 60, 90, 84}},
    {"DEEP", {0, 60, 90, 51, 0, 60, 70, 30, 30, 30, 50, 0, 30, 30, 110, 100}},
    {"SHIMMER", {0, 120, 0, 64, 10, 70, 90, 70, 80, 20, 20, 0, 70, 110, 100, 84}},
};
static const preset_t PRE_LEAD[] = {
    {"SOAR", {0, 90, 20, 64, 10, 70, 100, 60, 90, 30, 30, 0, 60, 80, 60, 88}},
    {"HOOVER", {90, 120, 80, 64, 6, 70, 100, 40, 80, 30, 30, 0, 40, 60, 60, 90}},
    {"SAW", {0, 40, 30, 64, 0, 60, 100, 40, 100, 20, 20, 0, 50, 60, 60, 90}},
    {"PIPE", {127, 10, 0, 64, 20, 60, 110, 60, 70, 10, 10, 0, 70, 80, 60, 84}},
    {"BRIGHT", {0, 80, 0, 77, 0, 50, 90, 50, 115, 40, 20, 0, 70, 90, 60, 84}},
    {"DARK", {0, 70, 40, 51, 10, 70, 100, 60, 50, 40, 40, 0, 50, 70, 60, 90}},
};
static const preset_t PRE_PAD[] = {
    {"WARM", {0, 100, 0, 64, 45, 80, 110, 75, 70, 20, 10, 110, 30, 90, 90, 76}},
    {"STRINGS", {0, 110, 0, 64, 70, 90, 110, 90, 85, 10, 0, 0, 20, 100, 80, 76}},
    {"GLASS", {60, 40, 0, 77, 30, 80, 100, 80, 90, 70, 20, 0, 60, 110, 80, 72}},
    {"DARK", {0, 90, 20, 51, 60, 90, 110, 90, 45, 30, 10, 0, 20, 100, 90, 80}},
    {"GATED", {0, 110, 0, 64, 2, 70, 120, 30, 95, 30, 10, 127, 50, 60, 100, 80}},
    {"AIR", {40, 127, 0, 77, 90, 100, 110, 110, 100, 20, 0, 0, 40, 127, 70, 70}},
};

static const preset_t *const PRESETS[NTR] = {PRE_KICK, PRE_909, PRE_BASS, PRE_ROLL, PRE_LEAD, PRE_PAD};
static const uint8_t NPRESET[NTR] = {sizeof PRE_KICK / sizeof PRE_KICK[0], sizeof PRE_909 / sizeof PRE_909[0],
                                     sizeof PRE_BASS / sizeof PRE_BASS[0], sizeof PRE_ROLL / sizeof PRE_ROLL[0],
                                     sizeof PRE_LEAD / sizeof PRE_LEAD[0], sizeof PRE_PAD / sizeof PRE_PAD[0]};

#define TR_PRESET(t) song.tr[t].rsv[0]          /* the preset a track last loaded */

static const char *preset_name(uint32_t t) { return PRESETS[t][TR_PRESET(t) % NPRESET[t]].name; }

static void preset_apply(uint32_t t, uint32_t i)
{
    uint32_t k;
    i %= NPRESET[t];
    for (k = 0; k < NPAR; k++) song.tr[t].par[k] = PRESETS[t][i].p[k];
    TR_PRESET(t) = (uint8_t)i;
    if (t == T_ROLL) song.voice = (uint8_t)(i == 1u);  /* GLOBAL ROLLER VOICE follows SUPERSAW / PLUCK */
}
