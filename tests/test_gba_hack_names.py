from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class GbaHackNameTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('c++'), 'C++ compiler required')
    def test_only_the_hack_takes_its_name(self):
        with tempfile.TemporaryDirectory() as folder:
            exe = Path(folder) / 'hack-names'
            subprocess.run(['c++', '-std=c++17', '-I' + str(ROOT / 'gba/frontend'),
                            str(ROOT / 'tests/test_gba_hack_names.cpp'), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)
