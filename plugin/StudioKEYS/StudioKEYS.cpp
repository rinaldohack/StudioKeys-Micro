#include "StudioKEYS.h"
#include "IPlug_include_in_plug_src.h"
#include "SkPanelControls.h"
#include "SettingsDesk.h"
#include "PanelLayout.h"
#include "LangStrings.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#if defined(__APPLE__) || defined(__linux__)
#include <execinfo.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#endif

// #region agent log — shared across TUs (debug-2f1224)
std::atomic<int> gUiStage{-1};
// #endregion

namespace {

/* #region agent log — NDJSON to debug-2f1224 (session 2f1224) */
static void sk_plog(const char* hyp, const char* loc, const char* msg, long a, long b, long c)
{
  sk_host_dbg(hyp, loc, msg, a, b, c);
}
/* Hang watchdog: samples audio/ui liveness + the render mutex from its own
 * thread and logs the block when one thread stalls. */
static std::atomic<long long> gLastAudioUs{0};
static std::atomic<long long> gLastUiUs{0};
#if defined(__APPLE__) || defined(__linux__)
static pthread_t gMainThread;
#endif
static std::atomic<bool> gWatchdogStarted{false};
static std::once_flag gWatchdogOnce;
static long long sk_now_us()
{
  return std::chrono::duration_cast<std::chrono::microseconds>(
           std::chrono::steady_clock::now().time_since_epoch()).count();
}
static void sk_watchdog()
{
  long long rep = 0;
  while (true)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    const long long now = sk_now_us();
    const long long ad = now - gLastAudioUs.load();
    const long long ud = now - gLastUiUs.load();
    if (ad > 300000) /* audio callback silent > 300 ms */
    {
      const int mtxFree = sk_host_audio_mtx_trylock();
      if (mtxFree)
        sk_host_audio_mtx_unlock();
      sk_plog("G", "watchdog", "audio_stall", (long)(ad / 1000), (long)mtxFree, rep++);
      gLastAudioUs.store(now); /* avoid spamming; re-arm */
    }
    if (ud > 1500000) /* UI thread did not tick for > 1.5 s */
    {
      sk_plog("H", "watchdog", "ui_stall", (long)(ud / 1000), (long)gUiStage.load(), (long)(ad / 1000));
#if defined(__APPLE__) || defined(__linux__)
      static int armed = 0;
      if (!armed) { armed = 1; pthread_kill(gMainThread, SIGUSR1); }
#endif
      gLastUiUs.store(now);
    }
  }
}
static void sk_watchdog_start()
{
  std::call_once(gWatchdogOnce, []() {
    gLastAudioUs.store(sk_now_us());
    gLastUiUs.store(sk_now_us());
    gWatchdogStarted.store(true);
    std::thread(sk_watchdog).detach();
  });
}

/* Signal handler (runs on the MAIN thread when the watchdog signals a UI stall):
 * dump the main thread's native stack to the log so we can see exactly where it
 * is blocked. async-signal-safe: backtrace_symbols_fd writes directly. */
static int gSigFd = -1;
static void sk_sig_dump(int)
{
  if (gSigFd >= 0)
  {
    static const char tag[] = "{\"sessionId\":\"2f1224\",\"hypothesisId\":\"M\",\"location\":\"sigsegv\",\"message\":\"main_thread_backtrace\",\"data\":{}}\n";
    (void)!write(gSigFd, tag, sizeof(tag) - 1);
    void* bt[64];
    const int n = backtrace(bt, 64);
    backtrace_symbols_fd(bt, n, gSigFd);
  }
}
static void sk_arm_backtrace_dump(const char* path)
{
  gSigFd = open(path, O_WRONLY | O_APPEND | O_CREAT, 0644);
  if (gSigFd < 0)
    return;
  struct sigaction sa;
  std::memset(&sa, 0, sizeof sa);
  sa.sa_handler = sk_sig_dump;
  sa.sa_flags = SA_RESTART;
  sigaction(SIGUSR1, &sa, nullptr);
}
/* Non-signal-context backtrace dump: used when we detect the OCT bit push so we
 * can see exactly how a desk knob interaction ends up poking the octave. */
static void sk_dump_bt(const char* why)
{
  if (gSigFd < 0)
    return;
  char hdr[128];
  const int hn = snprintf(hdr, sizeof hdr, "\n=== BT DUMP %s ===\n", why ? why : "");
  if (hn > 0)
    (void)!write(gSigFd, hdr, (size_t) hn);
  void* bt[64];
  const int n = backtrace(bt, 64);
  backtrace_symbols_fd(bt, n, gSigFd);
}
/* #endregion */

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

/* PATCH button for the shared per-patch strip. It shows the selected track's
 * current sound and opens the PATCH BROWSER window, which browses the keyboard's
 * own preset bank (the board's PRESETS / USER pages): every engine's factory
 * presets in ENGINE_ORDER, then the NOR user slots, with the CFW FAV star and
 * LIST ALL|FAV filter. Selecting a factory preset of another engine swaps the
 * desk. The adjacent LIB / BAY buttons open their windows. */
class PatchSelector final : public IControl
{
public:
  using ActionFn = std::function<void()>;

  PatchSelector(const IRECT& bounds, ActionFn onOpen)
  : IControl(bounds)
  , mOnOpen(std::move(onOpen))
  {
  }

  void Draw(IGraphics& g) override
  {
    g.FillRoundRect(skmint::kWell, mRECT, 4.f);
    g.DrawRoundRect(skmint::kTheme, mRECT, 4.f, 0, 1.25f);
    g.DrawText(IText(13.f, skmint::kInk, skmint::kFont, EAlign::Near), "PATCH", mRECT.GetFromTop(18.f).GetPadded(-3.f));

    const char* name = sk_host_trk_name(sk_host_selected_track());
    const IRECT valueRect = mRECT.GetFromBottom(22.f).GetPadded(-3.f);
    g.DrawText(IText(14.f, skmint::kAccent, skmint::kFont, EAlign::Near),
      name && name[0] ? name : "Browse…", valueRect.GetReducedFromRight(14.f));
    const float cx = mRECT.R - 9.f;
    const float cy = valueRect.MH();
    g.FillTriangle(skmint::kTheme, cx - 3.f, cy - 2.f, cx + 3.f, cy - 2.f, cx, cy + 3.f);
  }

  void OnMouseDown(float, float, const IMouseMod&) override
  {
    if (mOnOpen)
      mOnOpen();
  }

private:
  ActionFn mOnOpen;
};

static int FwEditId(int i)
{
  static const int kIds[] = {
    skdesk::kP_E0, skdesk::kP_E1, skdesk::kP_E2, skdesk::kP_E3,
    skdesk::kP_E4, skdesk::kP_E5, skdesk::kP_E6, skdesk::kP_E7
  };
  return kIds[i & 7];
}

/* Remove one control by tag WITHOUT dangling pointers.
 *
 * IGraphics::RemoveControlWithTag() is NOT safe for dynamically rebuilt UI:
 * it calls WDL_PtrList<IControl>::DeletePtr() directly and never touches the
 * graphics bookkeeping that still references the freed object —
 *   mMouseOver / mMouseOverIdx, mCapturedMap, mInPopupMenu, mInTextEntry.
 * Because LayoutUI() calls EnableMouseOver(true), mousemoves keep mMouseOver
 * pointing at whatever is under the cursor. Tearing down desk controls (the
 * mixer strips are desk controls) while the pointer sits on one, then moving
 * the mouse, makes IGraphics::OnMouseOver() call mMouseOver->OnMouseOut() on
 * freed memory → SIGSEGV at vtable +0x38 (the "select mixer channel" crash).
 *
 * IGraphics::RemoveControl(IControl*) does the full cleanup (ReleaseMouseCapture
 * + ClearMouseOver + popup/text-entry reset + tag erase), so always remove by
 * pointer. We look the control up by tag first so callers can keep using tags.
 * Calling ClearMouseOver/ReleaseMouseCapture directly is not an option: both are
 * private/insufficient (ReleaseMouseCapture leaves mMouseOver dangling). */
