/* SPDX-License-Identifier: GPL-3.0-only */
/* APEX melodic voice: a supersaw (1 to 7 saws, JP-8000 detune and mix curves), a square sub an
 * octave down, a centre saw that morphs to a square, a TPT state-variable low-pass and one ADSR
 * for amp and filter. Runs in the audio ISR, a control tick (CR frames) at a time. */

#define NOSC 7u
#define NVOICE 8u
/* voices per track and the most saws each may stack (the CPU budget, measured in the emulator) */
static const uint8_t TR_V0[NTR] = {0, 0, 0, 1, 3, 5}, TR_NV[NTR] = {0, 0, 1, 2, 2, 3};
static const uint8_t TR_NOSC[NTR] = {0, 0, 3, 7, 7, 7};
static const uint8_t TR_STEREO[NTR] = {0, 0, 0, 1, 1, 1};

/* JP-8000 detune offsets of the side saws, Q15 (Szabo, "How to emulate the super saw") */
static const int16_t DETOFF[6] = {-3605, 3521, -2061, 2037, -640, 652};

enum { ENV_IDLE, ENV_ATK, ENV_DEC, ENV_REL };

typedef struct {
    uint32_t ph[NOSC], inc[NOSC], subph;
    int32_t note_q8, tgt_q8;                    /* pitch now and where a slide goes (Q8 semitones) */
    int32_t env;                                /* Q24 */
    int32_t gate_ticks;                         /* control ticks until release, -1 = held */
    int32_t vel;                                /* Q15 */
    ramp_t amp;
    svf_t fl, fr;
    uint8_t stage, track;
    uint32_t age;
} voice_t;
static voice_t vox[NVOICE];

/* what a track's knobs mean right now (from the smoothed parameters) */
typedef struct {
    int32_t wave, center, side, sub, det;       /* Q15 */
    uint32_t atk, dec, rel;                     /* ATTACK_Q24 / DECAY_Q16 */
    int32_t sus;                                /* Q24 */
    int32_t cut, res, fenv;                     /* cut and fenv in SVF index units */
    uint8_t nosc;
} tparam_t;
static tparam_t tp[NTR];

static void voice_incs(voice_t *v, const tparam_t *p)
{
    uint32_t base = note_inc(v->note_q8), o, b15 = base >> 15;
    v->inc[0] = base;
    for (o = 1; o < p->nosc; o++)
        v->inc[o] = base + (uint32_t)((int32_t)b15 * ((DETOFF[o - 1u] * p->det) >> 15));
}

static void voice_on(voice_t *v, uint32_t t, int32_t note, int32_t vel, int32_t ticks, int legato)
{
    v->tgt_q8 = note << 8;
    v->gate_ticks = ticks;
    v->track = (uint8_t)t;
    if (legato && v->stage != ENV_IDLE && v->stage != ENV_REL)
        return;                                 /* a slide: the pitch glides, the envelope runs on */
    v->note_q8 = v->tgt_q8;
    v->vel = vel * 258;                         /* 127 -> 32766 */
    v->stage = ENV_ATK;
    if (v->env == 0) {                          /* from silence: start the saws spread, as analogue */
        uint32_t o;
        for (o = 0; o < NOSC; o++) v->ph[o] = (o * 0x9E3779B9u) ^ (uint32_t)note * 0x01000193u;
        v->subph = 0;
    }
}

static void voice_off(voice_t *v)
{
    if (v->stage != ENV_IDLE) v->stage = ENV_REL;
    v->gate_ticks = 0;
}

/* one control tick of envelope and pitch; returns the amp target (Q15) */
static int32_t voice_ctl(voice_t *v, const tparam_t *p)
{
    switch (v->stage) {
    case ENV_ATK:
        v->env += (int32_t)p->atk;
        if (v->env >= (1 << 24)) { v->env = 1 << 24; v->stage = ENV_DEC; }
        break;
    case ENV_DEC:
        v->env = p->sus + (int32_t)(((int64_t)(v->env - p->sus) * p->dec) >> 16);
        break;
    case ENV_REL:
        v->env = (int32_t)(((int64_t)v->env * p->rel) >> 16);
        if (v->env < 512) { v->env = 0; v->stage = ENV_IDLE; }
        break;
    default:
        v->env = 0;
    }
    if (v->gate_ticks > 0 && --v->gate_ticks == 0 && v->stage != ENV_IDLE)
        v->stage = ENV_REL;
    if (v->note_q8 != v->tgt_q8) {              /* slide: ~45 ms */
        int32_t d = v->tgt_q8 - v->note_q8;
        v->note_q8 += d / 24 ? d / 24 : d;
    }
    voice_incs(v, p);
    return (int32_t)(((int64_t)(v->env >> 9) * v->vel) >> 15);
}

