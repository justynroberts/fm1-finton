/* SPDX-License-Identifier: GPL-3.0-only */
/* Finton screens, in the Matrix's green. Every frame is drawn whole into scr (render.c); r_flush sends what changed.
 *
 *   RAIN     the six tracks as lanes of falling code: the coming steps rain down onto a line, a note's
 *            glyph and column are its pitch; old code falls behind, thicker as LIFT and BUILD rise
 *   STEPS    all six tracks for the bar page, and the selected one in detail with its p-locks
 *   GLOBAL   the panels: the key and the eight chord tiles, the roller, the build, the groove, system
 *   ABOUT    the dog, and who made it */

enum { V_HOME, V_SEQ, V_GLO, V_ABOUT, V_SPLASH };
#define NGPAGE 6u

static struct {
    uint8_t view, prev_view, track, page, gpage, barpage, kitv, rec, calm;
    int8_t oct;
    int8_t held;                                /* the step key held (index in the page), -1 none */
    uint8_t held_edit, glo_bar, slot;
    int8_t touched;                             /* the knob last turned, -1 none */
    uint32_t touched_ms, lift_ms, drop_ms, msg_ms, home_ms, cut_ms;
    int32_t lift, lift_peak;
    char msg[24];
} ui = {V_SPLASH, V_HOME, T_ROLL, PG_PERFORM, 0, 0, 0, 0, 0, 0, -1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0, ""};

static void ui_say(const char *s)
{
    uint32_t i = 0;
    while (s[i] && i < sizeof ui.msg - 1u) { ui.msg[i] = s[i]; i++; }
    ui.msg[i] = 0;
    ui.msg_ms = fm1_ms ? fm1_ms : 1u;
}

/* ---- the GLOBAL pages: four knobs each, Rollwerk's controls */
static const char *const GPAGE_NAME[NGPAGE] = {"HARMONY", "ROLLER", "BUILD", "GROOVE", "ECHO+HALL", "SYSTEM"};
static const char *const GKNOB[NGPAGE][4] = {{"KEY", "MODE", "MOOD", "PROG"}, {"FIGURE", "TOP", "SUS", "VOICE"},
                                              {"BUILD", "AMOUNT", "REGIST", "R.AMT"}, {"BASS", "PUMP", "DRIVE", "HALL"},
                                              {"DELAY", "FEEDBK", "TONE", "DAMP"}, {"BPM", "SLOT", "MOVE", ""}};
/* a GLOBAL knob's value as text, and as a share of its range (0..54 px) */
static int32_t glo_value(uint32_t pg, uint32_t k, char *b)
{
    static const char *const DT[5] = {"1/8", "3/16", "1/4", "3/8", "1/2"};
    const char *t = 0;
    int32_t v = 0, n = 127;
    switch (pg * 4u + k) {
    case 0: t = (key_flats() ? NOTE_FLAT : NOTE_NAME)[song.key]; v = song.key; n = 11; break;
    case 1: t = song.mode ? "MAJOR" : "MINOR"; v = song.mode; n = 1; break;
    case 2: t = MOOD_NAME[song.mood]; v = song.mood; n = NMOOD - 1; break;
    case 3: v = song.prog - MOOD_START[song.mood]; n = MOOD_START[song.mood + 1u] - MOOD_START[song.mood] - 1; r_int(b, v + 1, 0); break;
    case 4: t = FIG_NAME[song.figure]; v = song.figure; n = NFIG - 1; break;
    case 5: t = TOP_NAME[song.top]; v = song.top; n = NTOP - 1; break;
    case 6: t = song.sus_turn ? "ON" : "OFF"; v = song.sus_turn; n = 1; break;
    case 7: t = song.voice ? "PLUCK" : "SAW"; v = song.voice; n = 1; break;
    case 8: t = SHAPE_NAME[song.build]; v = song.build; n = NSHAPE - 1; break;
    case 9: v = song.build_amt; r_int(b, v * 100 / 127, 0); break;
    case 10: t = SHAPE_NAME[song.reg]; v = song.reg; n = NSHAPE - 1; break;
    case 11: v = song.reg_amt; r_int(b, v * 100 / 127, 0); break;
    case 12: t = BASS_NAME[song.bass]; v = song.bass; n = NBASS - 1; break;
    case 13: t = song.pump ? "ON" : "OFF"; v = song.pump; n = 1; break;
    case 14: v = song.g[G_DRIVE]; r_int(b, v, 0); break;
    case 15: v = song.g[G_VSIZE]; r_int(b, v, 0); break;
    case 16: v = song.g[G_DTIME]; t = DT[v * 5 / 128]; break;
    case 17: v = song.g[G_DFB]; r_int(b, v, 0); break;
    case 18: v = song.g[G_DTONE]; r_int(b, v, 0); break;
    case 19: v = song.g[G_VDAMP]; r_int(b, v, 0); break;
    case 20: v = song.bpm10 / 10 - 60; n = 140; r_int(b, song.bpm10 / 10, 0); break;
    case 21: v = ui.slot; n = NSLOT - 1; r_int(b, v + 1, 0); break;
    case 22: t = ui.calm ? "CALM" : "FULL"; v = !ui.calm; n = 1; break;
    default: t = ""; v = 0; n = 1;
    }
    if (t) { uint32_t i = 0; while (t[i] && i < 9u) { b[i] = t[i]; i++; } b[i] = 0; }
    return n ? v * 54 / n : 0;
}

/* the value a knob shows: its step lock while a step is held, else the track's knob */
static int32_t par_value(uint32_t t, uint32_t p)
{
    if (ui.view == V_SEQ && ui.held >= 0) {
        uint32_t lk = song.tr[t].lock[(ui.barpage * 16u + (uint32_t)ui.held) % NSTEP][p];
        if (lk) return (int32_t)lk - 1;
    }
    return song.tr[t].par[p];
}

/* a parameter value as text */
static void par_text(uint32_t t, uint32_t p, int32_t v, char *b)
{
    if (t >= T_BASS && p == P_OCT) { int32_t o = v * 5 / 128 - 2; b[0] = o < 0 ? '-' : '+'; b[1] = (char)('0' + (o < 0 ? -o : o)); b[2] = 0; return; }
    r_int(b, v, 0);
}

static void note_text(int32_t n, char *b)
{
    const char *p = (key_flats() ? NOTE_FLAT : NOTE_NAME)[((n % 12) + 12) % 12];
    uint32_t i = 0;
    while (*p) b[i++] = *p++;
    r_int(b + i, n / 12 - 1, 0);
}

