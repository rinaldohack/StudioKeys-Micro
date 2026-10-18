/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) */
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
/* The three faceplate columns tile the panel left to right:
 *   [0, kScreenColFrac)                     screen + under-screen buttons
 *   [kScreenColFrac, 1 - kRightFrac)        knobs + pads
 *   [1 - kRightFrac, 1)                     faders + side keys
 * They must add up exactly: a gap is wasted width, an overlap draws one column over the
 * other. P9 widened the screen column 0.20 -> 0.24 while the mid column still started at a
 * hard-coded 0.20, so the pad grid was drawn on top of the under-screen buttons (pads 1/9 cut
 * through KNOB BANK / FADER BANK). LayoutUI now derives every column from these fractions. */
static constexpr float kMidFrac = 0.46f;
static constexpr float kRightFrac = 0.30f;
static_assert(kScreenColFrac + kMidFrac + kRightFrac <= 1.0001f,
              "faceplate columns overlap: screen + mid + right must be <= 1");
/* Under-screen: 2 columns × 3 rows (KNOB BANK|FADER BANK / <|> / OCT+|OCT-). P9: 148 -> 120. */
static constexpr float kUnderScreenH = 120.f;
static constexpr float kKnobSize = 52.f;
/* Mid column: knobs on top, pads under them (fractions of the mid column's height). */
static constexpr float kKnobsBandFrac = 0.48f;
static constexpr float kPadsBandFrac = 0.44f;
/* Right column: faders on top, the 12 printed side keys under them. */
static constexpr float kFaderBandFrac = 0.55f;
static constexpr float kBtn12BandFrac = 0.40f;
/* Librarian modal */
static constexpr float kLibWinW = 760.f;
static constexpr float kLibWinH = 420.f;

} // namespace sklayout
