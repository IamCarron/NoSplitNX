#pragma once

// Share edge detection across DS/GBA and all input sources. A held button must
// be released after a menu/focus transition before it can toggle playback.
struct FastForwardButton
{
    bool downBefore = false, armed = true;
    void Block() { armed = false; }
    bool Update(bool toggle, bool down, bool active, bool& latched)
    {
        const bool edge = down && !downBefore;
        if (!active) armed = false;
        bool changed = false;
        if (active && armed && toggle && edge) { latched = !latched; changed = true; }
        downBefore = down;
        if (active && !down) armed = true;
        return changed;
    }
    bool Requested(bool toggle, bool down, bool latched) const
    {
        return latched || (!toggle && armed && down);
    }
};
