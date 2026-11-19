from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ResetBindingTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('c++') and shutil.which('sdl2-config'), 'C++ and SDL required')
    def test_opt_in_binding_persistence_conflicts_and_press_guards(self):
        source = (ROOT / 'frontend/main.cpp').read_text(encoding='utf-8')
        remap = source[source.index('static PadState RemapPad('):source.index('// The input first pressed')]
        held = source[source.index('static bool InputHeld('):source.index('// Zoom belongs to the physical')]
        edge = '\n'.join(line for line in source.splitlines() if line.strip().startswith(('const int resetIn =', 'resetEdge =')))
        dispatch = next(line.strip() for line in source.splitlines() if line.strip().startswith('if (resetEdge)'))
        keyboard = next(line.strip() for line in source.splitlines() if line.strip().startswith('else if (act == Controls::RESET)'))
        harness = r'''
#include "controls.h"
#include <cassert>
#include <sstream>
enum { PAD_A, PAD_B, PAD_X, PAD_Y, PAD_BACK, PAD_GUIDE, PAD_START, PAD_LS, PAD_RS,
       PAD_LB, PAD_RB, PAD_UP, PAD_DOWN, PAD_LEFT, PAD_RIGHT, PAD_COUNT };
struct PadState { bool btn[PAD_COUNT] = {}; float lx=0, ly=0, rx=0, ry=0, lt=0, rt=0; };
REMAP
HELD
static int resets = 0;
static void resetGame() { ++resets; }
static void say(const char*) {}
static void Frame(const Controls::Bindings& bindings, const PadState& physPad, const PadState& prevPhysPad, bool open, bool focused) {
    struct { Controls::Bindings controls; bool byLabel = true; } cfg{bindings};
    struct { bool open; } menu{open};
    bool fastInputFocused = focused, resetEdge = false;
EDGE
DISPATCH
}
static void Key(int act) { if (act < 0) {} KEYBOARD }
int main() {
    Controls::Bindings b;
    assert(b.Key(Controls::RESET) == SDL_SCANCODE_UNKNOWN);
    assert(b.Pad(Controls::RESET, true) < 0);
    assert(b.Key(Controls::SAVE) == SDL_SCANCODE_F5);
    assert(b.Key(Controls::LOAD) == SDL_SCANCODE_F8);
    assert(Controls::Bindings::Clearable(Controls::RESET));
    b.SetKey(Controls::RESET, SDL_SCANCODE_F9);
    b.SetPad(Controls::RESET, Controls::IN_LT, true);
    std::ostringstream out; b.Save(out);
    Controls::Bindings loaded;
    std::istringstream in(out.str()); std::string line;
    while (std::getline(in, line)) { auto eq = line.find('='); loaded.Apply(line.substr(0, eq), line.substr(eq + 1)); }
    assert(loaded.Key(Controls::RESET) == SDL_SCANCODE_F9);
    assert(loaded.Pad(Controls::RESET, true) == Controls::IN_LT);
    PadState idle, down; down.lt = 1;
    Frame(loaded, down, idle, false, true); assert(resets == 1);
    Frame(loaded, down, down, false, true); assert(resets == 1);
    Frame(loaded, idle, down, false, true); assert(resets == 1);
    Frame(loaded, down, idle, true, true); assert(resets == 1);
    Frame(loaded, down, idle, false, false); assert(resets == 1);
    Frame(loaded, down, idle, false, true); assert(resets == 2);
    Key(loaded.ActionOfKey(SDL_SCANCODE_F9)); assert(resets == 3);
    loaded.ClearKey(Controls::RESET); loaded.ClearPad(Controls::RESET);
    assert(loaded.Key(Controls::RESET) == 0 && loaded.Pad(Controls::RESET, true) < 0);
    Frame(loaded, down, idle, false, true); Key(loaded.ActionOfKey(SDL_SCANCODE_F9)); assert(resets == 3);
    for (bool labels : {true, false}) {
        b.Reset(); b.SetPad(Controls::RESET, Controls::IN_A, labels);
        assert(b.ActionOfPad(Controls::IN_A, labels) == Controls::RESET);
        assert(b.Pad(labels ? Controls::A : Controls::B, labels) < 0);
        PadState pad; pad.btn[PAD_A] = true;
        auto mapped = RemapPad(pad, b, labels);
        for (bool held : mapped.btn) assert(!held);
        assert(mapped.rt == 0);
    }
}
'''.replace('REMAP', remap).replace('HELD', held).replace('EDGE', edge).replace('DISPATCH', dispatch).replace('KEYBOARD', keyboard)
        with tempfile.TemporaryDirectory() as folder:
            cpp = Path(folder) / 'reset.cpp'
            exe = Path(folder) / 'reset'
            cpp.write_text(harness, encoding='utf-8')
            flags = shlex.split(subprocess.check_output(['sdl2-config', '--cflags', '--libs'], text=True))
            subprocess.run(['c++', '-std=c++17', '-I' + str(ROOT / 'frontend'), str(cpp), '-o', str(exe), *flags], check=True)
            subprocess.run([str(exe)], check=True)
