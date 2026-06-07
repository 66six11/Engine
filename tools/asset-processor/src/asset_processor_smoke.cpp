#include "asset_processor_smoke.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
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
#include "asharia/asset_pipeline/asset_product_manifest_io.hpp"
#include "asharia/asset_pipeline/asset_scanned_import_planning.hpp"
#include "asharia/project/project_descriptor_io.hpp"

#include "asset_processor_dry_run.hpp"
#include "asset_processor_execute.hpp"
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

    int runSmokeProductExecution() {
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

        const std::filesystem::path outputRoot = workspace->root / "ProductCache";
        const std::filesystem::path manifestPath = outputRoot / "product-manifest.json";
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
            !expectReportText(firstExecution.text,
                              "diagnostic stage=planning severity=Warning "
                              "code=MetadataSourceHashDrift") ||
            !expectReportText(firstExecution.text,
                              "execution written=2 cacheHits=0 diagnostics=0 manifestProducts=2 "
                              "manifestWritten=true") ||
            !expectReportText(firstExecution.text,
                              "product-written source=\"Content/Textures/Crate.png\"") ||
            !expectReportText(firstExecution.text,
                              "product-written source=\"Content/Textures/Decal.png\"")) {
            return EXIT_FAILURE;
        }

        auto manifest = asharia::asset::readAssetProductManifestFile(manifestPath);
        if (!manifest || manifest->products.size() != 2) {
            std::cerr << "asset-processor product execution smoke could not read output "
                         "manifest.\n";
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
            !expectReportText(cacheHitExecution.text,
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
            !expectReportText(projectExecution.text, "assetCacheRoot=\".asharia/cache/assets\"") ||
            !expectReportText(projectExecution.text,
                              "source-root rootName=\"project-assets\"") ||
            !expectReportText(projectExecution.text, "planning requests=2 cacheHits=0") ||
            !expectReportText(projectExecution.text,
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
            !expectReportText(projectCacheHitExecution.text,
                              "productManifest=") ||
            !expectReportText(projectCacheHitExecution.text,
                              "products.aproducts.json") ||
            !expectReportText(projectCacheHitExecution.text,
                              "planning requests=0 cacheHits=2") ||
            !expectReportText(projectCacheHitExecution.text,
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
            !expectReportText(changedExecution.text,
                              "execution written=1 cacheHits=1 diagnostics=0 manifestProducts=3 "
                              "manifestWritten=true") ||
            !expectReportText(changedExecution.text,
                              "product-written source=\"Content/Textures/Crate.png\"") ||
            !expectReportText(changedExecution.text,
                              "cache-hit source=\"Content/Textures/Decal.png\"")) {
            return EXIT_FAILURE;
        }

        std::cout << "asset-processor product execution smoke passed\n";
        return EXIT_SUCCESS;
    }

} // namespace asharia::asset_processor
