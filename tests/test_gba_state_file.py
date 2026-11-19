from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class GbaStateFileTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('c++'), 'C++ compiler required')
    def test_actual_save_method_preserves_previous_state_on_failure(self):
        source = (ROOT / 'gba/frontend/main.cpp').read_text(encoding='utf-8')
        method = source[source.index('    void SaveState(int slot = 0)'):source.index('    void LoadState(int slot = 0)')]
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / 'save_state_method.h').write_text(method, encoding='utf-8')
            exe = root / ('state-file.exe' if os.name == 'nt' else 'state-file')
            subprocess.run(['c++', '-std=c++17', '-I' + str(ROOT / 'gba/frontend'), '-I' + str(root),
                            str(ROOT / 'tests/test_gba_state_file.cpp'), '-o', str(exe)], check=True)
            subprocess.run([str(exe), str(root)], check=True)
