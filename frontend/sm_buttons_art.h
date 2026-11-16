// The words of BATTLE BUTTONS: SUN AND MOON (sm_buttons.h), drawn by FullDex into a picture the
// compositor takes as one more screen (SmButtons::kScreen): FIGHT, POKEMON, BAG and RUN in heavy
// dark letters, each on its button's own colour so that it is laid on the button as it is. Made
// again when the menus' language changes (Japanese and Korean keep the English words).
// Included after ui.h (Canvas::ModernText renders ASCII and Latin accents cleanly without TTFs).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "sm_buttons.h"
#include "ui_lang.h"

namespace SmButtonsArt
{
inline uint32_t* Pixels() { static uint32_t px[256 * 192]; return px; }

// The picture for the menus' language [lang] (UiLang::lang); made once for each language.
inline void Ensure(int lang)
{
    static int made = -1;
    if (made == lang) return;
    made = lang;

    uint32_t* px = Pixels();
    std::fill(px, px + 256 * 192, 0u);
    SmButtons::State& st = SmButtons::S();
    for (auto& l : st.label) l = {};

    static const char* const words[5][SmButtons::COUNT] = {
        { "FIGHT",   "POKÉMON", "BAG",     "RUN" },    // en / ja / ko
        { "LUCHAR",  "POKÉMON", "BOLSA",   "HUIR" },   // es
        { "LOTTA",   "POKÉMON", "BORSA",   "FUGA" },   // it
        { "ATTAQUE", "POKÉMON", "SAC",     "FUITE" },  // fr
        { "KAMPF",   "POKÉMON", "BEUTEL",  "FLUCHT" }, // de
    };
    const int wLang = (lang >= 1 && lang <= 4) ? lang : 0;
    // (FIGHT's letters slightly bigger, as they are drawn bigger.)
    const int sizes[SmButtons::COUNT] = { 38, 32, 32, 32 };
    int top = 0, left = 0, rowH = 0; // (the words packed in rows inside the 256x192 atlas)

    Canvas capBuf, wordBuf;
    capBuf.Resize(64, 64);
    wordBuf.Resize(256, 64);

    for (int b = 0; b < SmButtons::COUNT; b++)
    {
        const std::string word = words[wLang][b];

        const int fontH = sizes[b];
        const int drawY = 14;
        // Measure capital H's pixel top and height at the exact fontH used for the word.
        capBuf.Fill(0, 0, 64, 64, 0xFF000000u);
        capBuf.ModernText(4, drawY, "H", fontH, 0xFFFFFFFFu, false);
        int hy0 = 64, hy1 = -1;
        for (int yy = 0; yy < 64; yy++)
            for (int xx = 0; xx < 64; xx++)
                if ((capBuf.px[(size_t)yy * 64 + xx] & 0xFFu) > 32)
                {
                    hy0 = std::min(hy0, yy);
                    hy1 = std::max(hy1, yy);
                }
        if (hy1 < hy0) { hy0 = drawY; hy1 = drawY + fontH - 1; }

        // Render the word white-on-black, make it slightly heavier horizontally (max of self and
        // left neighbour, matching Sun & Moon's thick dark lettering), and find its tight box.
        wordBuf.Fill(0, 0, 256, 64, 0xFF000000u);
        wordBuf.ModernText(6, drawY, word, fontH, 0xFFFFFFFFu, false);

        int minX = 256, minY = 64, maxX = -1, maxY = -1;
        for (int yy = 0; yy < 64; yy++)
            for (int xx = 0; xx < 256; xx++)
            {
                uint32_t c = wordBuf.px[(size_t)yy * 256 + xx] & 0xFFu;
                if (xx > 0) c = std::max(c, wordBuf.px[(size_t)yy * 256 + xx - 1] & 0xFFu);
                if (c > 0)
                {
                    minX = std::min(minX, xx);
                    minY = std::min(minY, yy);
                    maxX = std::max(maxX, xx);
                    maxY = std::max(maxY, yy);
                }
            }
        if (maxX < minX || maxY < minY) continue;

        const int w = std::min(250, maxX - minX + 1);
        const int h = std::min(58, maxY - minY + 1);
        const int pad = 3;
        if (w + 2 * pad > 256) break;
        if (left + w + 2 * pad > 256) { top += rowH + 1; left = 0; rowH = 0; }
        if (top + h + 2 * pad > 192) break;

        const uint32_t fill = SmButtons::LookOf(b).fill, ink = 0x100E12u;
        // (A margin of the button's colour all round: the compositor's smoothing reads a little
        // past the word's edges.)
        for (int yy = 0; yy < h + 2 * pad; yy++)
            for (int xx = 0; xx < w + 2 * pad; xx++)
                px[(top + yy) * 256 + left + xx] = fill;
        for (int yy = 0; yy < h; yy++)
        {
            const int sy = minY + yy;
            for (int xx = 0; xx < w; xx++)
            {
                const int sx = minX + xx;
                uint32_t cov = wordBuf.px[(size_t)sy * 256 + sx] & 0xFFu;
                if (sx > 0) cov = std::max(cov, wordBuf.px[(size_t)sy * 256 + sx - 1] & 0xFFu);
                const float a = cov / 255.0f;
                uint32_t c = 0;
                for (int sh = 0; sh <= 16; sh += 8)
                {
                    const float v = ((fill >> sh) & 0xFFu) * (1.0f - a) + ((ink >> sh) & 0xFFu) * a;
                    c |= (uint32_t)std::lround(v) << sh;
                }
                px[(top + pad + yy) * 256 + left + pad + xx] = c;
            }
        }

        SmButtons::Label& l = st.label[b];
        l.x = left + pad;
        l.y = top + pad;
        l.w = w;
        l.h = h;
        l.capTop = (float)(hy0 - minY);
        l.capH = (float)(hy1 - hy0 + 1);
        left += w + 2 * pad + 1;
        rowH = std::max(rowH, h + 2 * pad);
    }
}
} // namespace SmButtonsArt
