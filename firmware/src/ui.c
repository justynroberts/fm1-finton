/* SPDX-License-Identifier: GPL-3.0-only */
/* Finton controls and the main loop.
 *
 *   PLAY            start / stop (SCL+PLAY: from the top)
 *   REC             arm: knob moves become motion, keys become steps, the LIFT knob writes the BUILD lane
 *   HOME            the horizon (hold: about)        SEQ   steps        GLO   global (again: next page)
 *   EDIT ENV LFO FX the track's knob page: SOUND SHAPE FILTER SEND; HOME brings back PERFORM:
 *                   FILTER RES SPACE PUMP, macros over the whole mix (SCL+turn: back to neutral)
 *   ARP             generate this track again (ROLL: Rollwerk's reroll; GLOBAL: any progression);
 *                   SCL+ARP: a whole new song
 *   SAVE            save the song (stopped); SCL+SAVE: load the slot
 *   SCL             shift; held: CPU readout. SCL + black track key: mute
 *   SELECT          track (step held: its note; GLOBAL: the selected bar's chord)
 *   ALGORITHM       LIFT, the performance build: filters open, a riser climbs, the snare rolls, the kick
 *                   and bass drop out; back to 0 from the top and the drop lands
 *   PRESETS         CUTOFF of the selected track (drums selected: ROLL), recordable as motion;
 *                   SCL+PRESETS: the track's sound presets (step held: its length;
 *                   GLOBAL HARMONY: the bar's second-half chord). Tempo: GLOBAL SYSTEM
 *   KNOB1-4         the page's parameters (step held: a p-lock; SCL+turn: clear that motion)
 *   keys            HOME: play the track (909: the five voices)
 *                   SEQ: white = the 16 steps of the bar (tap toggles, hold edits), black = tracks 1-6,
 *                   then on 909 the five voices; OCT-/+ the bar (1-8)
 *                   GLOBAL: white 1-8 pick a bar */

#define KEY0 53u                                /* key 0 = F3 */
static int key_black(uint32_t k) { return (0x54Au >> ((KEY0 + k) % 12u)) & 1u; }
static int8_t white_idx[27], black_idx[27];     /* key -> white / black index, -1 */
static int8_t white_key[16], black_key[11];     /* index -> key */

static void keymap_init(void)
{
    uint32_t k, w = 0, b = 0;
    for (k = 0; k < 27u; k++) {
        white_idx[k] = black_idx[k] = -1;
        if (key_black(k)) { black_idx[k] = (int8_t)b; black_key[b++] = (int8_t)k; }
        else { white_idx[k] = (int8_t)w; white_key[w++] = (int8_t)k; }
    }
}

static uint32_t held_keys_note[27];             /* the note each key started (live play) */
static const uint8_t TR_LIVE_C[NTR] = {0, 0, 36, 60, 72, 60};   /* the C each track's keys start from */
static uint32_t prev_keys;

/* the track the CUTOFF knob moves: the selected one if it has a filter, else ROLL */
static uint32_t cut_track(void) { return ui.track >= T_BASS ? ui.track : T_ROLL; }

static int32_t accel(int32_t d) { int32_t a = d < 0 ? -d : d; return a >= 3 ? d * 3 : d; }

static void track_select(uint32_t t)
{
    ui.track = (uint8_t)(t % NTR);
    ui.held = -1;
}

/* the step the playhead is on, in track t */
static uint32_t play_step(uint32_t t) { uint32_t len = song.tr[t].len ? song.tr[t].len : 16u; return eng_pos % len; }

