// Software-drawn overlay: pause menu, touch cursor and status messages.
// Drawn into an ARGB buffer the size of the output, so the window and the
// headless screenshots show the same thing.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "ui_text.h"

struct Canvas
{
    int w = 0, h = 0;
    std::vector<uint32_t> px; // ARGB; alpha 0 means "show the game underneath"

    void Resize(int nw, int nh)
    {
        if (nw != w || nh != h)
        {
            w = nw;
            h = nh;
            px.assign((size_t)w * h, 0);
            x0d = y0d = 0;
            x1d = w;
            y1d = h;
        }
    }
    // The rectangle that has been drawn into since the last Clear. Only that part is
    // cleared and sent to the screen: the canvas is as large as the window.
    int x0d = 0, y0d = 0, x1d = 0, y1d = 0;
    bool Dirty() const { return x1d > x0d && y1d > y0d; }
    void Clear()
    {
        for (int yy = y0d; yy < y1d; yy++)
            std::fill(px.begin() + (size_t)yy * w + x0d, px.begin() + (size_t)yy * w + x1d, 0);
        x0d = w;
        y0d = h;
        x1d = y1d = 0;
    }

    void Fill(int x, int y, int rw, int rh, uint32_t argb)
    {
        int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
        int x1 = x + rw > w ? w : x + rw, y1 = y + rh > h ? h : y + rh;
        if (x1 <= x0 || y1 <= y0) return;
        if (x0 < x0d) x0d = x0;
        if (y0 < y0d) y0d = y0;
        if (x1 > x1d) x1d = x1;
        if (y1 > y1d) y1d = y1;
        for (int yy = y0; yy < y1; yy++)
            for (int xx = x0; xx < x1; xx++)
                px[(size_t)yy * w + xx] = argb;
    }

    static inline void BlendPixel(uint32_t& dst, uint32_t src)
    {
        const uint32_t sa = src >> 24;
        if (sa == 0) return;
        if (sa == 255 || (dst >> 24) == 0)
        {
            dst = src;
            return;
        }
        const uint32_t ia = 255 - sa;
        const uint32_t r = (((src >> 16) & 0xFF) * sa + ((dst >> 16) & 0xFF) * ia) / 255;
        const uint32_t g = (((src >> 8) & 0xFF) * sa + ((dst >> 8) & 0xFF) * ia) / 255;
        const uint32_t b = ((src & 0xFF) * sa + (dst & 0xFF) * ia) / 255;
        const uint32_t da = dst >> 24;
        const uint32_t outA = da == 255 ? 255 : std::min<uint32_t>(255, sa + (da * ia) / 255);
        dst = (outA << 24) | (r << 16) | (g << 8) | b;
    }

    void FillBlend(int x, int y, int rw, int rh, uint32_t argb)
    {
        if ((argb >> 24) == 255) { Fill(x, y, rw, rh, argb); return; }
        if ((argb >> 24) == 0) return;
        int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
        int x1 = x + rw > w ? w : x + rw, y1 = y + rh > h ? h : y + rh;
        if (x1 <= x0 || y1 <= y0) return;
        if (x0 < x0d) x0d = x0;
        if (y0 < y0d) y0d = y0;
        if (x1 > x1d) x1d = x1;
        if (y1 > y1d) y1d = y1;
        for (int yy = y0; yy < y1; yy++)
        {
            uint32_t* row = &px[(size_t)yy * w];
            for (int xx = x0; xx < x1; xx++) BlendPixel(row[xx], argb);
        }
    }

    static inline void RoundedSpan(int rw, int rh, int radius, int dy, int& dx0, int& dx1)
    {
        dx0 = 0;
        dx1 = rw;
        if (radius <= 0 || rw <= 0 || rh <= 0) return;
        const int r = std::min({ radius, rw / 2, rh / 2 });
        int cy = -1;
        if (dy < r) cy = r - 1 - dy;
        else if (dy >= rh - r) cy = dy - (rh - r);
        if (cy >= 0)
        {
            int inset = r;
            while (inset > 0 && (r - inset) * (r - inset) + cy * cy <= r * r) inset--;
            dx0 = inset;
            dx1 = rw - inset;
        }
    }

