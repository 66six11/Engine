#include "editor_asset_catalog.hpp"

#include <algorithm>
#include <array>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "asharia/asset_core/asset_catalog.hpp"
#include "asharia/asset_core/asset_guid.hpp"
#include "asharia/asset_core/asset_metadata_io.hpp"
#include "asharia/asset_core/asset_product.hpp"
#include "asharia/asset_pipeline/asset_import_planning.hpp"
#include "asharia/asset_pipeline/asset_product_manifest_io.hpp"
#include "asharia/asset_pipeline/asset_source_discovery.hpp"
#include "asharia/asset_pipeline/asset_source_scan.hpp"
#include "asharia/asset_pipeline/asset_source_snapshot.hpp"
#include "asharia/asset_pipeline/asset_texture_import_profile.hpp"
#include "asharia/project/project_descriptor_io.hpp"

namespace asharia::editor {
    namespace {

        constexpr std::string_view kDefaultAssetTypeName = "com.asharia.asset.DefaultAsset";
        constexpr std::string_view kDefaultAssetRoleName = "com.asharia.asset.DefaultAsset";

        void addDiagnostic(EditorAssetCatalogSnapshot& snapshot,
                           EditorAssetCatalogDiagnosticCode code,
                           EditorAssetCatalogDiagnosticSeverity severity, std::string sourcePath,
                           std::filesystem::path path, std::string message) {
            snapshot.diagnostics.push_back(EditorAssetCatalogDiagnostic{
                .code = code,
                .severity = severity,
                .sourcePath = std::move(sourcePath),
                .path = std::move(path),
                .message = std::move(message),
            });
        }

        [[nodiscard]] std::filesystem::path
        projectFilePathFor(const std::filesystem::path& projectPath) {
            if (projectPath.empty()) {
                return {};
            }

            std::error_code error;
            const bool isDirectory = std::filesystem::is_directory(projectPath, error);
            if (isDirectory && !error) {
                return projectPath / std::string{asharia::project::kDefaultAshariaProjectFileName};
            }
            return projectPath;
        }

        [[nodiscard]] std::string pathText(const std::filesystem::path& path) {
            const std::u8string text = path.generic_u8string();
            return std::string{text.begin(), text.end()};
        }

        [[nodiscard]] std::filesystem::path pathFromUtf8(std::string_view text) {
            std::u8string utf8;
            utf8.reserve(text.size());
            for (const char value : text) {
                utf8.push_back(static_cast<char8_t>(static_cast<unsigned char>(value)));
            }
            return std::filesystem::path{utf8};
        }

        [[nodiscard]] std::filesystem::path
        projectDirectoryFor(const std::filesystem::path& projectFile) {
            const std::filesystem::path directory = projectFile.parent_path();
            return directory.empty() ? std::filesystem::path{"."} : directory;
        }

        [[nodiscard]] bool sourcePathHasPrefix(std::string_view sourcePath,
                                               std::string_view sourcePathPrefix) {
            if (sourcePathPrefix.empty()) {
                return true;
            }
            return sourcePath.starts_with(sourcePathPrefix) &&
                   (sourcePath.size() == sourcePathPrefix.size() ||
                    sourcePath[sourcePathPrefix.size()] == '/');
        }

        [[nodiscard]] std::string_view sourcePathWithoutPrefix(std::string_view sourcePath,
                                                               std::string_view sourcePathPrefix) {
            if (sourcePathPrefix.empty()) {
                return sourcePath;
            }
            sourcePath.remove_prefix(sourcePathPrefix.size());
            if (!sourcePath.empty() && sourcePath.front() == '/') {
                sourcePath.remove_prefix(1U);
            }
            return sourcePath;
        }

        [[nodiscard]] std::string_view sourcePathFileName(std::string_view sourcePath) {
            const std::size_t slash = sourcePath.find_last_of('/');
            if (slash == std::string_view::npos) {
                return sourcePath;
            }
            return sourcePath.substr(slash + 1U);
        }

        [[nodiscard]] std::string extensionForSourcePath(std::string_view sourcePath) {
            const std::string_view name = sourcePathFileName(sourcePath);
            const std::size_t dot = name.find_last_of('.');
            if (dot == std::string_view::npos || dot == 0U || dot + 1U >= name.size()) {
                return {};
            }
            return std::string{name.substr(dot)};
        }

        [[nodiscard]] std::string_view sourcePathDirectory(std::string_view sourcePath) {
            const std::size_t slash = sourcePath.find_last_of('/');
            if (slash == std::string_view::npos) {
                return {};
            }
            return sourcePath.substr(0U, slash);
        }

        [[nodiscard]] std::string sourceRootNavigationKey(std::size_t index) {
            return "source-root:" + std::to_string(index);
        }

        [[nodiscard]] std::string folderNavigationKey(std::string_view scopePath) {
            return "folder:" + std::string{scopePath};
        }

        [[nodiscard]] std::string assetNavigationKey(std::string_view sourcePath) {
            return "asset:" + std::string{sourcePath};
        }

        [[nodiscard]] std::string subAssetNavigationKey(std::string_view sourcePath,
                                                        std::string_view stableId) {
            std::string key{"sub-asset:"};
            key += sourcePath;
            key += '#';
            key += stableId;
            return key;
        }

        [[nodiscard]] bool
        hasNavigationNodeKey(std::span<const EditorAssetCatalogNavigationNode> nodes,
                             std::string_view key) {
            return std::ranges::any_of(nodes, [key](const EditorAssetCatalogNavigationNode& node) {
                return node.key == key;
            });
        }

        [[nodiscard]] std::size_t
        sourceRootIndexForSourcePath(const EditorAssetCatalogSnapshot& snapshot,
                                     std::string_view sourcePath) {
            std::size_t bestIndex = snapshot.project.assetSourceRoots.size();
            std::size_t bestPrefixSize = 0U;
            for (std::size_t index = 0U; index < snapshot.project.assetSourceRoots.size();
                 ++index) {
                const asharia::project::AssetSourceRootDesc& root =
                    snapshot.project.assetSourceRoots[index];
                if (!sourcePathHasPrefix(sourcePath, root.sourcePathPrefix) ||
                    root.sourcePathPrefix.size() < bestPrefixSize) {
                    continue;
                }
                bestIndex = index;
                bestPrefixSize = root.sourcePathPrefix.size();
            }
            return bestIndex;
        }

