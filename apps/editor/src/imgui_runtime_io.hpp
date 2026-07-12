#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "asharia/core/result.hpp"

namespace asharia::editor::detail {

    [[nodiscard]] std::string pathToUtf8String(const std::filesystem::path& path);

    [[nodiscard]] Result<std::vector<std::uint32_t>>
    readImGuiSpirvFile(const std::filesystem::path& path, std::uint64_t maxBytes);

    [[nodiscard]] Result<std::vector<char>> readImGuiFontFile(const std::filesystem::path& path,
                                                              std::uint64_t maxBytes);

} // namespace asharia::editor::detail
