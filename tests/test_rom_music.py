from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class RomMusicTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('c++'), 'C++ compiler required')
    def test_synthetic_nds_and_gba_soundtrack_synthesis_and_fuzz(self):
        with tempfile.TemporaryDirectory() as folder:
            obj = Path(folder) / 'rommusic.o'
            exe = Path(folder) / 'rommusic'
            subprocess.run([
                'c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                '-I' + str(ROOT / 'frontend'),
                str(ROOT / 'tests/test_rom_music.cpp'),
                '-c', '-o', str(obj),
            ], check=True)
            subprocess.run([
                'c++', '-O2', '-std=c++17',
                '-I' + str(ROOT / 'frontend'),
                str(ROOT / 'tests/test_rom_music.cpp'),
                '-o', str(exe),
            ], check=True)
            subprocess.run([str(exe)], check=True)

    def test_switch_platform_and_launcher_audio_wiring(self):
        launcher_h = (ROOT / 'frontend/launcher.h').read_text(encoding='utf-8')
        sw_plat = (ROOT / 'frontend/switch_platform.cpp').read_text(encoding='utf-8')

        self.assertIn('const float* musicBands = nullptr', launcher_h)
        self.assertIn('rom_music.h', sw_plat)
        self.assertIn('MusicAudioSession', sw_plat)
        self.assertIn('music.Shutdown();', sw_plat)
        # Switch launcher music uses libnx audout directly and tears down before main.cpp SDL_Init.
        self.assertGreaterEqual(sw_plat.count('audoutInitialize()'), 1)
        self.assertGreaterEqual(sw_plat.count('audoutExit()'), sw_plat.count('audoutInitialize()'))
        self.assertEqual(sw_plat.count('audoutGetAudioOutState()'), 0)
        self.assertEqual(sw_plat.count('SDL_OpenAudioDevice('), 0)
        self.assertEqual(sw_plat.count('SDL_InitSubSystem(SDL_INIT_AUDIO)'), 0)
