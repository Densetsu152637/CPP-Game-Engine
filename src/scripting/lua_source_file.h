#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

namespace scripting
{
    inline constexpr std::size_t max_lua_source_bytes = 1024 * 1024;

    enum class LuaSourceReadError { Unavailable, TooLarge, ReadFailed };

    inline std::string_view lua_source_error_message(const LuaSourceReadError error)
    {
        switch (error)
        {
        case LuaSourceReadError::Unavailable: return "Lua source is unavailable";
        case LuaSourceReadError::TooLarge: return "Lua source exceeds the 1 MiB limit";
        case LuaSourceReadError::ReadFailed: return "Lua source could not be read completely";
        }
        return "Lua source could not be read";
    }

    inline std::expected<std::string, LuaSourceReadError> read_lua_source_file(
        const std::filesystem::path& path)
    {
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error) || error)
            return std::unexpected(LuaSourceReadError::Unavailable);
        const auto bytes = std::filesystem::file_size(path, error);
        if (error) return std::unexpected(LuaSourceReadError::Unavailable);
        if (bytes > max_lua_source_bytes)
            return std::unexpected(LuaSourceReadError::TooLarge);

        std::ifstream file(path, std::ios::binary);
        if (!file) return std::unexpected(LuaSourceReadError::Unavailable);
        std::string source(static_cast<std::size_t>(bytes), '\0');
        if (!source.empty()) file.read(source.data(), static_cast<std::streamsize>(source.size()));
        if (file.bad() || file.gcount() != static_cast<std::streamsize>(source.size()))
            return std::unexpected(LuaSourceReadError::ReadFailed);
        char extra = 0;
        if (file.get(extra)) return std::unexpected(LuaSourceReadError::TooLarge);
        if (!file.eof()) return std::unexpected(LuaSourceReadError::ReadFailed);
        return source;
    }
}
