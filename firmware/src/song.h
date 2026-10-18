/* SPDX-License-Identifier: GPL-3.0-only */
/* Finton song: six tracks over Rollwerk's eight-bar harmony, a BUILD lane. Everything the engine plays
 * and the flash keeps is in one song_t; the main loop writes it, the audio ISR reads it (byte stores only).
 *
 *   KICK  909 kick, modelled          909   CH OH CLAP SNARE RIDE, modelled, a voice mask per step
 *   BASS  roll, offbeat or drag       ROLL  Rollwerk's 16th roller over the eight bars
 *   LEAD  the soaring supersaw       PAD   the bar's chord, trance-gated
 *
 * The harmony is Rollwerk's: a key and mode, eight bars each holding a chord from the mode's vocabulary
 * (or two, split at the half bar), picked from the progression library by mood or edited by hand.
 * BASS, ROLL and LEAD hold MIDI notes the generator derives from the chords; a harmony change derives
 * them again (unless a track was edited by hand). PAD resolves the chord as it plays. */
#pragma once
#include <stdint.h>
#include "finton_harmony.h"

#define NTR 6u
#define NSTEP 128u                              /* eight bars of 16ths */
#define NPAR 16u
#define NPAGE 4u
#define NBAR 8u
#define LIFT_STEPS 128u                         /* the BUILD lane: 8 bars of 16ths */
enum { T_KICK, T_909, T_BASS, T_ROLL, T_LEAD, T_PAD };
enum { K_CH, K_OH, K_CLAP, K_SNARE, K_RIDE, NKIT };

#define ST_ON 1u
#define ST_SLIDE 2u
#define ST_RATCHET 4u                           /* a second hit at the half step (Rollwerk's bar-8 ratchet) */

typedef struct {
    int8_t note;                                /* MIDI note; 909: voice mask; PAD: unused (the chord) */
    uint8_t vel;                                /* 1..127 */
    uint8_t len;                                /* in steps, 1..128 */
    uint8_t fl;                                 /* ST_ON | ST_SLIDE | ST_RATCHET */
} step_t;

typedef struct {
    step_t st[NSTEP];
    uint8_t lock[NSTEP][NPAR];                  /* 0 = none, else value + 1: p-locks and recorded motion */
    uint8_t par[NPAR];                          /* the knob values, 0..127 */
    uint8_t len;                                /* 16, 32, 48, 64 .. 128 */
    uint8_t mute;
    uint16_t gate;                              /* trance-gate pattern, bit s = 16th s open */
    uint8_t seed, edited, rsv[2];               /* edited: steps changed by hand, keep them on a harmony change */
} track_t;

enum { G_DTIME, G_DFB, G_DTONE, G_VSIZE, G_VDAMP, G_DRIVE, G_SLOT, G_MOTION,
       G_MFILT, G_MRES, G_MSPACE, G_MPUMP, NGPAR };   /* the last four: the PERFORM macros, 64 = neutral (SPACE 0) */
enum { FIG_AUTO, FIG_LIFT, FIG_CLASSIC, FIG_CLIMB, FIG_CYCLE, NFIG };
enum { TOP_OFF, TOP_PEDAL, TOP_THIRD, NTOP };
enum { SHAPE_OFF, SHAPE_RISE, SHAPE_FALL, SHAPE_ARC, NSHAPE };
enum { BASS_OFF, BASS_ROLL, BASS_OFFBEAT, BASS_DRAG, NBASS };

typedef struct {
    uint32_t magic;
    uint16_t ver, bpm10;                        /* tempo x10 */
    uint8_t key, mode, mood, prog;              /* tonic 0 = C; mode 0 minor 1 major; the library entry */
    uint8_t deg[NBAR], half[NBAR];              /* the chord of each bar (vocabulary index), half: 255 = whole */
    uint8_t figure, top, sus_turn, reg;         /* the roller: figure, top note, sus turn in bar 8, register shape */
    uint8_t reg_amt, build, build_amt, bass;    /* register amount, BUILD shape and amount (0..127), bass style */
    uint8_t pump, voice, seed, rsv;             /* kick pump on/off, ROLL voice 0 supersaw 1 pluck */
    uint8_t g[NGPAR];
    uint8_t lift[LIFT_STEPS];                   /* the BUILD lane: 0 = none, else value + 1 */
    track_t tr[NTR];
} song_t;

#define SONG_MAGIC 0x4E544E46u                  /* "FNTN" */
#define SONG_VER 2u

static song_t song;

/* ---- names */
static const char *const TR_NAME[NTR] = {"KICK", "909", "BASS", "ROLL", "LEAD", "PAD"};
static const char *const TR_SHORT[NTR] = {"KCK", "909", "BAS", "RLL", "LED", "PAD"};
static const char *const PAGE_NAME[NPAGE + 1] = {"SOUND", "SHAPE", "FILTER", "SEND", "PERFORM"};
#define PG_PERFORM NPAGE                       /* the HOME page: FILTER RES SPACE PUMP over the whole mix */
static const char *const MACRO_NAME[4] = {"FILTER", "RES", "SPACE", "PUMP"};
static const char *const KIT_NAME[NKIT] = {"CH", "OH", "CLAP", "SNR", "RIDE"};
static const char *const FIG_NAME[NFIG] = {"AUTO", "LIFT", "1-5-8", "CLIMB", "CYCLE"};
static const char *const TOP_NAME[NTOP] = {"OFF", "PEDAL", "3RD"};
static const char *const SHAPE_NAME[NSHAPE] = {"OFF", "RISE", "FALL", "ARC"};
static const char *const BASS_NAME[NBASS] = {"OFF", "ROLL", "OFFBEAT", "DRAG"};

