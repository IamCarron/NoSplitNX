from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ZoomBindingTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('c++') and shutil.which('sdl2-config'), 'C++ and SDL required')
    def test_physical_zoom_and_remapped_swap_are_independent(self):
        source = (ROOT / 'frontend/main.cpp').read_text(encoding='utf-8')
        remap = source[source.index('static PadState RemapPad('):source.index('// The input first pressed')]
        zoom = source[source.index('static bool ZoomPressed('):source.index('// DS buttons from the controller.')]
        routing = source.split('// (SWAP SCREENS given a button of its own', 1)[1]
        routing = routing[routing.index('            if '):routing.index('            if (!padTouchNow)\n')]
        harness = r'''
#include <SDL.h>
#include "controls.h"
#include <cassert>
enum { PAD_A, PAD_B, PAD_X, PAD_Y, PAD_BACK, PAD_GUIDE, PAD_START, PAD_LS, PAD_RS,
       PAD_LB, PAD_RB, PAD_UP, PAD_DOWN, PAD_LEFT, PAD_RIGHT, PAD_COUNT };
struct PadState { bool btn[PAD_COUNT] = {}; float lx=0, ly=0, rx=0, ry=0, lt=0, rt=0; };
REMAP
ZOOM
static int Route(bool padTouchNow, const Controls::Bindings& b, const PadState& now, const PadState& before, bool labels) {
    struct { Controls::Bindings controls; } cfg{b};
    struct { bool manualFlip = false; } focus;
    const PadState mapped = RemapPad(now, b, labels), prev = RemapPad(before, b, labels);
    auto pressed = [&](int i) { return mapped.btn[i] && !prev.btn[i]; };
    bool dsZoomCycle = false, zoomEdge = ZoomPressed(now, before, b, labels);
ROUTING
    return (focus.manualFlip ? 1 : 0) | (dsZoomCycle ? 2 : 0);
}
int main() {
    for (bool labels : {true, false}) {
        Controls::Bindings b;
        PadState empty, stick, back;
        stick.btn[PAD_RS] = true; back.btn[PAD_LS] = true;
        assert(ZoomPressed(stick, empty, b, labels));
        assert(Route(false, b, stick, empty, labels) == 2);
        assert(Route(true, b, stick, empty, labels) == 0);
        assert(!ZoomPressed(stick, stick, b, labels));
        assert(!ZoomPressed(empty, stick, b, labels));
        b.SetPad(Controls::SWAP, Controls::IN_LS, labels);
        assert(!RemapPad(stick, b, labels).btn[PAD_RS]);
        assert(ZoomPressed(stick, empty, b, labels));
        assert(Route(false, b, stick, empty, labels) == 2);
        assert(Route(false, b, back, empty, labels) == 1);
        assert(Route(false, b, stick, stick, labels) == 0);
        assert(RemapPad(back, b, labels).btn[PAD_RS]);
        assert(!ZoomPressed(back, empty, b, labels));
        b.SetPad(Controls::SWAP, Controls::IN_RS, labels);
        assert(!ZoomPressed(stick, empty, b, labels));
        assert(Route(false, b, stick, empty, labels) == 1);
        b.SetPad(Controls::SWAP, Controls::IN_A, labels);
        assert(b.ActionOfPad(Controls::IN_RS, labels) != Controls::SWAP);
        assert(!ZoomPressed(stick, empty, b, labels));
        b.Reset(); b.ClearPad(Controls::SWAP);
        assert(ZoomPressed(stick, empty, b, labels));
        b.Reset(); b.SetPad(Controls::SAVE, Controls::IN_RS, labels);
        assert(!ZoomPressed(stick, empty, b, labels));
    }
}
'''.replace('REMAP', remap).replace('ZOOM', zoom).replace('ROUTING', routing)
        with tempfile.TemporaryDirectory() as folder:
            cpp = Path(folder) / 'zoom.cpp'
            exe = Path(folder) / 'zoom'
            cpp.write_text(harness, encoding='utf-8')
            flags = shlex.split(subprocess.check_output(['sdl2-config', '--cflags', '--libs'], text=True))
            subprocess.run(['c++', '-std=c++17', '-I' + str(ROOT / 'frontend'), str(cpp), '-o', str(exe), *flags], check=True)
            subprocess.run([str(exe)], check=True)
