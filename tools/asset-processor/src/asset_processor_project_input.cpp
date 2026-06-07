#include "asset_processor_project_input.hpp"

#include <algorithm>
#include <iterator>
#include <span>
#include <utility>

#include "asharia/asset_pipeline/asset_import_planning.hpp"
#include "asharia/asset_pipeline/asset_source_discovery.hpp"
#include "asharia/asset_pipeline/asset_source_scan.hpp"
#include "asharia/asset_pipeline/asset_source_snapshot.hpp"
#include "asharia/project/project_descriptor_io.hpp"

namespace asharia::asset_processor {
    namespace {

        [[nodiscard]] std::filesystem::path
        projectRootFromPath(const std::filesystem::path& projectPath) {
            const std::filesystem::path parent = projectPath.parent_path();
            return parent.empty() ? std::filesystem::path{"."} : parent;
        }

        [[nodiscard]] std::vector<asharia::asset::AssetSourceDiscoveryEntry>
        makeDiscoveryEntries(std::span<const asharia::asset::AssetSourceScanEntry> scanEntries) {
            std::vector<asharia::asset::AssetSourceDiscoveryEntry> discoveryEntries;
            discoveryEntries.reserve(scanEntries.size());

            for (const asharia::asset::AssetSourceScanEntry& entry : scanEntries) {
                discoveryEntries.push_back(asharia::asset::AssetSourceDiscoveryEntry{
                    .sourcePath = entry.sourcePath,
                    .metadataPath = entry.metadataPath,
                });
            }

            return discoveryEntries;
        }

        [[nodiscard]] std::vector<asharia::asset::AssetSourceSnapshotEntry>
        makeSnapshotEntries(std::span<const asharia::asset::AssetSourceScanEntry> scanEntries) {
            std::vector<asharia::asset::AssetSourceSnapshotEntry> snapshotEntries;
            snapshotEntries.reserve(scanEntries.size());

            for (const asharia::asset::AssetSourceScanEntry& entry : scanEntries) {
                snapshotEntries.push_back(asharia::asset::AssetSourceSnapshotEntry{
                    .sourcePath = entry.sourcePath,
                    .sourceFilePath = entry.sourceFilePath,
                });
            }

            return snapshotEntries;
        }

    } // namespace

    AssetProcessorResolvedInput
    resolveAssetProcessorInput(const AssetProcessorInputOptions& options) {
        if (!options.projectPath) {
            return AssetProcessorResolvedInput{
                .succeeded = true,
                .projectPath = std::nullopt,
                .projectRoot = {},
                .projectName = {},
                .projectId = {},
                .assetCacheRoot = {},
                .sourceRoots =
                    {
                        AssetProcessorSourceRoot{
                            .rootName = "explicit",
                            .sourceRoot = options.sourceRoot,
                            .directory = {},
                            .sourcePathPrefix = options.sourcePathPrefix,
                        },
                    },
                .ignoredDirectoryNames = options.ignoredDirectoryNames,
                .error = {},
            };
        }

        auto descriptor = asharia::project::readAshariaProjectDescriptorFile(*options.projectPath);
        if (!descriptor) {
            return AssetProcessorResolvedInput{
                .succeeded = false,
                .projectPath = options.projectPath,
                .projectRoot = {},
                .projectName = {},
                .projectId = {},
                .assetCacheRoot = {},
                .sourceRoots = {},
                .ignoredDirectoryNames = {},
                .error = descriptor.error().message,
            };
        }

        const std::filesystem::path projectRoot = projectRootFromPath(*options.projectPath);
        std::vector<AssetProcessorSourceRoot> sourceRoots;
        sourceRoots.reserve(descriptor->assetSourceRoots.size());
        for (const asharia::project::AssetSourceRootDesc& root : descriptor->assetSourceRoots) {
            sourceRoots.push_back(AssetProcessorSourceRoot{
                .rootName = root.rootName,
                .sourceRoot = projectRoot / std::filesystem::path{root.directory},
                .directory = root.directory,
                .sourcePathPrefix = root.sourcePathPrefix,
            });
        }

        std::vector<std::string> ignoredDirectoryNames =
            descriptor->assetDiscovery.ignoredDirectoryNames;
        ignoredDirectoryNames.insert(ignoredDirectoryNames.end(),
                                     options.ignoredDirectoryNames.begin(),
                                     options.ignoredDirectoryNames.end());

        return AssetProcessorResolvedInput{
            .succeeded = true,
            .projectPath = options.projectPath,
            .projectRoot = projectRoot,
            .projectName = descriptor->projectName,
            .projectId = asharia::project::formatProjectId(descriptor->projectId),
            .assetCacheRoot = descriptor->assetCacheRoot,
            .sourceRoots = std::move(sourceRoots),
            .ignoredDirectoryNames = std::move(ignoredDirectoryNames),
            .error = {},
        };
    }

    asharia::asset::AssetSourceScanResult
    scanAssetProcessorSourceRoots(const AssetProcessorResolvedInput& input) {
        asharia::asset::AssetSourceScanResult combined;
        for (const AssetProcessorSourceRoot& root : input.sourceRoots) {
            asharia::asset::AssetSourceScanResult rootScan =
                asharia::asset::scanAssetSourceTree(asharia::asset::AssetSourceScanRequest{
                    .sourceRoot = root.sourceRoot,
                    .sourcePathPrefix = root.sourcePathPrefix,
                    .metadataSuffix = std::string{kDefaultAssetProcessorMetadataSuffix},
                    .ignoredDirectoryNames = input.ignoredDirectoryNames,
                });

            combined.entries.insert(combined.entries.end(),
                                    std::make_move_iterator(rootScan.entries.begin()),
                                    std::make_move_iterator(rootScan.entries.end()));
            combined.diagnostics.insert(combined.diagnostics.end(),
                                        std::make_move_iterator(rootScan.diagnostics.begin()),
                                        std::make_move_iterator(rootScan.diagnostics.end()));
        }

        std::ranges::sort(combined.entries,
                          [](const asharia::asset::AssetSourceScanEntry& left,
                             const asharia::asset::AssetSourceScanEntry& right) {
                              return left.sourcePath < right.sourcePath;
                          });
        return combined;
    }

    asharia::asset::AssetScannedImportPlanResult
    planAssetProcessorImports(
        const AssetProcessorResolvedInput& input,
        const asharia::asset::AssetProductManifestDocument& productManifest,
        std::string_view targetProfile) {
        asharia::asset::AssetScannedImportPlanResult result;

        result.scan = scanAssetProcessorSourceRoots(input);
        if (!result.scan.succeeded()) {
            return result;
        }

        const std::vector<asharia::asset::AssetSourceDiscoveryEntry> discoveryEntries =
            makeDiscoveryEntries(result.scan.entries);
        result.discovery = asharia::asset::discoverAssetSources(discoveryEntries);
        if (!result.discovery.succeeded()) {
            return result;
        }

        const std::vector<asharia::asset::AssetSourceSnapshotEntry> snapshotEntries =
            makeSnapshotEntries(result.scan.entries);
        result.snapshot = asharia::asset::snapshotAssetSourceFiles(snapshotEntries);
        if (!result.snapshot.succeeded()) {
            return result;
        }

        result.plan = asharia::asset::planAssetImports(result.discovery.manifest.records,
                                                       result.snapshot.snapshots,
                                                       productManifest, targetProfile);
        return result;
    }

} // namespace asharia::asset_processor