/* the GLOBAL knobs: Rollwerk's controls; anything harmonic derives the following tracks again */
static int vocab_find(const char *id)
{
    uint32_t i;
    for (i = 0; i < nvocab(); i++) {
        const char *a = vocab()[i].id, *b = id;
        while (*a && *a == *b) { a++; b++; }
        if (!*a && !*b) return (int)i;
    }
    return -1;
}
static uint8_t mode_map(uint8_t v, uint32_t from_major)   /* Rollwerk's W(): the same chord in the other mode */
{
    static const char *const SWAP[8][2] = {{"i", "I"}, {"I", "i"}, {"VI", "vi"}, {"vi", "VI"}, {"III", "iii"}, {"iii", "III"}, {"VII", "bVII"}, {"bVII", "VII"}};
    const char *id = (from_major ? VOCAB_MAJOR : VOCAB_MINOR)[v].id;
    int r = vocab_find(id), k;
    for (k = 0; r < 0 && k < 8; k++) {
        const char *a = SWAP[k][0], *b = id;
        while (*a && *a == *b) { a++; b++; }
        if (!*a && !*b) r = vocab_find(SWAP[k][1]);
    }
    return (uint8_t)(r < 0 ? 0 : r);
}

static uint32_t step_of(int32_t v, int32_t d, int32_t n) { return (uint32_t)clampi(v + d, 0, n - 1); }

static void glo_turn(uint32_t pg, uint32_t k, int32_t d)
{
    uint32_t b, harm = 1;
    switch (pg * 4u + k) {
    case 0: song.key = (uint8_t)((song.key + 12 + d % 12) % 12); break;
    case 1:
        if ((uint32_t)clampi(song.mode + d, 0, 1) != song.mode) {
            uint32_t from = song.mode;
            song.mode ^= 1u;
            for (b = 0; b < NBAR; b++) {
                song.deg[b] = mode_map(song.deg[b], from);
                if (song.half[b] != 255u) song.half[b] = mode_map(song.half[b], from);
            }
        }
        break;
    case 2: song.mood = (uint8_t)step_of(song.mood, d, NMOOD); prog_load(MOOD_START[song.mood]); break;
    case 3: prog_load(MOOD_START[song.mood] + step_of(song.prog - MOOD_START[song.mood], d, MOOD_START[song.mood + 1u] - MOOD_START[song.mood])); break;
    case 4: song.figure = (uint8_t)step_of(song.figure, d, NFIG); break;
    case 5: song.top = (uint8_t)step_of(song.top, d, NTOP); break;
    case 6: song.sus_turn = d > 0; break;
    case 7: voice_preset(d > 0); harm = 0; break;
    case 8: song.build = (uint8_t)step_of(song.build, d, NSHAPE); gen_build(); harm = 0; break;
    case 9: song.build_amt = (uint8_t)clampi(song.build_amt + accel(d), 0, 127); gen_build(); harm = 0; break;
    case 10: song.reg = (uint8_t)step_of(song.reg, d, NSHAPE); break;
    case 11: song.reg_amt = (uint8_t)clampi(song.reg_amt + accel(d), 0, 127); break;
    case 12: song.bass = (uint8_t)step_of(song.bass, d, NBASS); song.tr[T_BASS].edited = 0; break;
    case 13: song.pump = d > 0; harm = 0; break;
    case 14: song.g[G_DRIVE] = (uint8_t)clampi(song.g[G_DRIVE] + accel(d), 0, 127); harm = 0; break;
    case 15: song.g[G_VSIZE] = (uint8_t)clampi(song.g[G_VSIZE] + accel(d), 0, 127); harm = 0; break;
    case 16: song.g[G_DTIME] = (uint8_t)clampi(song.g[G_DTIME] + accel(d), 0, 127); harm = 0; break;
    case 17: song.g[G_DFB] = (uint8_t)clampi(song.g[G_DFB] + accel(d), 0, 127); harm = 0; break;
    case 18: song.g[G_DTONE] = (uint8_t)clampi(song.g[G_DTONE] + accel(d), 0, 127); harm = 0; break;
    case 19: song.g[G_VDAMP] = (uint8_t)clampi(song.g[G_VDAMP] + accel(d), 0, 127); harm = 0; break;
    case 20: song.bpm10 = (uint16_t)clampi(song.bpm10 + d * 10, 600, 2000); eng_tempo(song.bpm10); harm = 0; break;
    case 21: ui.slot = (uint8_t)step_of(ui.slot, d, NSLOT); harm = 0; break;
    case 22: ui.calm = d < 0; song.g[G_MOTION] = ui.calm ? 0 : 127; harm = 0; break;
    default: harm = 0;
    }
    if (harm) harmony_changed();
}

