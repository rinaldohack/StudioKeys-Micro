#include "StudioKEYS.h"
#include "IPlug_include_in_plug_src.h"
#include "SkPanelControls.h"

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
    .WithLabelText(IText(12.f, skmint::kText, "Roboto-Regular"))
    .WithValueText(IText(12.f, skmint::kText, "Roboto-Regular"));
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

  const IRECT chrome = b.GetFromTop(36.f);
  pGraphics->AttachControl(new ITextControl(chrome.GetFromLeft(220.f),
    "StudioKEYS Micro", IText(20.f, skmint::kTheme, "Roboto-Regular", EAlign::Near)));
  pGraphics->AttachControl(new IVToggleControl(chrome.GetFromLeft(380.f).GetFromRight(150.f).GetMidVPadded(14.f),
    kParamUnleashed, "", mintStyle, "Limited", "Unleashed"));
  pGraphics->AttachControl(new ITextControl(chrome.GetFromRight(560.f).GetFromLeft(300.f),
    "", IText(11.f, skmint::kText, "Roboto-Regular", EAlign::Near)), kCtrlTagStatus);
  pGraphics->AttachControl(new ITextControl(chrome.GetFromRight(250.f).GetFromLeft(220.f),
    "", IText(11.f, skmint::kAccent, "Roboto-Regular", EAlign::Near)), kCtrlTagPolyWarn);
  pGraphics->AttachControl(new IVButtonControl(chrome.GetFromRight(100.f).GetMidVPadded(12.f),
    [this](IControl*) {
      mLinkCable = true;
      mSurf.SetKeyboardConnected(true);
      mLock.OnPlugDetected();
      ShowSyncDialog(mLock.State() == SK_LOCK_ASK);
      UpdateStatusText();
      UpdateSurfaceStatus("Link cable up.");
    }, "Link", mintStyle));

  const IRECT deskBand = b.GetReducedFromTop(42.f).GetFromTop(b.H() * 0.32f);
  pGraphics->AttachControl(new IPanelControl(deskBand, skmint::kSurf));
  pGraphics->AttachControl(new IVMenuButtonControl(deskBand.GetFromTop(28.f).GetFromLeft(160.f).GetPadded(-6.f),
    kParamDesk, "Desk", mintStyle));
  pGraphics->AttachControl(new ITextControl(deskBand.GetFromTop(28.f).GetReducedFromLeft(170.f).GetPadded(-6.f),
    "ANALOG", IText(16.f, skmint::kTheme, "Roboto-Regular", EAlign::Near)), kCtrlTagDeskTitle);
  mLastDeskBuilt = -1;
  RebuildDesk(pGraphics);

  const IRECT libBand = b.GetReducedFromTop(42.f + deskBand.H() + 4.f).GetFromTop(88.f);
  pGraphics->AttachControl(new IPanelControl(libBand, skmint::kSurf));
  pGraphics->AttachControl(new ITextControl(libBand.GetFromTop(18.f).GetPadded(-8.f).GetReducedFromRight(100.f),
    "Librarian · Hardware-ready / VST-only · per-patch glide/glmode/mono",
    IText(12.f, skmint::kText, "Roboto-Regular", EAlign::Near)));
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
    pGraphics->AttachControl(new ITextControl(libBand.GetReducedFromTop(20.f).GetFromTop(20.f).GetPadded(-8.f),
      libText.Get(), IText(11.f, skmint::kText, "Roboto-Regular", EAlign::Near)), kCtrlTagLibrarian);
  }
  pGraphics->AttachControl(new IVButtonControl(libBand.GetFromTop(22.f).GetFromRight(96.f).GetPadded(-4.f),
    [this](IControl*) {
      const LibPatch* sel = nullptr;
      if (!mLib.Hw().empty())
        sel = &mLib.Hw()[0];
      else if (!mLib.Vst().empty())
        sel = &mLib.Vst()[0];
      if (!sel)
        return;
      Librarian::LoadIntoHost(*sel);
      GetParam(kParamGlide)->Set(sel->glide);
      GetParam(kParamGlMode)->Set(sel->glmode);
      if (sel->blob.size() >= sknor::kPCount * sizeof(int16_t))
      {
        int16_t mapped[sknor::kPCount];
        std::memcpy(mapped, sel->blob.data(), sizeof mapped);
        GetParam(kParamAttack)->Set(mapped[skdesk::kP_ATK]);
        GetParam(kParamDecay)->Set(mapped[skdesk::kP_DEC]);
        GetParam(kParamSustain)->Set(mapped[skdesk::kP_SUS]);
        GetParam(kParamRelease)->Set(mapped[skdesk::kP_REL]);
        for (int i = 0; i < 8; ++i)
          GetParam(kParamE0 + i)->Set(mapped[skdesk::kP_E0 + i]);
      }
      const int desk = skdesk::FindEngine(sel->engine.c_str());
      if (desk >= 0)
      {
        GetParam(kParamDesk)->Set(desk);
        mDeskIndex = desk;
        if (GetUI())
          RebuildDesk(GetUI());
      }
      if (GetUI())
      {
        WDL_String t;
        t.SetFormatted(96, "Loaded \"%s\" %s g=%d/%d m=%d",
          sel->name.c_str(), sel->engine.c_str(), sel->glide, sel->glmode, sel->mono);
        if (auto* c = GetUI()->GetControlWithTag(kCtrlTagLibrarian))
          c->As<ITextControl>()->SetStr(t.Get());
        GetUI()->SetAllControlsDirty();
      }
    }, "Load", mintStyle));

  pGraphics->AttachControl(new IVButtonControl(libBand.GetFromBottom(28.f).GetFromLeft(120.f).GetPadded(-4.f),
    [this](IControl*) {
      std::vector<uint8_t> dump(6 + 128 * 32 + 2, 0);
      dump[0] = 0xF0; dump[1] = 0x43; dump[2] = 0x00; dump[3] = 0x09; dump[4] = 0x20; dump[5] = 0x00;
      for (int v = 0; v < 32; ++v)
        for (int c = 0; c < 10; ++c)
          dump[6 + v * 128 + 118 + c] = (uint8_t) "INIT VOICE"[c];
      dump.back() = 0xF7;
      mBay.Clear();
      const int n = mBay.LoadDx7Bulk(dump.data(), dump.size());
      if (GetUI())
      {
        WDL_String t;
        t.SetFormatted(64, "Bay: %d DX7 rows", n);
        if (auto* c = GetUI()->GetControlWithTag(kCtrlTagImportBay))
          c->As<ITextControl>()->SetStr(t.Get());
      }
    }, "DX7 demo", mintStyle));

  pGraphics->AttachControl(new IVButtonControl(libBand.GetFromBottom(28.f).GetFromLeft(240.f).GetFromLeft(110.f).GetPadded(-4.f),
    [this](IControl*) {
      /* Classic Access single: pages A+B after 10 bb ss (see ImportBay map). */
      uint8_t virus[9 + 256 + 2];
      memset(virus, 0, sizeof virus);
      virus[0] = 0xF0; virus[1] = 0x00; virus[2] = 0x20; virus[3] = 0x33;
      virus[4] = 0x01; virus[5] = 0x00; virus[6] = 0x10; virus[7] = 0x00; virus[8] = 0x40;
      uint8_t* page = virus + 9;
      page[5] = 32;    /* A5 portamento → glide */
      page[17] = 64;   /* A17 osc1 shape (saw) */
      page[40] = 100;  /* A40 cutoff */
      page[42] = 40;   /* A42 resonance */
      page[94] = 1;    /* A94 key mode mono */
      memcpy(page + 128 + 112, "VIRUS DEMO", 10); /* B112 name */
      unsigned sum = virus[5] + 0x10 + virus[7] + virus[8];
      for (int i = 0; i < 256; ++i)
        sum += page[i];
      virus[9 + 256] = (uint8_t) (sum & 0x7F);
      virus[9 + 256 + 1] = 0xF7;
      mBay.Clear();
      const int n = mBay.LoadVirusTi(virus, sizeof virus);
      if (GetUI())
      {
        WDL_String t;
        t.SetFormatted(64, "Bay: %d Virus → ANALOG", n);
        if (auto* c = GetUI()->GetControlWithTag(kCtrlTagImportBay))
          c->As<ITextControl>()->SetStr(t.Get());
      }
    }, "Virus demo", mintStyle));

  pGraphics->AttachControl(new IVButtonControl(libBand.GetFromBottom(28.f).GetFromLeft(360.f).GetFromLeft(130.f).GetPadded(-4.f),
    [this](IControl*) {
      if (mBay.Rows().empty())
        return;
      auto p = mBay.StoreRow(0, LibKind::VstOnly);
      mLib.Add(p);
      if (GetUI())
      {
        WDL_String t;
        t.SetFormatted(96, "Stored \"%s\" [%s]", p.name.c_str(), LibKindName(p.kind));
        if (auto* c = GetUI()->GetControlWithTag(kCtrlTagLibrarian))
          c->As<ITextControl>()->SetStr(t.Get());
      }
    }, "Bay → Lib", mintStyle));

  /* Live / HUI surface strip */
  pGraphics->AttachControl(new IVButtonControl(libBand.GetFromBottom(28.f).GetFromLeft(500.f).GetFromLeft(70.f).GetPadded(-4.f),
    [this](IControl*) {
      mSurf.PulseHold(sksurface::Mode::Live);
      UpdateSurfaceStatus(mSurf.KeyboardConnected()
        ? "Holding > → Ableton Live (link → keyboard Live port)"
        : "Keyboard not connected — Live/HUI leave on the keyboard's ports.");
    }, "Live", mintStyle));
  pGraphics->AttachControl(new IVButtonControl(libBand.GetFromBottom(28.f).GetFromLeft(575.f).GetFromLeft(70.f).GetPadded(-4.f),
    [this](IControl*) {
      mSurf.PulseHold(sksurface::Mode::Hui);
      UpdateSurfaceStatus(mSurf.KeyboardConnected()
        ? "Holding Fader Bank → Mackie HUI"
        : "Keyboard not connected — Live/HUI leave on the keyboard's ports.");
    }, "HUI", mintStyle));
  pGraphics->AttachControl(new IVButtonControl(libBand.GetFromBottom(28.f).GetFromLeft(650.f).GetFromLeft(80.f).GetPadded(-4.f),
    [this](IControl*) {
      std::string refuse;
      if (!mSurf.SendLiveNote(36, 100, mMidi, refuse))
        UpdateSurfaceStatus(refuse.c_str());
      else
        UpdateSurfaceStatus("Clip pad → link cable (keyboard Live port).");
    }, "Pad1", mintStyle));
  pGraphics->AttachControl(new ITextControl(libBand.GetFromBottom(28.f).GetReducedFromLeft(740.f).GetPadded(-4.f),
    "Surface: synth", IText(11.f, skmint::kText, "Roboto-Regular", EAlign::Near)), kCtrlTagSurfaceStatus);

  pGraphics->AttachControl(new ITextControl(libBand.GetFromBottom(28.f).GetFromRight(200.f).GetPadded(-4.f),
    "Import bay", IText(11.f, skmint::kText, "Roboto-Regular", EAlign::Near)), kCtrlTagImportBay);

  const float panelTop = 42.f + deskBand.H() + 4.f + libBand.H() + 4.f;
  const IRECT panel = b.GetReducedFromTop(panelTop).GetFromTop(b.H() - panelTop - 96.f);
  pGraphics->AttachControl(new IPanelControl(panel, skmint::kSurf));

  const IRECT screenCol = panel.GetFromLeft(panel.W() * 0.22f).GetPadded(-6.f);
  const IRECT screenR = screenCol.GetFromTop(screenCol.W()).GetCentredInside(screenCol.W() - 4.f);
  pGraphics->AttachControl(new SkScreenControl(screenR), kCtrlTagScreen);

  auto onBit = [this](uint32_t bit, bool down) {
    if (down)
      mBtnMask |= (1u << bit);
    else
      mBtnMask &= ~(1u << bit);
    PushButtonMask();
  };

  const IRECT btn6 = screenCol.GetFromBottom(52.f);
  static const char* kUnderScreen[] = {"GLOBE", "SCALE", "SEQ", "PARA", "ARP", "FX"};
  static const uint32_t kUnderScreenBits[] = {kBtnGLO, kBtnSCL, kBtnSEQ, kBtnEDIT, kBtnARP, kBtnFX};
  for (int i = 0; i < 6; ++i)
  {
    const IRECT cell = btn6.GetGridCell(i, 1, 6).GetPadded(-2.f);
    pGraphics->AttachControl(new SkHoldButton(cell.GetCentredInside(std::max(44.f, cell.W()), 44.f),
      kUnderScreenBits[i], kUnderScreen[i], onBit));
  }

  const IRECT mid = panel.GetReducedFromLeft(panel.W() * 0.22f).GetFromLeft(panel.W() * 0.48f).GetPadded(-6.f);
  static const char* kKnobNames[] = {"SELECT", "ALGO", "PRESET", "K1", "K2", "K3", "K4", "MST"};
  static const uint32_t kKnobRoles[] = {kEncSelect, kEncAlgo, kEncPreset, kEncK1, kEncK2, kEncK3, kEncK4, 0xFFu};
  const IRECT knobsRow = mid.GetFromTop(mid.H() * 0.48f);
  for (int i = 0; i < 8; ++i)
  {
    const IRECT cell = knobsRow.GetGridCell(i, 1, 8).GetCentredInside(56.f);
    if (kKnobRoles[i] == 0xFFu)
      pGraphics->AttachControl(new SkFaderControl(cell, 1, kKnobNames[i]));
    else
      pGraphics->AttachControl(new SkEncKnob(cell, kKnobRoles[i], kKnobNames[i]));
  }

  const IRECT padsRow = mid.GetFromBottom(mid.H() * 0.48f);
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
  for (int i = 0; i < 8; ++i)
  {
    const IRECT cell = padsRow.GetGridCell(i, 1, 8).GetPadded(-3.f);
    const float s = std::max(44.f, std::min(cell.W(), cell.H()));
    pGraphics->AttachControl(new SkPadControl(cell.GetCentredInside(s), i, onPad));
  }

  const IRECT right = panel.GetFromRight(panel.W() * 0.30f).GetPadded(-6.f);
  const IRECT faders = right.GetFromTop(right.H() * 0.55f);
  for (int i = 0; i < 4; ++i)
  {
    char lab[8];
    snprintf(lab, sizeof lab, "F%d", kFaderHwIndex[i]);
    pGraphics->AttachControl(new SkFaderControl(faders.GetGridCell(i, 1, 4).GetPadded(-4.f), kFaderHwIndex[i], lab));
  }

  /* Side keys: LEFT / RIGHT (Live) / F.BANK (HUI) / K.BANK via sk_host_smk_extra */
  const IRECT btn12 = right.GetFromBottom(right.H() * 0.42f);
  static const char* kUnderFaders[] = {
    "ENV", "LFO", "HOME", "PATCH", "PLAY", "REC",
    "OCT-", "OCT+", "K.BANK", "F.BANK", "LEFT", "RIGHT"
  };
  static const uint32_t kUnderFaderBits[] = {
    kBtnENV, kBtnLFO, kBtnHOME, kBtnSAVE, kBtnPLAY, kBtnREC,
    kBtnOCTDN, kBtnOCTUP, 0xFFu, 0xFEu, 0xFDu, 0xFCu
  };
  for (int i = 0; i < 12; ++i)
  {
    const IRECT cell = btn12.GetGridCell(i, 2, 6).GetPadded(-2.f);
    const uint32_t bit = kUnderFaderBits[i];
    if (bit >= 0xFCu)
    {
      const uint32_t xbit = bit == 0xFCu ? SK_X_RIGHT
                          : bit == 0xFDu ? SK_X_LEFT
                          : bit == 0xFEu ? SK_X_FADER
                                         : SK_X_CHORD;
      pGraphics->AttachControl(new SkHoldButton(cell.GetCentredInside(cell.W(), std::max(44.f, cell.H())),
        xbit, kUnderFaders[i], [this](uint32_t x, bool down) {
          static uint32_t sMask = 0;
          if (down)
            sMask |= (1u << x);
          else
            sMask &= ~(1u << x);
          sk_host_smk_extra(sMask);
          if (down && x == SK_X_RIGHT)
            UpdateSurfaceStatus(mSurf.KeyboardConnected()
              ? "Hold RIGHT for Live…" : "Keyboard not connected — Live/HUI leave on the keyboard's ports.");
          if (down && x == SK_X_FADER)
            UpdateSurfaceStatus(mSurf.KeyboardConnected()
              ? "Hold F.BANK for HUI…" : "Keyboard not connected — Live/HUI leave on the keyboard's ports.");
        }));
    }
    else
    {
      pGraphics->AttachControl(new SkHoldButton(cell.GetCentredInside(cell.W(), std::max(44.f, cell.H())),
        bit, kUnderFaders[i], onBit));
    }
  }

  const IRECT sync = b.GetCentredInside(420.f, 160.f);
  pGraphics->AttachControl(new IPanelControl(sync, skmint::kBg), kCtrlTagSyncPanel)->Hide(true);
  pGraphics->AttachControl(new ITextControl(sync.GetFromTop(40.f).GetPadded(-12.f),
    "Sync keyboard?", IText(16.f, skmint::kTheme, "Roboto-Regular")), kCtrlTagSyncDlg)->Hide(true);
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

  IRECT keyboardBounds = b.GetFromBottom(90.f);
  IRECT wheelsBounds = keyboardBounds.ReduceFromLeft(90.f).GetPadded(-4.f);
  pGraphics->AttachControl(new IVKeyboardControl(keyboardBounds, 21, 108), kCtrlTagKeyboard);
  pGraphics->AttachControl(new IWheelControl(wheelsBounds.FracRectHorizontal(0.5)), kCtrlTagBender);
  pGraphics->AttachControl(new IWheelControl(wheelsBounds.FracRectHorizontal(0.5, true),
    IMidiMsg::EControlChangeMsg::kModWheel), kCtrlTagModWheel);

  pGraphics->SetQwertyMidiKeyHandlerFunc([pGraphics](const IMidiMsg& msg) {
    dynamic_cast<IVKeyboardControl*>(pGraphics->GetControlWithTag(kCtrlTagKeyboard))->SetNoteFromMidi(
      msg.NoteNumber(), msg.StatusMsg() == IMidiMsg::kNoteOn);
  });

  UpdateStatusText();
}

