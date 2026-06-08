#include "editor_asset_catalog_smoke.hpp"

#include <algorithm>
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

#include "asharia/archive/archive_value.hpp"
#include "asharia/archive/json_archive.hpp"
#include "asharia/asset_core/asset_guid.hpp"
#include "asharia/asset_core/asset_metadata_io.hpp"
#include "asharia/asset_pipeline/asset_import_planning.hpp"
#include "asharia/asset_pipeline/asset_product_manifest_io.hpp"
#include "asharia/asset_pipeline/asset_source_scan.hpp"
#include "asharia/asset_pipeline/asset_source_snapshot.hpp"
#include "asharia/asset_pipeline/asset_texture_import_profile.hpp"
#include "asharia/core/log.hpp"
#include "asharia/project/project_descriptor_io.hpp"

#include "editor_asset_catalog.hpp"
#include "editor_asset_catalog_report.hpp"
#include "editor_asset_icon.hpp"
#include "editor_asset_import_settings_command.hpp"
#include "editor_smoke.hpp"

namespace asharia::editor {
    namespace {

        constexpr std::string_view kTargetProfile = "editor-preview";
        constexpr std::string_view kGuidText = "5d3cdcbf-7396-40d0-b497-4fa2fe54f92a";
        constexpr std::string_view kAssetTypeName = "com.asharia.asset.Material";
        constexpr std::string_view kImporterName = "asharia.material";
        constexpr std::string_view kSourcePath = "Assets/Materials/brushed.amat";
        constexpr std::string_view kTextureSourcePath = "Assets/Textures/checker.png";
        constexpr std::string_view kDefaultAssetTypeName = "com.asharia.asset.DefaultAsset";
        constexpr std::string_view kDefaultAssetRoleName = "com.asharia.asset.DefaultAsset";
        constexpr std::size_t kFixtureCatalogRows = 8U;

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

