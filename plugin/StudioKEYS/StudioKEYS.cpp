#include "StudioKEYS.h"
#include "IPlug_include_in_plug_src.h"
#include "SkPanelControls.h"
#include "SettingsDesk.h"
#include "PanelLayout.h"
#include "LangStrings.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

static std::atomic<int> gProcessInstanceCount {0};

IVStyle MintStyle()
{
  return DEFAULT_STYLE
    .WithColor(kBG, skmint::kSurf)
    .WithColor(kFG, skmint::kTheme)
    .WithColor(kPR, skmint::kAccent)
    .WithColor(kFR, skmint::kSurf)
    .WithColor(kSH, IColor(40, 0, 0, 0))
    .WithLabelText(IText(14.f, skmint::kText, "Roboto-Regular"))
    .WithValueText(IText(14.f, skmint::kText, "Roboto-Regular"));
}

static int FwEditId(int i)
{
  static const int kIds[] = {
    skdesk::kP_E0, skdesk::kP_E1, skdesk::kP_E2, skdesk::kP_E3,
    skdesk::kP_E4, skdesk::kP_E5, skdesk::kP_E6, skdesk::kP_E7
  };
  return kIds[i & 7];
}

} // namespace

StudioKEYS::StudioKEYS(const InstanceInfo& info)
: iplug::Plugin(info, MakeConfig(kNumParams, kNumPresets))
{
  GetParam(kParamGain)->InitDouble("Gain", 80., 0., 100.0, 0.01, "%");
  GetParam(kParamUnleashed)->InitBool("Unleashed", false);
  GetParam(kParamDesk)->InitEnum("Desk", 0, skdesk::kNumEngines + 1);
  for (int i = 0; i < skdesk::kNumEngines; ++i)
    GetParam(kParamDesk)->SetDisplayText(i, skdesk::kEngines[i].name);
  GetParam(kParamDesk)->SetDisplayText(skdesk::kNumEngines, "SETTINGS");
  GetParam(kParamColor)->InitInt("COLOR", 0, 0, 18);
  GetParam(kParamGlide)->InitInt("Glide", 0, 0, 127);
  GetParam(kParamGlMode)->InitInt("GlMode", 0, 0, 3);
  GetParam(kParamAttack)->InitDouble("Attack", 10., 0., 127., 1.);
  GetParam(kParamDecay)->InitDouble("Decay", 70., 0., 127., 1.);
  GetParam(kParamSustain)->InitDouble("Sustain", 90., 0., 127., 1.);
  GetParam(kParamRelease)->InitDouble("Release", 60., 0., 127., 1.);
  static const char* kEn[] = {"E0", "E1", "E2", "E3", "E4", "E5", "E6", "E7"};
  for (int i = 0; i < 8; ++i)
    GetParam(kParamE0 + i)->InitDouble(kEn[i], 64., 0., 127., 1.);
  GetParam(kParamExtClock)->InitBool("Use external MIDI clock", true);

  mLib.BootLibraries();
  gProcessInstanceCount.fetch_add(1, std::memory_order_relaxed);

#if IPLUG_EDITOR
  mMakeGraphicsFunc = [&]() {
    return MakeGraphics(*this, PLUG_WIDTH, PLUG_HEIGHT, PLUG_FPS, GetScaleForScreen(PLUG_WIDTH, PLUG_HEIGHT));
  };
  mLayoutFunc = [&](IGraphics* pGraphics) { LayoutUI(pGraphics); };
#endif
}

StudioKEYS::~StudioKEYS()
{
  gProcessInstanceCount.fetch_sub(1, std::memory_order_relaxed);
  mLock.Release();
}

#if IPLUG_DSP

void StudioKEYS::ApplyMintFromHost()
{
  skmint::SyncFromHost();
}