    void FillRound(int x, int y, int rw, int rh, int radius, uint32_t argb)
    {
        if (radius <= 0) { FillBlend(x, y, rw, rh, argb); return; }
        if ((argb >> 24) == 0 || rw <= 0 || rh <= 0) return;
        int y0 = std::max(0, y), y1 = std::min(h, y + rh);
        int x0 = std::max(0, x), x1 = std::min(w, x + rw);
        if (x1 <= x0 || y1 <= y0) return;
        if (x0 < x0d) x0d = x0;
        if (y0 < y0d) y0d = y0;
        if (x1 > x1d) x1d = x1;
        if (y1 > y1d) y1d = y1;
        const bool opaque = (argb >> 24) == 255;
        for (int yy = y0; yy < y1; yy++)
        {
            int dx0 = 0, dx1 = rw;
            RoundedSpan(rw, rh, radius, yy - y, dx0, dx1);
            const int sx0 = std::max(0, x + dx0), sx1 = std::min(w, x + dx1);
            uint32_t* row = &px[(size_t)yy * w];
            if (opaque)
                for (int xx = sx0; xx < sx1; xx++) row[xx] = argb;
            else
                for (int xx = sx0; xx < sx1; xx++) BlendPixel(row[xx], argb);
        }
    }

    void FillGradV(int x, int y, int rw, int rh, int radius, uint32_t topArgb, uint32_t botArgb)
    {
        if (rw <= 0 || rh <= 0) return;
        int y0 = std::max(0, y), y1 = std::min(h, y + rh);
        int x0 = std::max(0, x), x1 = std::min(w, x + rw);
        if (x1 <= x0 || y1 <= y0) return;
        if (x0 < x0d) x0d = x0;
        if (y0 < y0d) y0d = y0;
        if (x1 > x1d) x1d = x1;
        if (y1 > y1d) y1d = y1;
        const int span = std::max(1, rh - 1);
        for (int yy = y0; yy < y1; yy++)
        {
            const int dy = yy - y;
            const int inv = span - dy;
            const uint32_t a = (((topArgb >> 24) & 0xFF) * inv + ((botArgb >> 24) & 0xFF) * dy) / span;
            const uint32_t r = (((topArgb >> 16) & 0xFF) * inv + ((botArgb >> 16) & 0xFF) * dy) / span;
            const uint32_t g = (((topArgb >> 8) & 0xFF) * inv + ((botArgb >> 8) & 0xFF) * dy) / span;
            const uint32_t b = ((topArgb & 0xFF) * inv + (botArgb & 0xFF) * dy) / span;
            const uint32_t col = (a << 24) | (r << 16) | (g << 8) | b;
            int dx0 = 0, dx1 = rw;
            if (radius > 0) RoundedSpan(rw, rh, radius, dy, dx0, dx1);
            const int sx0 = std::max(0, x + dx0), sx1 = std::min(w, x + dx1);
            uint32_t* row = &px[(size_t)yy * w];
            if (a == 255)
                for (int xx = sx0; xx < sx1; xx++) row[xx] = col;
            else if (a > 0)
                for (int xx = sx0; xx < sx1; xx++) BlendPixel(row[xx], col);
        }
    }

