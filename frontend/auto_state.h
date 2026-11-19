#pragma once
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>

namespace AutoState
{
inline bool Present(const std::string& path)
{
    std::error_code ec;
    const bool exists = std::filesystem::exists(std::filesystem::u8path(path), ec);
    return exists || bool(ec); // An empty, unreadable or damaged state must not be silently replaced.
}

inline std::string Path(const std::string& quick, uint32_t crc, size_t bytes)
{
    char identity[48];
    std::snprintf(identity, sizeof identity, ".auto-%08x-%llu", crc, (unsigned long long)bytes);
    return quick + identity;
}

struct Timer
{
    uint32_t since = 0;
    bool armed = false;

    bool Due(uint32_t now, bool enabled, bool paused)
    {
        if (!enabled) { armed = false; return false; }
        if (!armed) { since = now; armed = true; return false; }
        if (paused || uint32_t(now - since) < 60000) return false;
        since = now;
        return true;
    }

    void Saved(uint32_t now) { since = now; armed = true; }
};
}
