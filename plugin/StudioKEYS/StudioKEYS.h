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
#include "Fm6Desk.h"
#include "AnalogDesk.h"
#include "WheelDesk.h"

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
  kCtrlTagSyncPanel,
  kCtrlTagSyncDlg,
  kCtrlTagSyncPluginToHw,
  kCtrlTagSyncHwToPlugin,
  kCtrlTagSyncDismiss,
  kCtrlTagPolyWarn,
  kCtrlTagLibrarian,
  kCtrlTagImportBay,
  kCtrlTagDeskTitle,
  kCtrlTagSurfaceStatus,
  kCtrlTagPageTitle,
  kCtrlTagPageKnob0,
  kCtrlTagPageKnob1,
  kCtrlTagPageKnob2,
  kCtrlTagPageKnob3,
  kCtrlTagBayDrag,
  kCtrlTagEnc0,
  kCtrlTagEnc7 = kCtrlTagEnc0 + 7,
  kCtrlTagFader0,
  kCtrlTagFader3 = kCtrlTagFader0 + 3,
  kCtrlTagDeskPanel,
  kCtrlTagLibPanel,
  kCtrlTagLibScrim,
  kCtrlTagLibTitle,
  kCtrlTagLibClose,
  kCtrlTagAboutPanel, /* sksettings::ModalOverlay (About + UBOOT roast) */
  kCtrlTagSmkPanel,
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
  void OnParamChange(int paramIdx) override;
  void OnIdle() override;
  bool OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData) override;
  bool SerializeState(IByteChunk& chunk) const override;
  int UnserializeState(const IByteChunk& chunk, int startPos) override;

private:
  void DrainCableQueues();
  void LayoutUI(IGraphics* pGraphics) override;
  void RebuildDesk(IGraphics* pGraphics);
  void ShowSyncDialog(bool show);
  void ShowLibrarian(bool show);
  void ShowAbout(bool show);
  void ShowUbootModal(bool hwMode);
  void SetDeskHiddenForModal(bool hide);
  void RaiseModalOverlay(IGraphics* pGraphics = nullptr);
  void UpdateStatusText();
  void UpdateSurfaceStatus(const char* msg);
  void PushButtonMask();
  void ApplyMintFromHost();
  void ApplyLargeScale();
  void SendDeviceUboot();
  sksettings::ModalOverlay* Modal();

  HostEngine mHost;
  LockSession mLock;
  Librarian mLib;
  ImportBay mBay;
  sksurface::SurfaceBridge mSurf;
  skusb::MidiLink mUsb;
  IPeakAvgSender<2> mMeterSender;
  skmidi::MidiRouter mMidi;
  uint32_t mBtnMask = 0;
  uint32_t mExtraMask = 0;
  uint32_t mLastScreenDraws = 0;
  bool mLinkCable = false;
  bool mContinuousSave = false;
  std::vector<float> mProcessBufL, mProcessBufR;
  bool mLibWinOpen = false;
  bool mAboutWinOpen = false;
  bool mPendingUboot = false;
  int mDeskIndex = 0;
  int mLastDeskBuilt = -1;
  int mLastHostDesk = -2;
  int mLastPalette = -1;
  int mLastLang = -1;
  int mLastLarge = -1;
  int mPageIndex = 0;
  std::vector<int> mDeskCtrlTags;
  std::vector<IControl*> mLibWinCtrls;
  void SyncDeskFromHost();
  void PullHostParamToUI(int paramIdx, double raw);
  void RefreshPanelScribbles();
  bool mRebuildPending = false;
  bool mPullingHost = false;
#endif
};
