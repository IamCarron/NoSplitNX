#pragma once
// Host-rendered GBA pause menu, toast and FPS overlay using NoSplitNX's shared frontend/ui.h
// Canvas & Menu so GBA pauses look identical to DS pauses at native resolution.
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "embed.h"
#include "ui.h"

namespace GbaHostUi
{

inline void DrawOverlay(void*, std::vector<uint32_t>& outPx, int outW, int outH,
                        bool menuOpen, const std::vector<GbaWide::MenuHostItem>& items, int selected,
                        const std::string& title, const std::string& hint, const std::string& note,
                        const std::string& toast, int toastAlpha, int fpsShown, bool showFps,
                        const std::string& accentName, GbaWide::MenuHostState& drawn)
{
    if (outW <= 0 || outH <= 0) return;
    Canvas canvas;
    canvas.px.swap(outPx);
    const size_t need = (size_t)outW * (size_t)outH;
    if (canvas.px.size() != need) canvas.px.assign(need, 0);
    else std::fill(canvas.px.begin(), canvas.px.end(), 0);
    canvas.w = outW;
    canvas.h = outH;

    Menu::Accent = AccentNamed(accentName);

    if (menuOpen && !items.empty())
    {
        Menu m;
        m.open = true;
        m.selected = std::clamp(selected, 0, (int)items.size() - 1);
        m.scroll = drawn.scroll;
        m.items.reserve(items.size());
        for (const GbaWide::MenuHostItem& it : items)
            m.items.push_back({ it.label, it.value, it.info, it.slider });
        m.Draw(canvas, title, hint, note, 0, true);
        drawn.boxX = m.drawnX;
        drawn.boxY = m.drawnY;
        drawn.boxW = m.drawnW;
        drawn.boxH = m.drawnH;
        drawn.lineH = m.drawnLineH;
        drawn.firstY = m.drawnFirstY;
        drawn.shown = m.drawnShown;
        drawn.scroll = m.scroll;
        drawn.barX = m.barX;
        drawn.barStep = m.barStep;
        drawn.count = (int)items.size();
    }
    else
    {
        drawn = GbaWide::MenuHostState();
    }

    if (showFps)
    {
        const int sc = outH / 240 < 1 ? 1 : outH / 240;
        const std::string text = std::to_string(fpsShown) + " FPS";
        const int dotR = std::max(2, 2 * sc);
        const int tw = Canvas::TextWidth(text, sc) + dotR * 2 + 12 * sc;
        const int th = 13 * sc;
        const int fx = outW - 4 * sc - tw - 4 * sc;
        const int fy = 3 * sc;
        const uint32_t ledCol = fpsShown >= 55 ? 0xFF00E676 : fpsShown >= 40 ? 0xFFFFB020 : 0xFFFF453A;
        canvas.ShadowRound(fx, fy, tw, th, 3 * sc, sc, sc);
        canvas.FillGlass(fx, fy, tw, th, 3 * sc, 0xD0171D2A, 0xD00F131D, 0xFF28344C, 1);
        const int dotX = fx + 4 * sc + dotR;
        const int dotY = fy + th / 2;
        canvas.FillRound(dotX - dotR - 1, dotY - dotR - 1, (dotR + 1) * 2, (dotR + 1) * 2, dotR + 1, (ledCol & 0x00FFFFFF) | 0x33000000u);
        canvas.FillRound(dotX - dotR, dotY - dotR, dotR * 2, dotR * 2, dotR, ledCol);
        canvas.Fill(dotX - 1, dotY - 1, 2, 2, 0xFFFFFFFF);
        canvas.Text(dotX + dotR + 4 * sc, fy + 3 * sc, text, sc, 0xFFFFFFFF);
    }

    if (!toast.empty() && toastAlpha > 0)
    {
        const int sc = outH / 240 < 1 ? 1 : outH / 240;
        const int fontH = std::max(14, 7 * sc);
        const std::string nice = ToNiceSentence(UiLang::Tr(toast));
        const int maxW = std::max(120, outW - 24 * sc);
        const int textW = std::min(maxW - 14 * sc, Canvas::ModernTextWidth(nice, fontH));
        const int tw = textW + 14 * sc;
        const int th = fontH + 10 * sc;
        const int tx = 6 * sc, ty = 5 * sc;
        const uint32_t aGlass = (uint32_t)(0xE0 * toastAlpha / 255) << 24;
        const uint32_t aFull = (uint32_t)toastAlpha << 24;
        canvas.ShadowRound(tx, ty, tw, th, 3 * sc, sc, sc);
        canvas.FillGlass(tx, ty, tw, th, 3 * sc, aGlass | 0x161C28u, aGlass | 0x0E121Au, aFull | 0x2A354Au, 1);
        canvas.FillRound(tx + sc, ty + 2 * sc, std::max(2, 2 * sc), th - 4 * sc, sc, aFull | (Menu::Accent & 0x00FFFFFFu));
        std::string shown = nice;
        while (shown.size() > 3 && Canvas::ModernTextWidth(shown, fontH) > textW)
        {
            while (!shown.empty() && ((unsigned char)shown.back() & 0xC0) == 0x80) shown.pop_back();
            if (!shown.empty()) shown.pop_back();
        }
        if (shown.size() < nice.size() && shown.size() > 3) shown += "...";
        canvas.ModernText(tx + 7 * sc, ty + (th - fontH) / 2, shown, fontH, aFull | 0xFFFFFFu);
    }

    outPx.swap(canvas.px);
}

inline const GbaWide::UiHost* Api()
{
    static const GbaWide::UiHost host = { DrawOverlay, nullptr };
    return &host;
}

} // namespace GbaHostUi
