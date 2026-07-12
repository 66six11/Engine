#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>

#include "asharia/core/result.hpp"

namespace asharia::asset {

    struct AssetToolFingerprint {
        std::uint64_t fileSize{};
        std::uint64_t contentHash{};
        std::uint64_t versionHash{};

        [[nodiscard]] friend bool operator==(const AssetToolFingerprint&,
                                             const AssetToolFingerprint&) = default;
    };

    // versionHash is FNV-1a over a versioned domain tag, logical tool name, executable filename,
    // actual file size, and contentHash in that order. Only ASCII A-Z is folded to lowercase in
    // the two names; all other UTF-8 bytes are preserved. Paths and timestamps are not identity.
    [[nodiscard]] Result<AssetToolFingerprint>
    fingerprintAssetTool(const std::filesystem::path& executable, std::string_view logicalToolName);

} // namespace asharia::asset