/* ---- the frame around every screen */
static void draw_header(void)
{
    char b[16], c[12];
    uint32_t pos = eng_pos % NSTEP, bar = pos / 16u, beat = (pos / 4u) % 4u, nb = (bar + 1u) % NBAR;
    int32_t x;
    r_image(2, 0, LOGO_S, LOGO_S_W, LOGO_S_H);
    r_int(b, song.bpm10 / 10, 0);
    x = r_text(&F16, 26, 1, b, K_N5);
    b[0] = '.'; b[1] = (char)('0' + song.bpm10 % 10); b[2] = 0;
    x = r_text(&F12, x, 4, b, K_N3);
    chord_name(chord_at(pos), c);
    x = r_text(&F16, x + 8, 1, c, eng_play ? K_N5 : K_N4);
    x = r_text(&F12, x + 4, 4, chord_roman((pos & 15u) >= 8u && song.half[bar] != 255u ? song.half[bar] : song.deg[bar]), K_N3);
    chord_name(chord_of(song.deg[nb]), c);
    x = r_text(&F12, x + 6, 4, ">", K_N2);
    if (x < 150) r_text(&F12, x + 2, 4, c, K_N3);
    /* right: bar.beat, then the transport */
    b[0] = (char)('1' + bar); b[1] = '.'; b[2] = (char)('1' + beat); b[3] = 0;
    r_text_r(&F12, 204, 4, b, eng_play ? K_N4 : K_N2);
    if (ui.rec) r_fill(212, 5, 7, 7, K_RED);    /* the red pill */
    else r_line(212, 5, 218, 11, K_N2, 256), r_line(218, 5, 212, 11, K_N2, 256);
    if (eng_play) { r_line(226, 4, 234, 8, K_ACC, 256); r_line(234, 8, 226, 12, K_ACC, 256); r_vline(226, 4, 12, K_ACC); }
    else r_fill(227, 5, 7, 7, K_N3);
    r_hline(0, W - 1, 19, K_N1);
}

/* the strip at the bottom: track, page, the four knobs */
static void draw_strip(void)
{
    uint32_t t = ui.track, k, p;
    char b[12];
    int32_t x;
    r_fill(0, 194, W, H - 194, K_PANEL);
    r_hline(0, W - 1, 194, K_N2);
    if (ui.view == V_GLO) {
        x = r_text(&F12, 4, 198, "GLOBAL", K_N5);
        x = r_text(&F12, x + 6, 198, GPAGE_NAME[ui.gpage], K_N4);
        for (k = 0; k < NGPAGE; k++) r_fill(206 + (int32_t)k * 6, 202, 4, 4, k == ui.gpage ? K_ACC : K_N2);
    } else {
        x = r_text(&F12, 4, 198, TR_NAME[t], song.tr[t].mute ? K_N2 : K_N5);
        x = r_text(&F12, x + 6, 198, preset_name(t), K_N4);
        if (song.tr[t].mute) x = r_text(&F12, x + 6, 198, "MUTE", K_N3);
        x = r_text(&F12, x + 6, 198, PAGE_NAME[ui.page], K_N4);
        if (ui.view == V_SEQ && ui.held >= 0) {
            uint32_t s = ui.barpage * 16u + (uint32_t)ui.held;
            const step_t *st = &song.tr[t].st[s];
            r_int(b, (int32_t)s + 1, 0);
            x = r_text(&F12, x + 8, 198, "STEP ", K_N3);
            x = r_text(&F12, x, 198, b, K_N5);
            if ((st->fl & ST_ON) && t >= T_BASS && t != T_PAD) {
                note_text(st->note + oct_shift(t), b);
                x = r_text(&F12, x + 6, 198, b, K_N5);
                r_int(b, st->len, 0);
                x = r_text(&F12, x + 6, 198, "L", K_N3);
                r_text(&F12, x, 198, b, K_N4);
            }
        }
    }
    {                                           /* CUTOFF (PRESETS), always here, as Rollwerk's transport slider */
        uint32_t ct = ui.track >= T_BASS ? ui.track : T_ROLL;
        int32_t v = song.tr[ct].par[P_CUT], xr = ui.view == V_GLO ? 200 : 236, hot = ui.cut_ms && fm1_ms - ui.cut_ms < 1500u;
        r_fill(xr - 30, 203, 30, 1, K_N2);
        r_fill(xr - 30, 202, v * 30 / 127, 3, hot ? K_ACC : K_N4);
        r_text_r(&F12, xr - 34, 198, ui.track < T_BASS && hot ? "RLL CUT" : "CUT", hot ? K_ACC : K_N3);
    }
    for (k = 0; k < 4u; k++) {                  /* the four knobs */
        int32_t x0 = 4 + (int32_t)k * 59, v, w;
        const char *name;
        int hot = ui.touched == (int8_t)k && fm1_ms - ui.touched_ms < 1500u;
        uint32_t locked = 0;
        if (ui.view == V_GLO) {
            w = glo_value(ui.gpage, k, b);
            r_text(&F12, x0, 212, GKNOB[ui.gpage][k], hot ? K_ACC : K_N4);
            r_text_r(&F12, x0 + 54, 224, b, K_N5);
            r_fill(x0, 238, 54, 1, K_N2);
            if (w > 0) r_fill(x0, 237, w, 2, hot ? K_ACC : K_N4);
            continue;
        }
        if (ui.page == PG_PERFORM) {            /* the macros: FILTER and RES centred, SPACE and PUMP from 0 */
            static const uint8_t G[4] = {G_MFILT, G_MRES, G_MSPACE, G_MPUMP};
            int32_t mv = song.g[G[k]], c = k == 2u ? 0 : 64;
            if (k == 2u) r_int(b, mv, 0);
            else { b[0] = mv >= c ? '+' : '-'; r_int(b + 1, mv >= c ? mv - c : c - mv, 0); }
            r_text(&F12, x0, 212, MACRO_NAME[k], hot ? K_ACC : K_N4);
            r_text_r(&F12, x0 + 54, 224, b, mv != c ? K_N5 : K_N3);
            r_fill(x0, 238, 54, 1, K_N2);
            if (k == 2u) { if (mv) r_fill(x0, 237, mv * 54 / 127, 2, hot ? K_ACC : K_N4); }
            else {                              /* from the centre out */
                int32_t w2 = (mv - 64) * 27 / 64;
                r_fill(x0 + 26, 235, 1, 4, K_N3);
                if (w2 > 0) r_fill(x0 + 27, 237, w2, 2, hot ? K_ACC : K_N4);
                if (w2 < 0) r_fill(x0 + 26 + w2, 237, -w2, 2, hot ? K_ACC : K_N4);
            }
            continue;
        }
        p = ui.page * 4u + k;
        name = par_name(t, p);
        if (!name[0]) { r_fill(x0, 238, 54, 1, K_N2); continue; }
        v = par_value(t, p);
        if (ui.view == V_SEQ && ui.held >= 0) locked = song.tr[t].lock[(ui.barpage * 16u + (uint32_t)ui.held) % NSTEP][p];
        r_text(&F12, x0, 212, name, hot ? K_ACC : K_N4);
        par_text(t, p, v, b);
        r_text_r(&F12, x0 + 54, 224, b, locked ? K_ACC : K_N5);
        w = v * 54 / 127;
        r_fill(x0, 238, 54, 1, K_N2);
        if (w > 0) r_fill(x0, 237, w, 2, hot ? K_ACC : K_N4);
        {                                       /* a tick where recorded motion / locks live in this bar */
            uint32_t s, any = 0, base = (eng_pos / 16u) * 16u;
            for (s = 0; s < 16u; s++) any |= song.tr[t].lock[(base + s) % (song.tr[t].len ? song.tr[t].len : 16u)][p];
            if (any) r_fill(x0, 226, 3, 3, K_ACC);
        }
    }
    if (ui.msg_ms && fm1_ms - ui.msg_ms < 1400u) {            /* a passing message, over the track line */
        int32_t w = r_textw(&F12, ui.msg) + 10;
        r_fill(W - w - 2, 196, w, 14, K_ACC);
        r_text_r(&F12, W - 7, 198, ui.msg, K_ACC_FG);
    }
}

