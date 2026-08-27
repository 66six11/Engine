#include "asset_processor_smoke.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "asharia/asset_core/asset_guid.hpp"
#include "asharia/asset_core/asset_metadata.hpp"
#include "asharia/asset_core/asset_metadata_io.hpp"
#include "asharia/asset_core/asset_product.hpp"
#include "asharia/asset_core/asset_type.hpp"
#include "asharia/asset_pipeline/asset_glb_import.hpp"
#include "asharia/asset_pipeline/asset_product_blob.hpp"
#include "asharia/asset_pipeline/asset_product_manifest_io.hpp"
#include "asharia/asset_pipeline/asset_scanned_import_planning.hpp"
#include "asharia/asset_pipeline/asset_texture_import.hpp"
#include "asharia/asset_pipeline/asset_texture_import_profile.hpp"
#include "asharia/mesh_product/mesh_product_v1.hpp"
#include "asharia/project/project_descriptor_io.hpp"
#include "asharia/resource_runtime/mesh_resource_store.hpp"

#include "asset_processor_dry_run.hpp"
#include "asset_processor_execute.hpp"
#include "asset_processor_project_input.hpp"
#include "asset_processor_text.hpp"

namespace asharia::asset_processor {
    namespace {

        constexpr std::string_view kDefaultMetadataSuffix = ".ameta";

        struct SmokeSourceFixture {
            std::filesystem::path relativePath;
            std::string bytes;
            std::string guidText;
            std::uint64_t metadataSourceHash{};
        };

        struct SmokeWorkspace {
            std::filesystem::path root;

            SmokeWorkspace() = default;
            SmokeWorkspace(const SmokeWorkspace&) = delete;
            SmokeWorkspace& operator=(const SmokeWorkspace&) = delete;

            SmokeWorkspace(SmokeWorkspace&& other) noexcept : root(std::move(other.root)) {
                other.root.clear();
            }

            SmokeWorkspace& operator=(SmokeWorkspace&& other) noexcept {
                if (this != &other) {
                    cleanup();
                    root = std::move(other.root);
                    other.root.clear();
                }
                return *this;
            }

            ~SmokeWorkspace() {
                cleanup();
            }

            void cleanup() noexcept {
                if (root.empty()) {
                    return;
                }

                std::error_code removeError;
                std::filesystem::remove_all(root, removeError);
                root.clear();
            }
        };

        [[nodiscard]] std::optional<SmokeWorkspace> makeSmokeWorkspace() {
            const std::filesystem::path tempRoot = std::filesystem::temp_directory_path();
            const auto seed = static_cast<std::uint64_t>(
                std::chrono::steady_clock::now().time_since_epoch().count());

            for (std::uint64_t attempt = 0; attempt < 32; ++attempt) {
                const std::filesystem::path candidate =
                    tempRoot /
                    ("asharia-asset-processor-smoke-dry-run-" + formatHash64(seed + attempt));
                std::error_code createError;
                if (std::filesystem::create_directory(candidate, createError)) {
                    SmokeWorkspace workspace;
                    workspace.root = candidate;
                    return workspace;
                }
                if (createError) {
                    std::cerr << "Failed to create smoke workspace " << pathText(candidate) << ": "
                              << createError.message() << ".\n";
                }
            }

            std::cerr << "Failed to allocate a unique asset-processor smoke workspace.\n";
            return std::nullopt;
        }

        [[nodiscard]] bool writeTextFile(const std::filesystem::path& path, std::string_view text) {
            std::ofstream stream{path, std::ios::binary};
            if (!stream) {
                std::cerr << "Failed to open smoke file " << pathText(path) << ".\n";
                return false;
            }

            stream << text;
            return static_cast<bool>(stream);
        }