    void OutlineRound(int x, int y, int rw, int rh, int radius, int t, uint32_t argb)
    {
        if (t <= 0 || rw <= 0 || rh <= 0 || (argb >> 24) == 0) return;
        int y0 = std::max(0, y), y1 = std::min(h, y + rh);
        int x0 = std::max(0, x), x1 = std::min(w, x + rw);
        if (x1 <= x0 || y1 <= y0) return;
        if (x0 < x0d) x0d = x0;
        if (y0 < y0d) y0d = y0;
        if (x1 > x1d) x1d = x1;
        if (y1 > y1d) y1d = y1;
        const bool opaque = (argb >> 24) == 255;
        const int irw = rw - 2 * t, irh = rh - 2 * t, ir = std::max(0, radius - t);
        for (int yy = y0; yy < y1; yy++)
        {
            const int dy = yy - y;
            int ox0 = 0, ox1 = rw;
            RoundedSpan(rw, rh, radius, dy, ox0, ox1);
            uint32_t* row = &px[(size_t)yy * w];
            if (dy < t || dy >= rh - t || irw <= 0 || irh <= 0)
            {
                const int sx0 = std::max(0, x + ox0), sx1 = std::min(w, x + ox1);
                if (opaque) for (int xx = sx0; xx < sx1; xx++) row[xx] = argb;
                else for (int xx = sx0; xx < sx1; xx++) BlendPixel(row[xx], argb);
            }
            else
            {
                int ix0 = 0, ix1 = irw;
                RoundedSpan(irw, irh, ir, dy - t, ix0, ix1);
                ix0 += t;
                ix1 += t;
                const int lx0 = std::max(0, x + ox0), lx1 = std::min(w, x + ix0);
                const int rx0 = std::max(0, x + ix1), rx1 = std::min(w, x + ox1);
                if (opaque)
                {
                    for (int xx = lx0; xx < lx1; xx++) row[xx] = argb;
                    for (int xx = rx0; xx < rx1; xx++) row[xx] = argb;
                }
                else
                {
                    for (int xx = lx0; xx < lx1; xx++) BlendPixel(row[xx], argb);
                    for (int xx = rx0; xx < rx1; xx++) BlendPixel(row[xx], argb);
                }
            }
        }
    }

    void ShadowRound(int x, int y, int rw, int rh, int radius, int spread, uint32_t maxAlpha = 88)
    {
        for (int s = spread; s >= 1; s--)
        {
            const uint32_t a = maxAlpha * (uint32_t)(spread - s + 1) / (uint32_t)(spread * 2);
            if (a > 0) OutlineRound(x - s, y - s, rw + 2 * s, rh + 2 * s, radius + s, 1, a << 24);
        }
    }

    void Pill(int x, int y, int rw, int rh, uint32_t fillArgb, uint32_t borderArgb = 0, int borderT = 1)
    {
        const int r = rh / 2;
        FillRound(x, y, rw, rh, r, fillArgb);
        if (borderArgb >> 24) OutlineRound(x, y, rw, rh, r, borderT, borderArgb);
    }

    void GlowRound(int x, int y, int rw, int rh, int radius, int spread, uint32_t glowArgb)
    {
        const uint32_t rgb = glowArgb & 0x00FFFFFF;
        const uint32_t baseA = (glowArgb >> 24) & 0xFF;
        if (baseA == 0 || spread <= 0 || rw <= 0 || rh <= 0) return;
        for (int s = spread; s >= 1; s--)
        {
            const uint32_t a = (baseA * (uint32_t)(spread - s + 1) * (uint32_t)(spread - s + 1)) / (uint32_t)(spread * spread * 2);
            if (a > 0) OutlineRound(x - s, y - s, rw + 2 * s, rh + 2 * s, radius + s, 1, (a << 24) | rgb);
        }
    }

    void FillGlass(int x, int y, int rw, int rh, int radius, uint32_t topArgb, uint32_t botArgb, uint32_t borderArgb, int borderT = 1)
    {
        if (rw <= 0 || rh <= 0) return;
        FillGradV(x, y, rw, rh, topArgb, botArgb, radius);
        if (rh > 4 && radius > 0)
        {
            int dx0 = 0, dx1 = rw;
            RoundedSpan(rw, rh, radius, 1, dx0, dx1);
            if (dx1 > dx0 + 4)
                FillBlend(x + dx0 + 2, y + 1, dx1 - dx0 - 4, 1, 0x44FFFFFF);
        }
        if ((borderArgb >> 24) > 0 && borderT > 0)
            OutlineRound(x, y, rw, rh, radius, borderT, borderArgb);
    }

    void FillVignette(int intensity = 120)
    {
        if (w <= 0 || h <= 0) return;
        const int cornerSteps = std::min(w, h) / 6;
        for (int i = 0; i < cornerSteps; i += 4)
        {
            const uint32_t alpha = (uint32_t)(intensity * (cornerSteps - i)) / (uint32_t)cornerSteps;
            if (alpha > 0)
                OutlineRound(i, i, w - 2 * i, h - 2 * i, cornerSteps - i, 4, (alpha << 24));
        }
    }