void StudioKEYS::LayoutUI(IGraphics* pGraphics)
{
  mHost.Init();
  mLock.Claim();
  ApplyMintFromHost();

  pGraphics->AttachCornerResizer(EUIResizerMode::Scale, false);
  pGraphics->AttachPanelBackground(skmint::kBg);
  pGraphics->EnableMouseOver(true);
  pGraphics->LoadFont("Roboto-Regular", ROBOTO_FN);

  const IVStyle mintStyle = MintStyle();
  const IRECT b = pGraphics->GetBounds().GetPadded(-10.f);

  /* Top chrome bar (FL Studio style top strip) */
  const IRECT chrome = b.GetFromTop(sklayout::kChromeH);
  pGraphics->AttachControl(new SkBrandTitleControl(chrome.GetFromLeft(290.f)));
  pGraphics->AttachControl(new IVToggleControl(chrome.GetFromLeft(415.f).GetFromRight(110.f).GetMidVPadded(4.f),
    kParamUnleashed, "", mintStyle, "Limited", "Unleashed"));

  /* FL Studio style vertical dual-channel LED peak/VU meter */
  const IRECT vuRect = chrome.GetFromRight(126.f).GetFromLeft(22.f).GetPadded(-2.f);
  IVStyle vuStyle = mintStyle.WithColor(kFG, skmint::kTheme).WithColor(kBG, skmint::kWell);
  pGraphics->AttachControl(new IVLEDMeterControl<2>(vuRect, "", vuStyle, EDirection::Vertical), kCtrlTagMeter);

  pGraphics->AttachControl(new IVButtonControl(chrome.GetFromRight(64.f).GetMidVPadded(4.f),
    [this](IControl*) {
      mLinkCable = !mLinkCable;
      mSurf.SetKeyboardConnected(mLinkCable);
      mLock.OnPlugDetected();
      ShowSyncDialog(mLock.State() == SK_LOCK_ASK);
      UpdateStatusText();
      UpdateSurfaceStatus(mLinkCable ? "Link cable connected." : "Link cable disconnected.");
    }, "Link", mintStyle));

  pGraphics->AttachControl(new ITextControl(chrome.GetFromRight(580.f).GetFromLeft(320.f),
    "", IText(13.f, skmint::kText, "Roboto-Regular", EAlign::Near)), kCtrlTagStatus);
  pGraphics->AttachControl(new ITextControl(chrome.GetFromRight(270.f).GetFromLeft(140.f),
    "", IText(13.f, skmint::kAccent, "Roboto-Regular", EAlign::Near)), kCtrlTagPolyWarn);

  /* Desk band — hero editor and DMA debugger */
  const IRECT deskBand = b.GetReducedFromTop(sklayout::kChromeH + 4.f).GetFromTop(b.H() * sklayout::kDeskFrac);
  pGraphics->AttachControl(new SkThemedPanel(deskBand, SkThemedPanel::Style::Surface, 6.f), kCtrlTagDeskPanel);
  /* Top of desk band: Track tabs [ T1..T4 ] on left, Desk selector & Quick actions on right */
  const IRECT deskHdr = deskBand.GetFromTop(28.f).GetPadded(-3.f);
  const IRECT tabsRect = deskHdr.GetFromLeft(360.f);
  auto onTabSelect = [this](uint32_t tr) {
    sk_host_select_track(tr);
    uint32_t eng = sk_host_trk_engine(tr);
    GetParam(kParamDesk)->Set((int)eng);
    sk_host_set_desk(eng);
    sk_host_ui_pump();
    RebuildDesk(GetUI());
  };
  pGraphics->AttachControl(new TrackTabsControl(tabsRect, onTabSelect));

  pGraphics->AttachControl(new IVMenuButtonControl(deskHdr.GetReducedFromLeft(368.f).GetFromLeft(120.f).GetPadded(-2.f),
    kParamDesk, "", mintStyle));
  pGraphics->AttachControl(new ITextControl(deskHdr.GetReducedFromLeft(494.f).GetFromLeft(180.f).GetPadded(-2.f),
    "ANALOG", IText(16.f, skmint::kTheme, "Roboto-Regular", EAlign::Near)), kCtrlTagDeskTitle);

  /* Quick action buttons on desk top right: Init / Load / Save / Store */
  const IRECT actR = deskHdr.GetFromRight(310.f).GetPadded(-2.f);
  pGraphics->AttachControl(new IVButtonControl(actR.GetGridCell(0, 1, 4).GetPadded(-2.f),
    [this](IControl*) {
      for (int i = 0; i < 8; ++i)
        sk_host_set_param((uint32_t) skdesk::kP_E0 + i, 64);
      sk_host_set_param((uint32_t) skdesk::kP_ATK, 10);
      sk_host_set_param((uint32_t) skdesk::kP_DEC, 70);
      sk_host_set_param((uint32_t) skdesk::kP_SUS, 90);
      sk_host_set_param((uint32_t) skdesk::kP_REL, 60);
      sk_host_ui_pump();
      SyncDeskFromHost();
      UpdateStatusText();
    }, "Init", mintStyle));
  pGraphics->AttachControl(new IVButtonControl(actR.GetGridCell(1, 1, 4).GetPadded(-2.f),
    [this](IControl*) {
      const LibPatch* sel = nullptr;
      if (!mLib.Hw().empty())
        sel = &mLib.Hw()[0];
      else if (!mLib.Vst().empty())
        sel = &mLib.Vst()[0];
      if (sel)
      {
        Librarian::LoadIntoHost(*sel);
        SyncDeskFromHost();
      }
      UpdateStatusText();
    }, "Load", mintStyle));
  pGraphics->AttachControl(new IVButtonControl(actR.GetGridCell(2, 1, 4).GetPadded(-2.f),
    [this](IControl*) {
      if (mLock.State() == SK_LOCK_ON)
        mLock.Autosave();
      UpdateStatusText();
    }, "Save", mintStyle));
  pGraphics->AttachControl(new IVButtonControl(actR.GetGridCell(3, 1, 4).GetPadded(-2.f),
    [this](IControl*) {
      if (mLock.State() == SK_LOCK_ON)
        mLock.Autosave();
      UpdateStatusText();
    }, "Store", mintStyle));

  /* Invisible tags for backwards compatibility */
  pGraphics->AttachControl(new ITextControl(IRECT(), "", IText()), kCtrlTagLibrarian)->Hide(true);
  pGraphics->AttachControl(new ITextControl(IRECT(), "", IText()), kCtrlTagImportBay)->Hide(true);
  pGraphics->AttachControl(new ITextControl(IRECT(), "", IText()), kCtrlTagSurfaceStatus)->Hide(true);

  /* Compact hardware SMK37 faceplate below desk */
  const float panelTop = sklayout::kChromeH + 4.f + deskBand.H() + 4.f;
  const IRECT panel = b.GetReducedFromTop(panelTop).GetReducedFromBottom(sklayout::kKeybedH + 6.f);
  pGraphics->AttachControl(new SkThemedPanel(panel, SkThemedPanel::Style::DeepBlack, 6.f), kCtrlTagSmkPanel);

  const IRECT screenCol = panel.GetFromLeft(panel.W() * sklayout::kScreenColFrac).GetPadded(-4.f);
  const IRECT btnUnder = screenCol.GetFromBottom(sklayout::kUnderScreenH);
  const float screenSide = std::min(screenCol.W() - 2.f, screenCol.H() - sklayout::kUnderScreenH - 2.f);
  const IRECT screenR = screenCol.GetReducedFromBottom(sklayout::kUnderScreenH)
                          .GetCentredInside(screenSide, screenSide);
  pGraphics->AttachControl(new SkScreenControl(screenR), kCtrlTagScreen);

  auto onBit = [this](uint32_t bit, bool down) {
    if (down)
      mBtnMask |= (1u << bit);
    else
      mBtnMask &= ~(1u << bit);
    PushButtonMask();
  };
  auto onExtra = [this](uint32_t x, bool down) {
    if (down)
      mExtraMask |= (1u << x);
    else
      mExtraMask &= ~(1u << x);
    sk_host_smk_extra(mExtraMask);
    if (down && x == SK_X_RIGHT)
      UpdateSurfaceStatus(mSurf.KeyboardConnected()
        ? "Hold > for Live…" : "Keyboard not connected — Live/HUI leave on the keyboard's ports.");
    if (down && x == SK_X_FADER)
      UpdateSurfaceStatus(mSurf.KeyboardConnected()
        ? "Hold FADER for HUI…" : "Keyboard not connected — Live/HUI leave on the keyboard's ports.");
  };

  /* Under screen: 2 columns × 3 rows
   *   BANK | FADER
   *   <    | >
   *   OCT- | OCT+ */
  struct UnderScreenBtn { const char* title; uint32_t bit; bool extra; };
  static const UnderScreenBtn kUnderScreen[6] = {
    {"BANK", kBtnHOME, false},
    {"FADER", SK_X_FADER, true},
    {"<", SK_X_LEFT, true},
    {">", SK_X_RIGHT, true},
    {"OCT-", kBtnOCTDN, false},
    {"OCT+", kBtnOCTUP, false},
  };
  for (int i = 0; i < 6; ++i)
  {
    const IRECT cell = btnUnder.GetGridCell(i, 3, 2).GetPadded(-2.f);
    if (kUnderScreen[i].extra)
      pGraphics->AttachControl(new SkHoldButton(cell, kUnderScreen[i].bit,
        kUnderScreen[i].title, onExtra));
    else
      pGraphics->AttachControl(new SkHoldButton(cell, kUnderScreen[i].bit,
        kUnderScreen[i].title, onBit));
  }

  const float midLeft = panel.W() * 0.20f;
  const IRECT mid = panel.GetReducedFromLeft(midLeft)
                      .GetFromLeft(panel.W() * 0.50f).GetPadded(-4.f);
  const IRECT knobsRow = mid.GetFromTop(mid.H() * 0.48f);
  for (int i = 0; i < 8; ++i)
  {
    const IRECT cell = knobsRow.GetGridCell(i, 1, 8).GetPadded(-2.f);
    const float side = std::min({cell.W(), cell.H(), sklayout::kKnobSize + 30.f});
    pGraphics->AttachControl(new SkEncKnob(cell.GetCentredInside(side, side), (uint32_t) i),
                             kCtrlTagEnc0 + i);
  }

  const IRECT padsRow = mid.GetFromBottom(mid.H() * 0.44f);
  auto onPad = [this](int pad, bool down) {
    if (mSurf.GetMode() == sksurface::Mode::Live || mSurf.GetMode() == sksurface::Mode::Hui)
    {
      std::string refuse;
      if (!mSurf.SendLiveNote((uint8_t) (36 + pad), down ? 100 : 0, mMidi, refuse))
        UpdateSurfaceStatus(refuse.c_str());
      return;
    }
    sk_host_midi(down ? 0x99 : 0x89, (uint8_t) (36 + pad), down ? 100 : 0);
  };
  for (int i = 0; i < 16; ++i)
  {
    const IRECT cell = padsRow.GetGridCell(i / 8, i % 8, 2, 8).GetPadded(-2.f);
    pGraphics->AttachControl(new SkPadControl(cell, i, onPad));
  }

  const IRECT right = panel.GetFromRight(panel.W() * 0.30f).GetPadded(-4.f);
  const IRECT faders = right.GetFromTop(right.H() * 0.55f);
  for (int i = 0; i < 4; ++i)
  {
    pGraphics->AttachControl(
      new SkFaderControl(faders.GetGridCell(i, 1, 4).GetPadded(-4.f), i), kCtrlTagFader0 + i);
  }

  /* Side keys: 12 buttons under faders */
  const IRECT btn12 = right.GetFromBottom(right.H() * 0.42f);
  struct UnderFaderBtn { const char* title; uint32_t bit; bool extra; };
  static const UnderFaderBtn kUnderFaders[12] = {
    {"ARP", kBtnARP, false},
    {"RPT", SK_X_REPEAT, true},
    {"SCL", kBtnSCL, false},
    {"CHD", SK_X_CHORD, true},
    {"GLO", kBtnGLO, false},
    {"BT", SK_X_BT, true},
    {"SAVE", kBtnSAVE, false},
    {"EDIT", kBtnEDIT, false},
    {"FX", kBtnFX, false},
    {"SEQ", kBtnSEQ, false},
    {"PLAY", kBtnPLAY, false},
    {"REC", kBtnREC, false},
  };
  for (int i = 0; i < 12; ++i)
  {
    const IRECT cell = btn12.GetGridCell(i, 2, 6).GetPadded(-2.f);
    if (kUnderFaders[i].extra)
      pGraphics->AttachControl(new SkHoldButton(cell, kUnderFaders[i].bit,
        kUnderFaders[i].title, onExtra));
    else
      pGraphics->AttachControl(new SkHoldButton(cell, kUnderFaders[i].bit,
        kUnderFaders[i].title, onBit));
  }

  const IRECT sync = b.GetCentredInside(420.f, 160.f);
  pGraphics->AttachControl(new IPanelControl(sync, skmint::kBg), kCtrlTagSyncPanel)->Hide(true);
  pGraphics->AttachControl(new ITextControl(sync.GetFromTop(40.f).GetPadded(-12.f),
    "Sync keyboard?", IText(18.f, skmint::kTheme, "Roboto-Regular")), kCtrlTagSyncDlg)->Hide(true);
  pGraphics->AttachControl(new IVButtonControl(sync.GetFromBottom(70.f).GetGridCell(0, 1, 3).GetPadded(-6.f),
    [this](IControl*) {
      mLock.ChoosePluginToHw();
      mContinuousSave = mLock.State() == SK_LOCK_ON;
      ShowSyncDialog(false);
      UpdateStatusText();
    }, "Plugin → Keyboard", mintStyle), kCtrlTagSyncPluginToHw)->Hide(true);
  pGraphics->AttachControl(new IVButtonControl(sync.GetFromBottom(70.f).GetGridCell(1, 1, 3).GetPadded(-6.f),
    [this](IControl*) {
      mLock.ChooseHwToPlugin();
      mContinuousSave = mLock.State() == SK_LOCK_ON;
      ShowSyncDialog(false);
      UpdateStatusText();
    }, "Keyboard → Plugin", mintStyle), kCtrlTagSyncHwToPlugin)->Hide(true);
  pGraphics->AttachControl(new IVButtonControl(sync.GetFromBottom(70.f).GetGridCell(2, 1, 3).GetPadded(-6.f),
    [this](IControl*) {
      mLock.Dismiss();
      ShowSyncDialog(false);
      UpdateStatusText();
    }, "Dismiss", mintStyle), kCtrlTagSyncDismiss)->Hide(true);

  IRECT keyboardBounds = b.GetFromBottom(sklayout::kKeybedH);
  IRECT wheelsBounds = keyboardBounds.ReduceFromLeft(90.f).GetPadded(-4.f);
  pGraphics->AttachControl(new IVKeyboardControl(keyboardBounds, 21, 108), kCtrlTagKeyboard);
  pGraphics->AttachControl(new IWheelControl(wheelsBounds.FracRectHorizontal(0.5)), kCtrlTagBender);
  pGraphics->AttachControl(new IWheelControl(wheelsBounds.FracRectHorizontal(0.5, true),
    IMidiMsg::EControlChangeMsg::kModWheel), kCtrlTagModWheel);

  pGraphics->SetQwertyMidiKeyHandlerFunc([pGraphics](const IMidiMsg& msg) {
    dynamic_cast<IVKeyboardControl*>(pGraphics->GetControlWithTag(kCtrlTagKeyboard))->SetNoteFromMidi(
      msg.NoteNumber(), msg.StatusMsg() == IMidiMsg::kNoteOn);
  });

  /* —— About / UBOOT roast modal (single self-drawing overlay) —— */
  mAboutWinOpen = false;

  mLastDeskBuilt = -1;
  RebuildDesk(pGraphics);

  mLastLarge = (int) sk_host_menu_get(sksettings::kMiLarge);
  ApplyLargeScale();

  UpdateStatusText();
}

