#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <sstream>
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

namespace {

    constexpr std::string_view kDefaultMetadataSuffix = ".ameta";

    enum class CommandKind : std::uint8_t {
        Help,
        DryRun,
        SmokeDryRun,
    };

    enum class DryRunOptionKind : std::uint8_t {
        SourceRoot,
        SourcePathPrefix,
        TargetProfile,
        ProductManifest,
        IgnoreDirectory,
        Unknown,
    };

    struct DryRunOptions {
        std::filesystem::path sourceRoot;
        std::string sourcePathPrefix;
        std::string targetProfile;
        std::optional<std::filesystem::path> productManifestPath;
        std::vector<std::string> ignoredDirectoryNames;
    };

    struct ParsedArguments {
        CommandKind command{CommandKind::Help};
        DryRunOptions dryRun;
        std::string error;
    };

    struct ArgumentValueResult {
        bool succeeded{};
        std::string value;
        std::string error;
    };

    struct ManifestLoadResult {
        bool succeeded{};
        asharia::asset::AssetProductManifestDocument manifest;
        std::string error;
    };

    struct DryRunExecution {
        int exitCode{EXIT_FAILURE};
        std::string text;
    };

    struct SmokeSourceFixture {
        std::filesystem::path relativePath;
        std::string bytes;
        std::string guidText;
        std::uint64_t metadataSourceHash{};
    };

    [[nodiscard]] std::string pathText(const std::filesystem::path& path) {
        return path.generic_string();
    }

    [[nodiscard]] std::string escaped(std::string_view text) {
        std::string result;
        result.reserve(text.size());
        for (const char character : text) {
            switch (character) {
            case '\\':
                result += "\\\\";
                break;
            case '"':
                result += "\\\"";
                break;
            case '\n':
                result += "\\n";
                break;
            case '\r':
                result += "\\r";
                break;
            case '\t':
                result += "\\t";
                break;
            default:
                result += character;
                break;
            }
        }
        return result;
    }

    [[nodiscard]] std::string quoteText(std::string_view text) {
        return "\"" + escaped(text) + "\"";
    }

    [[nodiscard]] std::string quotePath(const std::filesystem::path& path) {
        return quoteText(pathText(path));
    }

    [[nodiscard]] std::string formatHash64(std::uint64_t value) {
        constexpr std::string_view kHexDigits = "0123456789abcdef";
        std::string text(16, '0');
        for (std::size_t index = 0; index < text.size(); ++index) {
            const auto shift = static_cast<std::uint32_t>((text.size() - index - 1) * 4);
            text[index] = kHexDigits[(value >> shift) & 0xFU];
        }
        return text;
    }

    [[nodiscard]] std::string toText(asharia::asset::AssetSourceScanDiagnosticCode code) {
        using Code = asharia::asset::AssetSourceScanDiagnosticCode;
        switch (code) {
        case Code::InvalidRequest:
            return "InvalidRequest";
        case Code::InvalidRoot:
            return "InvalidRoot";
        case Code::FilesystemError:
            return "FilesystemError";
        case Code::InvalidSourcePath:
            return "InvalidSourcePath";
        case Code::DuplicateSourcePath:
            return "DuplicateSourcePath";
        case Code::DuplicateMetadataPath:
            return "DuplicateMetadataPath";
        case Code::MissingMetadata:
            return "MissingMetadata";
        case Code::OrphanMetadata:
            return "OrphanMetadata";
        }
        return "Unknown";
    }

    [[nodiscard]] std::string toText(asharia::asset::AssetSourceDiscoveryDiagnosticCode code) {
        using Code = asharia::asset::AssetSourceDiscoveryDiagnosticCode;
        switch (code) {
        case Code::InvalidEntry:
            return "InvalidEntry";
        case Code::MissingMetadata:
            return "MissingMetadata";
        case Code::MetadataReadFailed:
            return "MetadataReadFailed";
        case Code::SourcePathMismatch:
            return "SourcePathMismatch";
        case Code::DuplicateGuid:
            return "DuplicateGuid";
        case Code::DuplicateSourcePath:
            return "DuplicateSourcePath";
        case Code::CatalogRejected:
            return "CatalogRejected";
        }
        return "Unknown";
    }

