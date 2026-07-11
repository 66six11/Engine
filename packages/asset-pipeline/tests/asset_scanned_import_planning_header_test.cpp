#include <string>

#include "asharia/asset_pipeline/asset_scanned_import_planning.hpp"

namespace asharia::asset_pipeline_header_tests {

    void touchScannedImportPlanningHeader() {
        [[maybe_unused]] const asharia::asset::AssetScannedImportPlanRequest request{
            .scan =
                asharia::asset::AssetSourceScanRequest{
                    .sourceRoot = "Content",
                    .sourcePathPrefix = "Content",
                    .metadataSuffix = std::string{asharia::asset::kAssetMetadataSidecarSuffix},
                    .ignoredDirectoryNames = {},
                },
            .productManifest = {},
            .targetProfile = "windows-msvc-debug",
            .toolVersions = {},
        };
        [[maybe_unused]] asharia::asset::AssetScannedImportPlanResult result{};
    }

} // namespace asharia::asset_pipeline_header_tests
