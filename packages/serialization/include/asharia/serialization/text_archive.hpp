#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "asharia/core/result.hpp"
#include "asharia/serialization/archive_value.hpp"

namespace asharia::serialization {

    [[nodiscard]] Result<std::string> writeTextArchive(const ArchiveValue& value);
    [[nodiscard]] Result<ArchiveValue> readTextArchive(std::string_view text);
    [[nodiscard]] Result<ArchiveValue> readTextArchiveFile(const std::filesystem::path& path);

} // namespace asharia::serialization
