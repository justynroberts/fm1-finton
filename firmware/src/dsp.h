/* SPDX-License-Identifier: GPL-3.0-only */
/* APEX DSP primitives. Integers only: the chip has no FPU and no 64-bit division.
 *   signals   Q15 (+-32767) in int32, headroom above
 *   gains     Q15 unless named otherwise
 *   filters   TPT state-variable (Zavalishin), coefficients Q14, fed at half level so the
 *             products stay inside 32 bits; the states saturate, which is the drive */
#pragma once
#include <stdint.h>

#define CR 32u                                  /* frames per control tick (1378 Hz) */
#define CR_SHIFT 5u

static inline int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline int32_t sat16(int32_t v) { return v > 32767 ? 32767 : v < -32767 ? -32767 : v; }
static inline int32_t mq15(int32_t a, int32_t b) { return (a * b) >> 15; }   /* |a| < 2^16, |b| <= 2^15 */
static inline int32_t mqs(int32_t a, int32_t b) { return ((a >> 2) * b) >> 13; } /* |a| < 2^18 (filter outputs) */

/* soft clip, Q15 in and out: linear to 1/2, then a cubic knee to full scale */
static inline int32_t soft(int32_t x)
{
    int32_t a = x < 0 ? -x : x, y;
    if (a <= 16384) return x;
    if (a >= 49152) y = 32767;
    else { int32_t t = a - 16384; y = 16384 + t - ((t * t) >> 16); }   /* slope 1 -> 0 at full scale */
    return x < 0 ? -y : y;
}

/* parabolic sine of a 32-bit phase, Q15 (Dinghy's, ~0.1 %) */
static inline int32_t psine(uint32_t ph)
{
    int32_t x = (int32_t)ph >> 16, ax = x < 0 ? -x : x, y, ay;
    y = (x * (32768 - ax)) >> 13;
    ay = y < 0 ? -y : y;
    y += ((((y * ay) >> 15) - y) * 7373) >> 15;
    return y > 32767 ? 32767 : y < -32767 ? -32767 : y;
}

/* noise: one xorshift per user, each with one writer */
static inline uint32_t xs32(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *s = x;
}

/* ---- the state-variable filter */
typedef struct { int32_t ic1, ic2; } svf_t;
typedef struct { int32_t a1, a2, a3, k; } svfc_t;

/* cut: index into SVF_G (0..255, 30 Hz .. 16 kHz); res: 0..127 */
static void svf_coef(svfc_t *c, int32_t cut, int32_t res)
{
    int32_t g = SVF_G[clampi(cut, 0, 255)], k = 32768 - res * 245, d;   /* k = 2R in Q14: 2.0 .. ~0.1 */
    d = 16384 + (int32_t)(((int64_t)g * (g + k)) >> 14);
    c->k = k;
    c->a1 = (int32_t)((1u << 28) / (uint32_t)d);
    c->a2 = (g * c->a1) >> 14;
    c->a3 = (g * c->a2) >> 14;
}

#define SVF_LIM 49151
/* one sample; returns low-pass, *bp gets band-pass (both Q15) */
static inline int32_t svf_lp(svf_t *s, const svfc_t *c, int32_t x)
{
    int32_t v1, v2, v3;
    x >>= 1;
    v3 = x - s->ic2;
    v1 = (c->a1 * s->ic1 + c->a2 * v3) >> 14;
    v2 = s->ic2 + ((c->a2 * s->ic1 + c->a3 * v3) >> 14);
    s->ic1 = clampi(2 * v1 - s->ic1, -SVF_LIM, SVF_LIM);
    s->ic2 = clampi(2 * v2 - s->ic2, -SVF_LIM, SVF_LIM);
    return v2 << 1;
}
static inline int32_t svf_bp(svf_t *s, const svfc_t *c, int32_t x)
{
    int32_t v1, v2, v3;
    x >>= 1;
    v3 = x - s->ic2;
    v1 = (c->a1 * s->ic1 + c->a2 * v3) >> 14;
    v2 = s->ic2 + ((c->a2 * s->ic1 + c->a3 * v3) >> 14);
    s->ic1 = clampi(2 * v1 - s->ic1, -SVF_LIM, SVF_LIM);
    s->ic2 = clampi(2 * v2 - s->ic2, -SVF_LIM, SVF_LIM);
    return v1 << 1;
}
static inline int32_t svf_hp(svf_t *s, const svfc_t *c, int32_t x)
{
    int32_t v1, v2, v3;
    x >>= 1;
    v3 = x - s->ic2;
    v1 = (c->a1 * s->ic1 + c->a2 * v3) >> 14;
    v2 = s->ic2 + ((c->a2 * s->ic1 + c->a3 * v3) >> 14);
    s->ic1 = clampi(2 * v1 - s->ic1, -SVF_LIM, SVF_LIM);
    s->ic2 = clampi(2 * v2 - s->ic2, -SVF_LIM, SVF_LIM);
    return (x - ((c->k * v1) >> 14) - v2) << 1;
}

/* a gain that glides across one control tick: g walks from g0 to g1 in CR steps */
typedef struct { int32_t g, step; } ramp_t;
static inline void ramp_to(ramp_t *r, int32_t target) { r->step = (target - r->g) >> CR_SHIFT; }
static inline void ramp_end(ramp_t *r, int32_t target) { r->g = target; r->step = 0; }

/* the phase increment of a MIDI note plus a fraction (Q8 semitones) */
static inline uint32_t note_inc(int32_t note_q8)
{
    int32_t n = note_q8 >> 8, f = note_q8 & 255;
    if (n < 0) { n = 0; f = 0; }
    if (n > 126) { n = 126; f = 255; }
    return (uint32_t)(((uint64_t)PITCH_INC[n] * SEMI_FRAC[f]) >> 15);
}
