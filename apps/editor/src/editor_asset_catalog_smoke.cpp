#include "editor_asset_catalog_smoke.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "asharia/asset_core/asset_guid.hpp"
#include "asharia/asset_core/asset_metadata_io.hpp"
#include "asharia/asset_pipeline/asset_import_planning.hpp"
#include "asharia/asset_pipeline/asset_product_manifest_io.hpp"
#include "asharia/asset_pipeline/asset_source_scan.hpp"
#include "asharia/asset_pipeline/asset_source_snapshot.hpp"
#include "asharia/core/log.hpp"
#include "asharia/project/project_descriptor_io.hpp"

#include "editor_asset_catalog.hpp"
#include "editor_smoke.hpp"

namespace asharia::editor {
    namespace {

        constexpr std::string_view kTargetProfile = "editor-preview";
        constexpr std::string_view kGuidText = "5d3cdcbf-7396-40d0-b497-4fa2fe54f92a";
        constexpr std::string_view kAssetTypeName = "com.asharia.asset.Material";
        constexpr std::string_view kImporterName = "asharia.material";
        constexpr std::string_view kSourcePath = "Assets/Materials/brushed.amat";

        struct CatalogSourceFixtureDesc {
            std::string_view guidText;
            std::string_view assetTypeName;
            std::string_view importerName;
            std::string_view sourcePath;
            std::string_view relativePath;
            std::string_view text;
        };

        [[nodiscard]] std::filesystem::path smokeRoot() {
            std::error_code error;
            std::filesystem::path root = std::filesystem::temp_directory_path(error);
            if (error || root.empty()) {
                root = std::filesystem::current_path(error);
            }
            if (root.empty()) {
                root = ".";
            }
            static const std::string kRunId =
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
            return root / "Asharia" / "EditorAssetCatalogSnapshotSmoke" / kRunId;
        }

        [[nodiscard]] bool writeTextFile(const std::filesystem::path& path, std::string_view text) {
            std::ofstream output{path, std::ios::binary};
            if (!output) {
                asharia::logError("Editor asset catalog smoke could not open fixture file.");
                return false;
            }
            output << text;
            return static_cast<bool>(output);
        }

        [[nodiscard]] bool prepareDirectory(const std::filesystem::path& root) {
            std::error_code error;
            std::filesystem::remove_all(root, error);
            error.clear();
            std::filesystem::create_directories(root, error);
            if (error) {
                asharia::logError("Editor asset catalog smoke could not create temp root: " +
                                  error.message());
                return false;
            }
            return true;
        }

        [[nodiscard]] bool writeProjectDescriptorWithRoots(
            const std::filesystem::path& projectFile,
            std::span<const asharia::project::AssetSourceRootDesc> sourceRoots) {
            auto projectId =
                asharia::project::parseProjectId("2111839a-45b8-4030-91aa-2a10cc730b88");
            const asharia::project::AshariaProjectDescriptor project{
                .projectName = "Editor Catalog Smoke",
                .projectId = projectId ? *projectId : asharia::project::ProjectId{},
                .assetSourceRoots =
                    std::vector<asharia::project::AssetSourceRootDesc>{sourceRoots.begin(),
                                                                       sourceRoots.end()},
                .assetCacheRoot = ".asharia/cache",
                .assetDiscovery =
                    asharia::project::AssetDiscoveryDesc{
                        .ignoredDirectoryNames = {".asharia"},
                    },
            };
            auto written =
                asharia::project::writeAshariaProjectDescriptorFile(projectFile, project);
            if (!written) {
                asharia::logError(written.error().message);
                return false;
            }
            return true;
        }

        [[nodiscard]] bool writeProjectDescriptor(const std::filesystem::path& projectFile) {
            const std::array roots{
                asharia::project::AssetSourceRootDesc{
                    .rootName = "content",
                    .directory = "Content",
                    .sourcePathPrefix = "Assets",
                },
            };
            return writeProjectDescriptorWithRoots(projectFile, roots);
        }