/* ---- ORBIT: the HOME screen, a living cell. A bar is a turn. The six tracks are membranes, KICK inside,
 * PAD outside, each a blend of slow-drifting waves: the cutoff is how many ripples it carries (and morphs
 * between counts as it moves), the resonance sharpens them, the level makes it breathe, and the ripples run
 * bigger on one side than the other. Each membrane's centre drifts. The beat is a swell of light that travels
 * round every ring; notes ride the membranes and bloom as they play; spores drift out of the nucleus on the
 * kick and from the selected track's notes. LIFT feeds the nucleus; the drop bursts it. Last frame's light
 * fades instead of vanishing. */
#define OX 120
#define OY 108
#define ONP 96u                                 /* points a membrane */
#define SC_TOP 20
#define SC_BOT 193
#define NSPORE 40

static uint32_t hash32(uint32_t x) { x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16; return x; }
static uint8_t home_drawn;                      /* the scene holds last frame's light (for the afterglow) */
static uint32_t cell_t;                         /* the cell's own clock: phases drift on it */
static struct { int32_t x, y, vx, vy; uint16_t life, max; uint16_t c; } spore[NSPORE];
static uint32_t spore_rng = 0xC0FFEE1u;

static void spore_add(int32_t x, int32_t y, uint32_t speed, uint32_t c)
{
    uint32_t i, best = 0, a = xs32(&spore_rng);
    for (i = 0; i < NSPORE; i++) if (spore[i].life < spore[best].life) best = i;
    spore[best].x = x; spore[best].y = y;
    spore[best].vx = (psine(a) >> 8) * (int32_t)speed / 64;
    spore[best].vy = -(psine(a + 0x40000000u) >> 8) * (int32_t)speed / 64;
    spore[best].max = spore[best].life = (uint16_t)(500u + xs32(&spore_rng) % 900u);
    spore[best].c = (uint16_t)c;
}

static void spores(uint32_t dt)
{
    uint32_t i;
    for (i = 0; i < NSPORE; i++) {
        int32_t cx, cy;
        if (!spore[i].life) continue;
        spore[i].life = spore[i].life > dt ? (uint16_t)(spore[i].life - dt) : 0u;
        cx = spore[i].x - OX * 16; cy = spore[i].y - OY * 16;
        spore[i].vx += (-cy >> 9) + (psine(cell_t * 3u + i * (1u << 24)) >> 13);   /* a slow curl, a little drift */
        spore[i].vy += (cx >> 9) + (psine(cell_t * 5u + i * (35u << 20)) >> 13);
        spore[i].vx -= spore[i].vx >> 5; spore[i].vy -= spore[i].vy >> 5;
        spore[i].x += spore[i].vx * (int32_t)dt / 32; spore[i].y += spore[i].vy * (int32_t)dt / 32;
        if (spore[i].y < SC_TOP * 16 || spore[i].y > SC_BOT * 16) { spore[i].life = 0; continue; }
        r_disc(spore[i].x, spore[i].y, 16 + (int32_t)(spore[i].max - spore[i].life) / 40, spore[i].c,
               (uint32_t)spore[i].life * 200u / spore[i].max);
    }
}

/* smooth value noise round a circle: cell c of 16 around, interpolated, evolving with z (Q15 out) */
static int32_t vnoise(uint32_t th, uint32_t z, uint32_t seed)
{
    uint32_t c = th >> 28, f = (th >> 12) & 0xFFFFu, zc = z >> 24, zf = (z >> 8) & 0xFFFFu;
    int32_t a = (int32_t)(hash32(seed + c * 131u + zc * 7919u) & 0xFFFFu) - 32768;
    int32_t b = (int32_t)(hash32(seed + ((c + 1u) & 15u) * 131u + zc * 7919u) & 0xFFFFu) - 32768;
    int32_t a2 = (int32_t)(hash32(seed + c * 131u + (zc + 1u) * 7919u) & 0xFFFFu) - 32768;
    int32_t b2 = (int32_t)(hash32(seed + ((c + 1u) & 15u) * 131u + (zc + 1u) * 7919u) & 0xFFFFu) - 32768;
    uint32_t sf = (f * f >> 16) * (196608u - 2u * f) >> 16, sz = (zf * zf >> 16) * (196608u - 2u * zf) >> 16;   /* smoothstep */
    int32_t lo = a + (int32_t)(((int64_t)(b - a) * sf) >> 16), hi = a2 + (int32_t)(((int64_t)(b2 - a2) * sf) >> 16);
    return lo + (int32_t)(((int64_t)(hi - lo) * sz) >> 16);
}

typedef struct {
    int32_t cx, cy, r0;                         /* centre (1/16 px), resting radius (px) */
    uint32_t lobes_q8;                          /* ripples, fractional: it morphs between counts */
    int32_t amp, res;                           /* 1/16 px; 0..127 */
    uint32_t ph1, ph2, ph3, hand;               /* drifting phases; the beat's angle */
    int32_t swell;                              /* how far the beat lifts the membrane (1/16 px) */
    uint32_t seed, z;                           /* its noise, and where in time the noise is */
} membrane_t;