    [[nodiscard]] std::string toText(asharia::asset::AssetSourceSnapshotDiagnosticCode code) {
        using Code = asharia::asset::AssetSourceSnapshotDiagnosticCode;
        switch (code) {
        case Code::InvalidEntry:
            return "InvalidEntry";
        case Code::MissingSourceFile:
            return "MissingSourceFile";
        case Code::SourceFileNotRegular:
            return "SourceFileNotRegular";
        case Code::SourceFileReadFailed:
            return "SourceFileReadFailed";
        case Code::DuplicateSourcePath:
            return "DuplicateSourcePath";
        }
        return "Unknown";
    }

    [[nodiscard]] std::string toText(asharia::asset::AssetImportPlanDiagnosticCode code) {
        using Code = asharia::asset::AssetImportPlanDiagnosticCode;
        switch (code) {
        case Code::InvalidTargetProfile:
            return "InvalidTargetProfile";
        case Code::InvalidSource:
            return "InvalidSource";
        case Code::InvalidSourceSnapshot:
            return "InvalidSourceSnapshot";
        case Code::MissingSourceSnapshot:
            return "MissingSourceSnapshot";
        case Code::DuplicateSource:
            return "DuplicateSource";
        case Code::DuplicateSourceSnapshot:
            return "DuplicateSourceSnapshot";
        case Code::InvalidProductManifest:
            return "InvalidProductManifest";
        }
        return "Unknown";
    }

    [[nodiscard]] std::string toText(asharia::asset::AssetImportRequestReason reason) {
        using Reason = asharia::asset::AssetImportRequestReason;
        switch (reason) {
        case Reason::MissingProduct:
            return "MissingProduct";
        case Reason::SourceChanged:
            return "SourceChanged";
        case Reason::SettingsChanged:
            return "SettingsChanged";
        case Reason::ImporterChanged:
            return "ImporterChanged";
        case Reason::AssetTypeChanged:
            return "AssetTypeChanged";
        case Reason::DependencyChanged:
            return "DependencyChanged";
        case Reason::TargetProfileChanged:
            return "TargetProfileChanged";
        }
        return "Unknown";
    }

    [[nodiscard]] ArgumentValueResult readArgumentValue(std::span<char* const> arguments,
                                                        std::size_t& index,
                                                        std::string_view optionName) {
        if (index + 1 >= arguments.size()) {
            return ArgumentValueResult{
                .succeeded = false,
                .value = {},
                .error = "Missing value for option " + std::string{optionName} + ".",
            };
        }

        ++index;
        return ArgumentValueResult{
            .succeeded = true,
            .value = arguments[index],
            .error = {},
        };
    }

    [[nodiscard]] DryRunOptionKind classifyDryRunOption(std::string_view option) noexcept {
        if (option == "--source-root") {
            return DryRunOptionKind::SourceRoot;
        }
        if (option == "--source-path-prefix") {
            return DryRunOptionKind::SourcePathPrefix;
        }
        if (option == "--target-profile") {
            return DryRunOptionKind::TargetProfile;
        }
        if (option == "--product-manifest") {
            return DryRunOptionKind::ProductManifest;
        }
        if (option == "--ignore-dir") {
            return DryRunOptionKind::IgnoreDirectory;
        }
        return DryRunOptionKind::Unknown;
    }

