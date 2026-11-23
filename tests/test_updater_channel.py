from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

HARNESS = r'''
#include "updater.h"
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace Achievements {
bool HttpPost(const std::string&, const std::string&, const std::string&, const std::string&, int& status, std::string&, std::string&) {
    status = 404;
    return false;
}
bool HttpDownload(const std::string&, const std::string&, int& status, std::string&, std::string&) {
    status = 404;
    return false;
}
void HttpStop(bool) {}
}

int main()
{
    const std::string json = R"([
      {"tag_name":"v1.3.0-beta.2","draft":false,"prerelease":true,"html_url":"https://gh/b2","assets":[
        {"name":"nosplitnx-switch-beta.zip","size":100,"updated_at":"2026-11-24T12:00:00Z","browser_download_url":"https://dl/b2/nosplitnx-switch-beta.zip"},
        {"name":"SHA256SUMS.txt","size":64,"updated_at":"2026-11-24T12:00:00Z","browser_download_url":"https://dl/b2/SHA256SUMS.txt"}
      ]},
      {"tag_name":"v1.3.0-beta.1","draft":false,"prerelease":true,"html_url":"https://gh/b1","assets":[
        {"name":"nosplitnx-switch-beta.zip","size":100,"updated_at":"2026-11-23T12:00:00Z","browser_download_url":"https://dl/b1/nosplitnx-switch-beta.zip"},
        {"name":"SHA256SUMS.txt","size":64,"updated_at":"2026-11-23T12:00:00Z","browser_download_url":"https://dl/b1/SHA256SUMS.txt"}
      ]},
      {"tag_name":"v1.2.6","draft":false,"prerelease":false,"html_url":"https://gh/126","assets":[
        {"name":"nosplitnx-switch.zip","size":100,"updated_at":"2026-11-22T12:00:00Z","browser_download_url":"https://dl/126/nosplitnx-switch.zip"},
        {"name":"SHA256SUMS.txt","size":64,"updated_at":"2026-11-22T12:00:00Z","browser_download_url":"https://dl/126/SHA256SUMS.txt"}
      ]},
      {"tag_name":"v1.4.0-draft","draft":true,"prerelease":true,"html_url":"https://gh/draft","assets":[
        {"name":"nosplitnx-switch.zip","size":100,"updated_at":"2026-11-30T12:00:00Z","browser_download_url":"https://dl/d/nosplitnx-switch.zip"}
      ]}
    ])";

    Updater::Release r;
    std::string err;

    // Stable channel (default): ignores betas and drafts, picks v1.2.6.
    assert(Updater::FindRelease(json, "nosplitnx-switch.zip", "https://dl/", r, err, false));
    assert(r.tag == "v1.2.6" && !r.prerelease);

    // Beta channel: picks newest non-draft pre-release v1.3.0-beta.2 (including -beta asset name), still ignores draft.
    assert(Updater::FindRelease(json, "nosplitnx-switch.zip", "https://dl/", r, err, true));
    assert(r.tag == "v1.3.0-beta.2" && r.prerelease && r.asset == "nosplitnx-switch-beta.zip");

    // Once a newer stable release ships after the beta, both channels pick the stable release.
    const std::string jsonWithStable = R"([
      {"tag_name":"v1.3.0","draft":false,"prerelease":false,"html_url":"https://gh/130","assets":[
        {"name":"nosplitnx-switch.zip","size":100,"updated_at":"2026-11-25T12:00:00Z","browser_download_url":"https://dl/130/nosplitnx-switch.zip"}
      ]},
      {"tag_name":"v1.3.0-beta.2","draft":false,"prerelease":true,"html_url":"https://gh/b2","assets":[
        {"name":"nosplitnx-switch.zip","size":100,"updated_at":"2026-11-24T12:00:00Z","browser_download_url":"https://dl/b2/nosplitnx-switch.zip"}
      ]}
    ])";
    assert(Updater::FindRelease(jsonWithStable, "nosplitnx-switch.zip", "https://dl/", r, err, false));
    assert(r.tag == "v1.3.0" && !r.prerelease);
    assert(Updater::FindRelease(jsonWithStable, "nosplitnx-switch.zip", "https://dl/", r, err, true));
    assert(r.tag == "v1.3.0" && !r.prerelease);

    // settings.cfg channel reading & ResetCheck()
    const auto tmp = std::filesystem::temp_directory_path() / "nosplitnx_updater_channel_test";
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp);
    Updater::Init(tmp.string(), tmp.string(), (tmp / "nosplitnx.nro").string());
    assert(!Updater::IncludeBetas());
    {
        std::ofstream f(tmp / "settings.cfg");
        f << "update_channel=beta\n";
    }
    assert(Updater::IncludeBetas());
    {
        std::ofstream f(tmp / "settings.cfg");
        f << "update_channel=stable\n";
    }
    assert(!Updater::IncludeBetas());
    Updater::ResetCheck();
    assert(Updater::Get().state == Updater::State::Idle && Updater::Get().text == "CHECK");
    std::filesystem::remove_all(tmp);

    std::puts("Updater STABLE/BETA channel filtering and settings.cfg persistence OK.");
    return 0;
}
'''


class UpdaterChannelTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('c++'), 'C++ compiler required')
    def test_stable_and_beta_channel_selection(self):
        with tempfile.TemporaryDirectory() as folder:
            tmp = Path(folder)
            src = tmp / 'harness.cpp'
            exe = tmp / 'updater_channel_test'
            src.write_text(HARNESS, encoding='utf-8')
            subprocess.run([
                'c++', '-std=c++17', '-O2', '-pthread',
                '-I' + str(ROOT / 'frontend'),
                str(src), str(ROOT / 'frontend/updater.cpp'),
                '-o', str(exe),
            ], check=True)
            subprocess.run([str(exe)], check=True)