void StudioKEYS::RebuildDesk(IGraphics* pGraphics)
{
  IGraphics* g = pGraphics ? pGraphics : GetUI();
  if (!g)
    return;

  const int desk = GetParam(kParamDesk)->Int();
  sk_host_ui_t ui {};
  sk_host_ui_get(&ui);
  const bool mixerPage = (std::strcmp(ui.page_title, "MIXER") == 0 || std::strcmp(ui.page_title, "GLOBAL") == 0 || std::strcmp(ui.page_title, "SYSTEM") == 0);
  int buildKey = 0;
  if (desk >= skdesk::kNumEngines)
    buildKey = 1000;
  else if (mixerPage)
    buildKey = 2000 + (std::strcmp(ui.page_title, "MIXER") == 0 ? 0 : (std::strcmp(ui.page_title, "GLOBAL") == 0 ? 1 : 2));
  else
    buildKey = (int)(ui.engine * 32 + desk);

  if (auto* t = g->GetControlWithTag(kCtrlTagDeskTitle))
  {
    if (mixerPage)
      t->As<ITextControl>()->SetStr(ui.page_title[0] ? ui.page_title : "MIXER");
    else
      t->As<ITextControl>()->SetStr(desk >= skdesk::kNumEngines ? "SETTINGS" : skdesk::kEngines[desk].name);
  }

  /* Cleanly remove previous desk controls when desk view changes. */
  if (mLastDeskBuilt == buildKey && !mDeskCtrlTags.empty())
    return;

  for (int tag : mDeskCtrlTags)
  {
    g->RemoveControlWithTag(tag);
  }
  mDeskCtrlTags.clear();
  mLastDeskBuilt = buildKey;

  const IVStyle mintStyle = MintStyle();
  const IRECT b = g->GetBounds().GetPadded(-10.f);
  const IRECT deskBand = b.GetReducedFromTop(sklayout::kChromeH + 4.f).GetFromTop(b.H() * sklayout::kDeskFrac);
  const IRECT body = deskBand.GetReducedFromTop(32.f).GetPadded(-8.f);
  const bool settings = desk >= skdesk::kNumEngines;
  const skdesk::EngineDesk* eng = settings ? nullptr : &skdesk::kEngines[std::clamp(desk, 0, skdesk::kNumEngines - 1)];
  int tag = kCtrlTagDeskKnob0;

  auto attach = [&](IControl* c) {
    g->AttachControl(c, tag);
    mDeskCtrlTags.push_back(tag++);
  };

  auto attachParam = [&](const IRECT& r, uint32_t ei) {
    if (ei >= (uint32_t)eng->nParams)
      return;
    const char* lab = sk_host_edit_label((uint32_t) desk, (uint32_t) ei);
    if (!lab || !lab[0] || lab[0] == '-')
      lab = eng->params[ei];
    if (lab && lab[0] == '-')
      return;

    const uint32_t fwId = sk_host_pe0_id() + ei;
    struct sk_host_param_info info = {0};
    if (sk_host_get_param_info(fwId, &info) && (info.names || info.fmt == 8))
    {
      attach(new SkParamMenu(r.GetCentredInside(std::min(r.W() - 4.f, 96.f), 42.f), fwId, lab, mintStyle));
    }
    else
    {
      attach(new SkParamKnob(r.GetCentredInside(58.f), fwId, lab, mintStyle));
    }
  };

  if (settings)
  {
    attach(new ITextControl(body.GetFromTop(20.f),
      sklang::T("SETTINGS · SYSTEM & HARDWARE CONFIGURATION"),
      IText(16.f, skmint::kTheme, skmint::kFont, EAlign::Near)));
    attach(new sksettings::SettingsPanel(body.GetReducedFromTop(24.f),
      [this](uint32_t row) {
        if (row == sksettings::kMiAbout)
          ShowAbout(true);
        else if (row == sksettings::kMiUpdate)
          mPendingUboot = true;
        else if (row == sksettings::kMiExtClock)
        {
          const bool cur = GetParam(kParamExtClock)->Bool();
          GetParam(kParamExtClock)->Set(!cur);
          sk_host_set_gparam(2 /* G_CLOCK */, !cur ? 1 : 0);
        }
        else if (row == sksettings::kMiPanel)
        {
          sk_host_set_desk(skdesk::kNumEngines);
          sk_host_ui_pump();
          mBtnMask |= (1u << kBtnOCTUP);
          PushButtonMask();
          mBtnMask &= ~(1u << kBtnOCTUP);
          PushButtonMask();
        }
      }));
  }
  else
  {
    const IRECT heroBody = body.GetFromLeft(body.W() * 0.76f).GetPadded(-4.f);
    const IRECT mixerBody = body.GetFromRight(body.W() * 0.24f).GetPadded(-4.f);

    // Always attach compact 4-track mixer on the right (24% width)
    attach(new skengui::ModulePanel(mixerBody, "4-TRACK MIXER"));
    const IRECT strips = mixerBody.GetReducedFromTop(24.f).GetPadded(-2.f);
    auto onSelectTrack = [this](uint32_t tr) {
      if (!GetUI()) return;
      sk_host_select_track(tr);
      uint32_t engIdx = sk_host_trk_engine(tr);
      GetParam(kParamDesk)->Set((int)engIdx);
      sk_host_set_desk(engIdx);
      sk_host_ui_pump();
      mRebuildPending = true;
      GetUI()->SetAllControlsDirty();
    };
    for (uint32_t tr = 0; tr < 4; ++tr)
    {
      attach(new skfm6::MixStrip(strips.GetGridCell((int)tr, 1, 4).GetPadded(-2.f), tr, onSelectTrack));
    }

    // Hero Engine Editor on the left (76% width)
    if (eng && std::strcmp(eng->name, "ANALOG") == 0)
    {
      attach(new ITextControl(heroBody.GetFromTop(18.f), "ANALOG — DUAL OSC · LADDER FILTER · ADSR ENVELOPE",
        IText(15.f, skmint::kTheme, "Roboto-Regular", EAlign::Near)));
      const IRECT aArea = heroBody.GetReducedFromTop(22.f).GetPadded(-2.f);
      const uint32_t pe0 = sk_host_pe0_id();
      skanalog::EasyParams p {
        pe0 + 0, pe0 + 1, pe0 + 2, pe0 + 3, pe0 + 4, pe0 + 5, pe0 + 6, pe0 + 7,
        kParamAttack, kParamDecay, kParamSustain, kParamRelease
      };
      skanalog::AttachEasyEdit(attach, aArea, mintStyle, p);
    }
    else if (eng && std::strcmp(eng->name, "FM6") == 0)
    {
      attach(new ITextControl(heroBody.GetFromTop(18.f), "FM6 — DX7 6-OP SYNTHESIZER · ROUTING MATRIX & OPERATORS",
        IText(15.f, skmint::kTheme, "Roboto-Regular", EAlign::Near)));
      const IRECT fmArea = heroBody.GetReducedFromTop(22.f).GetPadded(-2.f);
      skfm6::AttachFm6Desk(attach, fmArea, mintStyle);
    }
else if (eng && std::strcmp(eng->name, "WHEEL") == 0)
    {
      attach(new ITextControl(heroBody.GetFromTop(18.f), "WHEEL — HAMMOND B3 TONEWHEEL ORGAN · 9 DRAWBARS · LESLIE ROTARY · PERCUSSION",
        IText(15.f, skmint::kTheme, "Roboto-Regular", EAlign::Near)));
      const IRECT wheelArea = heroBody.GetReducedFromTop(22.f).GetPadded(-2.f);
      skwheel::AttachWheelDesk(attach, wheelArea, mintStyle, kParamAttack, kParamDecay, kParamSustain, kParamRelease);
    }
    else if (eng && std::strcmp(eng->name, "SAMPLE") == 0)
    {
      attach(new ITextControl(heroBody.GetFromTop(18.f), "SAMPLE — live waveform peek & edit parameters",
        IText(15.f, skmint::kTheme, "Roboto-Regular", EAlign::Near)));
      const IRECT wave = heroBody.GetReducedFromTop(24.f).GetFromTop(44.f).GetPadded(-4.f);
      attach(new IPanelControl(wave, skmint::kBg));
      attach(new ILambdaControl(wave, [](ILambdaControl*, IGraphics& gg, IRECT& r) {
        float buf[128];
        sk_host_waveform_peek(buf, 128);
        gg.FillRect(skmint::kBg, r);
        const float mid = r.MH();
        gg.DrawLine(skmint::kTheme, r.L, mid, r.R, mid, 0, 1.f);
        for (int i = 1; i < 128; ++i)
        {
          const float x0 = r.L + (r.W() * (i - 1)) / 127.f;
          const float x1 = r.L + (r.W() * i) / 127.f;
          const float y0 = mid - buf[i - 1] * r.H() * 0.45f;
          const float y1 = mid - buf[i] * r.H() * 0.45f;
          gg.DrawLine(skmint::kAccent, x0, y0, x1, y1, 0, 1.5f);
        }
      }));
      const IRECT kArea = heroBody.GetReducedFromTop(72.f).GetReducedFromBottom(56.f);
      for (int i = 0; i < eng->nParams; ++i)
      {
        const IRECT cell = kArea.GetGridCell(i, 2, 4).GetPadded(-4.f);
        attachParam(cell, i);
      }
      /* ADSR strip with real values */
      const IRECT adsrArea = heroBody.GetFromBottom(54.f).GetPadded(-2.f);
      attach(new SkParamKnob(adsrArea.GetGridCell(0, 1, 4).GetCentredInside(54.f),
        skdesk::kP_ATK, "ATK", mintStyle));
      attach(new SkParamKnob(adsrArea.GetGridCell(1, 1, 4).GetCentredInside(54.f),
        skdesk::kP_DEC, "DEC", mintStyle));
      attach(new SkParamKnob(adsrArea.GetGridCell(2, 1, 4).GetCentredInside(54.f),
        skdesk::kP_SUS, "SUS", mintStyle));
      attach(new SkParamKnob(adsrArea.GetGridCell(3, 1, 4).GetCentredInside(54.f),
        skdesk::kP_REL, "REL", mintStyle));
    }
    else if (eng && std::strcmp(eng->name, "PHASE") == 0)
    {
      attach(new ITextControl(heroBody.GetFromTop(18.f), "PHASE — multi-waveform synthesizer with crossfeed and drone",
        IText(15.f, skmint::kTheme, "Roboto-Regular", EAlign::Near)));
      const IRECT phaseArea = heroBody.GetReducedFromTop(22.f).GetPadded(-2.f);
      const IRECT wave = phaseArea.GetReducedFromTop(36.f).GetPadded(-4.f);
      attach(new IPanelControl(wave, skmint::kBg));
      attach(new ILambdaControl(wave, [](ILambdaControl*, IGraphics& gg, IRECT& r) {
        float buf[128];
        sk_host_waveform_peek(buf, 128);
        gg.FillRect(skmint::kBg, r);
        const float mid = r.MH();
        gg.DrawLine(skmint::kTheme, r.L, mid, r.R, mid, 0, 1.f);
        for (int i = 1; i < 128; ++i)
        {
          const float x0 = r.L + (r.W() * (i - 1)) / 127.f;
          const float x1 = r.L + (r.W() * i) / 127.f;
          const float y0 = mid - buf[i - 1] * r.H() * 0.45f;
          const float y1 = mid - buf[i] * r.H() * 0.45f;
          gg.DrawLine(skmint::kAccent, x0, y0, x1, y1, 0, 1.5f);
        }
      }));
      const IRECT kArea = phaseArea.GetReducedFromBottom(56.f);
      for (int i = 0; i < eng->nParams; ++i)
      {
        const IRECT cell = kArea.GetGridCell(i, 2, 4).GetPadded(-4.f);
        attachParam(cell, i);
      }
      /* ADSR strip with real values */
      const IRECT adsrArea = phaseArea.GetFromBottom(54.f).GetPadded(-2.f);
      attach(new SkParamKnob(adsrArea.GetGridCell(0, 1, 4).GetCentredInside(54.f),
        skdesk::kP_ATK, "ATK", mintStyle));
      attach(new SkParamKnob(adsrArea.GetGridCell(1, 1, 4).GetCentredInside(54.f),
        skdesk::kP_DEC, "DEC", mintStyle));
      attach(new SkParamKnob(adsrArea.GetGridCell(2, 1, 4).GetCentredInside(54.f),
        skdesk::kP_SUS, "SUS", mintStyle));
      attach(new SkParamKnob(adsrArea.GetGridCell(3, 1, 4).GetCentredInside(54.f),
        skdesk::kP_REL, "REL", mintStyle));
    }
   else if (eng)
    {
      WDL_String title;
      title.SetFormatted(64, "%s — edit parameters", eng->name);
      attach(new ITextControl(heroBody.GetFromTop(18.f), title.Get(),
        IText(15.f, skmint::kTheme, "Roboto-Regular", EAlign::Near)));
      const IRECT kArea = heroBody.GetReducedFromTop(24.f).GetReducedFromBottom(56.f);
      for (int i = 0; i < eng->nParams; ++i)
      {
        const IRECT cell = kArea.GetGridCell(i, 2, 4).GetPadded(-4.f);
        attachParam(cell, i);
      }
      /* ADSR strip with real values */
      const IRECT adsrArea = heroBody.GetFromBottom(54.f).GetPadded(-2.f);
      attach(new SkParamKnob(adsrArea.GetGridCell(0, 1, 4).GetCentredInside(54.f),
        skdesk::kP_ATK, "ATK", mintStyle));
      attach(new SkParamKnob(adsrArea.GetGridCell(1, 1, 4).GetCentredInside(54.f),
        skdesk::kP_DEC, "DEC", mintStyle));
      attach(new SkParamKnob(adsrArea.GetGridCell(2, 1, 4).GetCentredInside(54.f),
        skdesk::kP_SUS, "SUS", mintStyle));
      attach(new SkParamKnob(adsrArea.GetGridCell(3, 1, 4).GetCentredInside(54.f),
        skdesk::kP_REL, "REL", mintStyle));
    }
  }

  /* Desk attaches after the modal — raise overlay so About/UBOOT sit on top. */
  RaiseModalOverlay(g);
  if (auto* m = Modal(); m && m->IsOpen())
    SetDeskHiddenForModal(true);
}

