from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class GbaMenuSwipeTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('c++') and shutil.which('sdl2-config'), 'C++ and SDL required')
    def test_swipe_scrolls_and_only_taps_activate(self):
        # Also verify gba/frontend/main.cpp tracks pauseTouchDown / pauseTouchMoved cleanly.
        gba_main = (ROOT / 'gba/frontend/main.cpp').read_text(encoding='utf-8')
        self.assertIn('pauseTouchMoved', gba_main)
        self.assertIn('CancelPauseTouch()', gba_main)
        self.assertIn('mix((uint64_t)hostMenu.scroll)', gba_main)
        with tempfile.TemporaryDirectory() as folder:
            exe = Path(folder) / 'gba-menu-swipe'
            flags = shlex.split(subprocess.check_output(['sdl2-config', '--cflags', '--libs'], text=True))
            subprocess.run([
                'c++', '-std=c++17', '-O1',
                '-I' + str(ROOT / 'frontend'),
                '-I' + str(ROOT / 'gba/frontend'),
                str(ROOT / 'tests/test_gba_menu_swipe.cpp'),
                '-o', str(exe), *flags,
            ], check=True)
            subprocess.run([str(exe)], check=True)
