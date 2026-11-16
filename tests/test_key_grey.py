from pathlib import Path
import os, shutil, subprocess, tempfile, unittest

ROOT = Path(__file__).resolve().parents[1]


class KeyGreyTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('c++') and shutil.which('sdl2-config'), 'C++ and SDL required')
    def test_every_compose_path_leaves_out_the_dark_around_a_label(self):
        with tempfile.TemporaryDirectory() as folder:
            exe = Path(folder) / 'key-grey'
            flags = subprocess.check_output(['sdl2-config', '--cflags', '--libs'], text=True).split()
            subprocess.run(['c++', '-std=c++17', '-I' + str(ROOT / 'frontend'), str(ROOT / 'tests/test_key_grey.cpp'), '-o', str(exe)] + flags, check=True)
            subprocess.run([str(exe)], check=True, env=dict(os.environ, SDL_VIDEODRIVER='dummy'))