        [[nodiscard]] std::string pathJsonString(const std::filesystem::path& path) {
            const std::u8string value = path.generic_u8string();
            return std::string{value.begin(), value.end()};
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
                .assetCacheRoot = ".asharia/cache/assets",
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

        [[nodiscard]] std::vector<asharia::asset::AssetImportSetting>
        settingsForCatalogSourceFixture(const CatalogSourceFixtureDesc& desc) {
            std::vector<asharia::asset::AssetImportSetting> settings{
                asharia::asset::AssetImportSetting{.key = "profile", .value = "default"},
            };
            if (std::string_view{desc.assetTypeName}.contains("Texture") ||
                std::string_view{desc.sourcePath}.ends_with(".png")) {
                settings.push_back(asharia::asset::AssetImportSetting{
                    .key = std::string{asharia::asset::kTextureImportProfileSettingKey},
                    .value = std::string{asharia::asset::kTextureImportProfileTexture2D},
                });
            }
            return settings;
        }

        [[nodiscard]] std::filesystem::path
        sourceFileForFixture(const std::filesystem::path& sourceRoot,
                             const CatalogSourceFixtureDesc& desc) {
            return sourceRoot / std::filesystem::path{std::string{desc.relativePath}};
        }

        [[nodiscard]] bool writeCatalogSourceFixture(const std::filesystem::path& sourceRoot,
                                                     const CatalogSourceFixtureDesc& desc) {
            const std::filesystem::path sourceFile = sourceFileForFixture(sourceRoot, desc);
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
            const std::vector<asharia::asset::AssetImportSetting> settings =
                settingsForCatalogSourceFixture(desc);
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

        [[nodiscard]] std::optional<asharia::asset::AssetProductRecord>
        productRecordForFixture(const std::filesystem::path& sourceRoot,
                                const CatalogSourceFixtureDesc& desc) {
            const std::filesystem::path sourceFile = sourceFileForFixture(sourceRoot, desc);
            const std::uint64_t sourceHash = sourceHashFor(desc.sourcePath, sourceFile);
            const std::vector<asharia::asset::AssetImportSetting> settings =
                settingsForCatalogSourceFixture(desc);
            const asharia::asset::SourceAssetRecord source =
                sourceRecordFor(desc, sourceHash, settings);
            asharia::asset::AssetProductRecord product = productRecord(source);
            if (!source || sourceHash == 0U || !product) {
                return std::nullopt;
            }
            return product;
        }

        [[nodiscard]] bool
        writeProductManifestFixture(const std::filesystem::path& manifestPath,
                                    std::span<const asharia::asset::AssetProductRecord> products) {
            std::error_code error;
            std::filesystem::create_directories(manifestPath.parent_path(), error);
            if (error) {
                asharia::logError("Editor asset catalog smoke could not create manifest root: " +
                                  error.message());
                return false;
            }

            auto written = asharia::asset::writeAssetProductManifestFile(
                manifestPath,
                asharia::asset::AssetProductManifestDocument{
                    .products = std::vector<asharia::asset::AssetProductRecord>{products.begin(),
                                                                                products.end()},
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
            if (!snapshot.succeeded() || snapshot.catalogView.entries.size() != 1U ||
                snapshot.targetProfile != kTargetProfile) {
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

        [[nodiscard]] bool
        expectUntrackedDefaultAssetSnapshot(const std::filesystem::path& projectFile,
                                            const std::filesystem::path& manifestFile) {
            const std::filesystem::path looseSourceFile =
                projectFile.parent_path() / "Content" / "Loose" / "readme.txt";
            std::error_code error;
            std::filesystem::create_directories(looseSourceFile.parent_path(), error);
            if (error || !writeTextFile(looseSourceFile, "loose untracked source\n")) {
                asharia::logError(
                    "Editor asset catalog smoke could not create loose source fixture.");
                return false;
            }

            const EditorAssetCatalogSnapshot snapshot =
                loadEditorAssetCatalogSnapshot(EditorAssetCatalogSnapshotRequest{
                    .projectFile = projectFile,
                    .productManifestFile = manifestFile,
                    .targetProfile = std::string{kTargetProfile},
                });
            if (!snapshot.succeeded() || snapshot.catalogView.entries.size() != 2U) {
                asharia::logError("Editor asset catalog smoke expected ready and untracked rows.");
                return false;
            }

            const auto material =
                std::ranges::find_if(snapshot.catalogView.entries,
                                     [](const asharia::asset::AssetCatalogViewEntry& entry) {
                                         return entry.sourcePath == kSourcePath;
                                     });
            const auto loose =
                std::ranges::find_if(snapshot.catalogView.entries,
                                     [](const asharia::asset::AssetCatalogViewEntry& entry) {
                                         return entry.sourcePath == "Assets/Loose/readme.txt";
                                     });
            if (material == snapshot.catalogView.entries.end() ||
                material->productState != asharia::asset::AssetCatalogProductState::Ready ||
                loose == snapshot.catalogView.entries.end()) {
                asharia::logError(
                    "Editor asset catalog smoke lost a tracked row after loose source scan.");
                return false;
            }

            if (!loose->guidText.empty() || loose->assetTypeName != kDefaultAssetTypeName ||
                loose->assetRoleName != kDefaultAssetRoleName || !loose->importerName.empty() ||
                loose->productState != asharia::asset::AssetCatalogProductState::NotTracked ||
                loose->extension != ".txt" || loose->diagnostics.empty()) {
                asharia::logError(
                    "Editor asset catalog smoke produced invalid untracked default asset row.");
                return false;
            }
            if (!std::ranges::any_of(
                    snapshot.diagnostics, [](const EditorAssetCatalogDiagnostic& diagnostic) {
                        return diagnostic.code == EditorAssetCatalogDiagnosticCode::SourceScan &&
                               diagnostic.severity ==
                                   EditorAssetCatalogDiagnosticSeverity::Warning &&
                               diagnostic.sourcePath == "Assets/Loose/readme.txt";
                    })) {
                asharia::logError(
                    "Editor asset catalog smoke missed loose source warning diagnostic.");
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

        struct ExpectedArchiveString {
            std::string_view value;
        };

        [[nodiscard]] bool expectArchiveStringMember(const asharia::archive::ArchiveValue& object,
                                                     std::string_view key,
                                                     ExpectedArchiveString expected) {
            const asharia::archive::ArchiveValue* value = object.findMemberValue(key);
            return value != nullptr && value->kind == asharia::archive::ArchiveValueKind::String &&
                   value->stringValue == expected.value;
        }

        [[nodiscard]] bool expectArchiveIntegerMember(const asharia::archive::ArchiveValue& object,
                                                      std::string_view key, std::int64_t expected) {
            const asharia::archive::ArchiveValue* value = object.findMemberValue(key);
            return value != nullptr && value->kind == asharia::archive::ArchiveValueKind::Integer &&
                   value->integerValue == expected;
        }

        [[nodiscard]] bool
        expectProjectDirectorySnapshot(const std::filesystem::path& projectFile,
                                       const std::filesystem::path& manifestFile) {
            const std::filesystem::path projectDirectory = projectFile.parent_path();
            const EditorAssetCatalogSnapshotRequest request{
                .projectFile = projectDirectory,
                .productManifestFile = {},
                .targetProfile = std::string{kTargetProfile},
            };
            const EditorAssetCatalogSnapshot snapshot = loadEditorAssetCatalogSnapshot(request);
            if (!snapshot.succeeded() || snapshot.catalogView.entries.size() != 1U ||
                snapshot.projectFile != projectFile || snapshot.targetProfile != kTargetProfile ||
                snapshot.productManifestFile != manifestFile) {
                asharia::logError(
                    "Editor asset catalog smoke failed project directory resolution.");
                return false;
            }

            const asharia::asset::AssetCatalogViewEntry& entry = snapshot.catalogView.entries[0];
            if (entry.sourcePath != kSourcePath ||
                entry.productState != asharia::asset::AssetCatalogProductState::Ready ||
                entry.currentProductCount != 1U) {
                asharia::logError(
                    "Editor asset catalog smoke produced invalid project directory row.");
                return false;
            }

            auto report = writeEditorAssetCatalogSnapshotJsonReport(request, snapshot);
            if (!report) {
                asharia::logError(report.error().message);
                return false;
            }
            auto parsed = asharia::archive::readJsonArchive(*report);
            const std::string projectFileText = pathJsonString(projectFile);
            const std::string manifestFileText = pathJsonString(manifestFile);
            if (!parsed || parsed->kind != asharia::archive::ArchiveValueKind::Object ||
                !expectArchiveStringMember(*parsed, "projectFile",
                                           ExpectedArchiveString{projectFileText}) ||
                !expectArchiveStringMember(*parsed, "productManifestFile",
                                           ExpectedArchiveString{manifestFileText})) {
                asharia::logError(
                    "Editor asset catalog smoke missed resolved project directory paths.");
                return false;
            }

            return true;
        }

        [[nodiscard]] bool expectCatalogJsonReportRoot(const asharia::archive::ArchiveValue& parsed,
                                                       const std::filesystem::path& projectFile,
                                                       const std::filesystem::path& manifestFile) {
            const std::string projectFileText = pathJsonString(projectFile);
            const std::string manifestFileText = pathJsonString(manifestFile);
            if (parsed.kind != asharia::archive::ArchiveValueKind::Object ||
                !expectArchiveStringMember(
                    parsed, "schema",
                    ExpectedArchiveString{"com.asharia.editor.assetCatalogCheck"}) ||
                !expectArchiveIntegerMember(parsed, "schemaVersion", 1) ||
                !expectArchiveStringMember(parsed, "projectFile",
                                           ExpectedArchiveString{projectFileText}) ||
                !expectArchiveStringMember(parsed, "productManifestFile",
                                           ExpectedArchiveString{manifestFileText}) ||
                !expectArchiveStringMember(parsed, "targetProfile",
                                           ExpectedArchiveString{kTargetProfile}) ||
                !expectArchiveIntegerMember(parsed, "rowCount", 1) ||
                !expectArchiveIntegerMember(parsed, "diagnosticCount", 0)) {
                asharia::logError("Editor asset catalog smoke produced invalid JSON report root.");
                return false;
            }
            return true;
        }

        [[nodiscard]] bool
        expectCatalogJsonSourceRoot(const asharia::archive::ArchiveValue& parsed,
                                    const std::filesystem::path& expectedSourceRoot) {
            const asharia::archive::ArchiveValue* sourceRoots =
                parsed.findMemberValue("sourceRoots");
            if (sourceRoots == nullptr ||
                sourceRoots->kind != asharia::archive::ArchiveValueKind::Array ||
                sourceRoots->arrayValue.size() != 1U) {
                asharia::logError("Editor asset catalog smoke produced invalid JSON source roots.");
                return false;
            }

            const std::string expectedSourceRootText = pathJsonString(expectedSourceRoot);
            const asharia::archive::ArchiveValue& sourceRoot = sourceRoots->arrayValue.front();
            if (sourceRoot.kind != asharia::archive::ArchiveValueKind::Object ||
                !expectArchiveStringMember(sourceRoot, "name", ExpectedArchiveString{"content"}) ||
                !expectArchiveStringMember(sourceRoot, "sourcePathPrefix",
                                           ExpectedArchiveString{"Assets"}) ||
                !expectArchiveStringMember(sourceRoot, "directory",
                                           ExpectedArchiveString{"Content"}) ||
                !expectArchiveStringMember(sourceRoot, "resolvedDirectory",
                                           ExpectedArchiveString{expectedSourceRootText})) {
                asharia::logError(
                    "Editor asset catalog smoke produced invalid JSON source root details.");
                return false;
            }
            return true;
        }

        [[nodiscard]] bool
        expectCatalogJsonNavigation(const asharia::archive::ArchiveValue& parsed) {
            const asharia::archive::ArchiveValue* navigationNodes =
                parsed.findMemberValue("navigationNodes");
            if (navigationNodes == nullptr ||
                navigationNodes->kind != asharia::archive::ArchiveValueKind::Array ||
                navigationNodes->arrayValue.size() != 3U) {
                asharia::logError(
                    "Editor asset catalog smoke produced invalid JSON navigation nodes.");
                return false;
            }

            const asharia::archive::ArchiveValue& sourceRootNode = navigationNodes->arrayValue[0];
            const asharia::archive::ArchiveValue& folderNode = navigationNodes->arrayValue[1];
            const asharia::archive::ArchiveValue& assetNode = navigationNodes->arrayValue[2];
            if (sourceRootNode.kind != asharia::archive::ArchiveValueKind::Object ||
                !expectArchiveStringMember(sourceRootNode, "kind",
                                           ExpectedArchiveString{"source-root"}) ||
                !expectArchiveStringMember(sourceRootNode, "key",
                                           ExpectedArchiveString{"source-root:0"}) ||
                !expectArchiveStringMember(sourceRootNode, "displayName",
                                           ExpectedArchiveString{"content"}) ||
                !expectArchiveStringMember(sourceRootNode, "scopePath",
                                           ExpectedArchiveString{"Assets"})) {
                asharia::logError(
                    "Editor asset catalog smoke produced invalid source-root navigation node.");
                return false;
            }
            if (folderNode.kind != asharia::archive::ArchiveValueKind::Object ||
                !expectArchiveStringMember(folderNode, "kind", ExpectedArchiveString{"folder"}) ||
                !expectArchiveStringMember(folderNode, "key",
                                           ExpectedArchiveString{"folder:Assets/Materials"}) ||
                !expectArchiveStringMember(folderNode, "parentKey",
                                           ExpectedArchiveString{"source-root:0"}) ||
                !expectArchiveStringMember(folderNode, "displayName",
                                           ExpectedArchiveString{"Materials"}) ||
                !expectArchiveStringMember(folderNode, "scopePath",
                                           ExpectedArchiveString{"Assets/Materials"})) {
                asharia::logError(
                    "Editor asset catalog smoke produced invalid folder navigation node.");
                return false;
            }
            if (assetNode.kind != asharia::archive::ArchiveValueKind::Object ||
                !expectArchiveStringMember(assetNode, "kind", ExpectedArchiveString{"asset"}) ||
                !expectArchiveStringMember(
                    assetNode, "key",
                    ExpectedArchiveString{"asset:Assets/Materials/brushed.amat"}) ||
                !expectArchiveStringMember(assetNode, "parentKey",
                                           ExpectedArchiveString{"folder:Assets/Materials"}) ||
                !expectArchiveStringMember(assetNode, "sourcePath",
                                           ExpectedArchiveString{kSourcePath}) ||
                !expectArchiveStringMember(assetNode, "guid", ExpectedArchiveString{kGuidText}) ||
                !expectArchiveStringMember(assetNode, "productState",
                                           ExpectedArchiveString{"ready"})) {
                asharia::logError(
                    "Editor asset catalog smoke produced invalid asset navigation node.");
                return false;
            }
            return true;
        }

        [[nodiscard]] bool expectCatalogJsonRow(const asharia::archive::ArchiveValue& parsed,
                                                const std::filesystem::path& expectedSourceRoot,
                                                const std::filesystem::path& expectedSourceFile,
                                                const std::filesystem::path& expectedMetadataFile) {
            const asharia::archive::ArchiveValue* rows = parsed.findMemberValue("rows");
            if (rows == nullptr || rows->kind != asharia::archive::ArchiveValueKind::Array ||
                rows->arrayValue.size() != 1U) {
                asharia::logError("Editor asset catalog smoke produced invalid JSON report rows.");
                return false;
            }

            const std::string expectedSourceRootText = pathJsonString(expectedSourceRoot);
            const std::string expectedSourceFileText = pathJsonString(expectedSourceFile);
            const std::string expectedMetadataFileText = pathJsonString(expectedMetadataFile);
            const asharia::archive::ArchiveValue& row = rows->arrayValue.front();
            if (row.kind != asharia::archive::ArchiveValueKind::Object ||
                !expectArchiveStringMember(row, "guid", ExpectedArchiveString{kGuidText}) ||
                !expectArchiveStringMember(row, "sourcePath", ExpectedArchiveString{kSourcePath}) ||
                !expectArchiveStringMember(row, "sourceRootName",
                                           ExpectedArchiveString{"content"}) ||
                !expectArchiveStringMember(row, "sourceRootPrefix",
                                           ExpectedArchiveString{"Assets"}) ||
                !expectArchiveStringMember(row, "sourceRootDirectory",
                                           ExpectedArchiveString{expectedSourceRootText}) ||
                !expectArchiveStringMember(row, "sourceFilePath",
                                           ExpectedArchiveString{expectedSourceFileText}) ||
                !expectArchiveStringMember(row, "metadataFilePath",
                                           ExpectedArchiveString{expectedMetadataFileText}) ||
                !expectArchiveStringMember(row, "productState", ExpectedArchiveString{"ready"}) ||
                !expectArchiveIntegerMember(row, "currentProductCount", 1)) {
                asharia::logError("Editor asset catalog smoke produced invalid JSON report row.");
                return false;
            }

            const asharia::archive::ArchiveValue* resolvedIcon =
                row.findMemberValue("resolvedIcon");
            if (resolvedIcon == nullptr ||
                resolvedIcon->kind != asharia::archive::ArchiveValueKind::Object ||
                !expectArchiveStringMember(*resolvedIcon, "id",
                                           ExpectedArchiveString{"lucide.palette"}) ||
                !expectArchiveStringMember(*resolvedIcon, "tooltipKey",
                                           ExpectedArchiveString{"icon.material"})) {
                asharia::logError(
                    "Editor asset catalog smoke produced invalid JSON report row icon.");
                return false;
            }

            const asharia::archive::ArchiveValue* subAssets = row.findMemberValue("subAssets");
            const asharia::archive::ArchiveValue* rowDiagnostics =
                row.findMemberValue("diagnostics");
            if (subAssets == nullptr ||
                subAssets->kind != asharia::archive::ArchiveValueKind::Array ||
                !subAssets->arrayValue.empty() || rowDiagnostics == nullptr ||
                rowDiagnostics->kind != asharia::archive::ArchiveValueKind::Array ||
                !rowDiagnostics->arrayValue.empty()) {
                asharia::logError(
                    "Editor asset catalog smoke produced invalid JSON report row details.");
                return false;
            }
            return true;
        }

        [[nodiscard]] bool
        expectCatalogJsonCustomIcon(const EditorAssetCatalogSnapshotRequest& request,
                                    const EditorAssetCatalogSnapshot& snapshot) {
            EditorAssetIconRegistry iconRegistry;
            auto customIcon =
                iconRegistry.registerRule("smoke.catalog-json-material-icon",
                                          EditorAssetIconRule{
                                              .folder = false,
                                              .diagnostic = EditorAssetIconDiagnosticState::None,
                                              .minimumSubAssetCount = {},
                                              .assetTypeContains = "material",
                                              .importerIdContains = {},
                                              .extension = ".amat",
                                              .sourcePathContains = {},
                                              .displayNameContains = {},
                                              .guidText = {},
                                              .importProfile = {},
                                              .assetRoleContains = {},
                                              .descriptor = makeLucideEditorIconDescriptor(
                                                  "sparkles", editorIconTint(0.78F, 0.58F, 0.92F),
                                                  "icon.material.custom", "Custom material"),
                                          });
            if (!customIcon) {
                asharia::logError(customIcon.error().message);
                return false;
            }

            auto customReport =
                writeEditorAssetCatalogSnapshotJsonReport(request, snapshot, iconRegistry);
            if (!customReport) {
                asharia::logError(customReport.error().message);
                return false;
            }
            auto customParsed = asharia::archive::readJsonArchive(*customReport);
            if (!customParsed || customParsed->kind != asharia::archive::ArchiveValueKind::Object) {
                asharia::logError("Editor asset catalog smoke produced invalid custom JSON.");
                return false;
            }

            const asharia::archive::ArchiveValue* customRows =
                customParsed->findMemberValue("rows");
            if (customRows == nullptr ||
                customRows->kind != asharia::archive::ArchiveValueKind::Array ||
                customRows->arrayValue.size() != 1U) {
                asharia::logError("Editor asset catalog smoke produced invalid custom JSON rows.");
                return false;
            }
            const asharia::archive::ArchiveValue* customResolvedIcon =
                customRows->arrayValue.front().findMemberValue("resolvedIcon");
            if (customResolvedIcon == nullptr ||
                customResolvedIcon->kind != asharia::archive::ArchiveValueKind::Object ||
                !expectArchiveStringMember(*customResolvedIcon, "id",
                                           ExpectedArchiveString{"lucide.sparkles"}) ||
                !expectArchiveStringMember(*customResolvedIcon, "tooltipKey",
                                           ExpectedArchiveString{"icon.material.custom"})) {
                asharia::logError(
                    "Editor asset catalog smoke missed injected JSON report icon registry.");
                return false;
            }
            return true;
        }

        [[nodiscard]] bool expectCatalogJsonReport(const std::filesystem::path& projectFile,
                                                   const std::filesystem::path& manifestFile) {
            std::filesystem::path expectedSourceRoot = projectFile.parent_path() / "Content";
            expectedSourceRoot.make_preferred();
            std::filesystem::path expectedSourceFile =
                expectedSourceRoot / "Materials" / "brushed.amat";
            expectedSourceFile.make_preferred();
            std::filesystem::path expectedMetadataFile =
                std::filesystem::path{expectedSourceFile.string() +
                                      std::string{asharia::asset::kAssetMetadataSidecarSuffix}};
            expectedMetadataFile.make_preferred();
            const EditorAssetCatalogSnapshotRequest request{
                .projectFile = projectFile,
                .productManifestFile = manifestFile,
                .targetProfile = std::string{kTargetProfile},
            };
            const EditorAssetCatalogSnapshot snapshot = loadEditorAssetCatalogSnapshot(request);
            auto report = writeEditorAssetCatalogSnapshotJsonReport(request, snapshot);
            if (!report) {
                asharia::logError(report.error().message);
                return false;
            }

            auto parsed = asharia::archive::readJsonArchive(*report);
            if (!parsed) {
                asharia::logError("Editor asset catalog smoke produced invalid JSON report.");
                return false;
            }

            return expectCatalogJsonReportRoot(*parsed, projectFile, manifestFile) &&
                   expectCatalogJsonSourceRoot(*parsed, expectedSourceRoot) &&
                   expectCatalogJsonNavigation(*parsed) &&
                   expectCatalogJsonRow(*parsed, expectedSourceRoot, expectedSourceFile,
                                        expectedMetadataFile) &&
                   expectCatalogJsonCustomIcon(request, snapshot);
        }

        [[nodiscard]] const asharia::asset::AssetCatalogViewEntry*
        findFixtureProfile(const asharia::asset::AssetCatalogView& catalogView,
                           std::string_view importProfile) {
            const auto found = std::ranges::find_if(
                catalogView.entries,
                [importProfile](const asharia::asset::AssetCatalogViewEntry& entry) {
                    return entry.importProfileName == importProfile;
                });
            return found == catalogView.entries.end() ? nullptr : &*found;
        }

        [[nodiscard]] bool
        expectFixtureTextureProfiles(const asharia::asset::AssetCatalogView& catalogView) {
            const asharia::asset::AssetCatalogViewEntry* texture2d =
                findFixtureProfile(catalogView, asharia::asset::kTextureImportProfileTexture2D);
            const asharia::asset::AssetCatalogViewEntry* spriteSheet =
                findFixtureProfile(catalogView, asharia::asset::kTextureImportProfileSpriteSheet);
            const asharia::asset::AssetCatalogViewEntry* textureCube =
                findFixtureProfile(catalogView, asharia::asset::kTextureImportProfileTextureCube);
            const asharia::asset::AssetCatalogViewEntry* skybox =
                findFixtureProfile(catalogView, asharia::asset::kTextureImportProfileSkybox);
            if (texture2d == nullptr || spriteSheet == nullptr || textureCube == nullptr ||
                skybox == nullptr) {
                asharia::logError(
                    "Editor asset catalog smoke missed fixture texture profile rows.");
                return false;
            }
            if (texture2d->assetRoleName != asharia::asset::kTextureRoleTexture2D ||
                spriteSheet->assetRoleName != asharia::asset::kTextureRoleSpriteSheet ||
                textureCube->assetRoleName != asharia::asset::kTextureRoleTextureCube ||
                skybox->assetRoleName != asharia::asset::kTextureRoleSkybox) {
                asharia::logError(
                    "Editor asset catalog smoke produced invalid fixture texture roles.");
                return false;
            }
            if (spriteSheet->subAssets.size() != 2U ||
                spriteSheet->subAssets[0].stableId != "hero-idle-0" ||
                spriteSheet->subAssets[1].stableId != "hero-run-0") {
                asharia::logError(
                    "Editor asset catalog smoke missed fixture sprite-sheet sub-assets.");
                return false;
            }
            return true;
        }

        [[nodiscard]] bool expectCatalogStoreSelection(const std::filesystem::path& projectFile,
                                                       const std::filesystem::path& manifestFile) {
            EditorAssetCatalogStore store;
            if (store.snapshot() != nullptr || !store.diagnostics().empty() ||
                store.catalogView().entries.size() != kFixtureCatalogRows ||
                !expectFixtureTextureProfiles(store.catalogView())) {
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
            if (store.snapshot() != nullptr ||
                store.catalogView().entries.size() != kFixtureCatalogRows ||
                !expectFixtureTextureProfiles(store.catalogView())) {
                asharia::logError("Editor asset catalog smoke did not restore fixture store view.");
                return false;
            }
            return true;
        }

        struct CatalogSmokeFixturePaths {
            std::filesystem::path projectFile;
            std::filesystem::path manifestFile;
        };

        struct CatalogSmokeFixtureOptions {
            bool writeManifest{true};
            bool includeTexture{false};
        };

        struct CatalogDeterminismFixturePaths {
            std::filesystem::path projectFile;
            std::filesystem::path reversedProjectFile;
        };

        [[nodiscard]] std::optional<CatalogSmokeFixturePaths>
        prepareCatalogSmokeFixture(const std::filesystem::path& root,
                                   CatalogSmokeFixtureOptions options = {}) {
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
            const std::filesystem::path manifestFile =
                projectRoot / ".asharia" / "cache" / "products.aproducts.json";
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
            std::vector<asharia::asset::AssetProductRecord> products{product};
            if (options.includeTexture) {
                const CatalogSourceFixtureDesc textureDesc{
                    .guidText = "cd9c0f3d-20e2-4028-a3e9-c3f42d3fd515",
                    .assetTypeName = "com.asharia.asset.Texture2D",
                    .importerName = "asharia.texture-placeholder",
                    .sourcePath = "Assets/Textures/checker.png",
                    .relativePath = "Textures/checker.png",
                    .text = "smoke texture source\n",
                };
                if (!writeCatalogSourceFixture(contentRoot, textureDesc)) {
                    asharia::logError(
                        "Editor asset catalog smoke could not prepare texture fixture.");
                    return std::nullopt;
                }
                const std::optional<asharia::asset::AssetProductRecord> textureProduct =
                    productRecordForFixture(contentRoot, textureDesc);
                if (!textureProduct) {
                    asharia::logError(
                        "Editor asset catalog smoke could not prepare texture fixture.");
                    return std::nullopt;
                }
                products.push_back(*textureProduct);
            }
            if (!source || sourceHash == 0U || !product ||
                !writeMetadataFixture(metadataFile, source, settings) ||
                (options.writeManifest && !writeProductManifestFixture(manifestFile, products))) {
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
                kTextureSourcePath,
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

            const auto textureEntry =
                std::ranges::find_if(snapshot.catalogView.entries,
                                     [](const asharia::asset::AssetCatalogViewEntry& candidate) {
                                         return candidate.sourcePath == kTextureSourcePath;
                                     });
            if (textureEntry == snapshot.catalogView.entries.end() ||
                textureEntry->importProfileName != asharia::asset::kTextureImportProfileTexture2D ||
                textureEntry->assetRoleName != asharia::asset::kTextureRoleTexture2D) {
                asharia::logError(
                    "Editor asset catalog smoke missed texture profile catalog fields.");
                return false;
            }

            return true;
        }

        [[nodiscard]] const asharia::asset::AssetCatalogViewEntry*
        findTextureCatalogRow(const EditorAssetCatalogSnapshot& snapshot) {
            const auto found =
                std::ranges::find_if(snapshot.catalogView.entries,
                                     [](const asharia::asset::AssetCatalogViewEntry& candidate) {
                                         return candidate.sourcePath == kTextureSourcePath;
                                     });
            return found == snapshot.catalogView.entries.end() ? nullptr : &*found;
        }

        [[nodiscard]] bool
        expectCatalogStoreRefreshFromPendingImportSettings(const std::filesystem::path& root) {
            const std::optional<CatalogSmokeFixturePaths> fixture = prepareCatalogSmokeFixture(
                root / "RefreshPending", CatalogSmokeFixtureOptions{.includeTexture = true});
            if (!fixture) {
                return false;
            }

            const EditorAssetCatalogSnapshotRequest request{
                .projectFile = fixture->projectFile,
                .productManifestFile = fixture->manifestFile,
                .targetProfile = std::string{kTargetProfile},
            };
            EditorAssetCatalogStore store;
            store.useSnapshot(loadEditorAssetCatalogSnapshot(request));
            const EditorAssetCatalogSnapshot* initialSnapshot = store.snapshot();
            const asharia::asset::AssetCatalogViewEntry* initialTexture =
                initialSnapshot == nullptr ? nullptr : findTextureCatalogRow(*initialSnapshot);
            if (initialSnapshot == nullptr || !initialSnapshot->succeeded() ||
                initialTexture == nullptr ||
                initialTexture->importProfileName !=
                    asharia::asset::kTextureImportProfileTexture2D ||
                initialTexture->assetRoleName != asharia::asset::kTextureRoleTexture2D ||
                initialTexture->productState != asharia::asset::AssetCatalogProductState::Ready ||
                initialTexture->currentProductCount != 1U) {
                asharia::logError(
                    "Editor asset catalog smoke could not prepare refresh baseline row.");
                return false;
            }

            const std::filesystem::path metadataFile =
                resolveEditorAssetCatalogMetadataFilePath(*initialSnapshot, kTextureSourcePath);
            EditorAssetReimportRequestLog reimportRequests;
            auto command = makeEditorTextureProfileEditCommand(metadataFile, kTargetProfile,
                                                               "SpriteSheet", reimportRequests);
            if (!command) {
                asharia::logError(command.error().message);
                return false;
            }
            if (auto executed = (*command)->execute(); !executed) {
                asharia::logError(executed.error().message);
                return false;
            }

            EditorAssetReimportPendingState pendingReimports;
            pendingReimports.recordAll(reimportRequests.requests());
            const std::vector<EditorAssetPendingReimportWorkItem> pendingWork =
                pendingReimports.snapshotPendingWork();
            if (pendingWork.size() != 1U || pendingWork.front().sourcePath != kTextureSourcePath ||
                pendingWork.front().targetProfile != kTargetProfile ||
                pendingWork.front().changedSettingKeys.size() != 1U ||
                pendingWork.front().changedSettingKeys.front() !=
                    asharia::asset::kTextureImportProfileSettingKey) {
                asharia::logError(
                    "Editor asset catalog smoke missed pending refresh handoff facts.");
                return false;
            }

            const EditorAssetCatalogSnapshot* refreshedSnapshot =
                refreshEditorAssetCatalogStore(store);
            const asharia::asset::AssetCatalogViewEntry* refreshedTexture =
                refreshedSnapshot == nullptr ? nullptr : findTextureCatalogRow(*refreshedSnapshot);
            if (refreshedSnapshot == nullptr || !refreshedSnapshot->succeeded() ||
                refreshedTexture == nullptr ||
                refreshedTexture->importProfileName !=
                    asharia::asset::kTextureImportProfileSpriteSheet ||
                refreshedTexture->assetRoleName != asharia::asset::kTextureRoleSpriteSheet ||
                refreshedTexture->productState !=
                    asharia::asset::AssetCatalogProductState::StaleProduct ||
                refreshedTexture->currentProductCount != 0U ||
                refreshedTexture->staleProductCount != 1U ||
                refreshedTexture->diagnostics.empty()) {
                asharia::logError(
                    "Editor asset catalog smoke refresh did not reflect metadata change.");
                return false;
            }

            if (!pendingReimports.hasPending(EditorAssetReimportSourceKey{
                    .guid = refreshedTexture->guid,
                    .sourcePath = refreshedTexture->sourcePath,
                    .targetProfile = kTargetProfile,
                })) {
                asharia::logError(
                    "Editor asset catalog smoke refresh consumed pending reimport state.");
                return false;
            }

            EditorAssetCatalogStore fixtureStore;
            if (refreshEditorAssetCatalogStore(fixtureStore) != nullptr) {
                asharia::logError(
                    "Editor asset catalog smoke refreshed a fixture-only catalog store.");
                return false;
            }
            return true;
        }

    } // namespace

    EditorAssetCatalogSnapshot loadEditorAssetCatalogSmokeSnapshot() {
        const std::filesystem::path root = smokeRoot() / "RunSnapshot";
        std::error_code error;
        const std::optional<CatalogSmokeFixturePaths> fixture =
            prepareCatalogSmokeFixture(root, CatalogSmokeFixtureOptions{.includeTexture = true});
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
        const std::optional<CatalogSmokeFixturePaths> fixture =
            prepareCatalogSmokeFixture(root / "Ready");
        const std::optional<CatalogSmokeFixturePaths> missingFixture = prepareCatalogSmokeFixture(
            root / "Missing", CatalogSmokeFixtureOptions{.writeManifest = false});
        const std::optional<CatalogSmokeFixturePaths> untrackedFixture =
            prepareCatalogSmokeFixture(root / "UntrackedDefault");
        if (!fixture || !missingFixture || !untrackedFixture) {
            return false;
        }

        const bool passed =
            expectReadySnapshot(fixture->projectFile, fixture->manifestFile) &&
            expectMissingProductSnapshot(missingFixture->projectFile) &&
            expectUntrackedDefaultAssetSnapshot(untrackedFixture->projectFile,
                                                untrackedFixture->manifestFile) &&
            expectProjectDiagnostic(root) &&
            expectProjectDirectorySnapshot(fixture->projectFile, fixture->manifestFile) &&
            expectCatalogJsonReport(fixture->projectFile, fixture->manifestFile) &&
            expectCatalogStoreSelection(fixture->projectFile, fixture->manifestFile) &&
            expectDeterministicSnapshotRows(root) &&
            expectCatalogStoreRefreshFromPendingImportSettings(root);
        std::filesystem::remove_all(root, error);
        return passed;
    }

} // namespace asharia::editor