void StudioKEYS::ShowSyncDialog(bool show)
{
  IGraphics* g = GetUI();
  if (!g)
    return;
  for (int tag : {kCtrlTagSyncPanel, kCtrlTagSyncDlg, kCtrlTagSyncPluginToHw,
                  kCtrlTagSyncHwToPlugin, kCtrlTagSyncDismiss})
  {
    if (auto* c = g->GetControlWithTag(tag))
      c->Hide(!show);
  }
}

void StudioKEYS::UpdateSurfaceStatus(const char* msg)
{
  IGraphics* g = GetUI();
  if (!g || !msg)
    return;
  if (auto* c = g->GetControlWithTag(kCtrlTagSurfaceStatus))
    c->As<ITextControl>()->SetStr(msg);
}

void StudioKEYS::UpdateStatusText()
{
  IGraphics* g = GetUI();
  if (!g)
    return;
  const int lock = mLock.State();
  const char* lockStr = lock == SK_LOCK_ON ? "Locked" : (lock == SK_LOCK_ASK ? "Sync?" : "Unlocked");
  WDL_String s;
  if (sk_host_unleashed())
  {
    const uint32_t bud = sk_host_voice_budget();
    const uint32_t ceil = sk_host_voice_ceiling();
    if (bud >= ceil)
      s.SetFormatted(256, "%s · Unleashed · %s · link %s · save %s · %s",
        sk_host_version(), lockStr, mLinkCable ? "cable" : "—",
        mContinuousSave ? "●" : "○", mLock.Message().c_str());
    else
      s.SetFormatted(256, "%s · Unleashed (CPU→%u) · %s · link %s · save %s · %s",
        sk_host_version(), bud, lockStr, mLinkCable ? "cable" : "—",
        mContinuousSave ? "●" : "○", mLock.Message().c_str());
  }
  else
  {
    s.SetFormatted(256, "%s · Limited 8 · %s · link %s · save %s · %s",
      sk_host_version(), lockStr, mLinkCable ? "cable" : "—",
      mContinuousSave ? "●" : "○", mLock.Message().c_str());
  }
  if (auto* c = g->GetControlWithTag(kCtrlTagStatus))
    c->As<ITextControl>()->SetStr(s.Get());
  if (auto* w = g->GetControlWithTag(kCtrlTagPolyWarn))
    w->As<ITextControl>()->SetStr(sk_host_unleashed() ? "Polyphony won’t match the keyboard" : "");
}

