/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) */
#pragma once

/* P19 / P20 — the easter eggs on the companion.
 *
 * The CFW's MENU has two hidden rows (FLAPPY, TETRIS). Opening either one mutes every output
 * and shows a tiny game on the 240x240 panel; this window is the same game, bigger. The state
 * is the firmware's own (sk_host_game_view), so the two frames cannot disagree: while the cable-3
 * link is locked the plug-in is the authority and the keyboard mirrors it, and either side can
 * input (the keyboard's pads/keys arrive here through the link, the mouse clicks go the other
 * way). The window's Close carries the same meaning as OCT- on the panel. */

#include "sk_host.h"
#include "SkMint.h"
#include "PatchOverlays.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>

namespace skgame {

/* the tetromino colours, the same seven hues the firmware's pads and panel use */
inline const IColor kTet[7] = {
    IColor(255, 0, 210, 220), IColor(255, 235, 220, 20), IColor(255, 170, 70, 220),
    IColor(255, 60, 100, 235), IColor(255, 235, 150, 40), IColor(255, 60, 205, 80),
    IColor(255, 230, 60, 60)};

/* the two spawn shapes the board packs into nibbles: rotation is recomputed here exactly as the
 * firmware does it (a 4x4 bitmap turned clockwise: bit (x, y) -> (3 - y, x)) */
inline uint32_t TetMask(uint32_t piece, uint32_t rot)
{
    static const uint16_t kShape[7] = {0x0F00, 0x0660, 0x0E40, 0x00E1, 0x00E2, 0x00C6, 0x006C};
    uint32_t m = kShape[piece % 7u];
    for (uint32_t r = 0; r < rot % 4u; ++r)
    {
        uint32_t n = 0;
        for (uint32_t y = 0; y < 4u; ++y)
            for (uint32_t x = 0; x < 4u; ++x)
                if (m & (1u << (y * 4u + x)))
                    n |= 1u << (x * 4u + (3u - y));
        m = n;
    }
    return m;
}

class GameWindow final : public skoverlay::SkWindow
{
public:
    explicit GameWindow(const IRECT& bounds, CloseFn onClose)
    : SkWindow(bounds, "EASTER EGG", std::move(onClose))
    {
    }

    float WinW() const override { return 760.f; }
    float WinH() const override { return 640.f; }
    int RowCount() const override { return 0; }

    /* Called from OnIdle: repaint only when the firmware's state moved. */
    void RefreshState()
    {
        if (IsHidden())
            return;
        sk_game_view_t v {};
        sk_host_game_view(&v);
        if (v.rev == mRev && v.kind == mKind)
            return;
        mRev = v.rev;
        mKind = v.kind;
        SetDirty(false);
    }

protected:
    void OnOpened() override
    {
        mRev = 0xFFFFFFFFu;
        mKind = 0xFFu;
        RefreshState();
    }

    void OnLayout(const IRECT& in) override
    {
        mTop = in.GetFromTop(26.f);
        mFoot = in.GetFromBottom(42.f);
        mPanel = in.GetReducedFromTop(32.f).GetReducedFromBottom(50.f);
        /* a square playfield, centred: the panel's 240x240 game, scaled up */
        const float side = std::min(mPanel.W(), mPanel.H());
        mBoard = mPanel.GetCentredInside(side, side);
        /* TETRIS' four buttons live here, NOT in DrawTetris: SkWindow::OnMouseDown() calls
         * Layout() before OnBodyDown(), so a rect computed only at draw time was emptied and
         * re-read in the same click — every button missed. Geometry belongs in OnLayout. */
        for (int i = 0; i < 4; ++i)
            mBtn[i] = mFoot.GetGridCell(i, 1, 4).GetPadded(-4.f);
    }

