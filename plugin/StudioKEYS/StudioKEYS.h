/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) */
#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "IControls.h"
#include "MidiCables.h"
#include "HostEngine.h"
#include "DeskParams.h"
#include "PageInspector.h"
#include "Librarian.h"
#include "ImportBay.h"
#include "LockSession.h"
#include "SurfaceCard.h"
#include "UsbMidiLink.h"
#include "BayDrag.h"
#include "SkMint.h"
#include "SettingsDesk.h"
#include "PatchOverlays.h"
#include "MotionWindow.h"
#include "GameWindow.h"
#include "UndoBar.h"
#include "Fm6Desk.h"
#include "AnalogDesk.h"
#include "WheelDesk.h"
#include "LofiDesk.h"
#include "SampleDesk.h"
#include "VoiceDesk.h"
#include "TrioDesk.h"
#include "GrainDesk.h"
#include "PhysDesk.h"
#include "NoiseDesk.h"
#include "SliceDesk.h"
#include "DrumDesk.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

const int kNumPresets = 1;

enum EParams
{
  kParamGain = 0,
  kParamUnleashed,
  kParamDesk,
  kParamColor,
  kParamLang, /* 0 EN, 1 ID — CFW MENU > LANG */
  kParamGlide,
  kParamGlMode,
  kParamAttack,
  kParamDecay,
  kParamSustain,
  kParamRelease,
  kParamE0,
  kParamE1,
  kParamE2,
  kParamE3,
  kParamE4,
  kParamE5,
  kParamE6,
  kParamE7,
  kParamLevel,
  kParamPan,
  kParamVoice,
  kParamPrio,
  kParamAlloc,
  kParamDetune,
  kParamExtClock,
  kNumParams
};

enum EControlTags
{
  kCtrlTagMeter = 0,
  kCtrlTagScope,
  kCtrlTagKeyboard,
  kCtrlTagBender,
  kCtrlTagModWheel,
  kCtrlTagScreen,
  kCtrlTagStatus,
  kCtrlTagMasterFader,
  kCtrlTagSyncWindow, /* P12: the LINK window (port selector + sync direction) */
  kCtrlTagLinkBtn,    /* P12: the chrome LINK button */
  kCtrlTagSplitWindow, /* P13: the KEY SPLIT editor window */
  kCtrlTagSplitBtn,    /* P13: the chrome SPLIT chip */
  kCtrlTagSplitOverlay, /* P13: zone tint + split line over the virtual keybed */
  kCtrlTagMotionWindow, /* P16: the step-motion (automation) editor window */
  kCtrlTagMotionBtn,    /* P16: the chrome AUTO chip */
  kCtrlTagUndoBtn,      /* P18: the chrome UNDO chip */
  kCtrlTagRedoBtn,      /* P18: the chrome REDO chip */
  kCtrlTagPolyWarn,
  kCtrlTagDeskTitle,
  kCtrlTagSurfaceStatus,
  kCtrlTagPageTitle,
  kCtrlTagPageKnob0,
  kCtrlTagPageKnob7 = kCtrlTagPageKnob0 + 7,
  kCtrlTagBayDrag,
  kCtrlTagEnc0,
  kCtrlTagEnc7 = kCtrlTagEnc0 + 7,
  kCtrlTagFader0,
  kCtrlTagFader3 = kCtrlTagFader0 + 3,
  kCtrlTagDeskPanel,
  kCtrlTagGameWindow, /* P19/P20: the FLAPPY / TETRIS companion window */
  kCtrlTagAboutPanel, /* sksettings::ModalOverlay (About + UBOOT roast) */
  kCtrlTagPatchBay,      /* skoverlay::PatchBayWindow (presets+user+imports) */
  kCtrlTagSmkPanel,
  kCtrlTagSmkBtn0, /* P6c: the 18 printed buttons (SkHoldButton scribble strips) */
  kCtrlTagSmkBtn17 = kCtrlTagSmkBtn0 + 17,
  kCtrlTagLogo,
  kCtrlTagDeskKnob0,
  kNumCtrlTags = kCtrlTagDeskKnob0 + 200
};

using namespace iplug;
using namespace igraphics;

class StudioKEYS final : public Plugin
{
public:
  StudioKEYS(const InstanceInfo& info);
  ~StudioKEYS() override;

#if IPLUG_DSP
  void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;
  void ProcessMidiMsg(const IMidiMsg& msg) override;
  void OnReset() override;
  void OnActivate(bool active) override;
  void OnParamChange(int paramIdx) override;
  void OnParamChangeUI(int paramIdx, EParamSource source) override;
  void OnIdle() override;
  bool OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData) override;
  bool SerializeState(IByteChunk& chunk) const override;
  int UnserializeState(const IByteChunk& chunk, int startPos) override;

