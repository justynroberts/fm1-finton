/* SPDX-License-Identifier: GPL-3.0-only */
/* APEX engine: the sequencer clock and app_audio(). Everything here runs in the audio ISR.
 * The main loop talks to it through the song (bytes), a few volatile words, and the live-note queue.
 *
 * Per control tick (CR frames): the clock advances and fires steps, parameters slide toward their
 * p-locked or knob values, LIFT reshapes the mix, every track renders into its own buffer, then the
 * mixer applies level, trance gate and sidechain duck and feeds the delay and the hall. */

/* ---- shared with the main loop */
static volatile uint8_t eng_play, eng_start;    /* main: eng_start = 1 to (re)start from the top */
static volatile uint32_t eng_spb_q16 = 314572800u;   /* samples per 16th, Q16 (main: from the tempo) */
static volatile uint32_t eng_pos;               /* the 16th now playing, counted from the start */
static volatile uint32_t eng_acc;               /* Q16 samples into it */
static volatile int32_t eng_lift_knob = -1;     /* main: the LIFT knob, -1 = follow the lane */
static volatile int32_t eng_lift;               /* the LIFT in use (0..127) */
static volatile uint32_t eng_hit_ms[NTR];       /* when each track last fired (for the screen) */
static volatile uint8_t eng_hit_note[NTR];
static volatile uint32_t eng_kit_hit_ms[NKIT];
static volatile uint32_t eng_load_q8, eng_peak_q8;  /* ISR time per buffer, 256 = all of it */
static volatile int32_t eng_meter[NTR];         /* per-track output level (peak, decaying) */
static int16_t scope[512];                      /* the output, every other frame, for the sky */
static volatile uint32_t scope_w;

/* live notes from the keys: a ring with one writer (main) and one reader (ISR) */
typedef struct { uint8_t on, track, note, vel; } live_t;
static live_t live_q[32];
static volatile uint32_t live_w, live_r;

static int32_t build_q8;                        /* Rollwerk's BUILD from the lane, Q8 of 0..127 */
static volatile int32_t eng_build;
static int32_t eff_q8[NTR][NPAR];               /* parameters as played: smoothed, Q8 */
static int32_t lift_q8;
static uint32_t cur_step[NTR];
static uint32_t vage;
static int32_t duck;                            /* Q24, set by the kick, falls */
static uint32_t duck_k = 60000;
static ramp_t tgain[NTR], gatep[NTR];
static int32_t tbuf[NTR][2 * CR];
static int32_t dsend[2 * CR], vsend[2 * CR];

static uint32_t step_ticks(uint32_t len)        /* a length in steps as control ticks */
{
    return len * (eng_spb_q16 >> 8) / (CR << 8);
}

static voice_t *voice_pick(uint32_t t, int mono)
{
    uint32_t v0 = TR_V0[t], n = TR_NV[t], i, best = v0, oldest = 0xFFFFFFFFu;
    if (mono) {                                 /* the live voice, else the next one */
        for (i = v0; i < v0 + n; i++)
            if (vox[i].stage == ENV_ATK || vox[i].stage == ENV_DEC) return &vox[i];
    }
    for (i = v0; i < v0 + n; i++) {             /* a silent voice, else the oldest */
        uint32_t score = vox[i].stage == ENV_IDLE ? 0u : vox[i].stage == ENV_REL ? vox[i].age >> 1 : vox[i].age;
        if (vox[i].stage == ENV_IDLE) return &vox[i];
        if (score < oldest) { oldest = score; best = i; }
    }
    return &vox[best];
}

static void note_play(uint32_t t, int32_t note, int32_t vel, int32_t ticks, int slide)
{
    uint32_t i;
    voice_t *v;
    if (t == T_BASS || t == T_LEAD) {
        v = voice_pick(t, 1);
        if (!(slide && (v->stage == ENV_ATK || v->stage == ENV_DEC))) {
            for (i = TR_V0[t]; i < TR_V0[t] + TR_NV[t]; i++) voice_off(&vox[i]);   /* mono: the old note lets go */
            v = voice_pick(t, 0);
            slide = 0;
        }
    } else
        v = voice_pick(t, 0);
    v->age = ++vage;
    voice_on(v, t, note, vel, ticks, slide);
    voice_incs(v, &tp[t]);
}

/* fire step s of track t (bar: the progression bar now) */
static uint8_t ratchet_note[NTR];               /* a second hit owed at the half step, 0 = none */
static uint8_t ratchet_vel[NTR];