static void knob(uint32_t k, int32_t d)
{
    uint32_t t = ui.track, p = ui.page * 4u + k, shift = fm1_in.buttons >> BTN_SEL & 1u;
    ui.touched = (int8_t)k;
    ui.touched_ms = fm1_ms;
    if (ui.view == V_GLO) { glo_turn(ui.gpage, k, d); return; }
    if (ui.page == PG_PERFORM) {                /* the macros, over the whole mix */
        static const uint8_t G[4] = {G_MFILT, G_MRES, G_MSPACE, G_MPUMP};
        if (fm1_in.buttons >> BTN_SEL & 1u) song.g[G[k]] = k == 2u ? 0u : 64u;   /* SCL+turn: neutral */
        else song.g[G[k]] = (uint8_t)clampi(song.g[G[k]] + accel(d) * 2, 0, 127);
        return;
    }
    if (!par_name(t, p)[0]) return;
    if (shift) {                                /* SCL + turn: clear this parameter's motion */
        uint32_t s;
        for (s = 0; s < NSTEP; s++) song.tr[t].lock[s][p] = 0;
        ui_say("MOTION CLEARED");
        return;
    }
    if (ui.view == V_SEQ && ui.held >= 0) {     /* a p-lock on the held step */
        uint32_t s = (ui.barpage * 16u + (uint32_t)ui.held) % NSTEP, lk = song.tr[t].lock[s][p];
        int32_t v = clampi((lk ? (int32_t)lk - 1 : song.tr[t].par[p]) + accel(d), 0, 127);
        song.tr[t].lock[s][p] = (uint8_t)(v + 1);
        ui.held_edit = 1;
        return;
    }
    song.tr[t].par[p] = (uint8_t)clampi(song.tr[t].par[p] + accel(d), 0, 127);
    if (ui.rec && eng_play)                     /* motion: the step playing takes the value */
        song.tr[t].lock[play_step(t)][p] = (uint8_t)(song.tr[t].par[p] + 1u);
}

/* while recording, a knob touched in the last 400 ms keeps writing as the steps pass */
static void motion_tick(void)
{
    static uint32_t last_pos = 0xFFFFFFFFu;
    uint32_t t = ui.track, p;
    if (!ui.rec || !eng_play || ui.view == V_GLO || ui.touched < 0) return;
    if (eng_pos == last_pos) return;
    last_pos = eng_pos;
    if (fm1_ms - ui.touched_ms < 400u && ui.page != PG_PERFORM) {
        p = ui.page * 4u + (uint32_t)ui.touched;
        if (par_name(t, p)[0]) song.tr[t].lock[play_step(t)][p] = (uint8_t)(song.tr[t].par[p] + 1u);
    }
    if (ui.cut_ms && fm1_ms - ui.cut_ms < 400u) {             /* the CUTOFF knob, likewise */
        uint32_t ct = cut_track();
        song.tr[ct].lock[play_step(ct)][P_CUT] = (uint8_t)(song.tr[ct].par[P_CUT] + 1u);
    }
    if (ui.lift_ms && fm1_ms - ui.lift_ms < 400u) {           /* the LIFT lane, likewise */
        song.lift[eng_pos % LIFT_STEPS] = (uint8_t)(ui.lift + 1);
    }
}