/* parameter index = page * 4 + knob */
enum { P_WAVE, P_DETUNE, P_SUB, P_OCT, P_ATK, P_DEC, P_SUS, P_REL, P_CUT, P_RES, P_FENV, P_GATE,
       P_DLY, P_VERB, P_DUCK, P_LVL };
enum { PK_TUNE, PK_PITCH, PK_SWEEP, PK_DECAY, PK_CLICK, PK_DRIVE, PK_TONE, PK_BOOM, PK_DUCKREL };
enum { PD_CHDEC, PD_OHDEC, PD_HATTONE, PD_RIDEDEC, PD_CLAPDEC, PD_CLAPTONE, PD_SNRTONE, PD_SNRSNAP,
       PD_CHLVL, PD_OHLVL, PD_CLAPLVL, PD_SNRLVL, PD_DLY2, PD_VERB2, PD_RIDELVL, PD_LVL2 };

static const char *const PAR_MEL[NPAR] = {"WAVE", "WIDTH", "SUB", "OCT", "ATK", "DEC", "SUS", "REL",
                                          "CUT", "RES", "ENV", "GATE", "DELAY", "VERB", "PUMP", "LEVEL"};
static const char *const PAR_KICK[NPAR] = {"TUNE", "PITCH", "SWEEP", "DECAY", "CLICK", "DRIVE", "TONE", "BOOM",
                                           "PUMP.T", "", "", "", "DELAY", "VERB", "", "LEVEL"};
static const char *const PAR_909[NPAR] = {"CH.DEC", "OH.DEC", "HAT.T", "RIDE.D", "CLP.D", "CLP.T", "SNR.T", "SNAP",
                                          "CH", "OH", "CLAP", "SNARE", "DELAY", "VERB", "RIDE", "LEVEL"};

static const char *par_name(uint32_t t, uint32_t p)
{
    return t == T_KICK ? PAR_KICK[p] : t == T_909 ? PAR_909[p] : PAR_MEL[p];
}

/* ---- harmony */
static const char *const NOTE_NAME[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char *const NOTE_FLAT[12] = {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};
static const uint8_t SCALE_MINOR[7] = {0, 2, 3, 5, 7, 8, 10}, SCALE_MAJOR[7] = {0, 2, 4, 5, 7, 9, 11};

typedef struct { uint8_t root, q; } chord_t;    /* root: pitch class 0..11 */

static const vocab_t *vocab(void) { return song.mode ? VOCAB_MAJOR : VOCAB_MINOR; }
static uint32_t nvocab(void) { return song.mode ? NVOCAB_MAJOR : NVOCAB_MINOR; }
static int key_flats(void)                       /* Rollwerk spells these keys with flats */
{
    return song.mode ? (0x52Au >> song.key) & 1u : (0x4ADu >> song.key) & 1u;
}

static chord_t chord_of(uint32_t v)
{
    chord_t c;
    const vocab_t *vb = &vocab()[v % nvocab()];
    c.root = (uint8_t)((song.key + vb->offset) % 12u);
    c.q = vb->q;
    return c;
}
/* the chord sounding at step s (0..127): the bar's, or its second half's after step 8 of a split bar */
static chord_t chord_at(uint32_t s)
{
    uint32_t b = (s / 16u) % NBAR;
    return chord_of((s & 15u) >= 8u && song.half[b] != 255u ? song.half[b] : song.deg[b]);
}
static uint32_t chord_ntones(chord_t c) { return Q_IV[c.q][3] == 255u ? 3u : 4u; }

static void chord_name(chord_t c, char *out)
{
    const char *n = (key_flats() ? NOTE_FLAT : NOTE_NAME)[c.root], *s = Q_SUFFIX[c.q];
    uint32_t i = 0;
    while (*n) out[i++] = *n++;
    while (*s && i < 9u) out[i++] = *s++;
    out[i] = 0;
}
static const char *chord_roman(uint32_t v) { return vocab()[v % nvocab()].id; }

/* load entry p of the library (MOOD_START tells the mood) */
static void prog_load(uint32_t p)
{
    uint32_t b, m;
    p %= NPROG;
    for (m = 0; m + 1u < NMOOD && p >= MOOD_START[m + 1u]; m++) {}
    song.mood = (uint8_t)m;
    song.prog = (uint8_t)p;
    song.mode = m == 4u;                        /* the MAJOR mood is in a major key */
    for (b = 0; b < NBAR; b++) { song.deg[b] = PROG[p][b][0]; song.half[b] = PROG[p][b][1]; }
}

/* a scale step from a note: the next note of the key's scale up (dir +1) or down (-1) */
static int32_t scale_step(int32_t n, int32_t dir)
{
    const uint8_t *sc = song.mode ? SCALE_MAJOR : SCALE_MINOR;
    int32_t k;
    for (k = 1; k < 12; k++) {
        int32_t m = n + dir * k, pc = ((m - song.key) % 12 + 12) % 12, i;
        for (i = 0; i < 7; i++) if (sc[i] == pc) return m;
    }
    return n + dir;
}

/* a track's octave knob in semitones */
static int32_t oct_shift(uint32_t t) { return ((int32_t)song.tr[t].par[P_OCT] * 5 / 128 - 2) * 12; }
