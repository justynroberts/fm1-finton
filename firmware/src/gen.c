/* SPDX-License-Identifier: GPL-3.0-only */
/* Finton generator. Main loop only.
 *   ROLL   Rollwerk's roller, ported from its pattern code: chord tones voice-led around a register
 *          anchor, walked by a figure (LIFT, 1-5-8, CLIMB, CYCLE), a colour tone, the pedal or third
 *          on the offbeats, a sus4 turn into bar 8, accents and a held note on fixed steps
 *   BASS   Rollwerk's bass: ROLL (every 16th), OFFBEAT, or DRAG (the chord's root held, gliding)
 *   KICK   four on the floor, a breath before the turnaround now and then
 *   909    offbeat open hats, 16th closed-hat grooves, claps on 2 and 4, a fill
 *   LEAD   four bars, call and response over the chords, climbing to a long note on the root
 *   PAD    the chord, a bar long, trance-gated
 *   BUILD  Rollwerk's build over 8 bars (rise, fall, arc; smoothstep), drawn into the lane */

static uint32_t gen_rng = 0xA5A5F00Du;
static uint32_t rnd(uint32_t n) { return n ? xs32(&gen_rng) % n : 0u; }
static void gen_seed(uint32_t t)
{
    gen_rng = (uint32_t)song.tr[t].seed * 2654435761u ^ (t + 1u) * 0x9E3779B9u ^ (uint32_t)song.seed * 40503u;
    if (!gen_rng) gen_rng = 1;
    xs32(&gen_rng); xs32(&gen_rng);
}

static void clear_steps(uint32_t t, uint32_t len)
{
    uint32_t s;
    track_t *tr = &song.tr[t];
    for (s = 0; s < NSTEP; s++) { tr->st[s].note = 0; tr->st[s].vel = 100; tr->st[s].len = 1; tr->st[s].fl = 0; }
    tr->len = (uint8_t)len;
}
static void clear_locks(uint32_t t)
{
    uint32_t s, k;
    for (s = 0; s < NSTEP; s++) for (k = 0; k < NPAR; k++) song.tr[t].lock[s][k] = 0;
}

static void put(uint32_t t, uint32_t s, int32_t note, uint32_t vel, uint32_t len, uint32_t fl)
{
    step_t *st = &song.tr[t].st[s % NSTEP];
    st->note = (int8_t)note; st->vel = (uint8_t)clampi((int32_t)vel, 1, 127); st->len = (uint8_t)len; st->fl = (uint8_t)(ST_ON | fl);
}

/* Rollwerk's shapes over the phrase: x 0..256 -> 0..256 */
static int32_t shape(uint32_t sh, int32_t x)
{
    x = clampi(x, 0, 256);
    switch (sh) {
    case SHAPE_RISE: return x;
    case SHAPE_FALL: return 256 - x;
    case SHAPE_ARC: return 256 - (2 * x - 256 < 0 ? 256 - 2 * x : 2 * x - 256);
    default: return 0;
    }
}

/* ---- the roller */
static uint32_t chord_tones(chord_t c, int32_t *h)   /* every chord tone from C2 to C8 */
{
    uint32_t n = 0, k, nt = chord_ntones(c);
    int32_t m;
    for (m = 36; m <= 108; m++)
        for (k = 0; k < nt; k++)
            if ((uint32_t)((m - c.root) % 12 + 12) % 12u == Q_IV[c.q][k]) { h[n++] = m; break; }
    return n;
}
static uint32_t nearest(const int32_t *h, uint32_t n, int32_t t)   /* ties go to the later one */
{
    uint32_t r, best = 0;
    for (r = 0; r < n; r++) {
        int32_t a = h[r] - t, b = h[best] - t;
        if ((a < 0 ? -a : a) <= (b < 0 ? -b : b)) best = r;
    }
    return best;
}
static uint32_t colour_pc(chord_t c)
{
    return Q_IV[c.q][3] != 255u ? (c.root + Q_IV[c.q][3]) % 12u : (c.root + Q_COLOUR[c.q]) % 12u;
}

#define FIG_C 99                                /* the colour tone between the 3rd and 4th chord tone */
static const int8_t FIG_TAB[4][8] = {{0, 2, FIG_C, 2, 0, 4, 5, 4}, {0, 2, 3, 2}, {0, 1, 2, 3}, {0, 1, 2, 3, 2, 1}};
static const uint8_t FIG_LEN[4] = {8, 4, 4, 6};
static const uint8_t AUTO_FIG[4] = {FIG_LIFT, FIG_CLASSIC, FIG_CLIMB, FIG_CYCLE};
static const int8_t HOLD_STEP[8] = {12, 12, 6, 6, 0, 0, -1, -1};   /* the note held two steps, per bar */
static const uint8_t VEL4[4] = {110, 88, 110, 99};                  /* Rollwerk: 1, .8, 1, .9 (x 110) */

