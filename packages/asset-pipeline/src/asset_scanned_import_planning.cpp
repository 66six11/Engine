#include "asharia/asset_pipeline/asset_scanned_import_planning.hpp"

#include <span>
#include <vector>

namespace asharia::asset {
    namespace {

        [[nodiscard]] std::vector<AssetSourceDiscoveryEntry>
        makeDiscoveryEntries(std::span<const AssetSourceScanEntry> scanEntries) {
            std::vector<AssetSourceDiscoveryEntry> discoveryEntries;
            discoveryEntries.reserve(scanEntries.size());

            for (const AssetSourceScanEntry& entry : scanEntries) {
                discoveryEntries.push_back(AssetSourceDiscoveryEntry{
                    .sourcePath = entry.sourcePath,
                    .metadataPath = entry.metadataPath,
                });
            }

            return discoveryEntries;
        }

        [[nodiscard]] std::vector<AssetSourceSnapshotEntry>
        makeSnapshotEntries(std::span<const AssetSourceScanEntry> scanEntries) {
            std::vector<AssetSourceSnapshotEntry> snapshotEntries;
            snapshotEntries.reserve(scanEntries.size());

            for (const AssetSourceScanEntry& entry : scanEntries) {
                snapshotEntries.push_back(AssetSourceSnapshotEntry{
                    .sourcePath = entry.sourcePath,
                    .sourceFilePath = entry.sourceFilePath,
                });
            }

            return snapshotEntries;
        }

    } // namespace

    AssetScannedImportPlanResult
    planScannedAssetImports(const AssetScannedImportPlanRequest& request) {
        AssetScannedImportPlanResult result;

        result.scan = scanAssetSourceTree(request.scan);
        if (!result.scan.succeeded()) {
            return result;
        }

        const std::vector<AssetSourceDiscoveryEntry> discoveryEntries =
            makeDiscoveryEntries(result.scan.entries);
        result.discovery = discoverAssetSources(discoveryEntries);
        if (!result.discovery.succeeded()) {
            return result;
        }

        const std::vector<AssetSourceSnapshotEntry> snapshotEntries =
            makeSnapshotEntries(result.scan.entries);
        result.snapshot = snapshotAssetSourceFiles(snapshotEntries);
        if (!result.snapshot.succeeded()) {
            return result;
        }

        result.plan = planAssetImports(
            result.discovery.manifest.records, result.snapshot.snapshots, request.productManifest,
            request.targetProfile, AssetImportPlanOptions{.toolVersions = request.toolVersions});
        return result;
    }

} // namespace asharia::asset