static void lift_turn(int32_t d)
{
    int32_t prev = ui.lift;
    ui.lift = clampi(ui.lift + d * 4, 0, 127);
    ui.lift_ms = fm1_ms;
    eng_lift_knob = ui.lift;
    if (ui.lift > ui.lift_peak) ui.lift_peak = ui.lift;
    if (prev > 0 && ui.lift <= 8 && ui.lift_peak >= 96) {       /* back down from the top: the drop */
        ui.drop_ms = fm1_ms;
        ui.lift = 0;
        eng_lift_knob = 0;
        ui.lift_peak = 0;
    }
}

static void step_key(uint32_t w, int down)
{
    uint32_t t = ui.track, s = (ui.barpage * 16u + w) % NSTEP;
    step_t *st = &song.tr[t].st[s];
    if (down) { ui.held = (int8_t)w; ui.held_edit = 0; return; }
    if (ui.held != (int8_t)w) return;
    song.tr[t].edited = 1;
    if (!ui.held_edit) {                        /* a tap: toggle */
        if (t == T_909) {
            uint8_t m = (uint8_t)st->note ^ (uint8_t)(1u << ui.kitv);
            st->note = (int8_t)m;
            st->fl = m ? (uint8_t)(st->fl | ST_ON) : 0u;
            if (!st->vel) st->vel = 100;
        } else if (st->fl & ST_ON) st->fl = 0;
        else {
            st->fl = ST_ON;
            if (!st->vel) st->vel = 100;
            if (!st->len) st->len = t == T_PAD ? 16u : 1u;
            if (t >= T_BASS && t != T_PAD && !st->note) st->note = (int8_t)(TR_LIVE_C[t] + chord_at(s).root);   /* the chord's root */
        }
        if (s >= song.tr[t].len) song.tr[t].len = (uint8_t)(((s / 16u) + 1u) * 16u);   /* tapping past the end grows it */
    }
    ui.held = -1;
}

static void held_step_turn(uint32_t enc, int32_t d)            /* SELECT: note, PRESETS: length */
{
    uint32_t t = ui.track, s = (ui.barpage * 16u + (uint32_t)ui.held) % NSTEP;
    step_t *st = &song.tr[t].st[s];
    song.tr[t].edited = 1;
    if (!(st->fl & ST_ON) && t != T_909) {
        st->fl = ST_ON; st->vel = st->vel ? st->vel : 100; st->len = st->len ? st->len : 1u;
        if (t >= T_BASS && t != T_PAD && !st->note) st->note = (int8_t)(TR_LIVE_C[t] + chord_at(s).root);
    }
    if (enc == ENC_SELECT) {
        if (t == T_909) ui.kitv = (uint8_t)clampi(ui.kitv + d, 0, NKIT - 1);
        else if (t == T_KICK || t == T_PAD) st->vel = (uint8_t)clampi(st->vel + d * 4, 1, 127);
        else {                                  /* the note, along the key's scale */
            int32_t n = st->note, i;
            for (i = 0; i < (d < 0 ? -d : d); i++) n = scale_step(n, d < 0 ? -1 : 1);
            st->note = (int8_t)clampi(n, 24, 108);
        }
    } else {
        st->len = (uint8_t)clampi(st->len + d, 1, 128);
    }
    ui.held_edit = 1;
}