void StudioKEYS::PushButtonMask()
{
  sk_host_buttons(mBtnMask);
}

void StudioKEYS::ProcessBlock(sample** inputs, sample** outputs, int nFrames)
{
  (void) inputs;
  mHost.Init();
  mSurf.Tick();
  DrainCableQueues();

  if (GetParam(kParamExtClock)->Bool())
  {
    // Only update tempo if not locked to prevent hangs
    if (mLock.State() != SK_LOCK_ON)
    {
      double dawBpm = GetTempo();
      if (dawBpm >= 40.0 && dawBpm <= 240.0)
      {
        int targetBpm = (int)std::lround(dawBpm);
        if (sk_host_get_gparam(0 /* G_BPM */) != targetBpm)
        {
          sk_host_set_gparam(0 /* G_BPM */, targetBpm);
        }
      }
      if (sk_host_get_gparam(2 /* G_CLOCK */) != 1)
      {
        sk_host_set_gparam(2 /* G_CLOCK */, 1);
      }
    }
  }
  else
  {
    if (sk_host_get_gparam(2 /* G_CLOCK */) != 0)
    {
      sk_host_set_gparam(2 /* G_CLOCK */, 0);
    }
  }

  if (mProcessBufL.size() < (size_t)nFrames) {
    mProcessBufL.resize((size_t)nFrames);
    mProcessBufR.resize((size_t)nFrames);
  }
  float* fouts[2] = { mProcessBufL.data(), mProcessBufR.data() };
  mHost.Process(fouts, nFrames);
  const float gain = (float) (GetParam(kParamGain)->Value() / 100.0);
  for (int i = 0; i < nFrames; ++i)
  {
    outputs[0][i] = (sample) (mProcessBufL[(size_t) i] * gain);
    outputs[1][i] = (sample) (mProcessBufR[(size_t) i] * gain);
  }

  mMeterSender.ProcessBlock(outputs, nFrames, kCtrlTagMeter);
  if (mLock.State() == SK_LOCK_ON)
  {
    mContinuousSave = true;
    mLock.Autosave();
  }
}