    void DrawButtonBadge(int x, int y, const std::string& label, int scale, uint32_t badgeColor = 0xFF2A344A, uint32_t textCol = 0xFFFFFFFF, uint32_t borderCol = 0xFF455577)
    {
        const int s = std::max(1, scale);
        const int tw = TextWidthRaw(label, s);
        const bool single = label.size() <= 1 || label == "+" || label == "-";
        const int bh = 11 * s;
        const int bw = single ? bh : tw + 8 * s;
        const int rad = bh / 2;
        const uint32_t topBevel = (((badgeColor >> 16) & 0xFF) + 30 <= 255 ? badgeColor + 0x001E1E1E : 0xFFFFFFFF);
        const uint32_t botBevel = (((badgeColor >> 16) & 0xFF) >= 25 ? badgeColor - 0x00191919 : badgeColor);
        FillGradV(x, y, bw, bh, topBevel, botBevel, rad);
        OutlineRound(x, y, bw, bh, rad, 1, borderCol);
        if (bh > 4) FillBlend(x + s + 1, y + 1, bw - 2 * (s + 1), 1, 0x33FFFFFF);
        TextRaw(x + (bw - tw) / 2, y + (bh - 7 * s) / 2, label, s, textCol);
    }

    void DrawSwitch(int x, int y, int scale, bool on, uint32_t accentColor)
    {
        const int s = std::max(1, scale);
        const int sw = 22 * s, sh = 11 * s;
        const int rad = sh / 2;
        const uint32_t trackBg = on ? accentColor : 0xFF262E3E;
        const uint32_t trackBorder = on ? (accentColor | 0xFF000000) : 0xFF3D4B63;
        FillRound(x, y, sw, sh, rad, trackBg);
        OutlineRound(x, y, sw, sh, rad, 1, trackBorder);
        const int knobSz = sh - 2 * s;
        const int knobX = on ? (x + sw - knobSz - s) : (x + s);
        const int knobY = y + s;
        ShadowRound(knobX, knobY, knobSz, knobSz, knobSz / 2, s, 60);
        FillGradV(knobX, knobY, knobSz, knobSz, 0xFFFFFFFF, 0xFFD8E2EC, knobSz / 2);
        OutlineRound(knobX, knobY, knobSz, knobSz, knobSz / 2, 1, 0xFF94A3B8);
    }

    void DrawSliderBar(int x, int y, int rw, int rh, int progress, int maxProgress, uint32_t accentColor, bool focused = false)
    {
        if (rw <= 0 || rh <= 0 || maxProgress <= 0) return;
        const int rad = rh / 2;
        FillRound(x, y, rw, rh, rad, 0xFF19202E);
        OutlineRound(x, y, rw, rh, rad, 1, 0xFF2E394E);
        const int fillW = std::clamp((progress * rw) / maxProgress, 0, rw);
        if (fillW > 0)
        {
            const uint32_t fillTop = (((accentColor >> 16) & 0xFF) + 35 <= 255 ? accentColor + 0x00232323 : 0xFFFFFFFF);
            FillGradV(x, y, fillW, rh, fillTop, accentColor, rad);
            if (focused) GlowRound(x, y, fillW, rh, rad, 2, (accentColor & 0x00FFFFFF) | 0x66000000);
        }
        const int thumbSz = rh + 4;
        const int thumbX = x + std::clamp((progress * (rw - thumbSz)) / maxProgress, 0, rw - thumbSz);
        const int thumbY = y - 2;
        ShadowRound(thumbX, thumbY, thumbSz, thumbSz, thumbSz / 2, 2, 80);
        FillGradV(thumbX, thumbY, thumbSz, thumbSz, 0xFFFFFFFF, 0xFFCBD5E1, thumbSz / 2);
        OutlineRound(thumbX, thumbY, thumbSz, thumbSz, thumbSz / 2, 1, focused ? accentColor : 0xFF64748B);
    }