static void step_fire(uint32_t t, uint32_t s, uint32_t pos)
{
    const step_t *st = &song.tr[t].st[s];
    uint32_t k;
    int32_t ticks, vel;
    cur_step[t] = s;
    if (!(st->fl & ST_ON) || song.tr[t].mute) return;
    vel = st->vel ? st->vel : 100;
    eng_hit_ms[t] = fm1_ms;
    switch (t) {
    case T_KICK:
        if (lift_q8 >> 8 > 110) return;                         /* the LIFT holds the kick back */
        kick_hit(vel);
        if (song.pump) duck = 1 << 24;
        break;
    case T_909:
        for (k = 0; k < NKIT; k++)
            if ((uint8_t)st->note >> k & 1u) { kit_hit(k, vel); eng_kit_hit_ms[k] = fm1_ms; }
        break;
    case T_PAD: {                                /* the chord sounding now: root, 3rd, and 5th (7th chords: 7th) */
        chord_t c = chord_at(pos % NSTEP);
        int32_t base = 48 + c.root + oct_shift(t);
        ticks = (int32_t)step_ticks(st->len ? st->len : 1u) - 1;
        for (k = 0; k < 3u; k++) {
            int32_t n = base + Q_IV[c.q][k == 2u && chord_ntones(c) == 4u ? 3u : k];
            voice_t *v = &vox[TR_V0[t] + k];
            v->age = ++vage;
            voice_on(v, t, n, vel, ticks > 0 ? ticks : 1, 0);
            voice_incs(v, &tp[t]);
        }
        eng_hit_note[t] = (uint8_t)base;
        break;
    }
    default: {
        int32_t n = clampi(st->note + oct_shift(t), 0, 127);
        ticks = (int32_t)step_ticks(st->len ? st->len : 1u);
        if (st->fl & ST_SLIDE) ticks += 2;                      /* a slide overlaps the next note */
        else if (st->fl & ST_RATCHET) ticks = ticks * 42 / 100; /* Rollwerk's ratchet: two short hits */
        else ticks = ticks * 7 / 8;                             /* a little air between notes */
        note_play(t, n, vel, ticks > 0 ? ticks : 1, st->fl & ST_SLIDE);
        eng_hit_note[t] = (uint8_t)n;
        if (st->fl & ST_RATCHET) { ratchet_note[t] = (uint8_t)n; ratchet_vel[t] = (uint8_t)vel; }
    }
    }
}

/* the snare roll of a build: quarter notes, then eighths, then sixteenths, rising */
static void lift_roll(uint32_t pos)
{
    int32_t l = lift_q8 >> 8, every;
    if (l < 48 || song.tr[T_909].mute) return;
    every = l < 76 ? 4 : l < 100 ? 2 : 1;
    if (pos % (uint32_t)every) return;
    kit_hit(K_SNARE, 30 + l * 3 / 4);
    eng_kit_hit_ms[K_SNARE] = fm1_ms;
}

static void clock_step(uint32_t pos)
{
    uint32_t t;
    for (t = 0; t < NTR; t++) {
        uint32_t len = song.tr[t].len ? song.tr[t].len : 16u;
        step_fire(t, pos % len, pos);
    }
    lift_roll(pos);
}

static void live_drain(void)
{
    while (live_r != live_w) {
        live_t e = live_q[live_r % 32u];
        live_r++;
        if (e.track == T_KICK) { if (e.on) { kick_hit(e.vel); duck = 1 << 24; eng_hit_ms[T_KICK] = fm1_ms; } }
        else if (e.track == T_909) { if (e.on && e.note < NKIT) { kit_hit(e.note, e.vel); eng_kit_hit_ms[e.note] = fm1_ms; } }
        else if (e.on) {
            note_play(e.track, e.note, e.vel, -1, 0);
            eng_hit_ms[e.track] = fm1_ms;
            eng_hit_note[e.track] = e.note;
        } else {
            uint32_t i;
            for (i = TR_V0[e.track]; i < TR_V0[e.track] + TR_NV[e.track]; i++)
                if ((vox[i].tgt_q8 >> 8) == e.note && vox[i].gate_ticks < 0) voice_off(&vox[i]);
        }
    }
}