        [[nodiscard]] std::uint64_t sourceHashFor(std::string_view sourcePath,
                                                  const std::filesystem::path& sourceFile) {
            const std::array entries{
                asharia::asset::AssetSourceSnapshotEntry{
                    .sourcePath = std::string{sourcePath},
                    .sourceFilePath = sourceFile,
                },
            };
            const asharia::asset::AssetSourceSnapshotResult result =
                asharia::asset::snapshotAssetSourceFiles(entries);
            if (!result.succeeded() || result.snapshots.empty()) {
                return 0U;
            }
            return result.snapshots.front().sourceHash;
        }

        [[nodiscard]] std::uint64_t sourceHashFor(const std::filesystem::path& sourceFile) {
            return sourceHashFor(kSourcePath, sourceFile);
        }

        [[nodiscard]] asharia::asset::SourceAssetRecord
        sourceRecordFor(const CatalogSourceFixtureDesc& desc, std::uint64_t sourceHash,
                        std::span<const asharia::asset::AssetImportSetting> settings) {
            auto guid = asharia::asset::parseAssetGuid(desc.guidText);
            return asharia::asset::SourceAssetRecord{
                .guid = guid ? *guid : asharia::asset::AssetGuid{},
                .assetType = asharia::asset::makeAssetTypeId(desc.assetTypeName),
                .assetTypeName = std::string{desc.assetTypeName},
                .sourcePath = std::string{desc.sourcePath},
                .importerId = asharia::asset::makeImporterId(desc.importerName),
                .importerName = std::string{desc.importerName},
                .importerVersion = asharia::asset::ImporterVersion{1},
                .sourceHash = sourceHash,
                .settingsHash = asharia::asset::hashAssetImportSettings(settings),
            };
        }

        [[nodiscard]] asharia::asset::SourceAssetRecord
        sourceRecord(std::uint64_t sourceHash,
                     std::span<const asharia::asset::AssetImportSetting> settings) {
            const CatalogSourceFixtureDesc desc{
                .guidText = kGuidText,
                .assetTypeName = kAssetTypeName,
                .importerName = kImporterName,
                .sourcePath = kSourcePath,
                .relativePath = "Materials/brushed.amat",
                .text = "smoke material source\n",
            };
            return sourceRecordFor(desc, sourceHash, settings);
        }

        [[nodiscard]] bool
        writeMetadataFixture(const std::filesystem::path& metadataPath,
                             const asharia::asset::SourceAssetRecord& source,
                             std::span<const asharia::asset::AssetImportSetting> settings) {
            auto written = asharia::asset::writeAssetMetadataFile(
                metadataPath,
                asharia::asset::AssetMetadataDocument{
                    .source = source,
                    .settings = std::vector<asharia::asset::AssetImportSetting>{settings.begin(),
                                                                                settings.end()},
                });
            if (!written) {
                asharia::logError(written.error().message);
                return false;
            }
            return true;
        }

        [[nodiscard]] bool writeCatalogSourceFixture(const std::filesystem::path& sourceRoot,
                                                     const CatalogSourceFixtureDesc& desc) {
            const std::filesystem::path sourceFile =
                sourceRoot / std::filesystem::path{std::string{desc.relativePath}};
            std::error_code error;
            std::filesystem::create_directories(sourceFile.parent_path(), error);
            if (error) {
                asharia::logError("Editor asset catalog smoke could not create source fixture "
                                  "directory: " +
                                  error.message());
                return false;
            }

            if (!writeTextFile(sourceFile, desc.text)) {
                return false;
            }

            const std::uint64_t sourceHash = sourceHashFor(desc.sourcePath, sourceFile);
            const std::array settings{
                asharia::asset::AssetImportSetting{.key = "profile", .value = "default"},
            };
            const asharia::asset::SourceAssetRecord source =
                sourceRecordFor(desc, sourceHash, settings);
            const std::filesystem::path metadataFile =
                std::filesystem::path{sourceFile.generic_string() +
                                      std::string{asharia::asset::kAssetMetadataSidecarSuffix}};
            if (!source || sourceHash == 0U ||
                !writeMetadataFixture(metadataFile, source, settings)) {
                asharia::logError("Editor asset catalog smoke could not prepare deterministic "
                                  "source fixture.");
                return false;
            }
            return true;
        }

