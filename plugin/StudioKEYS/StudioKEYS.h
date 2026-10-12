#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "IControls.h"
#include "MidiCables.h"
#include "HostEngine.h"
#include "DeskParams.h"
#include "Librarian.h"
#include "ImportBay.h"
#include "LockSession.h"
#include "SurfaceCard.h"
#include "SkMint.h"

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
  kNumParams
};

enum EControlTags
{
  kCtrlTagMeter = 0,
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
  kCtrlTagDeskKnob0,
  kNumCtrlTags = kCtrlTagDeskKnob0 + 16
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
  void UpdateStatusText();
  void UpdateSurfaceStatus(const char* msg);
  void PushButtonMask();
  void ApplyMintFromHost();

  HostEngine mHost;
  LockSession mLock;
  Librarian mLib;
  ImportBay mBay;
  sksurface::SurfaceBridge mSurf;
  IPeakAvgSender<2> mMeterSender;
  skmidi::MidiRouter mMidi;
  uint32_t mBtnMask = 0;
  uint32_t mLastScreenDraws = 0;
  bool mLinkCable = false;
  bool mContinuousSave = false;
  int mDeskIndex = 0;
  int mLastDeskBuilt = -1;
  std::vector<int> mDeskCtrlTags;
#endif
};