void StudioKEYS::OnIdle()
{
  mMeterSender.TransmitData(*this);
#if IPLUG_EDITOR
  if (GetUI())
  {
    /* Keep palette in sync with host/firmware (SETTINGS menu or CFW change). */
    const int curPalette = (int) sk_host_palette();
    if (curPalette != mLastPalette)
    {
      mLastPalette = curPalette;
      GetParam(kParamColor)->Set(curPalette);
      ApplyMintFromHost();
      GetUI()->SetAllControlsDirty();
    }

    SyncDeskFromHost();

    sk_host_ui_t ui {};
    sk_host_ui_get(&ui);
    const int currentDesk = GetParam(kParamDesk)->Int();
    const uint32_t selTr = sk_host_selected_track();
    int buildKey = 0;
    if (currentDesk >= skdesk::kNumEngines)
      buildKey = 1000;
    else
      buildKey = (int)(selTr * 64 + currentDesk);

    if (mRebuildPending && GetUI())
    {
      mRebuildPending = false;
      RebuildDesk(GetUI());
    }
    if (buildKey != mLastDeskBuilt)
    {
      RebuildDesk(GetUI());
    }

    const uint32_t draws = sk_host_screen_draws();
    if (draws != mLastScreenDraws)
    {
      mLastScreenDraws = draws;
      if (auto* s = GetUI()->GetControlWithTag(kCtrlTagScreen))
        s->SetDirty(false);
    }

    if (mPendingUboot)
    {
      mPendingUboot = false;
      const bool hw = sk_host_device_present() != 0;
      sk_host_menu_uboot();
      ShowUbootModal(hw);
    }
    if (sk_host_uboot_hw_take())
      SendDeviceUboot();

    /* MENU > LARGE → scale the whole companion window. */
    {
      const int large = (int) sk_host_menu_get(sksettings::kMiLarge);
      if (large != mLastLarge)
      {
        mLastLarge = large;
        ApplyLargeScale();
      }
    }

    /* Keep About modal in sync with firmware ABOUT (OCT- on panel closes both). */
    {
      auto* m = Modal();
      const bool fwAbout = sk_host_about_is_open() != 0;
      if (fwAbout && !mAboutWinOpen)
        ShowAbout(true);
      else if (!fwAbout && mAboutWinOpen && m && m->IsAbout())
      {
        m->Hide(true);
        mAboutWinOpen = false;
        SetDeskHiddenForModal(false);
      }
      if (m)
        m->OnGUIIdle();
    }

    RefreshPanelScribbles();
    UpdateStatusText();
  }
#endif
}