        [[nodiscard]] asharia::asset::AssetProductRecord
        productRecord(const asharia::asset::SourceAssetRecord& source) {
            const std::array dependencies{
                asharia::asset::AssetDependency{
                    .owner = source.guid,
                    .kind = asharia::asset::AssetDependencyKind::SourceFile,
                    .asset = {},
                    .path = source.sourcePath,
                    .hash = source.sourceHash,
                },
                asharia::asset::AssetDependency{
                    .owner = source.guid,
                    .kind = asharia::asset::AssetDependencyKind::ImportSettings,
                    .asset = {},
                    .path = {},
                    .hash = source.settingsHash,
                },
            };
            const std::uint64_t dependencyHash =
                asharia::asset::hashAssetDependencies(dependencies);
            const asharia::asset::AssetProductKey key = asharia::asset::makeAssetProductKey(
                source, dependencyHash, asharia::asset::makeAssetTargetProfileHash(kTargetProfile));
            return asharia::asset::AssetProductRecord{
                .key = key,
                .relativeProductPath =
                    asharia::asset::makeAssetImportProductPath(key, kTargetProfile),
                .productSizeBytes = 64U,
                .productHash = asharia::asset::hashAssetProductKey(key),
            };
        }

        [[nodiscard]] bool
        writeProductManifestFixture(const std::filesystem::path& manifestPath,
                                    const asharia::asset::AssetProductRecord& product) {
            auto written = asharia::asset::writeAssetProductManifestFile(
                manifestPath, asharia::asset::AssetProductManifestDocument{
                                  .products = {product},
                              });
            if (!written) {
                asharia::logError(written.error().message);
                return false;
            }
            return true;
        }

        [[nodiscard]] bool expectReadySnapshot(const std::filesystem::path& projectFile,
                                               const std::filesystem::path& manifestFile) {
            const EditorAssetCatalogSnapshot snapshot =
                loadEditorAssetCatalogSnapshot(EditorAssetCatalogSnapshotRequest{
                    .projectFile = projectFile,
                    .productManifestFile = manifestFile,
                    .targetProfile = std::string{kTargetProfile},
                });
            if (!snapshot.succeeded() || snapshot.catalogView.entries.size() != 1U) {
                asharia::logError("Editor asset catalog smoke expected one ready catalog row.");
                return false;
            }

            const asharia::asset::AssetCatalogViewEntry& entry = snapshot.catalogView.entries[0];
            if (entry.sourcePath != kSourcePath || entry.guidText != kGuidText ||
                entry.productState != asharia::asset::AssetCatalogProductState::Ready ||
                entry.currentProductCount != 1U || !entry.diagnostics.empty()) {
                asharia::logError("Editor asset catalog smoke produced an invalid ready row.");
                return false;
            }
            return true;
        }

        [[nodiscard]] bool expectMissingProductSnapshot(const std::filesystem::path& projectFile) {
            const EditorAssetCatalogSnapshot snapshot =
                loadEditorAssetCatalogSnapshot(EditorAssetCatalogSnapshotRequest{
                    .projectFile = projectFile,
                    .productManifestFile = {},
                    .targetProfile = std::string{kTargetProfile},
                });
            if (!snapshot.succeeded() || snapshot.catalogView.entries.size() != 1U) {
                asharia::logError(
                    "Editor asset catalog smoke expected one missing-product catalog row.");
                return false;
            }

            const asharia::asset::AssetCatalogViewEntry& entry = snapshot.catalogView.entries[0];
            if (entry.productState != asharia::asset::AssetCatalogProductState::MissingProduct ||
                entry.diagnostics.empty()) {
                asharia::logError("Editor asset catalog smoke missed missing-product diagnostic.");
                return false;
            }
            return true;
        }