        void appendNavigationFolderNodes(std::vector<EditorAssetCatalogNavigationNode>& nodes,
                                         const EditorAssetCatalogResolvedSourceRoot& sourceRoot,
                                         std::size_t sourceRootIndex,
                                         std::string_view folderScope) {
            std::string parentKey = sourceRootNavigationKey(sourceRootIndex);
            std::string currentScope{sourceRoot.sourcePathPrefix};
            std::string_view relativeFolder =
                sourcePathWithoutPrefix(folderScope, sourceRoot.sourcePathPrefix);
            while (!relativeFolder.empty()) {
                const std::size_t slash = relativeFolder.find('/');
                const std::string_view segment = slash == std::string_view::npos
                                                     ? relativeFolder
                                                     : relativeFolder.substr(0U, slash);
                if (!segment.empty()) {
                    if (!currentScope.empty()) {
                        currentScope += '/';
                    }
                    currentScope += segment;
                    std::string nodeKey = folderNavigationKey(currentScope);
                    if (!hasNavigationNodeKey(nodes, nodeKey)) {
                        nodes.push_back(EditorAssetCatalogNavigationNode{
                            .kind = EditorAssetCatalogNavigationNodeKind::Folder,
                            .key = nodeKey,
                            .parentKey = parentKey,
                            .displayName = std::string{segment},
                            .scopePath = currentScope,
                            .sourcePath = {},
                            .sourceRootName = sourceRoot.rootName,
                            .sourceRootPrefix = sourceRoot.sourcePathPrefix,
                            .sourceRootDirectory = sourceRoot.resolvedDirectory,
                            .guidText = {},
                            .stableId = {},
                            .assetTypeName = {},
                            .importerName = {},
                            .extension = {},
                            .importProfileName = {},
                            .assetRoleName = {},
                            .subAssetCount = 0U,
                            .productState = asharia::asset::AssetCatalogProductState::NotTracked,
                        });
                    }
                    parentKey = nodeKey;
                }
                if (slash == std::string_view::npos) {
                    break;
                }
                relativeFolder.remove_prefix(slash + 1U);
            }
        }

        [[nodiscard]] std::string
        navigationParentKeyForAsset(std::span<const EditorAssetCatalogNavigationNode> nodes,
                                    const EditorAssetCatalogResolvedSourceRoot& sourceRoot,
                                    std::size_t sourceRootIndex, std::string_view sourcePath) {
            const std::string_view folderScope = sourcePathDirectory(sourcePath);
            if (folderScope.empty() || folderScope == sourceRoot.sourcePathPrefix) {
                return sourceRootNavigationKey(sourceRootIndex);
            }
            std::string folderKey = folderNavigationKey(folderScope);
            if (hasNavigationNodeKey(nodes, folderKey)) {
                return folderKey;
            }
            return sourceRootNavigationKey(sourceRootIndex);
        }

        [[nodiscard]] std::filesystem::path defaultProjectProductManifestFile(
            const std::filesystem::path& projectDirectory,
            const asharia::project::AshariaProjectDescriptor& project) {
            std::filesystem::path outputRoot = std::filesystem::path{project.assetCacheRoot};
            if (outputRoot.is_relative()) {
                outputRoot = projectDirectory / outputRoot;
            }

            const std::filesystem::path manifestDirectory = outputRoot.parent_path();
            std::filesystem::path manifestFile;
            if (manifestDirectory.empty()) {
                manifestFile = std::filesystem::path{"products.aproducts.json"};
            } else {
                manifestFile = manifestDirectory / "products.aproducts.json";
            }
            manifestFile.make_preferred();
            return manifestFile;
        }

        [[nodiscard]] std::filesystem::path
        productManifestFileFor(const EditorAssetCatalogSnapshotRequest& request,
                               const std::filesystem::path& projectDirectory,
                               const asharia::project::AshariaProjectDescriptor& project) {
            if (!request.productManifestFile.empty()) {
                return request.productManifestFile;
            }

            std::filesystem::path defaultManifest =
                defaultProjectProductManifestFile(projectDirectory, project);
            std::error_code error;
            if (std::filesystem::exists(defaultManifest, error) && !error) {
                return defaultManifest;
            }
            return {};
        }

        [[nodiscard]] std::filesystem::path
        sourceRootPath(const std::filesystem::path& projectDirectory,
                       const asharia::project::AssetSourceRootDesc& root) {
            std::filesystem::path rootPath = std::filesystem::path{root.directory};
            if (rootPath.is_relative()) {
                rootPath = projectDirectory / rootPath;
            }
            return rootPath;
        }

        [[nodiscard]] std::vector<asharia::asset::AssetSourceDiscoveryEntry>
        makeDiscoveryEntries(std::span<const asharia::asset::AssetSourceScanEntry> scanEntries) {
            std::vector<asharia::asset::AssetSourceDiscoveryEntry> entries;
            entries.reserve(scanEntries.size());
            for (const asharia::asset::AssetSourceScanEntry& entry : scanEntries) {
                entries.push_back(asharia::asset::AssetSourceDiscoveryEntry{
                    .sourcePath = entry.sourcePath,
                    .metadataPath = entry.metadataPath,
                });
            }
            return entries;
        }

        [[nodiscard]] std::vector<asharia::asset::AssetSourceSnapshotEntry>
        makeSnapshotEntries(std::span<const asharia::asset::AssetSourceScanEntry> scanEntries) {
            std::vector<asharia::asset::AssetSourceSnapshotEntry> entries;
            entries.reserve(scanEntries.size());
            for (const asharia::asset::AssetSourceScanEntry& entry : scanEntries) {
                entries.push_back(asharia::asset::AssetSourceSnapshotEntry{
                    .sourcePath = entry.sourcePath,
                    .sourceFilePath = entry.sourceFilePath,
                });
            }
            return entries;
        }

        [[nodiscard]] asharia::asset::AssetProductManifestDocument
        readProductManifest(const std::filesystem::path& productManifestFile,
                            EditorAssetCatalogSnapshot& snapshot) {
            if (productManifestFile.empty()) {
                return {};
            }

            auto manifest = asharia::asset::readAssetProductManifestFile(productManifestFile);
            if (!manifest) {
                addDiagnostic(snapshot, EditorAssetCatalogDiagnosticCode::ProductManifestReadFailed,
                              EditorAssetCatalogDiagnosticSeverity::Error, {}, productManifestFile,
                              "Editor asset catalog snapshot could not read product manifest: " +
                                  manifest.error().message);
                return {};
            }
            return std::move(*manifest);
        }

        [[nodiscard]] bool
        scanDiagnosticIsFatal(asharia::asset::AssetSourceScanDiagnosticCode code) noexcept {
            switch (code) {
            case asharia::asset::AssetSourceScanDiagnosticCode::MissingMetadata:
            case asharia::asset::AssetSourceScanDiagnosticCode::OrphanMetadata:
                return false;
            case asharia::asset::AssetSourceScanDiagnosticCode::InvalidRequest:
            case asharia::asset::AssetSourceScanDiagnosticCode::InvalidRoot:
            case asharia::asset::AssetSourceScanDiagnosticCode::FilesystemError:
            case asharia::asset::AssetSourceScanDiagnosticCode::InvalidSourcePath:
            case asharia::asset::AssetSourceScanDiagnosticCode::DuplicateSourcePath:
            case asharia::asset::AssetSourceScanDiagnosticCode::DuplicateMetadataPath:
                return true;
            }
            return true;
        }

