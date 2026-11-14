#include "../frontend/fast_forward_audio.h"
#include <cassert>
#include <cstdio>

// The game's sound as a counter (left) so each played frame says which made frame it was.
int main()
{
    const int rate = 48000, frame = 800; // (a 60th of a second)
    for (int speed : { 2, 3, 4, 8 })
    {
        FastForwardAudio a;
        std::vector<int16_t> made((size_t)frame * speed * 2), out((size_t)frame * 2);
        long long produced = 0, played = 0, worst = 0, following = 0, last = -1;
        for (int shown = 0; shown < 60 * 20; shown++) // twenty seconds of fast forward
        {
            for (int i = 0; i < frame * speed; i++, produced++)
            {
                made[(size_t)i * 2] = (int16_t)(produced % 20000 + 1000); // (never near zero: a fade shows)
                made[(size_t)i * 2 + 1] = 12345;
            }
            a.Push(made.data(), frame * speed, rate);
            assert(a.Waiting() <= FastForwardAudio::Cap(rate));
            const int n = a.Pull(out.data(), 1.0 / 60, rate, frame);
            assert(n <= frame);
            for (int i = 0; i < n; i++, played++)
            {
                // Its own speed and pitch: inside a stretch each frame follows the last one made.
                const int v = out[(size_t)i * 2];
                if (v == last + 1 || (last == 20999 && v == 1000)) following++;
                last = v;
            }
            // How far behind the picture what is heard can be: what waits, at most.
            worst = std::max<long long>(worst, a.Waiting());
        }
        assert(played > 60 * 20 * frame * 9 / 10);          // sound nearly all the time
        assert(worst <= FastForwardAudio::Cap(rate));        // never more than two seconds behind
        assert(following > played * 95 / 100);               // long stretches, not a rush of pieces
        a.Clear();
        assert(a.Empty() && a.Pull(out.data(), 1.0 / 60, rate, frame) == 0); // letting go drops it
        printf("x%d: played %lld of %lld frames made, at most %.2f s behind\n", speed, played, produced, worst / (double)rate);
    }
    // A stretch ends and begins quietly (no click at the joins).
    FastForwardAudio a;
    std::vector<int16_t> loud(48000 * 2 * 2, 20000), out(1600);
    a.Push(loud.data(), 48000 * 2, 48000);
    a.Push(loud.data(), 100, 48000); // (past the two seconds: this stretch is closed)
    assert(!a.filling);
    int first = 30000, lastv = 30000, n;
    bool begun = false;
    while ((n = a.PullFrames(out.data(), 800, 48000)) > 0)
    {
        if (!begun) { first = out[0]; begun = true; }
        lastv = out[(size_t)(n - 1) * 2];
    }
    assert(first == 0 && lastv < 2000);
    puts("Fast forward sound: its own speed and pitch, bounded lag, quiet joins");
}