    // 5x7 glyphs, one row per byte, bit 4 is the leftmost pixel.
    static const uint8_t* Glyph(char c)
    {
        static const uint8_t font[][7] = {
            { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 }, // A
            { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E }, // B
            { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E }, // C
            { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E }, // D
            { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F }, // E
            { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 }, // F
            { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F }, // G
            { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 }, // H
            { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E }, // I
            { 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C }, // J
            { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 }, // K
            { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F }, // L
            { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 }, // M
            { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 }, // N
            { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E }, // O
            { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 }, // P
            { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D }, // Q
            { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 }, // R
            { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E }, // S
            { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 }, // T
            { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E }, // U
            { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 }, // V
            { 0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11 }, // W
            { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 }, // X
            { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 }, // Y
            { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F }, // Z
            { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E }, // 0
            { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E }, // 1
            { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F }, // 2
            { 0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E }, // 3
            { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 }, // 4
            { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E }, // 5
            { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E }, // 6
            { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 }, // 7
            { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E }, // 8
            { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C }, // 9
            { 0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00 }, // :
            { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 }, // -
            { 0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00 }, // +
            { 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x06 }, // .
            { 0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10 }, // /
            { 0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02 }, // <
            { 0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08 }, // >
            { 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04 }, // !
            { 0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02 }, // (
            { 0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08 }, // )
            // PlayStation's button symbols (controls.h, PS_*): cross, circle, square, triangle. Smaller
            // than the letters, so the cross and the circle are not taken for an X and an O.
            { 0x00, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x00 },
            { 0x00, 0x0E, 0x11, 0x11, 0x11, 0x0E, 0x00 },
            { 0x00, 0x1F, 0x11, 0x11, 0x11, 0x1F, 0x00 },
            { 0x00, 0x04, 0x0A, 0x0A, 0x11, 0x1F, 0x00 },
            { 0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03 }, // % (VOLUME)
        };
        if (c == '\'') { static const uint8_t apostrophe[] = { 4, 4, 8, 0, 0, 0, 0 }; return apostrophe; }
        if (c >= 'a' && c <= 'z') c -= 32;
        if (c >= 'A' && c <= 'Z') return font[c - 'A'];
        if (c >= '0' && c <= '9') return font[26 + c - '0'];
        static const char punct[] = ":-+./<>!()\x01\x02\x03\x04%";
        const char* p = c ? strchr(punct, c) : nullptr;
        return p ? font[36 + (p - punct)] : nullptr;
    }

    // (In the interface's language, UiLang: its UTF-8 letters are one character each.)
    static int TextWidth(const std::string& s, int scale)
    {
        return UiLang::Units(UiLang::Tr(s)) * scale;
    }

    static int TextWidthRaw(const std::string& text,int scale) { return UiLang::Units(text)*scale; }
    void TextRaw(int x,int y,const std::string& text,int scale,uint32_t argb) { UiText::DrawRaw(*this,x,y,text,scale,argb); }
    void Text(int x,int y,const std::string& text,int scale,uint32_t argb)
    {
        UiText::Draw(*this,x,y,text,scale,argb);
    }

};

// The accent colours of COLOUR (the game list's settings, GAME LIST): the game list's lines and
// frame (launcher.h, ThemeNamed) and the pause menu's chosen row, its box and text shaded from it.
inline uint32_t AccentNamed(const std::string& name)
{
    static const struct { const char* name; uint32_t argb; } colours[] = {
        { "red", 0xFFE52535 },      // Crimson Switch Red
        { "crimson", 0xFFE52535 },
        { "blue", 0xFF0090FF },     // Electric Azure Blue
        { "cyber", 0xFF0090FF },
        { "green", 0xFF00D668 },    // Emerald Mint
        { "emerald", 0xFF00D668 },
        { "purple", 0xFFA855F7 },   // Cosmic Amethyst
        { "amethyst", 0xFFA855F7 },
        { "gold", 0xFFFFB020 },     // Sunset Amber Gold
        { "amber", 0xFFFFB020 },
        { "pink", 0xFFFF3B88 },     // Neon Coral Pink
        { "neon_pink", 0xFFFF3B88 },
        { "teal", 0xFF00C7B5 },     // Cyan Aqua Mint
        { "cyan", 0xFF00C7B5 },
        { "obsidian", 0xFFCBD5E1 }, // Luxury Platinum Silver
        { "silver", 0xFFCBD5E1 },
    };
    for (const auto& c : colours)
        if (name == c.name) return c.argb;
    return colours[0].argb;
}

// Button presses the menu understands, edge-triggered.
struct MenuInput
{
    bool up = false, down = false, left = false, right = false, accept = false, back = false;
};