    [[nodiscard]] bool applyDryRunOption(DryRunOptions& options, DryRunOptionKind option,
                                         std::string value) {
        switch (option) {
        case DryRunOptionKind::SourceRoot:
            options.sourceRoot = std::filesystem::path{value};
            return true;
        case DryRunOptionKind::SourcePathPrefix:
            options.sourcePathPrefix = std::move(value);
            return true;
        case DryRunOptionKind::TargetProfile:
            options.targetProfile = std::move(value);
            return true;
        case DryRunOptionKind::ProductManifest:
            if (!value.empty()) {
                options.productManifestPath = std::filesystem::path{value};
            }
            return true;
        case DryRunOptionKind::IgnoreDirectory:
            options.ignoredDirectoryNames.push_back(std::move(value));
            return true;
        case DryRunOptionKind::Unknown:
            return false;
        }
        return false;
    }

    [[nodiscard]] ParsedArguments parseDryRunArguments(std::span<char* const> arguments) {
        ParsedArguments parsed{
            .command = CommandKind::DryRun,
            .dryRun = {},
            .error = {},
        };

        for (std::size_t index = 2; index < arguments.size(); ++index) {
            const std::string_view option = arguments[index];
            const DryRunOptionKind optionKind = classifyDryRunOption(option);
            if (optionKind == DryRunOptionKind::Unknown) {
                parsed.error = "Unknown dry-run option " + std::string{option} + ".";
                return parsed;
            }

            ArgumentValueResult value = readArgumentValue(arguments, index, option);
            if (!value.succeeded) {
                parsed.error = std::move(value.error);
                return parsed;
            }

            if (!applyDryRunOption(parsed.dryRun, optionKind, std::move(value.value))) {
                parsed.error = "Unknown dry-run option " + std::string{option} + ".";
                return parsed;
            }
        }

        if (parsed.dryRun.sourceRoot.empty()) {
            parsed.error = "dry-run requires --source-root.";
        } else if (parsed.dryRun.sourcePathPrefix.empty()) {
            parsed.error = "dry-run requires --source-path-prefix.";
        } else if (parsed.dryRun.targetProfile.empty()) {
            parsed.error = "dry-run requires --target-profile.";
        }

        return parsed;
    }

    [[nodiscard]] ParsedArguments parseArguments(std::span<char* const> arguments) {
        if (arguments.size() <= 1) {
            return ParsedArguments{.command = CommandKind::Help, .dryRun = {}, .error = {}};
        }

        const std::string_view command = arguments[1];
        if (command == "--help" || command == "-h" || command == "help") {
            return ParsedArguments{.command = CommandKind::Help, .dryRun = {}, .error = {}};
        }

        if (command == "--smoke-dry-run") {
            return ParsedArguments{.command = CommandKind::SmokeDryRun, .dryRun = {}, .error = {}};
        }

        if (command == "dry-run") {
            return parseDryRunArguments(arguments);
        }

        return ParsedArguments{
            .command = CommandKind::Help,
            .dryRun = {},
            .error = "Unknown command " + std::string{command} + ".",
        };
    }

    void printUsage(std::ostream& output) {
        output << "Usage:\n"
               << "  asharia-asset-processor dry-run --source-root <path> "
                  "--source-path-prefix <path> --target-profile <name> "
                  "[--product-manifest <path>] [--ignore-dir <name> ...]\n"
               << "  asharia-asset-processor --smoke-dry-run\n";
    }

    [[nodiscard]] ManifestLoadResult
    loadProductManifest(const std::optional<std::filesystem::path>& manifestPath) {
        if (!manifestPath || manifestPath->empty()) {
            return ManifestLoadResult{
                .succeeded = true,
                .manifest = {},
                .error = {},
            };
        }

        auto manifest = asharia::asset::readAssetProductManifestFile(*manifestPath);
        if (!manifest) {
            return ManifestLoadResult{
                .succeeded = false,
                .manifest = {},
                .error = "Failed to read product manifest path=" + quotePath(*manifestPath) + ": " +
                         manifest.error().message,
            };
        }

        return ManifestLoadResult{
            .succeeded = true,
            .manifest = std::move(*manifest),
            .error = {},
        };
    }