        [[nodiscard]] bool expectProjectDiagnostic(const std::filesystem::path& root) {
            const EditorAssetCatalogSnapshot snapshot =
                loadEditorAssetCatalogSnapshot(EditorAssetCatalogSnapshotRequest{
                    .projectFile = root / "missing.project.json",
                    .productManifestFile = {},
                    .targetProfile = std::string{kTargetProfile},
                });
            if (snapshot.succeeded() || snapshot.diagnostics.empty() ||
                snapshot.diagnostics.front().code !=
                    EditorAssetCatalogDiagnosticCode::ProjectDescriptorReadFailed) {
                asharia::logError("Editor asset catalog smoke missed project read diagnostic.");
                return false;
            }
            return true;
        }

        [[nodiscard]] bool expectCatalogStoreSelection(const std::filesystem::path& projectFile,
                                                       const std::filesystem::path& manifestFile) {
            EditorAssetCatalogStore store;
            if (store.snapshot() != nullptr || !store.diagnostics().empty() ||
                store.catalogView().entries.size() != 5U) {
                asharia::logError("Editor asset catalog smoke found invalid fixture store state.");
                return false;
            }

            store.useSnapshot(loadEditorAssetCatalogSnapshot(EditorAssetCatalogSnapshotRequest{
                .projectFile = projectFile,
                .productManifestFile = manifestFile,
                .targetProfile = std::string{kTargetProfile},
            }));
            if (store.snapshot() == nullptr || !store.snapshot()->succeeded() ||
                store.catalogView().entries.size() != 1U ||
                store.catalogView().entries.front().sourcePath != kSourcePath) {
                asharia::logError("Editor asset catalog smoke missed snapshot-backed store view.");
                return false;
            }

            store.useFixtureCatalog();
            if (store.snapshot() != nullptr || store.catalogView().entries.size() != 5U) {
                asharia::logError("Editor asset catalog smoke did not restore fixture store view.");
                return false;
            }
            return true;
        }

        struct CatalogSmokeFixturePaths {
            std::filesystem::path projectFile;
            std::filesystem::path manifestFile;
        };

        struct CatalogDeterminismFixturePaths {
            std::filesystem::path projectFile;
            std::filesystem::path reversedProjectFile;
        };

        [[nodiscard]] std::optional<CatalogSmokeFixturePaths>
        prepareCatalogSmokeFixture(const std::filesystem::path& root) {
            const std::filesystem::path projectRoot = root / "Project";
            const std::filesystem::path contentRoot = projectRoot / "Content";
            const std::filesystem::path materialDirectory = contentRoot / "Materials";
            const std::filesystem::path sourceFile = materialDirectory / "brushed.amat";
            std::error_code error;
            if (!prepareDirectory(root)) {
                return std::nullopt;
            }
            std::filesystem::create_directories(materialDirectory, error);
            if (error) {
                asharia::logError("Editor asset catalog smoke could not create content root: " +
                                  error.message());
                return std::nullopt;
            }

            const std::filesystem::path projectFile =
                projectRoot / asharia::project::kDefaultAshariaProjectFileName;
            const std::filesystem::path metadataFile =
                std::filesystem::path{sourceFile.generic_string() +
                                      std::string{asharia::asset::kAssetMetadataSidecarSuffix}};
            const std::filesystem::path manifestFile = projectRoot / "products.aproducts.json";
            if (!writeProjectDescriptor(projectFile) ||
                !writeTextFile(sourceFile, "smoke material source\n")) {
                return std::nullopt;
            }

            const std::uint64_t sourceHash = sourceHashFor(sourceFile);
            const std::array settings{
                asharia::asset::AssetImportSetting{.key = "shader", .value = "standard"},
            };
            const asharia::asset::SourceAssetRecord source = sourceRecord(sourceHash, settings);
            const asharia::asset::AssetProductRecord product = productRecord(source);
            if (!source || sourceHash == 0U || !product ||
                !writeMetadataFixture(metadataFile, source, settings) ||
                !writeProductManifestFixture(manifestFile, product)) {
                asharia::logError("Editor asset catalog smoke could not prepare asset fixtures.");
                return std::nullopt;
            }
            return CatalogSmokeFixturePaths{.projectFile = projectFile,
                                            .manifestFile = manifestFile};
        }