struct MenuItem
{
    std::string label;
    std::string value; // shown right-aligned; empty for plain actions
    bool info = false; // the value is only shown (no < > around it): nothing to change here
    int slider = -1;   // 0 to 10: also drawn as a bar of ten blocks, that many lit (VOLUME)
};

struct Menu
{
    bool open = false;
    int selected = 0;
    std::vector<MenuItem> items;

    // Returns the index of an item that was activated this frame, or -1.
    // dir is -1/+1 when left/right was used on it, 0 for accept.
    int Update(const MenuInput& in, int& dir)
    {
        dir = 0;
        if (!open || items.empty()) return -1;
        int n = (int)items.size();
        if (in.up) selected = (selected + n - 1) % n;
        if (in.down) selected = (selected + 1) % n;
        if (in.accept) return selected;
        if (in.left && !items[selected].value.empty())
        {
            dir = -1;
            return selected;
        }
        if (in.right && !items[selected].value.empty())
        {
            dir = 1;
            return selected;
        }
        return -1;
    }

    // Where the box and its rows were last drawn (Draw), in the canvas's pixels, and the first
    // row shown (a list longer than the screen scrolls with the chosen row: CHEATS).
    mutable int drawnX = 0, drawnY = 0, drawnW = 0, drawnH = 0, drawnLineH = 0, drawnFirstY = 0;
    mutable int scroll = 0;
    // Where a slider's bar was drawn (the same on every row): its left edge and one block's step.
    mutable int barX = 0, barStep = 0;

    // The value a tap at x on a slider's row stands for (0 to 10: the block tapped and those
    // before it; left of the bar, 0), or -1 for a tap beside the bar.
    int SliderAt(int x) const
    {
        if (barStep <= 0 || x >= barX + 10 * barStep + barStep / 2) return -1;
        if (x < barX) return x >= barX - 2 * barStep ? 0 : -1;
        return std::min(10, (x - barX) / barStep + 1);
    }

    // The row at (x, y) of the canvas the menu was drawn on (touch screens): its index, -1 for
    // a point in the box but on no row, -2 for a point outside the box.
    int ItemAt(int x, int y) const
    {
        if (drawnW <= 0 || x < drawnX || x >= drawnX + drawnW || y < drawnY || y >= drawnY + drawnH) return -2;
        if (drawnLineH <= 0 || y < drawnFirstY) return -1;
        const int i = scroll + (y - drawnFirstY) / drawnLineH;
        return i < (int)items.size() ? i : -1;
    }

    // note: a few lines of text in a strip under the box (CHEATS: what the database says about
    // the chosen cheat), wrapped to the box's width.
    // The chosen row's colour (COLOUR, AccentNamed); the box, text and bars are shaded from it.
    static inline uint32_t Accent = 0xFFE0242C;

