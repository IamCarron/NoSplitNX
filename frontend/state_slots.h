#pragma once
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <functional>
#include <string>

namespace StateSlots
{
namespace fs = std::filesystem;

inline std::string Path(const std::string& quick, int slot) { return quick + ".slot" + std::to_string(slot); }

inline bool Exists(const std::string& path)
{
    std::error_code ec;
    return fs::is_regular_file(fs::u8path(path), ec) && fs::file_size(fs::u8path(path), ec) > 4 && !ec;
}

inline bool PreviewCurrent(const std::string& path)
{
    std::error_code ec;
    const auto state = fs::last_write_time(fs::u8path(path), ec);
    if (ec) return false;
    const auto image = fs::last_write_time(fs::u8path(path + ".bmp"), ec);
    return !ec && image >= state;
}

inline std::string Date(const std::string& path)
{
    std::error_code ec;
    auto date = fs::last_write_time(fs::u8path(path), ec);
    if (ec) return "EMPTY";
    auto system = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        date - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    std::time_t t = std::chrono::system_clock::to_time_t(system);
    std::tm* local = std::localtime(&t);
    char text[32] = {};
    if (local) std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M", local);
    return text;
}

// Write a save state atomically via .pending and keep the previous one in .previous.
// Switch FAT/libnx cannot rename over an existing file, so backups are removed first.
inline bool Save(const std::string& path, const std::function<bool(const std::string&)>& write)
{
    const fs::path target = fs::u8path(path), temp = fs::u8path(path + ".pending"), backup = fs::u8path(path + ".previous");
    std::error_code ec;
    // An interrupted write (crash or power loss) is kept as .pending.failed for recovery,
    // so the slot can save again instead of blocking until manual deletion.
    if (fs::exists(temp, ec))
    {
        const fs::path failed = fs::u8path(path + ".pending.failed");
        fs::remove(failed, ec);
        ec.clear();
        fs::rename(temp, failed, ec);
        if (ec) return false;
    }
    else if (ec) return false;
    if (!write(temp.u8string()) || !Exists(temp.u8string())) return false;
    const bool had = fs::exists(target, ec);
    if (ec || (had && !fs::is_regular_file(target, ec)) || ec) return false;
    if (had)
    {
        const bool hasBackup = fs::exists(backup, ec);
        if (ec || (hasBackup && !fs::is_regular_file(backup, ec)) || ec) return false;
        fs::remove(backup, ec);
        if (ec) return false;
        fs::rename(target, backup, ec);
        if (ec) return false;
    }
    fs::rename(temp, target, ec);
    if (!ec) return true;
    if (had)
    {
        std::error_code recovery;
        fs::rename(backup, target, recovery);
    }
    return false;
}
}