/* the membrane's outline into x[], y[] (1/16 px) and each point's light 0..256 */
static void membrane(const membrane_t *m, int32_t *x, int32_t *y, uint16_t *lt)
{
    uint32_t k, n = m->lobes_q8 >> 8, f = m->lobes_q8 & 255u, step = 0xFFFFFFFFu / ONP;
    for (k = 0; k < ONP; k++) {
        uint32_t th = k * step;
        int32_t w1 = (psine(n * th + m->ph1) * (int32_t)(256u - f) + psine((n + 1u) * th + m->ph1) * (int32_t)f) >> 8;
        int32_t aw = w1 < 0 ? -w1 : w1, w2 = psine((2u * n + 3u) * th - m->ph2), env, w, r, d, bump, s, c;
        w1 = w1 + (((((w1 * aw) >> 15) * aw) >> 15) - w1) * m->res / 127;   /* resonance: sharper peaks */
        env = 19661 + (psine(th + m->ph3) * 13107 >> 15);                     /* ripples bigger on one side */
        w = ((w1 * env) >> 15) + (w2 >> 2);
        d = (int32_t)(th - m->hand);                                          /* the beat swell travelling round */
        d = d < 0 ? -d : d;
        bump = d < (1 << 29) ? (int32_t)(((uint32_t)((1 << 29) - d) >> 14) * ((uint32_t)((1 << 29) - d) >> 14) >> 14) : 0;   /* 0..32768 */
        r = m->r0 * 16 + ((m->amp * w) >> 15) + ((m->amp * (psine(2u * th + m->ph3 * 2u) >> 2)) >> 15)
            + ((m->swell * bump) >> 15) + ((vnoise(th, m->z, m->seed) * (m->amp + 24)) >> 15);   /* the noise: no two alike */
        s = psine(th) >> 3; c = psine(th + 0x40000000u) >> 3;
        x[k] = m->cx + ((r * s) >> 12);
        y[k] = m->cy - ((r * c) >> 12);
        lt[k] = (uint16_t)clampi(110 + (aw >> 8) + (bump >> 8) + (vnoise(th + 0x80000000u, m->z * 2u, m->seed + 77u) >> 9), 0, 256);
    }
}

static void membrane_draw(const int32_t *x, const int32_t *y, const uint16_t *lt, uint32_t c, uint32_t a, uint32_t fill)
{
    uint32_t i;
    if (fill) r_shape(x, y, ONP, c, fill);
    for (i = 0; i < ONP; i++) {
        uint32_t j = i + 1u == ONP ? 0u : i + 1u, l = (uint32_t)(lt[i] + lt[j]) / 2u, cc = l > 230u ? mix565(c, K_ACC, (l - 230u) * 8u) : c;
        if (l < 140u) r_line16(x[i], y[i], x[j], y[j], cc, a * l >> 8);          /* thin where it is quiet */
        else {
            r_glow16(x[i], y[i], x[j], y[j], cc, a * l >> 8);
            if (l > 200u) {                     /* and thick where it swells */
                r_line16(x[i] + 12, y[i] + 12, x[j] + 12, y[j] + 12, cc, a * (l - 150u) >> 8);
                r_line16(x[i] - 12, y[i] - 12, x[j] - 12, y[j] - 12, cc, a * (l - 150u) >> 8);
            }
        }
    }
}

/* the colour of a membrane by its level, drifting teal inside to lime outside */
static uint32_t heat(int32_t lvl, uint32_t t)
{
    static const uint16_t TINT[NTR] = {C565(0, 200, 150), C565(0, 210, 120), C565(0, 225, 95), C565(0, 240, 70), C565(70, 245, 60), C565(140, 250, 70)};
    uint32_t base = TINT[t];
    if (lvl < 8000) return mix565(K_N2, base, 110u + (uint32_t)lvl * 146u / 8000u);
    return mix565(base, K_ACC, (uint32_t)clampi((lvl - 8000) / 90, 0, 190));
}