static void SafeRemoveControlByTag(IGraphics* g, int tag)
{
  if (!g)
    return;
  if (IControl* c = g->GetControlWithTag(tag))
    g->RemoveControl(c);
}

} // namespace

StudioKEYS::StudioKEYS(const InstanceInfo& info)
: iplug::Plugin(info, MakeConfig(kNumParams, kNumPresets))
{
  sk_watchdog_start();
  // #region agent log
#if defined(__APPLE__) || defined(__linux__)
  gMainThread = pthread_self();
  sk_arm_backtrace_dump("/Users/rinaldojonathan/Documents/smk37/.cursor/debug-2f1224.log");
#endif
  // #endregion
  GetParam(kParamGain)->InitDouble("Gain", 80., 0., 100.0, 0.01, "%");
  GetParam(kParamUnleashed)->InitBool("Unleashed", false);
  GetParam(kParamDesk)->InitEnum("Desk", 0, skdesk::kNumEngines + 1);
  for (int i = 0; i < skdesk::kNumEngines; ++i)
    GetParam(kParamDesk)->SetDisplayText(i, skdesk::kEngines[i].name);
  GetParam(kParamDesk)->SetDisplayText(skdesk::kNumEngines, "SETTINGS");
  GetParam(kParamColor)->InitInt("COLOR", 0, 0, 18);
  GetParam(kParamGlide)->InitInt("Glide", 0, 0, 127);
  GetParam(kParamGlMode)->InitInt("GlMode", 0, 0, 3);
  GetParam(kParamVoice)->InitEnum("Voice", 0, 4); /* 0 POLY, 1 MONO, 2 LEG, 3 UNI (P_VOICE) */
  GetParam(kParamAttack)->InitDouble("Attack", 10., 0., 127., 1.);
  GetParam(kParamDecay)->InitDouble("Decay", 70., 0., 127., 1.);
  GetParam(kParamSustain)->InitDouble("Sustain", 90., 0., 127., 1.);
  GetParam(kParamRelease)->InitDouble("Release", 60., 0., 127., 1.);
  static const char* kEn[] = {"E0", "E1", "E2", "E3", "E4", "E5", "E6", "E7"};
  for (int i = 0; i < 8; ++i)
    GetParam(kParamE0 + i)->InitDouble(kEn[i], 64., 0., 127., 1.);
  GetParam(kParamExtClock)->InitBool("Use external MIDI clock", true);

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

  /* Top chrome bar (FL Studio style top strip). Kept minimal: the wordmark, a bare horizontal
   * VU meter, the live status line and the LINK button. The MASTER fader and the
   * Unleashed/Limited toggle live on the SETTINGS card, their natural home. The old top-right
   * chip row (status + polyphony warning overlapping each other) is gone: each element now owns
   * a fixed slice of the bar, laid out left to right. */
  const IRECT chrome = b.GetFromTop(sklayout::kChromeH);
  pGraphics->AttachControl(new SkBrandTitleControl(chrome.GetFromLeft(250.f)));
  /* Horizontal VU meter right beside the logo/wordmark. No dB markings: the
   * value text ("-xx dB") is suppressed (WithShowValue(false)) — the LED bar
   * alone reads the level, which is what we want. */
  IVStyle vuStyle = mintStyle.WithColor(kFG, skmint::kTheme).WithColor(kBG, skmint::kWell).WithShowValue(false);
  const IRECT vuRect = chrome.GetReducedFromLeft(258.f).GetFromLeft(200.f).GetMidVPadded(7.f);
  pGraphics->AttachControl(new IVLEDMeterControl<2>(vuRect, "", vuStyle, EDirection::Horizontal), kCtrlTagMeter);

  /* Chrome right: [ status ........ ][ poly warn ][ LINK ]. LINK is 88 px so the 14 pt label
   * fits with its padding (the old 64 px box clipped it). */
  const IRECT linkRect = chrome.GetFromRight(88.f).GetMidVPadded(4.f);
  pGraphics->AttachControl(new SkLinkButton(linkRect,
    [this]() { return mUsb.IsOpen(); },
    [this]() {
      mSurf.SetKeyboardConnected(mUsb.IsOpen());
      ShowSyncDialog(true);
      UpdateStatusText();
    }), kCtrlTagLinkBtn);

  const IRECT polyRect = chrome.GetReducedFromRight(92.f).GetFromRight(96.f).GetMidVPadded(6.f);
  pGraphics->AttachControl(new ITextControl(polyRect,
    "", IText(12.f, skmint::kAccent, "Roboto-Regular", EAlign::Far)), kCtrlTagPolyWarn);
  /* P13: SPLIT chip between the VU meter and the status line. It lights up while a split is armed
   * and opens the editor window; the keybed below mirrors the zones. */
  const IRECT splitRect(chrome.L + 466.f, chrome.T + 4.f, chrome.L + 528.f, chrome.B - 4.f);
  pGraphics->AttachControl(new sksplit::SplitButton(splitRect,
    [this]() { ShowSplitWindow(true); }), kCtrlTagSplitBtn);
  /* P16: AUTOMATION next to it — the step-motion pool and AUTO REC, for the selected track. */
  const IRECT motionRect(chrome.L + 534.f, chrome.T + 4.f, chrome.L + 596.f, chrome.B - 4.f);
  pGraphics->AttachControl(new skmotion::MotionButton(motionRect,
    [this]() { ShowMotionWindow(true); }), kCtrlTagMotionBtn);
  const IRECT statusRect(chrome.L + 604.f, chrome.T + 4.f, polyRect.L - 8.f, chrome.B - 4.f);
  pGraphics->AttachControl(new ITextControl(statusRect,
    "", IText(13.f, skmint::kText, "Roboto-Regular", EAlign::Near)), kCtrlTagStatus);

  /* Desk band — hero editor and DMA debugger */
  const IRECT deskBand = b.GetReducedFromTop(sklayout::kChromeH + 4.f).GetFromTop(b.H() * sklayout::kDeskFrac);
  pGraphics->AttachControl(new SkThemedPanel(deskBand, SkThemedPanel::Style::Surface, 6.f), kCtrlTagDeskPanel);
  /* Top of desk band: Track tabs [ T1..T4 ] on left, Desk selector & Quick actions on right */
  const IRECT deskHdr = deskBand.GetFromTop(28.f).GetPadded(-3.f);
  const IRECT tabsRect = deskHdr.GetFromLeft(360.f);
  auto onTabSelect = [this](uint32_t tr) {
    if (!GetUI())
      return;
    sk_host_select_track(tr);
    /* Use the desk RANK (ENGINE_ORDER position), not the firmware engine
     * INDEX: FM6 index 12 => rank 1, SLICE index 13 => rank 11. Feeding the raw
     * index opened the wrong card (FM6 -> DRUM) or SETTINGS (SLICE). */
    uint32_t rank = sk_host_trk_engine_rank(tr);
    GetParam(kParamDesk)->Set((int)rank);
    sk_host_set_desk(rank);
    sk_host_ui_pump();
    /* NEVER rebuild synchronously here: this runs inside an IControl::OnMouseDown,
     * while IGraphics holds this control in mCapturedMap. Tearing the desk down
     * mid-gesture is unsafe; defer the rebuild to OnIdle(), which waits for the
     * mouse gesture to end. */
    mRebuildPending.store(true);
    GetUI()->SetAllControlsDirty();
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
      /* Load the selected track's own first factory preset of its engine —
       * a real keyboard sound, not the plugin's demo library. */
      const unsigned rank = sk_host_sel_engine_rank();
      if (rank < (unsigned) skdesk::kNumEngines && sk_host_engine_factory_count(rank))
        ApplyBankPreset(rank, 0, 0);
      else
        RefreshAfterLoad();
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
  pGraphics->AttachControl(new ITextControl(IRECT(), "", IText()), kCtrlTagSurfaceStatus)->Hide(true);

  /* The single combined patch window (browser + user + imports). Created once;
   * the strip's PATCH button opens it. RaisePatchWindows re-attaches it on top
   * (iPlug2 z-order). */
  RaisePatchWindows();

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
    // #region agent log
    if (down && (bit == kBtnOCTUP || bit == kBtnOCTDN))
      sk_dump_bt(bit == kBtnOCTUP ? "onBit OCTUP down" : "onBit OCTDN down");
    // #endregion
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
    {"KNOB BANK", kBtnHOME, false},
    {"FADER BANK", SK_X_FADER, true},
    {"<", SK_X_LEFT, true},
    {">", SK_X_RIGHT, true},
    {"OCT-", kBtnOCTDN, false},
    {"OCT+", kBtnOCTUP, false},
  };
  for (int i = 0; i < 6; ++i)
  {
    const IRECT cell = btnUnder.GetGridCell(i, 3, 2).GetPadded(-2.f);
    const int tag = kCtrlTagSmkBtn0 + 12 + i;      /* P6c: labels 12..17 (KNOB BANK .. OCT+) */
    if (kUnderScreen[i].extra)
      pGraphics->AttachControl(new SkHoldButton(cell, kUnderScreen[i].bit,
        kUnderScreen[i].title, onExtra), tag);
    else
      pGraphics->AttachControl(new SkHoldButton(cell, kUnderScreen[i].bit,
        kUnderScreen[i].title, onBit), tag);
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
  /* Pads: hardware-faithful. The SMK-37 scan (hal/fm1_input.h) sends a FIXED
   * velocity note-on on the pad channel (SMK_PAD_CH, 0x93) and rides the hit
   * with CHANNEL PRESSURE (0xD3) = max pressure over the held pads; note-off on
   * 0x83. We mirror that exactly instead of the old 0x99/0x89 (channel 10,
   * which the firmware drops: channels 5..16 are ignored under ROUT CH1-4). */
  constexpr uint8_t kPadChStatus = 0x90 | 3;  /* SMK_PAD_CH = 3 (ch 4) */
  auto onPad = [this](int pad, SkPadEvent ev, int value) {
    const uint8_t note = (uint8_t) (36 + pad);
    if (mSurf.GetMode() == sksurface::Mode::Live || mSurf.GetMode() == sksurface::Mode::Hui)
    {
      const uint8_t vel = (ev == SkPadEvent::Hit) ? (uint8_t) kPadFixedVelocity : 0;
      std::string refuse;
      if (!mSurf.SendLiveNote(note, vel, mMidi, refuse))
        UpdateSurfaceStatus(refuse.c_str());
      return;
    }
    if (ev == SkPadEvent::Hit)
      sk_host_midi(kPadChStatus, note, (uint8_t) kPadFixedVelocity);
    else if (ev == SkPadEvent::Release)
      sk_host_midi(0x80 | 3, note, 0);
    else
    {
      mPadPressure[pad & 15] = value;
      int mx = 0;
      for (int p = 0; p < 16; ++p)
        mx = std::max(mx, mPadPressure[p]);
      sk_host_midi(0xD0 | 3, (uint8_t) mx, 0);   /* channel pressure, max over held */
    }
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
  /* P6b: the real SMK-37 printed names. Row 1 = ARP | NOTE REPEAT | SCALE | CHORD | GLOBAL | BT,
   * row 2 = PATCH | PARA | FX | SEQ | SEQ PLAY | SEQ REC. The printed PATCH button is the firmware's
   * B_SAVE role; PARA is B_EDIT. */
  static const UnderFaderBtn kUnderFaders[12] = {
    {"ARP", kBtnARP, false},
    {"NOTE REPEAT", SK_X_REPEAT, true},
    {"SCALE", kBtnSCL, false},
    {"CHORD", SK_X_CHORD, true},
    {"GLOBAL", kBtnGLO, false},
    {"BT", SK_X_BT, true},
    {"PATCH", kBtnSAVE, false},
    {"PARA", kBtnEDIT, false},
    {"FX", kBtnFX, false},
    {"SEQ", kBtnSEQ, false},
    {"SEQ PLAY", kBtnPLAY, false},
    {"SEQ REC", kBtnREC, false},
  };
  for (int i = 0; i < 12; ++i)
  {
    const IRECT cell = btn12.GetGridCell(i, 2, 6).GetPadded(-2.f);
    const int tag = kCtrlTagSmkBtn0 + i;           /* P6c: labels 0..11 (ARP .. SEQ REC) */
    if (kUnderFaders[i].extra)
      pGraphics->AttachControl(new SkHoldButton(cell, kUnderFaders[i].bit,
        kUnderFaders[i].title, onExtra), tag);
    else
      pGraphics->AttachControl(new SkHoldButton(cell, kUnderFaders[i].bit,
        kUnderFaders[i].title, onBit), tag);
  }

  /* P12: one self-drawing LINK window (MIDI port selector + first-sync direction), attached last
   * so it draws above the desk. The desk controls are hidden while it is open, so a desk rebuild
   * can never bury it (the old five-control dialog could sit under the rebuilt desk). */
  skoverlay::LinkWindow::Hooks linkHooks;
  linkHooks.ports = []() { return skusb::MidiLink::ListPorts(); };
  linkHooks.current = [this]() { return mUsb.PortName(); };
  linkHooks.linked = [this]() { return mUsb.IsOpen(); };
  linkHooks.connect = [this](const std::string& port) {
    if (!mUsb.OpenNamed(port))
      mSurf.SetKeyboardConnected(false);
    else
    {
      mSurf.SetKeyboardConnected(true);
      mLock.OnPlugDetected();
      mLinkCable = true;
    }
    mLinkCable = mUsb.IsOpen();
    UpdateStatusText();
    if (auto* b = GetUI() ? GetUI()->GetControlWithTag(kCtrlTagLinkBtn) : nullptr)
      b->SetDirty(false);
  };
  linkHooks.disconnect = [this]() {
    mUsb.Close();
    mLinkCable = false;
    mSurf.SetKeyboardConnected(false);
    UpdateStatusText();
    if (auto* b = GetUI() ? GetUI()->GetControlWithTag(kCtrlTagLinkBtn) : nullptr)
      b->SetDirty(false);
  };
  linkHooks.rescan = []() {};
  linkHooks.pluginToHw = [this]() {
    mLock.ChoosePluginToHw();
    mContinuousSave = mLock.State() == SK_LOCK_ON;
    UpdateStatusText();
  };
  linkHooks.hwToPlugin = [this]() {
    mLock.ChooseHwToPlugin();
    mContinuousSave = mLock.State() == SK_LOCK_ON;
    UpdateStatusText();
  };
  linkHooks.status = [this]() {
    if (mUsb.IsOpen())
    {
      const char* lock = mLock.State() == SK_LOCK_ON ? "locked" : (mLock.State() == SK_LOCK_ASK ? "sync?" : "unlocked");
      return "Connected · " + mUsb.PortName() + " · " + lock;
    }
    if (!mUsb.Error().empty())
      return mUsb.Error();
    return std::string("Not linked — pick the keyboard's MIDI port, then Connect.");
  };
  pGraphics->AttachControl(new skoverlay::LinkWindow(pGraphics->GetBounds(), linkHooks,
    [this]() { SetDeskHiddenForModal(false); UpdateStatusText(); }), kCtrlTagSyncWindow);

  /* P13: the key split editor, attached next to the LINK window so both modal windows sit above
   * the desk (RaiseSplitWindow re-attaches it when a desk rebuild happens while it is open). */
  pGraphics->AttachControl(new skoverlay::SplitWindow(pGraphics->GetBounds(),
    [this]() {
      SetDeskHiddenForModal(false);
      if (IGraphics* g = GetUI())
        g->SetAllControlsDirty();
    }), kCtrlTagSplitWindow);

  /* P16: the step-motion editor (one pool with the CFW's AUTOMATION page). */
  pGraphics->AttachControl(new skmotion::MotionWindow(pGraphics->GetBounds(),
    [this]() {
      SetDeskHiddenForModal(false);
      if (IGraphics* g = GetUI())
        g->SetAllControlsDirty();
    }), kCtrlTagMotionWindow);

  IRECT keyboardBounds = b.GetFromBottom(sklayout::kKeybedH);
  IRECT wheelsBounds = keyboardBounds.ReduceFromLeft(90.f).GetPadded(-4.f);
  pGraphics->AttachControl(new IVKeyboardControl(keyboardBounds, 21, 108), kCtrlTagKeyboard);
  /* P13: draw the split zones over the keys without taking any mouse event from the keybed. */
  pGraphics->AttachControl(new sksplit::KeybedOverlay(keyboardBounds, 21, 108), kCtrlTagSplitOverlay);
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

int StudioKEYS::CurrentDeskBuildKey() const
{
  /* One formula for both RebuildDesk() and OnIdle()'s change check. Mixing the
   * two (rank*64+track vs engine*32+desk) meant the rebuild no-op guard never
   * matched and the desk could stay on the previous card. */
  sk_host_ui_t u {};
  sk_host_ui_get(&u);
  const int desk = GetParam(kParamDesk)->Int();
  if (desk >= skdesk::kNumEngines)
    return 1000; /* SETTINGS */
  if (std::strcmp(u.page_title, "MIXER") == 0)
    return 2000;
  if (std::strcmp(u.page_title, "GLOBAL") == 0)
    return 2001;
  if (std::strcmp(u.page_title, "SYSTEM") == 0)
    return 2002;
  return desk; /* engine desk indexed by ENGINE_ORDER rank */
}

void StudioKEYS::RebuildDesk(IGraphics* pGraphics)
{
  IGraphics* g = pGraphics ? pGraphics : GetUI();
  if (!g)
    return;
  // #region agent log
  gUiStage.store(30);
  // #endregion

  const int desk = GetParam(kParamDesk)->Int();
  sk_host_ui_t ui {};
  sk_host_ui_get(&ui);
  const bool mixerPage = (std::strcmp(ui.page_title, "MIXER") == 0 || std::strcmp(ui.page_title, "GLOBAL") == 0 || std::strcmp(ui.page_title, "SYSTEM") == 0);
  const int buildKey = CurrentDeskBuildKey();

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

  /* Tear down by POINTER (SafeRemoveControlByTag → IGraphics::RemoveControl),
   * never RemoveControlWithTag: the latter frees the control but leaves
   * IGraphics::mMouseOver / mCapturedMap / mInPopupMenu pointing at it, and the
   * next mousemove or mouseup dereferences freed memory (SIGSEGV). The mixer
   * strips are desk controls, so selecting a channel was the trigger. */
  g->ReleaseMouseCapture();
  for (int tag : mDeskCtrlTags)
    SafeRemoveControlByTag(g, tag);
  mDeskCtrlTags.clear();
  mLastDeskBuilt = buildKey;
  // #region agent log
  gUiStage.store(31);
  // #endregion

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
          mPendingAbout = true; /* defer: SettingsPanel fires this from OnMouseDown */
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
    /* Hero area: engine desk on top, a shared per-patch VOICE module along the
     * bottom. Reserve the strip first so every engine desk lays out above it. */
    const IRECT heroBox = body.GetFromLeft(body.W() * 0.76f).GetPadded(-4.f);
    constexpr float kVoiceStripH = 92.f;
    const IRECT voiceRect = heroBox.GetFromBottom(kVoiceStripH).GetPadded(-2.f);
    const IRECT heroBody = heroBox.GetReducedFromBottom(kVoiceStripH + 8.f);
    const IRECT mixerBody = body.GetFromRight(body.W() * 0.24f).GetPadded(-4.f);

    // Always attach compact 4-track mixer on the right (24% width)
    attach(new skengui::ModulePanel(mixerBody, "4-TRACK MIXER"));
    /* P8: reserve a strip at the bottom of the mixer column for the live 8-up
     * PAGE inspector — the same PAGES[] the OLED is on, so `<`/`>`/GLO tracking
     * here always matches the hardware surface (P8b lock). */
    constexpr float kPageInspH = 96.f;
    const IRECT pageBox = mixerBody.GetFromBottom(kPageInspH).GetPadded(-2.f);
    const IRECT strips = mixerBody.GetReducedFromBottom(kPageInspH + 4.f).GetReducedFromTop(24.f).GetPadded(-2.f);
    attach(new skengui::ModulePanel(pageBox, "PAGE"));
    {
      const IRECT pg = pageBox.GetReducedFromTop(22.f).GetPadded(-4.f);
      for (int i = 0; i < 8; ++i)
      {
        const IRECT cell = pg.GetGridCell(i, 2, 4).GetPadded(-3.f);
        g->AttachControl(new SkPageKnob(cell, (uint32_t) i), kCtrlTagPageKnob0 + i);
        mDeskCtrlTags.push_back(kCtrlTagPageKnob0 + i);
      }
    }
    auto onSelectTrack = [this](uint32_t tr) {
      if (!GetUI()) return;
      sk_host_select_track(tr);
      uint32_t rank = sk_host_trk_engine_rank(tr); /* desk rank, not engine index */
      GetParam(kParamDesk)->Set((int)rank);
      sk_host_set_desk(rank);
      sk_host_ui_pump();
      mRebuildPending.store(true);
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
      /* P7: every engine has its own desk - a titled header with the engine's own subtitle,
       * a purpose-built visualiser, grouped module panels with real knobs/menus and the shared
       * ADSR strip. The visualisers read the same firmware params the knobs write. */
      const skengui::AttachParamFn putParam = [&](const IRECT& r, uint32_t ei) { attachParam(r, ei); };
      const int pAtk = skdesk::kP_ATK, pDec = skdesk::kP_DEC, pSus = skdesk::kP_SUS, pRel = skdesk::kP_REL;

      struct DeskRoute
      {
        const char* engine;
        const char* subtitle;
        void (*attach)(const skengui::AttachFn&, const skengui::AttachParamFn&, const IRECT&,
                       const IVStyle&, int, int, int, int);
      };
      static const DeskRoute kDesks[] = {
        {"LOFI",   "CHIP VOICE · 4-BIT WAVE RAM · QUANTISED OUTPUT · STEPPED ENVELOPE", sklofi::AttachLofiDesk},
        {"SAMPLE", "SAMPLE PLAYER · TUNE / BITS / LOOP · TONE FILTER", sksam::AttachSampleDesk},
        {"VOICE",  "FORMANT SYNTHESIZER · KLATT CASCADE · F1–F4 VOWEL MORPH", skvoice::AttachVoiceDesk},
        {"TRIO",   "THREE OSCILLATORS · RING / SYNC · MULTIMODE FILTER", sktrio::AttachTrioDesk},
        {"GRAIN",  "GRANULAR · POSITION / SIZE / DENSITY · SPRAY & RANDOM", skgrain::AttachGrainDesk},
        {"PHYS",   "PHYSICAL MODEL · MODAL / STRING / MEMBRANE / SYMPATHETIC", skphys::AttachPhysDesk},
        {"NOISE",  "NOISE SOURCE · COLOUR / RESONANCE · TRACK & DRIFT", sknoise::AttachNoiseDesk},
        {"SLICE",  "SLICER · DIVISIONS / START / REVERSE · PLAY MODES", skslice::AttachSliceDesk},
        {"DRUM",   "DRUM KIT · EIGHT VOICES · KIT / HIT SHAPING", skdrum::AttachDrumDesk},
      };
      const DeskRoute* route = nullptr;
      for (const DeskRoute& d : kDesks)
        if (!std::strcmp(eng->name, d.engine)) { route = &d; break; }

      WDL_String title;
      if (route)
      {
        title.SetFormatted(200, "%s — %s", eng->name, route->subtitle);
        attach(new ITextControl(heroBody.GetFromTop(18.f), title.Get(),
          IText(15.f, skmint::kTheme, "Roboto-Regular", EAlign::Near)));
        route->attach(attach, putParam, heroBody.GetReducedFromTop(22.f).GetPadded(-2.f),
                      mintStyle, pAtk, pDec, pSus, pRel);
      }
      else
      {
        /* Unknown engine (a firmware addition): the grouped two-panel desk, never a bare grid. */
        int ngroups = 0;
        const skengui::GroupDef* groups = skengui::GroupsFor(eng->name, ngroups);
        title.SetFormatted(160, "%s — %s", eng->name,
          ngroups >= 2 ? groups[0].title : "EDIT PARAMETERS");
        attach(new ITextControl(heroBody.GetFromTop(18.f), title.Get(),
          IText(15.f, skmint::kTheme, "Roboto-Regular", EAlign::Near)));
        const IRECT wave = heroBody.GetReducedFromTop(20.f).GetFromTop(40.f).GetPadded(-2.f);
        attach(new skengui::HeaderWave(wave));
        const IRECT kArea = heroBody.GetReducedFromTop(64.f).GetPadded(-2.f);
        const int perGroup = ngroups >= 2 ? 4 : (int)eng->nParams;
        for (int gi = 0; gi < ngroups && gi < 2; ++gi)
        {
          const IRECT col = kArea.GetGridCell(gi, 1, 2).GetPadded(-3.f);
          attach(new skengui::ModulePanel(col, groups[gi].title));
          const IRECT cells = col.GetReducedFromTop(22.f).GetPadded(-4.f);
          for (int i = 0; i < perGroup; ++i)
          {
            const int ei = groups[gi].start + i;
            if (ei >= (int)eng->nParams)
              break;
            attachParam(cells.GetGridCell(i, 2, 2).GetPadded(-4.f), (uint32_t)ei);
          }
        }
      }
    }

    /* Shared per-patch VOICE strip: the PATCH button opens the single patch
     * window (the keyboard's own preset bank + user slots + import bay). */
    attach(new skengui::ModulePanel(voiceRect, "PATCH · VOICE MODE · PORTAMENTO"));
    const IRECT vIn = voiceRect.GetReducedFromTop(22.f).GetPadded(-6.f);

    const IRECT patchCell = vIn.GetFromLeft(vIn.W() * 0.28f);
    attach(new PatchSelector(patchCell.GetCentredInside(std::min(220.f, patchCell.W() - 4.f), 46.f),
      [this]() { ShowPatchWindow(); }));

    const IRECT modeCell = vIn.GetReducedFromLeft(vIn.W() * 0.41f).GetFromLeft(vIn.W() * 0.19f);
    attach(new SkParamMenu(modeCell.GetCentredInside(104.f, 46.f),
      (uint32_t) skdesk::kP_VOICE, "MODE", mintStyle));
    const IRECT glideCell = vIn.GetReducedFromLeft(vIn.W() * 0.61f).GetFromLeft(vIn.W() * 0.18f);
    attach(new SkParamKnob(glideCell.GetCentredInside(60.f),
      (uint32_t) skdesk::kP_GLIDE, "GLIDE", mintStyle));
    const IRECT glModeCell = vIn.GetReducedFromLeft(vIn.W() * 0.80f);
    attach(new SkParamMenu(glModeCell.GetCentredInside(104.f, 46.f),
      (uint32_t) skdesk::kP_GLMODE, "GL MODE", mintStyle));
  }

  /* Desk attaches after the modal — raise overlay so About/UBOOT sit on top. */
  RaiseModalOverlay(g);
  if (auto* m = Modal(); m && m->IsOpen())
    SetDeskHiddenForModal(true);
  /* P12: the LINK window is attached in LayoutUI (before this rebuild), so the fresh desk
   * controls would draw over it. A rebuild while it is open must keep the desk hidden. */
  if (auto* lw = g->GetControlWithTag(kCtrlTagSyncWindow); lw && !lw->IsHidden())
    SetDeskHiddenForModal(true);
  /* P13: same for the key split editor — rebuild first, then put its window back on top. */
  if (auto* sw = g->GetControlWithTag(kCtrlTagSplitWindow); sw && !sw->IsHidden())
  {
    SafeRemoveControlByTag(g, kCtrlTagSplitWindow);
    g->AttachControl(sw, kCtrlTagSplitWindow);
    SetDeskHiddenForModal(true);
  }
  if (auto* mw = g->GetControlWithTag(kCtrlTagMotionWindow); mw && !mw->IsHidden())
  {
    SafeRemoveControlByTag(g, kCtrlTagMotionWindow);
    g->AttachControl(mw, kCtrlTagMotionWindow);
    SetDeskHiddenForModal(true);
  }
  /* The rebuilt desk controls were attached after the patch window, which would
   * cover it. If the window is open, raise it again and hide the freshly-built
   * desk (it is now a new pointer). */
  if (mPatchWinOpen)
  {
    RaisePatchWindows();
    SetDeskHiddenForModal(true);
  }
}

void StudioKEYS::ShowSyncDialog(bool show)
{
  IGraphics* g = GetUI();
  if (!g)
    return;
  auto* c = g->GetControlWithTag(kCtrlTagSyncWindow);
  auto* win = c ? static_cast<skoverlay::LinkWindow*>(c) : nullptr;
  if (win)
  {
    if (show)
    {
      win->Reload();
      win->Open();
      SetDeskHiddenForModal(true);
    }
    else
    {
      win->CloseWin();
      SetDeskHiddenForModal(false);
    }
  }
  if (auto* b = g->GetControlWithTag(kCtrlTagLinkBtn))
    b->SetDirty(false);
}

void StudioKEYS::ShowSplitWindow(bool show)
{
  IGraphics* g = GetUI();
  if (!g)
    return;
  if (auto* c = g->GetControlWithTag(kCtrlTagSplitWindow))
  {
    auto* win = static_cast<skoverlay::SplitWindow*>(c);
    if (show)
    {
      /* Re-attach so the window draws above the desk/mixer controls attached after it. */
      SafeRemoveControlByTag(g, kCtrlTagSplitWindow);
      g->AttachControl(win, kCtrlTagSplitWindow);
      win->Open();
      SetDeskHiddenForModal(true);
    }
    else
    {
      win->CloseWin();
      SetDeskHiddenForModal(false);
    }
  }
  if (auto* b = g->GetControlWithTag(kCtrlTagSplitBtn))
    b->SetDirty(false);
  g->SetAllControlsDirty();
}

void StudioKEYS::ShowMotionWindow(bool show)
{
  IGraphics* g = GetUI();
  if (!g)
    return;
  if (auto* c = g->GetControlWithTag(kCtrlTagMotionWindow))
  {
    auto* win = static_cast<skmotion::MotionWindow*>(c);
    if (show)
    {
      /* Re-attach so the window draws above the desk/mixer controls attached after it. */
      SafeRemoveControlByTag(g, kCtrlTagMotionWindow);
      g->AttachControl(win, kCtrlTagMotionWindow);
      win->Open();
      SetDeskHiddenForModal(true);
    }
    else
    {
      win->CloseWin();
      SetDeskHiddenForModal(false);
    }
  }
  if (auto* b = g->GetControlWithTag(kCtrlTagMotionBtn))
    b->SetDirty(false);
  g->SetAllControlsDirty();
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
        sk_host_version(), lockStr, mUsb.IsOpen() ? (mUsb.PortName().empty() ? "linked" : mUsb.PortName().c_str()) : "—",
        mContinuousSave ? "●" : "○", mLock.Message().c_str());
    else
      s.SetFormatted(256, "%s · Unleashed (CPU→%u) · %s · link %s · save %s · %s",
        sk_host_version(), bud, lockStr, mUsb.IsOpen() ? (mUsb.PortName().empty() ? "linked" : mUsb.PortName().c_str()) : "—",
        mContinuousSave ? "●" : "○", mLock.Message().c_str());
  }
  else
  {
    s.SetFormatted(256, "%s · Limited 8 · %s · link %s · save %s · %s",
      sk_host_version(), lockStr, mUsb.IsOpen() ? (mUsb.PortName().empty() ? "linked" : mUsb.PortName().c_str()) : "—",
      mContinuousSave ? "●" : "○", mLock.Message().c_str());
  }
  if (auto* c = g->GetControlWithTag(kCtrlTagStatus))
    c->As<ITextControl>()->SetStr(s.Get());
  if (auto* w = g->GetControlWithTag(kCtrlTagPolyWarn))
    w->As<ITextControl>()->SetStr(sk_host_unleashed() ? "Polyphony won’t match the keyboard" : "");
}

