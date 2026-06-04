#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "asharia/asset_core/asset_catalog.hpp"
#include "asharia/asset_core/asset_metadata.hpp"

namespace asharia::asset {

    enum class AssetSourceDiscoveryDiagnosticCode {
        InvalidEntry,
        MissingMetadata,
        MetadataReadFailed,
        SourcePathMismatch,
        DuplicateGuid,
        DuplicateSourcePath,
        CatalogRejected,
    };

    struct AssetSourceDiscoveryEntry {
        std::string sourcePath;
        std::filesystem::path metadataPath;

        [[nodiscard]] friend bool operator==(const AssetSourceDiscoveryEntry&,
                                             const AssetSourceDiscoveryEntry&) = default;
        [[nodiscard]] explicit operator bool() const noexcept {
            return !sourcePath.empty() && !metadataPath.empty();
        }
    };

    struct DiscoveredSourceAsset {
        AssetSourceDiscoveryEntry entry;
        SourceAssetRecord source;
        std::vector<AssetImportSetting> settings;

        [[nodiscard]] friend bool operator==(const DiscoveredSourceAsset&,
                                             const DiscoveredSourceAsset&) = default;
    };

    struct AssetSourceManifest {
        AssetCatalog catalog;
        std::vector<DiscoveredSourceAsset> records;
    };

    struct AssetSourceDiscoveryDiagnostic {
        AssetSourceDiscoveryDiagnosticCode code{AssetSourceDiscoveryDiagnosticCode::InvalidEntry};
        std::string sourcePath;
        std::filesystem::path metadataPath;
        std::string message;

        [[nodiscard]] friend bool operator==(const AssetSourceDiscoveryDiagnostic&,
                                             const AssetSourceDiscoveryDiagnostic&) = default;
    };

    struct AssetSourceDiscoveryResult {
        AssetSourceManifest manifest;
        std::vector<AssetSourceDiscoveryDiagnostic> diagnostics;

        [[nodiscard]] bool succeeded() const noexcept {
            return diagnostics.empty();
        }
    };

    [[nodiscard]] AssetSourceDiscoveryResult
    discoverAssetSources(std::span<const AssetSourceDiscoveryEntry> entries);

} // namespace asharia::asset