        [[nodiscard]] EditorAssetCatalogDiagnosticSeverity
        severityForScanDiagnostic(asharia::asset::AssetSourceScanDiagnosticCode code) noexcept {
            return scanDiagnosticIsFatal(code) ? EditorAssetCatalogDiagnosticSeverity::Error
                                               : EditorAssetCatalogDiagnosticSeverity::Warning;
        }

        [[nodiscard]] bool hasFatalScanDiagnostics(
            std::span<const asharia::asset::AssetSourceScanDiagnostic> diagnostics) {
            return std::ranges::any_of(
                diagnostics, [](const asharia::asset::AssetSourceScanDiagnostic& diagnostic) {
                    return scanDiagnosticIsFatal(diagnostic.code);
                });
        }

        void appendScanDiagnostics(
            EditorAssetCatalogSnapshot& snapshot,
            std::span<const asharia::asset::AssetSourceScanDiagnostic> diagnostics) {
            for (const asharia::asset::AssetSourceScanDiagnostic& diagnostic : diagnostics) {
                addDiagnostic(snapshot, EditorAssetCatalogDiagnosticCode::SourceScan,
                              severityForScanDiagnostic(diagnostic.code), diagnostic.sourcePath,
                              diagnostic.sourceFilePath, diagnostic.message);
            }
        }

        void appendDiscoveryDiagnostics(
            EditorAssetCatalogSnapshot& snapshot,
            std::span<const asharia::asset::AssetSourceDiscoveryDiagnostic> diagnostics) {
            for (const asharia::asset::AssetSourceDiscoveryDiagnostic& diagnostic : diagnostics) {
                addDiagnostic(snapshot, EditorAssetCatalogDiagnosticCode::SourceDiscovery,
                              EditorAssetCatalogDiagnosticSeverity::Error, diagnostic.sourcePath,
                              diagnostic.metadataPath, diagnostic.message);
            }
        }

        void appendSnapshotDiagnostics(
            EditorAssetCatalogSnapshot& snapshot,
            std::span<const asharia::asset::AssetSourceSnapshotDiagnostic> diagnostics) {
            for (const asharia::asset::AssetSourceSnapshotDiagnostic& diagnostic : diagnostics) {
                addDiagnostic(snapshot, EditorAssetCatalogDiagnosticCode::SourceSnapshot,
                              EditorAssetCatalogDiagnosticSeverity::Error, diagnostic.sourcePath,
                              diagnostic.sourceFilePath, diagnostic.message);
            }
        }

        [[nodiscard]] EditorAssetCatalogDiagnosticSeverity severityForImportPlanDiagnostic(
            asharia::asset::AssetImportPlanDiagnosticSeverity severity) noexcept {
            switch (severity) {
            case asharia::asset::AssetImportPlanDiagnosticSeverity::Info:
                return EditorAssetCatalogDiagnosticSeverity::Info;
            case asharia::asset::AssetImportPlanDiagnosticSeverity::Warning:
                return EditorAssetCatalogDiagnosticSeverity::Warning;
            case asharia::asset::AssetImportPlanDiagnosticSeverity::Error:
                return EditorAssetCatalogDiagnosticSeverity::Error;
            }
            return EditorAssetCatalogDiagnosticSeverity::Error;
        }

        void appendImportPlanDiagnostics(
            EditorAssetCatalogSnapshot& snapshot,
            std::span<const asharia::asset::AssetImportPlanDiagnostic> diagnostics) {
            for (const asharia::asset::AssetImportPlanDiagnostic& diagnostic : diagnostics) {
                addDiagnostic(snapshot, EditorAssetCatalogDiagnosticCode::ImportPlanning,
                              severityForImportPlanDiagnostic(diagnostic.severity),
                              diagnostic.sourcePath, {}, diagnostic.message);
            }
        }

        [[nodiscard]] EditorAssetCatalogDiagnosticSeverity
        severityForCatalogDiagnostic(const asharia::asset::AssetCatalogDiagnostic& diagnostic) {
            switch (diagnostic.severity) {
            case asharia::asset::AssetCatalogDiagnosticSeverity::Info:
                return EditorAssetCatalogDiagnosticSeverity::Info;
            case asharia::asset::AssetCatalogDiagnosticSeverity::Warning:
                return EditorAssetCatalogDiagnosticSeverity::Warning;
            case asharia::asset::AssetCatalogDiagnosticSeverity::Error:
                return EditorAssetCatalogDiagnosticSeverity::Error;
            }
            return EditorAssetCatalogDiagnosticSeverity::Warning;
        }

        void appendCatalogViewDiagnostics(
            EditorAssetCatalogSnapshot& snapshot,
            std::span<const asharia::asset::AssetCatalogDiagnostic> diagnostics) {
            for (const asharia::asset::AssetCatalogDiagnostic& diagnostic : diagnostics) {
                addDiagnostic(snapshot, EditorAssetCatalogDiagnosticCode::CatalogView,
                              severityForCatalogDiagnostic(diagnostic), diagnostic.sourcePath, {},
                              diagnostic.message);
            }
        }

        void mergeSource(asharia::asset::AssetCatalog& catalog,
                         EditorAssetCatalogSnapshot& snapshot,
                         asharia::asset::SourceAssetRecord source) {
            const std::string sourcePath = source.sourcePath;
            auto added = catalog.addSource(std::move(source));
            if (!added) {
                addDiagnostic(snapshot, EditorAssetCatalogDiagnosticCode::CatalogMerge,
                              EditorAssetCatalogDiagnosticSeverity::Error, sourcePath, {},
                              "Editor asset catalog snapshot could not merge source: " +
                                  added.error().message);
            }
        }

        void mergePlannedSources(asharia::asset::AssetCatalog& catalog,
                                 EditorAssetCatalogSnapshot& snapshot,
                                 const asharia::asset::AssetImportPlanResult& plan) {
            for (const asharia::asset::AssetImportCacheHit& hit : plan.cacheHits) {
                mergeSource(catalog, snapshot, hit.source);
            }
            for (const asharia::asset::AssetImportRequest& request : plan.requests) {
                mergeSource(catalog, snapshot, request.source);
            }
        }

        void
        appendExpectedProductKeys(std::vector<asharia::asset::AssetProductKey>& expectedProductKeys,
                                  const asharia::asset::AssetImportPlanResult& plan) {
            expectedProductKeys.reserve(expectedProductKeys.size() + plan.cacheHits.size() +
                                        plan.requests.size());
            for (const asharia::asset::AssetImportCacheHit& hit : plan.cacheHits) {
                expectedProductKeys.push_back(hit.product.key);
            }
            for (const asharia::asset::AssetImportRequest& request : plan.requests) {
                expectedProductKeys.push_back(request.productKey);
            }
        }

        [[nodiscard]] bool catalogViewContainsSourcePath(
            std::span<const asharia::asset::AssetCatalogViewEntry> entries,
            std::string_view sourcePath) {
            return std::ranges::any_of(
                entries, [sourcePath](const asharia::asset::AssetCatalogViewEntry& entry) {
                    return entry.sourcePath == sourcePath;
                });
        }