static void draw_home(void)
{
    static const uint8_t RAD[NTR] = {24, 36, 48, 60, 72, 84};
    static uint32_t last_ms, last_kick, last_note;
    uint32_t pos = eng_pos, t, k, dt_ms, hand, step16;
    int32_t frac = eng_play ? (int32_t)((eng_acc >> 8) * 256u / (eng_spb_q16 >> 8)) : 0, lift = eng_lift;
    uint32_t kick_dt = fm1_ms - eng_hit_ms[T_KICK];
    int32_t pulse = eng_play && kick_dt < 160u && !song.tr[T_KICK].mute ? (int32_t)(160u - kick_dt) : 0;
    int32_t drop = ui.drop_ms && fm1_ms - ui.drop_ms < 600u ? 600 - (int32_t)(fm1_ms - ui.drop_ms) : 0;
    int32_t mx[ONP], my[ONP];
    uint16_t ml[ONP];
    if (ui.calm) frac = 0;
    dt_ms = fm1_ms - last_ms; last_ms = fm1_ms;
    if (dt_ms > 100u) dt_ms = 100u;
    cell_t += dt_ms * (eng_play ? 60000u + (uint32_t)lift * 6000u : 15000u);
    step16 = pos & 15u;
    hand = (step16 << 28) + ((uint32_t)frac << 20);

    if (home_drawn && !ui.calm) r_fade(SC_TOP, SC_BOT, eng_play ? 160u : 120u);   /* the afterglow */
    else r_fill(0, SC_TOP, W, SC_BOT - SC_TOP + 1, K_BG);

    /* spores: the kick breathes them out of the nucleus; the drop bursts them */
    if (eng_play && eng_hit_ms[T_KICK] != last_kick && !song.tr[T_KICK].mute) {
        last_kick = eng_hit_ms[T_KICK];
        for (k = 0; k < 6u; k++) spore_add(OX * 16, OY * 16, 48u + (uint32_t)lift / 2u, K_N5);
    }
    if (drop > 560) for (k = 0; k < 6u; k++) spore_add(OX * 16, OY * 16, 160u, K_ACC);
    spores(dt_ms);

    /* the membranes */
    for (t = 0; t < NTR; t++) {
        const track_t *tr = &song.tr[t];
        membrane_t m;
        uint32_t sel = t == ui.track, c, len = tr->len ? tr->len : 16u;
        int32_t cut, res, lvl = eng_play ? eng_meter[t] : 0;
        if (t == T_KICK) { cut = tr->par[PK_TONE]; res = tr->par[PK_DRIVE] / 2; }
        else if (t == T_909) { cut = tr->par[PD_HATTONE]; res = tr->par[PD_SNRSNAP] / 2; }
        else {
            cut = clampi(tr->par[P_CUT] + ((int32_t)song.g[G_MFILT] - 64) * 3 / 2 + (t == T_ROLL ? eng_build / 2 : lift / 4), 0, 127);
            res = clampi(tr->par[P_RES] + (int32_t)song.g[G_MRES] - 64, 0, 127);
        }
        m.cx = OX * 16 + (psine(cell_t / 7u + t * 0x2A000000u) >> 10);   /* each centre drifts, ±2 px */
        m.cy = OY * 16 + (psine(cell_t / 9u + t * 0x47000000u) >> 10);
        m.r0 = RAD[t] + pulse / 45 + drop / 20 + (psine(cell_t / 5u + t * 0x55555555u) >> 13);   /* the spacing lives */
        m.seed = t * 0x9E3779B9u; m.z = cell_t / 3u + t * (1u << 24);
        m.lobes_q8 = 512u + (uint32_t)cut * 3200u / 127u;                  /* 2 .. 14.5 ripples, smoothly */
        m.amp = clampi(26 + lvl / 300 + lift / 3, 0, 104);
        m.res = res;
        m.ph1 = cell_t * (t & 1u ? 1u : 0xFFFFFFFFu) * (2u + t);
        m.ph2 = cell_t * (3u + t * 2u);
        m.ph3 = cell_t / 2u + t * 0x30000000u;
        m.hand = hand;
        m.swell = eng_play && !ui.calm ? 40 + lvl / 600 : 0;
        membrane(&m, mx, my, ml);
        c = tr->mute ? K_N1 : sel ? mix565(heat(lvl, t), K_ACC, 70) : heat(lvl, t);
        membrane_draw(mx, my, ml, c, tr->mute ? 120u : 256u, sel && !tr->mute ? 30u : t == NTR - 1u || t == 2u ? 12u : 0u);   /* cytoplasm: a soft depth */
        if (t == NTR - 1u && !tr->mute) {       /* cilia on the outer membrane, waving */
            uint32_t i;
            for (i = 0; i < ONP; i += 2u) {
                int32_t dx = mx[i] - m.cx, dy = my[i] - m.cy, wv = psine(i * 0x0AAAAAAAu - cell_t * 4u);
                int32_t len = 30 + lvl / 500 + ((wv * (14 + lift / 6)) >> 15), sway = psine(i * 0x05555555u + cell_t * 2u) >> 12;
                int32_t r = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy), ux, uy;
                if (len < 6 || !r) continue;
                ux = dx * len / r; uy = dy * len / r;    /* outward, then a little sideways */
                r_line16(mx[i], my[i], mx[i] + ux - (uy * sway >> 4), my[i] + uy + (ux * sway >> 4), c, 120u + (uint32_t)(wv >> 9) / 2u);
            }
        }

        /* the notes ride the membrane; the one playing blooms */
        for (k = 0; k < 16u; k++) {
            uint32_t s = ((pos / 16u) * 16u + k) % len, i = k * ONP / 16u;
            const step_t *st = &tr->st[s];
            if (!(st->fl & ST_ON) || tr->mute) continue;
            if (eng_play && k == step16) {
                uint32_t age = fm1_ms - eng_hit_ms[t];
                int32_t rr = 24 + (int32_t)(age < 400u ? age : 400u) / 6;
                r_disc(mx[i], my[i], rr, sel ? K_ACC : c, age < 400u ? 120u - age * 110u / 400u : 10u);   /* the bloom */
                r_disc(mx[i], my[i], 30, sel ? K_ACC : K_N5, 256);
                if (sel && eng_hit_ms[t] != last_note) { last_note = eng_hit_ms[t]; spore_add(mx[i], my[i], 24u, K_N5); }
            } else
                r_disc(mx[i], my[i], 18, eng_play && k < step16 ? K_N2 : sel ? K_N5 : K_N4, 220);
        }
    }

    /* organelles: small bodies drifting between the membranes */
    for (k = 0; k < 5u; k++) {
        uint32_t a = cell_t / (11u + k * 3u) + k * 0x33333333u;
        int32_t rr = (30 + (int32_t)k * 11 + (psine(cell_t / 13u + k * (1u << 25)) >> 12)) * 16, x, y;
        x = OX * 16 + ((rr * (psine(a) >> 3)) >> 12); y = OY * 16 - ((rr * (psine(a + 0x40000000u) >> 3)) >> 12);
        r_disc(x, y, 34 + (int32_t)k * 6, K_N2, 120);
        r_disc(x, y, 14 + (int32_t)k * 3, k & 1u ? K_N4 : C565(0, 210, 160), 170);
    }

    /* the nucleus: soft layers, brighter inward; LIFT feeds it, the drop bursts it */
    {
        membrane_t m;
        uint32_t layer;
        m.cx = OX * 16; m.cy = OY * 16;
        m.lobes_q8 = 768u + (uint32_t)lift * 22u;
        m.res = 40; m.hand = hand; m.swell = 0; m.seed = 0xA11CE5u; m.z = cell_t / 2u;
        m.ph1 = cell_t * 3u; m.ph2 = cell_t * 5u; m.ph3 = cell_t;
        for (layer = 0; layer < 3u; layer++) {
            m.r0 = (13 + lift / 16) * (int32_t)(3u - layer) / 3 - drop / 80;
            if (m.r0 < 2) m.r0 = 2;
            m.amp = 20 + lift / 3;
            membrane(&m, mx, my, ml);
            r_shape(mx, my, ONP, lift > 96 ? K_ACC : mix565(K_N4, K_ACC, layer * 80u), 50u + layer * 60u + (uint32_t)lift / 2u);
        }
    }

    {                                           /* corners: LIFT and BUILD; the chord; the macro being turned */
        char b[12];
        int32_t x;
        x = r_text_sh(&F12, 4, 22, "LIFT", K_N3);
        r_int(b, lift, 0);
        r_text_sh(&F12, x + 6, 22, b, ui.lift_ms && fm1_ms - ui.lift_ms < 1500u ? K_ACC : lift ? K_N5 : K_N3);
        x = r_text_sh(&F12, 4, 36, "BUILD", K_N3);
        r_int(b, eng_build * 100 / 127, 0);
        r_text_sh(&F12, x + 6, 36, b, eng_build ? K_N5 : K_N3);
        chord_name(chord_at(pos % NSTEP), b);
        r_text_sh(&F16, 236 - r_textw(&F16, b), 22, b, K_N5);
        r_text_sh(&F12, 236 - r_textw(&F12, TR_NAME[ui.track]), 40, TR_NAME[ui.track], K_N4);
        if (ui.touched >= 0 && ui.page == PG_PERFORM && fm1_ms - ui.touched_ms < 1200u) {
            static const uint8_t G[4] = {G_MFILT, G_MRES, G_MSPACE, G_MPUMP};
            int32_t v = song.g[G[ui.touched]];
            r_text_sh(&F12, 4, 160, MACRO_NAME[ui.touched], K_ACC);
            r_int(b, ui.touched == 2 ? v : v - 64, 0);
            r_text_sh(&F16, 4, 174, b, K_ACC);
        }
    }
    if (drop) r_text_sh(&F28, OX - 28, OY - 14, "DROP", K_ACC);
    home_drawn = 1;
}

/* ---- STEPS */
static int32_t step_x(uint32_t s) { return 32 + (int32_t)s * 12 + (int32_t)(s >> 2) * 4; }

