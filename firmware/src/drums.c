/* SPDX-License-Identifier: GPL-3.0-only */
/* APEX drums, all synthesised (no samples): a 909-style kick, and the kit track's closed and open
 * hats, clap, snare and ride. Runs in the audio ISR a control tick at a time. */

static uint32_t drum_rng = 0x2545F491u;

/* ---- kick: a sine swept down from a click to its tune, an exponential decay, drive, a tone LP */
static struct {
    uint32_t ph, inc_end, inc_top;
    int32_t sweep;                              /* Q16: the share of inc_top - inc_end still to fall */
    int32_t amp, boom;                          /* Q24: the body and a slower tail */
    int32_t click;                              /* frames of click left */
    int32_t vel, lp, out_amp_prev;
    uint32_t sweep_k, dec_k, boom_k;
    int32_t drive_q12, tone_q15, click_lvl, boom_lvl;
} kick;

static void kick_params(const int32_t *eff)
{
    kick.inc_end = note_inc((26 + eff[PK_TUNE] * 14 / 127) << 8);         /* D1 .. E2 */
    kick.inc_top = kick.inc_end + (kick.inc_end >> 6) * (uint32_t)(eff[PK_PITCH] * 3 / 2 + 8);
    kick.sweep_k = DECAY_Q16[eff[PK_SWEEP] * 40 / 127 + 4];                 /* 6 .. 60 ms to -60 dB */
    kick.dec_k = DECAY_Q16[eff[PK_DECAY] * 50 / 127 + 30];                  /* 70 ms .. 1.1 s */
    kick.boom_k = DECAY_Q16[eff[PK_DECAY] * 40 / 127 + 60];
    kick.boom_lvl = eff[PK_BOOM] * 160;
    kick.click_lvl = eff[PK_CLICK] * 180;
    kick.drive_q12 = 4096 + eff[PK_DRIVE] * 160;                            /* x1 .. x6 */
    kick.tone_q15 = 2000 + eff[PK_TONE] * 240;
}

static void kick_hit(int32_t vel)
{
    kick.sweep = 1 << 16;
    kick.amp = 1 << 24;
    kick.boom = 1 << 24;
    kick.click = 90;
    kick.vel = vel * 258;
    kick.ph = 0x40000000u;                      /* start at the crest: the 909's thump */
}

static void kick_render(int32_t *tb)
{
    int32_t i, a0, a1, b, step;
    uint32_t inc;
    if (kick.amp < 256 && kick.boom < 256) return;
    inc = kick.inc_end + (uint32_t)(((uint64_t)(kick.inc_top - kick.inc_end) * (uint32_t)kick.sweep) >> 16);
    kick.sweep = (int32_t)(((int64_t)kick.sweep * kick.sweep_k) >> 16);
    a0 = kick.out_amp_prev;
    kick.amp = (int32_t)(((int64_t)kick.amp * kick.dec_k) >> 16);
    kick.boom = (int32_t)(((int64_t)kick.boom * kick.boom_k) >> 16);
    b = (int32_t)(((int64_t)(kick.boom >> 9) * kick.boom_lvl) >> 15);
    a1 = (kick.amp >> 9) + b;
    a1 = (int32_t)(((int64_t)a1 * kick.vel) >> 15);
    kick.out_amp_prev = a1;
    step = (a1 - a0) >> CR_SHIFT;
    for (i = 0; i < (int32_t)CR; i++) {
        int32_t s;
        kick.ph += inc;
        s = psine(kick.ph);
        a0 += step;
        s = (s * (a0 >> 1)) >> 14;
        if (kick.click > 0) {                   /* a 2 ms knock of noise and a step */
            int32_t n = (int32_t)(xs32(&drum_rng) >> 17) - 16384;
            s += ((n + 12000) * kick.click_lvl >> 15) * kick.click / 90;
            kick.click--;
        }
        s = soft((s * kick.drive_q12) >> 12);
        kick.lp += ((s - kick.lp) * kick.tone_q15) >> 15;
        tb[2 * i] += kick.lp;
        tb[2 * i + 1] += kick.lp;
    }
}