    // boxUnits: the box's width at scale 1 (0: 190, as the other pages). CHEATS passes 260 so a
    // code's name (32 letters) fits beside "< OFF >".
    void Draw(Canvas& c, const std::string& title, const std::string& hint, const std::string& note = "", int boxUnits = 0) const
    {
        // (Each channel of the accent c: f(c) per part. With blue, 0xFF3D6FD1, it is the menu as
        // it was: the box 0x1C2A44, the text 0xC8D0E0, the hints 0x8FA0C0.)
        auto shade = [](uint32_t a, auto f) {
            const int r = f((int)(a >> 16 & 0xFF)), g = f((int)(a >> 8 & 0xFF)), b = f((int)(a & 0xFF));
            return 0xFF000000u | (uint32_t)std::clamp(r, 0, 255) << 16 | (uint32_t)std::clamp(g, 0, 255) << 8 | (uint32_t)std::clamp(b, 0, 255);
        };
        const uint32_t boxTop = shade(Accent, [](int v) { return 18 + v * 16 / 100; });
        const uint32_t boxBot = shade(Accent, [](int v) { return 12 + v * 11 / 100; });
        const uint32_t headerCol = shade(Accent, [](int v) { return 24 + v * 22 / 100; });
        const uint32_t rowCardCol = shade(Accent, [](int v) { return 22 + v * 18 / 100; });
        const uint32_t noteBox = shade(Accent, [](int v) { return 14 + v * 14 / 100; });
        const uint32_t borderCol = shade(Accent, [](int v) { return 55 + v * 45 / 100; });
        const uint32_t selTop = shade(Accent, [](int v) { return std::min(254, v + (255 - v) * 14 / 100); });
        const uint32_t selBot = shade(Accent, [](int v) { return v * 85 / 100; });
        const uint32_t selBorder = shade(Accent, [](int v) { return std::min(254, v + (255 - v) * 38 / 100); });
        const uint32_t textCol = shade(Accent, [](int v) { return 195 + v * 15 / 100; });
        const uint32_t hintCol = shade(Accent, [](int v) { return 130 + v * 32 / 100; });
        const uint32_t barOffSel = shade(Accent, [](int v) { return v + (255 - v) * 3 / 10; });
        const uint32_t barOff = shade(Accent, [](int v) { return 35 + v * 35 / 100; });
        int scale = std::min(c.w, c.h) / 240;
        if (scale < 1) scale = 1;
        int lineH = (UiLang::Tall() ? 16 : 11) * scale;
        // (Rows closer together if they would not all fit on the screen, down to 9 * scale.)
        // With no title the box starts with the first item.
        const bool hasTitle = !title.empty();
        // (A value as drawn: translated before its arrows go round it, as the whole would not be
        // in the language's table.)
        auto shownValue = [](const MenuItem& item) { return item.info ? item.value : "< " + UiLang::Tr(item.value) + " >"; };
        int boxW = (boxUnits > 0 ? boxUnits : 190) * scale;
        if (UiLang::lang)
            for (const auto& item : items)
                boxW = std::max(boxW, Canvas::TextWidth(item.label, scale) + (item.value.empty() ? 0 : Canvas::TextWidth(shownValue(item), scale)) + 20 * scale);
        if (boxW > c.w - 8) boxW = c.w - 8;
        // The note's lines (up to 4, the last cut short), and the box moved up to make room.
        std::vector<std::string> noteLines;
        if (!note.empty())
        {
            const int maxW = boxW - 12 * scale;
            noteLines = UiLang::Wrap(UiLang::Tr(note), maxW / scale);
            if (noteLines.size() > 4) { noteLines.resize(4); noteLines[3] = UiLang::Elide(noteLines[3]+"...",maxW/scale); }
        }
        const int noteH = noteLines.empty() ? 0 : (int)noteLines.size() * UiLang::LineUnits() * scale + 6 * scale;
        // (As many rows as fit at the smallest spacing; the rest scroll into view.)
        const int fit = std::max(1, (c.h - 4 - 6 * scale - (noteH ? noteH + 3 * scale : 0)) / ((UiLang::Tall() ? 14 : 9) * scale) - (hasTitle ? 3 : 2));
        const int shown = std::min((int)items.size(), fit);
        if (selected < scroll) scroll = selected;
        if (selected >= scroll + shown) scroll = selected - shown + 1;
        scroll = std::max(0, std::min(scroll, (int)items.size() - shown));
        // (The note's strip sits under the box, so the box leaves room for it: at 800x480 it was cut off.)
        const int availH = c.h - 4 - (noteH ? noteH + 3 * scale : 0);
        const int rows = shown + (hasTitle ? 3 : 2);
        if (lineH * rows + 6 * scale > availH) lineH = std::max(UiLang::LineUnits() * scale, (availH - 6 * scale) / rows);
        int boxH = lineH * rows + 6 * scale;
        int bx = (c.w - boxW) / 2, by = std::max(2, (c.h - boxH - noteH) / 2);
        // (Kept for ItemAt: where the rows were drawn.)
        drawnX = bx;
        drawnY = by;
        drawnW = boxW;
        drawnH = boxH;
        drawnLineH = lineH;
        drawnFirstY = by + (hasTitle ? lineH : 0) + 5 * scale - 2 * scale;

        if (!c.px.empty() && (c.px[0] >> 24) != 0)
            c.FillBlend(0, 0, c.w, c.h, 0xBC05070D);
        else
            c.Fill(0, 0, c.w, c.h, 0xB405070D);
        const_cast<Canvas&>(c).FillVignette(70);

        const int rad = std::max(3, 5 * scale);
        const_cast<Canvas&>(c).ShadowRound(bx, by, boxW, boxH, rad, 3 * scale, 110);
        const_cast<Canvas&>(c).GlowRound(bx, by, boxW, boxH, rad, 2 * scale, (Accent & 0x00FFFFFF) | 0x33000000u);
        const_cast<Canvas&>(c).FillGlass(bx, by, boxW, boxH, rad, boxTop, boxBot, borderCol, std::max(1, scale / 2));
        if (hasTitle)
        {
            const int hdrH = lineH + 3 * scale;
            c.FillGradV(bx + scale, by + scale, boxW - 2 * scale, hdrH, headerCol + 0x0006080C, headerCol, std::max(1, rad - scale));
            c.Fill(bx + 6 * scale, by + hdrH, boxW - 12 * scale, std::max(1, scale / 2), Accent);
            c.Text(bx + (boxW - Canvas::TextWidth(title, scale)) / 2, by + 4 * scale, title, scale, 0xFFFFE58F);
        }

        int y = by + (hasTitle ? lineH : 0) + 5 * scale;
        if (scroll > 0) c.Text(bx + boxW - 8 * scale, y - lineH + 2 * scale, "+", scale, hintCol);
        if (scroll + shown < (int)items.size()) c.Text(bx + boxW - 8 * scale, y + shown * lineH - 2 * scale, "+", scale, hintCol);
        const int rowRad = std::max(2, 3 * scale);
        for (int i = scroll; i < scroll + shown; i++)
        {
            bool sel = i == selected;
            if (sel)
            {
                const_cast<Canvas&>(c).GlowRound(bx + 2 * scale, y - 2 * scale, boxW - 4 * scale, lineH, rowRad, scale, (Accent & 0x00FFFFFF) | 0x55000000u);
                c.FillGradV(bx + 2 * scale, y - 2 * scale, boxW - 4 * scale, lineH, selTop, selBot, rowRad);
                c.OutlineRound(bx + 2 * scale, y - 2 * scale, boxW - 4 * scale, lineH, rowRad, std::max(1, scale / 2), selBorder);
                c.FillRound(bx + 3 * scale, y - scale, std::max(2, scale), lineH - 2 * scale, std::max(1, scale / 2), 0xFFFFFFFF);
            }
            else if (((i - scroll) & 1) == 1)
            {
                c.FillRound(bx + 3 * scale, y - 2 * scale, boxW - 6 * scale, lineH - scale, rowRad, rowCardCol);
            }
            uint32_t col = sel ? 0xFFFFFFFF : textCol;
            c.Text(bx + 7 * scale, y, items[i].label, scale, col);
            if (!items[i].value.empty())
            {
                const std::string v = shownValue(items[i]);
                const int vx = bx + boxW - 6 * scale - Canvas::TextWidth(items[i].slider >= 0 ? "< 100% >" : v, scale);
                c.Text(items[i].slider >= 0 ? bx + boxW - 6 * scale - Canvas::TextWidth(v, scale) : vx, y, v, scale, col);
                if (items[i].slider >= 0)
                {
                    barStep = 5 * scale;
                    barX = vx - 4 * scale - 10 * barStep;
                    const int barW = 10 * barStep - scale;
                    const int barH = 7 * scale;
                    const_cast<Canvas&>(c).DrawSliderBar(barX, y, barW, barH, items[i].slider, 10, sel ? 0xFFFFFFFF : Accent, sel);
                }
            }
            y += lineH;
        }
        c.Fill(bx + 6 * scale, y + scale, boxW - 12 * scale, std::max(1, scale / 2), borderCol);
        c.Text(bx + (boxW - Canvas::TextWidth(hint, scale)) / 2, y + 4 * scale, hint, scale, hintCol);
        if (!noteLines.empty())
        {
            const int ny = by + boxH + 3 * scale;
            const_cast<Canvas&>(c).ShadowRound(bx, ny, boxW, noteH, rad, 2 * scale, 90);
            c.FillGradV(bx, ny, boxW, noteH, noteBox, boxBot, rad);
            c.OutlineRound(bx, ny, boxW, noteH, rad, std::max(1, scale / 2), borderCol);
            for (size_t i = 0; i < noteLines.size(); i++)
                c.TextRaw(bx + 6 * scale, ny + 4 * scale + (int)i * UiLang::LineUnits() * scale, noteLines[i], scale, textCol);
        }
    }
};
