#pragma once

#include "IControl.h"
#include "sk_host.h"
#include <cstdint>

// Panel / inspector palette — follows firmware COLOR via sk_host_palette_rgb565.
namespace skmint {

inline iplug::igraphics::IColor From565(uint16_t c)
{
  const int r = ((c >> 11) & 31) * 255 / 31;
  const int g = ((c >> 5) & 63) * 255 / 63;
  const int b = (c & 31) * 255 / 31;
  return iplug::igraphics::IColor(255, r, g, b);
}

inline iplug::igraphics::IColor kBg(255, 25, 32, 25);
inline iplug::igraphics::IColor kSurf(255, 41, 50, 41);
inline iplug::igraphics::IColor kText(255, 239, 244, 220);
inline iplug::igraphics::IColor kTheme(255, 150, 220, 72);
inline iplug::igraphics::IColor kAccent(255, 254, 196, 64);

inline void SyncFromHost()
{
  uint16_t p[5] {};
  sk_host_palette_rgb565(p);
  kBg = From565(p[0]);
  kSurf = From565(p[1]);
  kText = From565(p[2]);
  kTheme = From565(p[3]);
  kAccent = From565(p[4]);
}

} // namespace skmint
