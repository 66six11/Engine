#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "asharia/asset_core/asset_product.hpp"
#include "asharia/core/result.hpp"

namespace asharia::asset {

    enum class AssetArtifactErrorCode : int {
        InvalidPath = 1,
        InvalidLocator = 2,
        InvalidLimits = 3,
        ByteBudgetExceeded = 4,
        FileReadFailed = 5,
        SizeMismatch = 6,
        HashMismatch = 7,
    };

    struct AssetArtifactLocatorV1 {
        std::string relativePath;
        std::uint64_t expectedBytes{};
        std::uint64_t expectedHash{};

        [[nodiscard]] friend bool operator==(const AssetArtifactLocatorV1&,
                                             const AssetArtifactLocatorV1&) = default;
        [[nodiscard]] explicit operator bool() const noexcept {
            return !relativePath.empty() && expectedHash != 0;
        }
    };

    struct AssetArtifactReadLimits {
        std::uint64_t maxBytes{512ULL * 1024ULL * 1024ULL};

        [[nodiscard]] friend bool operator==(AssetArtifactReadLimits,
                                             AssetArtifactReadLimits) = default;
    };

    struct VerifiedAssetArtifactV1 {
        AssetArtifactLocatorV1 locator;
        std::vector<std::byte> bytes;
    };

    [[nodiscard]] VoidResult validateAssetArtifactRelativePathV1(std::string_view relativePath);

    [[nodiscard]] std::uint64_t hashAssetArtifactBytesV1(std::span<const std::byte> bytes) noexcept;

    [[nodiscard]] Result<AssetArtifactLocatorV1>
    makeAssetArtifactLocatorV1(const AssetProductRecord& product);

    [[nodiscard]] Result<VerifiedAssetArtifactV1>
    readVerifiedAssetArtifactV1(const std::filesystem::path& artifactRoot,
                                const AssetArtifactLocatorV1& locator,
                                AssetArtifactReadLimits limits = {});

    [[nodiscard]] const char* assetArtifactErrorCodeName(AssetArtifactErrorCode code) noexcept;

} // namespace asharia::asset