static int32_t roll_pick(const int32_t *h, uint32_t n, uint32_t g, int32_t e)
{
    int32_t t = clampi((int32_t)g + e, 0, (int32_t)n - 1);
    while (h[t] > 96 && t > 0) t--;
    return h[t];
}

static void gen_roll(void)
{
    uint32_t bar, s, fig = song.figure == FIG_AUTO ? AUTO_FIG[song.tr[T_ROLL].seed % 4u] : song.figure;
    const int8_t *r = FIG_TAB[fig - 1u];
    uint32_t rl = FIG_LEN[fig - 1u], span = 0, i;
    int32_t h[64], u[64];
    for (i = 0; i < rl; i++) { uint32_t v = r[i] == FIG_C ? 3u : (uint32_t)r[i]; if (v > span) span = v; }
    clear_steps(T_ROLL, 128);
    for (bar = 0; bar < NBAR; bar++) {
        chord_t whole = chord_of(song.deg[bar]), sus = whole;
        int32_t anchor = 64;
        int sus_turn = song.sus_turn && bar == 7u && (whole.q == Q_MAJ || whole.q == Q_MIN) && song.half[bar] == 255u;
        uint32_t nu;
        if (song.reg != SHAPE_OFF)              /* the register over the phrase: in pairs of bars */
            anchor = 64 + shape(song.reg, (int32_t)(bar / 2u) * 256 / 3) * (12 + song.reg_amt * 16 / 127) / 256;
        sus.q = Q_SUS4;
        nu = chord_tones(sus, u);
        for (s = 0; s < 16u; s++) {
            chord_t c = chord_at(bar * 16u + s);
            uint32_t n = sus_turn && s < 8u ? nu : chord_tones(c, h), g, x;
            const int32_t *tones = sus_turn && s < 8u ? u : h;
            int32_t v = r[(bar * 16u + s) % rl], y, vel;
            int hold = HOLD_STEP[bar] == (int8_t)s;
            g = nearest(tones, n, anchor);
            g = g > span / 2u ? g - span / 2u : 0u;
            while (g > 0u && (g + span >= n || tones[g + span] > 96)) g--;
            if (v == FIG_C) {                   /* the colour tone nearest the middle of the 3rd and 4th */
                int32_t mid = (roll_pick(tones, n, g, 2) + roll_pick(tones, n, g, 3)) / 2, m, pc = (int32_t)colour_pc(c);
                y = -1;
                for (m = roll_pick(tones, n, g, 1) + 1; m <= 96; m++)
                    if (m % 12 == pc && (y < 0 || (m - mid < 0 ? mid - m : m - mid) < (y - mid < 0 ? mid - y : y - mid))) y = m;
                if (y < 0) y = roll_pick(tones, n, g, 2);
            } else
                y = roll_pick(tones, n, g, v);
            if (song.top != TOP_OFF && (s & 1u)) {   /* the pedal or the third on the offbeats */
                int32_t want = (int32_t)(song.top == TOP_PEDAL ? c.root : (c.root + Q_IV[c.q][1]) % 12u), best = -1, tgt = anchor + 12;
                for (x = 0; x < n; x++) {
                    int32_t m = tones[x];
                    if (m % 12 != want || m > 96) continue;
                    if (best < 0 || (m - tgt < 0 ? tgt - m : m - tgt) < (best - tgt < 0 ? tgt - best : best - tgt)) best = m;
                }
                if (best >= 0) y = best;
            }
            vel = VEL4[s & 3u] + ((s == 0u || hold) ? 16 : 0);
            put(T_ROLL, bar * 16u + s, y, (uint32_t)(vel > 126 ? 126 : vel), hold ? 2u : 1u,
                song.reg != SHAPE_OFF && bar == 7u && s >= 12u ? ST_RATCHET : 0u);
        }
    }
}