/* ---- the kit: a metallic bank of six squares (CH, OH, RIDE), noise (CLAP, SNARE) */
static const uint16_t METAL_HZ[6] = {205, 304, 370, 523, 540, 800};
static struct {
    uint32_t mph[6], minc[6];
    svf_t hbp, hhp, rbp, cbp, shp;
    svfc_t hbpc, hhpc, rbpc, cbpc, shpc;
    int32_t ch, oh, ride, clap, snr_n, snr_t;   /* Q24 envelopes */
    uint32_t ch_k, oh_k, ride_k, clap_k, snr_k, snrt_k;
    int32_t clap_t;                             /* control ticks since the clap began */
    uint32_t tph1, tph2, tinc1, tinc2;
    int32_t lvl[NKIT], vel[NKIT], snap;
    int32_t g_prev[6];
} kit;

static void kit_params(const int32_t *eff)
{
    uint32_t k, tone = 160 + (uint32_t)eff[PD_HATTONE];                       /* x1.25 .. x2.25 of METAL_HZ */
    for (k = 0; k < 6u; k++)
        kit.minc[k] = (uint32_t)METAL_HZ[k] * tone / 128u * 97391u;          /* 2^32 / 44100 = 97391.5 */
    svf_coef(&kit.hbpc, 226 + eff[PD_HATTONE] / 8, 50);                     /* ~9-11 kHz band */
    svf_coef(&kit.hhpc, 205, 10);                                            /* ~7 kHz high-pass */
    svf_coef(&kit.rbpc, 205 + eff[PD_HATTONE] / 10, 70);
    svf_coef(&kit.cbpc, 150 + eff[PD_CLAPTONE] / 3, 60);                    /* clap ~0.9-1.6 kHz */
    svf_coef(&kit.shpc, 150 + eff[PD_SNRTONE] / 4, 20);
    kit.ch_k = DECAY_Q16[eff[PD_CHDEC] * 30 / 127 + 8];
    kit.oh_k = DECAY_Q16[eff[PD_OHDEC] * 40 / 127 + 45];
    kit.ride_k = DECAY_Q16[eff[PD_RIDEDEC] * 40 / 127 + 60];
    kit.clap_k = DECAY_Q16[eff[PD_CLAPDEC] * 40 / 127 + 30];
    kit.snr_k = DECAY_Q16[eff[PD_SNRSNAP] * 40 / 127 + 25];
    kit.snrt_k = DECAY_Q16[30];
    kit.tinc1 = note_inc((52 + eff[PD_SNRTONE] / 12) << 8);                 /* ~165 Hz */
    kit.tinc2 = note_inc((62 + eff[PD_SNRTONE] / 12) << 8);                 /* ~290 Hz */
    kit.snap = 6000 + eff[PD_SNRSNAP] * 160;
    kit.lvl[K_CH] = eff[PD_CHLVL] * 200;
    kit.lvl[K_OH] = eff[PD_OHLVL] * 200;
    kit.lvl[K_CLAP] = eff[PD_CLAPLVL] * 220;
    kit.lvl[K_SNARE] = eff[PD_SNRLVL] * 220;
    kit.lvl[K_RIDE] = eff[PD_RIDELVL] * 150;
}

static void kit_hit(uint32_t v, int32_t vel)
{
    kit.vel[v] = vel * 258;
    switch (v) {
    case K_CH: kit.ch = 1 << 24; kit.oh = 0; break;   /* the closed hat chokes the open one */
    case K_OH: kit.oh = 1 << 24; break;
    case K_RIDE: kit.ride = 1 << 24; break;
    case K_CLAP: kit.clap = 1 << 24; kit.clap_t = 0; break;
    case K_SNARE: kit.snr_n = 1 << 24; kit.snr_t = 1 << 24; kit.tph1 = kit.tph2 = 0; break;
    }
}

