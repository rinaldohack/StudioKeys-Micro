/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) — StudioKEYS changes and the VST3 companion */
/* The real audio ISR with a simulated DMA and clock: overload must free work
 * within one block without cutting another part's lead or bass. */
#define main hostsim_main
#include "hostsim.c"
#undef main

#define FM1_AUDIO_HALF 0x80u
#define FM1_TICKS_PER_US 1u
static uint32_t host_clock, host_us, host_half_reads, host_dma_advanced, host_acks, host_nest;
static uint8_t host_pending;
static volatile uint32_t t5_nested_ticks;              /* audio.c's: TIMER5 nested in the render (main.c) */
static uint32_t fm1_ticks(void) { t5_nested_ticks = host_nest; host_clock += host_us; return host_clock; }
static uint8_t fm1_audio_pending(void) { return host_pending; }
static void fm1_audio_ack_aux(uint8_t p) { (void)p; }
static uint32_t fm1_audio_free_half(void) { return host_dma_advanced && host_half_reads++ > 0u; }
static void fm1_audio_ack_half(void) { host_acks++; }
static void fm1_audio_init(int32_t *b, uint32_t n, void (*isr)(void), uint32_t p)
{ (void)b; (void)n; (void)isr; (void)p; }
void isr_alnk0(void) {}
#define SKM_UAC 1                                  /* audio.c taps USB audio: here into host_tap */
static int32_t host_tap[2 * 64];
static uint32_t host_tap_n;
static void uac_tap(const int32_t *out, uint32_t n) { memcpy(host_tap, out, 8u * n); host_tap_n = n; }
static void uac_render_start(void) {}
#include "../firmware/src/audio.c"

/* ui.c and game.c are not part of this test: audio_block's mute gate reads their flags */
uint8_t smk_out_mute;
uint8_t sk_game_mute(void) { return 0; }

