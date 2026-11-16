from pathlib import Path
import os,subprocess,tempfile,unittest,sys,hashlib
ROOT=Path(__file__).resolve().parents[1]
class GbaLanguageTests(unittest.TestCase):
 def test_embedded_gba_language_renderer(self):
  default_src=ROOT/'gba'
  source=Path(os.environ['SSR_TEST_GBA_SOURCE']) if os.environ.get('SSR_TEST_GBA_SOURCE') else default_src
  if not (source/'frontend/ui.h').is_file():
   self.skipTest('GBA source directory not present')
  before={str(p.relative_to(source)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [source/'frontend/ui.h',source/'frontend/main.cpp']}
  with tempfile.TemporaryDirectory() as folder:
   folder=Path(folder);prepared=folder/'source'
   subprocess.run([sys.executable,str(ROOT/'tools/localisation/prepare_gba.py'),str(source),str(prepared)],check=True)
   exe=folder/'test'
   subprocess.run(['c++','-std=c++17','-DGBAW_EMBEDDED','-I'+str(prepared/'frontend'),'-I'+str(ROOT/'frontend'),str(ROOT/'tests/test_gba_languages.cpp'),'-o',str(exe)],check=True)
   subprocess.run([str(exe)],check=True)
  assert all(hashlib.sha256((source/p).read_bytes()).hexdigest()==sha for p,sha in before.items())