private:
  void DrainCableQueues();
  void LayoutUI(IGraphics* pGraphics) override;
  void RebuildDesk(IGraphics* pGraphics);
  void ShowSyncDialog(bool show);
  void ShowSplitWindow(bool show); /* P13: open/close the key split editor */
  void ShowMotionWindow(bool show); /* P16: open/close the automation editor */
  void ShowGameWindow(bool show);   /* P19/P20: open/close the easter-egg window */
  bool mGameWinOpen = false;
  void DoUndo();               /* P18: swap the selected track's sound back */
  void DoRedo();               /* P18: and forward again */
  void ShowPatchWindow();      /* open the single combined patch window */
  void ShowAbout(bool show);
  void ShowUbootModal(bool hwMode);
  void ShowBtModal();                          /* P6e: the BT key's roast in the companion */
  void SetDeskHiddenForModal(bool hide);
  void RaiseModalOverlay(IGraphics* pGraphics = nullptr);
  void UpdateStatusText();
  void UpdateSurfaceStatus(const char* msg);
  void PushButtonMask();
  void ApplyMintFromHost();
  void ApplyLargeScale();
  void SendDeviceUboot();
  sksettings::ModalOverlay* Modal();
  skoverlay::PatchBayWindow* PatchBay();
  void RaisePatchWindows(); /* re-attach the patch window on top (z-order) */
  void RefreshPatchWindows(); /* rebuild the combined list + repaint the window */
  void OnPatchLoaded();       /* desk swap + UI refresh after a bank load */
  /* Load a factory/user patch by bank data into the selected track, swap the
   * desk to its engine, and refresh the UI. Shared by the windows. */
  void ApplyBankPreset(unsigned engineRankOrUser, int isUser, unsigned index);
  void RefreshAfterLoad();

  HostEngine mHost;
  LockSession mLock;
  ImportBay mBay;
  sksurface::SurfaceBridge mSurf;
  skusb::MidiLink mUsb;
  IPeakAvgSender<2> mMeterSender;
  skmidi::MidiRouter mMidi;
  uint32_t mBtnMask = 0;
  uint32_t mExtraMask = 0;
  /* Live channel-aftertouch per pad while held (mouse, UI thread). */
  int mPadPressure[16] {};
  uint32_t mLastScreenDraws = 0;
  bool mLinkCable = false;
  /* P12: the MIDI endpoint the user picked for the CFW link ("" = name-based auto-detect).
   * Persisted in the plugin state and re-opened in OnActivate(). */
  std::string mUsbPort;
  bool mContinuousSave = false;
  std::vector<float> mProcessBufL, mProcessBufR;
  /* Which patch window is open (drives the desk-hide + rebuild guard). */
  bool mPatchWinOpen = false;   /* PatchBayWindow */
  bool mAboutWinOpen = false;
  bool mPendingUboot = false;
  bool mPendingAbout = false;
  int mDeskIndex = 0;
  int mLastDeskBuilt = -1;
  int mLastHostDesk = -2;
  /* Follow the firmware's selected track and page: `<`/`>` select a track and GLO
   * / family buttons change the page, neither of which changes the desk rank, so
   * without these the companion kept showing the previous track/page. */
  int mLastHostSel = -2;
  int mLastHostPage = -2;
  int mLastPalette = -1;
  /* P6c: the last button hints drawn, so the strips are only redrawn when the firmware changes
   * what a button does (page, layer, menu, transport). */
  std::string mBtnHintName[18];
  std::string mBtnHintAct[18];
  std::string mBtnHintHold[18];
  bool mPaletteSavePrimed = false;
  int mLastLang = -1;
  int mLastLarge = -1;
  int mLastSplit = -1; /* P13: note | lo << 8 | hi << 12, so the keybed repaints when it changes */
  uint32_t mLastMotionSig = 0; /* P16: the motion pool, so the open AUTOMATION window follows the CFW */
  uint32_t mLastUndoStamp = 0; /* P18: the undo/redo stacks, so the two chrome chips relight */
  uint32_t mLastFlashWrites = 0;
  int mPageIndex = 0;
  std::vector<int> mDeskCtrlTags;
  void SyncDeskFromHost();
  void PullHostParamToUI(int paramIdx, double raw);
  void RefreshPanelScribbles();
  /* Single source of truth for "which desk is showing" — used by both OnIdle()
   * change-detection and RebuildDesk(). They used different formulas before, so
   * the rebuild never triggered correctly (wrong card / stale card). */
  int CurrentDeskBuildKey() const;
  /* Audio thread → UI thread keybed note state (never touch IGraphics off-thread). */
  struct KbNote { int note; bool on; };
  static constexpr int kKbNoteCap = 64;
  KbNote mKbNoteBuf[kKbNoteCap] {};
  std::atomic<int> mKbNoteHead {0};
  std::atomic<int> mKbNoteTail {0};
  void PushKbNote(int note, bool on)
  {
    const int t = mKbNoteTail.load(std::memory_order_relaxed);
    const int next = (t + 1) % kKbNoteCap;
    if (next == mKbNoteHead.load(std::memory_order_acquire))
      return; /* drop if the UI is behind */
    mKbNoteBuf[t] = {note, on};
    mKbNoteTail.store(next, std::memory_order_release);
  }
  /* Written from the audio thread (OnParamChange) / read+cleared on the UI thread
   * (OnIdle). Must be atomic so the two never race on a torn read. */
  std::atomic<bool> mRebuildPending {false};
  /* Audio thread requests the desk follow the current engine; UI thread applies. */
  std::atomic<bool> mDeskDirty {false};
  /* Audio thread asked for a locked-session autosave; OnIdle() runs it (it does
   * a 1 MiB CRC + flash write and must never run on the audio thread). */
  std::atomic<bool> mAutosavePending {false};
  bool mPullingHost = false;
#endif
};