static int bad;
static void check(const char *what, int ok)
{
    printf("audio: %-74s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
static void fresh(void)
{
    uint32_t p;
    memset(trk, 0, sizeof trk);
    memset(&song, 0, sizeof song);
    memset(&chain, 0, sizeof chain);
    host_tracks_init();
    for (p = 0; p < NPART; p++) host_preset(&trk[p], 0, 0);
    memset(&skm_dbg, 0, sizeof skm_dbg);
    audio_cpu_rem = audio_halves = audio_max_us = 0;
    shed_req = 0; shed_count = 0; shed_over = 0;
    host_clock = host_us = host_half_reads = host_dma_advanced = host_acks = host_nest = 0;
    host_pending = FM1_AUDIO_HALF;
    transport_req = panic_req = 0;
}
static voice_t *held(track_t *t, uint32_t note)
{
    uint32_t i;
    for (i = 0; i < VOICE_POOL; i++)
        if (vpool[i].part == (uint8_t)(t - trk) && vpool[i].active && vpool[i].note == note)
            return &vpool[i];
    return 0;
}
static void overload(void)
{
    voice_t *lead, *bass, *extra;
    uint32_t mode, i;
    int32_t out[2 * CTL];
    fresh();
    trk[0].p[P_VOICE] = V_MONO;
    trk_note_on(&trk[0], 40, 100);
    trk[1].p[P_VOICE] = V_POLY;
    trk_note_on(&trk[1], 48, 100);
    trk_note_on(&trk[1], 60, 100);
    lead = held(&trk[0], 40); bass = held(&trk[1], 48); extra = held(&trk[1], 60);
    shed_voice();
    check("overload leaves held notes playing",
          lead && bass && extra && lead->gate && bass->gate && extra->gate && shed_count == 0u);
    mix_block(out, CTL);
    check("a held chord is still there after the block", extra && extra->active && voices_busy() == 3u);
    shed_voice();
    check("a lead alone on each part stays", lead->gate && bass->gate && shed_count == 0u);

    fresh();
    host_preset(&trk[0], ENGI_DRUM, 0);
    trk_note_on(&trk[0], 36, 100); trk_note_on(&trk[0], 38, 100);
    lead = held(&trk[0], 36); extra = held(&trk[0], 38);
    shed_voice(); mix_block(out, CTL);
    check("a held drum hit is not cut short",
          lead && extra && lead->active && extra->active && voices_busy() == 2u);

    fresh();
    trk[0].p[P_VOICE] = V_UNISON;
    trk_note_on(&trk[0], 55, 100);
    shed_voice(); mix_block(out, CTL);
    check("UNISON keeps every voice of the note you are holding",
          voice_lead(&trk[0])->active && voice_lead(&trk[0])->gate && voices_busy() == trk_unison(&trk[0]));

    for (mode = V_MONO; mode <= V_LEGATO; mode++) {
        fresh(); trk[0].p[P_VOICE] = V_POLY;
        for (i = 0; i < VOICE_POOL && !voice_full(&trk[0]); i++) trk_note_on(&trk[0], 48u + 2u * i, 100);
        trk[0].p[P_VOICE] = (int16_t)mode;
        shed_voice(); mix_block(out, CTL);
        check("a mode change keeps the notes that are still held",
              shed_count == 0u && voice_lead(&trk[0])->gate && voice_full(&trk[0]) &&
              voice_load() <= (uint32_t)SKM_VOICE_LOAD + 600u);
    }

    fresh();
    trk[0].p[P_VOICE] = V_POLY;
    trk_note_on(&trk[0], 60, 100); trk_note_on(&trk[0], 64, 100);
    lead = held(&trk[0], 60); extra = held(&trk[0], 64);
    trk_note_off(&trk[0], 60); trk_note_off(&trk[0], 64);
    lead->env = 10000; extra->env = 10;
    shed_voice();
    check("released voices shed the quieter tail first", extra->stage == 4u && lead->stage != 4u);
}
static void dma(void)
{
    uint32_t i, expected;
    fresh();
    host_pending = 0;
    fm1_alnk0_irq();
    check("an auxiliary interrupt does not render a DMA half", !host_acks && !audio_halves && !skm_dbg.in_audio);
    host_pending = FM1_AUDIO_HALF;
    host_us = 5000;
    fm1_alnk0_irq();
    check("one slow half records elapsed load but does not shed yet", host_acks == 1u && !shed_req && skm_dbg.last_us == 5000u && !skm_dbg.late);
    fm1_alnk0_irq();
    check("a second slow half in a row requests shedding", host_acks == 2u && shed_req);
    shed_req = 0;
    host_half_reads = 0; host_dma_advanced = 1; host_us = 100;
    fm1_alnk0_irq();
    check("DMA advancing during a render is counted once", host_acks == 3u && skm_dbg.late == 1u && !skm_dbg.in_audio && !shed_req);
    fresh();
    expected = HALF_FRAMES * 1000000u / FS;            /* the half's deadline in us */
    host_us = expected * 90u / 100u; host_nest = expected * 30u / 100u;   /* the render 60 %, TIMER5 nested 30 % */
    fm1_alnk0_irq(); fm1_alnk0_irq();
    check("TIMER5 nested in the render counts toward the deadline", shed_req && skm_dbg.last_us == host_us - host_nest);
    fresh();
    host_us = 5000;
    fm1_alnk0_irq(); host_us = 100; fm1_alnk0_irq(); host_us = 5000; fm1_alnk0_irq();
    check("slow halves that are not consecutive do not shed", !shed_req);
    fresh();
    host_us = 580;
    for (i = 0; i < 512u; i++) fm1_alnk0_irq();
    expected = host_us * 256u / (HALF_FRAMES * 1000000u / FS);
    check("steady CPU load converges without integer smoothing bias", song.cpu_q8 >= expected - 1u && song.cpu_q8 <= expected);
}
/* MENU > USB LEVEL (fx.c fx_usb_fixed, audio.c audio_block): MASTER (default) as before, USB = the DAC's signal and
 * follows the knob; FIXED: USB at the full level whatever MASTER is (MASTER 0: USB still plays, the DAC is silent),
 * the DAC exactly USB scaled by MASTER */
static double block_rms(const int32_t *b, uint32_t n)
{
    double s = 0;
    uint32_t i;
    for (i = 0; i < 2u * n; i++) s += (double)b[i] * b[i];
    return sqrt(s / (2.0 * n));
}
static double usb_run(uint32_t fixed, uint32_t master, int dac_is_tap_scaled_ok[1], double *dac_rms)
{
    int32_t out[2 * 32];
    uint32_t b, i;
    double usb = 0, dac = 0;
    fresh();
    lim_env = LIM_T; dc_l = dc_r = dce_l = dce_r = 0;    /* (the master's state as at power-on: runs compare) */
    trk[0].p[P_VOICE] = V_POLY;
    trk[0].p[P_DIST] = trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 0;   /* (no tails between runs) */
    trk_note_on(&trk[0], 48, 110); trk_note_on(&trk[0], 55, 110); trk_note_on(&trk[0], 64, 110);
    fx_usb_fixed = (uint8_t)fixed;
    song.master_q12 = master;
    dac_is_tap_scaled_ok[0] = 1;
    for (b = 0; b < 400u; b++) {                       /* ~0.3 s */
        audio_block(out, 32);
        for (i = 0; i < 64u; i++) {
            int32_t want = fixed ? ((host_tap[i] * (int32_t)master) >> 12) << OUT_SHIFT : host_tap[i] << OUT_SHIFT;
            if (out[i] != want) dac_is_tap_scaled_ok[0] = 0;
        }
        if (b >= 100u) { usb += block_rms(host_tap, 32); dac += block_rms(out, 32) / (1 << OUT_SHIFT); }
    }
    fx_usb_fixed = 0;
    *dac_rms = dac / 300.0;
    return usb / 300.0;
}
static void usb_level(void)
{
    int ok_full, ok_quiet, ok_off, ok_fixed, ok_fixed0;
    double d_full, d_quiet, d_off, d_fixed, d_fixed0;
    double u_full = usb_run(0, 4096, &ok_full, &d_full), u_quiet = usb_run(0, 1024, &ok_quiet, &d_quiet);
    double u_off = usb_run(0, 0, &ok_off, &d_off), u_fixed = usb_run(1, 1024, &ok_fixed, &d_fixed);
    double u_fixed0 = usb_run(1, 0, &ok_fixed0, &d_fixed0);
    printf("audio: USB / DAC rms: MASTER full %.0f / %.0f, 1/4 %.0f / %.0f, 0 %.0f / %.0f; FIXED 1/4 %.0f / %.0f, 0 %.0f / %.0f\n",
           u_full, d_full, u_quiet, d_quiet, u_off, d_off, u_fixed, d_fixed, u_fixed0, d_fixed0);
    check("USB LEVEL MASTER (default): USB is the DAC's signal and follows the knob (0: silent)",
          ok_full && ok_quiet && ok_off && u_full > 1000.0 && u_quiet < u_full * 0.5 && u_off == 0.0);
    check("USB LEVEL FIXED: USB at the full level whatever MASTER is (within 0.1 dB), MASTER 0 too",
          fabs(20.0 * log10(u_fixed / u_full)) < 0.1 && fabs(20.0 * log10(u_fixed0 / u_full)) < 0.1);
    check("USB LEVEL FIXED: the DAC gets exactly USB scaled by MASTER (MASTER 0: silent, 1/4: -12 dB)",
          ok_fixed && ok_fixed0 && d_fixed0 == 0.0 && fabs(20.0 * log10(d_fixed / u_fixed) + 12.04) < 0.2);
}

/* P14: the metronome's click and the 1-bar count-in before a take */
static void metronome(void)
{
    int32_t out[2 * CTL];
    uint32_t i, a, b, z;
    int32_t peak4 = 0;
    fresh();
    beat_pos = beat_n = 0;
    song.g[G_CLOCK] = 0;
    song.g[G_BPM] = 120;
    metro_set(4, 0);
    for (i = 0; i < 2u * CTL; i++) out[i] = 0;
    metro_click(1);
    metro_out(out, CTL);
    for (z = 0, i = 0; i < 2u * CTL; i++) {
        z += out[i] != 0;
        if (out[i] > peak4) peak4 = out[i];
        else if (-out[i] > peak4) peak4 = -out[i];
    }
    check("metronome: a click reaches the master", z > 0u && peak4 > 0);
    for (i = 0; i < 2u * CTL; i++) out[i] = 0;
    metro_set(1, 0);
    metro_click(1);
    metro_out(out, CTL);
    for (a = 0, i = 0; i < 2u * CTL; i++)
        if (out[i] > (int32_t)a) a = (uint32_t)out[i];
    check("metronome: level 1 is quieter than level 4", a > 0u && (int32_t)a * 3 < peak4);
    for (i = 0; i < 2u * CTL; i++) out[i] = 0;
    metro_set(0, 1);
    metro_click(1);
    metro_out(out, CTL);
    for (b = 0, i = 0; i < 2u * CTL; i++) b += out[i] != 0;
    check("metronome: OFF is silent (the count-in is off with it)", b == 0u && !metro_countin_on());

    /* the count-in: REC arms the take, four clicks, then the sequencer starts on the bar */
    fresh();
    beat_pos = beat_n = 0;
    song.g[G_CLOCK] = 0;
    song.g[G_BPM] = 120;
    metro_set(4, 1);
    transport_req = 1;
    count_in_req = 1;
    events_block(1u);
    check("count-in: the take waits, one bar left", !song.playing && count_in == 4u);
    events_block((uint32_t)FS / 2u);
    check("count-in: a beat gone, still waiting", !song.playing && count_in == 3u);
    events_block((uint32_t)FS / 2u);
    events_block((uint32_t)FS / 2u);
    check("count-in: the last beat before the bar", !song.playing && count_in == 1u);
    events_block((uint32_t)FS / 2u);
    check("count-in: the sequencer starts on the bar", song.playing && count_in == 0u);
    transport_req = 2;
    events_block(1u);
    check("count-in: STOP during it cancels it", !song.playing && count_in == 0u && count_in_req == 0u);
    metro_set(0, 0);
    fresh();

    /* a plain start (PLAY) is not delayed */
    fresh();
    beat_pos = beat_n = 0;
    song.g[G_CLOCK] = 0;
    metro_set(4, 1);
    transport_req = 1;
    events_block(1u);
    check("count-in: PLAY still starts at once", song.playing);
    transport_req = 2;
    events_block(1u);
    metro_set(0, 0);
}

/* A voice mode changed while notes are held must not leave a gate open once the keys come up: the
 * dropdown that writes P_VOICE (the companion's POLY / MONO list) was the report ("played notes,
 * changed POLY to MONO, the note stuck"). Each pair of modes, two notes held across the change, then
 * both released: nothing of the part may still be gated, and the mono stack must be empty. */
static void vmode(void)
{
    static const char *const NAME[4] = {"POLY", "MONO", "LEG", "UNI"};
    int32_t out[2 * CTL];
    uint32_t from, to, i, k, stuck;
    char what[96];
    for (from = V_POLY; from <= V_UNISON; from++)
        for (to = V_POLY; to <= V_UNISON; to++) {
            fresh();
            trk[0].p[P_VOICE] = (int16_t)from;
            trk_note_on(&trk[0], 60, 100);
            trk_note_on(&trk[0], 64, 100);
            mix_block(out, CTL);
            trk[0].p[P_VOICE] = (int16_t)to;            /* the list's write, mid-note */
            mix_block(out, CTL);
            trk_note_off(&trk[0], 60);
            trk_note_off(&trk[0], 64);
            mix_block(out, CTL);
            stuck = 0;
            for (k = 0; k < VOICE_POOL; k++)
                stuck += vpool[k].part == 0u && vpool[k].active && vpool[k].gate;
            snprintf(what, sizeof what, "%s -> %s: the notes let go release every voice",
                     NAME[from], NAME[to]);
            check(what, stuck == 0u && trk[0].mono_note == 0u && trk[0].nmono == 0u);
        }
    /* the same, with a key pressed after the change (it wins the mono voice) and then let go */
    for (from = V_POLY; from <= V_UNISON; from++)
        for (to = V_MONO; to <= V_LEGATO; to++) {
            fresh();
            trk[0].p[P_VOICE] = (int16_t)from;
            trk_note_on(&trk[0], 60, 100);
            trk[0].p[P_VOICE] = (int16_t)to;
            trk_note_on(&trk[0], 67, 100);              /* a note of the new mode */
            mix_block(out, CTL);
            trk_note_off(&trk[0], 67);
            mix_block(out, CTL);
            trk_note_off(&trk[0], 60);                  /* the one held across the change */
            mix_block(out, CTL);
            stuck = 0;
            for (i = 0; i < VOICE_POOL; i++)
                stuck += vpool[i].part == 0u && vpool[i].active && vpool[i].gate;
            snprintf(what, sizeof what, "%s -> %s: a key played meanwhile leaves nothing gated",
                     NAME[from], NAME[to]);
            check(what, stuck == 0u);
        }
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    usb_level();
    overload(); dma();
    metronome();
    vmode();
    printf(bad ? "audio: %d FAILED\n" : "audio: all passed\n", bad);
    return bad != 0;
}
