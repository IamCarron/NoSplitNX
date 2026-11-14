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
#include "launcher_font.h"

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

    void FillGradV(int x, int y, int rw, int rh, uint32_t topArgb, uint32_t botArgb, int radius = 0)
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
        const int step = std::max(8, cornerSteps / 6);
        for (int i = 0; i < cornerSteps; i += step)
        {
            const uint32_t alpha = (uint32_t)(intensity * (cornerSteps - i)) / (uint32_t)cornerSteps;
            if (alpha > 0)
                OutlineRound(i, i, w - 2 * i, h - 2 * i, cornerSteps - i, step, (alpha << 24));
        }
    }

    static inline int ModernCharAdvance(char ch, uint32_t code, float sc)
    {
        if (ch != 0)
            return std::max(1, (int)std::round(FontMetrics[(unsigned char)ch].advance * sc));
        if (code != 0)
        {
            if (const UiFont::Glyph* ug = UiFont::Find(code))
                return std::max(1, (int)std::round((float)ug->advance * 2.0f * sc));
            return std::max(1, (int)std::round(FontMetrics[(unsigned char)'?'].advance * sc));
        }
        return std::max(1, (int)std::round(FontMetrics[(unsigned char)' '].advance * sc));
    }

    static inline int ModernTextWidth(const std::string& text, int targetH)
    {
        if (text.empty() || targetH <= 0) return 0;
        const float sc = (float)targetH / 32.0f;
        int tw = 0;
        for (size_t i = 0; i < text.size(); i++)
        {
            uint32_t code = 0;
            auto [ch, mark] = DecodeChar(text, i, &code);
            (void)mark;
            tw += ModernCharAdvance(ch, code, sc);
        }
        return tw;
    }

    void ModernText(int x, int y, const std::string& text, int targetH, uint32_t colour, bool shadow = false)
    {
        if (text.empty() || targetH <= 0) return;
        const float sc = (float)targetH / 32.0f;
        const int renderH = (int)std::round(32.0f * sc);

        auto blendPx = [&](int px, int py, uint32_t cr, uint32_t cg, uint32_t cb, unsigned a) {
            if (a == 0 || px < 0 || px >= w || py < 0 || py >= h) return;
            uint32_t& dst = this->px[(size_t)py * this->w + px];
            const unsigned dr = (dst >> 16) & 0xFF;
            const unsigned dg = (dst >> 8) & 0xFF;
            const unsigned db = dst & 0xFF;
            const unsigned r = (cr * a + dr * (255 - a)) / 255;
            const unsigned g = (cg * a + dg * (255 - a)) / 255;
            const unsigned b = (cb * a + db * (255 - a)) / 255;
            dst = 0xFF000000u | (r << 16) | (g << 8) | b;
        };

        auto drawGlyph = [&](int gx, int gy, unsigned char ch, uint32_t col, float alphaMul, int mark) {
            if (ch >= 128) ch = '?';
            const GlyphMetric& m = FontMetrics[ch];
            if (m.width == 0 && mark == 0) return;
            const int renderW = std::max(1, (int)std::round(m.width * sc));
            const uint32_t cr = (col >> 16) & 0xFF;
            const uint32_t cg = (col >> 8) & 0xFF;
            const uint32_t cb = col & 0xFF;

            if (ch == 7) // Antialiased centered bullet dot
            {
                const float cx = gx + renderW * 0.5f;
                const float cy = gy + renderH * 0.52f;
                const float r = std::max(1.5f, 2.0f * sc);
                const int minX = std::max(0, (int)std::floor(cx - r - 1.0f));
                const int maxX = std::min(w - 1, (int)std::ceil(cx + r + 1.0f));
                const int minY = std::max(0, (int)std::floor(cy - r - 1.0f));
                const int maxY = std::min(h - 1, (int)std::ceil(cy + r + 1.0f));
                for (int py = minY; py <= maxY; ++py)
                {
                    for (int px = minX; px <= maxX; ++px)
                    {
                        const float fdx = px + 0.5f - cx;
                        const float fdy = py + 0.5f - cy;
                        const float dist = std::sqrt(fdx * fdx + fdy * fdy);
                        if (dist > r + 0.5f) continue;
                        const float cov = (dist <= r - 0.5f) ? 1.0f : (r + 0.5f - dist);
                        blendPx(px, py, cr, cg, cb, (unsigned)(cov * 255.0f * alphaMul));
                    }
                }
                return;
            }

            for (int dy = 0; dy < renderH; ++dy)
            {
                const int py = gy + dy;
                if (py < 0 || py >= h) continue;
                const float v0 = (float)dy * 32.0f / (float)renderH;
                const float v1 = (float)(dy + 1) * 32.0f / (float)renderH;
                const int sy0 = std::clamp((int)std::floor(v0), 0, 31);
                const int sy1 = std::clamp((int)std::ceil(v1), sy0 + 1, 32);

                for (int dx = 0; dx < renderW; ++dx)
                {
                    const int px = gx + dx;
                    if (px < 0 || px >= w) continue;
                    const float u0 = (float)dx * (float)m.width / (float)renderW;
                    const float u1 = (float)(dx + 1) * (float)m.width / (float)renderW;
                    const int sx0 = std::clamp(m.left + (int)std::floor(u0), 0, 23);
                    const int sx1 = std::clamp(m.left + (int)std::ceil(u1), sx0 + 1, 24);

                    unsigned sumA = 0;
                    int count = 0;
                    for (int sy = sy0; sy < sy1; ++sy)
                    {
                        const int rowOffset = ch * 768 + sy * 24;
                        for (int sx = sx0; sx < sx1; ++sx)
                        {
                            sumA += LauncherFont[rowOffset + sx];
                            count++;
                        }
                    }
                    if (sumA == 0) continue;
                    const float avgA = (float)sumA / (float)count;
                    const float norm = std::clamp(std::pow(avgA / 255.0f, 0.75f) * 1.25f, 0.0f, 1.0f);
                    blendPx(px, py, cr, cg, cb, (unsigned)(norm * 255.0f * alphaMul));
                }
            }

            const bool isLower = (ch >= 'a' && ch <= 'z');
            const int glyphTopDy = isLower ? (int)std::round(9.0f * sc) : (int)std::round(4.0f * sc);
            const unsigned a = (unsigned)(255.0f * alphaMul);
            const int ah = std::max(2, (int)std::round(3.5f * sc));
            const int ax = gx + (renderW / 2);
            const int baseAy = gy + glyphTopDy - std::max(1, (int)(1.5f * sc));

            if (mark == 1) // acute accent
            {
                for (int dy = 0; dy < ah; dy++)
                    blendPx(ax + dy - (ah / 2), baseAy - dy, cr, cg, cb, a);
            }
            else if (mark == 2) // tilde
            {
                const int tw = std::max(4, (int)std::round(6.0f * sc));
                const int tx = gx + (renderW - tw) / 2;
                const int ty = gy + glyphTopDy - std::max(2, (int)(2.0f * sc));
                for (int dx = 0; dx < tw; dx++)
                    blendPx(tx + dx, ty + ((dx < tw / 2) ? -1 : 0), cr, cg, cb, a);
            }
            else if (mark == 3) // grave accent
            {
                for (int dy = 0; dy < ah; dy++)
                    blendPx(ax - dy + (ah / 2), baseAy - dy, cr, cg, cb, a);
            }
            else if (mark == 4) // circumflex
            {
                const int half = std::max(2, (int)std::round(2.5f * sc));
                const int cx = gx + (renderW / 2);
                const int cy = baseAy - std::max(1, (int)std::round(1.5f * sc));
                for (int dx = -half; dx <= half; dx++)
                    blendPx(cx + dx, cy + (std::abs(dx) + 1) / 2, cr, cg, cb, a);
            }
            else if (mark == 5) // diaeresis / umlaut
            {
                const int gap = std::max(2, (int)std::round(2.2f * sc));
                const int dotSz = sc >= 0.72f ? 2 : 1;
                const int cy = gy + glyphTopDy - std::max(2, (int)std::round(2.5f * sc));
                for (int side : { -gap, gap })
                    for (int dy = 0; dy < dotSz; dy++)
                        for (int dx = 0; dx < dotSz; dx++)
                            blendPx(ax + side - dotSz / 2 + dx, cy - dy, cr, cg, cb, a);
            }
            else if (mark == 6) // cedilla
            {
                const int stemY = gy + (int)std::round((isLower ? 24.5f : 25.0f) * sc);
                const int stemH = std::max(2, (int)std::round(3.0f * sc));
                for (int dy = 0; dy < stemH; dy++)
                    blendPx(ax, stemY + dy, cr, cg, cb, a);
                blendPx(ax - 1, stemY + stemH - 1, cr, cg, cb, a);
                if (stemH >= 3) blendPx(ax - 2, stemY + stemH - 2, cr, cg, cb, a);
            }
        };

        auto drawUnicodeGlyph = [&](int gx, int gy, uint32_t code, uint32_t col, float alphaMul) {
            const UiFont::Glyph* ug = UiFont::Find(code);
            if (!ug)
            {
                drawGlyph(gx, gy, '?', col, alphaMul, 0);
                return;
            }
            const int adv = std::max(1, (int)ug->advance);
            const int drawW = std::max(1, (int)std::round((float)adv * 2.0f * sc));
            const int drawH = std::max(1, (int)std::round(28.0f * sc));
            const int baseGy = gy + (int)std::round(2.0f * sc);
            const uint32_t cr = (col >> 16) & 0xFF;
            const uint32_t cg = (col >> 8) & 0xFF;
            const uint32_t cb = col & 0xFF;

            for (int dy = 0; dy < drawH; ++dy)
            {
                const int py = baseGy + dy;
                if (py < 0 || py >= h) continue;
                const int sy0 = std::clamp((int)std::floor((float)dy * 14.0f / (float)drawH), 0, 13);
                const int sy1 = std::clamp((int)std::ceil((float)(dy + 1) * 14.0f / (float)drawH), sy0 + 1, 14);
                for (int dx = 0; dx < drawW; ++dx)
                {
                    const int px = gx + dx;
                    if (px < 0 || px >= w) continue;
                    const int sx0 = std::clamp((int)std::floor((float)dx * (float)adv / (float)drawW), 0, std::max(0, adv - 1));
                    const int sx1 = std::clamp((int)std::ceil((float)(dx + 1) * (float)adv / (float)drawW), sx0 + 1, adv);
                    int on = 0, count = 0;
                    for (int sy = sy0; sy < sy1; ++sy)
                    {
                        const uint16_t rowBits = ug->rows[sy];
                        for (int sx = sx0; sx < sx1; ++sx)
                        {
                            on += (rowBits >> (11 - sx)) & 1;
                            count++;
                        }
                    }
                    if (on == 0 || count == 0) continue;
                    const float cov = (float)on / (float)count;
                    const float norm = std::clamp(std::pow(cov, 0.72f) * 1.15f, 0.0f, 1.0f);
                    blendPx(px, py, cr, cg, cb, (unsigned)(norm * 255.0f * alphaMul));
                }
            }
        };

        auto drawRun = [&](int startX, int startY, uint32_t col, float alphaMul) {
            int curX = startX;
            for (size_t i = 0; i < text.size(); i++)
            {
                uint32_t code = 0;
                auto [ch, mark] = DecodeChar(text, i, &code);
                if (ch != 0)
                    drawGlyph(curX, startY, (unsigned char)ch, col, alphaMul, mark);
                else if (code != 0)
                    drawUnicodeGlyph(curX, startY, code, col, alphaMul);
                curX += ModernCharAdvance(ch, code, sc);
            }
        };

        if (shadow) drawRun(x + 1, y + (targetH >= 20 ? 2 : 1), 0xFF000000u, 0.65f);
        drawRun(x, y, colour, 1.0f);
    }

    int ButtonBadgeWidth(const std::string& label, int scale) const
    {
        const int s = std::max(1, scale);
        const int fontH = std::max(12, 5 * s + 1);
        const int tw = ModernTextWidth(label, fontH);
        const bool single = label.size() <= 1 || label == "+" || label == "-";
        const int bh = 12 * s;
        return single ? bh : tw + 8 * s;
    }

    static uint32_t ShadeArgb(uint32_t col, int delta)
    {
        const uint32_t a = col & 0xFF000000u;
        const uint32_t r = (uint32_t)std::clamp((int)((col >> 16) & 0xFF) + delta, 0, 255);
        const uint32_t g = (uint32_t)std::clamp((int)((col >> 8) & 0xFF) + delta, 0, 255);
        const uint32_t b = (uint32_t)std::clamp((int)(col & 0xFF) + delta, 0, 255);
        return a | (r << 16) | (g << 8) | b;
    }

    void DrawButtonBadge(int x, int y, const std::string& label, int scale, uint32_t badgeColor = 0xFF2A344A, uint32_t textCol = 0xFFFFFFFF, uint32_t borderCol = 0xFF455577)
    {
        const int s = std::max(1, scale);
        const int fontH = std::max(12, 5 * s + 1);
        const int tw = ModernTextWidth(label, fontH);
        const int bh = 12 * s;
        const int bw = ButtonBadgeWidth(label, s);
        const int rad = bh / 2;
        const uint32_t topBevel = ShadeArgb(badgeColor, 30);
        const uint32_t botBevel = ShadeArgb(badgeColor, -25);
        FillGradV(x, y, bw, bh, topBevel, botBevel, rad);
        OutlineRound(x, y, bw, bh, rad, 1, borderCol);
        if (bh > 4) FillBlend(x + s + 1, y + 1, bw - 2 * (s + 1), 1, 0x33FFFFFF);
        ModernText(x + (bw - tw) / 2, y + (bh - fontH) / 2, label, fontH, textCol, false);
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
            const uint32_t fillTop = ShadeArgb(accentColor, 35);
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
    static inline uint32_t Accent = 0xFF0090FF;

    // boxUnits: the box's width at scale 1 (0: 190, as the other pages). CHEATS passes 260 so a
    // code's name (32 letters) fits beside "< OFF >".
    void Draw(Canvas& c, const std::string& title, const std::string& hint, const std::string& note = "", int boxUnits = 0, bool dimBackdrop = true) const
    {
        // (Each channel of the accent c: f(c) per part. With blue, 0xFF3D6FD1, it is the menu as
        // it was: the box 0x1C2A44, the text 0xC8D0E0, the hints 0x8FA0C0.)
        auto shade = [](uint32_t a, auto f) {
            const int r = f((int)(a >> 16 & 0xFF)), g = f((int)(a >> 8 & 0xFF)), b = f((int)(a & 0xFF));
            return 0xFF000000u | (uint32_t)std::clamp(r, 0, 255) << 16 | (uint32_t)std::clamp(g, 0, 255) << 8 | (uint32_t)std::clamp(b, 0, 255);
        };
        const uint32_t boxBot = shade(Accent, [](int v) { return 12 + v * 11 / 100; });
        const uint32_t headerCol = shade(Accent, [](int v) { return 24 + v * 22 / 100; });
        const uint32_t noteBox = shade(Accent, [](int v) { return 14 + v * 14 / 100; });
        const uint32_t borderCol = 0xFF2A384E;
        const uint32_t textCol = shade(Accent, [](int v) { return 195 + v * 15 / 100; });
        const uint32_t hintCol = shade(Accent, [](int v) { return 130 + v * 32 / 100; });
        int scale = std::min(c.w, c.h) / 240;
        if (scale < 1) scale = 1;
        const int itemFontH = std::max(20, 11 * scale);
        int lineH = std::max(28, (UiLang::Tall() ? 19 : 17) * scale);
        // (Rows closer together if they would not all fit on the screen, down to 9 * scale.)
        // With no title the box starts with the first item.
        const bool hasTitle = !title.empty();
        // (A value as drawn: translated before its arrows go round it, as the whole would not be
        // in the language's table.)
        auto shownValue = [](const MenuItem& item) { return item.info ? UiLang::Tr(item.value) : "< " + UiLang::Tr(item.value) + " >"; };
        int boxW = (boxUnits > 0 ? std::max(boxUnits, 340) : 320) * scale;
        for (const auto& item : items)
        {
            const std::string trL = ToNiceTitle(UiLang::Tr(item.label));
            const std::string v = item.value.empty() ? "" : ToNiceTitle(shownValue(item));
            const int modernW = c.ModernTextWidth(trL, itemFontH) + (item.value.empty() ? 0 : c.ModernTextWidth(v, itemFontH) + 16 * scale) + 40 * scale;
            const int legacyW = Canvas::TextWidth(item.label, scale) + (item.value.empty() ? 0 : Canvas::TextWidth(shownValue(item), scale)) + 28 * scale;
            boxW = std::max({boxW, modernW, legacyW});
        }
        if (hasTitle)
        {
            const std::string niceTitle = (title == "NoSplitNX") ? title : ToNiceTitle(UiLang::Tr(title));
            const int titleFontH = std::max(22, 13 * scale);
            const int titleW = c.ModernTextWidth(niceTitle, titleFontH);
            boxW = std::max(boxW, titleW + 54 * scale);
        }
        if (boxW > c.w - 16) boxW = c.w - 16;
        // The note's lines (up to 4, the last cut short), and the box moved up to make room.
        std::vector<std::string> noteLines;
        const int noteFontH = std::max(16, 7 * scale);
        if (!note.empty())
        {
            const int maxW = boxW - 20 * scale;
            const int maxChars = std::max(10, maxW / (7 * scale));
            noteLines = UiLang::Wrap(UiLang::Tr(note), maxChars * 6);
            if (noteLines.size() > 4) { noteLines.resize(4); noteLines[3] = UiLang::Elide(noteLines[3] + "...", maxChars); }
        }
        const int noteH = noteLines.empty() ? 0 : (int)noteLines.size() * (noteFontH + 3 * scale) + 8 * scale;
        // (As many rows as fit at the smallest spacing; the rest scroll into view.)
        const int fit = std::max(1, (c.h - 4 - 6 * scale - (noteH ? noteH + 3 * scale : 0)) / ((UiLang::Tall() ? 15 : 12) * scale) - (hasTitle ? 2 : 1));
        const int shown = std::min((int)items.size(), fit);
        if (selected < scroll) scroll = selected;
        if (selected >= scroll + shown) scroll = selected - shown + 1;
        scroll = std::max(0, std::min(scroll, (int)items.size() - shown));
        // (The note's strip sits under the box, so the box leaves room for it: at 800x480 it was cut off.)
        const int availH = c.h - 4 - (noteH ? noteH + 3 * scale : 0);
        const int rows = shown + (hasTitle ? 2 : 1);
        if (lineH * rows + 6 * scale > availH) lineH = std::max(itemFontH + 4 * scale, (availH - 6 * scale) / rows);
        int boxH = lineH * rows + 6 * scale;
        int bx = (c.w - boxW) / 2, by = std::max(2, (c.h - boxH - noteH) / 2);
        // (Kept for ItemAt: where the rows were drawn.)
        drawnX = bx;
        drawnY = by;
        drawnW = boxW;
        drawnH = boxH;
        drawnLineH = lineH;
        drawnFirstY = by + (hasTitle ? lineH : 0) + 5 * scale - 2 * scale;

        if (dimBackdrop)
        {
            if (!c.px.empty() && (c.px[0] >> 24) != 0)
                c.FillBlend(0, 0, c.w, c.h, 0xBC05070D);
            else
                c.Fill(0, 0, c.w, c.h, 0xB405070D);
            const_cast<Canvas&>(c).FillVignette(70);
        }

        const int rad = std::max(4, 5 * scale);
        const_cast<Canvas&>(c).ShadowRound(bx, by, boxW, boxH, rad, 5 * scale, 130);
        const_cast<Canvas&>(c).FillGlass(bx, by, boxW, boxH, rad, 0xF8151D2C, 0xF80B0F18, 0xFF28364C, 1);
        if (hasTitle)
        {
            const std::string niceTitle = (title == "NoSplitNX") ? title : ToNiceTitle(UiLang::Tr(title));
            const int hdrH = lineH + 3 * scale;
            c.FillGradV(bx + scale, by + scale, boxW - 2 * scale, hdrH, headerCol + 0x0006080C, headerCol, std::max(1, rad - scale));
            c.Fill(bx + 6 * scale, by + hdrH, boxW - 12 * scale, std::max(1, scale / 2), 0xFF26354A);
            const int sw = 8 * scale, sh = 5 * scale, gap = scale;
            const int titleFontH = std::max(22, 13 * scale);
            const int titleW = c.ModernTextWidth(niceTitle, titleFontH);
            const int contentW = sw + 6 * scale + titleW;
            const int startX = bx + (boxW - contentW) / 2;
            const int logoX = startX, logoY = by + scale + (hdrH - (sh * 2 + gap)) / 2;
            c.OutlineRound(logoX, logoY, sw, sh, scale, 1, 0xFFFFFFFF);
            c.FillBlend(logoX + scale, logoY + scale, sw - 2 * scale, sh - 2 * scale, 0x33FFFFFF);
            c.OutlineRound(logoX, logoY + sh + gap, sw, sh, scale, 1, 0xFFFFFFFF);
            c.FillBlend(logoX + scale, logoY + sh + gap + scale, sw - 2 * scale, sh - 2 * scale, 0x66FFFFFF);
            c.ModernText(startX + sw + 6 * scale, by + scale + (hdrH - titleFontH) / 2, niceTitle, titleFontH, 0xFFFFFFFF);
        }

        int y = by + (hasTitle ? lineH : 0) + 5 * scale;
        if (scroll > 0) c.ModernText(bx + boxW - 14 * scale, y - lineH, "^", itemFontH, hintCol);
        if (scroll + shown < (int)items.size()) c.ModernText(bx + boxW - 14 * scale, y + shown * lineH - 2 * scale, "v", itemFontH, hintCol);
        const int rowRad = std::max(3, 4 * scale);
        const float t = AnimTime();
        const float pulse = 0.5f + 0.5f * sinf(t * 3.5f);
        for (int i = scroll; i < scroll + shown; i++)
        {
            const bool sel = i == selected;
            const int rx = bx + 5 * scale;
            const int ry = y - scale;
            const int rw = boxW - 10 * scale;
            const int rh = lineH - scale;

            if (sel)
            {
                const uint32_t glowAlpha = (uint32_t)(45 + 35 * pulse) << 24;
                const_cast<Canvas&>(c).GlowRound(rx, ry, rw, rh, rowRad, scale, (Accent & 0x00FFFFFF) | glowAlpha);
                c.FillGlass(rx, ry, rw, rh, rowRad, 0xFF20324E, 0xFF142034, 0xFF00E5FF, 1);
                c.FillRound(rx + 2 * scale, ry + 2 * scale, std::max(2, 2 * scale), rh - 4 * scale, scale, 0xFF00E5FF);
            }
            else
            {
                c.FillRound(rx, ry, rw, rh, rowRad, 0x45162234); c.OutlineRound(rx, ry, rw, rh, rowRad, 1, 0x5026364C);
            }
            const uint32_t col = sel ? 0xFFFFFFFF : 0xFFCAD4E4;
            const std::string trLabel = ToNiceTitle(UiLang::Tr(items[i].label));
            c.ModernText(rx + 10 * scale, ry + (rh - itemFontH) / 2, trLabel, itemFontH, col);
            if (!items[i].value.empty())
            {
                const std::string v = ToNiceTitle(shownValue(items[i]));
                const int textW = c.ModernTextWidth(v, itemFontH);
                const int valW = items[i].slider >= 0 ? c.ModernTextWidth("< 100% >", itemFontH) : textW;
                const int vx = rx + rw - 10 * scale - valW;
                const int textVx = items[i].slider >= 0 ? rx + rw - 10 * scale - textW : vx;
                const int pillW = textW + 10 * scale;
                const int pillX = textVx - 5 * scale;
                const int pillY = ry + 2 * scale;
                const int pillH = rh - 4 * scale;
                const int pillRad = std::max(3, pillH / 3);
                c.FillRound(pillX, pillY, pillW, pillH, pillRad, sel ? 0xFF284166 : 0xB0182438);
                c.OutlineRound(pillX, pillY, pillW, pillH, pillRad, 1, sel ? 0x9900E5FF : 0x802E3F5A);
                c.ModernText(textVx, ry + (rh - itemFontH) / 2, v, itemFontH, sel ? 0xFFFFFFFF : 0xFFD0DCF0);
                if (items[i].slider >= 0)
                {
                    barStep = 6 * scale;
                    barX = vx - 8 * scale - 10 * barStep;
                    const int barW = 10 * barStep - scale;
                    const int barH = 8 * scale;
                    const_cast<Canvas&>(c).DrawSliderBar(barX, ry + (rh - barH) / 2, barW, barH, items[i].slider, 10, sel ? 0xFFFFFFFF : Accent, sel);
                }
            }
            y += lineH;
        }
        if (!hint.empty()) c.Fill(bx + 6 * scale, y + scale, boxW - 12 * scale, std::max(1, scale / 2), 0xFF26354A);
        const int hintFontH = std::max(16, 8 * scale);
        const std::string niceHint = ToNiceTitle(UiLang::Tr(hint));
        const int footerAreaH = (by + boxH) - (y + scale);
        struct HintBadge { std::string key, label; };
        std::vector<HintBadge> badges;
        if (!hint.empty())
        {
            const std::string trRaw = UiLang::Tr(hint);
            size_t pos = 0;
            while (pos < trRaw.size())
            {
                while (pos < trRaw.size() && trRaw[pos] == ' ') pos++;
                if (pos >= trRaw.size()) break;
                size_t nextSep = trRaw.find("  ", pos);
                std::string part = (nextSep == std::string::npos) ? trRaw.substr(pos) : trRaw.substr(pos, nextSep - pos);
                pos = (nextSep == std::string::npos) ? trRaw.size() : nextSep;
                size_t colon = part.find(':');
                if (colon == std::string::npos || colon == 0 || colon > 5) { badges.clear(); break; }
                std::string k = part.substr(0, colon);
                std::string l = part.substr(colon + 1);
                size_t ls = l.find_first_not_of(' ');
                if (ls == std::string::npos) { badges.clear(); break; }
                badges.push_back({ k, ToNiceTitle(l.substr(ls)) });
            }
        }
        const int badgeScale = std::max(1, scale - (scale >= 3 ? 1 : 0));
        const int bh = 12 * badgeScale;
        bool drewBadges = false;
        if (badges.size() >= 2 && footerAreaH >= bh + 2 * scale)
        {
            int totalW = 0;
            const int gap = 14 * scale;
            for (size_t i = 0; i < badges.size(); i++)
            {
                totalW += c.ButtonBadgeWidth(badges[i].key, badgeScale) + 4 * scale + c.ModernTextWidth(badges[i].label, hintFontH);
                if (i + 1 < badges.size()) totalW += gap;
            }
            if (totalW <= boxW - 16 * scale)
            {
                int hx = bx + (boxW - totalW) / 2;
                const int hy = y + scale + (footerAreaH - bh) / 2;
                for (size_t i = 0; i < badges.size(); i++)
                {
                    const int bw = c.ButtonBadgeWidth(badges[i].key, badgeScale);
                    const_cast<Canvas&>(c).DrawButtonBadge(hx, hy, badges[i].key, badgeScale, 0xFF222B3D, 0xFFFFFFFF, 0xFF3D4B66);
                    c.ModernText(hx + bw + 4 * scale, y + scale + (footerAreaH - hintFontH) / 2, badges[i].label, hintFontH, hintCol);
                    hx += bw + 4 * scale + c.ModernTextWidth(badges[i].label, hintFontH) + gap;
                }
                drewBadges = true;
            }
        }
        if (!drewBadges)
        {
            const int hw = c.ModernTextWidth(niceHint, hintFontH);
            c.ModernText(bx + (boxW - hw) / 2, y + scale + std::max(2 * scale, (footerAreaH - hintFontH) / 2), niceHint, hintFontH, hintCol);
        }
        if (!noteLines.empty())
        {
            const int ny = by + boxH + 4 * scale;
            const_cast<Canvas&>(c).ShadowRound(bx, ny, boxW, noteH, rad, 2 * scale, 90);
            c.FillGradV(bx, ny, boxW, noteH, noteBox, boxBot, rad);
            c.OutlineRound(bx, ny, boxW, noteH, rad, std::max(1, scale / 2), borderCol);
            for (size_t i = 0; i < noteLines.size(); i++)
                c.ModernText(bx + 10 * scale, ny + 4 * scale + (int)i * (noteFontH + 3 * scale), noteLines[i], noteFontH, textCol);
        }
    }
};