/* a key in HOME: play the track; with REC and PLAY it becomes a step */
static void live_key(uint32_t k, int down)
{
    uint32_t t = ui.track;
    if (t == T_909 || t == T_KICK) {
        uint32_t w = white_idx[k] >= 0 ? (uint32_t)white_idx[k] : 99u;
        if (!down || (t == T_909 && w >= NKIT)) return;
        live_note(1, t, t == T_909 ? w : 0u, 110);
        if (ui.rec && eng_play) {
            step_t *st = &song.tr[t].st[play_step(t)];
            if (t == T_909) st->note = (int8_t)((uint8_t)st->note | (1u << w));
            st->fl |= ST_ON; st->vel = 110; st->len = 1;
        }
        return;
    }
    if (down) {
        uint32_t note = (uint32_t)clampi(TR_LIVE_C[t] + 12 * ui.oct - 7 + (int32_t)k, 0, 127);   /* key 7 (C4) = the track's C */
        held_keys_note[k] = note;
        live_note(1, t, (uint32_t)clampi((int32_t)note + oct_shift(t), 0, 127), 110);
        if (ui.rec && eng_play) {               /* into the step playing */
            uint32_t s = play_step(t);
            song.tr[t].st[s].note = (int8_t)note;
            song.tr[t].st[s].fl = ST_ON;
            song.tr[t].st[s].vel = 110;
            song.tr[t].st[s].len = t == T_PAD ? 16u : 1u;
            song.tr[t].edited = 1;
        }
    } else
        live_note(0, t, (uint32_t)clampi((int32_t)held_keys_note[k] + oct_shift(t), 0, 127), 0);
}

static void transport(int restart)
{
    if (restart || !eng_play) { eng_play = 1; eng_start = 1; }
    else {
        eng_play = 0;
        { uint32_t v; for (v = 0; v < NVOICE; v++) voice_off(&vox[v]); }
    }
}

static void buttons(uint32_t pressed, uint32_t released)
{
    uint32_t shift = fm1_in.buttons >> BTN_SEL & 1u;
    if (ui.view == V_ABOUT) { if (pressed) ui.view = ui.prev_view; return; }
    if (ui.view == V_SPLASH && pressed) ui.view = V_HOME;     /* a press ends the splash and still counts */
    if (pressed >> BTN_PLAY & 1u) transport((int)shift);
    if (pressed >> BTN_REC & 1u) { ui.rec ^= 1u; ui_say(ui.rec ? "REC ARMED" : "REC OFF"); }
    if (pressed >> BTN_HOME & 1u) { ui.view = V_HOME; ui.page = PG_PERFORM; ui.home_ms = fm1_ms; ui.held = -1; }
    if (released >> BTN_HOME & 1u) ui.home_ms = 0;
    if (pressed >> BTN_SEQ & 1u) { ui.view = V_SEQ; ui.held = -1; }
    if (pressed >> BTN_GLO & 1u) { if (ui.view == V_GLO) ui.gpage = (uint8_t)((ui.gpage + 1u) % NGPAGE); ui.view = V_GLO; }
    if (pressed >> BTN_EDIT & 1u) ui.page = 0;
    if (pressed >> BTN_ENV & 1u) ui.page = 1;
    if (pressed >> BTN_LFO & 1u) ui.page = 2;
    if (pressed >> BTN_FX & 1u) ui.page = 3;
    if (pressed >> BTN_ARP & 1u) {
        if (shift) { song.seed++; gen_song(); ui_say("NEW SONG"); }
        else if (ui.view == V_GLO && ui.gpage == 0u) {   /* Rollwerk's Any: a progression from any mood */
            prog_load(rnd(NPROG));
            harmony_changed();
            ui_say("ANY");
        } else {
            uint16_t g = song.tr[ui.track].gate;
            song.tr[ui.track].seed++;
            gen_track(ui.track);
            if (ui.track != T_PAD) song.tr[ui.track].gate = g;
            ui_say(ui.track == T_ROLL ? "REROLL" : "GENERATED");
        }
    }
    if (pressed >> BTN_SAVE & 1u) {
        if (eng_play) ui_say("STOP TO SAVE");
        else if (shift) {
            if (song_load(ui.slot) == 0) { eng_tempo(song.bpm10); ui.calm = song.g[G_MOTION] < 64u; ui_say("LOADED"); }
            else ui_say("EMPTY SLOT");
        } else {
            song.g[G_SLOT] = ui.slot;
            ui_say(song_save(ui.slot) == 0 ? "SAVED" : "SAVE FAILED");
            settings_save(&ui.slot, 1);
        }
    }
    if (ui.view == V_SEQ) {                     /* the bar page, 1-8 */
        if (pressed >> BTN_OCT_DOWN & 1u && ui.barpage > 0) { ui.barpage--; ui.held = -1; }
        if (pressed >> BTN_OCT_UP & 1u && ui.barpage < NBAR - 1u) { ui.barpage++; ui.held = -1; }
    } else {
        if (pressed >> BTN_OCT_DOWN & 1u && ui.oct > -2) ui.oct--;
        if (pressed >> BTN_OCT_UP & 1u && ui.oct < 2) ui.oct++;
    }
}