    void DrawBody(IGraphics& g) override
    {
        sk_game_view_t v {};
        sk_host_game_view(&v);

        const char* name = v.kind == SK_GAME_FLAPPY ? "FLAPPY" : v.kind == SK_GAME_TETRIS ? "TETRIS" : "—";
        g.DrawText(IText(20.f, skmint::kTheme, skmint::kFont, EAlign::Near, EVAlign::Middle), name,
                   mTop.GetFromLeft(200.f));
        g.DrawText(IText(12.f, skmint::kText, skmint::kFont, EAlign::Far, EVAlign::Middle),
                   "MUTED · the instrument is silent while a game is open", mTop.GetReducedFromLeft(205.f));

        if (v.kind == SK_GAME_FLAPPY)
            DrawFlappy(g, v);
        else if (v.kind == SK_GAME_TETRIS)
            DrawTetris(g, v);
    }

    /* The playfield is the panel's own 240 x 240 canvas, scaled into mBoard: every coordinate in
     * the two draws below is firmware/src/game.c's (fl_draw / tet_draw), so the two pictures
     * cannot drift, and the field is clipped to the board exactly as the panel clips at its
     * canvas edge (an upper pipe scrolling in from the left used to paint over the window). */
    struct Field
    {
        IRECT b;
        float s;

        IRECT At(float x, float y, float w, float h) const
        {
            return IRECT(b.L + x * s, b.T + y * s, b.L + (x + w) * s, b.T + (y + h) * s);
        }
    };

    void DrawFlappy(IGraphics& g, const sk_game_view_t& v)
    {
        const Field f { mBoard, mBoard.W() / 240.f };

        g.FillRect(skmint::kBg, f.b);
        g.PathClipRegion(f.b);
        /* the pipes: a body with a lip at each end of the gap */
        for (int i = 0; i < 3; ++i)
        {
            const float gx = (float) v.gx[i];
            const float top = (float) v.gy[i] - (float) (v.gh[i] / 2);   /* the panel halves an int */
            const float bot = (float) v.gy[i] + (float) (v.gh[i] / 2);
            if (gx > 240.f || gx + 34.f < 0.f)
                continue;
            if (top > 8.f)
                g.FillRect(skmint::kTheme, f.At(gx, 0.f, 34.f, top - 8.f));
            g.FillRect(skmint::kSel, f.At(gx - 3.f, top - 8.f, 40.f, 8.f));
            if (bot + 8.f < 240.f)
                g.FillRect(skmint::kTheme, f.At(gx, bot + 8.f, 34.f, 240.f - (bot + 8.f)));
            g.FillRect(skmint::kSel, f.At(gx - 3.f, bot, 40.f, 8.f));
        }
        /* the ground and the bird */
        g.FillRect(skmint::kSurf, f.At(0.f, 232.f, 240.f, 8.f));
        g.FillRect(skmint::kLine, f.At(0.f, 232.f, 240.f, 1.f));
        {
            const float by = (float) v.bird_y;
            const IRECT body = f.At(51.f, by - 6.f, 14.f, 12.f);
            g.FillRoundRect(v.alive ? skmint::kAccent : skmint::kDim, body, 5.f * f.s);
            g.DrawRoundRect(skmint::kBg, body, 5.f * f.s, 0, 1.f);
            g.FillRect(skmint::kPanelInk, f.At(60.f, by - 3.f, 3.f, 3.f));   /* the eye */
            g.FillRect(skmint::kSel, f.At(63.f, by + 1.f, 4.f, 2.f));       /* the beak */
        }
        /* the score, then the state */
        char sc[16];
        std::snprintf(sc, sizeof sc, "%d", (int) v.score);
        g.DrawText(IText(std::max(11.f, 22.f * f.s), v.alive ? skmint::kText : skmint::kMid, skmint::kFont,
                         EAlign::Center, EVAlign::Top), sc, f.At(0.f, 6.f, 240.f, 19.f));
        if (!v.alive)
        {
            const IRECT box = f.At(48.f, 84.f, 144.f, 46.f);
            g.FillRoundRect(skmint::kSurf, box, 8.f * f.s);
            g.DrawRoundRect(skmint::kAccent, box, 8.f * f.s, 0, 1.f);
            g.DrawText(IText(std::max(11.f, 22.f * f.s), skmint::kAccent, skmint::kFont, EAlign::Center,
                             EVAlign::Top), "GAME OVER", f.At(48.f, 92.f, 144.f, 19.f));
            g.DrawText(IText(std::max(8.f, 13.f * f.s), skmint::kText, skmint::kFont, EAlign::Center,
                             EVAlign::Top), "CLICK TO RETRY", f.At(48.f, 112.f, 144.f, 15.f));
        }
        g.PathClipRegion();

        /* the window's hint (the panel's "ANY PAD / KEY: FLAP" is for the pads, not the mouse) */
        g.FillRoundRect(skmint::kTheme, mFoot.GetPadded(-2.f), 6.f);
        g.DrawText(IText(16.f, skmint::kInk, skmint::kFont, EAlign::Center, EVAlign::Middle),
                   "CLICK ANYWHERE IN THE FIELD TO FLAP", mFoot);
    }