        [[nodiscard]] std::optional<CatalogDeterminismFixturePaths>
        prepareCatalogDeterminismFixture(const std::filesystem::path& root) {
            const std::filesystem::path projectRoot = root / "DeterminismProject";
            const std::filesystem::path contentRoot = projectRoot / "Content";
            const std::filesystem::path sharedRoot = projectRoot / "Shared";
            if (!prepareDirectory(root)) {
                return std::nullopt;
            }
            std::error_code error;
            std::filesystem::create_directories(projectRoot, error);
            if (error) {
                asharia::logError("Editor asset catalog smoke could not create determinism "
                                  "project root: " +
                                  error.message());
                return std::nullopt;
            }

            const std::array roots{
                asharia::project::AssetSourceRootDesc{
                    .rootName = "shared",
                    .directory = "Shared",
                    .sourcePathPrefix = "SharedAssets",
                },
                asharia::project::AssetSourceRootDesc{
                    .rootName = "content",
                    .directory = "Content",
                    .sourcePathPrefix = "Assets",
                },
            };
            const std::array reversedRoots{
                roots[1],
                roots[0],
            };
            const std::filesystem::path projectFile =
                projectRoot / asharia::project::kDefaultAshariaProjectFileName;
            const std::filesystem::path reversedProjectFile =
                projectRoot / "asharia.reversed.project.json";
            if (!writeProjectDescriptorWithRoots(projectFile, roots) ||
                !writeProjectDescriptorWithRoots(reversedProjectFile, reversedRoots)) {
                return std::nullopt;
            }

            const std::array contentAssets{
                CatalogSourceFixtureDesc{
                    .guidText = kGuidText,
                    .assetTypeName = kAssetTypeName,
                    .importerName = kImporterName,
                    .sourcePath = kSourcePath,
                    .relativePath = "Materials/brushed.amat",
                    .text = "determinism material source\n",
                },
                CatalogSourceFixtureDesc{
                    .guidText = "cd9c0f3d-20e2-4028-a3e9-c3f42d3fd515",
                    .assetTypeName = "com.asharia.asset.Texture2D",
                    .importerName = "asharia.texture-placeholder",
                    .sourcePath = "Assets/Textures/checker.png",
                    .relativePath = "Textures/checker.png",
                    .text = "determinism texture source\n",
                },
            };
            const std::array sharedAssets{
                CatalogSourceFixtureDesc{
                    .guidText = "13a10d4b-6987-48d1-ad27-ae4055e5a936",
                    .assetTypeName = "com.asharia.asset.Shader",
                    .importerName = "asharia.shader-slang",
                    .sourcePath = "SharedAssets/Shaders/grid.slang",
                    .relativePath = "Shaders/grid.slang",
                    .text = "determinism shader source\n",
                },
            };

            for (const CatalogSourceFixtureDesc& asset : contentAssets) {
                if (!writeCatalogSourceFixture(contentRoot, asset)) {
                    return std::nullopt;
                }
            }
            for (const CatalogSourceFixtureDesc& asset : sharedAssets) {
                if (!writeCatalogSourceFixture(sharedRoot, asset)) {
                    return std::nullopt;
                }
            }

            return CatalogDeterminismFixturePaths{.projectFile = projectFile,
                                                  .reversedProjectFile = reversedProjectFile};
        }

