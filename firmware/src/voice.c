/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) — StudioKEYS changes and the VST3 companion */
/* Voice allocation, envelopes, LFO and per-track rendering.
 * Runs in the audio ISR.
 *
 * Each synth part has its own NVOICE voices (so MONO / LEGATO / UNISON keep using
 * v[0..]), but only NVOICE of all the parts' voices sound at once: a part that starts
 * a voice while the budget is full takes one from any part (voice_victim): the oldest
 * released voice, else the oldest extra UNISON voice, else the oldest held voice of a
 * POLY part that is not its lowest note. A voice taken from another part fades out
 * over one block (stage 4: the envelope goes to 0, the block's amplitude ramp
 * declicks it); one of the part's own is restarted in place, as before. Extra UNISON
 * voices only start when there is room. */
static uint32_t vage;                                   /* voice ages: one clock for every part */
static int32_t lfo_wave(track_t *t, uint32_t ph)
{
    switch (t->p[P_LWAVE]) {
    case 1:
        return osc_tri(ph);
    case 2:
        return (int32_t)(ph >> 16) - 32768;
    case 3:
        return ph < 0x80000000u ? 32767 : -32767;
    case 4:
        return (int32_t)(t->lfo_rnd >> 16) - 32768;
    default:
        return osc_sine(ph);
    }
}

/* the S&H value of a new LFO cycle. Parts 1..3 draw it from the shared generator (rng); part 4 has its
 * own xorshift, so the shared sequence, which also seeds the engines' noise (PHYS, VOICE, ..), does not
 * depend on part 4's LFO (the golden renders rely on it) */
