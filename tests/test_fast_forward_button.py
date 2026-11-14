from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class FastForwardButtonTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('c++'), 'C++ compiler required')
    def test_press_edges_and_inactive_guards(self):
        with tempfile.TemporaryDirectory() as folder:
            exe = Path(folder) / 'fast-button'
            subprocess.run(['c++', '-std=c++17', '-I' + str(ROOT / 'frontend'),
                            str(ROOT / 'tests/test_fast_forward_button.cpp'), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)