static void keys(void)
{
    uint32_t now = fm1_in.notes, down = now & ~prev_keys, up = prev_keys & ~now, k;
    uint32_t shift = fm1_in.buttons >> BTN_SEL & 1u;
    prev_keys = now;
    for (k = 0; k < 27u; k++) {
        int d = (int)(down >> k & 1u), u = (int)(up >> k & 1u);
        if (!d && !u) continue;
        if (ui.view == V_SPLASH && d) ui.view = V_HOME;
        if (ui.view == V_ABOUT) { if (d) ui.view = ui.prev_view; continue; }
        if (black_idx[k] >= 0 && black_idx[k] < (int8_t)NTR && (shift || ui.view == V_SEQ)) {
            if (d) {
                if (shift) { song.tr[black_idx[k]].mute ^= 1u; }
                else track_select((uint32_t)black_idx[k]);
            }
            continue;
        }
        if (ui.view == V_SEQ) {
            if (white_idx[k] >= 0) step_key((uint32_t)white_idx[k], d);
            else if (d && ui.track == T_909 && black_idx[k] >= (int8_t)NTR) ui.kitv = (uint8_t)(black_idx[k] - (int8_t)NTR);
            continue;
        }
        if (ui.view == V_GLO) {
            if (d && white_idx[k] >= 0 && white_idx[k] < (int8_t)NBAR) ui.glo_bar = (uint8_t)white_idx[k];
            continue;
        }
        live_key(k, d);
    }
}

static void encoders(void)
{
    int32_t d;
    uint32_t k;
    if ((d = fm1_enc_take(ENC_SELECT)) != 0) {
        if (ui.view == V_SEQ && ui.held >= 0) held_step_turn(ENC_SELECT, d);
        else if (ui.view == V_GLO) {            /* the bar's chord, through the mode's vocabulary */
            song.deg[ui.glo_bar] = (uint8_t)(((int32_t)song.deg[ui.glo_bar] + (int32_t)nvocab() + d % (int32_t)nvocab()) % (int32_t)nvocab());
            harmony_changed();
        }
        else track_select((uint32_t)((int32_t)ui.track + NTR + d % (int32_t)NTR));
    }
    if ((d = fm1_enc_take(ENC_ALGORITHM)) != 0) lift_turn(d);
    if ((d = fm1_enc_take(ENC_PRESETS)) != 0) {
        if (ui.view == V_SEQ && ui.held >= 0) held_step_turn(ENC_PRESETS, d);
        else if (ui.view == V_GLO && ui.gpage == 0u) {   /* the second half: off, then the vocabulary */
            int32_t h = song.half[ui.glo_bar] == 255u ? -1 : song.half[ui.glo_bar];
            h = clampi(h + d, -1, (int32_t)nvocab() - 1);
            song.half[ui.glo_bar] = h < 0 ? 255u : (uint8_t)h;
            harmony_changed();
        } else if (fm1_in.buttons >> BTN_SEL & 1u) {   /* SCL + PRESETS: the track's sound presets */
            uint32_t t = ui.track, n = NPRESET[t];
            preset_apply(t, (TR_PRESET(t) + n + (uint32_t)(d % (int32_t)n)) % n);
            ui_say(preset_name(t));
        } else {                                /* PRESETS: CUTOFF, Rollwerk's always-there filter */
            uint32_t t = cut_track();
            song.tr[t].par[P_CUT] = (uint8_t)clampi(song.tr[t].par[P_CUT] + accel(d), 0, 127);
            ui.cut_ms = fm1_ms;
            if (ui.rec && eng_play) song.tr[t].lock[play_step(t)][P_CUT] = (uint8_t)(song.tr[t].par[P_CUT] + 1u);
        }
    }
    for (k = 0; k < 4u; k++)
        if ((d = fm1_enc_take(ENC_KNOB1 + k)) != 0) knob(k, d);
    /* LIFT let go at 0 for 1.5 s: the lane takes over again */
    if (eng_lift_knob == 0 && ui.lift == 0 && fm1_ms - ui.lift_ms > 1500u) eng_lift_knob = -1;
    if (ui.home_ms && fm1_ms - ui.home_ms > 700u && ui.view == V_HOME) { ui.prev_view = V_HOME; ui.view = V_ABOUT; ui.home_ms = 0; }
}

