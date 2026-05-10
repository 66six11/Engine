#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "vke/core/result.hpp"
#include "vke/serialization/archive_value.hpp"

namespace vke::serialization {

    [[nodiscard]] Result<std::string> writeTextArchive(const ArchiveValue& value);
    [[nodiscard]] Result<ArchiveValue> readTextArchive(std::string_view text);
    [[nodiscard]] Result<ArchiveValue> readTextArchiveFile(const std::filesystem::path& path);

} // namespace vke::serialization