static inline int32_t dec_tick(int32_t *e, uint32_t k)
{
    *e = (int32_t)(((int64_t)*e * k) >> 16);
    if (*e < 512) *e = 0;
    return *e >> 9;                             /* Q15 */
}

static void kit_render(int32_t *tb)
{
    int32_t g[6], st[6], i, k, metal_on, noise_on;
    /* this tick's gains: CH, OH, RIDE, CLAP, SNARE noise, SNARE tone */
    int32_t ch = dec_tick(&kit.ch, kit.ch_k), oh = dec_tick(&kit.oh, kit.oh_k), rd = dec_tick(&kit.ride, kit.ride_k);
    int32_t cl, sn = dec_tick(&kit.snr_n, kit.snr_k), stn = dec_tick(&kit.snr_t, kit.snrt_k);
    if (kit.clap) {                             /* three quick slaps then the tail */
        int32_t t = kit.clap_t++;
        if (t == 11 || t == 22 || t == 33) kit.clap = 1 << 24;
        cl = dec_tick(&kit.clap, t < 34 ? DECAY_Q16[6] : kit.clap_k);
    } else cl = 0;
    g[0] = mq15(mq15(ch, kit.lvl[K_CH]), kit.vel[K_CH]);
    g[1] = mq15(mq15(oh, kit.lvl[K_OH]), kit.vel[K_OH]);
    g[2] = mq15(mq15(rd, kit.lvl[K_RIDE]), kit.vel[K_RIDE]);
    g[3] = mq15(mq15(cl, kit.lvl[K_CLAP]), kit.vel[K_CLAP]);
    g[4] = mq15(mq15(mq15(sn, kit.snap), kit.lvl[K_SNARE]), kit.vel[K_SNARE]);
    g[5] = mq15(mq15(stn, kit.lvl[K_SNARE]), kit.vel[K_SNARE]);
    metal_on = g[0] | g[1] | g[2] | kit.g_prev[0] | kit.g_prev[1] | kit.g_prev[2];
    noise_on = g[3] | g[4] | g[5] | kit.g_prev[3] | kit.g_prev[4] | kit.g_prev[5];
    if (!metal_on && !noise_on) return;
    for (k = 0; k < 6; k++) { st[k] = (g[k] - kit.g_prev[k]) >> CR_SHIFT; }
    for (i = 0; i < (int32_t)CR; i++) {
        int32_t s = 0, n = (int32_t)(xs32(&drum_rng) >> 16) - 32768;
        for (k = 0; k < 6; k++) kit.g_prev[k] += st[k];
        if (metal_on) {
            int32_t m = 0, h, r;
            uint32_t j;
            for (j = 0; j < 6u; j++) { kit.mph[j] += kit.minc[j]; m += (kit.mph[j] & 0x80000000u) ? -4096 : 4096; }
            h = svf_hp(&kit.hhp, &kit.hhpc, svf_bp(&kit.hbp, &kit.hbpc, m + (n >> 2)));
            r = svf_bp(&kit.rbp, &kit.rbpc, m + (n >> 3));
            s += mqs(h, (kit.g_prev[0] + kit.g_prev[1]) >> 1) * 2 + mqs(r, kit.g_prev[2]);
        }
        if (noise_on) {
            int32_t c = svf_bp(&kit.cbp, &kit.cbpc, n >> 1), sh = svf_hp(&kit.shp, &kit.shpc, n >> 1);
            kit.tph1 += kit.tinc1; kit.tph2 += kit.tinc2;
            s += mqs(c, kit.g_prev[3]) * 2 + mqs(sh, kit.g_prev[4]);
            s += mq15((psine(kit.tph1) + (psine(kit.tph2) >> 1)) >> 1, kit.g_prev[5]);
        }
        tb[2 * i] += s * 2;                     /* (the kit sat 6 dB under the mix) */
        tb[2 * i + 1] += s * 2;
    }
    for (k = 0; k < 6; k++) kit.g_prev[k] = g[k];
}