static void draw_seq(void)
{
    uint32_t t, s, sel = ui.track, page0 = ui.barpage * 16u;
    const track_t *tr = &song.tr[sel];
    uint32_t len = tr->len ? tr->len : 16u, play = eng_pos % len;
    char b[8];
    /* the bar pages, top right of the overview */
    for (s = 0; s < NBAR; s++) {
        b[0] = (char)('1' + s); b[1] = 0;
        if (s == ui.barpage) r_fill(140 + (int32_t)s * 12, 22, 9, 13, K_ACC);
        r_text(&F12, 141 + (int32_t)s * 12, 22, b, s == ui.barpage ? K_ACC_FG : s * 16u < len ? K_N4 : K_N2);
    }
    r_text(&F12, 4, 22, "STEPS", K_N4);
    /* all six tracks, this bar page */
    for (t = 0; t < NTR; t++) {
        const track_t *r = &song.tr[t];
        uint32_t ln = r->len ? r->len : 16u;
        int32_t y = 36 + (int32_t)t * 10;
        r_text(&F12, 4, y - 2, TR_SHORT[t], r->mute ? K_N2 : t == sel ? K_N5 : K_N3);
        for (s = 0; s < 16u; s++) {
            uint32_t gs = page0 + s, on = gs < ln && (r->st[gs].fl & ST_ON), now = eng_play && (eng_pos % ln) == gs;
            int32_t x = step_x(s);
            if (gs >= ln) continue;
            if (on) r_fill(x, y + 1, 9, 6, now ? K_ACC : r->mute ? K_N2 : dim565(t == sel ? K_N5 : K_N4, 90u + r->st[gs].vel));
            else r_fill(x + 4, y + 4, 1, 1, now ? K_ACC : K_N2);
        }
    }
    r_hline(0, W - 1, 98, K_N1);
    /* the selected track in detail */
    if (ui.held >= 0) r_vline(step_x((uint32_t)ui.held) + 4, 100, 186, K_N2);
    if (sel == T_909) {
        uint32_t k;
        for (k = 0; k < NKIT; k++) {
            int32_t y = 104 + (int32_t)k * 13;
            r_text(&F12, 4, y - 2, KIT_NAME[k], k == ui.kitv ? K_N5 : K_N3);
            if (k == ui.kitv) r_fill(0, y - 1, 2, 9, K_ACC);
            for (s = 0; s < 16u; s++) {
                uint32_t gs = page0 + s, on = gs < len && (tr->st[gs].fl & ST_ON) && ((uint8_t)tr->st[gs].note >> k & 1u);
                int32_t x = step_x(s);
                if (gs >= len) continue;
                if (on) r_fill(x, y, 9, 7, eng_play && play == gs ? K_ACC : k == ui.kitv ? K_N5 : K_N4);
                else r_hline(x + 2, x + 6, y + 3, K_N1);
            }
        }
    } else if (sel == T_PAD) {                  /* the pad: the chords it will voice, Rollwerk's tiles */
        for (s = 0; s < 16u; s += 8u) {
            char c[12];
            uint32_t gs = page0 + s;
            int32_t x = step_x(s);
            chord_name(chord_at(gs % NSTEP), c);
            r_tile(x, 112, 8 * 12 + 4, 40, (tr->st[gs].fl & ST_ON) ? K_KEY : K_PANEL,
                   eng_play && play >= gs && play < gs + 8u ? K_ACC : K_N2);
            r_text(&F16, x + 6, 124, c, (tr->st[gs].fl & ST_ON) ? K_N5 : K_N3);
        }
    } else if (sel != T_KICK) {                 /* Rollwerk's pattern: a bar per step, as tall as its pitch */
        int32_t lo = 127, hi = 0, span;
        for (s = 0; s < len; s++)
            if (tr->st[s].fl & ST_ON) { if (tr->st[s].note < lo) lo = tr->st[s].note; if (tr->st[s].note > hi) hi = tr->st[s].note; }
        if (lo > hi) { lo = 48; hi = 72; }
        span = hi - lo + 4;
        for (s = 0; s < 16u; s++) {
            uint32_t gs = page0 + s;
            const step_t *st = &tr->st[gs];
            int32_t x = step_x(s), h, w;
            if (gs >= len || !(st->fl & ST_ON)) { r_hline(x, x + 8, 168, K_N2); continue; }
            h = 6 + (st->note - lo + 2) * 58 / span;
            w = st->len > 1u ? 9 + 12 * (int32_t)(st->len - 1u) : 9;
            if (x + w > W - 2) w = W - 2 - x;
            r_fill(x, 168 - h, w, h, eng_play && play >= gs && play < gs + st->len ? K_ACC
                                   : (st->fl & ST_RATCHET) ? K_N5 : dim565(K_N4, 120u + st->vel));
            if (st->fl & ST_SLIDE) r_hline(x - 3, x, 168 - h, K_N4);
            if ((s & 3u) == 0u) { note_text(st->note + oct_shift(sel), b); r_text(&F12, x, 160 - h - 12 < 100 ? 100 : 156 - h, b, K_N3); }
        }
    } else {                                    /* the kick: velocity as height */
        r_hline(30, W - 1, 166, K_N1);
        for (s = 0; s < 16u; s++) {
            uint32_t gs = page0 + s;
            const step_t *st = &tr->st[gs];
            if (gs >= len || !(st->fl & ST_ON)) continue;
            r_fill(step_x(s), 166 - (int32_t)st->vel * 56 / 127, 9, (int32_t)st->vel * 56 / 127,
                   eng_play && play == gs ? K_ACC : K_N4);
        }
    }
    {   /* p-locks and recorded motion of the knob last turned (else knob 1 of the page) */
        uint32_t p = (ui.page == PG_PERFORM ? 2u : ui.page) * 4u + (uint32_t)(ui.touched >= 0 ? ui.touched : 0);
        const char *nm = par_name(sel, p);
        r_text(&F12, 4, 176, nm[0] ? nm : "-", K_N3);
        for (s = 0; s < 16u; s++) {
            uint32_t gs = page0 + s, lk = gs < len ? tr->lock[gs][p] : 0u;
            int32_t x = step_x(s) + 4;
            if (lk) r_vline(x, 190 - (int32_t)(lk - 1u) * 14 / 127, 190, K_N4);
        }
    }
}

/* ---- GLOBAL: the panels */
static void glo_title(void)
{
    char b[16];
    int32_t x = r_text(&F16, 4, 24, (key_flats() ? NOTE_FLAT : NOTE_NAME)[song.key], K_N5);
    x = r_text(&F16, x + 8, 24, song.mode ? "major" : "minor", K_N5);
    b[0] = 0;
    r_int(b, song.prog - MOOD_START[song.mood] + 1, 0);
    x = 236 - r_textw(&F12, b);
    r_text(&F12, x, 27, b, K_N5);
    r_text_r(&F12, x - 6, 27, MOOD_NAME[song.mood], K_N4);
}