        [[nodiscard]] bool
        discoveryContainsSourcePath(std::span<const asharia::asset::DiscoveredSourceAsset> records,
                                    std::string_view sourcePath) {
            return std::ranges::any_of(
                records, [sourcePath](const asharia::asset::DiscoveredSourceAsset& discovered) {
                    return discovered.source.sourcePath == sourcePath;
                });
        }

        [[nodiscard]] asharia::asset::AssetCatalogDiagnostic
        defaultAssetDiagnostic(std::string_view sourcePath, std::string_view message) {
            return asharia::asset::AssetCatalogDiagnostic{
                .code = asharia::asset::AssetCatalogDiagnosticCode::SourceMetadata,
                .severity = asharia::asset::AssetCatalogDiagnosticSeverity::Warning,
                .guid = {},
                .sourcePath = std::string{sourcePath},
                .message = std::string{message},
            };
        }

        [[nodiscard]] asharia::asset::AssetCatalogViewEntry
        makeDefaultAssetEntry(std::string_view sourcePath, std::string_view message) {
            return asharia::asset::AssetCatalogViewEntry{
                .guid = {},
                .guidText = {},
                .assetType = asharia::asset::makeAssetTypeId(kDefaultAssetTypeName),
                .assetTypeName = std::string{kDefaultAssetTypeName},
                .sourcePath = std::string{sourcePath},
                .displayName = std::string{sourcePathFileName(sourcePath)},
                .extension = extensionForSourcePath(sourcePath),
                .importProfileName = {},
                .assetRoleName = std::string{kDefaultAssetRoleName},
                .importerId = {},
                .importerName = {},
                .importerVersion = {},
                .productState = asharia::asset::AssetCatalogProductState::NotTracked,
                .currentProductCount = 0U,
                .staleProductCount = 0U,
                .subAssets = {},
                .diagnostics = {defaultAssetDiagnostic(sourcePath, message)},
            };
        }

        void appendDefaultAssetRow(std::vector<asharia::asset::AssetCatalogViewEntry>& defaultRows,
                                   std::string_view sourcePath, std::string_view message) {
            if (sourcePath.empty() || catalogViewContainsSourcePath(defaultRows, sourcePath)) {
                return;
            }
            defaultRows.push_back(makeDefaultAssetEntry(sourcePath, message));
        }

        void appendMissingMetadataDefaultRows(
            std::vector<asharia::asset::AssetCatalogViewEntry>& defaultRows,
            std::span<const asharia::asset::AssetSourceScanDiagnostic> diagnostics) {
            for (const asharia::asset::AssetSourceScanDiagnostic& diagnostic : diagnostics) {
                if (diagnostic.code !=
                    asharia::asset::AssetSourceScanDiagnosticCode::MissingMetadata) {
                    continue;
                }
                appendDefaultAssetRow(defaultRows, diagnostic.sourcePath,
                                      "Asset source has no metadata sidecar; it is visible as a "
                                      "default asset and no product will be generated.");
            }
        }

        void appendUndiscoveredSourceDefaultRows(
            std::vector<asharia::asset::AssetCatalogViewEntry>& defaultRows,
            std::span<const asharia::asset::AssetSourceScanEntry> scanEntries,
            std::span<const asharia::asset::DiscoveredSourceAsset> discoveredSources) {
            for (const asharia::asset::AssetSourceScanEntry& entry : scanEntries) {
                if (discoveryContainsSourcePath(discoveredSources, entry.sourcePath)) {
                    continue;
                }
                appendDefaultAssetRow(defaultRows, entry.sourcePath,
                                      "Asset source metadata did not produce a catalog source; it "
                                      "is visible as a default asset and no product will be "
                                      "generated.");
            }
        }

        void sortCatalogViewEntries(std::vector<asharia::asset::AssetCatalogViewEntry>& entries) {
            std::ranges::sort(entries, [](const asharia::asset::AssetCatalogViewEntry& left,
                                          const asharia::asset::AssetCatalogViewEntry& right) {
                if (left.sourcePath != right.sourcePath) {
                    return left.sourcePath < right.sourcePath;
                }
                return left.guidText < right.guidText;
            });
        }

        void appendTextureProfileFacets(
            std::vector<asharia::asset::AssetCatalogSourceFacet>& sourceFacets,
            std::span<const asharia::asset::DiscoveredSourceAsset> discoveredSources) {
            sourceFacets.reserve(sourceFacets.size() + discoveredSources.size());
            for (const asharia::asset::DiscoveredSourceAsset& discovered : discoveredSources) {
                asharia::asset::AssetCatalogSourceFacet facet =
                    asharia::asset::makeTextureImportCatalogSourceFacet(discovered.source,
                                                                        discovered.settings);
                if (!facet.importProfileName.empty() || !facet.assetRoleName.empty() ||
                    !facet.subAssets.empty() || !facet.diagnostics.empty()) {
                    sourceFacets.push_back(std::move(facet));
                }
            }
        }

        void appendRootSnapshot(const std::filesystem::path& projectDirectory,
                                const asharia::project::AssetSourceRootDesc& root,
                                const asharia::asset::AssetProductManifestDocument& productManifest,
                                const std::string& targetProfile,
                                asharia::asset::AssetCatalog& catalog,
                                std::vector<asharia::asset::AssetProductKey>& expectedProductKeys,
                                std::vector<asharia::asset::AssetCatalogSourceFacet>& sourceFacets,
                                std::vector<asharia::asset::AssetCatalogViewEntry>& defaultRows,
                                EditorAssetCatalogSnapshot& snapshot) {
            const asharia::asset::AssetSourceScanRequest scanRequest{
                .sourceRoot = sourceRootPath(projectDirectory, root),
                .sourcePathPrefix = root.sourcePathPrefix,
                .metadataSuffix = std::string{asharia::asset::kAssetMetadataSidecarSuffix},
                .ignoredDirectoryNames = snapshot.project.assetDiscovery.ignoredDirectoryNames,
            };

            const asharia::asset::AssetSourceScanResult scan =
                asharia::asset::scanAssetSourceTree(scanRequest);
            appendScanDiagnostics(snapshot, scan.diagnostics);
            appendMissingMetadataDefaultRows(defaultRows, scan.diagnostics);
            if (hasFatalScanDiagnostics(scan.diagnostics)) {
                return;
            }

            const std::vector<asharia::asset::AssetSourceDiscoveryEntry> discoveryEntries =
                makeDiscoveryEntries(scan.entries);
            const asharia::asset::AssetSourceDiscoveryResult discovery =
                asharia::asset::discoverAssetSources(discoveryEntries);
            appendDiscoveryDiagnostics(snapshot, discovery.diagnostics);
            appendUndiscoveredSourceDefaultRows(defaultRows, scan.entries,
                                                discovery.manifest.records);

            const std::vector<asharia::asset::AssetSourceSnapshotEntry> snapshotEntries =
                makeSnapshotEntries(scan.entries);
            const asharia::asset::AssetSourceSnapshotResult sourceSnapshot =
                asharia::asset::snapshotAssetSourceFiles(snapshotEntries);
            appendSnapshotDiagnostics(snapshot, sourceSnapshot.diagnostics);

            const asharia::asset::AssetImportPlanResult plan = asharia::asset::planAssetImports(
                discovery.manifest.records, sourceSnapshot.snapshots, productManifest,
                targetProfile);
            appendImportPlanDiagnostics(snapshot, plan.diagnostics);
            appendTextureProfileFacets(sourceFacets, discovery.manifest.records);
            appendExpectedProductKeys(expectedProductKeys, plan);
            mergePlannedSources(catalog, snapshot, plan);
        }

