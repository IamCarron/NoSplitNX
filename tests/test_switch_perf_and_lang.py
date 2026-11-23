from pathlib import Path
import json
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class SwitchPerfAndLangTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('c++'), 'C++ compiler required')
    def test_launcher_language_formatting_and_sync(self):
        with tempfile.TemporaryDirectory() as folder:
            exe = Path(folder) / 'perf_and_lang'
            subprocess.run([
                'c++', '-std=c++17',
                '-I' + str(ROOT / 'frontend'),
                str(ROOT / 'tests/test_switch_perf_and_lang.cpp'),
                '-o', str(exe),
            ], check=True)
            subprocess.run([str(exe)], check=True)

    def test_gpucompose_crisp_optimisation_and_nro_trim(self):
        gpucomp = (ROOT / 'frontend/gpucompose.h').read_text(encoding='utf-8')
        cmake = (ROOT / 'CMakeLists.txt').read_text(encoding='utf-8')
        sw_plat = (ROOT / 'frontend/switch_platform.cpp').read_text(encoding='utf-8')
        sw_h = (ROOT / 'frontend/switch_platform.h').read_text(encoding='utf-8')
        main_cpp = (ROOT / 'frontend/main.cpp').read_text(encoding='utf-8')
        deko_cpp = (ROOT / 'melonDS/src/GPU3D_WideGpu_Deko.cpp').read_text(encoding='utf-8')
        gpu3d_cpp = (ROOT / 'melonDS/src/GPU3D.cpp').read_text(encoding='utf-8')

        # DrawCrisp must skip non-BG pixels, cache on pieceSig, skip empty pieces, and blit tight bounds.
        self.assertIn('BlitCrisp(', gpucomp)
        self.assertIn('bool anyOpaque = false;', gpucomp)
        self.assertIn('if (!anyOpaque)', gpucomp)
        self.assertIn('const uint8_t keepBg = (uint8_t)(0x0F & ~el.drop);', gpucomp)
        self.assertIn('SDL_UpdateTexture(t, nullptr, CrispBuf.data(), el.sw * 4);', gpucomp)

        # Switch NRO must not build the unused OpenGL 3.3 renderer or deko3D bring-up probe by default,
        # and must not carry dead overclocking code.
        self.assertIn('option(SSR_DEKO_PROBE', cmake)
        self.assertNotIn('if(NINTENDO_SWITCH OR CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")\n    set(SSR_GPU3D_DEFAULT ON)', cmake)
        self.assertNotIn('SetFastForwardBoost', sw_plat)
        self.assertNotIn('SetFastForwardBoost', sw_h)
        self.assertNotIn('bool cpuBoost', main_cpp)

        # Avoid per-frame synchronous SD-card log spam and 4 MB non-SoulSilver boot RAM scans on Switch.
        self.assertIn('_IOFBF, 8192', sw_plat)
        self.assertIn('if (soulSilverRom && !singleScreenRom', main_cpp)
        self.assertIn('SSR_WIDE_PROF', deko_cpp)
        self.assertIn('#ifdef __SWITCH__\n    // On Switch, stderr writes to game.log', gpu3d_cpp)

    def test_translations_parity_and_gpu3d_patch_roundtrip(self):
        data = json.loads((ROOT / 'localisation/translations.json').read_text(encoding='utf-8'))
        tables = data['tables']
        es_keys = set(tables['es'].keys())
        for lang, tbl in tables.items():
            self.assertEqual(set(tbl.keys()), es_keys, f'Key mismatch in {lang}')
        for required in [
            ' COVERS SAVED',
            ' CODES',
            'A: CHOOSE    B: BACK',
            'WIDESCREEN UNTESTED',
            'ROM HACK: GLITCHY SIDES? TRY ASPECT RATIO 3:2',
            'ROM HACK: WIDESCREEN TRIED ANYWAY (',
        ]:
            for lang, tbl in tables.items():
                self.assertIn(required, tbl, f'{required} missing in {lang}')

        # Verify src/GPU3D.cpp hunk in melonDS-ssrecomp.patch round-trips cleanly.
        patch_text = (ROOT / 'melonDS-ssrecomp.patch').read_text(encoding='utf-8')
        sections = [
            s for s in re.split(r'(?=^diff --git )', patch_text, flags=re.M)
            if s.startswith('diff --git a/src/GPU3D.cpp ')
        ]
        self.assertEqual(len(sections), 1)
        with tempfile.TemporaryDirectory() as folder:
            dst = Path(folder) / 'src/GPU3D.cpp'
            dst.parent.mkdir(parents=True, exist_ok=True)
            dst.write_bytes((ROOT / 'melonDS/src/GPU3D.cpp').read_bytes())
            pfile = Path(folder) / 'gpu3d.patch'
            pfile.write_text(sections[0], encoding='utf-8')
            r1 = subprocess.run(['git', 'apply', '-R', str(pfile)], cwd=folder, capture_output=True, text=True)
            self.assertEqual(r1.returncode, 0, r1.stderr)
            r2 = subprocess.run(['git', 'apply', str(pfile)], cwd=folder, capture_output=True, text=True)
            self.assertEqual(r2.returncode, 0, r2.stderr)
            self.assertEqual(dst.read_bytes(), (ROOT / 'melonDS/src/GPU3D.cpp').read_bytes())


if __name__ == '__main__':
    unittest.main()
