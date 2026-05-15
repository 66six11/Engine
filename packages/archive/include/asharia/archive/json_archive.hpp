#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "asharia/archive/archive_value.hpp"
#include "asharia/core/result.hpp"

namespace asharia::archive {

    [[nodiscard]] Result<std::string> writeJsonArchive(const ArchiveValue& value);
    [[nodiscard]] VoidResult writeJsonArchiveFile(const std::filesystem::path& path,
                                                  const ArchiveValue& value);
    [[nodiscard]] Result<ArchiveValue> readJsonArchive(std::string_view text);
    [[nodiscard]] Result<ArchiveValue> readJsonArchiveFile(const std::filesystem::path& path);

} // namespace asharia::archive
