#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "asharia/asset_core/asset_metadata.hpp"
#include "asharia/core/result.hpp"

namespace asharia::asset {

    struct AssetMetadataDocument {
        SourceAssetRecord source;
        std::vector<AssetImportSetting> settings;

        [[nodiscard]] friend bool operator==(const AssetMetadataDocument&,
                                             const AssetMetadataDocument&) = default;
        [[nodiscard]] explicit operator bool() const noexcept {
            return static_cast<bool>(source);
        }
    };

    [[nodiscard]] VoidResult validateAssetMetadataDocument(const AssetMetadataDocument& document);

    [[nodiscard]] Result<std::string> writeAssetMetadataText(const AssetMetadataDocument& document);
    [[nodiscard]] VoidResult writeAssetMetadataFile(const std::filesystem::path& path,
                                                    const AssetMetadataDocument& document);

    [[nodiscard]] Result<AssetMetadataDocument> readAssetMetadataText(std::string_view text);
    [[nodiscard]] Result<AssetMetadataDocument>
    readAssetMetadataFile(const std::filesystem::path& path);

} // namespace asharia::asset