/* polyBLEP correction of a rising saw at phase ph (Q15) */
static inline int32_t blep(uint32_t ph, uint32_t inc)
{
    uint32_t d = inc >> 15;
    int32_t x;
    if (!d) return 0;
    if (ph < inc) { x = (int32_t)(ph / d); return x + x - ((x * x) >> 15) - 32768; }
    if (ph > 0xFFFFFFFFu - inc) { x = -(int32_t)((0xFFFFFFFFu - ph) / d); return ((x * x) >> 15) + x + x + 32768; }
    return 0;
}

/* render one control tick of voice v into tb (stereo, adds) */
static void voice_render(voice_t *v, const tparam_t *p, int32_t lift_cut, int32_t *tb, int stereo)
{
    int32_t c[CR], sl[CR], sr[CR], cut, i, amp, step;
    svfc_t fc;
    uint32_t o, ph, inc;
    int32_t target = voice_ctl(v, p);
    if (v->stage == ENV_IDLE && v->amp.g == 0) return;
    ramp_to(&v->amp, target);

    ph = v->ph[0]; inc = v->inc[0];             /* the centre saw, band-limited, morphing to a square */
    for (i = 0; i < (int32_t)CR; i++) {
        int32_t s;
        ph += inc;
        s = ((int32_t)(ph >> 16) - 32768) - blep(ph, inc);
        if (p->wave) {
            uint32_t q = ph + 0x80000000u;
            int32_t sq = s - ((int32_t)(q >> 16) - 32768);
            s += (((sq - s) >> 1) * p->wave) >> 14;
        }
        c[i] = s;
    }
    v->ph[0] = ph;
    for (i = 0; i < (int32_t)CR; i++) sl[i] = sr[i] = 0;
    for (o = 1; o < p->nosc; o++) {             /* the side saws, alternating left and right */
        int32_t *d = (o & 1u) ? sl : sr;
        ph = v->ph[o]; inc = v->inc[o];
        for (i = 0; i < (int32_t)CR; i++) { ph += inc; d[i] += (int32_t)(ph >> 16) - 32768; }
        v->ph[o] = ph;
    }
    if (p->sub) {                               /* square sub, an octave down */
        uint32_t si = v->inc[0] >> 1;
        ph = v->subph;
        for (i = 0; i < (int32_t)CR; i++) { ph += si; c[i] += (ph & 0x80000000u) ? -p->sub : p->sub; }
        v->subph = ph;
    }
    /* filter: cutoff + envelope + key tracking + LIFT, one coefficient set per tick */
    cut = p->cut + ((p->fenv * (v->env >> 9)) >> 15) + ((v->note_q8 >> 8) - 60) + lift_cut;
    svf_coef(&fc, cut, p->res);
    amp = v->amp.g; step = v->amp.step;
    if (stereo) {
        for (i = 0; i < (int32_t)CR; i++) {
            int32_t m = (c[i] * (p->center >> 1)) >> 14, l, r;
            l = m + (((sl[i] >> 1) * (p->side >> 1)) >> 13);
            r = m + (((sr[i] >> 1) * (p->side >> 1)) >> 13);
            l = svf_lp(&v->fl, &fc, l >> 1);
            r = svf_lp(&v->fr, &fc, r >> 1);
            amp += step;
            tb[2 * i] += (l * (amp >> 1)) >> 14;
            tb[2 * i + 1] += (r * (amp >> 1)) >> 14;
        }
    } else {
        for (i = 0; i < (int32_t)CR; i++) {
            int32_t m = ((c[i] * (p->center >> 1)) >> 14) + ((((sl[i] + sr[i]) >> 1) * (p->side >> 1)) >> 13);
            m = svf_lp(&v->fl, &fc, m >> 1);
            amp += step;
            m = (m * (amp >> 1)) >> 14;
            tb[2 * i] += m;
            tb[2 * i + 1] += m;
        }
    }
    v->amp.g = target;
}

/* the knobs of melodic track t -> tparam_t (eff: smoothed parameter values, 0..127) */
static void tparam_mel(uint32_t t, const int32_t *eff)
{
    tparam_t *p = &tp[t];
    int32_t det = eff[P_DETUNE], x = det * 258;                 /* mix follows detune (Q15) */
    p->det = DETUNE_Q15[det];
    p->nosc = det ? TR_NOSC[t] : 1u;
    p->center = 32698 - ((x * 18142) >> 15);                    /* -0.55366 x + 0.99785 */
    p->side = det ? clampi(1454 + ((x * 42078) >> 15) - ((((x * x) >> 15) * 24171) >> 15), 0, 32767) : 0;
    if (p->nosc > 1u && p->nosc < 7u)                           /* fewer saws, the same width */
        p->side = clampi(p->side * 6 / (int32_t)(p->nosc - 1u), 0, 65535);
    p->wave = eff[P_WAVE] * 258;
    p->sub = eff[P_SUB] * 100;
    p->atk = ATTACK_Q24[eff[P_ATK]];
    p->dec = DECAY_Q16[eff[P_DEC]];
    p->sus = eff[P_SUS] << 17;
    p->rel = DECAY_Q16[eff[P_REL]];
    p->cut = eff[P_CUT] * 2;
    p->res = eff[P_RES];
    p->fenv = eff[P_FENV] * 2;
}
