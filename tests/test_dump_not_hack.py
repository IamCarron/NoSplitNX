from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class DumpNotHackTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('c++'), 'C++ compiler required')
    def test_full_size_dump_is_not_marked_modified(self):
        with tempfile.TemporaryDirectory() as folder:
            exe = Path(folder) / ('dump.exe' if os.name == 'nt' else 'dump')
            subprocess.run(['c++', '-std=c++17', '-pthread', str(ROOT / 'tests/test_dump_not_hack.cpp'), '-o', str(exe)], check=True)
            subprocess.run([str(exe), folder], check=True)
