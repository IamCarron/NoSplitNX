from pathlib import Path
import shutil, subprocess, tempfile, unittest

ROOT = Path(__file__).resolve().parents[1]

class Beta2FixesTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('c++'), 'C++ compiler required')
    def test_beta2_fixes(self):
        with tempfile.TemporaryDirectory() as folder:
            exe = Path(folder) / 'beta2_fixes'
            subprocess.run([
                'c++', '-std=c++17', '-O1',
                '-I' + str(ROOT / 'frontend'),
                '-I' + str(ROOT / 'melonDS/src'),
                str(ROOT / 'tests/test_beta2_fixes.cpp'),
                '-o', str(exe)
            ], check=True)
            subprocess.run([str(exe)], check=True)

if __name__ == '__main__':
    unittest.main()
