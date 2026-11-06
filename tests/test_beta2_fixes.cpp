#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "launcher.h"
#include "audiostretch.h"
#include <cassert>
#include <cstdio>
#include <fstream>

int main(int argc, char** argv)
{
    (void)argc; (void)argv;

    // Test 1: SettingsPage::Change saves to an existing settings file (FAT32 rename fix)
    const std::string testDir = "/tmp/ssr_test_beta2";
    std::filesystem::create_directories(testDir);
    const std::string cfgPath = testDir + "/settings.cfg";
    {
        std::ofstream f(cfgPath);
        f << "list_colour=red\n";
        f << "list_lang=es\n";
    }
    assert(Launcher::ConfigValue(cfgPath, "list_colour") == "red");

    Launcher::SettingsPage page;
    page.Open(cfgPath, false);
    // Find list_colour setting
    page.Enter(5); // GAME LIST
    int colourIdx = -1;
    for (size_t i = 0; i < page.items.size(); i++)
    {
        if (page.items[i].key && !strcmp(page.items[i].key, "list_colour"))
            colourIdx = (int)i;
    }
    assert(colourIdx >= 0);
    page.selected = colourIdx;
    const char* changedKey = page.Change(1); // change to next color (blue)
    assert(changedKey && !strcmp(changedKey, "list_colour"));

    // Verify in-memory stored value updated
    assert(page.Stored("list_colour") == "blue");

    // Verify on-disk file was replaced cleanly
    assert(Launcher::ConfigValue(cfgPath, "list_colour") == "blue");
    assert(Launcher::ConfigValue(cfgPath, "list_lang") == "es");

    // Test ThemeNamed
    Launcher::Theme th = Launcher::ThemeNamed(page.Stored("list_colour"));
    assert(th.frame == AccentNamed("blue"));
    assert(th.bandLine == AccentNamed("blue"));

    // Test 2: AudioQueueSlowdown behavior
    // Normal 60fps playback with healthy queue must NOT engage stretching
    assert(AudioQueueSlowdown(1.0, 1500, false) == 1.0);
    assert(AudioQueueSlowdown(1.0, 2400, false) == 1.0);
    // Severely starved queue (< 1024) must engage stretching even at 1.0
    assert(AudioQueueSlowdown(1.0, 0, false) > 1.08);
    assert(AudioQueueSlowdown(1.0, 500, false) > 1.08);
    // When already stretching, healthy queue scales smoothly towards target
    assert(AudioQueueSlowdown(1.0, 4800, true) == 1.0);
    // Overfilled queue (> 4800) drains smoothly
    assert(AudioQueueSlowdown(1.0, 6000, false) < 1.0);

    std::filesystem::remove_all(testDir);
    puts("PASS: Beta 2 fixes verified (launcher settings save and audio queue pacing)");
    return 0;
}
