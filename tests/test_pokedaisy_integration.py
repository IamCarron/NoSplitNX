from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class PokeDaisyIntegrationTests(unittest.TestCase):
    def test_notice_and_packaging_attribution(self):
        expected = 'Based on PokeDaisy by Lidor Itzhari - https://github.com/lidor30/pokedaisy'
        for rel in ('pokedaisy/NOTICE', 'gba/frontend/pokedaisy/NOTICE', 'README.md'):
            text = (ROOT / rel).read_text(encoding='utf-8')
            self.assertIn(expected, text, f'Missing PokeDaisy attribution in {rel}')
        build_sh = (ROOT / 'tools/switch/build_game.sh').read_text(encoding='utf-8')
        self.assertIn('NOTICE-PokeDaisy.txt', build_sh)
        root_cmake = (ROOT / 'CMakeLists.txt').read_text(encoding='utf-8')
        self.assertIn('add_library(pokedaisy_core STATIC', root_cmake)
        self.assertIn('add_library(pokedaisy_ds STATIC', root_cmake)
        self.assertIn('SSR_POKEDAISY', root_cmake)
        gba_cmake = (ROOT / 'gba/CMakeLists.txt').read_text(encoding='utf-8')
        self.assertIn('M_CORE_GB ON', gba_cmake)
        self.assertIn('pk_wide_gb.c', gba_cmake)
        self.assertIn('pk_rewind.c', gba_cmake)
        self.assertIn('GBAW_POKEDAISY', gba_cmake)

    @unittest.skipUnless(
        shutil.which('c++') and (shutil.which('cc') or shutil.which('clang') or shutil.which('gcc'))
        and shutil.which('sdl2-config'),
        'C/C++ compilers and sdl2-config required',
    )
    def test_pokedaisy_core_and_ds_bridge_compile_link_and_render(self):
        cc = shutil.which('cc') or shutil.which('clang') or shutil.which('gcc')
        sdl_cflags = shlex.split(subprocess.check_output(['sdl2-config', '--cflags'], text=True))
        sdl_libs = shlex.split(subprocess.check_output(['sdl2-config', '--libs'], text=True))

        c_files = sorted((ROOT / 'pokedaisy/core').glob('*.c')) + sorted((ROOT / 'pokedaisy/ui').glob('*.c'))
        cpp_files = sorted((ROOT / 'pokedaisy/ds').glob('*.cpp'))
        self.assertGreater(len(c_files), 40)
        self.assertGreater(len(cpp_files), 10)

        with tempfile.TemporaryDirectory() as folder:
            tmp = Path(folder)
            objs = []
            jobs = []
            for idx, src in enumerate(c_files):
                obj = tmp / f'c_{idx}_{src.stem}.o'
                objs.append(obj)
                opt = '-O1' if src.stem.endswith('_gen') else '-O2'
                jobs.append([
                    cc, '-std=c11', opt, '-D_GNU_SOURCE', '-Wno-unused-function', '-Wno-unused-parameter',
                    '-I' + str(ROOT / 'pokedaisy/core'),
                    '-I' + str(ROOT / 'pokedaisy/ui'),
                    '-c', str(src), '-o', str(obj),
                ])
            for idx, src in enumerate(cpp_files):
                obj = tmp / f'cpp_{idx}_{src.stem}.o'
                objs.append(obj)
                cmd = [
                    'c++', '-std=c++17', '-O2', '-Wno-unused-function', '-Wno-unused-parameter',
                    '-I' + str(ROOT / 'pokedaisy/ds'),
                    '-I' + str(ROOT / 'pokedaisy/core'),
                    '-I' + str(ROOT / 'pokedaisy/ui'),
                    *sdl_cflags,
                    '-c', str(src), '-o', str(obj),
                ]
                jobs.append(cmd)

            def compile_one(cmd):
                subprocess.run(cmd, check=True)

            with ThreadPoolExecutor(max_workers=8) as pool:
                list(pool.map(compile_one, jobs))

            exe = tmp / 'pokedaisy_ds_test'
            subprocess.run([
                'c++', '-std=c++17', '-O2',
                '-I' + str(ROOT / 'pokedaisy/ds'),
                *sdl_cflags,
                str(ROOT / 'tests/test_pokedaisy_integration.cpp'),
                *[str(o) for o in objs],
                *sdl_libs, '-lm',
                '-o', str(exe),
            ], check=True)
            data_dir = tmp / 'ds_data'
            data_dir.mkdir()
            subprocess.run([str(exe), str(data_dir)], check=True)
