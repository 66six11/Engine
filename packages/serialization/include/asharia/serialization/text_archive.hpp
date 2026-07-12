#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include "asharia/core/result.hpp"
#include "asharia/serialization/archive_value.hpp"

namespace asharia::serialization {

    inline constexpr std::uint64_t kMaxTextArchiveBytes = 64ULL * 1024ULL * 1024ULL;

    struct TextArchiveFileOptions {
        std::uint64_t maxBytes{kMaxTextArchiveBytes};
    };

    [[nodiscard]] Result<std::string> writeTextArchive(const ArchiveValue& value);
    [[nodiscard]] VoidResult writeTextArchiveFile(const std::filesystem::path& path,
                                                  const ArchiveValue& value);
    [[nodiscard]] Result<ArchiveValue> readTextArchive(std::string_view text);
    [[nodiscard]] Result<ArchiveValue> readTextArchiveFile(const std::filesystem::path& path,
                                                           TextArchiveFileOptions options = {});

} // namespace asharia::serialization
