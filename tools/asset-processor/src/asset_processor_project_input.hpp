#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "asharia/asset_pipeline/asset_scanned_import_planning.hpp"

namespace asharia::asset_processor {

    inline constexpr std::string_view kDefaultAssetProcessorMetadataSuffix = ".ameta";

    struct AssetProcessorSourceRoot {
        std::string rootName;
        std::filesystem::path sourceRoot;
        std::string directory;
        std::string sourcePathPrefix;
    };

    struct AssetProcessorInputOptions {
        std::optional<std::filesystem::path> projectPath;
        std::filesystem::path sourceRoot;
        std::string sourcePathPrefix;
        std::vector<std::string> ignoredDirectoryNames;
    };

    struct AssetProcessorResolvedInput {
        bool succeeded{};
        std::optional<std::filesystem::path> projectPath;
        std::filesystem::path projectRoot;
        std::string projectName;
        std::string projectId;
        std::string assetCacheRoot;
        std::vector<AssetProcessorSourceRoot> sourceRoots;
        std::vector<std::string> ignoredDirectoryNames;
        std::string error;
    };

    [[nodiscard]] AssetProcessorResolvedInput
    resolveAssetProcessorInput(const AssetProcessorInputOptions& options);

    [[nodiscard]] asharia::asset::AssetSourceScanResult
    scanAssetProcessorSourceRoots(const AssetProcessorResolvedInput& input);

    [[nodiscard]] asharia::asset::AssetScannedImportPlanResult
    planAssetProcessorImports(
        const AssetProcessorResolvedInput& input,
        const asharia::asset::AssetProductManifestDocument& productManifest,
        std::string_view targetProfile);

} // namespace asharia::asset_processor
