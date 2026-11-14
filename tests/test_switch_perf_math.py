from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class SwitchPerfTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('c++'), 'C++ compiler required')
    def test_spu_timer_inv_and_div255_identities(self):
        with tempfile.TemporaryDirectory() as folder:
            exe = Path(folder) / 'perfmath'
            subprocess.run([
                'c++', '-O2', '-std=c++17',
                str(ROOT / 'tests/test_switch_perf_math.cpp'),
                '-o', str(exe),
            ], check=True)
            subprocess.run([str(exe)], check=True)

    @unittest.skipUnless(shutil.which('c++'), 'C++ compiler required')
    def test_modified_melonds_sources_syntax(self):
        subprocess.run([
            'c++', '-std=c++17', '-fsyntax-only',
            '-I' + str(ROOT / 'melonDS/src'),
            '-I' + str(ROOT / 'frontend'),
            str(ROOT / 'melonDS/src/SPU.cpp'),
            str(ROOT / 'melonDS/src/GPU2D_Soft.cpp'),
        ], check=True)

    def test_switch_perf_optimizations_present_and_patch_valid(self):
        spu_h = (ROOT / 'melonDS/src/SPU.h').read_text(encoding='utf-8')
        spu_cpp = (ROOT / 'melonDS/src/SPU.cpp').read_text(encoding='utf-8')
        gpu2d_cpp = (ROOT / 'melonDS/src/GPU2D_Soft.cpp').read_text(encoding='utf-8')
        deko_cpp = (ROOT / 'melonDS/src/GPU3D_WideGpu_Deko.cpp').read_text(encoding='utf-8')
        gpucomp_h = (ROOT / 'frontend/gpucompose.h').read_text(encoding='utf-8')
        main_cpp = (ROOT / 'frontend/main.cpp').read_text(encoding='utf-8')

        self.assertIn('TimerInv', spu_h)
        self.assertIn('if (!(Cnt & (1u << 31))) return 0;', spu_h)
        self.assertIn('if (!file->Saving) UpdateTimerInv();', spu_cpp)
        self.assertIn('LineObjEmpty', gpu2d_cpp)
        self.assertIn('haveDrawState', deko_cpp)
        self.assertIn('(val + 1u + (val >> 8)) >> 8', gpucomp_h)
        self.assertIn('emuThread.Wait()', main_cpp)

        # Ensure melonDS-ssrecomp.patch preserves existing ssrecomp hunks and round-trips cleanly.
        import re
        patch_text = (ROOT / 'melonDS-ssrecomp.patch').read_text(encoding='utf-8')
        self.assertIn('SsrProf::Sound', patch_text)
        self.assertIn('LayerFlags', patch_text)
        self.assertIn('TimerInv', patch_text)
        self.assertIn('LineObjEmpty', patch_text)
        targets = ['src/SPU.cpp', 'src/SPU.h', 'src/GPU2D_Soft.cpp', 'src/GPU2D_Soft.h']
        sections = [
            s for s in re.split(r'(?=^diff --git )', patch_text, flags=re.M)
            if any(s.startswith(f'diff --git a/{rel} ') for rel in targets)
        ]
        self.assertEqual(len(sections), len(targets))
        sub_patch = ''.join(sections)
        with tempfile.TemporaryDirectory() as folder:
            for rel in targets:
                dst = Path(folder) / rel
                dst.parent.mkdir(parents=True, exist_ok=True)
                dst.write_bytes((ROOT / 'melonDS' / rel).read_bytes())
            pfile = Path(folder) / 'sub.patch'
            pfile.write_text(sub_patch, encoding='utf-8')
            r1 = subprocess.run(['git', 'apply', '-R', str(pfile)], cwd=folder, capture_output=True, text=True)
            self.assertEqual(r1.returncode, 0, r1.stderr)
            r2 = subprocess.run(['git', 'apply', str(pfile)], cwd=folder, capture_output=True, text=True)
            self.assertEqual(r2.returncode, 0, r2.stderr)
            for rel in targets:
                self.assertEqual((Path(folder) / rel).read_bytes(), (ROOT / 'melonDS' / rel).read_bytes())