        struct FixtureSourceDesc {
            std::string_view guidText;
            std::string_view assetTypeName;
            std::string_view sourcePath;
            std::string_view importerName;
            std::uint64_t sourceHash{};
            std::uint64_t settingsHash{};
        };

        [[nodiscard]] asharia::asset::SourceAssetRecord
        fixtureSourceRecord(const FixtureSourceDesc& desc) {
            auto guid = asharia::asset::parseAssetGuid(desc.guidText);
            return asharia::asset::SourceAssetRecord{
                .guid = guid ? *guid : asharia::asset::AssetGuid{},
                .assetType = asharia::asset::makeAssetTypeId(desc.assetTypeName),
                .assetTypeName = std::string{desc.assetTypeName},
                .sourcePath = std::string{desc.sourcePath},
                .importerId = asharia::asset::makeImporterId(desc.importerName),
                .importerName = std::string{desc.importerName},
                .importerVersion = asharia::asset::ImporterVersion{1},
                .sourceHash = desc.sourceHash,
                .settingsHash = desc.settingsHash,
            };
        }

        [[nodiscard]] asharia::asset::AssetProductRecord
        fixtureProductRecord(const asharia::asset::SourceAssetRecord& source,
                             std::uint64_t dependencyHash, std::uint64_t targetProfileHash,
                             std::string_view relativeProductPath, std::uint64_t productSizeBytes) {
            const asharia::asset::AssetProductKey productKey =
                asharia::asset::makeAssetProductKey(source, dependencyHash, targetProfileHash);
            return asharia::asset::AssetProductRecord{
                .key = productKey,
                .relativeProductPath = std::string{relativeProductPath},
                .productSizeBytes = productSizeBytes,
                .productHash = asharia::asset::hashAssetProductKey(productKey),
            };
        }

    } // namespace

    EditorAssetCatalogStore::EditorAssetCatalogStore()
        : fixtureCatalog_{makeEditorAssetBrowserFixtureCatalogView()} {}

    void EditorAssetCatalogStore::useFixtureCatalog() {
        snapshot_ = {};
        hasSnapshot_ = false;
    }

    void EditorAssetCatalogStore::useSnapshot(EditorAssetCatalogSnapshot snapshot) {
        snapshot_ = std::move(snapshot);
        hasSnapshot_ = true;
    }

    const asharia::asset::AssetCatalogView& EditorAssetCatalogStore::catalogView() const noexcept {
        return hasSnapshot_ ? snapshot_.catalogView : fixtureCatalog_;
    }

    const EditorAssetCatalogSnapshot* EditorAssetCatalogStore::snapshot() const noexcept {
        return hasSnapshot_ ? &snapshot_ : nullptr;
    }

    std::span<const EditorAssetCatalogDiagnostic>
    EditorAssetCatalogStore::diagnostics() const noexcept {
        return hasSnapshot_ ? std::span<const EditorAssetCatalogDiagnostic>{snapshot_.diagnostics}
                            : std::span<const EditorAssetCatalogDiagnostic>{};
    }

    EditorAssetCatalogSnapshotRequest
    makeEditorAssetCatalogSnapshotRequest(const EditorAssetCatalogSnapshot& snapshot) {
        return EditorAssetCatalogSnapshotRequest{
            .projectFile = snapshot.projectFile,
            .productManifestFile = snapshot.productManifestFile,
            .targetProfile = snapshot.targetProfile,
        };
    }

    const EditorAssetCatalogSnapshot*
    refreshEditorAssetCatalogStore(EditorAssetCatalogStore& store) {
        const EditorAssetCatalogSnapshot* snapshot = store.snapshot();
        if (snapshot == nullptr) {
            return nullptr;
        }

        return refreshEditorAssetCatalogStore(store,
                                              makeEditorAssetCatalogSnapshotRequest(*snapshot));
    }

    const EditorAssetCatalogSnapshot*
    refreshEditorAssetCatalogStore(EditorAssetCatalogStore& store,
                                   const EditorAssetCatalogSnapshotRequest& request) {
        store.useSnapshot(loadEditorAssetCatalogSnapshot(request));
        return store.snapshot();
    }

    std::string_view
    editorAssetCatalogDiagnosticCodeName(EditorAssetCatalogDiagnosticCode code) noexcept {
        switch (code) {
        case EditorAssetCatalogDiagnosticCode::InvalidRequest:
            return "invalid-request";
        case EditorAssetCatalogDiagnosticCode::ProjectDescriptorReadFailed:
            return "project-descriptor-read-failed";
        case EditorAssetCatalogDiagnosticCode::ProductManifestReadFailed:
            return "product-manifest-read-failed";
        case EditorAssetCatalogDiagnosticCode::SourceScan:
            return "source-scan";
        case EditorAssetCatalogDiagnosticCode::SourceDiscovery:
            return "source-discovery";
        case EditorAssetCatalogDiagnosticCode::SourceSnapshot:
            return "source-snapshot";
        case EditorAssetCatalogDiagnosticCode::ImportPlanning:
            return "import-planning";
        case EditorAssetCatalogDiagnosticCode::CatalogMerge:
            return "catalog-merge";
        case EditorAssetCatalogDiagnosticCode::CatalogView:
            return "catalog-view";
        }
        return "invalid-request";
    }

    std::string_view editorAssetCatalogDiagnosticSeverityName(
        EditorAssetCatalogDiagnosticSeverity severity) noexcept {
        switch (severity) {
        case EditorAssetCatalogDiagnosticSeverity::Info:
            return "info";
        case EditorAssetCatalogDiagnosticSeverity::Warning:
            return "warning";
        case EditorAssetCatalogDiagnosticSeverity::Error:
            return "error";
        }
        return "info";
    }

    bool EditorAssetCatalogSnapshot::succeeded() const noexcept {
        return std::ranges::none_of(
            diagnostics, [](const EditorAssetCatalogDiagnostic& diagnostic) {
                return diagnostic.severity == EditorAssetCatalogDiagnosticSeverity::Error;
            });
    }

