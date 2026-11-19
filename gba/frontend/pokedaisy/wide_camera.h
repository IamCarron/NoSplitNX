// ZOOM and the look-around (PokeDaisy's WideCamera.kt): which window of the scene the screen shows each frame.
// pk_wide.c draws any window around the GBA's own 240 x 160 frame from the game's memory; this picks it.
//
// - Zoom steps (level): 1x is the game as large as the screen shows all of it (its full height on a wide screen);
//   each step out is a whole number of screen pixels per GBA pixel, so the pixel art stays crisp, and shows at least
//   1.2x as much as the one before, inside pk_wide.h's largest window.
// - Close up when it matters: the game wanting it (pk_wide_scene: a battle starting, a menu screen, a text box) pulls
//   in to 1x and the player, one eased motion per event; the chosen step comes back as gently once that has been over
//   for half a second.
// - Look-around (the right stick): standing, it moves the view, which stays where it's left; walking glides it back
//   to the player, or with the stick held follows the stick but keeps the player on screen.
//
// Based on PokeDaisy by Lidor Itzhari - https://github.com/lidor30/pokedaisy (GNU GPL version 3).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace PkWideCamera
{
// pk_wide.h's PK_WIDE_SCENE_*.
enum { SCENE_FIELD, SCENE_TEXT, SCENE_OVERLAY, SCENE_BATTLE_START, SCENE_OFF, SCENE_FADE, SCENE_MENU };

// What the screen shows: the window pk_wide draws (x, y, w, h in the frame's GBA pixels), placed on the screen at
// (offX, offY) screen pixels from its top-left (<= 0: the window's whole pixels reach past the screen by the fraction
// the look-around or zoom has moved), scale screen pixels per GBA pixel.
struct Window { int x = 0, y = 0, w = 0, h = 0; float offX = 0, offY = 0, scale = 0; bool valid = false; };

class Camera
{
public:
    static constexpr int FrameW = 240, FrameH = 160, MaxW = 800, MaxH = 800;

    int viewW = 0, viewH = 0;  // the screen's size, in pixels
    int level = 0;             // the zoom step picked: 0 = 1x
    float stickX = 0, stickY = 0; // the right stick, -1..1 (right / down positive)
    bool walking = false;      // a direction is held
    bool zoomOnText = true;    // AUTO ZOOM IN: a text box pulls in to 1x
    bool lookAround = true;    // LOOK AROUND off: the stick does nothing

    // Screen pixels per GBA pixel for each zoom step, 1x first.
    std::vector<float> Levels() const { return LevelsFor(viewW, viewH); }

    // How much more of the map step i shows than 1x ("1.7x").
    float Factor(int i) const
    {
        const auto l = Levels();
        return i >= 0 && i < (int)l.size() ? l[0] / l[i] : 1.0f;
    }

    // The next step out, back to 1x after the last; returns the new step.
    int Cycle()
    {
        const int n = (int)Levels().size();
        level = n <= 1 ? 0 : (level + 1) % n;
        return level;
    }

    // Back to 1x on the player at once (a new game, a state load); the step picked comes back once the field shows.
    void Reset()
    {
        closeOverSince = 0;
        scale = 0;
        lookX = lookY = 0;
        lastNanos = 0;
        tweenTarget = 0;
    }

    // Whether the window is the plain 1x one on the player (nothing to draw past the game's own picture).
    bool Plain() const { return scale > 0 && Levels().size() && std::fabs(scale - Levels()[0]) < 0.002f && lookX == 0 && lookY == 0; }

    // Before a frame: the window to draw given what the last frame was (pk_wide_scene); now in nanoseconds.
    Window Frame(int scene, int64_t now)
    {
        Window out;
        if (viewW <= 0 || viewH <= 0) return out;
        const std::vector<float> steps = Levels();
        const float fit = steps[0];
        const float dt = lastNanos == 0 ? 0.0f : std::clamp((float)((now - lastNanos) / 1e9), 0.0f, 0.1f);
        lastNanos = now;

        const bool wantsClose = scene == SCENE_FIELD ? false : scene == SCENE_TEXT ? zoomOnText : true;
        // (A fade decides nothing; nor does a picture over the field, FireRed's map preview.)
        if (scene == SCENE_FADE || scene == SCENE_OVERLAY) {}
        else if (wantsClose) closeOverSince = 0;
        else if (closeOverSince == 0) closeOverSince = now;
        const bool close = closeOverSince == 0 || now - closeOverSince < ReturnDelayNanos;
        const float target = close ? fit : steps[std::clamp(level, 0, (int)steps.size() - 1)];
        // A timed ease-in-out in log space: a pull-in at the pace of what's coming, the way back out slower.
        if (target != tweenTarget)
        {
            tweenFrom = scale <= 0 ? target : scale;
            tweenTarget = target;
            tweenStart = now;
            tweenNanos = scale <= 0 ? 0 : target > tweenFrom ? (scene == SCENE_BATTLE_START ? PullInBattleNanos : PullInNanos) : ZoomOutNanos;
        }
        const float p = tweenNanos <= 0 ? 1.0f : std::clamp((float)(now - tweenStart) / (float)tweenNanos, 0.0f, 1.0f);
        scale = tweenFrom * std::pow(tweenTarget / tweenFrom, p * p * (3 - 2 * p));
        if (std::fabs(scale - target) < 0.002f) scale = target;

        const float fw = viewW / scale, fh = viewH / scale;
        const float sx = lookAround ? DeadZone(stickX) : 0, sy = lookAround ? DeadZone(stickY) : 0;
        const bool stick = sx != 0 || sy != 0;
        if (close) Spring(0, 0, dt, LookTau);
        else if (walking && stick)
            Spring(sx * std::max(0.0f, fw / 2 - PlayerKeep), sy * std::max(0.0f, fh / 2 - PlayerKeep), dt, LookTau);
        else if (walking) Spring(0, 0, dt, ReturnTau);
        else if (stick)
        {
            const float speed = LookSpeed * std::max(1.0f, fit / scale); // (further out, the view travels faster)
            lookX += sx * speed * dt;
            lookY += sy * speed * dt;
        }
        // As far as a screen's worth of map past the game's own view each way.
        const float limX = std::max(fw, (float)FrameW), limY = std::max(fh, (float)FrameH);
        lookX = std::clamp(lookX, -limX, limX);
        lookY = std::clamp(lookY, -limY, limY);

        const float fx = FrameW / 2.0f + lookX - fw / 2, fy = FrameH / 2.0f + lookY - fh / 2;
        out.x = (int)std::floor(fx);
        out.y = (int)std::floor(fy);
        out.w = std::min(MaxW, (int)std::ceil(fx + fw) - out.x);
        out.h = std::min(MaxH, (int)std::ceil(fy + fh) - out.y);
        out.offX = (out.x - fx) * scale;
        out.offY = (out.y - fy) * scale;
        out.scale = scale;
        out.valid = true;
        return out;
    }

    // Screen pixels per game pixel for each step on a w x h screen: 1x (the whole frame as large as it fits), then
    // the whole numbers below it that show at least 1.2x more each and fit the largest window.
    static std::vector<float> LevelsFor(int w, int h)
    {
        if (w <= 0 || h <= 0) return { 1.0f };
        const float fit = std::min(w / (float)FrameW, h / (float)FrameH);
        std::vector<float> out = { fit };
        int k = (int)std::floor(fit);
        if ((float)k == fit) k--;
        for (; k >= 1; k--)
        {
            if (w / (float)k > MaxW || h / (float)k > MaxH) break;
            if (out.back() / k >= MinStep) out.push_back((float)k);
        }
        return out;
    }

private:
    static constexpr float MinStep = 1.2f;
    static constexpr int64_t PullInNanos = 420000000, PullInBattleNanos = 550000000, ZoomOutNanos = 650000000;
    static constexpr int64_t ReturnDelayNanos = 500000000;
    static constexpr float LookTau = 0.12f, ReturnTau = 0.18f, LookSpeed = 150.0f, PlayerKeep = 28.0f, DeadZoneSize = 0.2f;

    float scale = 0, lookX = 0, lookY = 0;
    int64_t lastNanos = 0, closeOverSince = 0, tweenStart = 0, tweenNanos = 0;
    float tweenFrom = 0, tweenTarget = 0;

    void Spring(float tx, float ty, float dt, float tau)
    {
        const float k = 1.0f - std::exp(-dt / tau);
        lookX += (tx - lookX) * k;
        lookY += (ty - lookY) * k;
        if (std::hypot(lookX - tx, lookY - ty) < 0.05f) { lookX = tx; lookY = ty; }
    }
    static float DeadZone(float v)
    {
        const float a = std::fabs(v);
        if (a < DeadZoneSize) return 0;
        return (a - DeadZoneSize) / (1 - DeadZoneSize) * (v < 0 ? -1.0f : 1.0f);
    }
};
} // namespace PkWideCamera
