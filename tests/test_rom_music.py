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
            exe = Path(folder) / 'rommusic'
            subprocess.run([
                'c++', '-O2', '-std=c++17', '-Wall', '-Wextra', '-Werror',
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
        # SDL audio subsystem opened inside PickGame must always be shut down cleanly before handover.
        self.assertEqual(sw_plat.count('SDL_OpenAudioDevice('), sw_plat.count('SDL_CloseAudioDevice('))
        self.assertEqual(sw_plat.count('SDL_InitSubSystem(SDL_INIT_AUDIO)'), sw_plat.count('SDL_QuitSubSystem(SDL_INIT_AUDIO)'))