/* the LEDs: SEQ shows the steps on the white keys and the tracks on the black; HOME lights the
 * notes sounding on the selected track and glows the chord tones */
#ifdef DINGHY_HOST
static void led_dim(uint32_t id, int on) { (void)id; (void)on; }
#else
static void led_dim(uint32_t id, int on)
{
    uint32_t p, r;
    for (p = 0; p < FM1_NCOL; p++)
        for (r = 1; r < 5u; r++)
            if (FM1_KEYMAP[r][p] == (int8_t)id) {
                if (on) fm1_led_dim[p] |= (uint8_t)(1u << r);
                else fm1_led_dim[p] &= (uint8_t)~(1u << r);
            }
}
#endif

static void leds(void)
{
    uint32_t k, t = ui.track, len = song.tr[t].len ? song.tr[t].len : 16u, play = eng_pos % len;
    uint32_t blink = (fm1_ms / 120u) & 1u;
    for (k = 0; k < 27u; k++) {
        int lit = 0, dim = 0;
        if (ui.view == V_SEQ) {
            if (white_idx[k] >= 0) {
                uint32_t s = ui.barpage * 16u + (uint32_t)white_idx[k];
                const step_t *st = &song.tr[t].st[s % NSTEP];
                int on = s < len && (st->fl & ST_ON) && (t != T_909 || ((uint8_t)st->note >> ui.kitv & 1u));
                lit = on;
                if (eng_play && s == play) lit = !on;
                dim = s < len;
            } else if (black_idx[k] < (int8_t)NTR) { lit = (uint32_t)black_idx[k] == t; dim = !song.tr[black_idx[k]].mute; }
            else if (t == T_909) { lit = (uint32_t)(black_idx[k] - (int8_t)NTR) == ui.kitv; dim = 1; }
        } else if (ui.view == V_GLO) {
            lit = white_idx[k] == (int8_t)ui.glo_bar;
            dim = white_idx[k] >= 0 && white_idx[k] < (int8_t)NBAR;
        } else if (t >= T_BASS) {
            int32_t note = TR_LIVE_C[t] + 12 * ui.oct - 7 + (int32_t)k, sounding = note + oct_shift(t);
            chord_t c = chord_at(eng_pos % NSTEP);
            uint32_t v, i, pc = (uint32_t)(note % 12 + 12) % 12u;
            for (v = TR_V0[t]; v < TR_V0[t] + TR_NV[t]; v++)
                if ((vox[v].stage == ENV_ATK || vox[v].stage == ENV_DEC) && (vox[v].tgt_q8 >> 8) == sounding) lit = 1;
            for (i = 0; i < chord_ntones(c); i++) if ((c.root + Q_IV[c.q][i]) % 12u == pc) dim = 1;   /* the chord tones glow */
        } else {
            lit = t == T_909 ? (white_idx[k] >= 0 && white_idx[k] < (int8_t)NKIT && fm1_ms - eng_kit_hit_ms[white_idx[k]] < 90u)
                             : (white_idx[k] == 0 && fm1_ms - eng_hit_ms[T_KICK] < 90u);
            dim = t == T_909 ? white_idx[k] >= 0 && white_idx[k] < (int8_t)NKIT : white_idx[k] == 0;
        }
        fm1_led_key(KEY_LED(k), lit);
        led_dim(KEY_LED(k), dim);
    }
    fm1_led_key(BTN_PLAY, eng_play && (eng_pos & 3u) == 0u ? 1 : eng_play);
    fm1_led_key(BTN_REC, ui.rec ? (eng_play ? 1 : blink) : 0);
    fm1_led_key(BTN_HOME, ui.view == V_HOME);
    fm1_led_key(BTN_SEQ, ui.view == V_SEQ);
    fm1_led_key(BTN_GLO, ui.view == V_GLO);
    fm1_led_key(BTN_EDIT, ui.page == 0u && ui.view != V_GLO);
    fm1_led_key(BTN_ENV, ui.page == 1u && ui.view != V_GLO);
    fm1_led_key(BTN_LFO, ui.page == 2u && ui.view != V_GLO);
    fm1_led_key(BTN_FX, ui.page == 3u && ui.view != V_GLO);
    fm1_led_key(BTN_SEL, fm1_in.buttons >> BTN_SEL & 1u);
    fm1_led_key(BTN_OCT_DOWN, ui.oct < 0);
    fm1_led_key(BTN_OCT_UP, ui.oct > 0);
}