void StudioKEYS::OnReset()
{
  mHost.Init();
  mHost.SetSampleRate(GetSampleRate());
  mLock.Claim();
  mMeterSender.Reset(GetSampleRate());
  mMidi.ClearAll();
  sk_host_set_gparam(14 /* G_ROUTE */, 0);
}

void StudioKEYS::ProcessMidiMsg(const IMidiMsg& msg)
{
  TRACE;
  const uint8_t status = (uint8_t) msg.mStatus;
  const uint8_t d1 = (uint8_t) msg.mData1;
  const uint8_t d2 = (uint8_t) msg.mData2;
  mMidi.Enqueue(skmidi::Cable::Performance, status, d1, d2);
  mHost.Midi(status, d1, d2);

  switch (msg.StatusMsg())
  {
    case IMidiMsg::kNoteOn:
    case IMidiMsg::kNoteOff:
    case IMidiMsg::kPolyAftertouch:
    case IMidiMsg::kControlChange:
    case IMidiMsg::kProgramChange:
    case IMidiMsg::kChannelAftertouch:
    case IMidiMsg::kPitchWheel:
      SendMidiMsg(msg);
      break;
    default:
      break;
  }

#if IPLUG_EDITOR
  if (GetUI())
  {
    auto* kb = dynamic_cast<IVKeyboardControl*>(GetUI()->GetControlWithTag(kCtrlTagKeyboard));
    if (kb && (msg.StatusMsg() == IMidiMsg::kNoteOn || msg.StatusMsg() == IMidiMsg::kNoteOff))
    {
      if (msg.Channel() == 0 || msg.Channel() == (int)sk_host_selected_track() + 1)
      {
        kb->SetNoteFromMidi(msg.NoteNumber(), msg.StatusMsg() == IMidiMsg::kNoteOn);
      }
    }
  }
#endif
}

void StudioKEYS::DrainCableQueues()
{
  /* Surface / DAW fragments on the link queue — forward as raw bytes if they
   * look like SysEx starts; SKL frames are pushed by LockSession directly. */
  uint8_t status, d1, d2, cable;
  while (mMidi.Queue(skmidi::Cable::Link).Pop(status, d1, d2, cable))
  {
    (void) cable;
    if (status == 0xF0)
    {
      uint8_t buf[3] = {status, d1, d2};
      sk_host_link_push(buf, 3);
    }
  }
  mLock.Drain();
}

void StudioKEYS::OnParamChange(int paramIdx)
{
  if (mPullingHost)
    return;
  if (paramIdx == kParamUnleashed)
  {
    sk_host_set_unleashed(GetParam(kParamUnleashed)->Bool() ? 1 : 0);
#if IPLUG_EDITOR
    UpdateStatusText();
#endif
  }
  if (paramIdx == kParamDesk)
  {
    mDeskIndex = GetParam(kParamDesk)->Int();
    sk_host_set_desk((uint32_t) mDeskIndex);
    sk_host_ui_pump();
#if IPLUG_EDITOR
    if (GetUI())
      RebuildDesk(GetUI());
#endif
  }
  if (paramIdx == kParamColor)
  {
    sk_host_set_palette((uint32_t) GetParam(kParamColor)->Int());
    sk_host_ui_pump();
    ApplyMintFromHost();
    mLastPalette = GetParam(kParamColor)->Int();
    if (mLock.State() == SK_LOCK_ON)
      mLock.Autosave();
#if IPLUG_EDITOR
    if (GetUI())
    {
      GetUI()->SetAllControlsDirty();
    }
#endif
  }
  auto setAndLog = [this](uint32_t id, int32_t v) {
    if (mLock.State() == SK_LOCK_ON && id == skdesk::kP_ATK) return; // skip if locked to prevent hang
    sk_host_set_param(id, v);
    sk_host_ui_pump();
    if (mLock.State() == SK_LOCK_ON)
      mLock.LogParam(id, v);
  };
  if (paramIdx == kParamGlide)
    setAndLog(skdesk::kP_GLIDE, GetParam(kParamGlide)->Int());
  if (paramIdx == kParamGlMode)
    setAndLog(skdesk::kP_GLMODE, GetParam(kParamGlMode)->Int());
  if (paramIdx == kParamAttack)
    setAndLog(skdesk::kP_ATK, (int32_t) GetParam(kParamAttack)->Value());
  if (paramIdx == kParamDecay)
    setAndLog(skdesk::kP_DEC, (int32_t) GetParam(kParamDecay)->Value());
  if (paramIdx == kParamSustain)
    setAndLog(skdesk::kP_SUS, (int32_t) GetParam(kParamSustain)->Value());
  if (paramIdx == kParamRelease)
    setAndLog(skdesk::kP_REL, (int32_t) GetParam(kParamRelease)->Value());
  if (paramIdx >= kParamE0 && paramIdx <= kParamE7)
    setAndLog((uint32_t) FwEditId(paramIdx - kParamE0),
               (int32_t) GetParam(paramIdx)->Value());
}

bool StudioKEYS::OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData)
{
  (void) msgTag;
  (void) ctrlTag;
  (void) dataSize;
  (void) pData;
  return false;
}

bool StudioKEYS::SerializeState(IByteChunk& chunk) const
{
  const uint8_t* nor = Librarian::Nor();
  const uint32_t n = Librarian::NorSize();
  chunk.Put(&n);
  if (nor && n)
    chunk.PutBytes(nor, (int) n);
  return true;
}

