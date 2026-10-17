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
        for (int i = 0; i < 4; ++i)
            mBtn[i] = IRECT();
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

    void DrawFlappy(IGraphics& g, const sk_game_view_t& v)
    {
        const IRECT b = mBoard;
        const float s = b.W() / 240.f;
        auto px = [&](float x) { return b.L + x * s; };
        auto py = [&](float y) { return b.T + y * s; };

        g.FillRect(skmint::kBg, b);
        g.DrawRoundRect(skmint::kWell, b, 6.f, 0, 2.f);

        for (int i = 0; i < 3; ++i)
        {
            const float gx = (float) v.gx[i];
            const float top = (float) v.gy[i] - (float) v.gh[i] / 2.f;
            const float bot = (float) v.gy[i] + (float) v.gh[i] / 2.f;
            const float w = 34.f * s;
            if (gx > 246.f || gx + 34.f < 0.f)
                continue;
            g.FillRect(skmint::kTheme, IRECT(px(gx), py(0.f), px(gx) + w, py(top - 8.f)));
            g.FillRect(skmint::kAccent, IRECT(px(gx) - 2.f, py(top - 8.f), px(gx) + w + 2.f, py(top)));
            g.FillRect(skmint::kTheme, IRECT(px(gx), py(bot + 8.f), px(gx) + w, py(240.f)));
            g.FillRect(skmint::kAccent, IRECT(px(gx) - 2.f, py(bot), px(gx) + w + 2.f, py(bot + 8.f)));
        }
        g.FillRect(skmint::kWell, IRECT(b.L, py(232.f), b.R, b.B));
        /* the bird: a rounded body, the eye and the beak */
        const float by = (float) v.bird_y;
        g.FillRoundRect(v.alive ? skmint::kAccent : skmint::kWell,
                        IRECT(px(51.f), py(by - 6.f), px(65.f), py(by + 6.f)), 5.f * s);
        g.FillRect(skmint::kBg, IRECT(px(59.f), py(by - 3.f), px(62.f), py(by)));
        g.FillRect(skmint::kTheme, IRECT(px(62.f), py(by + 1.f), px(66.f), py(by + 3.f)));

        char sc[16];
        std::snprintf(sc, sizeof sc, "%d", (int) v.score);
        g.DrawText(IText(34.f, skmint::kInk, skmint::kFont, EAlign::Center, EVAlign::Top), sc,
                   IRECT(b.L, b.T + 8.f, b.R, b.T + 54.f));

        if (!v.alive)
        {
            const IRECT box = b.GetCentredInside(std::min(b.W() - 40.f, 320.f), 108.f);
            g.FillRoundRect(skmint::kSurf, box, 10.f);
            g.DrawRoundRect(skmint::kTheme, box, 10.f, 0, 2.f);
            g.DrawText(IText(24.f, skmint::kTheme, skmint::kFont, EAlign::Center, EVAlign::Middle),
                       "GAME OVER", box.GetFromTop(54.f));
            g.DrawText(IText(14.f, skmint::kText, skmint::kFont, EAlign::Center, EVAlign::Middle),
                       "Click the field to try again", box.GetReducedFromTop(54.f));
        }

        g.FillRoundRect(skmint::kTheme, mFoot.GetPadded(-2.f), 6.f);
        g.DrawText(IText(16.f, skmint::InkOn(skmint::kTheme), skmint::kFont, EAlign::Center, EVAlign::Middle),
                   "CLICK ANYWHERE IN THE FIELD TO FLAP", mFoot);
    }

    void DrawTetris(IGraphics& g, const sk_game_view_t& v)
    {
        const float side = std::min(mBoard.H() / 18.f, (mBoard.W() - 220.f) / 10.f);
        const float cell = std::max(10.f, side);
        const float bw = cell * 10.f, bh = cell * 18.f;
        const IRECT well(mBoard.L + 20.f, mBoard.T, mBoard.L + 20.f + bw, mBoard.T + bh);
        const IRECT sideBar(well.R + 24.f, well.T, mBoard.R, well.B);

        g.FillRect(skmint::kBg, mBoard);
        g.FillRect(skmint::kWell, well);
        for (uint32_t y = 0; y < SK_GAME_TET_ROWS; ++y)
            for (uint32_t x = 0; x < SK_GAME_TET_COLS; ++x)
            {
                const uint8_t c = v.board[y * SK_GAME_TET_COLS + x];
                if (!c)
                    continue;
                const IRECT r(well.L + (float) x * cell, well.T + (float) y * cell,
                              well.L + (float) (x + 1) * cell - 1.f, well.T + (float) (y + 1) * cell - 1.f);
                g.FillRect(kTet[(c - 1u) % 7u], r);
                g.FillRect(IColor(60, 255, 255, 255), r.GetFromTop(1.f));
            }
        /* the falling piece, drawn from the same mask the firmware rotates */
        {
            const uint32_t m = TetMask(v.piece, v.rot);
            for (uint32_t y = 0; y < 4u; ++y)
                for (uint32_t x = 0; x < 4u; ++x)
                {
                    if (!(m & (1u << (y * 4u + x))))
                        continue;
                    const int bx = (int) v.px + (int) x, by = (int) v.py + (int) y;
                    if (by < 0 || bx < 0 || bx >= SK_GAME_TET_COLS)
                        continue;
                    g.FillRect(kTet[v.piece % 7u],
                               IRECT(well.L + (float) bx * cell, well.T + (float) by * cell,
                                     well.L + (float) (bx + 1) * cell - 1.f,
                                     well.T + (float) (by + 1) * cell - 1.f));
                }
        }

        auto label = [&](const char* s, float y, const char* val, float sz) {
            g.DrawText(IText(13.f, skmint::kText, skmint::kFont, EAlign::Near, EVAlign::Top), s,
                       IRECT(sideBar.L, y, sideBar.R, y + 18.f));
            g.DrawText(IText(sz, skmint::kTheme, skmint::kFont, EAlign::Near, EVAlign::Top), val,
                       IRECT(sideBar.L, y + 18.f, sideBar.R, y + 18.f + sz));
        };
        char buf[24];
        std::snprintf(buf, sizeof buf, "%d", (int) v.score);
        label("SCORE", sideBar.T, buf, 30.f);
        std::snprintf(buf, sizeof buf, "%d", (int) v.lines);
        label("LINES", sideBar.T + 66.f, buf, 24.f);
        g.DrawText(IText(13.f, skmint::kText, skmint::kFont, EAlign::Near, EVAlign::Top), "NEXT",
                   IRECT(sideBar.L, sideBar.T + 132.f, sideBar.R, sideBar.T + 150.f));
        for (int n = 0; n < 2; ++n)
        {
            const uint32_t pc = n ? v.next1 : v.next0;
            const uint32_t m = TetMask(pc, 0);
            const IRECT box(sideBar.L + (float) n * 74.f, sideBar.T + 152.f,
                            sideBar.L + (float) n * 74.f + 64.f, sideBar.T + 216.f);
            g.FillRoundRect(skmint::kWell, box, 5.f);
            for (uint32_t y = 0; y < 4u; ++y)
                for (uint32_t x = 0; x < 4u; ++x)
                    if (m & (1u << (y * 4u + x)))
                        g.FillRect(kTet[pc % 7u], IRECT(box.L + 8.f + (float) x * 12.f,
                                                        box.T + 8.f + (float) y * 12.f,
                                                        box.L + 20.f + (float) x * 12.f,
                                                        box.T + 20.f + (float) y * 12.f));
        }

        /* the four buttons: left, rotate, right, drop */
        for (int i = 0; i < 4; ++i)
            mBtn[i] = mFoot.GetGridCell(i, 1, 4).GetPadded(-4.f);
        static const char* kLab[4] = {"<  LEFT", "ROTATE", "RIGHT  >", "DROP"};
        for (int i = 0; i < 4; ++i)
        {
            g.FillRoundRect(skmint::kTheme, mBtn[i], 6.f);
            g.DrawText(IText(15.f, skmint::InkOn(skmint::kTheme), skmint::kFont, EAlign::Center,
                             EVAlign::Middle),
                       kLab[i], mBtn[i]);
        }
        if (v.over)
        {
            const IRECT box = well.GetCentredInside(std::min(well.W() - 20.f, 260.f), 100.f);
            g.FillRoundRect(skmint::kSurf, box, 10.f);
            g.DrawRoundRect(skmint::kTheme, box, 10.f, 0, 2.f);
            g.DrawText(IText(22.f, skmint::kTheme, skmint::kFont, EAlign::Center, EVAlign::Middle),
                       "GAME OVER", box.GetFromTop(50.f));
            g.DrawText(IText(13.f, skmint::kText, skmint::kFont, EAlign::Center, EVAlign::Middle),
                       "DROP to try again", box.GetReducedFromTop(50.f));
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
