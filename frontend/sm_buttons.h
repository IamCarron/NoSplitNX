// BATTLE BUTTONS: SUN AND MOON. The command menu's buttons drawn in the look of Pokemon Sun and
// Moon's: FIGHT across the top (red), POKEMON, BAG and RUN in a row under it (green, yellow,
// blue); each a rounded button with a dark outline, a darker lip along its bottom and a light
// line inside its top, its word in heavy dark letters. Drawn by FullDex (the compositor's
// rounded rectangles; the words from a picture FullDex makes, sm_buttons_art.h, given to the
// compositor as screen kScreen), not taken from either game.
//
// The chosen button has a white outline. While the buttons are up the D-pad moves it as they are
// laid out and A touches the game's own button (Press, from main.cpp's battle keys); a click or
// tap on a button reaches the game's button through a piece of the touch screen laid over it and
// not drawn. Used by the Gen 4 strip (main.cpp's gen4BattleInput) and the Gen 5 strips
// (gen5strip.h's Commands, for Black, White, Black 2 and White 2).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "merged.h"

namespace SmButtons
{
// The buttons, in the order they are shown and chosen.
enum { FIGHT, POKEMON, BAG, RUN, COUNT };
// The screen index of the words' picture (main.cpp's screenImages).
constexpr int kScreen = 6;

struct Look { uint32_t fill, lip, outline, light; };
inline const Look& LookOf(int b)
{
    // (Measured from a picture of Sun and Moon's battle buttons.)
    static const Look looks[COUNT] = {
        { 0xE95245, 0x6C2622, 0x561E1B, 0xF47F72 },
        { 0x48C783, 0x29694D, 0x21543E, 0x8CE2B4 },
        { 0xE2B32F, 0x6B5216, 0x564112, 0xF0D682 },
        { 0x509AEA, 0x234766, 0x1C3952, 0x96C4F4 },
    };
    return looks[std::clamp(b, 0, COUNT - 1)];
}

// A word in the picture: its rectangle and where its capital letters are (rows from the top, and
// their height), for sizing it on a button.
struct Label { int x = 0, y = 0, w = 0, h = 0; float capTop = 0, capH = 0; };

struct State
{
    bool on = false;          // BATTLE BUTTONS is SUN AND MOON
    uint32_t clock = 0;       // main.cpp's frame, set every frame
    uint32_t shown = 0;       // the frame the buttons were last drawn (+1: 0 = never)
    bool fightOnly = false;   // only FIGHT (Platinum's first battle)
    int touch[COUNT][2] = {}; // where A touches the game's button for each
    int sel = 0;              // the chosen button
    Label label[COUNT];       // the words (sm_buttons_art.h)
};
inline State& S() { static State s; return s; }

// Whether the buttons are up now (drawn this frame or the last two).
inline bool Shown() { const State& s = S(); return s.shown && s.clock + 1 - s.shown <= 2; }

// The D-pad: left, right, up, down from each button, as they are laid out.
inline int Move(int from, int dir)
{
    static const int8_t to[COUNT][4] = { { FIGHT, FIGHT, FIGHT, BAG }, { POKEMON, BAG, FIGHT, POKEMON },
                                         { POKEMON, RUN, FIGHT, BAG }, { BAG, RUN, FIGHT, RUN } };
    const int t = to[std::clamp(from, 0, COUNT - 1)][dir & 3];
    return S().fightOnly ? FIGHT : t;
}

// A rectangle of one colour at any size (FillRect's are whole base pixels), corners rounded.
inline Element Rect(float x, float y, float w, float h, uint32_t rgb, float radius)
{
    Element e = FillRect(x, y, 1000, std::max(1, (int)std::lround(1000 * h / std::max(w, 0.01f))), rgb);
    e.scale = w / 1000;
    e.borderRadius = std::min(radius, std::min(w, h) / 2);
    return e;
}

// Button b in the view's rectangle (x, y, w, h); chosen: its outline white.
inline void Button(Scene& sc, int b, float x, float y, float w, float h, float unit, bool chosen)
{
    const Look& k = LookOf(b);
    const float r = 0.17f * unit, lip = std::max(0.6f, 0.065f * unit);
    Element body = Rect(x, y, w, h, k.lip, r);
    body.border = true;
    body.borderColor = chosen ? 0xFFFFFF : k.outline;
    body.borderWidth = chosen ? 14 : 5;
    sc.items.push_back(body);
    sc.items.push_back(Rect(x, y, w, h - lip, k.fill, r));
    const float lineY = y + 0.065f * unit, lineH = std::max(0.5f, 0.03f * unit);
    sc.items.push_back(Rect(x + r, lineY, w - 2 * r, lineH, k.light, lineH / 2));
    // The word: its capitals 0.31 of FIGHT's height, 0.30 of the others', narrower if it would
    // not fit with room at its ends; in the middle of the face.
    const Label& l = S().label[b];
    if (l.w <= 0 || l.capH <= 0) return;
    float s = (b == FIGHT ? 0.31f : 0.30f) * h / l.capH;
    s = std::min(s, (w - 2 * r) / l.w);
    Element word = Crop(kScreen, l.x, l.y, l.w, l.h);
    word.scale = s;
    word.dx = x + (w - l.w * s) / 2;
    word.dy = y + (h - lip) / 2 - (l.capTop + l.capH / 2) * s;
    sc.items.push_back(word);
}

// Where a click on the view's rectangle goes: onto t (x, y, w, h) of the touch screen, laid over
// it and not drawn.
inline void TouchOver(Scene& sc, const int t[4], float x, float y, float w, float h)
{
    if (t[2] <= 0 || t[3] <= 0) return;
    Element e = Crop(1, t[0], t[1], t[2], t[3]);
    e.dx = x;
    e.dy = y;
    e.scale = h / t[3];
    e.stretchX = w / (t[2] * e.scale);
    e.alpha = 0;
    sc.items.push_back(e);
}

// The four buttons in the view's rectangle (x, y, w, h), in Sun and Moon's proportions (FIGHT 195
// high to the row's 155, 25 apart; the row's three 17 apart in 1025). touch: the game's buttons
// on the touch screen (x, y, w, h) in the order shown; fightOnly: FIGHT alone.
inline void Draw(Scene& sc, float x, float y, float w, float h, const int touch[COUNT][4], bool fightOnly)
{
    State& st = S();
    const float fightH = h * 195 / 375, gapY = h * 25 / 375, rowH = h * 155 / 375, gapX = w * 17 / 1025;
    const float rowY = y + fightH + gapY, cellW = (w - 2 * gapX) / 3;
    st.fightOnly = fightOnly;
    if (fightOnly) st.sel = FIGHT;
    // (The rounding and the lip go with the row's height, the same on all four.)
    const float unit = rowH;
    float bx[COUNT] = { x, x, x + cellW + gapX, x + 2 * (cellW + gapX) };
    float by[COUNT] = { y, rowY, rowY, rowY }, bw[COUNT] = { w, cellW, cellW, w - 2 * (cellW + gapX) }, bh[COUNT] = { fightH, rowH, rowH, rowH };
    for (int b = 0; b < COUNT; b++)
    {
        if (fightOnly && b != FIGHT) continue;
        Button(sc, b, bx[b], by[b], bw[b], bh[b], unit, b == st.sel);
        TouchOver(sc, touch[b], bx[b], by[b], bw[b], bh[b]);
        st.touch[b][0] = touch[b][0] + touch[b][2] / 2;
        st.touch[b][1] = touch[b][1] + touch[b][3] / 2;
    }
    st.shown = st.clock + 1;
}
} // namespace SmButtons