    void appendScanDiagnostics(std::ostream& output,
                               const asharia::asset::AssetSourceScanResult& scan) {
        for (const asharia::asset::AssetSourceScanDiagnostic& diagnostic : scan.diagnostics) {
            output << "diagnostic stage=scan"
                   << " code=" << toText(diagnostic.code)
                   << " source=" << quoteText(diagnostic.sourcePath)
                   << " file=" << quotePath(diagnostic.sourceFilePath)
                   << " metadata=" << quotePath(diagnostic.metadataPath)
                   << " message=" << quoteText(diagnostic.message) << '\n';
        }
    }

    void appendDiscoveryDiagnostics(std::ostream& output,
                                    const asharia::asset::AssetSourceDiscoveryResult& discovery) {
        for (const asharia::asset::AssetSourceDiscoveryDiagnostic& diagnostic :
             discovery.diagnostics) {
            output << "diagnostic stage=discovery"
                   << " code=" << toText(diagnostic.code)
                   << " source=" << quoteText(diagnostic.sourcePath)
                   << " metadata=" << quotePath(diagnostic.metadataPath)
                   << " message=" << quoteText(diagnostic.message) << '\n';
        }
    }

    void appendSnapshotDiagnostics(std::ostream& output,
                                   const asharia::asset::AssetSourceSnapshotResult& snapshot) {
        for (const asharia::asset::AssetSourceSnapshotDiagnostic& diagnostic :
             snapshot.diagnostics) {
            output << "diagnostic stage=snapshot"
                   << " code=" << toText(diagnostic.code)
                   << " source=" << quoteText(diagnostic.sourcePath)
                   << " file=" << quotePath(diagnostic.sourceFilePath)
                   << " message=" << quoteText(diagnostic.message) << '\n';
        }
    }

    void appendPlanDiagnostics(std::ostream& output,
                               const asharia::asset::AssetImportPlanResult& plan) {
        for (const asharia::asset::AssetImportPlanDiagnostic& diagnostic : plan.diagnostics) {
            output << "diagnostic stage=planning"
                   << " code=" << toText(diagnostic.code)
                   << " source=" << quoteText(diagnostic.sourcePath)
                   << " message=" << quoteText(diagnostic.message) << '\n';
        }
    }

    void appendImportRequests(std::ostream& output,
                              const asharia::asset::AssetImportPlanResult& plan) {
        for (const asharia::asset::AssetImportRequest& request : plan.requests) {
            output << "import-request"
                   << " source=" << quoteText(request.source.sourcePath)
                   << " guid=" << quoteText(asharia::asset::formatAssetGuid(request.source.guid))
                   << " assetType=" << quoteText(request.source.assetTypeName)
                   << " importer=" << quoteText(request.source.importerName)
                   << " reason=" << toText(request.reason)
                   << " productPath=" << quoteText(request.relativeProductPath)
                   << " sourceHash=" << formatHash64(request.source.sourceHash)
                   << " settingsHash=" << formatHash64(request.source.settingsHash)
                   << " dependencyHash=" << formatHash64(request.productKey.dependencyHash) << '\n';
        }
    }

    void appendCacheHits(std::ostream& output, const asharia::asset::AssetImportPlanResult& plan) {
        for (const asharia::asset::AssetImportCacheHit& hit : plan.cacheHits) {
            output << "cache-hit"
                   << " source=" << quoteText(hit.source.sourcePath)
                   << " guid=" << quoteText(asharia::asset::formatAssetGuid(hit.source.guid))
                   << " assetType=" << quoteText(hit.source.assetTypeName)
                   << " importer=" << quoteText(hit.source.importerName)
                   << " productPath=" << quoteText(hit.product.relativeProductPath)
                   << " productHash=" << formatHash64(hit.product.productHash) << '\n';
        }
    }