    std::vector<EditorAssetCatalogResolvedSourceRoot>
    resolveEditorAssetCatalogSourceRoots(const EditorAssetCatalogSnapshot& snapshot) {
        std::vector<EditorAssetCatalogResolvedSourceRoot> roots;
        roots.reserve(snapshot.project.assetSourceRoots.size());

        const std::filesystem::path projectDirectory = projectDirectoryFor(snapshot.projectFile);
        for (const asharia::project::AssetSourceRootDesc& root :
             snapshot.project.assetSourceRoots) {
            std::filesystem::path resolvedDirectory = sourceRootPath(projectDirectory, root);
            resolvedDirectory.make_preferred();
            roots.push_back(EditorAssetCatalogResolvedSourceRoot{
                .matched = true,
                .rootName = root.rootName,
                .sourcePathPrefix = root.sourcePathPrefix,
                .directory = std::filesystem::path{root.directory},
                .resolvedDirectory = std::move(resolvedDirectory),
            });
        }
        return roots;
    }

    EditorAssetCatalogResolvedSourceRoot
    resolveEditorAssetCatalogSourceRootForSourcePath(const EditorAssetCatalogSnapshot& snapshot,
                                                     std::string_view sourcePath) {
        const asharia::project::AssetSourceRootDesc* bestRoot = nullptr;
        std::size_t bestPrefixSize = 0U;
        for (const asharia::project::AssetSourceRootDesc& root :
             snapshot.project.assetSourceRoots) {
            if (!sourcePathHasPrefix(sourcePath, root.sourcePathPrefix) ||
                root.sourcePathPrefix.size() < bestPrefixSize) {
                continue;
            }
            bestRoot = &root;
            bestPrefixSize = root.sourcePathPrefix.size();
        }
        if (bestRoot == nullptr) {
            return {};
        }

        std::filesystem::path resolvedDirectory =
            sourceRootPath(projectDirectoryFor(snapshot.projectFile), *bestRoot);
        resolvedDirectory.make_preferred();
        return EditorAssetCatalogResolvedSourceRoot{
            .matched = true,
            .rootName = bestRoot->rootName,
            .sourcePathPrefix = bestRoot->sourcePathPrefix,
            .directory = std::filesystem::path{bestRoot->directory},
            .resolvedDirectory = std::move(resolvedDirectory),
        };
    }

    std::string_view
    editorAssetCatalogNavigationNodeKindName(EditorAssetCatalogNavigationNodeKind kind) noexcept {
        switch (kind) {
        case EditorAssetCatalogNavigationNodeKind::SourceRoot:
            return "source-root";
        case EditorAssetCatalogNavigationNodeKind::Folder:
            return "folder";
        case EditorAssetCatalogNavigationNodeKind::Asset:
            return "asset";
        case EditorAssetCatalogNavigationNodeKind::SubAsset:
            return "sub-asset";
        }
        return "asset";
    }

    std::vector<EditorAssetCatalogNavigationNode>
    makeEditorAssetCatalogNavigationNodes(const EditorAssetCatalogSnapshot& snapshot) {
        std::vector<EditorAssetCatalogNavigationNode> nodes;
        const std::vector<EditorAssetCatalogResolvedSourceRoot> sourceRoots =
            resolveEditorAssetCatalogSourceRoots(snapshot);
        nodes.reserve(sourceRoots.size() + snapshot.catalogView.entries.size());

        for (std::size_t sourceRootIndex = 0U; sourceRootIndex < sourceRoots.size();
             ++sourceRootIndex) {
            const EditorAssetCatalogResolvedSourceRoot& sourceRoot = sourceRoots[sourceRootIndex];
            const std::string displayName =
                sourceRoot.rootName.empty() ? sourceRoot.sourcePathPrefix : sourceRoot.rootName;
            nodes.push_back(EditorAssetCatalogNavigationNode{
                .kind = EditorAssetCatalogNavigationNodeKind::SourceRoot,
                .key = sourceRootNavigationKey(sourceRootIndex),
                .parentKey = {},
                .displayName =
                    displayName.empty() ? pathText(sourceRoot.resolvedDirectory) : displayName,
                .scopePath = sourceRoot.sourcePathPrefix,
                .sourcePath = {},
                .sourceRootName = sourceRoot.rootName,
                .sourceRootPrefix = sourceRoot.sourcePathPrefix,
                .sourceRootDirectory = sourceRoot.resolvedDirectory,
                .guidText = {},
                .stableId = {},
                .assetTypeName = {},
                .importerName = {},
                .extension = {},
                .importProfileName = {},
                .assetRoleName = {},
                .subAssetCount = 0U,
                .productState = asharia::asset::AssetCatalogProductState::NotTracked,
            });
        }

        for (const asharia::asset::AssetCatalogViewEntry& entry : snapshot.catalogView.entries) {
            const std::size_t sourceRootIndex =
                sourceRootIndexForSourcePath(snapshot, entry.sourcePath);
            const bool hasSourceRoot = sourceRootIndex < sourceRoots.size();
            const EditorAssetCatalogResolvedSourceRoot sourceRoot =
                hasSourceRoot
                    ? sourceRoots[sourceRootIndex]
                    : resolveEditorAssetCatalogSourceRootForSourcePath(snapshot, entry.sourcePath);
            const std::string_view folderScope = sourcePathDirectory(entry.sourcePath);
            if (hasSourceRoot && !folderScope.empty()) {
                appendNavigationFolderNodes(nodes, sourceRoot, sourceRootIndex, folderScope);
            }

            const std::string assetKey = assetNavigationKey(entry.sourcePath);
            nodes.push_back(EditorAssetCatalogNavigationNode{
                .kind = EditorAssetCatalogNavigationNodeKind::Asset,
                .key = assetKey,
                .parentKey = hasSourceRoot
                                 ? navigationParentKeyForAsset(nodes, sourceRoot, sourceRootIndex,
                                                               entry.sourcePath)
                                 : std::string{},
                .displayName = entry.displayName.empty()
                                   ? std::string{sourcePathFileName(entry.sourcePath)}
                                   : entry.displayName,
                .scopePath = {},
                .sourcePath = entry.sourcePath,
                .sourceRootName = sourceRoot.rootName,
                .sourceRootPrefix = sourceRoot.sourcePathPrefix,
                .sourceRootDirectory = sourceRoot.resolvedDirectory,
                .guidText = entry.guidText,
                .stableId = {},
                .assetTypeName = entry.assetTypeName,
                .importerName = entry.importerName,
                .extension = entry.extension,
                .importProfileName = entry.importProfileName,
                .assetRoleName = entry.assetRoleName,
                .subAssetCount = entry.subAssets.size(),
                .productState = entry.productState,
            });

            for (const asharia::asset::AssetCatalogSubAssetViewEntry& subAsset : entry.subAssets) {
                nodes.push_back(EditorAssetCatalogNavigationNode{
                    .kind = EditorAssetCatalogNavigationNodeKind::SubAsset,
                    .key = subAssetNavigationKey(entry.sourcePath, subAsset.stableId),
                    .parentKey = assetKey,
                    .displayName =
                        subAsset.displayName.empty() ? subAsset.stableId : subAsset.displayName,
                    .scopePath = {},
                    .sourcePath = entry.sourcePath,
                    .sourceRootName = sourceRoot.rootName,
                    .sourceRootPrefix = sourceRoot.sourcePathPrefix,
                    .sourceRootDirectory = sourceRoot.resolvedDirectory,
                    .guidText = entry.guidText,
                    .stableId = subAsset.stableId,
                    .assetTypeName = entry.assetTypeName,
                    .importerName = entry.importerName,
                    .extension = entry.extension,
                    .importProfileName = entry.importProfileName,
                    .assetRoleName = subAsset.assetRoleName,
                    .subAssetCount = 0U,
                    .productState = entry.productState,
                });
            }
        }

        return nodes;
    }