/* parameters toward their targets: the step's p-lock, else the knob */
static void params_tick(void)
{
    uint32_t t, k;
    for (t = 0; t < NTR; t++) {
        const track_t *tr = &song.tr[t];
        int32_t e[NPAR];
        for (k = 0; k < NPAR; k++) {
            uint32_t lk = eng_play ? tr->lock[cur_step[t]][k] : 0u;
            int32_t target = (int32_t)(lk ? lk - 1u : tr->par[k]) << 8;
            eff_q8[t][k] += (target - eff_q8[t][k]) >> 3;
            e[k] = clampi(eff_q8[t][k] >> 8, 0, 127);
        }
        if (t == T_KICK) { kick_params(e); duck_k = DECAY_Q16[e[PK_DUCKREL] * 40 / 127 + 30]; }
        else if (t == T_909) kit_params(e);
        else {
            if (t == T_ROLL) e[P_RES] = clampi(e[P_RES] + eng_build * 38 / 127, 0, 127);   /* Rollwerk: res + .3 x build */
            e[P_RES] = clampi(e[P_RES] + ((int32_t)song.g[G_MRES] - 64), 0, 120);        /* the RES macro */
            tparam_mel(t, e);
        }
    }
}



static void control_tick(void)
{
    int32_t lt;
    if (eng_start) {
        eng_start = 0;
        eng_pos = 0;
        eng_acc = 0;
        if (eng_play) clock_step(0);
    } else if (eng_play) {
        eng_acc += CR << 16;
        if (eng_acc >= eng_spb_q16) {
            eng_acc -= eng_spb_q16;
            eng_pos++;
            clock_step(eng_pos);
        }
    }
    if (eng_play && eng_acc >= eng_spb_q16 / 2u) {      /* ratchets owed at the half step */
        uint32_t t;
        for (t = 0; t < NTR; t++)
            if (ratchet_note[t]) {
                note_play(t, ratchet_note[t], ratchet_vel[t], (int32_t)step_ticks(1) * 42 / 100, 0);
                ratchet_note[t] = 0;
            }
    }
    live_drain();
    lt = eng_lift_knob < 0 ? 0 : eng_lift_knob; /* LIFT: the knob, the performance */
    lift_q8 += ((lt << 8) - lift_q8) >> 4;
    eng_lift = lift_q8 >> 8;
    lt = eng_play && song.lift[eng_pos % LIFT_STEPS] ? song.lift[eng_pos % LIFT_STEPS] - 1 : 0;   /* BUILD: the lane */
    build_q8 += ((lt << 8) - build_q8) >> 4;
    eng_build = build_q8 >> 8;
    params_tick();
    duck = (int32_t)(((int64_t)duck * duck_k) >> 16);
}

/* the trance gate of track t this tick (Q15 gain): open on the pattern's 16ths for 70 % of the step */
static int32_t gate_gain(uint32_t t)
{
    int32_t depth = eff_q8[t][P_GATE] >> 8;
    uint32_t s = eng_pos & 15u, open;
    if (!depth || !eng_play) return 32767;
    open = (song.tr[t].gate >> s & 1u) && eng_acc < (eng_spb_q16 / 10u) * 7u;
    return open ? 32767 : 32767 - depth * 258;
}