    void DrawTetris(IGraphics& g, const sk_game_view_t& v)
    {
        const Field f { mBoard, mBoard.W() / 240.f };
        const float cw = 10.f, ch = 12.f, bx = 24.f, by = 12.f;    /* game.c tet_draw's cell and well */
        const float fontS = std::max(8.f, 13.f * f.s), fontM = std::max(10.f, 22.f * f.s);

        g.FillRect(skmint::kBg, f.b);
        g.PathClipRegion(f.b);
        /* the well: its frame, the settled cells, then the falling piece */
        g.FillRect(skmint::kLine, f.At(bx - 2.f, by - 2.f, 10.f * cw + 4.f, 18.f * ch + 4.f));
        g.FillRect(skmint::kBg, f.At(bx, by, 10.f * cw, 18.f * ch));
        for (uint32_t y = 0; y < SK_GAME_TET_ROWS; ++y)
            for (uint32_t x = 0; x < SK_GAME_TET_COLS; ++x)
            {
                const uint8_t c = v.board[y * SK_GAME_TET_COLS + x];
                if (!c)
                    continue;
                const IRECT r = f.At(bx + (float) x * cw, by + (float) y * ch, cw - 1.f, ch - 1.f);
                g.FillRect(kTet[(c - 1u) % 7u], r);
                g.FillRect(skmint::kRaise, r.GetFromTop(1.f));
            }
        {
            const uint32_t m = TetMask(v.piece, v.rot);
            for (uint32_t y = 0; y < 4u; ++y)
                for (uint32_t x = 0; x < 4u; ++x)
                {
                    if (!(m & (1u << (y * 4u + x))))
                        continue;
                    const int cx = (int) v.px + (int) x, cy = (int) v.py + (int) y;
                    if (cy < 0 || cx < 0 || cx >= SK_GAME_TET_COLS || cy >= SK_GAME_TET_ROWS)
                        continue;
                    const IRECT r = f.At(bx + (float) cx * cw, by + (float) cy * ch, cw - 1.f, ch - 1.f);
                    g.FillRect(kTet[v.piece % 7u], r);
                    g.FillRect(skmint::kRaise, r.GetFromTop(1.f));
                }
        }
        /* the side panel: title, lines, score, the two next pieces */
        g.DrawText(IText(fontM, skmint::kTheme, skmint::kFont, EAlign::Near, EVAlign::Top), "TETRIS",
                   f.At(140.f, 12.f, 100.f, 19.f));
        char buf[24];
        g.DrawText(IText(fontS, skmint::kMid, skmint::kFont, EAlign::Near, EVAlign::Top), "LINES",
                   f.At(140.f, 34.f, 60.f, 15.f));
        std::snprintf(buf, sizeof buf, "%d", (int) v.lines);
        g.DrawText(IText(fontS, skmint::kText, skmint::kFont, EAlign::Near, EVAlign::Top), buf,
                   f.At(204.f, 34.f, 36.f, 15.f));
        g.DrawText(IText(fontS, skmint::kMid, skmint::kFont, EAlign::Near, EVAlign::Top), "SCORE",
                   f.At(140.f, 50.f, 60.f, 15.f));
        std::snprintf(buf, sizeof buf, "%d", (int) v.score);
        g.DrawText(IText(fontS, skmint::kText, skmint::kFont, EAlign::Near, EVAlign::Top), buf,
                   f.At(204.f, 50.f, 36.f, 15.f));
        g.DrawText(IText(fontS, skmint::kMid, skmint::kFont, EAlign::Near, EVAlign::Top), "NEXT",
                   f.At(140.f, 74.f, 60.f, 15.f));
        for (int n = 0; n < 2; ++n)
        {
            const uint32_t pc = n ? v.next1 : v.next0;
            const uint32_t m = TetMask(pc, 0);
            const float ox = 146.f, oy = 88.f + (float) n * 40.f;
            g.FillRect(skmint::kSurf, f.At(ox - 2.f, oy - 2.f, 20.f, 20.f));
            for (uint32_t y = 0; y < 4u; ++y)
                for (uint32_t x = 0; x < 4u; ++x)
                    if (m & (1u << (y * 4u + x)))
                        g.FillRect(kTet[pc % 7u], f.At(ox + (float) x * 4.f, oy + (float) y * 4.f, 4.f, 4.f));
        }
        if (v.over)
        {
            const IRECT box = f.At(32.f, 96.f, 128.f, 46.f);
            g.FillRoundRect(skmint::kSurf, box, 8.f * f.s);
            g.DrawRoundRect(skmint::kAccent, box, 8.f * f.s, 0, 1.f);
            g.DrawText(IText(fontM, skmint::kAccent, skmint::kFont, EAlign::Center, EVAlign::Top),
                       "GAME OVER", f.At(32.f, 104.f, 128.f, 19.f));
            g.DrawText(IText(fontS, skmint::kText, skmint::kFont, EAlign::Center, EVAlign::Top),
                       "DROP TO RETRY", f.At(32.f, 124.f, 128.f, 15.f));
        }
        g.PathClipRegion();

        /* the four buttons: left, rotate, right, drop (their rects come from OnLayout) */
        static const char* kLab[4] = {"<  LEFT", "ROTATE", "RIGHT  >", "DROP"};
        for (int i = 0; i < 4; ++i)
        {
            g.FillRoundRect(skmint::kTheme, mBtn[i], 6.f);
            g.DrawText(IText(15.f, skmint::kInk, skmint::kFont, EAlign::Center, EVAlign::Middle), kLab[i],
                       mBtn[i]);
        }
    }