void StudioKEYS::PushButtonMask()
{
  // #region agent log
  { static uint32_t _pm = 0; if (_pm < 200u) sk_plog("F", "StudioKEYS.cpp:PushButtonMask", "btn_mask", (long)mBtnMask, 0, 0); _pm++; }
  if (mBtnMask & ((1u << kBtnOCTUP) | (1u << kBtnOCTDN)))
    sk_dump_bt("PushButtonMask OCT bit set");
  // #endregion
  sk_host_buttons(mBtnMask);
}

void StudioKEYS::ProcessBlock(sample** inputs, sample** outputs, int nFrames)
{
  (void) inputs;
  // #region agent log
  gLastAudioUs.store(sk_now_us());
  { static uint32_t _pb = 0; if (_pb < 4u || (_pb % 1600u) == 0u) sk_plog("F", "StudioKEYS.cpp:ProcessBlock", "pb_enter", (long)_pb, (long)nFrames, 0); _pb++; }
  // #endregion
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
  // #region agent log
  gLastUiUs.store(sk_now_us());
  gUiStage.store(0);
#if defined(__APPLE__) || defined(__linux__)
  gMainThread = pthread_self();
#endif
  { static uint32_t _oi = 0; if (_oi < 4u) sk_plog("H", "StudioKEYS.cpp:OnIdle", "onidle_enter", (long)_oi, 0, 0); _oi++; }
  // #endregion
  mMeterSender.TransmitData(*this);
  /* Mirror the NOR image to disk whenever the firmware wrote flash (a settings save). This is
   * what makes the theme/panel/prefs survive a process exit or a new instance; infer_erase +
   * load happen in sk_host_init(). */
  {
    const uint32_t fw = sk_host_flash_writes();
    if (fw != mLastFlashWrites)
    {
      mLastFlashWrites = fw;
      sk_host_state_save();
    }
  }
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
      /* Persist the new palette to NOR. The companion SETTINGS desk is not the
       * device MENU, so menu_close() (which saves on hardware) never runs here;
       * without this the theme is lost on reload. UI thread, once per change. */
      if (mPaletteSavePrimed)
        sk_host_settings_save();
      mPaletteSavePrimed = true;
    }

    SyncDeskFromHost();
    gUiStage.store(2);

    /* P13: the keybed overlay and the SPLIT chip read the live setting; repaint when the CFW (a
     * project load, a restored backup) or the split window changes it. */
    {
      int note = 0, lo = 0, hi = 0;
      sk_host_split_get(&note, &lo, &hi);
      const int key = std::clamp(note, 0, 127) | (std::clamp(lo, 0, 3) << 8) | (std::clamp(hi, 0, 3) << 12);
      if (key != mLastSplit)
      {
        mLastSplit = key;
        GetUI()->SetAllControlsDirty();
      }
    }

    /* P16: the AUTOMATION window and chip read the live motion pool; the CFW (or the
     * window itself) changes it, so repaint them when the pool's signature moves. */
    {
      uint32_t sig = 2166136261u;
      for (uint32_t tr = 0; tr < 4u; ++tr)
      {
        const uint32_t n = sk_host_motion_count(tr);
        sig = (sig ^ (n + 1u + 16u * (uint32_t) sk_host_motion_on(tr) + 32u * (uint32_t) sk_host_motion_armed(tr))) *
              16777619u;
        for (uint32_t i = 0; i < n; ++i)
        {
          uint32_t st = 0, pr = 0;
          int32_t v = 0;
          if (sk_host_motion_event(tr, i, &st, &pr, &v))
            sig = (sig ^ (st + 64u * pr + 8192u * (uint32_t) (v + 64))) * 16777619u;
        }
      }
      if (sig != mLastMotionSig)
      {
        mLastMotionSig = sig;
        if (auto* mw = GetUI()->GetControlWithTag(kCtrlTagMotionWindow))
          if (!mw->IsHidden())
            mw->SetDirty(false);
        if (auto* mb = GetUI()->GetControlWithTag(kCtrlTagMotionBtn))
          mb->SetDirty(false);
      }
    }

    /* P11: the Program Bay's USER column mirrors the CFW user bank. A save on the keyboard (or
     * from the librarian) changes the bank hash, and the next idle rebuilds the open window. */
    if (auto* pb = PatchBay())
      pb->SyncFromHost();

    /* Follow the virtual keyboard's selected track and page. `<`/`>` on the SMK
     * side keys select an adjacent track (ui.c track_select) and GLO/family
     * buttons change ui.page — neither touches the desk rank, so without this the
     * companion kept showing the previous track and page. A track change with a
     * different engine already rebuilds via SyncDeskFromHost(); this repaints the
     * track tabs, mixer strips, encoders, page strip and screen in every case. */
    {
      sk_host_ui_t u {};
      sk_host_ui_get(&u);
      if ((int)u.sel != mLastHostSel || (int)u.page != mLastHostPage)
      {
        mLastHostSel = (int)u.sel;
        mLastHostPage = (int)u.page;
        GetUI()->SetAllControlsDirty();
      }
    }

    const int buildKey = CurrentDeskBuildKey();

    /* RebuildDesk() tears the desk down and re-attaches controls. Teardown now
     * goes through SafeRemoveControlByTag → IGraphics::RemoveControl, which
     * keeps IGraphics bookkeeping (capture, mouse-over, popup/text-entry)
     * consistent, so a rebuild is safe even while the cursor hovers a desk
     * control. We still defer while a mouse GESTURE is active (button held) so
     * OnMouseUp never lands on a control freed mid-gesture, but we do NOT defer
     * on plain hover: the cursor sits over the plugin almost always and that
     * would freeze the desk on a stale card. */
    IGraphics* gui = GetUI();
    const bool uiBusy = gui && gui->ControlIsCaptured();

    const bool wantRebuild = mRebuildPending.exchange(false) || (buildKey != mLastDeskBuilt);
    if (wantRebuild)
    {
      if (uiBusy)
      {
        /* Defer to a later OnIdle once the mouse gesture has finished. */
        mRebuildPending.store(true);
      }
      else
      {
        // #region agent log
        gUiStage.store(3);
        // #endregion
        RebuildDesk(gui);
        // #region agent log
        gUiStage.store(4);
        // #endregion
        mDeskDirty.store(false);
      }
    }
    else if (mDeskDirty.exchange(false) && gui && !uiBusy)
    {
      ApplyMintFromHost();
      gui->SetAllControlsDirty();
    }

    /* Locked-session autosave requested from the audio thread: run it here (UI
     * thread) so the 1 MiB CRC + flash write never blocks the audio callback. */
    if (mAutosavePending.exchange(false) && mLock.State() == SK_LOCK_ON)
      mLock.Autosave();
    gUiStage.store(6);

    const uint32_t draws = sk_host_screen_draws();
    if (draws != mLastScreenDraws)
    {
      mLastScreenDraws = draws;
      if (auto* s = GetUI()->GetControlWithTag(kCtrlTagScreen))
        s->SetDirty(false);
    }

    /* Apply buffered keybed note state (pushed from the audio thread). */
    if (mKbNoteHead.load(std::memory_order_acquire) != mKbNoteTail.load(std::memory_order_acquire))
    {
      if (auto* kb = dynamic_cast<IVKeyboardControl*>(GetUI()->GetControlWithTag(kCtrlTagKeyboard)))
      {
        int h = mKbNoteHead.load(std::memory_order_relaxed);
        const int t = mKbNoteTail.load(std::memory_order_acquire);
        while (h != t)
        {
          kb->SetNoteFromMidi(mKbNoteBuf[h].note, mKbNoteBuf[h].on);
          h = (h + 1) % kKbNoteCap;
        }
        mKbNoteHead.store(h, std::memory_order_release);
      }
      else
      {
        mKbNoteHead.store(mKbNoteTail.load(std::memory_order_acquire), std::memory_order_release);
      }
    }

    if (mPendingUboot && !uiBusy)
    {
      mPendingUboot = false;
      const bool hw = sk_host_device_present() != 0;
      sk_host_menu_uboot();
      ShowUbootModal(hw);
    }
    if (mPendingAbout && !uiBusy)
    {
      mPendingAbout = false;
      ShowAbout(true);
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
      if (fwAbout && !mAboutWinOpen && !uiBusy)
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
    gUiStage.store(9);
  }
