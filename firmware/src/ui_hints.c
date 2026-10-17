/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) — StudioKEYS changes and the VST3 companion */
/* P6c: the 18 printed panel buttons and what each one does *now*.
 *
 * The printed name is only the top line of a button: its function follows the page, the layer,
 * the menu or a dialog, exactly the conditions ui_input.c's handlers test. ui_hints() derives
 * both lines from that same state (the family tables, open_family's rule, the EF page walk, the
 * footer's action words), so the companion's scribble strips (host sk_host_button_hints, the
 * VST's SkHoldButton) show the words the CFW itself would draw; there is no second table to
 * retype.
 *
 * Included by studiokeys.c and host/studiokeys_host.c after ui_draw.c / ui_input.c / ui_layer.c. */
#ifndef SKM_UI_HINTS_C
#define SKM_UI_HINTS_C

typedef struct {
    const char *act;                 /* a tap; "" = the button does nothing here */
    const char *hold;                /* a long press (0 / "" = none) */
} ui_hint_t;

/* the page a tap on a family button opens (open_family's rule: same family again -> the next page) */
static uint32_t hint_family_page(uint32_t fam)
{
    uint32_t i, n;
    if (!ui.home && cur_page()->fam == fam) {
        for (n = 1; n <= NPAGES; n++) {
            i = (ui.page + n) % NPAGES;
            if (PAGES[i].fam == fam && page_visible(i))
                return i;
        }
        return ui.page;
    }
    if (fam == FAM_SAVE) {                           /* SAVE enters the sound save screen */
        for (i = 0; i < NPAGES; i++)
            if (PAGES[i].graph == GR_USER)
                return i;
    }
    i = ui.fam_last[fam];
    return i && i < NPAGES && PAGES[i].fam == fam && page_visible(i) ? i : page_first(fam);
}

/* GLO's own walk (open_global): MIXER -> GLOBAL -> MIXER, and from any other family the MIXER */
static uint32_t hint_global_page(void)
{
    uint32_t i;
    if (!ui.home && cur_page()->fam == FAM_TRK)
        return page_first(FAM_GLO);
    if (!ui.home && cur_page()->fam == FAM_GLO) {
        for (i = ui.page + 1u; i < NPAGES && PAGES[i].fam != FAM_GLO; i++)
            ;
        return i < NPAGES ? i : page_first(FAM_TRK);
    }
    return page_first(FAM_TRK);
}

/* PARA's tap: EDIT's pages, then ENV's and LFO's, wrapping back (page_tap's EF walk) */
static uint32_t hint_edit_page(void)
{
    static const uint8_t EF[3] = {FAM_EDIT, FAM_ENV, FAM_LFO};
    uint32_t cur = ui.home ? FAM_HOME : cur_page()->fam, f, n, cnt = 0, at = NPAGES, i, first = NPAGES;
    uint8_t walk[NPAGES];
    for (f = 0; f < 3u; f++)
        for (n = 0; n < NPAGES; n++)
            if (PAGES[n].fam == EF[f] && page_visible(n)) {
                if (first >= NPAGES)
                    first = n;
                if (EF[f] == cur && n == ui.page)
                    at = cnt;
                walk[cnt++] = (uint8_t)n;
            }
    if (!cnt)
        return ui.page;
    if (at >= cnt)                                   /* another page: the last page it left, as open_family */
        return ui.fam_last[FAM_EDIT] && PAGES[ui.fam_last[FAM_EDIT]].fam == FAM_EDIT ? ui.fam_last[FAM_EDIT] : first;
    return walk[(at + 1u) % cnt];
}

