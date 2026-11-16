// ZOOM AND LOOK AROUND for the DS games: the overworld drawn smaller (1.5x, 2x out), the view moved with the right
// stick, and back close by itself for text, menus and battles. The drawing is melonDS's widescreen replay
// (GPU3D_Wide.cpp: WideState::Zoom, LookX, LookY); this is the camera that sets those, once a frame.
#pragma once
#include <algorithm>
#include <cmath>

struct DsZoom
{
    static constexpr int Levels = 3;
    static constexpr float Steps[Levels] = { 1.0f, 1.5f, 2.0f };
    static constexpr float MaxLook = 0.3f; // how far the view goes, in halves of the picture

    int level = 0;          // the step picked (0 = 1x)
    float zoom = 1.0f;      // as drawn now (eases to the step)
    float lookX = 0, lookY = 0;
    float closeFor = 0;     // seconds since text, a menu or a battle ended (the view waits a moment)

    int Cycle()
    {
        level = (level + 1) % Levels;
        return level;
    }
    float Factor(int l) const { return Steps[std::clamp(l, 0, Levels - 1)]; }
    void Reset() { zoom = 1.0f; lookX = lookY = 0; closeFor = 0; }

    // One frame: `close` while the view must be at 1x (text, a menu, a battle, not the overworld); the right
    // stick (x right, y down, -1..1) looks around; walking brings the view back to the player.
    void Update(bool close, float sx, float sy, bool walking, float dt)
    {
        dt = std::clamp(dt, 0.0f, 0.1f);
        if (close) closeFor = 0;
        else closeFor += dt;
        const bool held = close || closeFor < 0.4f; // (a moment after a text box closes before zooming back out)
        const float target = held ? 1.0f : Steps[level];
        // An ease in log space: in quickly, back out a little slower.
        const float rate = target < zoom ? 10.0f : 5.0f;
        zoom = std::exp(std::log(zoom) + (std::log(target) - std::log(zoom)) * std::min(1.0f, dt * rate));
        if (std::fabs(zoom - target) < 0.002f) zoom = target;

        const float dead = 0.2f;
        auto deadZone = [&](float v) { return std::fabs(v) < dead ? 0.0f : (v - std::copysign(dead, v)) / (1 - dead); };
        const float x = deadZone(sx), y = deadZone(sy);
        if (zoom <= 1.001f || held || (walking && x == 0 && y == 0))
        {
            const float k = std::min(1.0f, dt * (walking ? 4.0f : 8.0f));
            lookX -= lookX * k;
            lookY -= lookY * k;
        }
        else
        {
            lookX += x * dt * 1.2f;
            lookY -= y * dt * 1.2f; // (up on the stick looks up)
        }
        lookX = std::clamp(lookX, -MaxLook, MaxLook);
        lookY = std::clamp(lookY, -MaxLook, MaxLook);
        if (std::fabs(lookX) < 0.001f) lookX = 0;
        if (std::fabs(lookY) < 0.001f) lookY = 0;
    }
};
