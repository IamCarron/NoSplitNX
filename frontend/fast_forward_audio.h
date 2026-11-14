#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

// The sound of a fast forward, for the DS and the GBA games. Several frames are run for each one
// shown, and each makes a frame of sound: played all at once the music is rushed and high, so it
// is played at its own speed and pitch instead, a stretch at a time. A stretch is kept until
// about two seconds of it wait to be heard, the rest of what the game makes meanwhile is left
// out, and once the stretch has played the next one starts from where the game is now. What is
// heard is so never more than about two seconds behind the picture (it was up to half a minute).
// Each stretch fades in and out over a few milliseconds, so the joins do not click.
// Samples are stereo, 16 bits: two values a frame.
struct FastForwardAudio
{
    std::vector<int16_t> queue;
    size_t at = 0;        // the next unplayed value in queue
    bool filling = true;  // this stretch still takes what the game makes
    int fadeIn = 0;       // frames of the fade in still to play
    double frac = 0;      // the part of a frame owed to the device (Pull by time)

    static int Cap(int rate) { return rate * 2; }
    static int Fade(int rate) { return std::max(1, rate / 200); } // (5 ms)
    int Waiting() const { return (int)((queue.size() - at) / 2); }
    bool Empty() const { return queue.size() == at; }

    void Clear()
    {
        queue.clear();
        at = 0;
        filling = true;
        fadeIn = 0;
        frac = 0;
    }

    // What the game made this time: n frames at `rate` a second.
    void Push(const int16_t* samples, int n, int rate)
    {
        if (n <= 0) return;
        if (Empty())
        {
            // (A new stretch, from where the game is now.)
            queue.clear();
            at = 0;
            filling = true;
            fadeIn = Fade(rate);
        }
        if (!filling) return;
        if (Waiting() + n > Cap(rate)) { filling = false; return; }
        queue.insert(queue.end(), samples, samples + (size_t)n * 2);
    }

    // Up to `want` frames to play now, into out. Returns how many.
    int PullFrames(int16_t* out, int want, int rate)
    {
        const int have = Waiting(), take = std::min(want, have), fade = Fade(rate);
        for (int i = 0; i < take; i++)
        {
            int gain = 256;
            const int left = have - i; // frames of this stretch from this one on
            if (!filling && left <= fade) gain = 256 * left / fade;
            if (fadeIn > 0) { gain = gain * (fade - fadeIn) / fade; fadeIn--; }
            out[i * 2] = (int16_t)(queue[at + (size_t)i * 2] * gain / 256);
            out[i * 2 + 1] = (int16_t)(queue[at + (size_t)i * 2 + 1] * gain / 256);
        }
        at += (size_t)std::max(0, take) * 2;
        if (at >= 8192)
        {
            queue.erase(queue.begin(), queue.begin() + at);
            at = 0;
        }
        return std::max(0, take);
    }

    // As PullFrames, for `seconds` of playing time (no more than room frames).
    int Pull(int16_t* out, double seconds, int rate, int room)
    {
        frac += rate * seconds;
        int want = (int)frac;
        frac -= want;
        return PullFrames(out, std::min(want, room), rate);
    }
};
