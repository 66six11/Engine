#include "asharia/editor_content/asset_catalog_snapshot.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <set>
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

        struct IngestionTotals {
            std::uint64_t sourceFiles{};
            std::uint64_t sourceBytes{};
        };

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

        [[nodiscard]] bool addDiagnosticBounded(EditorAssetCatalogSnapshot& snapshot,
                                                std::uint64_t maxDiagnostics,
                                                EditorAssetCatalogDiagnosticCode code,
                                                EditorAssetCatalogDiagnosticSeverity severity,
                                                std::string sourcePath, std::filesystem::path path,
                                                std::string message) {
            if (snapshot.diagnostics.size() >= maxDiagnostics) {
                snapshot.diagnostics.back() = EditorAssetCatalogDiagnostic{
                    .code = EditorAssetCatalogDiagnosticCode::LimitExceeded,
                    .severity = EditorAssetCatalogDiagnosticSeverity::Error,
                    .sourcePath = {},
                    .path = {},
                    .message = "Editor asset catalog snapshot exceeded the diagnostic limit.",
                };
                return false;
            }
            addDiagnostic(snapshot, code, severity, std::move(sourcePath), std::move(path),
                          std::move(message));
            return true;
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
                                         std::set<std::string, std::less<>>& nodeKeys,
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
                    if (nodeKeys.emplace(nodeKey).second) {
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
        navigationParentKeyForAsset(const std::set<std::string, std::less<>>& nodeKeys,
                                    const EditorAssetCatalogResolvedSourceRoot& sourceRoot,
                                    std::size_t sourceRootIndex, std::string_view sourcePath) {
            const std::string_view folderScope = sourcePathDirectory(sourcePath);
            if (folderScope.empty() || folderScope == sourceRoot.sourcePathPrefix) {
                return sourceRootNavigationKey(sourceRootIndex);
            }
            std::string folderKey = folderNavigationKey(folderScope);
            if (nodeKeys.contains(folderKey)) {
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
                            EditorAssetCatalogSnapshot& snapshot, std::uint64_t maxDiagnostics) {
            if (productManifestFile.empty()) {
                return {};
            }

            auto manifest = asharia::asset::readAssetProductManifestFile(productManifestFile);
            if (!manifest) {
                (void)addDiagnosticBounded(
                    snapshot, maxDiagnostics,
                    EditorAssetCatalogDiagnosticCode::ProductManifestReadFailed,
                    EditorAssetCatalogDiagnosticSeverity::Warning, {}, productManifestFile,
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
            case asharia::asset::AssetSourceScanDiagnosticCode::LimitExceeded:
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
            std::span<const asharia::asset::AssetSourceScanDiagnostic> diagnostics,
            std::uint64_t maxDiagnostics) {
            for (const asharia::asset::AssetSourceScanDiagnostic& diagnostic : diagnostics) {
                if (!addDiagnosticBounded(
                        snapshot, maxDiagnostics, EditorAssetCatalogDiagnosticCode::SourceScan,
                        severityForScanDiagnostic(diagnostic.code), diagnostic.sourcePath,
                        diagnostic.sourceFilePath, diagnostic.message)) {
                    break;
                }
            }
        }

        void appendDiscoveryDiagnostics(
            EditorAssetCatalogSnapshot& snapshot,
            std::span<const asharia::asset::AssetSourceDiscoveryDiagnostic> diagnostics,
            std::uint64_t maxDiagnostics) {
            for (const asharia::asset::AssetSourceDiscoveryDiagnostic& diagnostic : diagnostics) {
                if (!addDiagnosticBounded(
                        snapshot, maxDiagnostics, EditorAssetCatalogDiagnosticCode::SourceDiscovery,
                        EditorAssetCatalogDiagnosticSeverity::Error, diagnostic.sourcePath,
                        diagnostic.metadataPath, diagnostic.message)) {
                    break;
                }
            }
        }

        void appendSnapshotDiagnostics(
            EditorAssetCatalogSnapshot& snapshot,
            std::span<const asharia::asset::AssetSourceSnapshotDiagnostic> diagnostics,
            std::uint64_t maxDiagnostics) {
            for (const asharia::asset::AssetSourceSnapshotDiagnostic& diagnostic : diagnostics) {
                if (!addDiagnosticBounded(
                        snapshot, maxDiagnostics, EditorAssetCatalogDiagnosticCode::SourceSnapshot,
                        EditorAssetCatalogDiagnosticSeverity::Error, diagnostic.sourcePath,
                        diagnostic.sourceFilePath, diagnostic.message)) {
                    break;
                }
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
            std::span<const asharia::asset::AssetImportPlanDiagnostic> diagnostics,
            std::uint64_t maxDiagnostics) {
            for (const asharia::asset::AssetImportPlanDiagnostic& diagnostic : diagnostics) {
                if (!addDiagnosticBounded(snapshot, maxDiagnostics,
                                          EditorAssetCatalogDiagnosticCode::ImportPlanning,
                                          severityForImportPlanDiagnostic(diagnostic.severity),
                                          diagnostic.sourcePath, {}, diagnostic.message)) {
                    break;
                }
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
            std::span<const asharia::asset::AssetCatalogDiagnostic> diagnostics,
            std::uint64_t maxDiagnostics) {
            for (const asharia::asset::AssetCatalogDiagnostic& diagnostic : diagnostics) {
                if (!addDiagnosticBounded(snapshot, maxDiagnostics,
                                          EditorAssetCatalogDiagnosticCode::CatalogView,
                                          severityForCatalogDiagnostic(diagnostic),
                                          diagnostic.sourcePath, {}, diagnostic.message)) {
                    break;
                }
            }
        }

        void mergeSource(asharia::asset::AssetCatalog& catalog,
                         EditorAssetCatalogSnapshot& snapshot,
                         asharia::asset::SourceAssetRecord source, std::uint64_t maxDiagnostics) {
            const std::string sourcePath = source.sourcePath;
            auto added = catalog.addSource(std::move(source));
            if (!added) {
                (void)addDiagnosticBounded(
                    snapshot, maxDiagnostics, EditorAssetCatalogDiagnosticCode::CatalogMerge,
                    EditorAssetCatalogDiagnosticSeverity::Error, sourcePath, {},
                    "Editor asset catalog snapshot could not merge source: " +
                        added.error().message);
            }
        }

        void mergeDiscoveredSources(
            asharia::asset::AssetCatalog& catalog, EditorAssetCatalogSnapshot& snapshot,
            std::span<const asharia::asset::DiscoveredSourceAsset> discoveredSources,
            std::uint64_t maxDiagnostics) {
            for (const asharia::asset::DiscoveredSourceAsset& discovered : discoveredSources) {
                mergeSource(catalog, snapshot, discovered.source, maxDiagnostics);
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
                                   std::set<std::string, std::less<>>& defaultRowSourcePaths,
                                   std::string_view sourcePath, std::string_view message) {
            if (sourcePath.empty() || !defaultRowSourcePaths.emplace(sourcePath).second) {
                return;
            }
            defaultRows.push_back(makeDefaultAssetEntry(sourcePath, message));
        }

        void appendMissingMetadataDefaultRows(
            std::vector<asharia::asset::AssetCatalogViewEntry>& defaultRows,
            std::set<std::string, std::less<>>& defaultRowSourcePaths,
            std::span<const asharia::asset::AssetSourceScanDiagnostic> diagnostics) {
            for (const asharia::asset::AssetSourceScanDiagnostic& diagnostic : diagnostics) {
                if (diagnostic.code !=
                    asharia::asset::AssetSourceScanDiagnosticCode::MissingMetadata) {
                    continue;
                }
                appendDefaultAssetRow(defaultRows, defaultRowSourcePaths, diagnostic.sourcePath,
                                      "Asset source has no metadata sidecar; it is visible as a "
                                      "default asset and no product will be generated.");
            }
        }

        void appendUndiscoveredSourceDefaultRows(
            std::vector<asharia::asset::AssetCatalogViewEntry>& defaultRows,
            std::set<std::string, std::less<>>& defaultRowSourcePaths,
            std::span<const asharia::asset::AssetSourceScanEntry> scanEntries,
            std::span<const asharia::asset::DiscoveredSourceAsset> discoveredSources) {
            std::set<std::string_view, std::less<>> discoveredSourcePaths;
            for (const asharia::asset::DiscoveredSourceAsset& discovered : discoveredSources) {
                discoveredSourcePaths.emplace(discovered.source.sourcePath);
            }
            for (const asharia::asset::AssetSourceScanEntry& entry : scanEntries) {
                if (discoveredSourcePaths.contains(entry.sourcePath)) {
                    continue;
                }
                appendDefaultAssetRow(defaultRows, defaultRowSourcePaths, entry.sourcePath,
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

        void appendRootSnapshot(const std::filesystem::path& sourceRootDirectory,
                                const asharia::project::AssetSourceRootDesc& root,
                                const asharia::asset::AssetProductManifestDocument& productManifest,
                                const std::string& targetProfile,
                                asharia::asset::AssetCatalog& catalog,
                                std::vector<asharia::asset::AssetProductKey>& expectedProductKeys,
                                std::vector<asharia::asset::AssetCatalogSourceFacet>& sourceFacets,
                                std::vector<asharia::asset::AssetCatalogViewEntry>& defaultRows,
                                std::set<std::string, std::less<>>& defaultRowSourcePaths,
                                const EditorAssetCatalogSnapshotRequest& request,
                                IngestionTotals& totals, EditorAssetCatalogSnapshot& snapshot) {
            const asharia::asset::AssetSourceScanRequest scanRequest{
                .sourceRoot = sourceRootDirectory,
                .sourcePathPrefix = root.sourcePathPrefix,
                .metadataSuffix = std::string{asharia::asset::kAssetMetadataSidecarSuffix},
                .ignoredDirectoryNames = snapshot.project.assetDiscovery.ignoredDirectoryNames,
                // Check the aggregate after this bounded root scan. Passing only the remaining
                // count would reject an empty trailing root when prior roots use the budget
                // exactly.
                .maxDiscoveredFiles = request.maxSourceFiles,
            };

            const asharia::asset::AssetSourceScanResult scan =
                asharia::asset::scanAssetSourceTree(scanRequest);
            if (scan.discoveredFileCount > request.maxSourceFiles - totals.sourceFiles) {
                (void)addDiagnosticBounded(
                    snapshot, request.maxDiagnostics,
                    EditorAssetCatalogDiagnosticCode::LimitExceeded,
                    EditorAssetCatalogDiagnosticSeverity::Error, {}, {},
                    "Editor asset catalog snapshot exceeded the aggregate source file limit.");
                return;
            }
            totals.sourceFiles += scan.discoveredFileCount;
            appendScanDiagnostics(snapshot, scan.diagnostics, request.maxDiagnostics);
            if (std::ranges::any_of(scan.diagnostics, [](const auto& diagnostic) {
                    return diagnostic.code ==
                           asharia::asset::AssetSourceScanDiagnosticCode::LimitExceeded;
                })) {
                (void)addDiagnosticBounded(
                    snapshot, request.maxDiagnostics,
                    EditorAssetCatalogDiagnosticCode::LimitExceeded,
                    EditorAssetCatalogDiagnosticSeverity::Error, {}, {},
                    "Editor asset catalog snapshot exceeded the aggregate source file limit.");
            }
            appendMissingMetadataDefaultRows(defaultRows, defaultRowSourcePaths, scan.diagnostics);
            if (hasFatalScanDiagnostics(scan.diagnostics)) {
                return;
            }

            const std::vector<asharia::asset::AssetSourceDiscoveryEntry> discoveryEntries =
                makeDiscoveryEntries(scan.entries);
            const asharia::asset::AssetSourceDiscoveryResult discovery =
                asharia::asset::discoverAssetSources(discoveryEntries);
            appendDiscoveryDiagnostics(snapshot, discovery.diagnostics, request.maxDiagnostics);
            mergeDiscoveredSources(catalog, snapshot, discovery.manifest.records,
                                   request.maxDiagnostics);
            appendUndiscoveredSourceDefaultRows(defaultRows, defaultRowSourcePaths, scan.entries,
                                                discovery.manifest.records);

            const std::vector<asharia::asset::AssetSourceSnapshotEntry> snapshotEntries =
                makeSnapshotEntries(scan.entries);
            const asharia::asset::AssetSourceSnapshotResult sourceSnapshot =
                asharia::asset::snapshotAssetSourceFiles(
                    snapshotEntries, request.maxTotalSourceBytes - totals.sourceBytes);
            totals.sourceBytes += sourceSnapshot.bytesHashed;
            appendSnapshotDiagnostics(snapshot, sourceSnapshot.diagnostics, request.maxDiagnostics);
            if (std::ranges::any_of(sourceSnapshot.diagnostics, [](const auto& diagnostic) {
                    return diagnostic.code ==
                           asharia::asset::AssetSourceSnapshotDiagnosticCode::ByteLimitExceeded;
                })) {
                (void)addDiagnosticBounded(
                    snapshot, request.maxDiagnostics,
                    EditorAssetCatalogDiagnosticCode::LimitExceeded,
                    EditorAssetCatalogDiagnosticSeverity::Error, {}, {},
                    "Editor asset catalog snapshot exceeded the aggregate source byte limit "
                    "while hashing source files.");
                return;
            }

            const asharia::asset::AssetImportPlanResult plan = asharia::asset::planAssetImports(
                discovery.manifest.records, sourceSnapshot.snapshots, productManifest,
                targetProfile,
                asharia::asset::AssetImportPlanOptions{
                    .toolVersions = {},
                    .toolFingerprintResolver = {},
                    .toolDependencyPolicy =
                        asharia::asset::AssetImportToolDependencyPolicy::DeclaredOnly,
                });
            appendImportPlanDiagnostics(snapshot, plan.diagnostics, request.maxDiagnostics);
            appendTextureProfileFacets(sourceFacets, discovery.manifest.records);
            appendExpectedProductKeys(expectedProductKeys, plan);
        }

    } // namespace

    EditorAssetCatalogSnapshotRequest
    makeEditorAssetCatalogSnapshotRequest(const EditorAssetCatalogSnapshot& snapshot) {
        return EditorAssetCatalogSnapshotRequest{
            .projectFile = snapshot.projectFile,
            .productManifestFile = snapshot.productManifestFile,
            .targetProfile = snapshot.targetProfile,
        };
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
        case EditorAssetCatalogDiagnosticCode::LimitExceeded:
            return "limit-exceeded";
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
            auto containedDirectory = asharia::project::resolveContainedProjectPath(
                projectDirectory, std::filesystem::path{root.directory},
                "asset source root '" + root.rootName + "'");
            if (!containedDirectory) {
                roots.push_back(EditorAssetCatalogResolvedSourceRoot{});
                continue;
            }
            std::filesystem::path resolvedDirectory = std::move(*containedDirectory);
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

        auto containedDirectory = asharia::project::resolveContainedProjectPath(
            projectDirectoryFor(snapshot.projectFile), std::filesystem::path{bestRoot->directory},
            "asset source root '" + bestRoot->rootName + "'");
        if (!containedDirectory) {
            return {};
        }
        std::filesystem::path resolvedDirectory = std::move(*containedDirectory);
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
        std::set<std::string, std::less<>> nodeKeys;
        const std::vector<EditorAssetCatalogResolvedSourceRoot> sourceRoots =
            resolveEditorAssetCatalogSourceRoots(snapshot);
        nodes.reserve(sourceRoots.size() + snapshot.catalogView.entries.size());

        for (std::size_t sourceRootIndex = 0U; sourceRootIndex < sourceRoots.size();
             ++sourceRootIndex) {
            const EditorAssetCatalogResolvedSourceRoot& sourceRoot = sourceRoots[sourceRootIndex];
            const std::string displayName =
                sourceRoot.rootName.empty() ? sourceRoot.sourcePathPrefix : sourceRoot.rootName;
            std::string sourceRootKey = sourceRootNavigationKey(sourceRootIndex);
            nodeKeys.emplace(sourceRootKey);
            nodes.push_back(EditorAssetCatalogNavigationNode{
                .kind = EditorAssetCatalogNavigationNodeKind::SourceRoot,
                .key = std::move(sourceRootKey),
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
                appendNavigationFolderNodes(nodes, nodeKeys, sourceRoot, sourceRootIndex,
                                            folderScope);
            }

            const std::string assetKey = assetNavigationKey(entry.sourcePath);
            nodeKeys.emplace(assetKey);
            nodes.push_back(EditorAssetCatalogNavigationNode{
                .kind = EditorAssetCatalogNavigationNodeKind::Asset,
                .key = assetKey,
                .parentKey = hasSourceRoot
                                 ? navigationParentKeyForAsset(nodeKeys, sourceRoot,
                                                               sourceRootIndex, entry.sourcePath)
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
                std::string subAssetKey =
                    subAssetNavigationKey(entry.sourcePath, subAsset.stableId);
                nodeKeys.emplace(subAssetKey);
                nodes.push_back(EditorAssetCatalogNavigationNode{
                    .kind = EditorAssetCatalogNavigationNodeKind::SubAsset,
                    .key = std::move(subAssetKey),
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
        if (snapshot.project.assetSourceRoots.size() > request.maxSourceRoots) {
            addDiagnostic(snapshot, EditorAssetCatalogDiagnosticCode::LimitExceeded,
                          EditorAssetCatalogDiagnosticSeverity::Error, {}, resolvedProjectFile,
                          "Editor asset catalog snapshot exceeded the source root limit.");
            return snapshot;
        }

        const std::filesystem::path projectDirectory = projectDirectoryFor(resolvedProjectFile);
        snapshot.productManifestFile =
            productManifestFileFor(request, projectDirectory, snapshot.project);
        const asharia::asset::AssetProductManifestDocument productManifest =
            readProductManifest(snapshot.productManifestFile, snapshot, request.maxDiagnostics);
        asharia::asset::AssetCatalog catalog;
        std::vector<asharia::asset::AssetProductKey> expectedProductKeys;
        std::vector<asharia::asset::AssetCatalogSourceFacet> sourceFacets;
        std::vector<asharia::asset::AssetCatalogViewEntry> defaultRows;
        std::set<std::string, std::less<>> defaultRowSourcePaths;
        IngestionTotals totals;
        for (const asharia::project::AssetSourceRootDesc& root :
             snapshot.project.assetSourceRoots) {
            auto sourceRoot = asharia::project::resolveContainedProjectPath(
                projectDirectory, std::filesystem::path{root.directory},
                "asset source root '" + root.rootName + "'");
            if (!sourceRoot) {
                (void)addDiagnosticBounded(
                    snapshot, request.maxDiagnostics, EditorAssetCatalogDiagnosticCode::SourceScan,
                    EditorAssetCatalogDiagnosticSeverity::Error, {},
                    sourceRootPath(projectDirectory, root), sourceRoot.error().message);
                continue;
            }
            appendRootSnapshot(*sourceRoot, root, productManifest, request.targetProfile, catalog,
                               expectedProductKeys, sourceFacets, defaultRows,
                               defaultRowSourcePaths, request, totals, snapshot);
            if (std::ranges::any_of(snapshot.diagnostics, [](const auto& diagnostic) {
                    return diagnostic.code == EditorAssetCatalogDiagnosticCode::LimitExceeded;
                })) {
                break;
            }
        }

        snapshot.catalogView = asharia::asset::buildAssetCatalogView(
            catalog, productManifest.products,
            asharia::asset::AssetCatalogViewOptions{.requireProducts = true,
                                                    .expectedProductKeys = expectedProductKeys,
                                                    .sourceFacets = sourceFacets});
        std::set<std::string, std::less<>> catalogSourcePaths;
        for (const asharia::asset::AssetCatalogViewEntry& entry : snapshot.catalogView.entries) {
            catalogSourcePaths.emplace(entry.sourcePath);
        }
        for (asharia::asset::AssetCatalogViewEntry& defaultRow : defaultRows) {
            if (catalogSourcePaths.contains(defaultRow.sourcePath)) {
                continue;
            }
            snapshot.catalogView.entries.push_back(std::move(defaultRow));
        }
        sortCatalogViewEntries(snapshot.catalogView.entries);
        appendCatalogViewDiagnostics(snapshot, snapshot.catalogView.diagnostics,
                                     request.maxDiagnostics);
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

} // namespace asharia::editor