        [[nodiscard]] bool expectDeterministicSnapshotRows(const std::filesystem::path& root) {
            const std::optional<CatalogDeterminismFixturePaths> fixture =
                prepareCatalogDeterminismFixture(root / "Determinism");
            if (!fixture) {
                return false;
            }

            const auto loadSnapshot = [](const std::filesystem::path& projectFile) {
                return loadEditorAssetCatalogSnapshot(EditorAssetCatalogSnapshotRequest{
                    .projectFile = projectFile,
                    .productManifestFile = {},
                    .targetProfile = std::string{kTargetProfile},
                });
            };
            const EditorAssetCatalogSnapshot snapshot = loadSnapshot(fixture->projectFile);
            const EditorAssetCatalogSnapshot reversedSnapshot =
                loadSnapshot(fixture->reversedProjectFile);
            if (!snapshot.succeeded() || !reversedSnapshot.succeeded() ||
                snapshot.catalogView.entries.size() != 3U ||
                reversedSnapshot.catalogView.entries.size() != 3U) {
                asharia::logError("Editor asset catalog smoke expected three deterministic rows.");
                return false;
            }

            if (snapshot.catalogView.entries != reversedSnapshot.catalogView.entries) {
                asharia::logError("Editor asset catalog smoke rows changed when source roots were "
                                  "reversed.");
                return false;
            }

            const std::array expectedSourcePaths{
                std::string_view{"Assets/Materials/brushed.amat"},
                std::string_view{"Assets/Textures/checker.png"},
                std::string_view{"SharedAssets/Shaders/grid.slang"},
            };
            auto entry = snapshot.catalogView.entries.begin();
            for (std::string_view expectedSourcePath : expectedSourcePaths) {
                if (entry->sourcePath != expectedSourcePath ||
                    entry->productState !=
                        asharia::asset::AssetCatalogProductState::MissingProduct ||
                    entry->diagnostics.empty()) {
                    asharia::logError("Editor asset catalog smoke produced unstable row ordering "
                                      "or state.");
                    return false;
                }
                ++entry;
            }

            return true;
        }

    } // namespace

    EditorAssetCatalogSnapshot loadEditorAssetCatalogSmokeSnapshot() {
        const std::filesystem::path root = smokeRoot() / "RunSnapshot";
        std::error_code error;
        const std::optional<CatalogSmokeFixturePaths> fixture = prepareCatalogSmokeFixture(root);
        if (!fixture) {
            std::filesystem::remove_all(root, error);
            return loadEditorAssetCatalogSnapshot(EditorAssetCatalogSnapshotRequest{});
        }

        EditorAssetCatalogSnapshot snapshot =
            loadEditorAssetCatalogSnapshot(EditorAssetCatalogSnapshotRequest{
                .projectFile = fixture->projectFile,
                .productManifestFile = fixture->manifestFile,
                .targetProfile = std::string{kTargetProfile},
            });
        std::filesystem::remove_all(root, error);
        return snapshot;
    }

    bool validateEditorAssetCatalogSnapshotSmoke(EditorRunMode mode) {
        if (!isEditorSmokeMode(mode)) {
            return true;
        }

        const std::filesystem::path root = smokeRoot();
        std::error_code error;
        const std::optional<CatalogSmokeFixturePaths> fixture = prepareCatalogSmokeFixture(root);
        if (!fixture) {
            return false;
        }

        const bool passed =
            expectReadySnapshot(fixture->projectFile, fixture->manifestFile) &&
            expectMissingProductSnapshot(fixture->projectFile) && expectProjectDiagnostic(root) &&
            expectCatalogStoreSelection(fixture->projectFile, fixture->manifestFile) &&
            expectDeterministicSnapshotRows(root);
        std::filesystem::remove_all(root, error);
        return passed;
    }

} // namespace asharia::editor