        [[nodiscard]] bool writeBytesFile(const std::filesystem::path& path,
                                          std::span<const std::uint8_t> bytes) {
            std::ofstream stream{path, std::ios::binary};
            if (!stream) {
                std::cerr << "Failed to open smoke file " << pathText(path) << ".\n";
                return false;
            }

            for (const std::uint8_t byte : bytes) {
                stream.put(static_cast<char>(byte));
                if (!stream) {
                    std::cerr << "Failed to write smoke file " << pathText(path) << ".\n";
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] std::optional<std::vector<std::uint8_t>>
        readBytesFile(const std::filesystem::path& path) {
            std::ifstream stream{path, std::ios::binary | std::ios::ate};
            if (!stream) {
                std::cerr << "Failed to open smoke fixture " << pathText(path) << ".\n";
                return std::nullopt;
            }
            const std::streampos end = stream.tellg();
            if (end < 0 || static_cast<std::uint64_t>(end) > 256ULL * 1024ULL * 1024ULL) {
                std::cerr << "Smoke fixture has an invalid or oversized byte length.\n";
                return std::nullopt;
            }
            stream.seekg(0, std::ios::beg);
            std::vector<char> characters(static_cast<std::size_t>(end));
            if (!characters.empty()) {
                stream.read(characters.data(), static_cast<std::streamsize>(characters.size()));
            }
            if (!stream) {
                std::cerr << "Failed to read smoke fixture " << pathText(path) << ".\n";
                return std::nullopt;
            }

            std::vector<std::uint8_t> bytes;
            bytes.reserve(characters.size());
            for (const char character : characters) {
                bytes.push_back(static_cast<std::uint8_t>(static_cast<unsigned char>(character)));
            }
            return bytes;
        }

        [[nodiscard]] bool createDirectories(const std::filesystem::path& path);

        [[nodiscard]] bool hasAtomicTemporaryFile(const std::filesystem::path& root) {
            std::error_code error;
            for (const std::filesystem::directory_entry& entry :
                 std::filesystem::recursive_directory_iterator{root, error}) {
                if (entry.path().filename().string().find(".tmp.") != std::string::npos) {
                    return true;
                }
            }
            return static_cast<bool>(error);
        }

        [[nodiscard]] bool
        prepareManifestReplacementFixture(const std::filesystem::path& outputRoot) {
            if (!createDirectories(outputRoot)) {
                return false;
            }
            const std::filesystem::path manifestPath = outputRoot / "product-manifest.json";
            auto written = asharia::asset::writeAssetProductManifestFile(
                manifestPath, asharia::asset::AssetProductManifestDocument{});
            if (!written) {
                std::cerr << written.error().message << '\n';
                return false;
            }
            return true;
        }

        [[nodiscard]] bool expectReplacedManifestOutput(const std::filesystem::path& outputRoot,
                                                        std::size_t expectedProductCount) {
            const std::filesystem::path manifestPath = outputRoot / "product-manifest.json";
            auto manifest = asharia::asset::readAssetProductManifestFile(manifestPath);
            if (!manifest || manifest->products.size() != expectedProductCount ||
                hasAtomicTemporaryFile(outputRoot)) {
                std::cerr << "asset-processor product execution smoke could not read output "
                             "manifest or found a leaked atomic temporary.\n";
                return false;
            }
            return true;
        }

        [[nodiscard]] std::vector<std::uint8_t> validPngTextureBytes() {
            return {
                0x89U, 0x50U, 0x4EU, 0x47U, 0x0DU, 0x0AU, 0x1AU, 0x0AU, 0x00U, 0x00U, 0x00U, 0x0DU,
                0x49U, 0x48U, 0x44U, 0x52U, 0x00U, 0x00U, 0x00U, 0x01U, 0x00U, 0x00U, 0x00U, 0x01U,
                0x08U, 0x06U, 0x00U, 0x00U, 0x00U, 0x1FU, 0x15U, 0xC4U, 0x89U, 0x00U, 0x00U, 0x00U,
                0x0DU, 0x49U, 0x44U, 0x41U, 0x54U, 0x78U, 0xDAU, 0x63U, 0x10U, 0x50U, 0x30U, 0xF8U,
                0x0FU, 0x00U, 0x02U, 0x04U, 0x01U, 0x60U, 0x52U, 0xE2U, 0xA9U, 0x61U, 0x00U, 0x00U,
                0x00U, 0x00U, 0x49U, 0x45U, 0x4EU, 0x44U, 0xAEU, 0x42U, 0x60U, 0x82U,
            };
        }

        [[nodiscard]] std::uint64_t smokeHashBytes(std::span<const std::uint8_t> bytes) noexcept {
            std::uint64_t hash = 14695981039346656037ULL;
            for (const std::uint8_t byte : bytes) {
                hash ^= byte;
                hash *= 1099511628211ULL;
            }
            return hash;
        }

        [[nodiscard]] bool createDirectories(const std::filesystem::path& path) {
            std::error_code error;
            std::filesystem::create_directories(path, error);
            if (error) {
                std::cerr << "Failed to create smoke directory " << pathText(path) << ": "
                          << error.message() << ".\n";
                return false;
            }
            return true;
        }

        [[nodiscard]] std::filesystem::path metadataSidecarPath(const std::filesystem::path& path) {
            std::filesystem::path metadataPath = path;
            metadataPath += kDefaultMetadataSuffix;
            return metadataPath;
        }

        [[nodiscard]] std::optional<asharia::asset::AssetMetadataDocument>
        makeSmokeMetadataDocument(const SmokeSourceFixture& fixture) {
            auto guid = asharia::asset::parseAssetGuid(fixture.guidText);
            if (!guid) {
                std::cerr << guid.error().message << '\n';
                return std::nullopt;
            }

            std::vector<asharia::asset::AssetImportSetting> settings{
                asharia::asset::AssetImportSetting{
                    .key = "usage",
                    .value = "color",
                },
            };
            const std::string assetTypeName = "com.asharia.asset.Texture2D";
            const std::string importerName = "com.asharia.importer.texture2d";
            const std::string sourcePath = "Content/" + fixture.relativePath.generic_string();

            return asharia::asset::AssetMetadataDocument{
                .source =
                    asharia::asset::SourceAssetRecord{
                        .guid = *guid,
                        .assetType = asharia::asset::makeAssetTypeId(assetTypeName),
                        .assetTypeName = assetTypeName,
                        .sourcePath = sourcePath,
                        .importerId = asharia::asset::makeImporterId(importerName),
                        .importerName = importerName,
                        .importerVersion = asharia::asset::ImporterVersion{1},
                        .sourceHash = fixture.metadataSourceHash,
                        .settingsHash = asharia::asset::hashAssetImportSettings(settings),
                    },
                .settings = std::move(settings),
            };
        }

        [[nodiscard]] bool writeSmokeSource(const std::filesystem::path& contentRoot,
                                            const SmokeSourceFixture& fixture) {
            const std::filesystem::path sourcePath = contentRoot / fixture.relativePath;
            if (!createDirectories(sourcePath.parent_path()) ||
                !writeTextFile(sourcePath, fixture.bytes)) {
                return false;
            }

            std::optional<asharia::asset::AssetMetadataDocument> document =
                makeSmokeMetadataDocument(fixture);
            if (!document) {
                return false;
            }

            auto written =
                asharia::asset::writeAssetMetadataFile(metadataSidecarPath(sourcePath), *document);
            if (!written) {
                std::cerr << written.error().message << '\n';
                return false;
            }

            return true;
        }

        [[nodiscard]] bool writePngTextureSmokeSource(const std::filesystem::path& contentRoot) {
            const std::filesystem::path sourcePath = contentRoot / "Textures" / "Crate.png";
            const std::vector<std::uint8_t> sourceBytes = validPngTextureBytes();
            if (!createDirectories(sourcePath.parent_path()) ||
                !writeBytesFile(sourcePath, sourceBytes)) {
                return false;
            }

            auto guid = asharia::asset::parseAssetGuid("9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c21");
            if (!guid) {
                std::cerr << guid.error().message << '\n';
                return false;
            }

            std::vector<asharia::asset::AssetImportSetting> settings{
                asharia::asset::AssetImportSetting{
                    .key = std::string{asharia::asset::kTextureImportProfileSettingKey},
                    .value = std::string{asharia::asset::kTextureImportProfileTexture2D},
                },
                asharia::asset::AssetImportSetting{
                    .key = std::string{asharia::asset::kTextureImportSettingsVersionSettingKey},
                    .value = std::to_string(asharia::asset::kTextureImportContractSettingsVersion),
                },
                asharia::asset::AssetImportSetting{
                    .key = std::string{asharia::asset::kTextureImportFormatSettingKey},
                    .value = std::string{asharia::asset::kTextureImportFormatRgba8Srgb},
                },
            };
            const asharia::asset::AssetTextureImporterDescriptor importer =
                asharia::asset::makePngTextureImporterDescriptor();
            auto written = asharia::asset::writeAssetMetadataFile(
                metadataSidecarPath(sourcePath),
                asharia::asset::AssetMetadataDocument{
                    .source =
                        asharia::asset::SourceAssetRecord{
                            .guid = *guid,
                            .assetType =
                                asharia::asset::makeAssetTypeId("com.asharia.asset.Texture2D"),
                            .assetTypeName = "com.asharia.asset.Texture2D",
                            .sourcePath = "Content/Textures/Crate.png",
                            .importerId = asharia::asset::makeImporterId(importer.importerName),
                            .importerName = importer.importerName,
                            .importerVersion = importer.importerVersion,
                            .sourceHash = smokeHashBytes(sourceBytes),
                            .settingsHash = asharia::asset::hashAssetImportSettings(settings),
                        },
                    .settings = std::move(settings),
                });
            if (!written) {
                std::cerr << written.error().message << '\n';
                return false;
            }

            return true;
        }

        [[nodiscard]] std::optional<std::vector<std::uint8_t>>
        writeGlbMeshSmokeSource(const std::filesystem::path& contentRoot) {
            const std::filesystem::path fixturePath = std::filesystem::path{"fixtures"} /
                                                      "mesh-product-v1" /
                                                      "restricted-static-mesh.glb";
            auto sourceBytes = readBytesFile(fixturePath);
            const std::filesystem::path sourcePath = contentRoot / "Meshes" / "Fixture.glb";
            if (!sourceBytes || !createDirectories(sourcePath.parent_path()) ||
                !writeBytesFile(sourcePath, *sourceBytes)) {
                return std::nullopt;
            }

            auto guid = asharia::asset::parseAssetGuid("6f5299ad-9c29-47b2-9366-159c00ebfe9c");
            if (!guid) {
                std::cerr << guid.error().message << '\n';
                return std::nullopt;
            }
            const std::vector<asharia::asset::AssetImportSetting> settings;
            const asharia::asset::AssetGlbImporterDescriptor importer =
                asharia::asset::makeRestrictedGlbMeshImporterDescriptor();
            auto written = asharia::asset::writeAssetMetadataFile(
                metadataSidecarPath(sourcePath),
                asharia::asset::AssetMetadataDocument{
                    .source =
                        asharia::asset::SourceAssetRecord{
                            .guid = *guid,
                            .assetType =
                                asharia::asset::makeAssetTypeId(asharia::mesh::kMeshAssetTypeName),
                            .assetTypeName = std::string{asharia::mesh::kMeshAssetTypeName},
                            .sourcePath = "Content/Meshes/Fixture.glb",
                            .importerId = asharia::asset::makeImporterId(importer.importerName),
                            .importerName = importer.importerName,
                            .importerVersion = importer.importerVersion,
                            .sourceHash = smokeHashBytes(*sourceBytes),
                            .settingsHash = asharia::asset::hashAssetImportSettings(settings),
                        },
                    .settings = settings,
                });
            if (!written) {
                std::cerr << written.error().message << '\n';
                return std::nullopt;
            }
            return sourceBytes;
        }

        [[nodiscard]] bool writeSmokeProjectDescriptor(const std::filesystem::path& projectPath) {
            auto projectId =
                asharia::project::parseProjectId("f65d07f1-f0d6-4f4b-9834-13c2bd4d32aa");
            if (!projectId) {
                std::cerr << projectId.error().message << '\n';
                return false;
            }

            auto written = asharia::project::writeAshariaProjectDescriptorFile(
                projectPath, asharia::project::AshariaProjectDescriptor{
                                 .projectName = "AssetProcessorSmoke",
                                 .projectId = *projectId,
                                 .assetSourceRoots =
                                     {
                                         asharia::project::AssetSourceRootDesc{
                                             .rootName = "project-assets",
                                             .directory = "Content",
                                             .sourcePathPrefix = "Content",
                                         },
                                     },
                                 .assetCacheRoot = ".asharia/cache/assets",
                                 .assetDiscovery =
                                     asharia::project::AssetDiscoveryDesc{
                                         .ignoredDirectoryNames = {"Ignored"},
                                     },
                             });
            if (!written) {
                std::cerr << written.error().message << '\n';
                return false;
            }

            return true;
        }

        [[nodiscard]] asharia::asset::AssetProductRecord
        makeProductRecord(const asharia::asset::AssetImportRequest& request) {
            return asharia::asset::AssetProductRecord{
                .key = request.productKey,
                .relativeProductPath = request.relativeProductPath,
                .productSizeBytes = 128,
                .productHash = 0x123456789abcdef0ULL,
            };
        }

        [[nodiscard]] bool containsText(std::string_view text, std::string_view token) {
            return text.find(token) != std::string_view::npos;
        }

        [[nodiscard]] bool expectReportText(std::string_view report, std::string_view token) {
            if (containsText(report, token)) {
                return true;
            }

            std::cerr << "asset-processor smoke missing report token: " << token << "\n" << report;
            return false;
        }

    } // namespace

    int runSmokeDryRun() {
        std::optional<SmokeWorkspace> workspace = makeSmokeWorkspace();
        if (!workspace) {
            return EXIT_FAILURE;
        }

        const std::filesystem::path contentRoot = workspace->root / "Content";
        if (!writeSmokeSource(contentRoot,
                              SmokeSourceFixture{
                                  .relativePath = "Textures/Crate.png",
                                  .bytes = "crate bytes",
                                  .guidText = "9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c21",
                                  .metadataSourceHash = 0x1000f00d1234cafeULL,
                              }) ||
            !writeSmokeSource(contentRoot, SmokeSourceFixture{
                                               .relativePath = "Textures/Decal.png",
                                               .bytes = "decal bytes",
                                               .guidText = "785e2474-65c4-4f28-a8fb-ff8a21449a61",
                                               .metadataSourceHash = 0x2000f00d1234cafeULL,
                                           })) {
            return EXIT_FAILURE;
        }

        const std::filesystem::path countRootA = workspace->root / "CountA";
        const std::filesystem::path countRootB = workspace->root / "CountB";
        if (!writeSmokeSource(countRootA,
                              SmokeSourceFixture{
                                  .relativePath = "First.png",
                                  .bytes = "first",
                                  .guidText = "a39aca3d-2094-4fd7-a19b-814db0709a0d",
                                  .metadataSourceHash = 0x3000f00d1234cafeULL,
                              }) ||
            !writeSmokeSource(countRootB, SmokeSourceFixture{
                                              .relativePath = "Second.png",
                                              .bytes = "second",
                                              .guidText = "67fd6437-c89e-41a4-a2b0-3456feb6fa99",
                                              .metadataSourceHash = 0x4000f00d1234cafeULL,
                                          })) {
            return EXIT_FAILURE;
        }
        const asharia::asset::AssetSourceScanResult combinedScan =
            scanAssetProcessorSourceRoots(AssetProcessorResolvedInput{
                .succeeded = true,
                .projectPath = std::nullopt,
                .projectRoot = {},
                .projectName = {},
                .projectId = {},
                .assetCacheRoot = {},
                .sourceRoots =
                    {
                        AssetProcessorSourceRoot{.rootName = "count-a",
                                                 .sourceRoot = countRootA,
                                                 .directory = "CountA",
                                                 .sourcePathPrefix = "CountA"},
                        AssetProcessorSourceRoot{.rootName = "count-b",
                                                 .sourceRoot = countRootB,
                                                 .directory = "CountB",
                                                 .sourcePathPrefix = "CountB"},
                    },
                .ignoredDirectoryNames = {},
                .error = {},
            });
        if (combinedScan.discoveredFileCount != 4U || combinedScan.entries.size() != 2U) {
            std::cerr << "asset-processor smoke did not aggregate discovered source files\n";
            return EXIT_FAILURE;
        }

        const DryRunOptions emptyManifestOptions{
            .projectPath = std::nullopt,
            .sourceRoot = contentRoot,
            .sourcePathPrefix = "Content",
            .targetProfile = "windows-msvc-debug",
            .productManifestPath = std::nullopt,
            .ignoredDirectoryNames = {"Ignored"},
        };
        const DryRunExecution emptyManifestDryRun = runDryRun(emptyManifestOptions);
        if (emptyManifestDryRun.exitCode != EXIT_SUCCESS ||
            !expectReportText(emptyManifestDryRun.text, "ignoredDirectories=1 \"Ignored\"") ||
            !expectReportText(emptyManifestDryRun.text, "sourceRoots=1") ||
            !expectReportText(emptyManifestDryRun.text,
                              "planning requests=2 cacheHits=0 diagnostics=2") ||
            !expectReportText(emptyManifestDryRun.text,
                              "diagnostic stage=planning severity=Warning "
                              "code=MetadataSourceHashDrift") ||
            !expectReportText(emptyManifestDryRun.text,
                              "import-request source=\"Content/Textures/Crate.png\"") ||
            !expectReportText(emptyManifestDryRun.text,
                              "import-request source=\"Content/Textures/Decal.png\"")) {
            return EXIT_FAILURE;
        }

        const std::filesystem::path projectPath =
            workspace->root / std::string{asharia::project::kDefaultAshariaProjectFileName};
        if (!writeSmokeProjectDescriptor(projectPath)) {
            return EXIT_FAILURE;
        }

        const DryRunExecution projectDryRun = runDryRun(DryRunOptions{
            .projectPath = projectPath,
            .sourceRoot = {},
            .sourcePathPrefix = {},
            .targetProfile = "windows-msvc-debug",
            .productManifestPath = std::nullopt,
            .ignoredDirectoryNames = {},
        });
        if (projectDryRun.exitCode != EXIT_SUCCESS ||
            !expectReportText(projectDryRun.text, "projectName=\"AssetProcessorSmoke\"") ||
            !expectReportText(projectDryRun.text, "source-root rootName=\"project-assets\"") ||
            !expectReportText(projectDryRun.text, "ignoredDirectories=1 \"Ignored\"") ||
            !expectReportText(projectDryRun.text, "planning requests=2 cacheHits=0")) {
            return EXIT_FAILURE;
        }

        const asharia::asset::AssetScannedImportPlanResult firstPlan =
            asharia::asset::planScannedAssetImports(asharia::asset::AssetScannedImportPlanRequest{
                .scan =
                    asharia::asset::AssetSourceScanRequest{
                        .sourceRoot = contentRoot,
                        .sourcePathPrefix = "Content",
                        .metadataSuffix = std::string{kDefaultMetadataSuffix},
                        .ignoredDirectoryNames = {},
                    },
                .productManifest = {},
                .targetProfile = "windows-msvc-debug",
                .toolVersions = {},
            });
        if (!firstPlan.succeeded() || firstPlan.plan.requests.empty()) {
            std::cerr << "asset-processor smoke could not build manifest fixture.\n";
            return EXIT_FAILURE;
        }

        const std::filesystem::path manifestPath = workspace->root / "product-manifest.json";
        auto writtenManifest = asharia::asset::writeAssetProductManifestFile(
            manifestPath, asharia::asset::AssetProductManifestDocument{
                              .products = {makeProductRecord(firstPlan.plan.requests.front())},
                          });
        if (!writtenManifest) {
            std::cerr << writtenManifest.error().message << '\n';
            return EXIT_FAILURE;
        }

        const DryRunExecution manifestDryRun = runDryRun(DryRunOptions{
            .projectPath = std::nullopt,
            .sourceRoot = contentRoot,
            .sourcePathPrefix = "Content",
            .targetProfile = "windows-msvc-debug",
            .productManifestPath = manifestPath,
            .ignoredDirectoryNames = {},
        });
        if (manifestDryRun.exitCode != EXIT_SUCCESS ||
            !expectReportText(manifestDryRun.text, "planning requests=1 cacheHits=1") ||
            !expectReportText(manifestDryRun.text,
                              "cache-hit source=\"Content/Textures/Crate.png\"")) {
            return EXIT_FAILURE;
        }

        const DryRunExecution invalidRootDryRun = runDryRun(DryRunOptions{
            .projectPath = std::nullopt,
            .sourceRoot = workspace->root / "MissingContent",
            .sourcePathPrefix = "Content",
            .targetProfile = "windows-msvc-debug",
            .productManifestPath = std::nullopt,
            .ignoredDirectoryNames = {},
        });
        if (invalidRootDryRun.exitCode == EXIT_SUCCESS ||
            !expectReportText(invalidRootDryRun.text, "diagnostic stage=scan")) {
            return EXIT_FAILURE;
        }

        const std::filesystem::path badManifestPath = workspace->root / "bad-product-manifest.json";
        if (!writeTextFile(badManifestPath, "{")) {
            return EXIT_FAILURE;
        }
        const DryRunExecution badManifestDryRun = runDryRun(DryRunOptions{
            .projectPath = std::nullopt,
            .sourceRoot = contentRoot,
            .sourcePathPrefix = "Content",
            .targetProfile = "windows-msvc-debug",
            .productManifestPath = badManifestPath,
            .ignoredDirectoryNames = {},
        });
        if (badManifestDryRun.exitCode == EXIT_SUCCESS ||
            !expectReportText(badManifestDryRun.text, "diagnostic stage=product-manifest")) {
            return EXIT_FAILURE;
        }

        std::cout << "asset-processor dry-run smoke passed\n";
        return EXIT_SUCCESS;
    }

    namespace {

        [[nodiscard]] int runBasicProductExecutionSmoke() {
            std::optional<SmokeWorkspace> workspace = makeSmokeWorkspace();
            if (!workspace) {
                return EXIT_FAILURE;
            }

            const std::filesystem::path contentRoot = workspace->root / "Content";
            if (!writeSmokeSource(contentRoot,
                                  SmokeSourceFixture{
                                      .relativePath = "Textures/Crate.png",
                                      .bytes = "crate bytes",
                                      .guidText = "9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c21",
                                      .metadataSourceHash = 0x1000f00d1234cafeULL,
                                  }) ||
                !writeSmokeSource(contentRoot,
                                  SmokeSourceFixture{
                                      .relativePath = "Textures/Decal.png",
                                      .bytes = "decal bytes",
                                      .guidText = "785e2474-65c4-4f28-a8fb-ff8a21449a61",
                                      .metadataSourceHash = 0x2000f00d1234cafeULL,
                                  })) {
                return EXIT_FAILURE;
            }

            const std::filesystem::path outputRoot = workspace->root / "ProductCache";
            const std::filesystem::path manifestPath = outputRoot / "product-manifest.json";
            if (!prepareManifestReplacementFixture(outputRoot)) {
                return EXIT_FAILURE;
            }
            const ProductExecution firstExecution = runProductExecution(ProductExecutionOptions{
                .sourceRoot = contentRoot,
                .sourcePathPrefix = "Content",
                .targetProfile = "windows-msvc-debug",
                .outputRoot = outputRoot,
                .productManifestPath = std::nullopt,
                .productManifestOutputPath = manifestPath,
                .ignoredDirectoryNames = {},
                .projectPath = std::nullopt,
            });
            if (firstExecution.exitCode != EXIT_SUCCESS ||
                !expectReportText(firstExecution.text, "asset-processor execute") ||
                !expectReportText(firstExecution.text,
                                  "planning requests=2 cacheHits=0 diagnostics=2") ||
                !expectReportText(firstExecution.text, "diagnostic stage=planning severity=Warning "
                                                       "code=MetadataSourceHashDrift") ||
                !expectReportText(
                    firstExecution.text,
                    "execution written=2 cacheHits=0 diagnostics=0 manifestProducts=2 "
                    "manifestWritten=true") ||
                !expectReportText(firstExecution.text,
                                  "product-written source=\"Content/Textures/Crate.png\"") ||
                !expectReportText(firstExecution.text,
                                  "product-written source=\"Content/Textures/Decal.png\"")) {
                return EXIT_FAILURE;
            }

            if (!expectReplacedManifestOutput(outputRoot, 2U)) {
                return EXIT_FAILURE;
            }

            const ProductExecution cacheHitExecution = runProductExecution(ProductExecutionOptions{
                .sourceRoot = contentRoot,
                .sourcePathPrefix = "Content",
                .targetProfile = "windows-msvc-debug",
                .outputRoot = outputRoot,
                .productManifestPath = manifestPath,
                .productManifestOutputPath = manifestPath,
                .ignoredDirectoryNames = {},
                .projectPath = std::nullopt,
            });
            if (cacheHitExecution.exitCode != EXIT_SUCCESS ||
                !expectReportText(cacheHitExecution.text, "planning requests=0 cacheHits=2") ||
                !expectReportText(
                    cacheHitExecution.text,
                    "execution written=0 cacheHits=2 diagnostics=0 manifestProducts=2 "
                    "manifestWritten=true") ||
                !expectReportText(cacheHitExecution.text,
                                  "cache-hit source=\"Content/Textures/Crate.png\"") ||
                !expectReportText(cacheHitExecution.text,
                                  "cache-hit source=\"Content/Textures/Decal.png\"")) {
                return EXIT_FAILURE;
            }

            const std::filesystem::path projectPath =
                workspace->root / std::string{asharia::project::kDefaultAshariaProjectFileName};
            if (!writeSmokeProjectDescriptor(projectPath)) {
                return EXIT_FAILURE;
            }

            const ProductExecution projectExecution = runProductExecution(ProductExecutionOptions{
                .sourceRoot = {},
                .sourcePathPrefix = {},
                .targetProfile = "windows-msvc-debug",
                .outputRoot = {},
                .productManifestPath = std::nullopt,
                .productManifestOutputPath = {},
                .ignoredDirectoryNames = {},
                .projectPath = projectPath,
            });
            if (projectExecution.exitCode != EXIT_SUCCESS ||
                !expectReportText(projectExecution.text, "projectPath=") ||
                !expectReportText(projectExecution.text,
                                  "assetCacheRoot=\".asharia/cache/assets\"") ||
                !expectReportText(projectExecution.text,
                                  "source-root rootName=\"project-assets\"") ||
                !expectReportText(projectExecution.text, "planning requests=2 cacheHits=0") ||
                !expectReportText(
                    projectExecution.text,
                    "execution written=2 cacheHits=0 diagnostics=0 manifestProducts=2 "
                    "manifestWritten=true")) {
                return EXIT_FAILURE;
            }

            const ProductExecution projectCacheHitExecution =
                runProductExecution(ProductExecutionOptions{
                    .sourceRoot = {},
                    .sourcePathPrefix = {},
                    .targetProfile = "windows-msvc-debug",
                    .outputRoot = {},
                    .productManifestPath = std::nullopt,
                    .productManifestOutputPath = {},
                    .ignoredDirectoryNames = {},
                    .projectPath = projectPath,
                });
            if (projectCacheHitExecution.exitCode != EXIT_SUCCESS ||
                !expectReportText(projectCacheHitExecution.text, "productManifest=") ||
                !expectReportText(projectCacheHitExecution.text, "products.aproducts.json") ||
                !expectReportText(projectCacheHitExecution.text,
                                  "planning requests=0 cacheHits=2") ||
                !expectReportText(
                    projectCacheHitExecution.text,
                    "execution written=0 cacheHits=2 diagnostics=0 manifestProducts=2 "
                    "manifestWritten=true")) {
                return EXIT_FAILURE;
            }

            const std::filesystem::path cratePath = contentRoot / "Textures" / "Crate.png";
            if (!writeTextFile(cratePath, "crate bytes v2")) {
                return EXIT_FAILURE;
            }

            const ProductExecution changedExecution = runProductExecution(ProductExecutionOptions{
                .sourceRoot = contentRoot,
                .sourcePathPrefix = "Content",
                .targetProfile = "windows-msvc-debug",
                .outputRoot = outputRoot,
                .productManifestPath = manifestPath,
                .productManifestOutputPath = manifestPath,
                .ignoredDirectoryNames = {},
                .projectPath = std::nullopt,
            });
            if (changedExecution.exitCode != EXIT_SUCCESS ||
                !expectReportText(changedExecution.text, "planning requests=1 cacheHits=1") ||
                !expectReportText(
                    changedExecution.text,
                    "execution written=1 cacheHits=1 diagnostics=0 manifestProducts=3 "
                    "manifestWritten=true") ||
                !expectReportText(changedExecution.text,
                                  "product-written source=\"Content/Textures/Crate.png\"") ||
                !expectReportText(changedExecution.text,
                                  "cache-hit source=\"Content/Textures/Decal.png\"")) {
                return EXIT_FAILURE;
            }

            return EXIT_SUCCESS;
        }

        [[nodiscard]] int runPngProductExecutionSmoke() {
            std::optional<SmokeWorkspace> pngWorkspace = makeSmokeWorkspace();
            if (!pngWorkspace) {
                return EXIT_FAILURE;
            }
            const std::filesystem::path pngContentRoot = pngWorkspace->root / "Content";
            if (!writePngTextureSmokeSource(pngContentRoot)) {
                return EXIT_FAILURE;
            }

            const std::filesystem::path pngOutputRoot = pngWorkspace->root / "PngProductCache";
            const std::filesystem::path pngManifestPath = pngOutputRoot / "product-manifest.json";
            const ProductExecution pngExecution = runProductExecution(ProductExecutionOptions{
                .sourceRoot = pngContentRoot,
                .sourcePathPrefix = "Content",
                .targetProfile = "windows-msvc-debug",
                .outputRoot = pngOutputRoot,
                .productManifestPath = std::nullopt,
                .productManifestOutputPath = pngManifestPath,
                .ignoredDirectoryNames = {},
                .projectPath = std::nullopt,
            });
            if (pngExecution.exitCode != EXIT_SUCCESS ||
                !expectReportText(pngExecution.text,
                                  "planning requests=1 cacheHits=0 diagnostics=0") ||
                !expectReportText(
                    pngExecution.text,
                    "execution written=1 cacheHits=0 diagnostics=0 manifestProducts=1 "
                    "manifestWritten=true") ||
                !expectReportText(pngExecution.text,
                                  "product-written source=\"Content/Textures/Crate.png\"")) {
                return EXIT_FAILURE;
            }

            auto pngManifest = asharia::asset::readAssetProductManifestFile(pngManifestPath);
            if (!pngManifest || pngManifest->products.size() != 1U) {
                std::cerr << "asset-processor product execution smoke could not read PNG product "
                             "manifest.\n";
                return EXIT_FAILURE;
            }
            const asharia::asset::AssetProductRecord& pngProduct = pngManifest->products.front();
            auto texturePayload = asharia::asset::readTexture2DProductPayload(
                asharia::asset::AssetProductBlobReadRequest{
                    .productFilePath = pngOutputRoot / pngProduct.relativeProductPath,
                    .relativeProductPath = pngProduct.relativeProductPath,
                });
            const std::vector<std::uint8_t> expectedPngPayload{0x10U, 0x20U, 0x30U, 0xFFU};
            if (!texturePayload || texturePayload->width != 1U || texturePayload->height != 1U ||
                texturePayload->format != asharia::asset::AssetTextureImportFormat::Rgba8Srgb ||
                texturePayload->payload != expectedPngPayload) {
                std::cerr << "asset-processor product execution smoke could not read PNG texture "
                             "product payload.\n";
                return EXIT_FAILURE;
            }

            return EXIT_SUCCESS;
        }

        [[nodiscard]] bool expectGlbMeshProduct(const std::filesystem::path& productPath) {
            auto meshProduct = asharia::mesh::readMeshProductV1File(productPath);
            if (meshProduct && meshProduct->vertices().size() == 11U &&
                meshProduct->indices().size() == 9U && meshProduct->submeshes().size() == 3U &&
                meshProduct->materialSlots().size() == 3U &&
                meshProduct->bounds() == asharia::mesh::MeshAabbV1{
                                             .minX = -2.0F,
                                             .minY = 0.0F,
                                             .minZ = 0.0F,
                                             .maxX = 2.0F,
                                             .maxY = 1.0F,
                                             .maxZ = 1.0F,
                                         }) {
                return true;
            }

            std::cerr << "asset-processor product execution smoke could not round-trip Mesh "
                         "Product v1";
            if (!meshProduct) {
                std::cerr << ": " << meshProduct.error().message;
            } else {
                const asharia::mesh::MeshAabbV1 bounds = meshProduct->bounds();
                std::cerr << " counts=" << meshProduct->vertices().size() << "/"
                          << meshProduct->indices().size() << "/" << meshProduct->submeshes().size()
                          << "/" << meshProduct->materialSlots().size() << " bounds=("
                          << bounds.minX << "," << bounds.minY << "," << bounds.minZ << ")..("
                          << bounds.maxX << "," << bounds.maxY << "," << bounds.maxZ << ")";
            }
            std::cerr << ".\n";
            return false;
        }

        [[nodiscard]] bool
        expectGlbMeshResource(const std::filesystem::path& productRoot,
                              const asharia::asset::AssetProductRecord& product) {
            auto store = asharia::resource::MeshResourceStore::create(
                {.artifactRoot = productRoot, .artifactLimits = {}, .meshLimits = {}});
            if (!store) {
                std::cerr << store.error().message << '\n';
                return false;
            }

            const asharia::resource::MeshResourceKey resourceKey{
                .guid = product.key.guid,
                .assetType = product.key.assetType,
            };
            auto request = store->request(resourceKey, product.key, std::span{&product, 1U});
            if (!request || !request->loadPlan ||
                request->disposition !=
                    asharia::resource::MeshResourceRequestDisposition::LoadQueued) {
                std::cerr << (request ? "Mesh resource smoke did not queue a load.\n"
                                      : request.error().message + "\n");
                return false;
            }

            auto published =
                store->publish(asharia::resource::loadMeshResourceCandidate(*request->loadPlan));
            auto lease = store->acquire(request->handle);
            if (published && published->state == asharia::resource::MeshResourceState::Ready &&
                published->activeRevision == 1U && lease &&
                lease->product().vertices().size() == 11U &&
                lease->product().indices().size() == 9U &&
                lease->product().submeshes().size() == 3U &&
                lease->product().materialSlots().size() == 3U &&
                lease->product().bounds() == asharia::mesh::MeshAabbV1{
                                                 .minX = -2.0F,
                                                 .minY = 0.0F,
                                                 .minZ = 0.0F,
                                                 .maxX = 2.0F,
                                                 .maxY = 1.0F,
                                                 .maxZ = 1.0F,
                                             }) {
                return true;
            }

            std::cerr << "asset-processor Mesh Product v1 could not become a typed runtime "
                         "resource";
            if (!published) {
                std::cerr << ": " << published.error().message;
            } else if (!lease) {
                std::cerr << ": " << lease.error().message;
            }
            std::cerr << ".\n";
            return false;
        }

        [[nodiscard]] int runGlbProductExecutionSmoke() {
            std::optional<SmokeWorkspace> glbWorkspace = makeSmokeWorkspace();
            if (!glbWorkspace) {
                return EXIT_FAILURE;
            }
            const std::filesystem::path glbContentRoot = glbWorkspace->root / "Content";
            const auto glbSourceBytes = writeGlbMeshSmokeSource(glbContentRoot);
            if (!glbSourceBytes) {
                return EXIT_FAILURE;
            }

            const std::filesystem::path glbOutputRoot = glbWorkspace->root / "GlbProductCache";
            const std::filesystem::path glbManifestPath = glbOutputRoot / "product-manifest.json";
            const ProductExecution glbExecution = runProductExecution(ProductExecutionOptions{
                .sourceRoot = glbContentRoot,
                .sourcePathPrefix = "Content",
                .targetProfile = "windows-msvc-debug",
                .outputRoot = glbOutputRoot,
                .productManifestPath = std::nullopt,
                .productManifestOutputPath = glbManifestPath,
                .ignoredDirectoryNames = {},
                .projectPath = std::nullopt,
            });
            if (glbExecution.exitCode != EXIT_SUCCESS) {
                std::cerr << glbExecution.text;
                return EXIT_FAILURE;
            }
            if (!expectReportText(glbExecution.text,
                                  "planning requests=1 cacheHits=0 diagnostics=0") ||
                !expectReportText(
                    glbExecution.text,
                    "execution written=1 cacheHits=0 diagnostics=0 manifestProducts=1 "
                    "manifestWritten=true") ||
                !expectReportText(glbExecution.text,
                                  "product-written source=\"Content/Meshes/Fixture.glb\"")) {
                return EXIT_FAILURE;
            }

            auto glbManifest = asharia::asset::readAssetProductManifestFile(glbManifestPath);
            if (!glbManifest || glbManifest->products.size() != 1U) {
                std::cerr << "asset-processor product execution smoke could not read GLB product "
                             "manifest.\n";
                return EXIT_FAILURE;
            }
            const asharia::asset::AssetProductRecord& glbProduct = glbManifest->products.front();
            const std::filesystem::path glbProductPath =
                glbOutputRoot / glbProduct.relativeProductPath;
            if (!expectGlbMeshProduct(glbProductPath) ||
                !expectGlbMeshResource(glbOutputRoot, glbProduct)) {
                return EXIT_FAILURE;
            }

            const auto firstGlbProductBytes = readBytesFile(glbProductPath);
            auto firstGlbManifestText = asharia::asset::writeAssetProductManifestText(*glbManifest);
            if (!firstGlbProductBytes || !firstGlbManifestText) {
                std::cerr
                    << "asset-processor product execution smoke could not capture deterministic "
                       "GLB outputs.\n";
                return EXIT_FAILURE;
            }

            const std::filesystem::path deterministicOutputRoot =
                glbWorkspace->root / "GlbProductCacheRepeat";
            const std::filesystem::path deterministicManifestPath =
                deterministicOutputRoot / "product-manifest.json";
            const ProductExecution deterministicGlbExecution =
                runProductExecution(ProductExecutionOptions{
                    .sourceRoot = glbContentRoot,
                    .sourcePathPrefix = "Content",
                    .targetProfile = "windows-msvc-debug",
                    .outputRoot = deterministicOutputRoot,
                    .productManifestPath = std::nullopt,
                    .productManifestOutputPath = deterministicManifestPath,
                    .ignoredDirectoryNames = {},
                    .projectPath = std::nullopt,
                });
            auto deterministicManifest =
                asharia::asset::readAssetProductManifestFile(deterministicManifestPath);
            if (deterministicGlbExecution.exitCode != EXIT_SUCCESS) {
                std::cerr << deterministicGlbExecution.text;
                return EXIT_FAILURE;
            }
            if (!deterministicManifest || deterministicManifest->products.size() != 1U) {
                std::cerr
                    << "asset-processor product execution smoke failed deterministic GLB rerun.\n";
                return EXIT_FAILURE;
            }
            const auto deterministicProductBytes =
                readBytesFile(deterministicOutputRoot /
                              deterministicManifest->products.front().relativeProductPath);
            auto deterministicManifestText =
                asharia::asset::writeAssetProductManifestText(*deterministicManifest);
            if (!deterministicProductBytes || !deterministicManifestText ||
                *deterministicProductBytes != *firstGlbProductBytes ||
                *deterministicManifestText != *firstGlbManifestText) {
                std::cerr
                    << "asset-processor GLB artifact or manifest was not byte deterministic.\n";
                return EXIT_FAILURE;
            }

            const ProductExecution glbCacheHit = runProductExecution(ProductExecutionOptions{
                .sourceRoot = glbContentRoot,
                .sourcePathPrefix = "Content",
                .targetProfile = "windows-msvc-debug",
                .outputRoot = glbOutputRoot,
                .productManifestPath = glbManifestPath,
                .productManifestOutputPath = glbManifestPath,
                .ignoredDirectoryNames = {},
                .projectPath = std::nullopt,
            });
            if (glbCacheHit.exitCode != EXIT_SUCCESS) {
                std::cerr << glbCacheHit.text;
                return EXIT_FAILURE;
            }
            if (!expectReportText(glbCacheHit.text, "planning requests=0 cacheHits=1") ||
                !expectReportText(
                    glbCacheHit.text,
                    "execution written=0 cacheHits=1 diagnostics=0 manifestProducts=1 "
                    "manifestWritten=true")) {
                return EXIT_FAILURE;
            }

            const auto lastKnownGoodProductBytes = readBytesFile(glbProductPath);
            const auto lastKnownGoodManifestBytes = readBytesFile(glbManifestPath);
            const std::filesystem::path glbSourcePath = glbContentRoot / "Meshes" / "Fixture.glb";
            const std::vector<std::uint8_t> malformedGlb{0x67U, 0x6CU, 0x54U, 0x46U};
            if (!lastKnownGoodProductBytes || !lastKnownGoodManifestBytes ||
                !writeBytesFile(glbSourcePath, malformedGlb)) {
                return EXIT_FAILURE;
            }
            const ProductExecution malformedGlbExecution =
                runProductExecution(ProductExecutionOptions{
                    .sourceRoot = glbContentRoot,
                    .sourcePathPrefix = "Content",
                    .targetProfile = "windows-msvc-debug",
                    .outputRoot = glbOutputRoot,
                    .productManifestPath = glbManifestPath,
                    .productManifestOutputPath = glbManifestPath,
                    .ignoredDirectoryNames = {},
                    .projectPath = std::nullopt,
                });
            const auto preservedProductBytes = readBytesFile(glbProductPath);
            const auto preservedManifestBytes = readBytesFile(glbManifestPath);
            if (malformedGlbExecution.exitCode == EXIT_SUCCESS ||
                !expectReportText(malformedGlbExecution.text, "code=MeshImportFailed") ||
                !preservedProductBytes || !preservedManifestBytes ||
                *preservedProductBytes != *lastKnownGoodProductBytes ||
                *preservedManifestBytes != *lastKnownGoodManifestBytes) {
                std::cerr
                    << "asset-processor malformed GLB did not preserve last-known-good output.\n";
                return EXIT_FAILURE;
            }

            return EXIT_SUCCESS;
        }

    } // namespace

    int runSmokeProductExecution() {
        if (runBasicProductExecutionSmoke() != EXIT_SUCCESS ||
            runPngProductExecutionSmoke() != EXIT_SUCCESS ||
            runGlbProductExecutionSmoke() != EXIT_SUCCESS) {
            return EXIT_FAILURE;
        }

        std::cout << "asset-processor product execution smoke passed\n";
        return EXIT_SUCCESS;
    }

    int runSmokeMeshResource() {
        if (runGlbProductExecutionSmoke() != EXIT_SUCCESS) {
            return EXIT_FAILURE;
        }

        std::cout << "asset-processor mesh resource smoke passed\n";
        return EXIT_SUCCESS;
    }

} // namespace asharia::asset_processor