static void app_audio(int32_t *out, uint32_t n)
{
    uint32_t off, t0 = fm1_ticks();
    for (off = 0; off + CR <= n; off += CR) {
        uint32_t t, i, v;
        int32_t *o = out + 2u * off, lift;
        control_tick();
        lift = eng_lift;
        for (i = 0; i < 2u * CR; i++) dsend[i] = vsend[i] = 0;
        for (t = 0; t < NTR; t++) {
            int32_t *tb = tbuf[t], e[4], g, gs, ds, vs, lvl, pk = 0;
            for (i = 0; i < 2u * CR; i++) tb[i] = 0;
            if (t == T_KICK) kick_render(tb);
            else if (t == T_909) kit_render(tb);
            else {
                /* LIFT opens the filters; Rollwerk's BUILD opens ROLL's most of the way (less for LEAD, PAD) */
                int32_t b = eng_build, lc = t == T_BASS ? -(lift > 80 ? (lift - 80) : 0) : lift / 2;
                if (t >= T_ROLL) {
                    int32_t bc = (254 - tp[t].cut) * b * 9 / 1270;
                    if (t != T_ROLL) bc /= 2;
                    if (bc > lc) lc = bc;
                }
                lc += ((int32_t)song.g[G_MFILT] - 64) * 3;           /* the FILTER macro: the whole mix */
                for (v = TR_V0[t]; v < TR_V0[t] + TR_NV[t]; v++)
                    voice_render(&vox[v], &tp[t], lc, tb, TR_STEREO[t]);
            }
            /* level (squared), trance gate, sidechain duck, the build's fades */
            for (i = 0; i < 4u; i++) e[i] = clampi(eff_q8[t][12u + i] >> 8, 0, 127);
            lvl = (e[3] * e[3]) << 1;                                /* 127 -> 32258 */
            if (t == T_BASS && lift > 80) lvl = lvl * (127 - lift) / 47;
            if (t == T_ROLL) lvl = lvl * (102 + eng_build * 51 / 127) / 128;   /* Rollwerk: the saws grow with the build */
            g = mq15(lvl, gate_gain(t));
            if (t != T_KICK && t != T_909 && e[2]) {   /* the duck, scaled by the PUMP macro */
                int32_t dk = clampi(e[2] * (int32_t)song.g[G_MPUMP] / 64, 0, 127);
                g = mq15(g, 32767 - (int32_t)(((int64_t)(duck >> 9) * dk * 258) >> 15));
            }
            ramp_to(&tgain[t], g);
            gs = tgain[t].step;
            g = tgain[t].g;
            ds = clampi(e[0] * e[0] * 2 + (t >= T_BASS ? song.g[G_MSPACE] * 200 : 0), 0, 32767);   /* SPACE throws */
            vs = clampi(e[1] * e[1] * 2 + lift * e[1] * 2 + (t >= T_BASS ? song.g[G_MSPACE] * 160 : 0), 0, 32767);
            for (i = 0; i < CR; i++) {
                int32_t l = mqs(tb[2 * i], g >> 1), r = mqs(tb[2 * i + 1], g >> 1), a;   /* x level / 2 */
                g += gs;
                o[2 * i] += l;
                o[2 * i + 1] += r;
                dsend[2 * i] += mqs(l, ds); dsend[2 * i + 1] += mqs(r, ds);
                vsend[2 * i] += mqs(l, vs); vsend[2 * i + 1] += mqs(r, vs);
                a = l < 0 ? -l : l;
                if (a > pk) pk = a;
            }
            tgain[t].g = g;
            if (pk > eng_meter[t]) eng_meter[t] = pk;
            else eng_meter[t] -= (eng_meter[t] >> 6) + 1;
            if (eng_meter[t] < 0) eng_meter[t] = 0;
        }
        fx_render(dsend, vsend, o, lift > eng_build ? lift : eng_build);
        {
            int32_t drv = 4096 + song.g[G_DRIVE] * 48;               /* x1 .. x2.5 into the clipper */
            static int32_t dc[2];               /* a DC blocker (~5 Hz): the kick's thump is not all above zero */
            uint32_t w = scope_w;
            for (i = 0; i < 2u * CR; i++) {
                int32_t x = o[i] << 4;
                dc[i & 1u] += (x - dc[i & 1u]) >> 11;
                o[i] = soft((((x - dc[i & 1u]) >> 4) * (drv >> 2)) >> 10);
            }
            for (i = 0; i < CR; i += 2u) scope[w++ & 511u] = (int16_t)((o[2u * i] + o[2u * i + 1u]) >> 1);
            scope_w = w;
        }
    }
    {
        uint32_t dt = fm1_ticks() - t0, q = dt * 256u / (5805u * FM1_TICKS_PER_US);
        eng_load_q8 = (eng_load_q8 * 15u + q) >> 4;
        if (q > eng_peak_q8) eng_peak_q8 = q;
    }
}

/* ---- the main loop's side */
static void eng_tempo(uint32_t bpm10)           /* samples per 16th: FS * 60 / (bpm * 4), Q16 */
{
    uint32_t spb = ((FS * 150u) << 8) / bpm10;   /* (FS * 15 * 10) / bpm10, Q8 */
    eng_spb_q16 = spb << 8;
    fx_params(eng_spb_q16, song.g, eng_lift);
}

static void live_note(uint32_t on, uint32_t t, uint32_t note, uint32_t vel)
{
    live_t *e = &live_q[live_w % 32u];
    e->on = (uint8_t)on; e->track = (uint8_t)t; e->note = (uint8_t)note; e->vel = (uint8_t)vel;
    RING_PUBLISH();
    live_w++;
}