/* the roller's notes of bar b as Rollwerk's pattern bars, in the box x, y, w, h */
static void pattern_bars(uint32_t b, int32_t x, int32_t y, int32_t w, int32_t h)
{
    const track_t *tr = &song.tr[T_ROLL];
    int32_t lo = 127, hi = 0, s, cw = w / 16;
    uint32_t play = eng_pos % NSTEP;
    for (s = 0; s < 16; s++) {
        const step_t *st = &tr->st[b * 16u + (uint32_t)s];
        if (st->fl & ST_ON) { if (st->note < lo) lo = st->note; if (st->note > hi) hi = st->note; }
    }
    if (lo > hi) return;
    for (s = 0; s < 16; s++) {
        const step_t *st = &tr->st[b * 16u + (uint32_t)s];
        int32_t bh, xx = x + s * cw;
        if (!(st->fl & ST_ON)) continue;
        bh = 4 + (st->note - lo) * (h - 6) / (hi - lo + 1);
        r_fill(xx, y + h - bh, cw - 2, bh, eng_play && play == b * 16u + (uint32_t)s ? K_ACC
                                         : (s & 3) == 0 ? K_N5 : K_N4);
        if ((s & 3) == 0) { char t[8]; note_text(st->note, t); r_text(&F12, xx, y + h + 3, t, K_N3); }
    }
}

static void draw_glo(void)
{
    char b[16];
    uint32_t i, bar = (eng_pos % NSTEP) / 16u;
    glo_title();
    if (ui.gpage == 0u) {                       /* the eight chords as Rollwerk's tiles */
        for (i = 0; i < NBAR; i++) {
            int32_t bx = 4 + (int32_t)(i % 4u) * 59, by = 46 + (int32_t)(i / 4u) * 46;
            uint32_t sel = i == ui.glo_bar;
            r_tile(bx, by, 55, 42, K_PANEL, sel ? K_ACC : K_N2);
            b[0] = (char)('1' + i); b[1] = 0;
            r_text(&F12, bx + 4, by + 3, b, K_N3);
            r_text_r(&F12, bx + 51, by + 3, chord_roman(song.deg[i]), K_N3);
            chord_name(chord_of(song.deg[i]), b);
            if (song.half[i] != 255u) {
                char c[12];
                uint32_t k = 0;
                chord_name(chord_of(song.half[i]), c);
                while (b[k]) k++;
                b[k++] = '|';
                for (uint32_t m = 0; c[m] && k < 15u; m++) b[k++] = c[m];
                b[k] = 0;
                r_text(&F12, bx + 4, by + 24, b, K_N5);
            } else
                r_text(&F16, bx + 4, by + 21, b, K_N5);
            if (eng_play && i == bar) r_fill(bx + 3, by + 38, 49, 2, K_ACC);
        }
        r_text(&F12, 4, 142, "SELECT  CHORD OF THE BAR", K_N3);
        r_text(&F12, 4, 156, "PRESETS SPLIT AT THE HALF", K_N3);
        r_text(&F12, 4, 170, "ARP     ANY PROGRESSION", K_N3);
        r_text(&F12, 4, 180, "KEYS 1-8 PICK A BAR", K_N2);
    } else if (ui.gpage == 1u) {                /* the roller: the selected bar's pattern, the register */
        b[0] = 'B'; b[1] = 'A'; b[2] = 'R'; b[3] = ' '; b[4] = (char)('1' + ui.glo_bar); b[5] = 0;
        r_text(&F12, 4, 46, "PATTERN", K_N4);
        r_text_r(&F12, 236, 46, b, K_N3);
        pattern_bars(ui.glo_bar, 4, 62, 232, 60);
        r_text(&F12, 4, 142, "REGISTER ACROSS THE PHRASE", K_N4);
        for (i = 0; i < NBAR; i++) {
            int32_t bx = 4 + (int32_t)i * 29, a = 64;
            if (song.reg != SHAPE_OFF) a = 64 + shape(song.reg, (int32_t)(i / 2u) * 256 / 3) * (12 + song.reg_amt * 16 / 127) / 256;
            r_tile(bx, 158, 26, 30, K_PANEL, i == ui.glo_bar ? K_ACC : K_N2);
            r_fill(bx + 3, 158 + 26 - (a - 58) * 22 / 34 - 6, 20, 6, K_N4);
        }
    } else if (ui.gpage == 2u) {                /* the build over eight bars, as Rollwerk sweeps it */
        int32_t px = -1, py = 0;
        r_text(&F12, 4, 46, "BUILD OVER 8 BARS", K_N4);
        for (i = 0; i < LIFT_STEPS; i++) {
            int32_t v = song.lift[i] ? song.lift[i] - 1 : 0, xx = 4 + (int32_t)i * 232 / (int32_t)LIFT_STEPS, yy = 130 - v * 64 / 127;
            r_vline(xx, yy, 130, (i / 16u) & 1u ? K_KEY : K_PANEL);
            if (px >= 0) r_line(px, py, xx, yy, K_ACC, 256);
            px = xx; py = yy;
        }
        r_hline(4, 236, 131, K_N2);
        if (eng_play) r_vline(4 + (int32_t)(eng_pos % LIFT_STEPS) * 232 / (int32_t)LIFT_STEPS, 60, 131, K_N5);
        r_text(&F12, 4, 142, "FILTER OPENS, RESONANCE RISES,", K_N3);
        r_text(&F12, 4, 156, "THE RISER CLIMBS. LIFT (ALGO)", K_N3);
        r_text(&F12, 4, 170, "PUSHES PAST IT, LIVE.", K_N3);
    } else if (ui.gpage == 3u) {                /* the groove: the bass of bar 1, the pump */
        const track_t *bt = &song.tr[T_BASS];
        r_text(&F12, 4, 46, "BASS", K_N4);
        r_text_r(&F12, 236, 46, BASS_NAME[song.bass], K_N3);
        for (i = 0; i < 16u; i++) {
            const step_t *st = &bt->st[i];
            int32_t xx = 4 + (int32_t)i * 14 + (int32_t)(i / 4u) * 3;
            if (st->fl & ST_ON) r_fill(xx, 62, (int32_t)(st->len > 1u ? st->len * 14u - 3u : 11u) > 220 ? 220 : (int32_t)(st->len > 1u ? st->len * 14u - 3u : 11u), 16, eng_play && (eng_pos & 15u) == i ? K_ACC : K_N4);
            else r_hline(xx, xx + 10, 70, K_N2);
        }
        r_text(&F12, 4, 96, "PUMP", K_N4);
        if (song.pump) {                        /* the kick's duck: down to .22, back over a beat */
            int32_t k, px = 4, py = 108;
            for (k = 0; k < 4; k++) {
                int32_t x0 = 4 + k * 58, xx;
                for (xx = 0; xx <= 56; xx += 4) {
                    int32_t g = 100 - 78 * (56 - xx) * (56 - xx) / (56 * 56), yy = 140 - g * 30 / 100;
                    r_line(px, py, x0 + xx, yy, K_ACC, 256);
                    px = x0 + xx; py = yy;
                }
            }
        } else r_hline(4, 236, 110, K_N4);
        r_hline(4, 236, 141, K_N2);
        r_text(&F12, 4, 156, "PUMP FOLLOWS THE KICK; EACH", K_N3);
        r_text(&F12, 4, 170, "TRACK'S PUMP KNOB IS ITS DEPTH", K_N3);
    } else if (ui.gpage == 5u) {                /* system: the tempo, the slots */
        int32_t x;
        r_int(b, song.bpm10 / 10, 0);
        x = r_text(&F28, 4, 46, b, K_N5);
        r_text(&F12, x + 6, 60, "BPM", K_N3);
        r_text(&F12, 4, 92, "SONG SLOTS", K_N4);
        for (i = 0; i < NSLOT; i++) {
            int32_t bx = 4 + (int32_t)i * 39;
            uint32_t used = slot_used(i);
            b[0] = (char)('1' + i); b[1] = 0;
            r_tile(bx, 108, 35, 30, used ? K_KEY : K_PANEL, i == ui.slot ? K_ACC : K_N2);
            r_text(&F16, bx + 13, 115, b, used ? K_N5 : K_N3);
        }
        r_text(&F12, 4, 152, "SAVE: KEEP   SCL+SAVE: LOAD", K_N3);
        r_text(&F12, 4, 166, "SCL+ARP: A NEW SONG", K_N3);
        r_text(&F12, 4, 180, "SCL+PRESETS: SOUND PRESETS", K_N3);
    } else {                                    /* echo and hall: the echoes falling, the hall's decay */
        static const uint8_t DIV[5] = {2, 3, 4, 6, 8};
        int32_t d = DIV[song.g[G_DTIME] * 5u / 128u], a = 220, k, px = 4, py = 140, g = 21000 + song.g[G_VSIZE] * 70, v = 32767, xx;
        r_text(&F12, 4, 46, "ECHOES", K_N4);
        for (k = 1; k < 12; k++) {
            xx = 4 + k * d * 4;
            if (xx > 236) break;
            r_vline(xx, 110 - a * 44 / 256, 110, k & 1 ? K_N5 : K_N4);
            a = a * (song.g[G_DFB] * 200) / 32768;
            if (a < 8) break;
        }
        r_hline(4, 236, 111, K_N2);
        r_text(&F12, 4, 122, "HALL", K_N4);
        for (xx = 8; xx <= 236; xx += 4) {
            int32_t yy;
            v = (int32_t)(((int64_t)v * (g + (32767 - g) * 3 / 4)) >> 15);
            yy = 184 - v * 44 / 32767;
            r_line(px, py, xx, yy, K_N5, 256);
            px = xx; py = yy;
        }
        r_hline(4, 236, 185, K_N2);
    }
}

