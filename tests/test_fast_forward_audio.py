from pathlib import Path
import shutil, subprocess, tempfile, unittest

ROOT = Path(__file__).resolve().parents[1]


class FastForwardAudioTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('c++'), 'C++ compiler required')
    def test_normal_pitch_with_bounded_lag(self):
        with tempfile.TemporaryDirectory() as folder:
            exe = Path(folder) / 'ffaudio'
            subprocess.run(['c++', '-std=c++17', str(ROOT / 'tests/test_fast_forward_audio.cpp'), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_players_use_it(self):
        for path in ('frontend/main.cpp',):
            source = (ROOT / path).read_text(encoding='utf-8')
            self.assertIn('ffAudio.Push(', source)
            self.assertNotIn('AudioFastForward(', source)
            self.assertNotIn('squeeze', source)