/* ---- the bass: the chord's root, C2 .. B2 (Rollwerk: 36 + root) */
static void gen_bass(void)
{
    uint32_t s;
    clear_steps(T_BASS, 128);
    for (s = 0; s < NSTEP; s++) {
        chord_t c = chord_at(s);
        int32_t n = 36 + c.root;
        uint32_t q = s & 3u, start = (s & 15u) == 0u || ((s & 15u) == 8u && song.half[(s / 16u) % NBAR] != 255u);
        switch (song.bass) {
        case BASS_ROLL: put(T_BASS, s, n, q == 0u ? 92 : q == 2u ? 112 : 100, 1, 0); break;
        case BASS_OFFBEAT: if (q == 2u) put(T_BASS, s, n, 112, 2, 0); break;
        case BASS_DRAG:
            if (start) put(T_BASS, s, n, 108, song.half[(s / 16u) % NBAR] != 255u ? 8u : 16u, ST_SLIDE);
            break;
        default: break;
        }
    }
}

static void gen_kick(void)
{
    uint32_t s;
    clear_steps(T_KICK, 32);
    for (s = 0; s < 32u; s += 4u) put(T_KICK, s, 0, s % 16u ? 112 : 120, 1, 0);
    if (rnd(3) == 0) song.tr[T_KICK].st[28].fl = 0;              /* a breath before the turnaround */
}

static void gen_909(void)
{
    uint32_t s, style = rnd(3), ride = rnd(2);
    clear_steps(T_909, 32);
    for (s = 0; s < 32u; s++) {
        uint32_t m = 0, q = s & 3u, vel = 96;
        if (q == 2u) m |= 1u << K_OH;                               /* the offbeat open hat */
        if (style == 0 && q != 2u) { m |= 1u << K_CH; vel = q == 0u ? 70 : 92; }
        if (style == 1 && (q == 1u || q == 3u)) { m |= 1u << K_CH; vel = q == 3u ? 100 : 64; }
        if (style == 2 && q == 3u) m |= 1u << K_CH;
        if ((s & 7u) == 4u) { m |= 1u << K_CLAP; vel = 110; }       /* 2 and 4 */
        if (ride && (s & 1u) == 0u) m |= 1u << K_RIDE;
        if (s >= 28u && rnd(2)) m |= 1u << K_SNARE;                 /* a small fill into bar 1 */
        if (m) put(T_909, s, (int32_t)m, vel, 1, 0);
    }
}

/* ---- the lead: the first four bars' chords, then the same answer over the last four */
static void gen_lead(void)
{
    static const struct { uint16_t on; uint8_t len[8]; } RH[5] = {
        {0x9228u /* 0 3 6 10 12 */, {3, 3, 4, 2, 4}},
        {0x924Au /* 0 3 6 9 12 14 */, {3, 3, 3, 3, 2, 2}},
        {0xA0A0u /* 0 2 8 10 */, {2, 6, 2, 6}},
        {0x8888u /* 0 4 8 12 */, {4, 4, 4, 4}},
        {0x8200u /* 0 6 */, {6, 10}},
    };
    uint32_t a = rnd(4), b = rnd(2) ? 4u : 2u, bar, s, half;
    int32_t cur = 72 + song.key % 12 - (song.key > 4u ? 12 : 0), slide = (int32_t)rnd(2);
    clear_steps(T_LEAD, 128);
    for (half = 0; half < 2u; half++)
        for (bar = 0; bar < 4u; bar++) {
            uint32_t r = bar == 3u ? b : a, k = 0, gb = half * 4u + bar;
            int32_t lo = 64 + (int32_t)bar * 2, hi = 79 + (int32_t)bar * 3;   /* the range climbs */
            for (s = 0; s < 16u; s++) {
                chord_t c = chord_at(gb * 16u + s);
                int32_t h[64], n = cur, best = 999;
                uint32_t nt, x;
                if (!(RH[r].on >> (15u - s) & 1u)) continue;
                nt = chord_tones(c, h);
                if ((s & 3u) == 0u) {           /* on the beat: a chord tone near, preferring up */
                    for (x = 0; x < nt; x++) {
                        int32_t d = h[x] - cur;
                        if (h[x] < lo || h[x] > hi || h[x] == cur) continue;
                        d = (d < 0 ? -d : d) + (int32_t)rnd(3) + (bar < 3u && h[x] < cur ? 2 : 0);
                        if (d < best) { best = d; n = h[x]; }
                    }
                } else {                        /* between: a step or two along the scale */
                    n = scale_step(cur, rnd(3) ? 1 : -1);
                    if (rnd(2)) n = scale_step(n, n > cur ? 1 : -1);
                    n = clampi(n, lo, hi);
                }
                if (bar == 3u && !(RH[r].on & ((1u << (15u - s)) - 1u))) {   /* the last note: the root */
                    int32_t root = c.root + 12 * ((cur - (int32_t)c.root + 6) / 12);
                    n = root;
                }
                put(T_LEAD, gb * 16u + s, n, (s & 3u) == 0u ? 112 : 96, RH[r].len[k],
                    (slide && k && (n - cur <= 2 && cur - n <= 2)) ? ST_SLIDE : 0u);
                cur = n;
                k++;
            }
        }
}