#endif
}

void StudioKEYS::OnActivate(bool active)
{
  /* P12: open the link on activation (the saved port, or name-based auto-detect) and close it
   * when the host deactivates the plug-in, so the endpoint is not held while bypassed. */
  if (active)
  {
    if (!mUsb.IsOpen())
      mUsb.OpenNamed(mUsbPort);
    mLinkCable = mUsb.IsOpen();
    mSurf.SetKeyboardConnected(mLinkCable);
  }
  else
  {
    mUsb.Close();
    mLinkCable = false;
    mSurf.SetKeyboardConnected(false);
  }
}

void StudioKEYS::OnReset()
{
  // #region agent log
  sk_plog("F", "StudioKEYS.cpp:OnReset", "on_reset", (long)GetSampleRate(), 0, 0);
  // #endregion
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
  /* Do NOT touch IGraphics from ProcessMidiMsg — it runs on the audio thread.
   * Buffer the note on/off and let OnIdle() drive the on-screen keybed. */
  if (msg.StatusMsg() == IMidiMsg::kNoteOn || msg.StatusMsg() == IMidiMsg::kNoteOff)
  {
    if (msg.Channel() == 0 || msg.Channel() == (int)sk_host_selected_track() + 1)
      PushKbNote(msg.NoteNumber(), msg.StatusMsg() == IMidiMsg::kNoteOn);
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
  /* WARNING: In this VST3 (non-distributed single-component) build, IPlug calls
   * OnParamChange(...) from ProcessParameterChanges() ON THE AUDIO THREAD.
   * Touching the GUI (RebuildDesk/GetUI/SetStr/SetAllControlsDirty/UpdateStatusText)
   * here races the UI thread and crashes the host. Everything GUI-related is
   * deferred: set a flag here, apply it in OnIdle() / OnParamChangeUI(). */
  if (mPullingHost)
    return;
  if (paramIdx == kParamUnleashed)
  {
    sk_host_set_unleashed(GetParam(kParamUnleashed)->Bool() ? 1 : 0);
    mRebuildPending.store(true); /* relabel limited/unleashed readouts on the UI thread */
  }
  if (paramIdx == kParamDesk)
  {
    mDeskIndex = GetParam(kParamDesk)->Int();
    sk_host_set_desk((uint32_t) mDeskIndex);
    sk_host_ui_pump();
    mRebuildPending.store(true);
  }
  if (paramIdx == kParamColor)
  {
    sk_host_set_palette((uint32_t) GetParam(kParamColor)->Int());
    sk_host_ui_pump();
    /* NEVER call mLock.Autosave() here: OnParamChange runs on the AUDIO thread
     * in this VST3 build, and Autosave does a CRC over the whole 1 MiB NOR image
     * plus a flash write — that stalls the audio thread and freezes the DAW.
     * Defer it to OnIdle() on the UI thread. */
    mAutosavePending.store(true);
    mDeskDirty.store(true); /* OnIdle() applies ApplyMintFromHost + repaint on the UI thread */
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
  if (paramIdx == kParamVoice)
    setAndLog(skdesk::kP_VOICE, GetParam(kParamVoice)->Int());
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

#if IPLUG_EDITOR
void StudioKEYS::OnParamChangeUI(int paramIdx, EParamSource source)
{
  (void) source;
  /* Always called on the main/UI thread. Safe place to touch IGraphics. */
  if (paramIdx == kParamUnleashed ||
      paramIdx == kParamGlide || paramIdx == kParamGlMode ||
      paramIdx == kParamVoice ||
      paramIdx == kParamAttack || paramIdx == kParamDecay ||
      paramIdx == kParamSustain || paramIdx == kParamRelease ||
      (paramIdx >= kParamE0 && paramIdx <= kParamE7))
  {
    UpdateStatusText();
  }
}
#else
void StudioKEYS::OnParamChangeUI(int paramIdx, EParamSource source)
{
  (void) paramIdx;
  (void) source;
}
#endif

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
  // #region agent log
  sk_plog("H1", "StudioKEYS.cpp:SerializeState", "state_save", (long)Librarian::NorSize(), 0, 0);
  // #endregion
  sk_host_state_save();
  const uint8_t* nor = Librarian::Nor();
  const uint32_t n = Librarian::NorSize();
  chunk.Put(&n);
  if (nor && n)
    chunk.PutBytes(nor, (int) n);
  /* P12: the companion <-> CFW MIDI port rides with the plug-in state, tagged so older states
   * (which end right after the NOR blob) still load. */
  const uint32_t portMagic = 0x31504B53u; /* "SKP1" */
  const uint32_t portLen = (uint32_t) std::min<size_t>(mUsbPort.size(), 255u);
  chunk.Put(&portMagic);
  chunk.Put(&portLen);
  if (portLen)
    chunk.PutBytes(mUsbPort.data(), (int) portLen);
  return true;
}

int StudioKEYS::UnserializeState(const IByteChunk& chunk, int startPos)
{
  uint32_t n = 0;
  startPos = chunk.Get(&n, startPos);
  // #region agent log
  { static uint32_t _us = 0; if (_us < 4u) { sk_plog("P3", "StudioKEYS.cpp:UnserializeState", "unser", (long)n, 0, 0); _us++; } }
  // #endregion
  if (n && n <= Librarian::NorSize())
  {
    std::vector<uint8_t> tmp(n);
    startPos = chunk.GetBytes(tmp.data(), (int) n, startPos);
    Librarian::NorWrite(0, tmp.data(), n);
  }

  /* P12: the optional tagged MIDI-port block after the NOR blob. */
  if (startPos > 0)
  {
    uint32_t magic = 0;
    const int afterMagic = chunk.GetBytes(&magic, 4, startPos);
    if (afterMagic > 0 && magic == 0x31504B53u)
    {
      uint32_t len = 0;
      const int afterLen = chunk.GetBytes(&len, 4, afterMagic);
      if (afterLen > 0 && len <= 255u)
      {
        if (len == 0)
          mUsbPort.clear();
        else
        {
          std::string port(len, '\0');
          if (chunk.GetBytes(port.data(), (int) len, afterLen) > 0)
            mUsbPort = port;
        }
      }
    }
  }
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
    /* By pointer: RemoveControlWithTag would dangle mMouseOver if the overlay
     * (or a control the cursor sits on) were freed by tag here. */
    SafeRemoveControlByTag(g, kCtrlTagAboutPanel);
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

skoverlay::PatchBayWindow* StudioKEYS::PatchBay()
{
  IGraphics* g = GetUI();
  if (!g)
    return nullptr;
  if (auto* c = g->GetControlWithTag(kCtrlTagPatchBay))
    return static_cast<skoverlay::PatchBayWindow*>(c);
  return nullptr;
}

/* iPlug2 draws later-attached controls on top. The window is created early in
 * LayoutUI (so the strip button owns its construction), but the desk, mixer and
 * About overlay are attached after it. Re-attach on show so it sits above
 * everything, preserving whether it is open. */
void StudioKEYS::RaisePatchWindows()
{
  IGraphics* g = GetUI();
  if (!g)
    return;

  bool patch = false;
  if (PatchBay()) { patch = true; SafeRemoveControlByTag(g, kCtrlTagPatchBay); }

  auto closeCb = [this](bool* flag) {
    return [this, flag]() {
      *flag = false;
      SetDeskHiddenForModal(false);
      if (IGraphics* gg = GetUI())
        gg->SetAllControlsDirty();
    };
  };

  auto* pw = new skoverlay::PatchBayWindow(g->GetBounds(), &mBay,
    [this]() { OnPatchLoaded(); },
    [this]() { RefreshPatchWindows(); },
    closeCb(&mPatchWinOpen));
  g->AttachControl(pw, kCtrlTagPatchBay);

  if (patch)
    PatchBay()->Open();
}

/* Rebuild the combined list + repaint the window (after a bank mutation). */
void StudioKEYS::RefreshPatchWindows()
{
  if (auto* pw = PatchBay())
    pw->Refresh();
  if (IGraphics* g = GetUI())
    g->SetAllControlsDirty();
}

/* A keyboard-bank sound was loaded: swap the desk to its engine and refresh. */
void StudioKEYS::OnPatchLoaded()
{
  const int desk = (int) sk_host_sel_engine_rank();
  if (desk >= 0 && desk < skdesk::kNumEngines)
  {
    sk_host_set_desk((uint32_t) desk);
    sk_host_ui_pump();
    GetParam(kParamDesk)->Set(desk);
    mDeskIndex = desk;
  }
  RefreshAfterLoad();
}

/* Load a keyboard-bank sound into the selected track, switch the desk to its
 * engine, then refresh the editor. isUser=1 loads NOR slot `index`; else factory
 * preset `index` (shown order) of the engine at desk rank `engineRankOrUser`. */
void StudioKEYS::ApplyBankPreset(unsigned engineRankOrUser, int isUser, unsigned index)
{
  int desk = (int) engineRankOrUser;
  if (isUser)
    sk_host_user_apply(index);
  else
  {
    if (!sk_host_engine_factory_apply(engineRankOrUser, index))
      return;
  }

  if (!isUser)
    desk = (int) engineRankOrUser;
  else
    desk = (int) sk_host_sel_engine_rank(); /* user sound's own engine */

  if (desk >= 0 && desk < skdesk::kNumEngines)
  {
    sk_host_set_desk((uint32_t) desk);
    sk_host_ui_pump();
    GetParam(kParamDesk)->Set(desk);
    mDeskIndex = desk;
  }

  RefreshAfterLoad();
}

void StudioKEYS::RefreshAfterLoad()
{
  sk_host_ui_pump();
  SyncDeskFromHost();
  mRebuildPending.store(true);
  if (IGraphics* g = GetUI())
    g->SetAllControlsDirty();
  UpdateStatusText();
}

/* Open the single combined PATCH · USER · IMPORTS window on top. */
void StudioKEYS::ShowPatchWindow()
{
  IGraphics* g = GetUI();
  if (!g)
    return;
  mPatchWinOpen = true;
  RaisePatchWindows(); /* re-attach on top; Open() restores the window */
  SetDeskHiddenForModal(true);
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
    /* Defer: SyncDeskFromHost() is reachable from button mouse-up callbacks and
     * from OnIdle. Only OnIdle may safely rebuild the desk (it waits for the
     * mouse gesture to end first). */
    mRebuildPending.store(true);
  }

  /* Sync parameters to UI controls if not captured by mouse */
  PullHostParamToUI(kParamGlide, sk_host_get_param(skdesk::kP_GLIDE));
  PullHostParamToUI(kParamGlMode, sk_host_get_param(skdesk::kP_GLMODE));
  PullHostParamToUI(kParamVoice, sk_host_get_param(skdesk::kP_VOICE));
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
  /* P6c: the 18 printed buttons. Line 1 is the printed name, line 2 what the button does now,
   * from the firmware's own ui_hints() (the same conditions its handlers test), so the strip
   * follows the page / layer / menu / transport and cannot drift from the CFW. */
  {
    sk_host_btn_hint_t h[SK_HOST_NBTN] {};
    const uint32_t n = sk_host_button_hints(h, SK_HOST_NBTN);
    for (uint32_t i = 0; i < n && i < SK_HOST_NBTN; ++i)
    {
      if (mBtnHintAct[i] == h[i].act && mBtnHintHold[i] == h[i].hold)
        continue;
      mBtnHintName[i] = h[i].name;
      mBtnHintAct[i] = h[i].act;
      mBtnHintHold[i] = h[i].hold;
      if (auto* c = g->GetControlWithTag(kCtrlTagSmkBtn0 + (int) i))
      {
        if (auto* btn = dynamic_cast<SkHoldButton*>(c))
          btn->SetHints(h[i].act, h[i].hold);
      }
    }
  }
  skfm6::PulseEgGraphs();
}
#endif

#endif // IPLUG_DSP
