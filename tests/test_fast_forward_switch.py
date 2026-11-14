from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class SwitchFastForwardTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('c++'), 'C++ compiler required')
    def test_pacer_picks_at_least_2x_and_keeps_hysteresis(self):
        with tempfile.TemporaryDirectory() as folder:
            exe = Path(folder) / 'ffswitch'
            subprocess.run(['c++', '-std=c++17', '-I' + str(ROOT / 'frontend'),
                            str(ROOT / 'tests/test_fast_forward_switch.cpp'), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_deferred_wide_flush_and_boost_wiring(self):
        gpu3d = (ROOT / 'melonDS/src/GPU3D.cpp').read_text(encoding='utf-8')
        gpu3d_h = (ROOT / 'melonDS/src/GPU3D.h').read_text(encoding='utf-8')
        wide = (ROOT / 'melonDS/src/GPU3D_Wide.cpp').read_text(encoding='utf-8')
        main_cpp = (ROOT / 'frontend/main.cpp').read_text(encoding='utf-8')
        sw_plat = (ROOT / 'frontend/switch_platform.cpp').read_text(encoding='utf-8')

        # Deferred VBlank wide flush must latch polygons into WidePendingRaw before RawCurrent clears.
        self.assertIn('WidePendingRaw', gpu3d_h)
        self.assertLess(gpu3d.index('WidePendingRaw.swap(RawCurrent)'), gpu3d.index('RawPrevious.swap(RawCurrent);'))
        self.assertIn('WideOnFlushRaw(*this, WidePendingRaw)', gpu3d)
        self.assertIn('void WideOnFlushRaw(', wide)

        # Switch fast-forward keeps 360-line low-res cap (no 192p pixelation) and uses FF clock boost.
        self.assertNotIn('WideLinesNow = std::min(WideLinesNow, 192)', main_cpp)
        self.assertIn('SwitchFastForwardPacer', main_cpp)
        self.assertIn('SwitchPlatform::SetFastForwardBoost(', main_cpp)

        # Switch clock boost must close every clkrst session immediately and never touch GPU or APM boost.
        self.assertNotIn('appletSetCpuBoostMode', sw_plat)
        self.assertEqual(sw_plat.count('clkrstOpenSession('), sw_plat.count('clkrstCloseSession('))