static void gen_pad(void)
{
    static const uint16_t GATES[5] = {0xEEEEu, 0xB6DBu, 0xDDDDu, 0xAAAAu, 0xFBEEu};
    uint32_t b;
    clear_steps(T_PAD, 128);
    for (b = 0; b < NBAR; b++) {                /* a chord a bar; a split bar changes at the half */
        if (song.half[b] != 255u) { put(T_PAD, b * 16u, 0, 100, 8, 0); put(T_PAD, b * 16u + 8u, 0, 96, 8, 0); }
        else put(T_PAD, b * 16u, 0, 100, 16, 0);
    }
    song.tr[T_PAD].gate = GATES[rnd(5)];
}

/* Rollwerk's build over the eight bars: smoothstep of the shape, times the amount */
static void gen_build(void)
{
    uint32_t s;
    for (s = 0; s < LIFT_STEPS; s++) {
        int32_t a = shape(song.build, (int32_t)s * 256 / 127), o = (a * a / 256) * (768 - 2 * a) / 256;   /* a^2 (3 - 2a) */
        song.lift[s] = (uint8_t)(1 + o * song.build_amt / 256);
    }
}

static void gen_track(uint32_t t)
{
    gen_seed(t);
    switch (t) {
    case T_KICK: gen_kick(); break;
    case T_909: gen_909(); break;
    case T_BASS: gen_bass(); break;
    case T_ROLL: gen_roll(); break;
    case T_LEAD: gen_lead(); break;
    case T_PAD: gen_pad(); break;
    }
    song.tr[t].edited = 0;
}

/* the harmony or the roller's settings changed: derive the tracks that follow them again */
static void harmony_changed(void)
{
    uint32_t t;
    for (t = T_BASS; t < NTR; t++)
        if (!song.tr[t].edited) {
            uint16_t g = song.tr[t].gate;
            gen_track(t);
            song.tr[t].gate = g;                /* (keep the pad's gate) */
        }
}

static void gen_song(void)
{
    uint32_t t;
    gen_rng ^= (uint32_t)song.seed * 0x85EBCA6Bu + 1u;
    prog_load(MOOD_START[song.mood] + rnd(MOOD_START[song.mood + 1u] - MOOD_START[song.mood]));
    for (t = 0; t < NTR; t++) { clear_locks(t); gen_track(t); }
    gen_build();
}

/* ROLL's two voices from Rollwerk: SUPERSAW (wide, held) and PLUCK (tight, a sharp filter envelope) */
static void voice_preset(uint32_t v) { preset_apply(T_ROLL, v & 1u); }

static void song_init(uint32_t seed)
{
    uint32_t t;
    song.magic = SONG_MAGIC;
    song.ver = SONG_VER;
    song.bpm10 = 1380;
    song.key = 9;                               /* A minor, Epic 1, as Rollwerk opens */
    song.seed = (uint8_t)seed;
    song.figure = FIG_AUTO; song.top = TOP_OFF; song.sus_turn = 1; song.reg = SHAPE_OFF; song.reg_amt = 102;
    song.build = SHAPE_RISE; song.build_amt = 108; song.bass = BASS_ROLL; song.pump = 1;
    song.g[G_DTIME] = 30; song.g[G_DFB] = 64; song.g[G_DTONE] = 60;
    song.g[G_VSIZE] = 96; song.g[G_VDAMP] = 60; song.g[G_DRIVE] = 30;
    song.g[G_MOTION] = 127;
    song.g[G_MFILT] = 64; song.g[G_MRES] = 64; song.g[G_MSPACE] = 0; song.g[G_MPUMP] = 64;
    for (t = 0; t < NTR; t++) {
        preset_apply(t, 0);                     /* each track starts on its first preset */
        song.tr[t].mute = 0;
        song.tr[t].seed = (uint8_t)(seed + t * 17u);
    }
    song.mood = 0;
    if (seed <= 1u) {                           /* the first song is Rollwerk's first: Epic 1 */
        for (t = 0; t < NTR; t++) { clear_locks(t); gen_seed(t); }
        prog_load(0);
        for (t = 0; t < NTR; t++) gen_track(t);
        gen_build();
    } else
        gen_song();
}
