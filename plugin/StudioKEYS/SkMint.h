#pragma once

#include "IControl.h"
#include "sk_host.h"
#include "resources/fonts/RobotoRegular.h"
#include <algorithm>
#include <cstdint>
#include <cstdlib>

// Panel / inspector palette — follows firmware COLOR via sk_host_palette_rgb565.
namespace skmint {

inline constexpr const char* kFont = "Roboto-Regular";

inline iplug::igraphics::IColor From565(uint16_t c)
{
  const int r = ((c >> 11) & 31) * 255 / 31;
  const int g = ((c >> 5) & 63) * 255 / 63;
  const int b = (c & 31) * 255 / 31;
  return iplug::igraphics::IColor(255, r, g, b);
}

inline int Luma(const iplug::igraphics::IColor& c)
{
  return (c.R * 54 + c.G * 183 + c.B * 19) >> 8;
}

/** Ink that stays readable on `bg` (for labels that must never vanish). */
inline iplug::igraphics::IColor InkOn(const iplug::igraphics::IColor& bg)
{
  return Luma(bg) > 140 ? iplug::igraphics::IColor(255, 16, 16, 18)
                        : iplug::igraphics::IColor(255, 245, 242, 235);
}

inline iplug::igraphics::IColor kBg(255, 25, 32, 25);
inline iplug::igraphics::IColor kSurf(255, 41, 50, 41);
inline iplug::igraphics::IColor kText(255, 239, 244, 220);
inline iplug::igraphics::IColor kTheme(255, 150, 220, 72);
inline iplug::igraphics::IColor kAccent(255, 254, 196, 64);
inline iplug::igraphics::IColor kWell(255, 55, 58, 62);
inline iplug::igraphics::IColor kInk(255, 245, 242, 235);

inline void SyncFromHost()
{
  uint16_t p[5] {};
  sk_host_palette_rgb565(p);
  kBg = From565(p[0]);
  kSurf = From565(p[1]);
  kText = From565(p[2]);
  kTheme = From565(p[3]);
  kAccent = From565(p[4]);
  /* Raised well for keycaps / faceplate — step above BG so chrome isn't wireframe. */
  const int wr = std::min(255, kSurf.R + 28);
  const int wg = std::min(255, kSurf.G + 28);
  const int wb = std::min(255, kSurf.B + 28);
  kWell = iplug::igraphics::IColor(255, wr, wg, wb);
  kInk = InkOn(kWell);
  /* If theme TEXT lands too close to SURF (light-on-light / dark-on-dark), force readable ink. */
  if (std::abs(Luma(kText) - Luma(kSurf)) < 55)
    kText = InkOn(kSurf);
  if (std::abs(Luma(kAccent) - Luma(kBg)) < 40)
    kAccent = kTheme;
}

/** Load Roboto from embedded bytes first.
 * Ableton/macOS sandbox: CGDataProviderCreateWithURL fails for .ttf on disk
 * (iPlug CoreTextHelpers comment). In-memory LoadFont is required for labels. */
inline bool EnsureFont(iplug::igraphics::IGraphics* g)
{
  if (!g)
    return false;
  /* void* overload — not const; cast away const on static blob. */
  if (g->LoadFont(kFont, const_cast<uint8_t*>(ROBOTO_REGULAR), ROBOTO_REGULAR_length))
    return true;
  if (g->LoadFont(kFont, "Roboto-Regular.ttf"))
    return true;
  if (g->LoadFont(kFont, "Helvetica Neue", iplug::igraphics::ETextStyle::Normal))
    return true;
  if (g->LoadFont(kFont, "Arial", iplug::igraphics::ETextStyle::Normal))
    return true;
  return g->LoadFont(kFont, "Helvetica", iplug::igraphics::ETextStyle::Normal);
}

inline iplug::igraphics::IVStyle KnobStyle()
{
  using namespace iplug::igraphics;
    return DEFAULT_STYLE
    .WithColor(kBG, kBg)
    .WithColor(kFG, kWell)
    .WithColor(kPR, kSurf)
    .WithColor(kFR, kTheme)
    .WithColor(kX1, kAccent)
    .WithColor(kHL, IColor(35, 255, 255, 255))
    .WithColor(kSH, IColor(50, 0, 0, 0))
    .WithDrawFrame(true)
    .WithFrameThickness(1.5f)
    .WithShowLabel(true)
    .WithLabelText(IText(16.f, kText, kFont))
    .WithValueText(IText(15.f, kAccent, kFont));
}

inline iplug::igraphics::IVStyle ButtonStyle()
{
  using namespace iplug::igraphics;
  return DEFAULT_STYLE
    .WithColor(kBG, kBg)
    .WithColor(kFG, kWell)
    .WithColor(kPR, kAccent)
    .WithColor(kFR, kTheme)
    .WithColor(kHL, IColor(30, 255, 255, 255))
    .WithColor(kSH, IColor(40, 0, 0, 0))
    .WithDrawFrame(true)
    .WithFrameThickness(1.25f)
    .WithRoundness(0.2f)
    .WithLabelText(IText(16.f, kInk, kFont))
    .WithValueText(IText(16.f, kInk, kFont));
}

} // namespace skmint