void StudioKEYS::RebuildDesk(IGraphics* pGraphics)
{
  IGraphics* g = pGraphics ? pGraphics : GetUI();
  if (!g)
    return;

  const int desk = GetParam(kParamDesk)->Int();
  if (auto* t = g->GetControlWithTag(kCtrlTagDeskTitle))
    t->As<ITextControl>()->SetStr(desk >= skdesk::kNumEngines ? "SETTINGS" : skdesk::kEngines[desk].name);

  /* Hide previous desk knobs (tagged) and rebuild when desk changes. */
  for (int tag : mDeskCtrlTags)
  {
    if (auto* c = g->GetControlWithTag(tag))
      c->Hide(true);
  }
  if (mLastDeskBuilt == desk && !mDeskCtrlTags.empty())
  {
    for (int tag : mDeskCtrlTags)
      if (auto* c = g->GetControlWithTag(tag))
        c->Hide(false);
    return;
  }
  mDeskCtrlTags.clear();
  mLastDeskBuilt = desk;

  const IVStyle mintStyle = MintStyle();
  const IRECT b = g->GetBounds().GetPadded(-10.f);
  const IRECT deskBand = b.GetReducedFromTop(42.f).GetFromTop(b.H() * 0.32f);
  const IRECT body = deskBand.GetReducedFromTop(32.f).GetPadded(-8.f);
  const bool settings = desk >= skdesk::kNumEngines;
  const skdesk::EngineDesk* eng = settings ? nullptr : &skdesk::kEngines[std::clamp(desk, 0, skdesk::kNumEngines - 1)];
  int tag = kCtrlTagDeskKnob0;

  auto attach = [&](IControl* c) {
    g->AttachControl(c, tag);
    mDeskCtrlTags.push_back(tag++);
  };

  if (settings)
  {
    for (int i = 0; i < skdesk::kNumSettingsRows; ++i)
    {
      const IRECT cell = body.GetGridCell(i, 2, 4).GetPadded(-4.f);
      attach(new ITextControl(cell.GetFromTop(16.f), skdesk::kSettingsRows[i],
        IText(11.f, skmint::kText, "Roboto-Regular")));
      attach(new IVKnobControl(cell.GetReducedFromTop(16.f).GetCentredInside(64.f),
        i == 0 ? kParamColor : kParamGain, skdesk::kSettingsRows[i], mintStyle));
    }
  }
  else if (eng && std::strcmp(eng->name, "SAMPLE") == 0)
  {
    attach(new ITextControl(body.GetFromTop(18.f), "SAMPLE — live waveform / start / end / audition",
      IText(12.f, skmint::kTheme, "Roboto-Regular", EAlign::Near)));
    const IRECT wave = body.GetReducedFromTop(24.f).GetFromTop(48.f).GetPadded(-4.f);
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
    attach(new IVSliderControl(body.GetFromBottom(70.f).GetGridCell(0, 1, 4).GetMidHPadded(16.f),
      kParamE0, "Start", mintStyle));
    attach(new IVSliderControl(body.GetFromBottom(70.f).GetGridCell(1, 1, 4).GetMidHPadded(16.f),
      kParamE1, "End", mintStyle));
    attach(new IVButtonControl(body.GetFromBottom(70.f).GetGridCell(2, 1, 4).GetPadded(-8.f),
      [](IControl*) { sk_host_midi(0x90, 60, 100); },
      "Audition", mintStyle));
    for (int i = 2; i < eng->nParams; ++i)
    {
      const IRECT cell = body.GetReducedFromTop(22.f).GetGridCell(i, 2, 4).GetPadded(-4.f);
      attach(new IVKnobControl(cell.GetCentredInside(60.f), kParamE0 + i, eng->params[i], mintStyle));
    }
  }
  else if (eng)
  {
    WDL_String title;
    title.SetFormatted(64, "%s — edit knobs → P_E0..P_E7", eng->name);
    attach(new ITextControl(body.GetFromTop(18.f), title.Get(),
      IText(12.f, skmint::kTheme, "Roboto-Regular", EAlign::Near)));
    for (int i = 0; i < eng->nParams; ++i)
    {
      const IRECT cell = body.GetReducedFromTop(22.f).GetGridCell(i, 2, 4).GetPadded(-4.f);
      attach(new IVKnobControl(cell.GetCentredInside(64.f), kParamE0 + i, eng->params[i], mintStyle));
    }
    /* ADSR strip */
    attach(new IVKnobControl(body.GetFromBottom(56.f).GetGridCell(0, 1, 4).GetCentredInside(52.f),
      kParamAttack, "ATK", mintStyle));
    attach(new IVKnobControl(body.GetFromBottom(56.f).GetGridCell(1, 1, 4).GetCentredInside(52.f),
      kParamDecay, "DEC", mintStyle));
    attach(new IVKnobControl(body.GetFromBottom(56.f).GetGridCell(2, 1, 4).GetCentredInside(52.f),
      kParamSustain, "SUS", mintStyle));
    attach(new IVKnobControl(body.GetFromBottom(56.f).GetGridCell(3, 1, 4).GetCentredInside(52.f),
      kParamRelease, "REL", mintStyle));
  }

  g->AttachControl(new IVLEDMeterControl<2>(deskBand.GetFromTRHC(70, 90).GetTranslated(-8.f, 36.f), "", mintStyle), kCtrlTagMeter);
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

  std::vector<float> tmpL((size_t) nFrames), tmpR((size_t) nFrames);
  float* fouts[2] = { tmpL.data(), tmpR.data() };
  mHost.Process(fouts, nFrames);
  const float gain = (float) (GetParam(kParamGain)->Value() / 100.0);
  for (int i = 0; i < nFrames; ++i)
  {
    outputs[0][i] = (sample) (tmpL[(size_t) i] * gain);
    outputs[1][i] = (sample) (tmpR[(size_t) i] * gain);
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
    const uint32_t draws = sk_host_screen_draws();
    if (draws != mLastScreenDraws)
    {
      mLastScreenDraws = draws;
      if (auto* s = GetUI()->GetControlWithTag(kCtrlTagScreen))
        s->SetDirty(false);
    }
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
#if IPLUG_EDITOR
    if (GetUI())
      RebuildDesk(GetUI());
#endif
  }
  if (paramIdx == kParamColor)
  {
    sk_host_set_palette((uint32_t) GetParam(kParamColor)->Int());
    ApplyMintFromHost();
#if IPLUG_EDITOR
    if (GetUI())
      GetUI()->SetAllControlsDirty();
#endif
  }
  auto setAndLog = [this](uint32_t id, int32_t v) {
    sk_host_set_param(id, v);
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

#endif // IPLUG_DSP