/* ---- ABOUT and the boot splash: the dog */
static void draw_about(void)
{
    r_image(8, 18, LOGO_L, LOGO_L_W, LOGO_L_H);
    r_text(&F28, 128, 34, "Finton", K_N5);
    r_text(&F12, 128, 68, "8 bars, 16ths", K_N4);
    r_text(&F12, 128, 82, "v" APP_VER, K_N3);
    r_text(&F16, 8, 138, "Made by FintonLabs", K_N5);
    r_fill(8, 156, 144, 2, K_ACC);
    r_text(&F12, 8, 164, "fintonlabs.com", K_N4);
    r_text(&F12, 8, 184, "AFTER ROLLWERK. GPL-3.0", K_N3);
    r_text(&F12, 8, 198, "DINGHY / FELUCCA / SLOOP", K_N2);
    r_text(&F12, 8, 210, "TYPE M PLUS 1 CODE (SIL OFL)", K_N2);
    r_text(&F12, 8, 226, "ANY BUTTON: BACK", K_N3);
}

/* the splash: the code falls, the dog wakes, a line types itself out */
static void draw_splash(void)
{
    static const char LINE[] = "wake up, finton...";
    char b[sizeof LINE];
    uint32_t n = fm1_ms / 60u, i;
    for (i = 0; i < 30u; i++) {                 /* a curtain of code */
        uint32_t k, h = hash32(i * 7919u), y0 = (fm1_ms / (4u + h % 5u) + h) % 300u;
        for (k = 0; k < 8u; k++)
            r_glyph(hash32(h + k + fm1_ms / 200u), (int32_t)i * 8, (int32_t)y0 - 60 - (int32_t)k * 12, k ? K_N2 : K_N4, 220u - k * 25u);
    }
    r_image((W - LOGO_L_W) / 2, 30, LOGO_L, LOGO_L_W, LOGO_L_H);
    r_text(&F28, (W - 6 * 14) / 2, 30 + LOGO_L_H + 6, "Finton", K_N5);
    if (n > sizeof LINE - 1u) n = sizeof LINE - 1u;
    for (i = 0; i < n; i++) b[i] = LINE[i];
    b[n] = 0;
    i = (uint32_t)r_text(&F12, 48, 30 + LOGO_L_H + 44, b, K_N4);
    if ((fm1_ms / 250u) & 1u) r_fill((int32_t)i + 1, 30 + LOGO_L_H + 45, 5, 10, K_N5);   /* the cursor */
}

static void draw_frame(void)
{
    lcd_sync();                                 /* the last frame's DMA has read scr */
    if (ui.view == V_HOME && home_drawn) {      /* HOME keeps its scene for the afterglow */
        r_fill(0, 0, W, SC_TOP, K_BG);
        r_fill(0, SC_BOT + 1, W, H - SC_BOT - 1, K_BG);
    } else {
        r_clear(K_BG);
        home_drawn = 0;
    }
    if (ui.view == V_ABOUT) { draw_about(); r_flush(); return; }
    if (ui.view == V_SPLASH) { draw_splash(); r_flush(); return; }
    draw_header();
    if (ui.view == V_HOME) draw_home();
    else if (ui.view == V_SEQ) draw_seq();
    else draw_glo();
    draw_strip();
    {                                           /* frames a second, counted */
        static uint32_t t0, n, fps;
        n++;
        if (fm1_ms - t0 >= 1000u) { fps = n * 1000u / (fm1_ms - t0); n = 0; t0 = fm1_ms; }
        if (fm1_in.buttons >> BTN_SEL & 1u) {   /* SCL held: the cost of it all */
            char b[8];
            int32_t x;
            r_fill(120, 21, 120, 12, K_BG);
            r_int(b, (int32_t)(eng_load_q8 * 100u / 256u), 0);
            x = r_text(&F12, 124, 21, b, K_N4);
            x = r_text(&F12, x, 21, "% CPU ", K_N3);
            r_int(b, (int32_t)fps, 0);
            x = r_text(&F12, x, 21, b, K_N4);
            r_text(&F12, x, 21, " FPS", K_N3);
        }
    }
    r_flush();
}
