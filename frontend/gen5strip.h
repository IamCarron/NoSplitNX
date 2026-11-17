// The battle's buttons on the message box's band (BATTLE BUTTONS: STRIP) in the Gen 5 games:
// what Black and White's strip (frontend/bw1.h) and Black 2 and White 2's (here) both use, and
// Black 2 and White 2's strip itself.
//
// On the DS the battle's buttons are on the touch screen, drawn on a big Poke Ball. The strip
// puts them on the top screen's message box, right of its text, in their own shapes without the
// Ball behind them, so that they look like part of the battle's own screen rather than a piece of
// the other one. Every piece keeps its place relative to the others (scaled), so that a click
// lands on the button it shows and the D-pad moves the cursor as it looks; the game's own cursor
// comes with the button it is on.
#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

#include "merged.h"
#include "sm_buttons.h"

namespace Gen5Strip
{
// Whether a screen is all one colour (black or white between two pages, as it fades).
inline bool OneColour(const ScreenImage& screen)
{
    if (!screen.pixels) return false;
    const uint32_t c = screen.pixels[0] & 0xFFFFFF;
    for (int i = 1; i < 256 * 192; i++)
        if ((screen.pixels[i] & 0xFFFFFF) != c) return false;
    return true;
}

// Where the battle's message ("What will Tepig do?") ends: the rightmost column of its white
// letters on the message box's band (rows 150-186 of the top screen).
inline int MessageEnd(const ScreenImage& top)
{
    if (!top.pixels) return 0;
    for (int x = 255; x >= 0; x--)
    {
        int n = 0;
        for (int y = 150; y < 187; y++)
        {
            const uint32_t c = top.pixels[y * 256 + x];
            n += ((c >> 16) & 0xFF) > 220 && ((c >> 8) & 0xFF) > 220 && (c & 0xFF) > 220;
        }
        if (n >= 2) return x + 1;
    }
    return 0;
}

// The box around the cursor's brackets on the touch screen (below its status bar): the pixels
// of their colour, which nothing else on these screens has. False when it is not shown.
inline bool CursorBox(const uint32_t* px, uint32_t colour, int& x0, int& y0, int& x1, int& y1)
{
    x0 = 256, y0 = 192, x1 = -1, y1 = -1;
    if (!px) return false;
    for (int y = 16; y < 192; y++)
        for (int x = 0; x < 256; x++)
            if ((px[y * 256 + x] & 0xFFFFFF) == colour)
            {
                x0 = std::min(x0, x); y0 = std::min(y0, y);
                x1 = std::max(x1, x); y1 = std::max(y1, y);
            }
    return x1 >= 0;
}

// The HP boxes where the battle had them a moment ago (against the view's edges, the wide arena
// showing where they were), for a scene of the battle's top screen made without them (a choice in
// the battle, a fade): battleTop is the battle's top piece as it was (its holes), boxes the boxes'
// pieces, inserted into the list at `at`. They do not jump back to the screen's places and out
// again.
inline void KeepBoxes(Scene& sc, const Element& battleTop, const std::vector<Element>& boxes, size_t at)
{
    if (sc.items.empty() || !battleTop.holeCount || boxes.empty()) return;
    sc.items[0].holeCount = battleTop.holeCount;
    std::copy(&battleTop.holes[0][0], &battleTop.holes[0][0] + 8, &sc.items[0].holes[0][0]);
    sc.items.insert(sc.items.begin() + std::min(at, sc.items.size()), boxes.begin(), boxes.end());
}

// A choice on the touch screen in a battle, its question on the top screen ("Will you switch your
// Pokemon?"): two bars (BG1, long hexagons as the moves are, x 9-246, rows 34-77 and 82-125, their
// words on BG2) with the backdrop around them, the cursor's brackets (sprites) beside the one it
// is on. As the moves are: in their own shapes on the message box's band (rows bandTop to bandTop
// + bandHeight), right of the question, as tall as the band (0.44, smaller if the question leaves
// less room), instead of the touch screen's pieces already in the scene, and only once they are in
// their places (the bars' insides in the middle below their words, the backdrop between them and
// beside them): not the Ball turning into them, before. False when they are not in their places
// (the scene then has no pieces of the touch screen).
inline bool ChoiceBars(Scene& sc, float right, const ScreenImage* screens, float bandTop, float bandHeight)
{
    sc.items.erase(std::remove_if(sc.items.begin(), sc.items.end(), [](const Element& e) { return e.screen == 1; }),
        sc.items.end());
    const uint8_t* fl = screens[1].flags;
    auto layer = [&](int x, int y) { return LayerClass(fl[y * 256 + x]); };
    if (!fl || layer(128, 70) != LAYER_BG1 || layer(128, 118) != LAYER_BG1 || layer(128, 80) != LAYER_BACKDROP ||
        layer(0, 80) != LAYER_BACKDROP || layer(255, 80) != LAYER_BACKDROP)
        return false;
    const float s = std::max(0.36f, std::min(0.44f, (right - (MessageEnd(screens[0]) + 4)) / 256));
    Element bars = Crop(1, 0, 32, 256, 96);
    bars.scale = s;
    bars.dx = right - 256 * s;
    bars.dy = bandTop + (bandHeight - 96 * s) / 2;
    bars.drop = LAYER_BACKDROP;
    sc.items.push_back(bars);
    return true;
}

// Where the parties' rows of balls go (top screen pixels; a right edge < 0: not drawn).
struct PartyPlaces { float foeRight = -1, foeTop = 0, mineRight = -1, mineTop = 0; };

// The parties' rows of balls (sprites: a ball for each Pokemon, a dot for each empty place),
// which every one of these menus has, the foe trainer's above the buttons and the player's below
// them: not with the buttons but by the HP boxes, as the game's own bars of balls show them as a
// trainer battle starts. The foe's under the foes' boxes, as large as they are; the player's
// under the player's (in a single battle: in the others its boxes reach the band) at 0.75,
// between the box and the band. foe, mine: their pieces of the touch screen.
inline void PartyRows(Scene& sc, const PartyPlaces& party, Element foe, Element mine)
{
    const uint8_t allBackgrounds = LAYER_BG0 | LAYER_BG1 | LAYER_BG2 | LAYER_BG3 | LAYER_BACKDROP;
    if (party.foeRight >= 0)
    {
        foe.dx = party.foeRight - foe.sw;
        foe.dy = party.foeTop;
        foe.drop = allBackgrounds;
        foe.crisp = true;
        sc.items.push_back(foe);
    }
    if (party.mineRight >= 0)
    {
        mine.scale = 0.75f;
        mine.dx = party.mineRight - mine.sw * mine.scale;
        mine.dy = party.mineTop;
        mine.drop = allBackgrounds;
        sc.items.push_back(mine);
    }
}

// The strip's rounded buttons: the command menu's (Commands) and the ones below the moves (back,
// SHIFT, the rotation's), as the battle's other buttons are rounded (the moves, the Pokemon a
// move is aimed at, the party). Each is in the colours of the game's own button, read off the
// touch screen every frame (its inside, its outline), so that it lights up as the game's does
// while it is pressed and dims with it, with the game's own label or icon on it: its colours and
// its light greys, the dark around them left out. A click anywhere on one goes to the game's
// button.
struct RoundButton
{
    int label[4];            // its label or icon on the touch screen (x, y, w, h)
    int touch[4];            // where a click on it goes (x, y, w, h: inside the game's button)
    int fill[2];             // where its inside is read (4 x 4 pixels of its pattern, averaged)
    int outline[2];          // where its outline is read
    uint32_t outlineColour;  // ... its colour where a sprite covers that pixel
    float labelScale;        // the label's size, times the grid's
    uint32_t key;            // a colour of the label's surroundings not drawn either (0: none)
    bool plain;              // the label drawn whole (the dark around it is the inside's colour)
};

// Averages a 4 x 4 pixel patch on the touch screen so fill tones follow button press/dim animations.
inline void SampleFillRGB(const uint32_t* px, const int pt[2], unsigned& r, unsigned& g, unsigned& bl)
{
    r = g = bl = 0;
    for (int j = 0; j < 4; j++)
        for (int i = 0; i < 4; i++)
        {
            const uint32_t c = px[(pt[1] + j) * 256 + pt[0] + i];
            r += (c >> 16) & 0xFF;
            g += (c >> 8) & 0xFF;
            bl += c & 0xFF;
        }
    r /= 16;
    g /= 16;
    bl /= 16;
}

// A stretched FillRect across fractional view bounds (x, y, w, h).
inline Element MakePlateRect(float x, float y, float w, float h, uint32_t rgb)
{
    Element e = FillRect(x, y, 1000, std::max(1, (int)std::lround(1000 * h / std::max(w, 0.01f))), rgb);
    e.scale = w / 1000;
    return e;
}

// The rounded button b in the view's rectangle (x, y, w, h), its label at s * b.labelScale, shift
// view pixels right of the middle. The label's left edge in the view.
inline float Round(Scene& sc, const ScreenImage& touch, const RoundButton& b, float x, float y, float w, float h,
    float s, float shift = 0, bool authentic = false)
{
    const uint32_t* px = touch.pixels;
    const uint8_t* fl = touch.flags;
    unsigned r = 0, g = 0, bl = 0;
    SampleFillRGB(px, b.fill, r, g, bl);
    Element e = MakePlateRect(x, y, w, h, (r << 16) | (g << 8) | bl);
    e.border = true;
    const int o = b.outline[1] * 256 + b.outline[0];
    e.borderColor = fl && LayerClass(fl[o]) == LAYER_OBJ ? b.outlineColour : px[o] & 0xFFFFFF;
    e.borderRadius = 2.5f;
    sc.items.push_back(e);
    if (authentic)
    {
        // Subtle metallic bevel + B2W2 horizontal accent trim framing a dark high-contrast center band.
        const uint32_t bevel = (((std::min(255u, r + 28)) << 16) | (std::min(255u, g + 28) << 8) | std::min(255u, bl + 28));
        Element topHi = MakePlateRect(x + 2.0f, y + 1.5f, std::max(1.0f, w - 4.0f), std::max(0.6f, h * 0.04f), bevel);
        topHi.borderRadius = 0.5f;
        sc.items.push_back(topHi);
        if (b.outlineColour != 0xFBFBFB && b.outlineColour != 0xF3F3FB)
        {
            const uint32_t barCol = b.key ? b.key : b.outlineColour;
            const float barH = std::max(1.2f, h * 0.08f);
            const float barW = std::max(2.0f, w - 3.0f);
            sc.items.push_back(MakePlateRect(x + 1.5f, y + h * 0.22f, barW, barH, barCol));
            sc.items.push_back(MakePlateRect(x + 1.5f, y + h * 0.78f - barH, barW, barH, barCol));
        }
    }
    // (At most as large as leaves it 3 grid pixels clear of the button's sides. Expand single-line
    // word labels safely within the button's interior so localized labels like ¡LUCHAR! or BOLSA
    // are never clipped.)
    int lx = b.label[0], ly = b.label[1], lw = b.label[2], lh = b.label[3];
    if (lh <= 18 && b.key == 0 && b.touch[2] > lw)
    {
        const int bgLum = (int)(r + g + bl);
        const int safeX0 = std::clamp(b.touch[0] + 4, 0, lx);
        const int safeX1 = std::clamp(b.touch[0] + b.touch[2] - 4, lx + lw, 256);
        int tx0 = lx, tx1 = lx + lw;
        for (int yy = ly; yy < ly + lh && yy < 192; yy++)
            for (int xx = safeX0; xx < safeX1; xx++)
            {
                const uint32_t c = px[yy * 256 + xx];
                const int cr = (int)((c >> 16) & 0xFF), cg = (int)((c >> 8) & 0xFF), cb = (int)(c & 0xFF);
                const int mx = std::max({ cr, cg, cb }), mn = std::min({ cr, cg, cb });
                if (mx > 130 && ((cr + cg + cb) - bgLum > 120 || mx - mn > 45))
                {
                    tx0 = std::min(tx0, xx);
                    tx1 = std::max(tx1, xx + 1);
                }
            }
        lx = tx0;
        lw = tx1 - tx0;
    }
    const float ls = std::min({ s * b.labelScale, (w - 6 * s) / lw, (h - 6 * s) / lh });
    Element l = Crop(1, lx, ly, lw, lh);
    l.scale = ls;
    l.dx = x + (w - lw * ls) / 2 + shift;
    l.dy = y + (h - lh * ls) / 2;
    if (!b.plain)
    {
        // (Greys as light as the inside and a little lighter: the dark around the label, and
        // any outlines of its letters.)
        l.keyGrey = (int)std::max({ r, g, bl }) + 40;
        if (b.key) l.keys[0] = b.key;
    }
    sc.items.push_back(l);
    return l.dx;
}

// The cursor's brackets (sprites, 16 x 16 at the corners of its box x0-x1, y0-y1 on the touch
// screen, their outlines included) at the corners of the view's rectangle (x, y, w, h), 3 grid
// pixels outside it, at the grid's scale s.
inline void Brackets(Scene& sc, int x0, int y0, int x1, int y1, float x, float y, float w, float h, float s)
{
    for (int k = 0; k < 4; k++)
    {
        const bool right = k & 1, down = k & 2;
        const int sx = std::clamp(right ? x1 - 14 : x0 - 1, 0, 240), sy = std::clamp(down ? y1 - 14 : y0 - 1, 0, 176);
        // (The box's corner pixel on the rectangle's corner pixel, moved out.)
        const float cx = right ? x + w - s + 3 * s : x - 3 * s, cy = down ? y + h - s + 3 * s : y - 3 * s;
        Element e = Crop(1, sx, sy, 16, 16);
        e.scale = s;
        e.dx = cx - ((right ? x1 : x0) - sx) * s;
        e.dy = cy - ((down ? y1 : y0) - sy) * s;
        e.drop = LAYER_BG0 | LAYER_BG1 | LAYER_BG2 | LAYER_BG3 | LAYER_BACKDROP;
        sc.items.push_back(e);
    }
}

// Where a click on the view's rectangle (x, y, w, h) goes: onto t (x, y, w, h) of the touch
// screen, laid over it and not drawn.
inline void TouchArea(Scene& sc, const int t[4], float x, float y, float w, float h)
{
    Element e = Crop(1, t[0], t[1], t[2], t[3]);
    e.dx = x;
    e.dy = y;
    e.scale = h / t[3];
    e.stretchX = w / (t[2] * e.scale);
    e.alpha = 0;
    sc.items.push_back(e);
}

// The command menu as the strip draws it, in both games: FIGHT!, BAG, RUN and POKEMON as rounded
// buttons (Round) on the moves' grid, so that the menu and the moves take the same rows of the
// band: FIGHT! across the upper row and BAG, RUN and POKEMON under it, left to right, as the
// touch screen has them (the D-pad moves the cursor as it looks). FIGHT! has the icons of the
// player's Pokemon in a double or triple battle beside its label (the one being given its
// command lit), as the touch screen has them below it. The cursor's brackets come to the corners
// of the button it is on.
struct CommandLook
{
    int cell[4][4];          // FIGHT!, BAG, RUN, POKEMON on the moves' grid (x, y, w, h)
    RoundButton button[4];
    int art[4][4];           // authentic DS crops (x, y, w, h): FIGHT! banner, BAG, RUN, POKEMON
    uint32_t cursor;         // the colour of the cursor's brackets
};

// The four buttons for `look`, the grid's touch-screen pixel (x, y) at (left + x * s, top + y * s).
// authentic: draw authentic BW1 / B2W2 FIGHT! metallic plate and real DS BAG/RUN/POKEMON shapes
// instead of flat Round() plates.
inline void Commands(Scene& sc, const ScreenImage& touch, const CommandLook& look, float left, float top, float s,
    bool authentic = false)
{
    const uint32_t* px = touch.pixels;
    const uint8_t* fl = touch.flags;
    if (!px) return;
    if (SmButtons::S().on)
    {
        // BATTLE BUTTONS: SUN AND MOON (sm_buttons.h): the four cells' room, the buttons shown
        // FIGHT, POKEMON, BAG, RUN (the cells' FIGHT!, BAG, RUN, POKEMON), each over its game button.
        float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
        for (const auto& c : look.cell)
        {
            x0 = std::min(x0, left + c[0] * s); y0 = std::min(y0, top + c[1] * s);
            x1 = std::max(x1, left + (c[0] + c[2]) * s); y1 = std::max(y1, top + (c[1] + c[3]) * s);
        }
        static const int from[SmButtons::COUNT] = { 0, 3, 1, 2 };
        int t[SmButtons::COUNT][4];
        for (int b = 0; b < SmButtons::COUNT; b++)
            for (int k = 0; k < 4; k++) t[b][k] = look.button[from[b]].touch[k];
        SmButtons::Draw(sc, x0, y0, x1 - x0, y1 - y0, t, false);
        return;
    }
    // The icons of a double or triple battle (sprites, x 78-176, rows 109-125; none in a single
    // battle, the player's row of balls starting at row 127).
    int ix0 = 256, iy0 = 192, ix1 = -1, iy1 = -1;
    if (fl)
        for (int y = 100; y < 127; y++)
            for (int x = 60; x < 197; x++)
                if (LayerClass(fl[y * 256 + x]) == LAYER_OBJ)
                {
                    ix0 = std::min(ix0, x); iy0 = std::min(iy0, y);
                    ix1 = std::max(ix1, x); iy1 = std::max(iy1, y);
                }
    const bool icons = ix1 - ix0 >= 8;
    auto rect = [&](int i, float& x, float& y, float& w, float& h) {
        const int* c = look.cell[i];
        x = left + c[0] * s; y = top + c[1] * s; w = c[2] * s; h = c[3] * s;
    };
    float drawnRect[4][4] = {};
    for (int i = 0; i < 4; i++)
    {
        float x, y, w, h;
        rect(i, x, y, w, h);
        drawnRect[i][0] = x; drawnRect[i][1] = y; drawnRect[i][2] = w; drawnRect[i][3] = h;
        if (authentic && i == 0)
        {
            unsigned r = 0, g = 0, bl = 0;
            SampleFillRGB(px, look.button[0].fill, r, g, bl);
            Element fightBg = MakePlateRect(x, y, w, h, (r << 16) | (g << 8) | bl);
            fightBg.border = true;
            fightBg.borderRadius = 2.0f;
            fightBg.borderColor = look.button[0].outlineColour;
            fightBg.borderWidth = 5;
            sc.items.push_back(fightBg);

            const int* fa = look.art[0];
            float fs = std::min((h - 2.0f) / fa[3], (w - 4.0f) / fa[2]);
            if (icons)
            {
                const int iw = ix1 - ix0 + 1, ih = iy1 - iy0 + 1;
                const float gap = 8.0f * s;
                float is = std::min(s * look.button[0].labelScale, (h - 2.0f) / ih);
                const float rawW = fa[2] * fs + iw * is;
                if (rawW + gap > w - 4.0f && rawW > 0.01f)
                {
                    const float fit = (w - 4.0f - gap) / rawW;
                    fs *= fit;
                    is *= fit;
                }
                const float totalW = fa[2] * fs + gap + iw * is;
                Element banner = Crop(1, fa[0], fa[1], fa[2], fa[3]);
                banner.scale = fs;
                banner.dx = x + (w - totalW) / 2.0f;
                banner.dy = y + (h - fa[3] * fs) / 2.0f;
                sc.items.push_back(banner);

                Element ic = Crop(1, ix0, iy0, iw, ih);
                ic.scale = is;
                ic.dx = banner.dx + fa[2] * fs + gap;
                ic.dy = y + (h - ih * is) / 2.0f;
                ic.drop = LAYER_BG0 | LAYER_BG1 | LAYER_BG2 | LAYER_BG3 | LAYER_BACKDROP;
                sc.items.push_back(ic);
            }
            else
            {
                Element banner = Crop(1, fa[0], fa[1], fa[2], fa[3]);
                banner.scale = fs;
                banner.dx = x + (w - fa[2] * fs) / 2.0f;
                banner.dy = y + (h - fa[3] * fs) / 2.0f;
                sc.items.push_back(banner);
            }
        }
        else if (authentic)
        {
            const int* a = look.art[i];
            const float scA = std::min(w / a[2], h / a[3]);
            const float aw = a[2] * scA, ah = a[3] * scA;
            const float ax = x + (w - aw) / 2.0f, ay = y + (h - ah) / 2.0f;
            Element e = Crop(1, a[0], a[1], a[2], a[3]);
            e.dx = ax;
            e.dy = ay;
            e.scale = scA;
            e.drop = LAYER_BACKDROP;
            if (!fl)
            {
                e.border = true;
                e.borderRadius = 2.0f;
                e.borderColor = look.button[i].outlineColour;
            }
            sc.items.push_back(e);
            drawnRect[i][0] = ax; drawnRect[i][1] = ay; drawnRect[i][2] = aw; drawnRect[i][3] = ah;
        }
        else if (i == 0 && icons)
        {
            // The label and the icons side by side, 10 pixels apart, in the middle together.
            const RoundButton& b = look.button[0];
            const float ls = s * b.labelScale;
            const int iw = ix1 - ix0 + 1, ih = iy1 - iy0 + 1, gap = 10;
            const float labelX = Round(sc, touch, b, x, y, w, h, s, -(gap + iw) * ls / 2);
            Element e = Crop(1, ix0, iy0, iw, ih);
            e.scale = ls;
            e.dx = labelX + (b.label[2] + gap) * ls;
            e.dy = y + (h - ih * ls) / 2;
            e.drop = LAYER_BG0 | LAYER_BG1 | LAYER_BG2 | LAYER_BG3 | LAYER_BACKDROP;
            sc.items.push_back(e);
        }
        else
            Round(sc, touch, look.button[i], x, y, w, h, s);
    }
    // The cursor's box: FIGHT!'s is the whole upper part of the screen (the only one that tall);
    // BAG's, RUN's and POKEMON's are below row 120, left, middle and right.
    int x0, y0, x1, y1;
    if (CursorBox(px, look.cursor, x0, y0, x1, y1))
    {
        const int middle = (x0 + x1) / 2;
        const int on = y1 - y0 >= 80 ? 0 : y0 < 120 ? -1 : middle < 85 ? 1 : middle > 171 ? 3 : 2;
        if (on >= 0)
            Brackets(sc, x0, y0, x1, y1, drawnRect[on][0], drawnRect[on][1], drawnRect[on][2], drawnRect[on][3], s);
    }
    for (int i = 0; i < 4; i++)
    {
        float x, y, w, h;
        rect(i, x, y, w, h);
        TouchArea(sc, look.button[i].touch, x, y, w, h);
    }
}

// A button the touch screen has below the moves (back, SHIFT, the rotation's), as the strip
// draws it: a rounded button (Round) as tall as the moves' two rows, left of them.
struct BelowButton
{
    RoundButton look;
    int x0, x1;              // its columns on the touch screen (where its cursor is)
    int width;               // its width on the grid
};

// The buttons below the moves, `n` of them in the touch screen's order, right to left from the
// view's x `from` (gap view pixels apart, at the grid's scale s), in the rows y to y + h; the
// cursor's brackets (colour `cursor`) on the one it is on.
inline void Below(Scene& sc, const ScreenImage& touch, const BelowButton* const* buttons, int n, float from, float y,
    float h, float s, float gap, uint32_t cursor, bool authentic = false)
{
    int x0, y0, x1, y1;
    const bool on = touch.pixels && CursorBox(touch.pixels, cursor, x0, y0, x1, y1) && y0 >= 132;
    float x = from;
    for (int i = n - 1; i >= 0; i--)
    {
        const BelowButton& b = *buttons[i];
        const float w = b.width * s;
        x -= gap + w;
        Round(sc, touch, b.look, x, y, w, h, s, 0, authentic);
        if (on && (x0 + x1) / 2 >= b.x0 && (x0 + x1) / 2 <= b.x1) Brackets(sc, x0, y0, x1, y1, x, y, w, h, s);
        TouchArea(sc, b.look.touch, x, y, w, h);
    }
}

// Black 2 and White 2's command menu (Commands): the touch screen's FIGHT! plate (x 82-173, rows
// 50-138: inside 0x202020, outline 0xF3F3FB, its banner x 84-171, rows 87-104) and BAG, RUN and
// POKEMON (hexagons of dotted grey, 0x282828 and 0x383838, each with a bar across it in its
// label's colour, 0xFBCB28, 0x20A2FB and 0x71DB30, the label on the bar).
inline const CommandLook& Black2Commands()
{
    static const CommandLook look = {
        { { 2, 35, 252, 42 }, { 2, 83, 80, 42 }, { 88, 83, 80, 42 }, { 174, 83, 80, 42 } },
        {
            { { 84, 87, 88, 18 }, { 100, 60, 56, 60 }, { 126, 66 }, { 128, 53 }, 0xF3F3FB, 1.2f, 0, false },
            { { 23, 163, 32, 10 }, { 8, 150, 60, 36 }, { 38, 148 }, { 60, 160 }, 0xFBCB28, 1.2f, 0, false },
            { { 112, 175, 32, 10 }, { 100, 158, 56, 30 }, { 126, 160 }, { 128, 173 }, 0x20A2FB, 1.2f, 0, false },
            { { 190, 161, 57, 11 }, { 188, 150, 60, 36 }, { 214, 148 }, { 185, 160 }, 0x71DB30, 1.2f, 0, false },
        },
        { { 82, 74, 92, 34 }, { 6, 146, 68, 40 }, { 94, 154, 68, 36 }, { 182, 146, 68, 40 } },
        0xC31028,
    };
    return look;
}

// Black 2 and White 2's buttons below the moves (Below): SHIFT (x 0-78, shaped and coloured as
// BAG is, its label x 25-58, rows 164-171), the two that turn the Pokemon round in a rotation
// battle (x 8-71 and 88-151, grey, 0x929292, a white icon across a bar whose light lines, 0xD3D3D3,
// run through it) and back (x 176-255, blue, 0x20A2FB, its white arrow x 210-220, rows 162-173).
inline const BelowButton& Black2Shift()
{
    static const BelowButton b = { { { 24, 163, 36, 10 }, { 8, 150, 60, 36 }, { 38, 148 }, { 60, 160 }, 0xFBCB28, 1.5f, 0, false }, 0, 78, 70 };
    return b;
}
inline const BelowButton& Black2Turn(int which)
{
    static const BelowButton b[2] = {
        { { { 28, 160, 26, 30 }, { 20, 160, 40, 28 }, { 22, 163 }, { 40, 156 }, 0x929292, 1.5f, 0xD3D3D3, false }, 8, 71, 56 },
        { { { 108, 160, 26, 30 }, { 100, 160, 40, 28 }, { 102, 163 }, { 120, 156 }, 0x929292, 1.5f, 0xD3D3D3, false }, 88, 151, 56 },
    };
    return b[which];
}
inline const BelowButton& Black2Back()
{
    static const BelowButton b = { { { 209, 161, 13, 14 }, { 186, 150, 60, 36 }, { 228, 149 }, { 230, 160 }, 0x20A2FB, 2.0f, 0, false }, 176, 255, 56 };
    return b;
}

// Black and White's command menu (Commands): the FIGHT! lens (x 51-204, rows 63-112, inside
// 0x413030, its label x 101-154, rows 82-92, outlined in black on it: drawn whole) and the octagons
// of BAG, RUN and POKEMON (the dotted grey, 0x383838 to 0x494949, outlined in white, 0xFBFBFB, as
// the moves are; FIGHT! too).
inline const CommandLook& BlackCommands()
{
    static const CommandLook look = {
        { { 2, 34, 252, 44 }, { 2, 82, 80, 44 }, { 88, 82, 80, 44 }, { 174, 82, 80, 44 } },
        {
            { { 100, 81, 56, 13 }, { 70, 70, 116, 36 }, { 73, 84 }, { 40, 135 }, 0xFBFBFB, 1.2f, 0, true },
            { { 29, 163, 22, 10 }, { 12, 145, 56, 40 }, { 38, 148 }, { 40, 135 }, 0xFBFBFB, 1.2f, 0, false },
            { { 116, 173, 23, 10 }, { 100, 158, 56, 30 }, { 126, 160 }, { 128, 151 }, 0xFBFBFB, 1.2f, 0, false },
            { { 195, 160, 52, 13 }, { 188, 145, 56, 40 }, { 214, 148 }, { 216, 135 }, 0xFBFBFB, 1.2f, 0, false },
        },
        { { 54, 70, 148, 34 }, { 3, 134, 72, 52 }, { 91, 150, 74, 40 }, { 180, 134, 72, 52 } },
        0xFB0092,
    };
    return look;
}

// Black and White's buttons below the moves (Below), octagons as BAG, RUN and POKEMON are: SHIFT
// (x 0-77, its label x 25-55, rows 164-171), the rotation's (x 2-77 and 82-157, grey icons x 26-53,
// rows 160-190) and back (x 178-255, its blue arrow x 203-225, rows 154-173).
inline const BelowButton& BlackShift()
{
    static const BelowButton b = { { { 24, 163, 33, 10 }, { 12, 145, 56, 40 }, { 38, 148 }, { 40, 135 }, 0xFBFBFB, 1.5f, 0, false }, 0, 77, 64 };
    return b;
}
inline const BelowButton& BlackTurn(int which)
{
    static const BelowButton b[2] = {
        { { { 25, 160, 31, 31 }, { 14, 155, 52, 34 }, { 57, 170 }, { 40, 151 }, 0xFBFBFB, 1.5f, 0, false }, 2, 77, 56 },
        { { { 105, 160, 31, 31 }, { 94, 155, 52, 34 }, { 137, 170 }, { 120, 151 }, 0xFBFBFB, 1.5f, 0, false }, 82, 157, 56 },
    };
    return b[which];
}
inline const BelowButton& BlackBack()
{
    static const BelowButton b = { { { 202, 153, 25, 22 }, { 190, 145, 56, 40 }, { 214, 148 }, { 216, 135 }, 0xFBFBFB, 1.5f, 0, false }, 178, 255, 56 };
    return b;
}

// Whether Black 2 / White 2's battle touch screen has finished sliding its command or move
// buttons into place (or flags are unavailable). Avoids showing mid-animation Poke Ball slices.
inline bool Black2ButtonsReady(const ScreenImage& touch, bool& gridOut)
{
    const uint8_t* fl = touch.flags;
    if (!fl) return true;
    auto layer = [&](int x, int y) { return LayerClass(fl[y * 256 + x]); };
    const bool commands = layer(39, 170) == LAYER_BG0 && layer(127, 180) == LAYER_BG0 &&
                          layer(216, 170) == LAYER_BG0 && layer(128, 70) == LAYER_BACKDROP &&
                          layer(39, 146) == LAYER_BG0 && layer(127, 156) == LAYER_BG0 && layer(216, 146) == LAYER_BG0;
    const bool buttons = layer(40, 74) == LAYER_BG1 && layer(216, 74) == LAYER_BG1 &&
                         layer(40, 120) == LAYER_BG1 && layer(216, 120) == LAYER_BG1 &&
                         layer(0, 80) == LAYER_BACKDROP && layer(255, 80) == LAYER_BACKDROP;
    if (!commands && !buttons) return false;
    gridOut = buttons;
    return true;
}

// Black 2 and White 2's battle buttons (STRIP / STANDARD / SUN AND MOON), on the message box's
// band (rows 148-187) right of its text: party Poké Ball rows under both HP boxes, Commands
// (authentic DS plates when authentic=true, or flat Round() / SmButtons) on command menus, and
// real hexagonal move/target cards (drop = LAYER_BACKDROP) plus SHIFT / Rotation / Back on move
// and target menus.
inline void Black2(Scene& sc, float viewRight, const ScreenImage* screens, bool grid, const PartyPlaces& party,
    float availRoom = -1.0f, bool authentic = true)
{
    const uint8_t* fl = screens[1].flags;
    auto layer = [&](int x, int y) { return fl ? LayerClass(fl[y * 256 + x]) : 0; };
    if (fl && !Black2ButtonsReady(screens[1], grid)) return;
    const float right = viewRight - 2;
    // The buttons are drawn smaller where they would reach the message (a long name on a narrow
    // view): at least 4 pixels clear of it. When AdaptGen5BattlePrompt already resized the tray,
    // use its computed room directly.
    const float room = availRoom > 0.0f ? availRoom : (right - (MessageEnd(screens[0]) + 4));
    // The rows of balls: the foe's x 105-151, rows 44-51 on the command menu and 20-27 on the
    // others; the player's x 80-171, rows 128-142.
    PartyRows(sc, party, Crop(1, 104, grid ? 19 : 43, 49, 10), Crop(1, 79, 127, 94, 16));
    if (!grid)
    {
        const float s = std::clamp(room / 256, 0.36f, 0.44f);
        Commands(sc, screens[1], Black2Commands(), right - 256 * s, 148 + (40 - 96 * s) / 2 - 32 * s, s, authentic);
        return;
    }
    const BelowButton* below[3];
    int n = 0;
    if (layer(39, 175) == LAYER_BG0 && layer(39, 150) == LAYER_BG0) below[n++] = &Black2Shift();
    else if (layer(39, 175) == LAYER_BG0) below[n++] = &Black2Turn(0);
    if (layer(120, 175) == LAYER_BG0) below[n++] = &Black2Turn(1);
    const bool back = layer(216, 175) == LAYER_BG0;
    const float gap = 3;
    auto fit = [&](bool withBack) {
        float w = 256, k = 0;
        for (int i = 0; i < n; i++) { w += below[i]->width; k++; }
        if (withBack) { w += Black2Back().width; k++; }
        return (room - gap * k) / w;
    };
    const bool drawBack = back && (fit(true) >= 0.36f || !fl);
    const float s = std::max(0.36f, std::min(0.44f, fit(drawBack)));
    if (drawBack) below[n++] = &Black2Back();
    const float left = right - 256 * s, top = 148 + (40 - 96 * s) / 2 - 32 * s;
    Element buttons = Crop(1, 0, 32, 256, 96);
    buttons.scale = s;
    buttons.dx = left;
    buttons.dy = top + 32 * s;
    buttons.drop = LAYER_BACKDROP;
    sc.items.push_back(buttons);
    Below(sc, screens[1], below, n, left, top + 35 * s, 90 * s, s, gap, 0xC31028, authentic);
}
} // namespace Gen5Strip