static void app_main(void)
{
    uint32_t last_frame = 0, pressed, released;
    uint8_t slot = 0;
    keymap_init();
    if (settings_load(&slot, 1) == 0 && slot < NSLOT) ui.slot = slot;
    if (song_load(ui.slot) != 0) song_init(1);
    ui.view = V_SPLASH;
    ui.calm = song.g[G_MOTION] < 64u;
    eng_tempo(song.bpm10);
#ifdef DINGHY_HOST
    {                                           /* host: FINTON_SOLO=n plays track n alone, FINTON_SEED=n another song */
        const char *e = getenv("FINTON_SEED");
        uint32_t t;
        if (e) song_init((uint32_t)atoi(e));
        if ((e = getenv("FINTON_SOLO")) != 0)
            for (t = 0; t < NTR; t++) song.tr[t].mute = (uint8_t)(t != (uint32_t)atoi(e));
    }
#endif
    for (;;) {
        platform_poll();
        pressed = fm1_input_edges(&released);
        buttons(pressed, released);
        keys();
        encoders();
        motion_tick();
        if (ui.view == V_SPLASH && (fm1_ms > 2600u || pressed)) ui.view = V_HOME;   /* the dog wakes, for a moment */
        if (fm1_ms - last_frame >= 30u) {
            last_frame = fm1_ms;
            fx_params(eng_spb_q16, song.g, eng_lift > eng_build ? eng_lift : eng_build);
            draw_frame();
            leds();
        }
    }
}

#ifdef DINGHY_HOST
static void app_host_end(void)                  /* what the tests check */
{
    uint32_t t, s, k, locks = 0;
    for (t = 0; t < NTR; t++)
        for (s = 0; s < NSTEP; s++)
            for (k = 0; k < NPAR; k++) locks += song.tr[t].lock[s][k] != 0;
    printf("end: seed %u locks %u roll.cut %u lead.sub %u bpm %u msg '%s' view %u track %u lift %d prog %u deg0 %u\n", song.seed, locks, song.tr[T_ROLL].par[P_CUT],
           song.tr[T_LEAD].par[P_SUB], song.bpm10, ui.msg, ui.view, ui.track, eng_lift, song.prog, song.deg[0]);
}
#endif