#define LFO_SHARED_RNG 3u
static uint32_t lfo_rand(track_t *t)
{
    uint32_t s = t->lfo_rnd ? t->lfo_rnd : 0x9E3779B9u;
    if ((uint32_t)(t - trk) < LFO_SHARED_RNG)
        return rng();
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

static void track_lfo_tick(track_t *t)
{
    uint32_t old = t->lfo_ph;
    t->lfo_ph += LFO_INC[t->p[P_LRATE] & 127];
    if (t->lfo_ph < old)
        t->lfo_rnd = lfo_rand(t);
    t->lfo_val = lfo_wave(t, t->lfo_ph);
    if (t->lfo_fade < 32767) {
        int32_t step = (int32_t)(ENV_LIN[t->p[P_LFADE] & 127] >> 9);
        t->lfo_fade = t->p[P_LFADE] ? clamp(t->lfo_fade + (step ? step : 1), 0, 32767) : 32767;
    }
}

/* voices the engine may use (POLY and UNISON): its cap, else the whole pool */
static uint32_t trk_nvoice(const track_t *t)
{
    uint32_t c = ENGINES[t->engine]->poly;
    return c && c < VOICE_POOL ? c : VOICE_POOL;
}

/* UNISON levels are mixed for the device's eight: a host with a larger NVOICE must not spread
 * them wider (the detune and the level are part of the preset, not of the voice budget). */
static uint32_t trk_unison(const track_t *t)
{
    uint32_t n = trk_nvoice(t);
    return n < SK_UNISON ? n : (uint32_t) SK_UNISON;
}

/* the part's voice mode: P_VOICE, POLY for an engine of hits (engine_t.oneshot: DRUM) */
static uint32_t trk_vmode(const track_t *t)
{
    return ENGINES[t->engine]->oneshot ? V_POLY : (uint32_t)t->p[P_VOICE];
}

/* ------------------------------------------------- the shared voice budget --- */
/* Every voice is charged what it costs (engine_t.cost, host instructions per sample: the measured
 * column of tests/cpu_baseline.txt), and the four parts share one pot. That is the difference between
 * a cap and a budget: 12 SAMPLE voices and 12 PHYS voices are not the same load, so a count-only cap
 * either wastes the CPU on light engines or cracks on heavy ones.
 *
 * The pot starts at SKM_VOICE_LOAD, the worst case the old count budget could be asked for (12 voices,
 * every one of them the heaviest voice on the instrument: 12 x 526 = 6,312 host instr/sample), and
 * audio.c moves it: down when a block runs over 85 % of real time, back up when the load is light. So
 * "the maximum polyphony" is what THIS unit sustains, found on the instrument, not a number guessed
 * against an unknown clock. */
static uint32_t part_cost(const track_t *t)             /* one voice of this part, now */
{
    const engine_t *e = ENGINES[t->engine];
    uint32_t w = e->cost_of ? e->cost_of(t) : e->cost;
    return w ? w : 1u;
}

static uint32_t voice_load(void)                        /* what the sounding voices cost together */
{
    uint32_t i, n = 0;
    for (i = 0; i < VOICE_POOL; i++)
        if (vpool[i].part != VOICE_FREE && vpool[i].active)
            n += part_cost(&trk[vpool[i].part % NPART]);
    return n;
}

/* the pot: SKM_VOICE_LOAD, moved by audio.c (voice_budget_shed on an overrun, voice_budget_rest on a
 * light block) and, on the host, by the companion's own CPU feedback (SK_VOICE_CAP) */
void voice_budget_shed(void);
void voice_budget_rest(void);
static uint32_t vcap = SKM_VOICE_LOAD;
static uint32_t voice_cap(void)
{
#ifdef SK_VOICE_CAP
    return SK_VOICE_CAP();
#else
    return vcap;
#endif
}

void voice_budget_shed(void)                            /* audio.c: the last block ran over 85% */
{
    if (vcap > SKM_VOICE_LOAD / 4u)
        vcap -= vcap / 8u;
}

void voice_budget_rest(void)                            /* .. and it is light again: creep back up */
{
    if (vcap < SKM_VOICE_LOAD)
        vcap += SKM_VOICE_LOAD / 64u;
}

/* 1 when one more voice of part t does not fit the pot (what the tests fill with) */
int voice_full(const track_t *t)
{
    return voice_load() + part_cost(t) > voice_cap();
}

/* the pot itself (engine-cost units), after any shedding: the DIAG page and the tests read it */
uint32_t voice_budget_cap(void)
{
    return voice_cap();
}

/* silence and empty the pool (host_tracks_init, the console's panic): every slot back to the arena and
 * the pot back to its nominal value */
void voices_reset(void)
{
    uint32_t i;
    memset(vpool, 0, sizeof vpool);
    for (i = 0; i < VOICE_POOL; i++)
        vpool[i].part = VOICE_FREE;
    vcap = SKM_VOICE_LOAD;
}

static uint32_t voices_busy(void)                       /* sounding voices of all parts (not the fading ones) */
{
    uint32_t i, n = 0;
    for (i = 0; i < VOICE_POOL; i++)
        n += vpool[i].part != VOICE_FREE && vpool[i].active && vpool[i].stage != 4u;
    return n;
}

/* the pool slot for part t at pi k: the part's k-th voice, made when it has none there yet (0 = the
 * pool is full and nothing could be reclaimed) */
static voice_t *voice_pi(track_t *t, uint32_t k)
{
    uint32_t p = (uint32_t)(t - trk), i, j;
    voice_t *v = voice_at(t, k);
    if (v)
        return v;
    for (i = 0; i < VOICE_POOL && vpool[i].part != VOICE_FREE; i++)
        ;
    if (i == VOICE_POOL) {                              /* full: reclaim a voice that is silent, or one that
                                                         * is already fading out for another part's note */
        for (i = 0; i < VOICE_POOL; i++)
            if (vpool[i].part != p && vpool[i].part != VOICE_FREE &&
                (!vpool[i].active || vpool[i].stage == 4u))
                break;
        if (i == VOICE_POOL) {                          /* none silent: a part with nothing to lose */
            for (j = 0; j < NPART && i == VOICE_POOL; j++) {
                uint32_t q, n = 0;
                for (q = 0; q < VOICE_POOL; q++)
                    n += vpool[q].part == j && vpool[q].active;
                if (!n)
                    for (q = 0; q < VOICE_POOL && i == VOICE_POOL; q++)
                        if (vpool[q].part == j)
                            i = q;
            }
        }
        if (i == VOICE_POOL)
            return 0;
    }
    v = &vpool[i];
    memset(v, 0, sizeof *v);
    v->part = (uint8_t)p;
    v->pi = (uint8_t)k;
    v->sslot = (uint8_t)k;
    return v;
}

/* a side-state slot of the part's engine that no other voice of the part is using (PHYS 3, GRAIN 3,
 * FM6 6, WHEEL / SLICE / DIGITAL 8): the engines index their tables with it, so it must not be the
 * pool index any more. A voice keeps the slot of its own pi when that one is free - the old
 * (v - t->v) mapping - so an engine's table does not shuffle between voices and a note sounds the
 * same as it did before the pool; only a voice beyond the cap (a mode or engine switch left it) and a
 * part with more voices than slots fall back to another slot. */
static uint32_t voice_sslot(const track_t *t, const voice_t *self, uint32_t np)
{
    uint32_t k, i;
    if (!np)
        return 0;
    if (self->pi < np) {
        for (i = 0; i < VOICE_POOL; i++)
            if (&vpool[i] != self && vpool[i].part == (uint8_t)(t - trk) && vpool[i].sslot == self->pi)
                break;
        if (i == VOICE_POOL)
            return self->pi;
    }
    for (k = 0; k < np; k++) {
        for (i = 0; i < VOICE_POOL; i++)
            if (&vpool[i] != self && vpool[i].part == (uint8_t)(t - trk) && vpool[i].sslot == k)
                break;
        if (i == VOICE_POOL)
            return k;
    }
    return (uint32_t)self->pi % np;                     /* more voices than slots: share, as the old modulo did */
}

static uint32_t lowest_held(const track_t *t)           /* the lowest held note (the bass), 255 = none */
{
    uint32_t k, i, low = 255u, np = trk_nvoice(t);
    for (k = 0; k < np; k++)
        for (i = 0; i < VOICE_POOL; i++)
            if (vpool[i].part == (uint8_t)(t - trk) && vpool[i].pi == k && vpool[i].active &&
                vpool[i].gate && vpool[i].note < low)
                low = vpool[i].note;
    return low;
}

/* the voice to give up for a new one (see the top); soft: only released voices and
 * extra voices of other non-POLY parts (also ones left after a mode change).
 * Returns its pool slot; *pp its part; VOICE_POOL = none */
static uint32_t voice_victim(const track_t *self, int soft, track_t **pp)
{
    uint32_t p, i, best = VOICE_POOL, cat = 4;
    uint32_t best_age = 0;
    for (p = 0; p < NPART; p++) {
        track_t *t = &trk[p];
        uint32_t mode = trk_vmode(t);
        uint32_t low = mode == V_POLY ? lowest_held(t) : 255u;
        if (soft && t == self)
            continue;
        for (i = 0; i < VOICE_POOL; i++) {
            const voice_t *v = &vpool[i];
            uint32_t c;
            if (v->part != p || !v->active || v->stage == 4u)
                continue;
            if (!v->gate)
                c = 1;                                  /* released */
            else if (mode != V_POLY && v->pi > 0)
                c = 2;                                  /* extra UNISON, or a voice left by a mode / cap change */
            else if (!soft && mode == V_POLY && v->note != low)
                c = 3;                                  /* held, not the bass */
            else
                continue;
            if (c < cat || (c == cat && v->age < best_age)) {
                cat = c;
                best = i;
                best_age = v->age;
                *pp = t;
            }
        }
    }
    return best;
}

static uint32_t voice_kills;                            /* voices given up (budget, overload): console, hostsim */
static void voice_kill(voice_t *v)                      /* fade out over the next block (env_tick stage 4) */
{
    v->gate = 0;
    v->stage = 4;
    voice_kills++;
}

/* the budget is full: free a voice for part t. 0 = nothing to take (soft) */
static int voice_room(track_t *t, int soft)
{
    track_t *vp = 0;
    uint32_t k;
    if (voice_load() + part_cost(t) <= voice_cap())
        return 1;
    k = voice_victim(t, soft, &vp);
    if (k == VOICE_POOL)
        return !soft;                                   /* hard: over the budget (cannot happen: each of the
                                                         * NPART parts protects its bass at most) */
    voice_kill(&vpool[k]);
    return 1;
}

static voice_t *voice_reuse(track_t *t, voice_t *v)
{
    if (v->stage == 4u)
        voice_room(t, 0);                               /* its budget was taken before this block */
    return v;
}

/* POLY allocation (within the engine's voice cap). DRUM: reuse its lane; other engines: the same note.
 * Else a free voice: ROTATE takes
 * the next one round-robin (release tails ring out), REUSE (or any GLIDE) takes
 * the free voice whose last pitch is closest, so poly portamento moves each
 * voice the shortest way. Steal: the oldest released voice, else the oldest
 * held one, never the lowest held note (the bass). A free voice needs room in the
 * shared budget: when the voice to give up is one of this part's, it is restarted
 * in place instead. */
static voice_t *voice_alloc(track_t *t, uint32_t note)
{
    uint32_t i, k, np = trk_nvoice(t), best = np, low, nfree = 0, best_age = 0;
    int32_t bd = 0x7FFFFFFF;
    voice_t *v;
    if (t->engine == ENGI_DRUM) {
        v = drum_reuse(t, note);
        if (v)
            return voice_reuse(t, v);
    }
    for (k = 0; k < np; k++) {
        v = voice_at(t, k);
        if (v && v->active && v->note == note)
            return voice_reuse(t, v);
        nfree += !v || !v->active;
    }
    if (nfree && voice_load() + part_cost(t) > voice_cap()) {
        track_t *vp = 0;
        uint32_t vi = voice_victim(t, 0, &vp);
        if (vi != VOICE_POOL) {
            if (vp == t)
                return &vpool[vi];                      /* our own: restart it in place (no click) */
            voice_kill(&vpool[vi]);
        }
    }
    if (t->p[P_ALLOC] || t->p[P_GLIDE]) {
        for (k = 0; k < np; k++) {
            int32_t d;
            v = voice_at(t, k);
            if (v && v->active)
                continue;
            d = (v ? v->pitch_cur : 0) - (int32_t)note * 16;
            d = d < 0 ? -d : d;
            if (!v || !v->pitch_cur)
                d = 0x7FFFFFF0;                          /* never played: last choice */
            if (d < bd) {
                bd = d;
                best = k;
            }
        }
    } else {
        uint32_t rr = t->rr % np;
        for (i = 0; i < np && best == np; i++) {
            k = (rr + i) % np;
            v = voice_at(t, k);
            if (!v || !v->active)
                best = k;
        }
        t->rr = (uint8_t)((best == np ? rr : best) + 1u) % np;
    }
    if (best < np) {
        v = voice_pi(t, best);
        return v ? v : 0;
    }
    /* nothing free: steal, the lowest held note protected */
    low = lowest_held(t);
    best = np;
    for (k = 0; k < np; k++)                             /* oldest released (not one fading out for another part) */
        if ((v = voice_at(t, k)) && v->active && !v->gate && v->stage != 4u &&
            (best == np || v->age < best_age)) {
            best = k;
            best_age = v->age;
        }
    if (best == np)                                      /* none released: the oldest held, not the bass */
        for (k = 0; k < np; k++)
            if ((v = voice_at(t, k)) && v->active && v->note != low &&
                (best == np || v->age < best_age)) {
                best = k;
                best_age = v->age;
            }
    v = voice_pi(t, best == np ? 0u : best);
    return v ? voice_reuse(t, v) : 0;
}

/* glide: RATE = a fixed speed, TIME = the same time for any interval */
static void glide_set(track_t *t, voice_t *v, int glide)
{
    if (!glide || !v->pitch_cur) {
        v->pitch_cur = v->pitch16;
        v->gstep = 0;
        return;
    }
    if (t->p[P_GLMODE]) {
        int32_t d = v->pitch16 - v->pitch_cur, ticks = 1 + t->p[P_GLIDE] * t->p[P_GLIDE] / 8;
        d = d < 0 ? -d : d;
        v->gstep = d / ticks > 0 ? d / ticks : 1;
    } else {
        v->gstep = 0;
    }
}

static void voice_start(track_t *t, voice_t *v, uint32_t note, uint32_t vel, int glide)
{
    const engine_t *e = ENGINES[t->engine];
    int sounding = v->active && v->stage != 0;
    uint32_t ph0 = v->ph[0], ph1 = v->ph[1], ph2 = v->ph[2];
    int32_t s0 = v->s[0], s1 = v->s[1], s4 = v->s[4], s5 = v->s[5], s6 = v->s[6], s7 = v->s[7];
    uint32_t keep = e->keep;
    v->note = (uint8_t)note;
    v->vel = (uint8_t)vel;
    v->gate = 1;
    v->active = 1;
    v->stage = 1;
    v->age = ++vage;
    v->pitch16 = (int32_t)note * 16;
    v->mvel = t->m_vel;                                 /* mod.c: the note's VEL and RAND */
    v->mrnd = t->m_rnd;
    t->m_vi = v->pi;
    if (!sounding)                                      /* a fresh voice takes a free side-state slot */
        v->sslot = (uint8_t)voice_sslot(t, v, trk_nvoice(t));
    glide_set(t, v, glide);
    if (!sounding) {
        v->env = 0;
        v->env_out = 0;
    }                                                   /* sounding: the attack starts from the current level */
    e->note_on(t, v);
    if (sounding && !e->sampled) {                     /* retrigger / steal: keep phases and filter */
        v->ph[0] = ph0;                                 /* states (resetting them clicks) */
        v->ph[1] = ph1;
        v->ph[2] = ph2;
        if (keep & 0x01u)                               /* engine_t.keep: the slots the engines use */
            v->s[0] = s0;
        if (keep & 0x02u)
            v->s[1] = s1;
        if (keep & 0x10u)
            v->s[4] = s4;
        if (keep & 0x20u)
            v->s[5] = s5;
        if (keep & 0x40u)
            v->s[6] = s6;
        if (keep & 0x80u)
            v->s[7] = s7;
    }
}

/* MONO / LEGATO / UNISON: one note on one voice (eight for UNISON, or the
 * engine's voice cap, spread by DETUNE over the same width) */
static void mono_play(track_t *t, uint32_t note, uint32_t vel, int retrig, int glide)
{
    uint32_t mode = (uint32_t)t->p[P_VOICE], nv = mode == V_UNISON ? trk_unison(t) : 1u, i;
    for (i = 0; i < nv; i++) {
        voice_t *v = voice_pi(t, i);
        int32_t k = 2 * (int32_t)i - (int32_t)(nv - 1u);   /* -7 .. 7 */
        if (!v)
            break;
        if (nv > 1u && nv < SK_UNISON)
            k = k * (SK_UNISON - 1) / (int32_t)(nv - 1u);   /* fewer voices: the outer ones as wide */
        v->fine = nv > 1u ? k * t->p[P_DETUNE] * 56 / 889 : 0;   /* up to ~±40 cents */
        if (retrig || !v->active || !v->gate) {
            if ((!v->active || v->stage == 4u) && !voice_room(t, i > 0))
                continue;                                   /* no room for this extra UNISON voice */
            /* level: about the same sum for 8 or 4 voices at random phases */
            voice_start(t, v, note, nv >= SK_UNISON ? vel * 36u / 100u : nv > 1u ? vel / 2u : vel, glide);
            if (nv > 1u && i && !ENGINES[t->engine]->sampled) {   /* random start phases: */
                static uint32_t seed = 0x1234567u;          /* in phase they stack, evenly spread they cancel */
                seed = seed * 1664525u + 1013904223u;
                v->ph[0] += seed;
            }
        } else {                                            /* legato: new pitch, same envelope */
            v->note = (uint8_t)note;
            v->pitch16 = (int32_t)note * 16;
            v->mvel = t->m_vel;
            v->mrnd = t->m_rnd;
            if (vel > v->vel && nv == 1u)
                v->vel = (uint8_t)vel;
            glide_set(t, v, glide);
        }
    }
    t->mono_note = (uint8_t)note;
}

/* the note the mono stack asks for (PRIO: LAST / LOW / HIGH), 0 = none */
static uint32_t mono_pick(const track_t *t)
{
    uint32_t i, n;
    if (!t->nmono)
        return 0;
    n = t->mono_stack[t->nmono - 1u];
    for (i = 0; i < t->nmono; i++) {
        if (t->p[P_PRIO] == 1 && t->mono_stack[i] < n)
            n = t->mono_stack[i];
        if (t->p[P_PRIO] == 2 && t->mono_stack[i] > n)
            n = t->mono_stack[i];
    }
    return n;
}

static void mono_remove(track_t *t, uint32_t note)
{
    uint32_t j, k = 0;
    for (j = 0; j < t->nmono; j++)
        if (t->mono_stack[j] != note)
            t->mono_stack[k++] = t->mono_stack[j];
    t->nmono = (uint8_t)k;
}

static void trk_note_on(track_t *t, uint32_t note, uint32_t vel)
{
    uint32_t any = 0, i, mode = trk_vmode(t);
    if (t->p[P_MUTE])
        return;
    if (t->xf_on || t->eng_req != t->engine) {          /* engine switch under way: after the fade */
        for (i = 0; i < t->xp_n && t->xp_note[i] != note; i++)
            ;
        if (i == t->xp_n && t->xp_n < 4u)
            t->xp_n++;
        i = i < t->xp_n ? i : t->xp_n - 1u;             /* (full: the last one is replaced) */
        t->xp_note[i] = (uint8_t)note;
        t->xp_vel[i] = (uint8_t)vel;
        return;
    }
    mod_note(t, note, vel);                             /* the matrix's VEL / KEY / RAND */
    for (i = 0; i < VOICE_POOL; i++)
        any |= vpool[i].part == (uint8_t)(t - trk) && vpool[i].gate;
    if (!any) {                                        /* fresh phrase: LFO retrigger and fade */
        t->lfo_ph = (uint32_t)t->p[P_LPHASE] << 25;
        t->lfo_fade = 0;
    }
    if (mode == V_POLY) {
        voice_t *v = voice_alloc(t, note);
        t->nmono = 0;                                   /* no stale mono stack after a mode change */
        t->mono_note = 0;
        if (!v)
            return;                                     /* the pool is spent (48 voices): nothing to give */
        v->fine = 0;
        voice_start(t, v, note, vel, t->p[P_GLIDE] != 0 && !ENGINES[t->engine]->oneshot);
        return;
    }
    {
        const voice_t *lead = voice_lead(t);
        uint32_t sounding = t->mono_note && lead && lead->active && lead->gate, want;
        mono_remove(t, note);
        if (t->nmono >= 8u)
            mono_remove(t, t->mono_stack[0]);
        t->mono_stack[t->nmono++] = (uint8_t)note;
        want = mono_pick(t);
        if (sounding && want == t->mono_note)
            return;                                     /* LOW / HIGH priority: this key does not win */
        if (mode == V_MONO)                             /* MONO: always retrigger, glide when GLIDE is set */
            mono_play(t, want, vel, 1, t->p[P_GLIDE] != 0 || t->slide_glide);
        else                                            /* LEGATO / UNISON: glide only between held notes */
            mono_play(t, want, vel, !sounding, sounding && (t->p[P_GLIDE] != 0 || t->slide_glide));
    }
}

static void trk_note_off(track_t *t, uint32_t note)
{
    uint32_t i, k = 0, mode = trk_vmode(t);
    for (i = 0; i < t->xp_n; i++)                       /* not sounding yet (engine switch): forget it */
        if (t->xp_note[i] != note) {
            t->xp_note[k] = t->xp_note[i];
            t->xp_vel[k++] = t->xp_vel[i];
        }
    t->xp_n = (uint8_t)k;
    mono_remove(t, note);                              /* a mode change must not retain a released key */
    if (mode != V_POLY) {
        uint32_t nv = mode == V_UNISON ? trk_unison(t) : 1u;
        if (t->mono_note == note) {
            const voice_t *lead = voice_lead(t);
            uint32_t next = mono_pick(t);
            if (next) {                                 /* fall back to a held note, legato */
                mono_play(t, next, lead ? lead->vel : 100u, 0, t->p[P_GLIDE] != 0);
            } else {
                for (i = 0; i < nv; i++) {
                    voice_t *v = voice_at(t, i);
                    if (v && v->gate) {
                        v->gate = 0;
                        v->stage = 3;
                    }
                }
                t->mono_note = 0;
            }
        }
    }
    for (i = 0; i < VOICE_POOL; i++)                    /* a fallback above already has its new pitch */
        if (vpool[i].part == (uint8_t)(t - trk) && vpool[i].note == note && vpool[i].gate) {
            vpool[i].gate = 0;
            vpool[i].stage = 3;
        }
}

static void trk_all_off(track_t *t)
{
    uint32_t i;
    for (i = 0; i < VOICE_POOL; i++)
        if (vpool[i].part == (uint8_t)(t - trk)) {
            vpool[i].gate = 0;
            vpool[i].stage = vpool[i].active ? 3 : 0;
        }
    t->nmono = 0;
    t->mono_note = 0;
    t->xp_n = 0;
}

/* engine switch, at each block start (events_block), before any note of the block. The UI writes
 * eng_req and the new engine's P_E0..P_E7 together (IRQ off). The part's sounding voices (released by
 * the preset change) then fade out over XF_BLOCKS blocks on the old engine, rendered with its own
 * parameters (pe_old: an engine never reads another engine's values, which index its tables); only
 * then the engine switches and the notes that came during the fade start on it. A part with nothing
 * sounding switches at once. */
#define XF_BLOCKS 4u                                    /* 4 x 32 samples: 2.9 ms */
static void engine_block(track_t *t)
{
    uint32_t i, any = 0;
    if (t->xf_on || t->eng_req != t->engine) {
        if (!t->xf_on) {
            for (i = 0; i < VOICE_POOL; i++)
                any |= vpool[i].part == (uint8_t)(t - trk) && vpool[i].active;
            if (any) {
                t->xf_on = 1;
                t->xf = XF_BLOCKS;
                return;
            }
        } else if (t->xf) {
            return;                                     /* still fading (track_render counts down) */
        }
        for (i = 0; i < VOICE_POOL; i++) {              /* faded to 0: the pool takes them back */
            voice_t *v = &vpool[i];
            if (v->part != (uint8_t)(t - trk))
                continue;
            memset(v, 0, sizeof *v);
            v->part = VOICE_FREE;
        }
        t->engine = eng_idx(t->eng_req);
        t->xf_on = 0;
        t->nmono = 0;
        t->mono_note = 0;
        {
            uint32_t n = t->xp_n;
            t->xp_n = 0;
            for (i = 0; i < n; i++)
                trk_note_on(t, t->xp_note[i], t->xp_vel[i]);
        }
    }
    for (i = 0; i < 12u; i++)                          /* the engine's own values, for a later fade */
        t->pe_old[i] = t->p[P_E0 + i];
}

/* one control tick (CTL samples) of the amplitude envelope; returns Q15 */
static int32_t env_tick(track_t *t, voice_t *v)
{
    const int16_t *p = t->p;
    int32_t sus = (int32_t)p[P_SUS] << 17;              /* Q24 */
    switch (v->stage) {
    case 1:
        v->env += (int32_t)ENV_LIN[p[P_ATK] & 127];
        if (v->env >= (1 << 24)) {
            v->env = 1 << 24;
            v->stage = 2;
        }
        break;
    case 2:
        v->env += mulq16(sus - v->env, ENV_EXP[p[P_DEC] & 127]);
        break;
    case 3:
        v->env -= mulq16(v->env, ENV_EXP[p[P_REL] & 127]);
        if (v->env < (1 << 12)) {
            v->env = 0;
            v->stage = 0;
            v->active = 0;
        }
        break;
    case 4:                                             /* given up for another part: this block fades it */
        v->env = 0;
        v->stage = 0;
        v->active = 0;
        break;
    default:
        v->env = 0;
        break;
    }
    return v->env >> 9;
}

/* render one block of a part into out (cleared here); returns the voices rendered. side: the part's
 * stereo side signal (S = L - R) for an engine with render_stereo, 0 = the mono path only. */
/* Channel bend is live performance state, outside projects/presets. Q8 semitones. */
static int32_t midi_bend_q8[NTRK], midi_bend_target[NTRK];
static uint32_t track_render(track_t *t, int32_t *out, int32_t *side, uint32_t n)
{
    const engine_t *e = ENGINES[t->engine];
    const int16_t *p = t->p;
    uint32_t i;
    int32_t lfo = mulq15(t->lfo_val, t->lfo_fade);
    /* TUNE in cents: whole 1/16 semitones in the pitch, the rest as a fine factor (no dead zone) */
    int32_t tune = song.g[G_TUNE] >= 0 ? song.g[G_TUNE] * 16 / 100 : -((-song.g[G_TUNE] * 16 + 99) / 100);
    int32_t tune_fine = (song.g[G_TUNE] * 16 - tune * 100) * 2367 / 16000;   /* rest, in 1/4096 (1 ct = 2.367) */
    int32_t bend, bend16, bend_fine;
    uint32_t ti = (uint32_t)(t - trk);
    if (ENGINES[eng_idx(t->eng_req)] == &ENG_DRUM) {
        midi_bend_q8[ti] = midi_bend_target[ti] = 0;
    } else {
        int32_t d = midi_bend_target[ti] - midi_bend_q8[ti];
        /* ~6 ms smoothing, with an exact landing (no permanent small offset). */
        midi_bend_q8[ti] += d > 0 ? (d < 4 ? d : (d + 3) / 4) : (d > -4 ? d : (d - 3) / 4);
    }
    bend = midi_bend_q8[ti];
    bend16 = bend >= 0 ? bend / 16 : -((-bend + 15) / 16);
    bend_fine = (bend - bend16 * 16) * 2367 / 2560;
    uint32_t nr = 0, fade = t->xf_on && t->xf;
    int16_t pe_new[12];
    for (i = 0; i < n; i++)
        out[i] = 0;
    if (side)
        for (i = 0; i < n; i++)
            side[i] = 0;
    if (fade)                                           /* engine switch: the old engine, its own values */
        for (i = 0; i < 12u; i++) {
            pe_new[i] = t->p[P_E0 + i];
            t->p[P_E0 + i] = t->pe_old[i];
        }
    track_lfo_tick(t);
    if (e->block)                                       /* the engine's per-part work (WHEEL: bars, rotor) */
        e->block(t);
    for (i = 0; i < VOICE_POOL; i++) {
        voice_t *v = &vpool[i];
        vmod_t m;
        int32_t env, pitch;
        if (v->part != (uint8_t)ti || !v->active)
            continue;
        {
            env = env_tick(t, v);
            if (e->ownenv && v->active) {               /* the engine's envelopes (FM6): they end the voice */
                if (e->done(t, v)) {
                    v->active = v->gate = 0;
                    v->stage = 0;
                    v->env = v->env_out = 0;
                    continue;
                }
                v->env = 1 << 24;                       /* (the ADSR's release never ends it) */
                env = 32767;
            }
            if (e->amp)                                 /* the engine's own amplitude curve */
                env = e->amp(t, v, env);
            m.envq15 = e->ownenv ? 0 : env;
            m.amp1 = e->ownenv ? env : mulq15(env, v->vel * 258);
            if (p[P_LD_AMP])
                m.amp1 = mulq15(m.amp1, 32767 - mulq15((lfo + 32768) >> 1, p[P_LD_AMP] * 258));
            if (fade)                                   /* linear to 0 over the fade */
                m.amp1 = m.amp1 * (int32_t)(t->xf - 1u) / (int32_t)XF_BLOCKS;
            m.amp0 = v->env_out;
            v->env_out = m.amp1;
        }
        if (v->pitch_cur != v->pitch16) {               /* glide */
            int32_t d = v->pitch16 - v->pitch_cur;
            int32_t st = v->gstep ? v->gstep : (int32_t)(ENV_LIN[(p[P_GLIDE] ? p[P_GLIDE] : 40) & 127] >> 14);   /* slide: ~45 ms */
            if (st < 1)
                st = 1;
            v->pitch_cur += d > 0 ? (d < st ? d : st) : (-d < st ? d : -st);
        }
        if (!env && !m.amp0 && v->stage == 2 && !e->sampled)
            continue;                                   /* held at a silent sustain (SUS 0): nothing to render */
        pitch = v->pitch_cur + tune + bend16 + ((lfo * p[P_LD_PIT] * 3) >> 15) + ((m.envq15 * p[P_ED_PIT] * 3) >> 15);
        m.pitch16 = clamp(pitch, 0, 2047);
        m.inc = tuned_inc(m.pitch16);
        m.fine = v->fine + tune_fine + bend_fine;
        if (v->fine + tune_fine + bend_fine)             /* residual below 1/16 semitone */
            m.inc += (uint32_t)((int32_t)(m.inc >> 12) * (v->fine + tune_fine + bend_fine));
        m.cutoff = ((lfo * p[P_LD_FLT]) >> 7) + ((m.envq15 * p[P_ED_FLT]) >> 7);
        m.cutoff += (int32_t)t->mw << 6;                /* mod wheel opens the filter, matrix or not */
        if (v->vel > 110)                               /* accent opens the filter with the env */
            m.cutoff += (m.envq15 * 24) >> 7;
        m.shape = (64 << 8) + ((lfo * p[P_LD_SHP]) >> 7) + ((m.envq15 * p[P_ED_SHP]) >> 7);
        if (mod.on)                                     /* the modulation matrix (mod.c) */
            mod_voice(t, v, &m, v->fine + tune_fine + bend_fine);
        if (side && e->render_stereo)
            e->render_stereo(t, v, out, side, n, &m);
        else
            e->render(t, v, out, n, &m);
        nr++;
    }
    if (fade) {
        for (i = 0; i < 12u; i++)
            t->p[P_E0 + i] = pe_new[i];
        t->xf--;
    }
    return nr;
}
