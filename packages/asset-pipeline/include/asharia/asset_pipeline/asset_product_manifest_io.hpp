#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "asharia/asset_core/asset_product.hpp"
#include "asharia/core/result.hpp"

namespace asharia::asset {

    inline constexpr std::string_view kAssetProductManifestSchema =
        "com.asharia.asset.product-manifest";
    inline constexpr std::uint32_t kAssetProductManifestVersion = 1;

    struct AssetProductManifestDocument {
        std::vector<AssetProductRecord> products;

        [[nodiscard]] friend bool operator==(const AssetProductManifestDocument&,
                                             const AssetProductManifestDocument&) = default;
    };

    [[nodiscard]] VoidResult validateAssetProductPath(std::string_view productPath);
    [[nodiscard]] VoidResult
    validateAssetProductManifestDocument(const AssetProductManifestDocument& document);

    [[nodiscard]] Result<std::string>
    writeAssetProductManifestText(const AssetProductManifestDocument& document);
    [[nodiscard]] VoidResult
    writeAssetProductManifestFile(const std::filesystem::path& path,
                                  const AssetProductManifestDocument& document);
    [[nodiscard]] Result<AssetProductManifestDocument>
    readAssetProductManifestText(std::string_view text);
    [[nodiscard]] Result<AssetProductManifestDocument>
    readAssetProductManifestFile(const std::filesystem::path& path);

} // namespace asharia::asset