    EditorAssetCatalogSnapshot
    loadEditorAssetCatalogSnapshot(const EditorAssetCatalogSnapshotRequest& request) {
        const std::filesystem::path resolvedProjectFile = projectFilePathFor(request.projectFile);
        EditorAssetCatalogSnapshot snapshot{
            .projectFile = resolvedProjectFile,
            .productManifestFile = {},
            .targetProfile = request.targetProfile,
            .project = {},
            .catalogView = {},
            .diagnostics = {},
        };

        if (!request) {
            addDiagnostic(snapshot, EditorAssetCatalogDiagnosticCode::InvalidRequest,
                          EditorAssetCatalogDiagnosticSeverity::Error, {}, resolvedProjectFile,
                          "Editor asset catalog snapshot request requires a project file and "
                          "target profile.");
            return snapshot;
        }

        auto project = asharia::project::readAshariaProjectDescriptorFile(resolvedProjectFile);
        if (!project) {
            addDiagnostic(snapshot, EditorAssetCatalogDiagnosticCode::ProjectDescriptorReadFailed,
                          EditorAssetCatalogDiagnosticSeverity::Error, {}, resolvedProjectFile,
                          "Editor asset catalog snapshot could not read project descriptor: " +
                              project.error().message);
            return snapshot;
        }
        snapshot.project = std::move(*project);

        const std::filesystem::path projectDirectory = projectDirectoryFor(resolvedProjectFile);
        snapshot.productManifestFile =
            productManifestFileFor(request, projectDirectory, snapshot.project);
        const asharia::asset::AssetProductManifestDocument productManifest =
            readProductManifest(snapshot.productManifestFile, snapshot);
        asharia::asset::AssetCatalog catalog;
        std::vector<asharia::asset::AssetProductKey> expectedProductKeys;
        std::vector<asharia::asset::AssetCatalogSourceFacet> sourceFacets;
        std::vector<asharia::asset::AssetCatalogViewEntry> defaultRows;
        for (const asharia::project::AssetSourceRootDesc& root :
             snapshot.project.assetSourceRoots) {
            appendRootSnapshot(projectDirectory, root, productManifest, request.targetProfile,
                               catalog, expectedProductKeys, sourceFacets, defaultRows, snapshot);
        }

        snapshot.catalogView = asharia::asset::buildAssetCatalogView(
            catalog, productManifest.products,
            asharia::asset::AssetCatalogViewOptions{.requireProducts = true,
                                                    .expectedProductKeys = expectedProductKeys,
                                                    .sourceFacets = sourceFacets});
        for (asharia::asset::AssetCatalogViewEntry& defaultRow : defaultRows) {
            if (catalogViewContainsSourcePath(snapshot.catalogView.entries,
                                              defaultRow.sourcePath)) {
                continue;
            }
            snapshot.catalogView.entries.push_back(std::move(defaultRow));
        }
        sortCatalogViewEntries(snapshot.catalogView.entries);
        appendCatalogViewDiagnostics(snapshot, snapshot.catalogView.diagnostics);
        for (const asharia::asset::AssetCatalogViewEntry& entry : snapshot.catalogView.entries) {
            appendCatalogViewDiagnostics(snapshot, entry.diagnostics);
        }
        return snapshot;
    }

    std::filesystem::path
    resolveEditorAssetCatalogSourceFilePath(const EditorAssetCatalogSnapshot& snapshot,
                                            std::string_view sourcePath) {
        const EditorAssetCatalogResolvedSourceRoot sourceRoot =
            resolveEditorAssetCatalogSourceRootForSourcePath(snapshot, sourcePath);
        if (!sourceRoot.matched) {
            return {};
        }

        const std::string_view relativeSourcePath =
            sourcePathWithoutPrefix(sourcePath, sourceRoot.sourcePathPrefix);
        std::filesystem::path sourceFile = sourceRoot.resolvedDirectory;
        if (!relativeSourcePath.empty()) {
            sourceFile /= pathFromUtf8(relativeSourcePath);
        }
        sourceFile.make_preferred();
        return sourceFile;
    }

    std::filesystem::path
    resolveEditorAssetCatalogMetadataFilePath(const EditorAssetCatalogSnapshot& snapshot,
                                              std::string_view sourcePath) {
        std::filesystem::path metadataFile =
            resolveEditorAssetCatalogSourceFilePath(snapshot, sourcePath);
        if (metadataFile.empty()) {
            return {};
        }
        metadataFile += std::string{asharia::asset::kAssetMetadataSidecarSuffix};
        metadataFile.make_preferred();
        return metadataFile;
    }

