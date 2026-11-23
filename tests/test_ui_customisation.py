"""Compile and exercise custom layout and localisation behaviour without game content."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class UiCustomisationTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('c++'), 'C++ compiler required')
    def test_languages_and_regional_saves(self):
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / ('languages.exe' if os.name == 'nt' else 'languages')
            subprocess.run(['c++', '-std=c++17', '-I' + str(ROOT/'frontend'), str(ROOT/'tests/test_languages.cpp'), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)