int StudioKEYS::UnserializeState(const IByteChunk& chunk, int startPos)
{
  uint32_t n = 0;
  startPos = chunk.Get(&n, startPos);
  if (n && n <= Librarian::NorSize())
  {
    std::vector<uint8_t> tmp(n);
    startPos = chunk.GetBytes(tmp.data(), (int) n, startPos);
    Librarian::NorWrite(0, tmp.data(), n);
  }
  mLib.BootLibraries();
#if IPLUG_EDITOR
  if (GetUI())
  {
    WDL_String libText;
    auto append = [&](const std::vector<LibPatch>& v) {
      for (size_t i = 0; i < v.size() && i < 4; ++i)
      {
        const auto& p = v[i];
        libText.AppendFormatted(128, "%s [%s] %s g=%d/%d m=%d  ",
          p.name.c_str(), LibKindName(p.kind), p.engine.c_str(), p.glide, p.glmode, p.mono);
      }
    };
    append(mLib.Hw());
    append(mLib.Vst());
    if (libText.GetLength() == 0)
      libText.Set("(empty)");
    if (auto* c = GetUI()->GetControlWithTag(kCtrlTagLibrarian))
      c->As<ITextControl>()->SetStr(libText.Get());
  }
#endif
  return startPos;
}

#if IPLUG_EDITOR
sksettings::ModalOverlay* StudioKEYS::Modal()
{
  IGraphics* g = GetUI();
  if (!g)
    return nullptr;
  if (auto* c = g->GetControlWithTag(kCtrlTagAboutPanel))
    return static_cast<sksettings::ModalOverlay*>(c);
  return nullptr;
}

void StudioKEYS::SetDeskHiddenForModal(bool hide)
{
  IGraphics* g = GetUI();
  if (!g)
    return;
  for (int tag : mDeskCtrlTags)
  {
    if (auto* c = g->GetControlWithTag(tag))
      c->Hide(hide);
  }
}

void StudioKEYS::RaiseModalOverlay(IGraphics* pGraphics)
{
  IGraphics* g = pGraphics ? pGraphics : GetUI();
  if (!g)
    return;

  bool open = false, about = true, hw = false;
  uint32_t autoMs = 0, openedAt = 0;
  if (auto* c = g->GetControlWithTag(kCtrlTagAboutPanel))
  {
    auto* m = static_cast<sksettings::ModalOverlay*>(c);
    open = m->IsOpen();
    about = m->IsAbout();
    hw = m->IsUbootHw();
    autoMs = m->AutoCloseMs();
    openedAt = m->OpenedAt();
    g->RemoveControlWithTag(kCtrlTagAboutPanel);
  }

  auto* neu = new sksettings::ModalOverlay(g->GetBounds(), [this]() {
    mAboutWinOpen = false;
    SetDeskHiddenForModal(false);
  });
  g->AttachControl(neu, kCtrlTagAboutPanel);
  if (open)
    neu->Restore(about, hw, autoMs, openedAt, true);
}

void StudioKEYS::ShowAbout(bool show)
{
  RaiseModalOverlay();
  auto* m = Modal();
  if (!m)
    return;
  mAboutWinOpen = show;
  if (show)
  {
    m->SetAbout();
    m->Open();
    SetDeskHiddenForModal(true);
  }
  else
  {
    m->Close();
    SetDeskHiddenForModal(false);
  }
  if (IGraphics* g = GetUI())
    g->SetAllControlsDirty();
}

void StudioKEYS::ShowUbootModal(bool hwMode)
{
  RaiseModalOverlay();
  auto* m = Modal();
  if (!m)
    return;
  mAboutWinOpen = true;
  m->SetUbootRoast(hwMode);
  m->Open();
  SetDeskHiddenForModal(true);
  if (IGraphics* g = GetUI())
    g->SetAllControlsDirty();
}

void StudioKEYS::ApplyLargeScale()
{
  IGraphics* g = GetUI();
  if (!g)
    return;
  const bool large = sk_host_menu_get(sksettings::kMiLarge) != 0;
  const float scale = large ? 1.25f : 1.0f;
  if (std::fabs(g->GetDrawScale() - scale) < 0.01f)
    return;
  g->Resize(sklayout::kPlugW, sklayout::kPlugH, scale);
}

void StudioKEYS::SendDeviceUboot()
{
  static const uint8_t kUbootKey[6] = {0xF0, 0x22, 0x24, 0x35, 0x7D, 0xF7};
  if (mUsb.IsOpen())
    mUsb.Send(kUbootKey, sizeof kUbootKey);
  UpdateSurfaceStatus("UBOOT SysEx sent to keyboard");
}

void StudioKEYS::PullHostParamToUI(int paramIdx, double raw)
{
  IParam* p = GetParam(paramIdx);
  if (!p)
    return;
  if (std::abs(p->Value() - raw) > 1e-4)
  {
    mPullingHost = true;
    p->Set(raw);
    mPullingHost = false;
    SendParameterValueFromDelegate(paramIdx, raw, false);
  }
}

void StudioKEYS::SyncDeskFromHost()
{
  sk_host_ui_t ui {};
  sk_host_ui_get(&ui);

  int targetDesk = mDeskIndex;
  if (ui.desk == 0xFFu || ui.menu != 0)
    targetDesk = skdesk::kNumEngines; /* 13 = SETTINGS */
  else if (ui.desk < skdesk::kNumEngines)
    targetDesk = (int) ui.desk;

  if (targetDesk != mDeskIndex)
  {
    mDeskIndex = targetDesk;
    mPullingHost = true;
    GetParam(kParamDesk)->Set(mDeskIndex);
    mPullingHost = false;
    if (GetUI())
      RebuildDesk(GetUI());
  }

  /* Sync parameters to UI controls if not captured by mouse */
  PullHostParamToUI(kParamGlide, sk_host_get_param(skdesk::kP_GLIDE));
  PullHostParamToUI(kParamGlMode, sk_host_get_param(skdesk::kP_GLMODE));
  PullHostParamToUI(kParamAttack, sk_host_get_param(skdesk::kP_ATK));
  PullHostParamToUI(kParamDecay, sk_host_get_param(skdesk::kP_DEC));
  PullHostParamToUI(kParamSustain, sk_host_get_param(skdesk::kP_SUS));
  PullHostParamToUI(kParamRelease, sk_host_get_param(skdesk::kP_REL));
  for (int i = 0; i < 8; ++i)
    PullHostParamToUI(kParamE0 + i, sk_host_get_param((uint32_t) FwEditId(i)));
}

void StudioKEYS::RefreshPanelScribbles()
{
  IGraphics* g = GetUI();
  if (!g)
    return;
  for (int i = 0; i < 8; ++i)
  {
    if (auto* c = g->GetControlWithTag(kCtrlTagEnc0 + i))
    {
      if (auto* k = dynamic_cast<SkEncKnob*>(c))
      {
        if (k->RefreshFromHost())
          c->SetDirty(false);
      }
    }
  }
  for (int i = 0; i < 4; ++i)
  {
    if (auto* c = g->GetControlWithTag(kCtrlTagFader0 + i))
    {
      if (auto* f = dynamic_cast<SkFaderControl*>(c))
      {
        if (f->RefreshFromHost())
          c->SetDirty(false);
      }
    }
  }
  skfm6::PulseEgGraphs();
}
#endif

#endif // IPLUG_DSP
