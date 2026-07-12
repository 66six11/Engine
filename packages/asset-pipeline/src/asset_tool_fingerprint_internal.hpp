#pragma once

#include <cstddef>
#include <cstdint>
#include <istream>
#include <string_view>

#include "asharia/asset_pipeline/asset_tool_fingerprint.hpp"

namespace asharia::asset::detail {

    struct AssetToolFingerprintStreamLimits {
        std::uint64_t maxBytes{};
        std::size_t bufferBytes{};
    };

    [[nodiscard]] Result<AssetToolFingerprint> fingerprintAssetToolStreamForTesting(
        std::istream& stream, std::uint64_t measuredSize, std::string_view filename,
        std::string_view logicalToolName, const AssetToolFingerprintStreamLimits& limits);

} // namespace asharia::asset::detail
