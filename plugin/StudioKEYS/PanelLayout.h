#pragma once

/** Shared panel geometry for LayoutUI + tools/check_hit_targets.py.
 * Nexus-style: large editor desk on top, compact SMK faceplate + keybed below. */
namespace sklayout {

static constexpr float kMinHit = 44.f;

static constexpr int kPlugW = 1280;
static constexpr int kPlugH = 880;
static constexpr float kPad = 8.f;
static constexpr float kChromeH = 32.f;
/* Desk is the hero — ~half the window. Faceplate stays compact (Nexus macros strip). */
static constexpr float kDeskFrac = 0.46f;
static constexpr float kKeybedH = 90.f;
/* Bender + mod wheel strip (before GetPadded(-4)); each FracRectHorizontal(0.5) ≥ kMinHit. */
static constexpr float kWheelsStripW = 96.f;
/* Faceplate columns: logo | screen+micros | knobs/pads | faders+buttons */
static constexpr float kLogoColFrac = 0.08f;
/* P9: a slightly wider screen column plus a shorter under-screen band give the 240x240
 * virtual screen more room; the under-screen 2x3 buttons stay ≥ kMinHit. */
static constexpr float kScreenColFrac = 0.24f;
static constexpr float kMidFrac = 0.42f;
static constexpr float kRightFrac = 0.28f;
/* Under-screen: 2 columns × 3 rows (KNOB BANK|FADER BANK / <|> / OCT+|OCT-). P9: 148 -> 120. */
static constexpr float kUnderScreenH = 120.f;
static constexpr float kKnobSize = 52.f;
static constexpr float kFaderBandFrac = 0.55f;
static constexpr float kBtn12BandFrac = 0.40f;
/* Librarian modal */
static constexpr float kLibWinW = 760.f;
static constexpr float kLibWinH = 420.f;

} // namespace sklayout
