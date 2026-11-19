#include "state_file.h"
#include <cassert>
#include <cstdio>
#include <fcntl.h>
#include <fstream>
#include <iterator>

static int failure = 0, writes = 0;
struct VFile { FILE* file; bool (*close)(VFile*); };
static bool Close(VFile* file)
{
    const bool closed = std::fclose(file->file) == 0;
    delete file;
    return closed && failure != 3;
}
static VFile* VFileOpen(const char* path, int)
{
    FILE* file = std::fopen(path, "w+b");
    return file ? new VFile { file, Close } : nullptr;
}
static bool mCoreSaveStateNamed(void*, VFile* file, int)
{
    ++writes;
    if (failure == 1) return false;
    std::fputs(failure == 2 ? "partial state" : "valid new state", file->file);
    return failure != 2;
}
constexpr int SAVESTATE_RTC = 0;
struct App {
    std::string root, toast;
    void* core = &writes;
    std::string StatePath(int slot) const { return root + "/slot" + std::to_string(slot) + ".ss"; }
    void Toast(const char* text) { toast = text; }
#include "save_state_method.h"
};
static std::string Read(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(in), {} };
}
int main(int argc, char** argv)
{
    assert(argc == 2);
    namespace fs = std::filesystem;
    App app;
    app.root = argv[1];
    const auto slot = app.StatePath(0);
    { std::ofstream out(slot); out << "last good state"; }
    for (int error : { 1, 2, 3 })
    {
        failure = error;
        app.SaveState();
        assert(Read(slot) == "last good state");
        assert(app.toast != "STATE SAVED");
        assert(fs::exists(fs::u8path(slot + ".pending")));
        const int before = writes;
        app.SaveState(); // Leftover .pending becomes .pending.failed and saving proceeds.
        assert(writes == before + 1);
        assert(fs::exists(fs::u8path(slot + ".pending.failed")));
        assert(Read(slot) == "last good state");
        fs::remove(fs::u8path(slot + ".pending"));
        fs::remove(fs::u8path(slot + ".pending.failed"));
    }
    failure = 0;
    app.SaveState();
    assert(app.toast == "STATE SAVED");
    assert(Read(slot) == "valid new state");
    assert(Read(slot + ".previous") == "last good state");
    assert(!fs::exists(fs::u8path(slot + ".pending")));
    app.SaveState(1);
    assert(Read(app.StatePath(1)) == "valid new state");
    assert(Read(slot + ".previous") == "last good state");

    fs::create_directory(fs::u8path(app.StatePath(2)));
    app.SaveState(2);
    assert(fs::is_directory(fs::u8path(app.StatePath(2))));
    fs::remove(fs::u8path(slot + ".previous"));
    fs::create_directory(fs::u8path(slot + ".previous"));
    app.SaveState();
    assert(Read(slot) == "valid new state");
    assert(fs::is_directory(fs::u8path(slot + ".previous")));
    std::puts("GBA SaveState preserves slots on serialization, partial-write, close and replacement failures.");
}