    asharia::asset::AssetCatalogView makeEditorAssetBrowserFixtureCatalogView() {
        constexpr std::string_view kMaterialTypeName = "com.asharia.asset.Material";
        constexpr std::string_view kMeshTypeName = "com.asharia.asset.Mesh";
        constexpr std::string_view kShaderTypeName = "com.asharia.asset.Shader";
        constexpr std::string_view kTextureTypeName = "com.asharia.asset.Texture";
        constexpr std::string_view kTextTypeName = "com.asharia.asset.Text";
        const asharia::asset::SourceAssetRecord material = fixtureSourceRecord(FixtureSourceDesc{
            .guidText = "b8373128-8e46-44e1-a5a4-df4c2ef9d2ad",
            .assetTypeName = kMaterialTypeName,
            .sourcePath = "Assets/Materials/brushed_metal.amat",
            .importerName = "asharia.material",
            .sourceHash = 0x1001ULL,
            .settingsHash = 0x2001ULL,
        });
        const asharia::asset::SourceAssetRecord shader = fixtureSourceRecord(FixtureSourceDesc{
            .guidText = "13a10d4b-6987-48d1-ad27-ae4055e5a936",
            .assetTypeName = kShaderTypeName,
            .sourcePath = "Assets/Shaders/grid.slang",
            .importerName = "asharia.shader-slang",
            .sourceHash = 0x1002ULL,
            .settingsHash = 0x2002ULL,
        });
        asharia::asset::SourceAssetRecord staleMesh = fixtureSourceRecord(FixtureSourceDesc{
            .guidText = "1135c477-65aa-4d44-92f1-f208fc6142ad",
            .assetTypeName = kMeshTypeName,
            .sourcePath = "Assets/Meshes/cube.mesh",
            .importerName = "asharia.mesh-placeholder",
            .sourceHash = 0x1003ULL,
            .settingsHash = 0x2003ULL,
        });
        const asharia::asset::SourceAssetRecord texture = fixtureSourceRecord(FixtureSourceDesc{
            .guidText = "cd9c0f3d-20e2-4028-a3e9-c3f42d3fd515",
            .assetTypeName = kTextureTypeName,
            .sourcePath = "Assets/Textures/checker.png",
            .importerName = "asharia.texture-placeholder",
            .sourceHash = 0x1004ULL,
            .settingsHash = 0x2004ULL,
        });
        const asharia::asset::SourceAssetRecord spriteSheet = fixtureSourceRecord(FixtureSourceDesc{
            .guidText = "fd2e5880-dffb-4d27-b5d1-0c249005023a",
            .assetTypeName = kTextureTypeName,
            .sourcePath = "Assets/Textures/hero_sprites.png",
            .importerName = "asharia.texture-placeholder",
            .sourceHash = 0x1006ULL,
            .settingsHash = 0x2006ULL,
        });
        const asharia::asset::SourceAssetRecord skybox = fixtureSourceRecord(FixtureSourceDesc{
            .guidText = "3b2cef92-bc92-43be-8e7d-a74a89c1d502",
            .assetTypeName = kTextureTypeName,
            .sourcePath = "Assets/Textures/studio_skybox.hdr",
            .importerName = "asharia.texture-placeholder",
            .sourceHash = 0x1007ULL,
            .settingsHash = 0x2007ULL,
        });
        const asharia::asset::SourceAssetRecord textureCube = fixtureSourceRecord(FixtureSourceDesc{
            .guidText = "38fd0dc8-55ee-44c9-b12f-0179e0039c6b",
            .assetTypeName = kTextureTypeName,
            .sourcePath = "Assets/Textures/studio_probe_cube.ktx2",
            .importerName = "asharia.texture-placeholder",
            .sourceHash = 0x1008ULL,
            .settingsHash = 0x2008ULL,
        });
        const asharia::asset::SourceAssetRecord note = fixtureSourceRecord(FixtureSourceDesc{
            .guidText = "f98f9d88-237f-4e8a-a4b6-9977d3a1fc2b",
            .assetTypeName = kTextTypeName,
            .sourcePath = "Assets/readme.md",
            .importerName = "asharia.text-placeholder",
            .sourceHash = 0x1005ULL,
            .settingsHash = 0x2005ULL,
        });

        asharia::asset::AssetCatalog catalog;
        if (!catalog.addSource(shader) || !catalog.addSource(note) || !catalog.addSource(texture) ||
            !catalog.addSource(spriteSheet) || !catalog.addSource(skybox) ||
            !catalog.addSource(textureCube) || !catalog.addSource(material) ||
            !catalog.addSource(staleMesh)) {
            return {};
        }

        const std::uint64_t targetProfile =
            asharia::asset::makeAssetTargetProfileHash("editor-preview");
        asharia::asset::SourceAssetRecord oldMesh = staleMesh;
        oldMesh.sourceHash ^= 0x40ULL;
        const std::array expectedProductKeys{
            asharia::asset::makeAssetProductKey(material, 0x3001ULL, targetProfile),
            asharia::asset::makeAssetProductKey(shader, 0x3002ULL, targetProfile),
            asharia::asset::makeAssetProductKey(staleMesh, 0x3003ULL, targetProfile),
            asharia::asset::makeAssetProductKey(texture, 0x3004ULL, targetProfile),
            asharia::asset::makeAssetProductKey(note, 0x3005ULL, targetProfile),
            asharia::asset::makeAssetProductKey(spriteSheet, 0x3006ULL, targetProfile),
            asharia::asset::makeAssetProductKey(skybox, 0x3007ULL, targetProfile),
            asharia::asset::makeAssetProductKey(textureCube, 0x3008ULL, targetProfile),
        };
        const std::array<asharia::asset::AssetProductRecord, 7> products{
            fixtureProductRecord(material, 0x3001ULL, targetProfile,
                                 "materials/brushed_metal.product", 512),
            fixtureProductRecord(shader, 0x3002ULL, targetProfile, "shaders/grid.product", 256),
            fixtureProductRecord(texture, 0x3004ULL, targetProfile, "textures/checker.product",
                                 1024),
            fixtureProductRecord(spriteSheet, 0x3006ULL, targetProfile,
                                 "textures/hero_sprites.product", 4096),
            fixtureProductRecord(skybox, 0x3007ULL, targetProfile, "textures/studio_skybox.product",
                                 8192),
            fixtureProductRecord(textureCube, 0x3008ULL, targetProfile,
                                 "textures/studio_probe_cube.product", 8192),
            fixtureProductRecord(oldMesh, 0x3003ULL, targetProfile, "meshes/cube.old.product",
                                 2048),
        };
        const std::array textureSettings{
            asharia::asset::AssetImportSetting{
                .key = std::string{asharia::asset::kTextureImportProfileSettingKey},
                .value = std::string{asharia::asset::kTextureImportProfileTexture2D},
            },
        };
        const std::array spriteSheetSettings{
            asharia::asset::AssetImportSetting{
                .key = std::string{asharia::asset::kTextureImportProfileSettingKey},
                .value = std::string{asharia::asset::kTextureImportProfileSpriteSheet},
            },
            asharia::asset::AssetImportSetting{.key = "texture.subAsset.0.id",
                                               .value = "hero-idle-0"},
            asharia::asset::AssetImportSetting{.key = "texture.subAsset.0.name",
                                               .value = "Hero Idle 0"},
            asharia::asset::AssetImportSetting{.key = "texture.subAsset.1.id",
                                               .value = "hero-run-0"},
            asharia::asset::AssetImportSetting{.key = "texture.subAsset.1.name",
                                               .value = "Hero Run 0"},
        };
        const std::array skyboxSettings{
            asharia::asset::AssetImportSetting{
                .key = std::string{asharia::asset::kTextureImportProfileSettingKey},
                .value = std::string{asharia::asset::kTextureImportProfileSkybox},
            },
        };
        const std::array textureCubeSettings{
            asharia::asset::AssetImportSetting{
                .key = std::string{asharia::asset::kTextureImportProfileSettingKey},
                .value = std::string{asharia::asset::kTextureImportProfileTextureCube},
            },
        };
        const std::array sourceFacets{
            asharia::asset::makeTextureImportCatalogSourceFacet(texture, textureSettings),
            asharia::asset::makeTextureImportCatalogSourceFacet(spriteSheet, spriteSheetSettings),
            asharia::asset::makeTextureImportCatalogSourceFacet(skybox, skyboxSettings),
            asharia::asset::makeTextureImportCatalogSourceFacet(textureCube, textureCubeSettings),
        };

        return asharia::asset::buildAssetCatalogView(
            catalog, products,
            asharia::asset::AssetCatalogViewOptions{.requireProducts = true,
                                                    .expectedProductKeys = expectedProductKeys,
                                                    .sourceFacets = sourceFacets});
    }

} // namespace asharia::editor