    void OnBodyDown(float x, float y) override
    {
        sk_game_view_t v {};
        sk_host_game_view(&v);
        if (v.kind == SK_GAME_FLAPPY)
        {
            if (mBoard.Contains(x, y))
                sk_host_game_input(SK_GAME_IN_FLAP);
            return;
        }
        if (v.kind != SK_GAME_TETRIS)
            return;
        for (int i = 0; i < 4; ++i)
            if (mBtn[i].Contains(x, y))
            {
                sk_host_game_input(i == 0   ? SK_GAME_IN_LEFT
                                   : i == 1 ? SK_GAME_IN_ROT
                                   : i == 2 ? SK_GAME_IN_RIGHT
                                            : SK_GAME_IN_DROP);
                SetDirty(false);
                return;
            }
    }

    void OnBodyWheel(float x, float y, float d) override
    {
        (void) x;
        (void) y;
        sk_game_view_t v {};
        sk_host_game_view(&v);
        if (v.kind != SK_GAME_TETRIS)
            return;
        sk_host_game_input(d > 0.f ? SK_GAME_IN_ROT : SK_GAME_IN_DROP);
        SetDirty(false);
    }

private:
    IRECT mTop, mFoot, mPanel, mBoard;
    IRECT mBtn[4];
    uint32_t mRev = 0xFFFFFFFFu;
    uint8_t mKind = 0xFFu;
};

} // namespace skgame