    [[nodiscard]] DryRunExecution runDryRun(const DryRunOptions& options) {
        std::ostringstream output;
        output << "asset-processor dry-run\n"
               << "sourceRoot=" << quotePath(options.sourceRoot) << '\n'
               << "sourcePathPrefix=" << quoteText(options.sourcePathPrefix) << '\n'
               << "targetProfile=" << quoteText(options.targetProfile) << '\n'
               << "productManifest="
               << (options.productManifestPath ? quotePath(*options.productManifestPath)
                                               : quoteText("<empty>"))
               << '\n'
               << "ignoredDirectories=" << options.ignoredDirectoryNames.size() << '\n';

        ManifestLoadResult manifest = loadProductManifest(options.productManifestPath);
        if (!manifest.succeeded) {
            output << "diagnostic stage=product-manifest"
                   << " code=ReadFailed"
                   << " message=" << quoteText(manifest.error) << '\n';
            return DryRunExecution{
                .exitCode = EXIT_FAILURE,
                .text = output.str(),
            };
        }

        const asharia::asset::AssetScannedImportPlanRequest request{
            .scan =
                asharia::asset::AssetSourceScanRequest{
                    .sourceRoot = options.sourceRoot,
                    .sourcePathPrefix = options.sourcePathPrefix,
                    .metadataSuffix = std::string{kDefaultMetadataSuffix},
                    .ignoredDirectoryNames = options.ignoredDirectoryNames,
                },
            .productManifest = std::move(manifest.manifest),
            .targetProfile = options.targetProfile,
        };

        const asharia::asset::AssetScannedImportPlanResult result =
            asharia::asset::planScannedAssetImports(request);

        output << "scan entries=" << result.scan.entries.size()
               << " diagnostics=" << result.scan.diagnostics.size() << '\n'
               << "discovery records=" << result.discovery.manifest.records.size()
               << " diagnostics=" << result.discovery.diagnostics.size() << '\n'
               << "snapshot snapshots=" << result.snapshot.snapshots.size()
               << " diagnostics=" << result.snapshot.diagnostics.size() << '\n'
               << "planning requests=" << result.plan.requests.size()
               << " cacheHits=" << result.plan.cacheHits.size()
               << " diagnostics=" << result.plan.diagnostics.size() << '\n';

        appendImportRequests(output, result.plan);
        appendCacheHits(output, result.plan);
        appendScanDiagnostics(output, result.scan);
        appendDiscoveryDiagnostics(output, result.discovery);
        appendSnapshotDiagnostics(output, result.snapshot);
        appendPlanDiagnostics(output, result.plan);

        return DryRunExecution{
            .exitCode = result.succeeded() ? EXIT_SUCCESS : EXIT_FAILURE,
            .text = output.str(),
        };
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

    [[nodiscard]] bool expectReportText(const DryRunExecution& execution, std::string_view token) {
        if (containsText(execution.text, token)) {
            return true;
        }

        std::cerr << "asset-processor smoke missing report token: " << token << "\n"
                  << execution.text;
        return false;
    }

    [[nodiscard]] int runSmokeDryRun() {
        const std::filesystem::path root =
            std::filesystem::temp_directory_path() / "asharia-asset-processor-smoke-dry-run";

        std::error_code removeError;
        std::filesystem::remove_all(root, removeError);
        if (removeError) {
            std::cerr << "Failed to clear smoke workspace " << pathText(root) << ": "
                      << removeError.message() << ".\n";
            return EXIT_FAILURE;
        }

        const std::filesystem::path contentRoot = root / "Content";
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
            .sourceRoot = contentRoot,
            .sourcePathPrefix = "Content",
            .targetProfile = "windows-msvc-debug",
            .productManifestPath = std::nullopt,
            .ignoredDirectoryNames = {},
        };
        const DryRunExecution emptyManifestDryRun = runDryRun(emptyManifestOptions);
        if (emptyManifestDryRun.exitCode != EXIT_SUCCESS ||
            !expectReportText(emptyManifestDryRun, "planning requests=2 cacheHits=0") ||
            !expectReportText(emptyManifestDryRun,
                              "import-request source=\"Content/Textures/Crate.png\"") ||
            !expectReportText(emptyManifestDryRun,
                              "import-request source=\"Content/Textures/Decal.png\"")) {
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

        const std::filesystem::path manifestPath = root / "product-manifest.json";
        auto writtenManifest = asharia::asset::writeAssetProductManifestFile(
            manifestPath, asharia::asset::AssetProductManifestDocument{
                              .products = {makeProductRecord(firstPlan.plan.requests.front())},
                          });
        if (!writtenManifest) {
            std::cerr << writtenManifest.error().message << '\n';
            return EXIT_FAILURE;
        }

        const DryRunExecution manifestDryRun = runDryRun(DryRunOptions{
            .sourceRoot = contentRoot,
            .sourcePathPrefix = "Content",
            .targetProfile = "windows-msvc-debug",
            .productManifestPath = manifestPath,
            .ignoredDirectoryNames = {},
        });
        if (manifestDryRun.exitCode != EXIT_SUCCESS ||
            !expectReportText(manifestDryRun, "planning requests=1 cacheHits=1") ||
            !expectReportText(manifestDryRun, "cache-hit source=\"Content/Textures/Crate.png\"")) {
            return EXIT_FAILURE;
        }

        const DryRunExecution invalidRootDryRun = runDryRun(DryRunOptions{
            .sourceRoot = root / "MissingContent",
            .sourcePathPrefix = "Content",
            .targetProfile = "windows-msvc-debug",
            .productManifestPath = std::nullopt,
            .ignoredDirectoryNames = {},
        });
        if (invalidRootDryRun.exitCode == EXIT_SUCCESS ||
            !expectReportText(invalidRootDryRun, "diagnostic stage=scan")) {
            return EXIT_FAILURE;
        }

        const std::filesystem::path badManifestPath = root / "bad-product-manifest.json";
        if (!writeTextFile(badManifestPath, "{")) {
            return EXIT_FAILURE;
        }
        const DryRunExecution badManifestDryRun = runDryRun(DryRunOptions{
            .sourceRoot = contentRoot,
            .sourcePathPrefix = "Content",
            .targetProfile = "windows-msvc-debug",
            .productManifestPath = badManifestPath,
            .ignoredDirectoryNames = {},
        });
        if (badManifestDryRun.exitCode == EXIT_SUCCESS ||
            !expectReportText(badManifestDryRun, "diagnostic stage=product-manifest")) {
            return EXIT_FAILURE;
        }

        std::cout << "asset-processor dry-run smoke passed\n";
        return EXIT_SUCCESS;
    }

} // namespace

namespace {

