#pragma once

#include <windows.h>
#include <filesystem>
#include <fstream>
#include <string_view>

namespace wincal
{
inline bool WriteFileAtomically(const std::filesystem::path& path, std::string_view content)
{
    if (path.empty()) return false;
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) return false;
    auto temporary = path;
    temporary += L".tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) return false;
        stream.write(content.data(), static_cast<std::streamsize>(content.size()));
        stream.flush();
        const bool written = static_cast<bool>(stream);
        stream.close();
        if (!written || stream.fail())
        {
            std::filesystem::remove(temporary, error);
            return false;
        }
    }
    if (MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return true;
    std::filesystem::remove(temporary, error);
    return false;
}
}
