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

    void FillGradH(int x, int y, int rw, int rh, uint32_t leftArgb, uint32_t rightArgb)
    {
        if (rw <= 0 || rh <= 0) return;
        int y0 = std::max(0, y), y1 = std::min(h, y + rh);
        int x0 = std::max(0, x), x1 = std::min(w, x + rw);
        if (x1 <= x0 || y1 <= y0) return;
        if (x0 < x0d) x0d = x0;
        if (y0 < y0d) y0d = y0;
        if (x1 > x1d) x1d = x1;
        if (y1 > y1d) y1d = y1;
        const int span = std::max(1, rw - 1);
        std::vector<uint32_t> cols((size_t)(x1 - x0));
        for (int xx = x0; xx < x1; xx++)
        {
            const int dx = xx - x, inv = span - dx;
            const uint32_t a = (((leftArgb >> 24) & 0xFF) * inv + ((rightArgb >> 24) & 0xFF) * dx) / span;
            const uint32_t r = (((leftArgb >> 16) & 0xFF) * inv + ((rightArgb >> 16) & 0xFF) * dx) / span;
            const uint32_t g = (((leftArgb >> 8) & 0xFF) * inv + ((rightArgb >> 8) & 0xFF) * dx) / span;
            const uint32_t b = ((leftArgb & 0xFF) * inv + (rightArgb & 0xFF) * dx) / span;
            cols[(size_t)(xx - x0)] = (a << 24) | (r << 16) | (g << 8) | b;
        }
        for (int yy = y0; yy < y1; yy++)
        {
            uint32_t* row = &px[(size_t)yy * w];
            for (int xx = x0; xx < x1; xx++)
            {
                const uint32_t col = cols[(size_t)(xx - x0)];
                const uint32_t a = col >> 24;
                if (a == 255) row[xx] = col;
                else if (a > 0) BlendPixel(row[xx], col);
            }
        }
    }

    void DrawChevron(int cx, int cy, int s, bool right, uint32_t col)
    {
        if ((col >> 24) == 0) return;
        const int half = std::max(3, s + 2);
        const int thk = std::max(1, (s + 1) / 2);
        const int wSpan = (half * 2) / 3 + 1;
        for (int dy = -half; dy <= half; dy++)
        {
            const int prog = half - std::abs(dy);
            const int px = right ? (cx - wSpan / 2 + (prog * 2) / 3) : (cx + wSpan / 2 - (prog * 2) / 3);
            FillBlend(px, cy + dy, thk, 1, col);
        }
    }

    void DrawGeometricWatermark(int cx, int cy, int r, uint32_t col, int clipX0 = 0, int clipY0 = 0, int clipX1 = -1, int clipY1 = -1)
    {
        if (r < 6 || (col >> 24) == 0) return;
        if (clipX1 < 0) clipX1 = w;
        if (clipY1 < 0) clipY1 = h;
        const int x0 = std::max(std::max(0, clipX0), cx - r);
        const int x1 = std::min(std::min(w, clipX1), cx + r + 1);
        const int y0 = std::max(std::max(0, clipY0), cy - r);
        const int y1 = std::min(std::min(h, clipY1), cy + r + 1);
        if (x1 <= x0 || y1 <= y0) return;
        const int ringT = std::max(2, r / 16);
        const int r1Out = r * r, r1In = std::max(1, (r - ringT) * (r - ringT));
        const int r2 = std::max(4, r * 64 / 100);
        const int r2Out = r2 * r2, r2In = std::max(1, (r2 - ringT) * (r2 - ringT));
        const int r3 = std::max(2, r * 30 / 100);
        const int r3Out = r3 * r3, r3In = std::max(1, (r3 - std::max(1, ringT - 1)) * (r3 - std::max(1, ringT - 1)));
        for (int py = y0; py < y1; py++)
        {
            const int dy = py - cy;
            for (int px = x0; px < x1; px++)
            {
                const int dx = px - cx;
                const int d2 = dx * dx + dy * dy;
                if (d2 > r1Out) continue;
                const bool hit = (d2 >= r1In) || (d2 <= r2Out && d2 >= r2In) || (d2 <= r3Out && d2 >= r3In);
                if (hit)
                    BlendPixel(this->px[(size_t)py * w + px], col);
            }
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
                        // Suppress the built-in dot on lowercase i/j when drawing a diacritic above the stem.
                        if (mark != 0 && (ch == 'i' || ch == 'j') && sy < 8)
                        {
                            count += (sx1 - sx0);
                            continue;
                        }
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

    void DrawButtonBadge(int x, int y, const std::string& label, int scale, uint32_t badgeColor = 0xFF26262E, uint32_t textCol = 0xFFFFFFFF, uint32_t borderCol = 0xFF42424E)
    {
        const int s = std::max(1, scale);
        const int fontH = std::max(12, 5 * s + 1);
        const int tw = ModernTextWidth(label, fontH);
        const int bh = 12 * s;
        const int bw = ButtonBadgeWidth(label, s);
        const int rad = bh / 2;
        const uint32_t topBevel = ShadeArgb(badgeColor, 26);
        const uint32_t botBevel = ShadeArgb(badgeColor, -20);
        FillGradV(x, y, bw, bh, topBevel, botBevel, rad);
        OutlineRound(x, y, bw, bh, rad, 1, borderCol);
        if (bh > 4) FillBlend(x + s + 1, y + 1, bw - 2 * (s + 1), 1, 0x2EFFFFFF);
        ModernText(x + (bw - tw) / 2, y + (bh - fontH) / 2, label, fontH, textCol, false);
    }

    void DrawSwitch(int x, int y, int scale, bool on, uint32_t accentColor)
    {
        const int s = std::max(1, scale);
        const int sw = 20 * s, sh = 10 * s;
        const int rad = sh / 2;
        const uint32_t trackTop = on ? ShadeArgb(accentColor, 20) : 0xFF272730;
        const uint32_t trackBot = on ? ShadeArgb(accentColor, -22) : 0xFF1C1C23;
        const uint32_t trackBd = on ? (ShadeArgb(accentColor, 42) | 0xFF000000u) : 0xFF42424E;
        FillGradV(x, y, sw, sh, trackTop, trackBot, rad);
        OutlineRound(x, y, sw, sh, rad, 1, trackBd);
        const int inset = std::max(1, s * 2 / 3);
        const int knobSz = sh - 2 * inset;
        const int knobX = on ? (x + sw - knobSz - inset) : (x + inset);
        const int knobY = y + inset;
        ShadowRound(knobX, knobY, knobSz, knobSz, knobSz / 2, std::max(1, s / 2), 70);
        FillGradV(knobX, knobY, knobSz, knobSz, on ? 0xFFFFFFFFu : 0xFFF2F2F5u, on ? 0xFFEAEAEFu : 0xFFC8C8D2u, knobSz / 2);
    }

    void DrawSliderBar(int x, int y, int rw, int rh, int progress, int maxProgress, uint32_t accentColor, bool focused = false)
    {
        if (rw <= 0 || rh <= 0 || maxProgress <= 0) return;
        const int rad = std::max(2, rh / 2);
        FillRound(x, y, rw, rh, rad, 0xFF26262F);
        OutlineRound(x, y, rw, rh, rad, 1, 0xFF3E3E4A);
        const int fillW = std::clamp((progress * rw) / maxProgress, 0, rw);
        if (fillW > 0)
        {
            const uint32_t fillTop = ShadeArgb(accentColor, 35);
            FillGradV(x, y, fillW, rh, fillTop, accentColor, rad);
            if (focused) GlowRound(x, y, fillW, rh, rad, 2, (accentColor & 0x00FFFFFFu) | 0x55000000u);
        }
        const int thumbSz = rh + std::max(4, rh / 2);
        const int thumbX = x + std::clamp((progress * (rw - thumbSz)) / maxProgress, 0, rw - thumbSz);
        const int thumbY = y - (thumbSz - rh) / 2;
        ShadowRound(thumbX, thumbY, thumbSz, thumbSz, thumbSz / 2, 2, 90);
        FillGradV(thumbX, thumbY, thumbSz, thumbSz, 0xFFFFFFFF, 0xFFE4E4EC, thumbSz / 2);
        OutlineRound(thumbX, thumbY, thumbSz, thumbSz, thumbSz / 2, 1, focused ? (accentColor | 0xFF000000u) : 0xFF6E6E7A);
    }

    void DrawMenuIcon(int x, int y, int sz, const std::string& rawLabel, uint32_t col, uint32_t accent)
    {
        if (sz < 8 || (col >> 24) == 0) return;
        const int cx = x + sz / 2, cy = y + sz / 2;
        const int s = std::max(1, sz / 9);
        if (rawLabel == "RESUME")
        {
            const int h2 = sz * 36 / 100;
            const int lx = x + sz * 30 / 100;
            const int pw = std::max(3, sz * 46 / 100);
            for (int dx = 0; dx < pw; dx++)
            {
                const int half = h2 * (pw - dx) / pw;
                FillBlend(lx + dx, cy - half, 1, half * 2 + 1, col);
            }
        }
        else if (rawLabel == "COMPANION")
        {
            const int bw = sz * 76 / 100, bh = sz * 64 / 100;
            OutlineRound(cx - bw / 2, cy - bh / 2, bw, bh, std::max(2, s), std::max(1, s), col);
            const int dr = std::max(2, sz * 20 / 100);
            for (int dy = -dr; dy <= dr; dy++)
            {
                const int span = dr - std::abs(dy);
                FillBlend(cx - span, cy + dy, span * 2 + 1, 1, accent);
            }
        }
        else if (rawLabel == "SAVE STATE" || rawLabel == "LOAD STATE" || rawLabel == "SAVE BACKUPS")
        {
            const int tw = sz * 70 / 100, tx = cx - tw / 2, th = std::max(2, s + 1);
            const int by = y + sz - th - s;
            FillBlend(tx, by, tw, th, col);
            FillBlend(tx, by - s * 2, th, s * 2, col);
            FillBlend(tx + tw - th, by - s * 2, th, s * 2, col);
            if (rawLabel == "SAVE BACKUPS")
            {
                FillRound(tx + s, y + s * 2, tw - 2 * s, sz * 42 / 100, s, col);
            }
            else
            {
                const bool up = rawLabel == "LOAD STATE";
                const int aw = std::max(2, s);
                FillBlend(cx - aw / 2, y + s, aw, sz * 46 / 100, col);
                const int tipY = up ? y + s : y + sz * 58 / 100;
                const int span = std::max(2, s * 2);
                for (int i = 0; i <= span; i++)
                {
                    const int ay = up ? (tipY + i) : (tipY - i);
                    FillBlend(cx - i, ay, i * 2 + 1, 1, col);
                }
            }
        }
        else if (rawLabel == "FAST FORWARD")
        {
            const int h2 = sz * 30 / 100, pw = std::max(2, sz * 28 / 100);
            for (int k = 0; k < 2; k++)
            {
                const int lx = x + sz * (18 + k * 34) / 100;
                for (int dx = 0; dx < pw; dx++)
                {
                    const int half = h2 * (pw - dx) / pw;
                    FillBlend(lx + dx, cy - half, 1, half * 2 + 1, col);
                }
            }
        }
        else if (rawLabel == "SETTINGS")
        {
            const int lx = x + s, lw = sz - 2 * s, lh = std::max(1, s);
            const int y1 = y + sz * 30 / 100, y2 = y + sz * 68 / 100;
            FillRound(lx, y1, lw, lh, 1, col);
            FillRound(lx, y2, lw, lh, 1, col);
            const int k = std::max(3, sz * 26 / 100);
            FillRound(lx + lw * 65 / 100 - k / 2, y1 + lh / 2 - k / 2, k, k, k / 2, col);
            FillRound(lx + lw * 32 / 100 - k / 2, y2 + lh / 2 - k / 2, k, k, k / 2, col);
        }
        else if (rawLabel == "GRAPHICS" || rawLabel == "DISPLAY")
        {
            const int mw = sz * 82 / 100, mh = sz * 58 / 100;
            const int mx = cx - mw / 2, my = y + s * 2;
            OutlineRound(mx, my, mw, mh, std::max(2, s), std::max(1, s), col);
            FillBlend(cx - s, my + mh, s * 2, s * 2, col);
            FillBlend(cx - mw / 4, my + mh + s * 2, mw / 2, std::max(1, s), col);
        }
        else if (rawLabel == "SOUND" || rawLabel == "VOLUME")
        {
            const int bw = std::max(2, sz * 22 / 100), bh = std::max(3, sz * 30 / 100);
            FillBlend(x + s, cy - bh / 2, bw, bh, col);
            const int cw = std::max(3, sz * 24 / 100);
            for (int dx = 0; dx < cw; dx++)
            {
                const int half = bh / 2 + dx * (sz * 18 / 100) / cw;
                FillBlend(x + s + bw + dx, cy - half, 1, half * 2 + 1, col);
            }
            FillBlend(x + sz - s * 2, cy - sz * 24 / 100, std::max(1, s), sz * 48 / 100, col);
        }
        else if (rawLabel == "REWIND")
        {
            const int h2 = sz * 30 / 100, pw = std::max(2, sz * 28 / 100);
            for (int k = 0; k < 2; k++)
            {
                const int rx2 = x + sz * (48 + k * 34) / 100;
                for (int dx = 0; dx < pw; dx++)
                {
                    const int half = h2 * (pw - dx) / pw;
                    FillBlend(rx2 - dx, cy - half, 1, half * 2 + 1, col);
                }
            }
        }
        else if (rawLabel == "AUTO-RUN")
        {
            const int chevronS = std::max(2, s);
            DrawChevron(cx - sz * 18 / 100, cy, chevronS, true, col);
            DrawChevron(cx + sz * 12 / 100, cy, chevronS, true, col);
            FillBlend(x + sz * 22 / 100, y + sz - s * 2, sz * 56 / 100, std::max(1, s), col);
        }
        else if (rawLabel == "GAMEPLAY")
        {
            const int r = sz * 36 / 100;
            OutlineRound(cx - r, cy - r, r * 2, r * 2, r, std::max(1, s), col);
            const int d = std::max(3, s * 2);
            FillRound(cx - d / 2, cy - d / 2, d, d, d / 2, accent);
        }
        else if (rawLabel == "CONTROLS" || rawLabel == "KEYS AND BUTTONS" || rawLabel == "CONTROLLER" || rawLabel == "KEY BINDINGS")
        {
            const int arm = std::max(2, sz * 26 / 100), len = sz * 76 / 100;
            FillRound(cx - len / 2, cy - arm / 2, len, arm, 1, col);
            FillRound(cx - arm / 2, cy - len / 2, arm, len, 1, col);
        }
        else if (rawLabel == "LANGUAGE")
        {
            const int r = sz * 38 / 100;
            OutlineRound(cx - r, cy - r, r * 2, r * 2, r, std::max(1, s), col);
            FillBlend(cx - r + 1, cy, r * 2 - 2, std::max(1, s), col);
            FillBlend(cx, cy - r + 1, std::max(1, s), r * 2 - 2, col);
        }
        else if (rawLabel == "CHEATS" || rawLabel == "POPULAR CHEATS")
        {
            DrawChevron(cx - sz * 20 / 100, cy, std::max(1, s), false, col);
            DrawChevron(cx + sz * 20 / 100, cy, std::max(1, s), true, col);
        }
        else if (rawLabel == "RETROACHIEVEMENTS" || rawLabel == "ACHIEVEMENTS")
        {
            const int cw = sz * 56 / 100, ch = sz * 44 / 100;
            const int rx = cx - cw / 2, ry = y + s;
            FillRound(rx, ry, cw, ch, std::max(2, s), col);
            FillBlend(cx - s, ry + ch, s * 2, s * 2, col);
            FillBlend(cx - cw / 3, ry + ch + s * 2, cw * 2 / 3, std::max(1, s), col);
        }
        else if (rawLabel == "RESET GAME")
        {
            const int r = sz * 36 / 100;
            OutlineRound(cx - r, cy - r, r * 2, r * 2, r, std::max(1, s), col);
            FillBlend(cx + 1, cy - r - 1, r, r / 2 + 2, 0x00000000);
            FillRound(cx, cy - r - s, s * 2, s * 2, 1, col);
        }
        else if (rawLabel == "QUIT" || rawLabel == "QUIT GAME" || rawLabel == "LOG OUT")
        {
            const int dw = sz * 44 / 100, dh = sz * 76 / 100;
            const int dx = x + s;
            OutlineRound(dx, cy - dh / 2, dw, dh, s, std::max(1, s), col);
            FillBlend(cx, cy - std::max(1, s / 2), sz * 36 / 100, std::max(2, s), col);
            DrawChevron(x + sz - s * 2, cy, std::max(1, s), true, col);
        }
        else if (rawLabel == "BACK" || rawLabel == "CANCEL")
        {
            DrawChevron(cx - s, cy, std::max(1, s), false, col);
            FillBlend(cx - s, cy - std::max(1, s / 2), sz * 40 / 100, std::max(2, s), col);
        }
        else
        {
            const int d = std::max(4, s * 2);
            FillRound(cx - d / 2, cy - d / 2, d, d, d / 2, col);
        }
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

// Original NoSplitNX geometric emblem: two interlocking halves fused around a single 16:9 core.
inline void DrawNoSplitLogo(Canvas& c, int x, int y, int s, uint32_t accentColor = 0xFFE53945)
{
    const int u = std::max(1, s);
    const int w = 11 * u, h = 9 * u;
    const float cx = w * 0.5f;
    const float cy = h * 0.5f;
    const float halfW = 5.15f * u, halfH = 3.75f * u, rad = 1.55f * u, stroke = 1.2f * u;
    const float coreW = 1.65f * u, coreH = 0.62f * u, coreRad = 0.55f * u;
    const float splitGap = 0.34f * u;
    const uint32_t accentMid = accentColor | 0xFF000000u;

    auto sdRoundedBox = [](float px, float py, float bx, float by, float hw, float hh, float r) {
        const float qx = std::fabs(px - bx) - (hw - r);
        const float qy = std::fabs(py - by) - (hh - r);
        return std::hypot(std::max(qx, 0.0f), std::max(qy, 0.0f)) + std::min(std::max(qx, qy), 0.0f) - r;
    };

    for (int py = 0; py < h; py++)
    {
        const int cyPx = y + py;
        if (cyPx < 0 || cyPx >= c.h) continue;
        for (int px = 0; px < w; px++)
        {
            const int cxPx = x + px;
            if (cxPx < 0 || cxPx >= c.w) continue;
            uint32_t sumR = 0, sumG = 0, sumB = 0, hits = 0;
            for (int sy = 0; sy < 2; sy++)
            {
                const float fy = (float)py + (sy ? 0.75f : 0.25f);
                for (int sx = 0; sx < 2; sx++)
                {
                    const float fx = (float)px + (sx ? 0.75f : 0.25f);
                    const float sdOut = sdRoundedBox(fx, fy, cx, cy, halfW, halfH, rad);
                    const float sdCore = sdRoundedBox(fx, fy, cx, cy, coreW, coreH, coreRad);
                    uint32_t col = 0;
                    if (sdOut <= 0.0f && sdOut >= -stroke)
                    {
                        const float diag = (fx - cx) * 0.55f + (fy - cy);
                        if (diag < -splitGap) col = accentMid;
                        else if (diag > splitGap) col = 0xFFF2F2F6u;
                    }
                    else if (sdCore <= 0.0f)
                    {
                        col = (fx < cx) ? accentMid : 0xFFF2F2F6u;
                    }
                    if (col != 0)
                    {
                        sumR += (col >> 16) & 0xFFu;
                        sumG += (col >> 8) & 0xFFu;
                        sumB += col & 0xFFu;
                        hits++;
                    }
                }
            }
            if (hits == 4)
                c.px[(size_t)cyPx * c.w + cxPx] = 0xFF000000u | ((sumR / 4) << 16) | ((sumG / 4) << 8) | (sumB / 4);
            else if (hits > 0)
            {
                const uint32_t a = hits * 63u;
                Canvas::BlendPixel(c.px[(size_t)cyPx * c.w + cxPx], (a << 24) | ((sumR / hits) << 16) | ((sumG / hits) << 8) | (sumB / hits));
            }
        }
    }
}

// The accent colours of COLOUR (the game list's settings, GAME LIST): the game list's lines and
// frame (launcher.h, ThemeNamed) and the pause menu's chosen row, its box and text shaded from it.
inline uint32_t AccentNamed(const std::string& name)
{
    static const struct { const char* name; uint32_t argb; } colours[] = {
        { "red", 0xFFE53945 },      // Poké Ball Crimson Red
        { "crimson", 0xFFE53945 },
        { "blue", 0xFF4A90D9 },     // Calm Azure Slate
        { "cyber", 0xFF4A90D9 },
        { "green", 0xFF45B380 },    // Soft Sage Emerald
        { "emerald", 0xFF45B380 },
        { "purple", 0xFF9B79D9 },   // Muted Twilight Lavender
        { "amethyst", 0xFF9B79D9 },
        { "gold", 0xFFDBA44A },     // Brushed Champagne Amber
        { "amber", 0xFFDBA44A },
        { "pink", 0xFFD97296 },     // Soft Dusty Rose
        { "neon_pink", 0xFFD97296 },
        { "teal", 0xFF4BA8A8 },     // Serene Seafoam Teal
        { "cyan", 0xFF4BA8A8 },
        { "obsidian", 0xFF9FB0C7 }, // Brushed Platinum Silver
        { "silver", 0xFF9FB0C7 },
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
    mutable int drawnX = 0, drawnY = 0, drawnW = 0, drawnH = 0, drawnLineH = 0, drawnFirstY = 0, drawnShown = 0;
    mutable int scroll = 0;
    // Smooth selection cursor state for 60 fps transitions.
    mutable float animSelY = -1.0f;
    mutable float animLastT = 0.0f;
    mutable size_t animCount = 0;
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
        const int rel = (y - drawnFirstY) / drawnLineH;
        if (rel < 0 || (drawnShown > 0 && rel >= drawnShown)) return -1;
        const int i = scroll + rel;
        return i < (int)items.size() ? i : -1;
    }

    // note: a few lines of text in a strip under the box (CHEATS: what the database says about
    // the chosen cheat), wrapped to the box's width.
    // The chosen row's colour (COLOUR, AccentNamed); the box, text and bars are shaded from it.
    static inline uint32_t Accent = 0xFFE53945;

    // boxUnits: the box's width at scale 1 (0: 190, as the other pages). CHEATS passes 260 so a
    // code's name (32 letters) fits beside "< OFF >".
    void Draw(Canvas& c, const std::string& title, const std::string& hint, const std::string& note = "", int boxUnits = 0, bool dimBackdrop = true) const
    {
        auto mixRgb = [](uint32_t a, uint32_t b, uint32_t wA, uint32_t wB, uint32_t alpha) -> uint32_t {
            const uint32_t d = wA + wB;
            const uint32_t r = (((a >> 16) & 0xFFu) * wA + ((b >> 16) & 0xFFu) * wB) / d;
            const uint32_t g = (((a >> 8) & 0xFFu) * wA + ((b >> 8) & 0xFFu) * wB) / d;
            const uint32_t bl = ((a & 0xFFu) * wA + (b & 0xFFu) * wB) / d;
            return (alpha << 24) | (r << 16) | (g << 8) | bl;
        };

        int scale = std::min(c.w, c.h) / 240;
        if (scale < 1) scale = 1;
        const bool sideDrawer = c.w >= 640 && c.h >= 480;
        const int marginX = sideDrawer ? std::max(24, 8 * scale) : 4;
        const int marginY = sideDrawer ? std::max(24, 8 * scale) : 4;

        const uint32_t panelTop = mixRgb(0x18181E, Accent, 14, 1, 0xFF);
        const uint32_t panelBot = mixRgb(0x101014, Accent, 18, 1, 0xFF);
        const uint32_t headerTop = mixRgb(0x202028, Accent, 9, 1, 0xFF);
        const uint32_t headerBot = mixRgb(0x15151B, Accent, 12, 1, 0xFF);
        const uint32_t textCol = 0xFFE4E4ECu;
        const uint32_t hintCol = 0xFF9898A4u;

        const int itemFontH = sideDrawer ? std::max(20, 10 * scale) : std::max(12, 9 * scale);
        int lineH = sideDrawer ? std::max(34, (UiLang::Tall() ? 19 : 17) * scale) : std::max(24, (UiLang::Tall() ? 18 : 16) * scale);
        const bool hasTitle = !title.empty();
        const int iconSz = sideDrawer ? std::max(16, 7 * scale) : 0;
        const int iconGap = sideDrawer ? 6 * scale : 0;
        const int leftPad = (sideDrawer ? 9 : 6) * scale;
        const int rightPad = (sideDrawer ? 9 : 6) * scale;
        const int labelXOff = leftPad + iconSz + iconGap;

        auto isToggle = [](const MenuItem& item) {
            return !item.info && item.slider < 0 && (item.value == "ON" || item.value == "OFF");
        };
        auto isSubmenu = [](const MenuItem& item) {
            if (!item.value.empty() || item.info) return false;
            const std::string& l = item.label;
            return l == "SETTINGS" || l == "GRAPHICS" || l == "DISPLAY" || l == "SOUND" || l == "GAMEPLAY" ||
                   l == "CONTROLS" || l == "KEYS AND BUTTONS" || l == "KEY BINDINGS" || l == "CONTROLLER" ||
                   l == "CHEATS" || l == "POPULAR CHEATS" || l == "RETROACHIEVEMENTS" || l == "ACHIEVEMENTS" ||
                   l == "COMPANION" || l == "SAVE BACKUPS" || l == "SHOW RESULTS";
        };
        auto shownValue = [&](const MenuItem& item) -> std::string {
            if (isToggle(item) || item.slider >= 0) return "";
            return item.value.empty() ? "" : ToNiceTitle(UiLang::Tr(item.value));
        };

        int boxW = sideDrawer
            ? (boxUnits > 0 ? std::max(boxUnits * scale, c.w * 48 / 100) : std::max(440, c.w * 42 / 100))
            : 296 * scale;
        const int maxBoxW = std::max(120, c.w - 2 * marginX);
        const int chevW = std::max(4, scale * 2);
        const int chevGap = 5 * scale;
        const int switchW = 20 * scale;
        for (const auto& item : items)
        {
            const std::string trL = ToNiceTitle(UiLang::Tr(item.label));
            const std::string v = shownValue(item);
            int ctrlW = 0;
            if (isToggle(item))
                ctrlW = switchW + 8 * scale;
            else if (item.slider >= 0)
            {
                const std::string sv = ToNiceTitle(UiLang::Tr(item.value));
                ctrlW = c.ModernTextWidth(sv, itemFontH) + 8 * scale + 10 * (6 * scale) + 12 * scale;
            }
            else if (!v.empty())
            {
                const int vw = c.ModernTextWidth(v, itemFontH);
                ctrlW = vw + (item.info ? 12 * scale : (16 * scale + 2 * (chevW + chevGap)));
            }
            else if (isSubmenu(item))
                ctrlW = chevW + 8 * scale;
            const int needW = labelXOff + c.ModernTextWidth(trL, itemFontH) + ctrlW + rightPad + 14 * scale;
            const int legacyW = Canvas::TextWidth(item.label, scale) + (item.value.empty() ? 0 : Canvas::TextWidth("< " + item.value + " >", scale)) + 28 * scale;
            boxW = std::max({boxW, needW, legacyW});
        }
        if (hasTitle)
        {
            const std::string niceTitle = (title == "NoSplitNX") ? title : ToNiceTitle(UiLang::Tr(title));
            const int titleFontH = std::max(sideDrawer ? 22 : 14, 13 * scale);
            const int titleW = c.ModernTextWidth(niceTitle, titleFontH);
            boxW = std::max(boxW, titleW + 64 * scale);
        }
        boxW = std::min(boxW, maxBoxW);

        auto wrapModern = [&](const std::string& text, int fontH, int maxPx, size_t maxLines) {
            std::vector<std::string> out;
            if (text.empty() || maxLines == 0) return out;
            const int limit = std::max(24, maxPx);
            std::string cur;
            bool truncated = false;
            auto pushLine = [&](std::string line) {
                if (out.size() < maxLines) out.push_back(std::move(line));
                else truncated = true;
            };
            auto splitWord = [&](const std::string& word) {
                std::string chunk;
                for (size_t at = 0; at < word.size(); )
                {
                    const size_t prev = at;
                    UiLang::Next(word, at);
                    std::string ch = word.substr(prev, at - prev);
                    if (!chunk.empty() && c.ModernTextWidth(chunk + ch, fontH) > limit)
                    {
                        pushLine(std::move(chunk));
                        chunk = std::move(ch);
                    }
                    else
                    {
                        chunk += ch;
                    }
                }
                if (!chunk.empty()) cur = std::move(chunk);
            };
            size_t pos = 0;
            while (pos < text.size() && out.size() < maxLines)
            {
                if (text[pos] == '\n')
                {
                    pushLine(std::move(cur));
                    cur.clear();
                    pos++;
                    continue;
                }
                if (text[pos] == ' ') { pos++; continue; }
                size_t end = pos;
                while (end < text.size() && text[end] != ' ' && text[end] != '\n') end++;
                const std::string word = text.substr(pos, end - pos);
                pos = end;
                const std::string cand = cur.empty() ? word : cur + " " + word;
                if (c.ModernTextWidth(cand, fontH) <= limit)
                {
                    cur = cand;
                }
                else if (!cur.empty())
                {
                    pushLine(std::move(cur));
                    cur.clear();
                    if (c.ModernTextWidth(word, fontH) <= limit) cur = word;
                    else splitWord(word);
                }
                else
                {
                    splitWord(word);
                }
                if (truncated) break;
            }
            if (!cur.empty()) pushLine(std::move(cur));
            if (truncated && !out.empty())
            {
                std::string& last = out.back();
                while (!last.empty() && c.ModernTextWidth(last + "...", fontH) > limit)
                {
                    size_t len = last.size();
                    while (len > 0 && (last[len - 1] & 0xC0) == 0x80) len--;
                    if (len > 0) len--;
                    last.resize(len);
                    while (!last.empty() && last.back() == ' ') last.pop_back();
                }
                last += "...";
            }
            return out;
        };

        std::vector<std::string> noteLines;
        const int noteFontH = std::max(14, 7 * scale);
        if (!note.empty())
            noteLines = wrapModern(ToNiceSentence(UiLang::Tr(note)), noteFontH, boxW - 24 * scale, 4);
        const int noteCardH = noteLines.empty() ? 0 : (int)noteLines.size() * (noteFontH + 3 * scale) + 10 * scale;

        const int hintFontH = std::max(13, 8 * scale);
        const std::string niceHint = ToNiceSentence(UiLang::Tr(hint));
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
                if (colon == std::string::npos || colon == 0) { badges.clear(); break; }
                std::string k = part.substr(0, colon);
                while (!k.empty() && k.back() == ' ') k.pop_back();
                if (k.empty() || k.size() > 5) { badges.clear(); break; }
                std::string l = part.substr(colon + 1);
                size_t ls = l.find_first_not_of(' ');
                if (ls == std::string::npos) { badges.clear(); break; }
                badges.push_back({ k, ToNiceTitle(l.substr(ls)) });
            }
        }
        const int badgeScale = std::max(1, scale - (scale >= 3 ? 1 : 0));
        const int bh = 12 * badgeScale;
        int badgesW = 0;
        const int badgeGap = 14 * scale;
        if (badges.size() >= 2)
        {
            for (size_t i = 0; i < badges.size(); i++)
            {
                badgesW += c.ButtonBadgeWidth(badges[i].key, badgeScale) + 4 * scale + c.ModernTextWidth(badges[i].label, hintFontH);
                if (i + 1 < badges.size()) badgesW += badgeGap;
            }
            if (badgesW > boxW - 18 * scale) badges.clear();
        }
        std::vector<std::string> hintLines;
        if (badges.size() < 2 && !niceHint.empty())
            hintLines = wrapModern(niceHint, hintFontH, boxW - 18 * scale, 4);

        const int hdrH = hasTitle ? (sideDrawer ? std::max(lineH + 6 * scale, 22 * scale) : lineH) : 0;
        const int listPadTop = (sideDrawer ? 6 : 4) * scale;
        const int listPadBot = 4 * scale;
        int hintAreaH = 0;
        if (badges.size() >= 2) hintAreaH = std::max(bh + 10 * scale, 16 * scale);
        else if (!hintLines.empty()) hintAreaH = (int)hintLines.size() * (hintFontH + 3 * scale) + 10 * scale;
        const int footerH = (noteCardH ? noteCardH + 6 * scale : 0) + (hintAreaH ? hintAreaH + 2 * scale : (!sideDrawer ? 4 * scale : 8 * scale));

        const int availH = std::max(2 * lineH, c.h - 2 * marginY);
        const int minLineH = sideDrawer ? std::max(itemFontH + 6 * scale, (UiLang::Tall() ? 16 : 14) * scale)
                                        : std::max(itemFontH + 4 * scale, (UiLang::Tall() ? 15 : 12) * scale);
        int listAvailH = std::max(minLineH, availH - hdrH - listPadTop - listPadBot - footerH);
        const int fit = std::max(1, listAvailH / minLineH);
        const int shown = std::min((int)items.size(), fit);
        if (lineH * shown > listAvailH && shown > 0)
            lineH = std::max(minLineH, listAvailH / shown);

        if (selected < scroll) scroll = selected;
        if (selected >= scroll + shown) scroll = selected - shown + 1;
        scroll = std::max(0, std::min(scroll, (int)items.size() - shown));

        const int boxH = sideDrawer ? availH : std::min(availH, hdrH + listPadTop + shown * lineH + listPadBot + footerH);
        const int bx = sideDrawer ? marginX : (c.w - boxW) / 2;
        const int by = sideDrawer ? marginY : std::max(marginY, (c.h - boxH) / 2);
        const int listFirstY = by + hdrH + listPadTop;

        drawnX = bx;
        drawnY = by;
        drawnW = boxW;
        drawnH = boxH;
        drawnLineH = lineH;
        drawnFirstY = listFirstY;
        drawnShown = shown;

        if (dimBackdrop)
        {
            const uint32_t leftScrim = 0xD608080Bu;
            const uint32_t rightScrim = sideDrawer ? 0x5208080Bu : 0xB808080Bu;
            const_cast<Canvas&>(c).FillGradH(0, 0, c.w, c.h, leftScrim, rightScrim);
        }

        const int rad = std::max(4, (sideDrawer ? 6 : 5) * scale);
        const uint32_t panelBorder = mixRgb(0x32323C, Accent, 3, 1, 0xFF);
        const_cast<Canvas&>(c).ShadowRound(bx, by, boxW, boxH, rad, (sideDrawer ? 4 : 3) * scale, 135);
        const_cast<Canvas&>(c).FillGlass(bx, by, boxW, boxH, rad, panelTop | 0xF5000000u, panelBot | 0xF8000000u, panelBorder, 1);

        if (hasTitle)
        {
            const std::string niceTitle = (title == "NoSplitNX") ? title : ToNiceTitle(UiLang::Tr(title));
            const int titleFontH = std::max(sideDrawer ? 22 : 14, 13 * scale);
            const int hdrR = std::max(2, rad - scale);
            c.FillGradV(bx + 1, by + 1, boxW - 2, hdrH, headerTop | 0xF5000000u, headerBot | 0xF5000000u, hdrR);
            c.FillBlend(bx + 2, by + hdrH, boxW - 4, std::max(1, scale / 2), mixRgb(0x2C2C36, Accent, 3, 1, 0xCC));
            if (sideDrawer)
                const_cast<Canvas&>(c).DrawGeometricWatermark(bx + boxW - hdrH * 3 / 4, by + hdrH / 2 + 2 * scale, hdrH * 72 / 100,
                    (Accent & 0x00FFFFFFu) | 0x16000000u, bx + 2, by + 2, bx + boxW - 2, by + hdrH);

            const int logoW = 11 * scale, logoH = 9 * scale;
            const int titleX = sideDrawer ? (bx + 12 * scale) : (bx + (boxW - (logoW + 6 * scale + c.ModernTextWidth(niceTitle, titleFontH))) / 2);
            DrawNoSplitLogo(const_cast<Canvas&>(c), titleX, by + (hdrH - logoH) / 2, scale, Accent | 0xFF000000u);
            c.ModernText(titleX + logoW + 6 * scale, by + (hdrH - titleFontH) / 2, niceTitle, titleFontH, 0xFFFFFFFF, true);

            if ((int)items.size() > shown && sideDrawer)
            {
                const std::string posStr = std::to_string(selected + 1) + "/" + std::to_string(items.size());
                const int cntH = std::max(14, 6 * scale);
                const int cw = c.ModernTextWidth(posStr, cntH);
                const int cpW = cw + 10 * scale, cpH = cntH + 4 * scale;
                const int cpX = bx + boxW - cpW - 10 * scale, cpY = by + (hdrH - cpH) / 2;
                c.FillRound(cpX, cpY, cpW, cpH, cpH / 2, 0x90121218);
                c.OutlineRound(cpX, cpY, cpW, cpH, cpH / 2, 1, (Accent & 0x00FFFFFFu) | 0x66000000u);
                c.ModernText(cpX + 5 * scale, cpY + (cpH - cntH) / 2, posStr, cntH, textCol);
            }
        }

        const float t = AnimTime();
        const float pulse = 0.5f + 0.5f * sinf(t * 3.5f);
        const int targetVis = (selected >= scroll && selected < scroll + shown) ? (selected - scroll) : -1;
        const float targetY = targetVis >= 0 ? (float)(listFirstY + targetVis * lineH) : (float)listFirstY;
        const float dt = t - animLastT;
        if (animCount != items.size() || animSelY < (float)listFirstY || animSelY > (float)(listFirstY + std::max(0, shown - 1) * lineH) || dt <= 0.0f || dt > 0.15f)
            animSelY = targetY;
        else
            animSelY += (targetY - animSelY) * (1.0f - std::exp(-24.0f * dt));
        animLastT = t;
        animCount = items.size();

        const int rx = bx + 6 * scale;
        const int rw = boxW - 12 * scale - ((int)items.size() > shown ? 5 * scale : 0);
        const int rh = lineH - 2 * scale;
        const int rowRad = std::max(3, 4 * scale);

        if (targetVis >= 0)
        {
            const int hlY = std::clamp((int)std::lround(animSelY) + scale, listFirstY + scale, listFirstY + std::max(0, shown - 1) * lineH + scale);
            const uint32_t rowTop = mixRgb(0x282832, Accent, 3, 1, 0xEC);
            const uint32_t rowBot = mixRgb(0x1C1C24, Accent, 4, 1, 0xEC);
            const uint32_t rowBd  = mixRgb(Accent, 0xF0F0F5, 3, 2, 0xD8);
            const uint32_t barCol = mixRgb(Accent, 0xFFFFFF, 4, 1, 0xF5);
            if (rx - scale >= bx)
                const_cast<Canvas&>(c).GlowRound(rx, hlY, rw, rh, rowRad, scale, (Accent & 0x00FFFFFFu) | ((uint32_t)(20 + 12 * pulse) << 24));
            c.FillGlass(rx, hlY, rw, rh, rowRad, rowTop, rowBot, rowBd, 1);
            c.FillRound(rx + 2 * scale, hlY + 2 * scale, std::max(2, 2 * scale), std::max(4, rh - 4 * scale), scale, barCol);
        }

        int y = listFirstY;
        for (int i = scroll; i < scroll + shown; i++)
        {
            const bool sel = i == selected;
            const int ry = y + scale;
            const uint32_t iconCol = sel ? (Accent | 0xFF000000u) : (items[i].info ? hintCol : 0xFF9494A2);
            if (iconSz > 0)
                const_cast<Canvas&>(c).DrawMenuIcon(rx + 6 * scale, ry + (rh - iconSz) / 2, iconSz, items[i].label, iconCol, Accent | 0xFF000000u);

            const uint32_t col = items[i].info ? textCol : (sel ? 0xFFFFFFFF : 0xFFDCDCE4);
            const std::string trLabel = ToNiceTitle(UiLang::Tr(items[i].label));
            const int lblX = rx + (iconSz > 0 ? (6 * scale + iconSz + iconGap) : 6 * scale);
            c.ModernText(lblX, ry + (rh - itemFontH) / 2, trLabel, itemFontH, col);

            const int ctrlRight = rx + rw - 6 * scale;
            if (isToggle(items[i]))
            {
                const bool on = items[i].value == "ON";
                const int swY = ry + (rh - 10 * scale) / 2;
                const_cast<Canvas&>(c).DrawSwitch(ctrlRight - switchW, swY, scale, on, Accent | 0xFF000000u);
            }
            else if (items[i].slider >= 0)
            {
                const std::string sv = ToNiceTitle(UiLang::Tr(items[i].value));
                const int valW = c.ModernTextWidth(sv, itemFontH);
                const int pillW = valW + 10 * scale;
                const int pillH = std::max(itemFontH + 2 * scale, rh - 6 * scale);
                const int pillX = ctrlRight - pillW;
                const int pillY = ry + (rh - pillH) / 2;
                c.FillRound(pillX, pillY, pillW, pillH, pillH / 2, sel ? mixRgb(0x282832, Accent, 3, 1, 0xEE) : 0xAA1B1B23);
                c.OutlineRound(pillX, pillY, pillW, pillH, pillH / 2, 1, sel ? ((Accent & 0x00FFFFFFu) | 0x99000000u) : 0x663A3A46);
                c.ModernText(pillX + 5 * scale, pillY + (pillH - itemFontH) / 2, sv, itemFontH, sel ? 0xFFFFFFFF : 0xFFD4D4DE);

                barStep = 6 * scale;
                barX = pillX - 8 * scale - 10 * barStep;
                const int barW = 10 * barStep - scale;
                const int barH = std::max(4, 3 * scale);
                const_cast<Canvas&>(c).DrawSliderBar(barX, ry + (rh - barH) / 2, barW, barH, items[i].slider, 10, Accent | 0xFF000000u, sel);
            }
            else if (!items[i].value.empty())
            {
                const std::string v = ToNiceTitle(UiLang::Tr(items[i].value));
                const int textW = c.ModernTextWidth(v, itemFontH);
                const int pillH = std::max(itemFontH + 2 * scale, rh - 6 * scale);
                const int pillY = ry + (rh - pillH) / 2;
                if (items[i].info)
                {
                    const int pillW = textW + 10 * scale;
                    const int pillX = ctrlRight - pillW;
                    c.FillRound(pillX, pillY, pillW, pillH, pillH / 2, 0x881B1B23);
                    c.ModernText(pillX + 5 * scale, pillY + (pillH - itemFontH) / 2, v, itemFontH, textCol);
                }
                else
                {
                    const int innerPad = 7 * scale;
                    const int pillW = innerPad + chevW + chevGap + textW + chevGap + chevW + innerPad;
                    const int pillX = ctrlRight - pillW;
                    const uint32_t pillFill = sel ? mixRgb(0x282832, Accent, 3, 1, 0xEE) : 0xA01B1B23u;
                    const uint32_t pillBd = sel ? ((Accent & 0x00FFFFFFu) | 0x88000000u) : 0x603A3A46u;
                    c.FillRound(pillX, pillY, pillW, pillH, pillH / 2, pillFill);
                    c.OutlineRound(pillX, pillY, pillW, pillH, pillH / 2, 1, pillBd);
                    const uint32_t arrowCol = sel ? 0xFFFFFFFFu : 0xFF9898A4u;
                    const int chevSz = std::max(2, scale);
                    const_cast<Canvas&>(c).DrawChevron(pillX + innerPad + chevW / 2, pillY + pillH / 2, chevSz, false, arrowCol);
                    c.ModernText(pillX + innerPad + chevW + chevGap, pillY + (pillH - itemFontH) / 2, v, itemFontH, sel ? 0xFFFFFFFF : 0xFFDCDCE4);
                    const_cast<Canvas&>(c).DrawChevron(pillX + pillW - innerPad - chevW / 2, pillY + pillH / 2, chevSz, true, arrowCol);
                }
            }
            else if (isSubmenu(items[i]))
            {
                const_cast<Canvas&>(c).DrawChevron(ctrlRight - chevW, ry + rh / 2, std::max(2, scale), true, sel ? 0xFFFFFFFF : 0xFF9898A4u);
            }

            if (i + 1 < scroll + shown && i != selected && i + 1 != selected)
                c.FillBlend(rx + 6 * scale, y + lineH - 1, rw - 12 * scale, 1, 0x1AFFFFFF);
            y += lineH;
        }

        if ((int)items.size() > shown)
        {
            const int trackX = bx + boxW - 5 * scale;
            const int trackY = listFirstY + 2 * scale;
            const int trackH = shown * lineH - 4 * scale;
            if (trackH > 8)
            {
                c.FillRound(trackX, trackY, std::max(2, scale), trackH, std::max(1, scale / 2), 0x33FFFFFF);
                const int thumbH = std::max(10 * scale, trackH * shown / (int)items.size());
                const int maxScr = std::max(1, (int)items.size() - shown);
                const int thumbY = trackY + (trackH - thumbH) * scroll / maxScr;
                c.FillRound(trackX, thumbY, std::max(2, scale), thumbH, std::max(1, scale / 2), (Accent & 0x00FFFFFFu) | 0xCC000000u);
            }
        }

        // Integrated footer inside the drawer (optional note panel + controller/touch hints).
        int footY = by + boxH - footerH;
        if (!noteLines.empty())
        {
            const int nx = bx + 6 * scale, nw = boxW - 12 * scale;
            c.FillRound(nx, footY, nw, noteCardH, std::max(3, 4 * scale), 0x99121217u);
            c.OutlineRound(nx, footY, nw, noteCardH, std::max(3, 4 * scale), 1, 0x443A3A48u);
            c.FillRound(nx + 2 * scale, footY + 3 * scale, std::max(2, scale), noteCardH - 6 * scale, 1, (Accent & 0x00FFFFFFu) | 0xCC000000u);
            for (size_t i = 0; i < noteLines.size(); i++)
                c.ModernText(nx + 8 * scale, footY + 5 * scale + (int)i * (noteFontH + 3 * scale), noteLines[i], noteFontH, textCol);
            footY += noteCardH + 6 * scale;
        }
        if (hintAreaH > 0)
        {
            c.FillBlend(bx + 8 * scale, footY, boxW - 16 * scale, 1, 0x26FFFFFF);
            if (badges.size() >= 2)
            {
                int hx = bx + (boxW - badgesW) / 2;
                const int hy = footY + (hintAreaH - bh) / 2;
                for (size_t i = 0; i < badges.size(); i++)
                {
                    const int bw = c.ButtonBadgeWidth(badges[i].key, badgeScale);
                    const_cast<Canvas&>(c).DrawButtonBadge(hx, hy, badges[i].key, badgeScale, 0xFF24242D, 0xFFFFFFFF, 0xFF3E3E4C);
                    c.ModernText(hx + bw + 4 * scale, footY + (hintAreaH - hintFontH) / 2, badges[i].label, hintFontH, hintCol);
                    hx += bw + 4 * scale + c.ModernTextWidth(badges[i].label, hintFontH) + badgeGap;
                }
            }
            else if (!hintLines.empty())
            {
                const int stepH = hintFontH + 3 * scale;
                const int totalTextH = (int)hintLines.size() * hintFontH + ((int)hintLines.size() - 1) * (3 * scale);
                int hy = footY + std::max(2 * scale, (hintAreaH - totalTextH) / 2);
                for (const std::string& hline : hintLines)
                {
                    const int hw = c.ModernTextWidth(hline, hintFontH);
                    c.ModernText(bx + std::max(8 * scale, (boxW - hw) / 2), hy, hline, hintFontH, hintCol);
                    hy += stepH;
                }
            }
        }
    }
};