    [[nodiscard]] int runMain(std::span<char* const> arguments) {
        const ParsedArguments parsed = parseArguments(arguments);
        if (!parsed.error.empty()) {
            std::cerr << parsed.error << '\n';
            printUsage(std::cerr);
            return EXIT_FAILURE;
        }

        switch (parsed.command) {
        case CommandKind::Help:
            printUsage(std::cout);
            return EXIT_SUCCESS;
        case CommandKind::DryRun: {
            const DryRunExecution execution = runDryRun(parsed.dryRun);
            std::cout << execution.text;
            return execution.exitCode;
        }
        case CommandKind::SmokeDryRun:
            return runSmokeDryRun();
        }

        return EXIT_FAILURE;
    }

} // namespace

// main catches all exceptions and reports them as process failure diagnostics.
// NOLINTNEXTLINE(bugprone-exception-escape)
int main(int argumentCount, char** argumentValues) noexcept {
    try {
        return runMain(
            std::span<char* const>{argumentValues, static_cast<std::size_t>(argumentCount)});
    } catch (const std::exception& exception) {
        std::cerr << "asset-processor failed: " << exception.what() << '\n';
        return EXIT_FAILURE;
    } catch (...) {
        std::cerr << "asset-processor failed with an unknown exception.\n";
        return EXIT_FAILURE;
    }
}
