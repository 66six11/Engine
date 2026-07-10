#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "asharia/core/result.hpp"

namespace asharia::core {

    struct FileReadLimits {
        std::uint64_t maxBytes{};
    };

    [[nodiscard]] Result<std::vector<std::byte>> readFileBytes(const std::filesystem::path& path,
                                                               FileReadLimits limits);

    [[nodiscard]] Result<std::string> readFileText(const std::filesystem::path& path,
                                                   FileReadLimits limits);

} // namespace asharia::core