static void ui_hints(ui_hint_t h[NB])
{
    uint32_t i;
    int menu = ui.menu && !ui.confirm;
    int dlg = ui.confirm != 0;
    int name = !ui.confirm && name_on();
    int acts = !menu && !dlg && !name && act_cols();
    int home = ui.home != 0;

    for (i = 0; i < NB; i++) {
        h[i].act = "";
        h[i].hold = "";
    }
    if (menu || dlg || name) {                       /* the screen owns the buttons */
        h[B_HOME].act = "CLOSE";
        h[B_HOME].hold = "CLOSE";
        if (dlg) {
            h[B_OCTDN].act = "CANCEL";
            h[B_OCTUP].act = "YES";
        } else if (name) {
            h[B_OCTDN].act = "CANCEL";
            h[B_OCTUP].act = "SAVE";
            h[B_PLAY].act = song.playing || chain_busy() ? "STOP" : "";
        } else {
            h[B_OCTDN].act = "BACK";
            h[B_OCTUP].act = ui.menu_sel >= MI_VALUES ? "OPEN" : "NEXT";
        }
        return;
    }

    /* the four family buttons and the two layer buttons */
    h[B_EDIT].act = PAGES[hint_edit_page()].title;
    h[B_EDIT].hold = "SET";
    h[B_FX].act = PAGES[hint_family_page(FAM_FX)].title;
    h[B_FX].hold = "HOLD";
    h[B_SCL].act = PAGES[hint_family_page(FAM_SCL)].title;
    h[B_SCL].hold = "SET";
    h[B_GLO].act = PAGES[hint_global_page()].title;
    h[B_GLO].hold = "SET";
    h[B_ARP].act = PAGES[hint_family_page(FAM_ARP)].title;
    h[B_SEQ].act = PAGES[hint_family_page(FAM_SEQ)].title;
    h[B_SEQ].hold = "SONG";
    h[B_SAVE].act = "SAVE";
    h[B_SAVE].hold = !ui.home && cur_page()->fam == FAM_SAVE ? "UNDO" : "PERFORM";
    h[B_HOME].act = home ? (ui.lock ? "HOME" : smk_kbank ? "SOUND" : "MORE") : "HOME";
    h[B_HOME].hold = "SETTINGS";

    /* transport */
    h[B_PLAY].act = song.playing || chain_busy() ? "STOP"
                    : (!home && cur_page()->graph == GR_SONG) ? "PLAY SONG" : "PLAY";
    h[B_REC].act = song.rec ? "STOP" : "REC";
    h[B_REC].hold = rec_hold_page() ? "CLEAR" : "";

    /* the six extra keys */
    h[B_LEFT].act = "TRACK-";
    h[B_RIGHT].act = "TRACK+";
    h[B_RIGHT].hold = smk_remote ? "SYNTH" : "LIVE";
    h[B_FADER].act = "FADERS";
    h[B_FADER].hold = smk_remote ? "SYNTH" : "HUI";
    h[B_CHRD].act = "CHORD+";
    h[B_REPEAT].act = TSEL->p[P_AMODE] != 6 ? "REPEAT ON" : "REPEAT OFF";
    h[B_BT].act = "BLUETOOTH";
    h[B_BT].hold = smk_ctrl ? "SYNTH" : "CTRL";

    /* OCT- / OCT+ are the octave shift unless a layer holds them */
    h[B_OCTDN].act = "OCT-";
    h[B_OCTUP].act = "OCT+";
    if (layer_set_open()) {
        h[B_OCTDN].act = "UNDO";
        h[B_OCTUP].act = "DONE";
    }
    if (ui.lock) {                                   /* a locked layer: its button is DONE */
        uint32_t b = LAYERS[ui.lock % LAYER_N].btn;
        h[b].act = "DONE";
        h[b].hold = "DONE";
    }
    if (acts) {                                      /* an action page: OCT+ / OCT- are the footer's words */
        char fa[16], fb[16];
        foot_hint(fa, fb);                           /* "OCT+ LOAD", "OCT- BACK" */
        h[B_OCTUP].act = fa + 5;
        h[B_OCTDN].act = fb + 5;
    }
}
#endif
