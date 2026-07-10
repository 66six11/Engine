#pragma once

#include <cstdint>
#include <filesystem>
#include <istream>

#include "asharia/core/file_io.hpp"

namespace asharia::core::detail {

    [[nodiscard]] Result<std::vector<std::byte>>
    readBoundedStream(std::istream& stream, std::uint64_t measuredBytes, FileReadLimits limits,
                      const std::filesystem::path& path);

} // namespace asharia::core::detail
