#pragma once

#include <string>

#include "asharia/asset_pipeline/asset_import_planning.hpp"
#include "asharia/asset_pipeline/asset_source_scan.hpp"

namespace asharia::asset {

    struct AssetScannedImportPlanRequest {
        AssetSourceScanRequest scan;
        AssetProductManifestDocument productManifest;
        std::string targetProfile;

        [[nodiscard]] friend bool operator==(const AssetScannedImportPlanRequest&,
                                             const AssetScannedImportPlanRequest&) = default;
        [[nodiscard]] explicit operator bool() const noexcept {
            return static_cast<bool>(scan) && !targetProfile.empty();
        }
    };

    struct AssetScannedImportPlanResult {
        AssetSourceScanResult scan;
        AssetSourceDiscoveryResult discovery;
        AssetSourceSnapshotResult snapshot;
        AssetImportPlanResult plan;

        [[nodiscard]] bool succeeded() const noexcept {
            return scan.succeeded() && discovery.succeeded() && snapshot.succeeded() &&
                   plan.succeeded();
        }
    };

    [[nodiscard]] AssetScannedImportPlanResult
    planScannedAssetImports(const AssetScannedImportPlanRequest& request);

} // namespace asharia::asset
