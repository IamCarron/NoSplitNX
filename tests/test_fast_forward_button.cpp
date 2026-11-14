#include "fast_forward_button.h"
#include <cassert>
int main()
{
    FastForwardButton input;
    bool latch = false;
    assert(!input.Update(false, true, true, latch));
    assert(input.Requested(false, true, latch));
    input.Update(false, false, true, latch);
    assert(!input.Requested(false, false, latch));
    assert(input.Update(true, true, true, latch) && latch);
    for (int i=0;i<100;i++) assert(!input.Update(true, true, true, latch) && latch);
    input.Update(true, false, true, latch);
    assert(input.Update(true, true, true, latch) && !latch);
    assert(!input.Requested(true, true, latch));
    // Opening menus/remapping while held must not produce a second press.
    input.Update(true, true, false, latch);
    assert(!input.Update(true, true, true, latch) && !latch);
    input.Update(true, false, true, latch);
    assert(input.Update(true, true, true, latch) && latch);
    // Focus loss and reconnect require a release before the next press.
    input.Block();
    assert(!input.Update(true, true, true, latch) && latch);
    input.Update(true, false, true, latch);
    assert(input.Update(true, true, true, latch) && !latch);
    input.Update(true, false, true, latch);
    assert(!input.Update(true, true, false, latch));
    assert(!input.Update(true, true, true, latch));
    input.Update(true, false, true, latch);
    assert(input.Update(true, true, true, latch) && latch);
    // Keyboard input may be suppressed by a menu; release there is not rearming.
    latch = false;
    input.Update(true, false, false, latch);
    assert(!input.Update(true, true, true, latch) && !latch);
    input.Update(true, false, true, latch);
    assert(input.Update(true, true, true, latch) && latch);
    // A menu-latched preference still works in Hold mode.
    assert(input.Requested(false, false, latch));
}
