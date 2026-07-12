#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include "asharia/core/result.hpp"

namespace asharia::editor::detail {

    [[nodiscard]] Result<std::vector<char>> readImGuiBinaryFile(const std::filesystem::path& path,
                                                                std::uint64_t maxBytes);

} // namespace asharia::editor::detail
