/* SPDX-License-Identifier: GPL-3.0-only */
/* APEX effects: a tempo-synced ping-pong delay, a four-line feedback-delay-network hall, and the
 * LIFT riser (noise through a resonant band-pass that climbs with LIFT). */

#define DLEN 24576u                             /* 0.56 s a side */
static int16_t dly_l[DLEN] POOL;
static int16_t dly_r[DLEN] POOL;
static struct {
    uint32_t w, time;                           /* samples */
    int32_t fb, lp_k, lpl, lpr, hpl, hpr;
} dly;

/* the hall: four lines, a Hadamard mix, damping in each line; two allpass diffusers in front */
#define VLEN 4608u
static int16_t vline[4][VLEN] POOL;
static int16_t vap1[331] POOL;
static int16_t vap2[557] POOL;
static const uint16_t VBASE[4] = {2903, 3329, 3803, 4271};
static struct {
    uint32_t w, len[4], a1, a2;
    int32_t g, damp, lp[4];
} verb;

static struct { svf_t f; svfc_t c; uint32_t rng; } riser = {{0, 0}, {0, 0, 0, 0}, 0x6A09E667u};

static void fx_params(uint32_t spb_q16, const uint8_t *g, int32_t lift)
{
    static const uint8_t DIV[5] = {2, 3, 4, 6, 8};             /* 16ths: 1/8, 3/16, 1/4, 3/8, 1/2 */
    uint32_t k, d = DIV[g[G_DTIME] * 5u / 128u], t = (uint32_t)(((uint64_t)spb_q16 * d) >> 16);
    while (t >= DLEN) t >>= 1;                  /* too slow a tempo: half the time */
    dly.time = t;
    dly.fb = clampi(g[G_DFB] * 200 + lift * 40, 0, 29000);
    dly.lp_k = 3000 + g[G_DTONE] * 200;
    for (k = 0; k < 4u; k++)
        verb.len[k] = (uint32_t)VBASE[k] * (64u + g[G_VSIZE] / 2u) / 128u;
    verb.g = clampi(21000 + g[G_VSIZE] * 70 + lift * 20, 0, 31500);
    verb.damp = 4000 + (127 - g[G_VDAMP]) * 200;
    svf_coef(&riser.c, 90 + lift * 5 / 4, 110);
}

/* in: CR stereo frames of delay send (ds) and reverb send (vs); adds the wet signal to out */
static void fx_render(const int32_t *ds, const int32_t *vs, int32_t *out, int32_t lift)
{
    uint32_t i, w = dly.w, vw = verb.w;
    int32_t rl = lift > 4 ? (lift * lift) >> 2 : 0;           /* the riser's level */
    for (i = 0; i < CR; i++) {
        uint32_t r = w >= dly.time ? w - dly.time : w + DLEN - dly.time, k;
        int32_t l = dly_l[r], rr = dly_r[r], in, x, a, b, o[4], s0, s1, s2, s3;
        /* ping-pong: the send enters left, each side feeds the other, filtered */
        dly.lpl += ((rr - dly.lpl) * dly.lp_k) >> 15;
        dly.lpr += ((l - dly.lpr) * dly.lp_k) >> 15;
        dly.hpl += (dly.lpl - dly.hpl) >> 7;
        dly.hpr += (dly.lpr - dly.hpr) >> 7;
        in = (ds[2 * i] + ds[2 * i + 1]) >> 1;
        dly_l[w] = (int16_t)sat16(in + (((dly.lpl - dly.hpl) * dly.fb) >> 15));
        dly_r[w] = (int16_t)sat16(((dly.lpr - dly.hpr) * dly.fb) >> 15);
        w = w + 1u == DLEN ? 0u : w + 1u;
        out[2 * i] += l;
        out[2 * i + 1] += rr;

        /* the riser goes into the hall too */
        x = (vs[2 * i] + vs[2 * i + 1]) >> 2;
        if (rl) {
            int32_t n = (int32_t)(xs32(&riser.rng) >> 16) - 32768, rz = mqs(svf_bp(&riser.f, &riser.c, n), rl);
            x += rz >> 1;
            out[2 * i] += rz;
            out[2 * i + 1] += rz;
        }
        /* two allpass diffusers */
        a = vap1[verb.a1]; b = x - (a >> 1); vap1[verb.a1] = (int16_t)sat16(b); x = a + (b >> 1);
        verb.a1 = verb.a1 + 1u == 331u ? 0u : verb.a1 + 1u;
        a = vap2[verb.a2]; b = x - (a >> 1); vap2[verb.a2] = (int16_t)sat16(b); x = a + (b >> 1);
        verb.a2 = verb.a2 + 1u == 557u ? 0u : verb.a2 + 1u;
        for (k = 0; k < 4u; k++) {
            int32_t v = vline[k][vw >= verb.len[k] ? vw - verb.len[k] : vw + VLEN - verb.len[k]];
            verb.lp[k] += ((v - verb.lp[k]) * verb.damp) >> 15;
            o[k] = verb.lp[k];
        }
        s0 = o[0] + o[1]; s1 = o[0] - o[1]; s2 = o[2] + o[3]; s3 = o[2] - o[3];
        o[0] = (s0 + s2) >> 1; o[1] = (s1 + s3) >> 1; o[2] = (s0 - s2) >> 1; o[3] = (s1 - s3) >> 1;
        for (k = 0; k < 4u; k++)
            vline[k][vw] = (int16_t)sat16(((o[k] * verb.g) >> 15) + (k & 1u ? -x : x));
        vw = vw + 1u == VLEN ? 0u : vw + 1u;
        out[2 * i] += o[0] + o[2];
        out[2 * i + 1] += o[1] + o[3];
    }
    dly.w = w;
    verb.w = vw;
}
