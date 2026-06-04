#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "asharia/asset_core/asset_guid.hpp"
#include "asharia/asset_core/asset_metadata_io.hpp"
#include "asharia/asset_pipeline/asset_source_discovery.hpp"
#include "asharia/asset_pipeline/asset_source_snapshot.hpp"

namespace {

    void logFailure(std::string_view message) {
        std::cerr << message << '\n';
    }

    [[nodiscard]] bool messageContains(std::string_view message, std::string_view token) {
        return message.find(token) != std::string_view::npos;
    }

    [[nodiscard]] bool prepareWorkspace(const std::filesystem::path& root) {
        std::error_code removeError;
        std::filesystem::remove_all(root, removeError);

        std::error_code createError;
        std::filesystem::create_directories(root, createError);
        if (createError) {
            logFailure("Asset pipeline smoke could not create temp workspace: " +
                       createError.message());
            return false;
        }

        return true;
    }

    [[nodiscard]] std::filesystem::path smokeRoot(std::string_view name) {
        std::error_code tempError;
        const std::filesystem::path temp = std::filesystem::temp_directory_path(tempError);
        if (tempError) {
            return {};
        }

        return temp / std::string{name};
    }

    [[nodiscard]] std::vector<asharia::asset::AssetImportSetting> defaultSettings() {
        return {
            asharia::asset::AssetImportSetting{.key = "colorSpace", .value = "srgb"},
            asharia::asset::AssetImportSetting{.key = "generateMipmaps", .value = "true"},
            asharia::asset::AssetImportSetting{.key = "compression", .value = "auto"},
        };
    }

    [[nodiscard]] asharia::asset::AssetMetadataDocument
    makeDocument(std::string_view guidText, std::string_view sourcePath, std::uint64_t sourceHash) {
        constexpr std::string_view kTextureTypeName = "com.asharia.asset.Texture2D";
        constexpr std::string_view kTextureImporterName = "com.asharia.importer.texture";

        auto guid = asharia::asset::parseAssetGuid(guidText);
        auto settings = defaultSettings();
        const std::uint64_t settingsHash = asharia::asset::hashAssetImportSettings(settings);
        return asharia::asset::AssetMetadataDocument{
            .source =
                asharia::asset::SourceAssetRecord{
                    .guid = guid ? *guid : asharia::asset::AssetGuid{},
                    .assetType = asharia::asset::makeAssetTypeId(kTextureTypeName),
                    .assetTypeName = std::string{kTextureTypeName},
                    .sourcePath = std::string{sourcePath},
                    .importerId = asharia::asset::makeImporterId(kTextureImporterName),
                    .importerName = std::string{kTextureImporterName},
                    .importerVersion = asharia::asset::ImporterVersion{1},
                    .sourceHash = sourceHash,
                    .settingsHash = settingsHash,
                },
            .settings = std::move(settings),
        };
    }

    [[nodiscard]] bool writeDocument(const std::filesystem::path& path,
                                     const asharia::asset::AssetMetadataDocument& document) {
        auto written = asharia::asset::writeAssetMetadataFile(path, document);
        if (!written) {
            logFailure(written.error().message);
            return false;
        }

        return true;
    }

    [[nodiscard]] bool writeTextFile(const std::filesystem::path& path, std::string_view text) {
        std::ofstream file(path, std::ios::binary);
        if (!file) {
            logFailure("Asset pipeline smoke could not open temp text file.");
            return false;
        }

        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!file) {
            logFailure("Asset pipeline smoke could not write temp text file.");
            return false;
        }

        return true;
    }

    [[nodiscard]] bool
    expectSingleDiagnostic(const asharia::asset::AssetSourceDiscoveryResult& result,
                           asharia::asset::AssetSourceDiscoveryDiagnosticCode expectedCode,
                           std::string_view expectedToken) {
        if (result.succeeded() || result.diagnostics.size() != 1) {
            logFailure("Asset pipeline smoke expected exactly one discovery diagnostic.");
            return false;
        }

        const asharia::asset::AssetSourceDiscoveryDiagnostic& diagnostic =
            result.diagnostics.front();
        if (diagnostic.code != expectedCode ||
            !messageContains(diagnostic.message, expectedToken)) {
            logFailure("Asset pipeline smoke produced an unexpected discovery diagnostic.");
            return false;
        }

        return true;
    }

    [[nodiscard]] bool
    expectSingleSnapshotDiagnostic(const asharia::asset::AssetSourceSnapshotResult& result,
                                   asharia::asset::AssetSourceSnapshotDiagnosticCode expectedCode,
                                   std::string_view expectedToken) {
        if (result.succeeded() || result.diagnostics.size() != 1) {
            logFailure("Asset pipeline smoke expected exactly one source snapshot diagnostic.");
            return false;
        }

        const asharia::asset::AssetSourceSnapshotDiagnostic& diagnostic =
            result.diagnostics.front();
        if (diagnostic.code != expectedCode ||
            !messageContains(diagnostic.message, expectedToken)) {
            logFailure("Asset pipeline smoke produced an unexpected source snapshot diagnostic.");
            return false;
        }

        return true;
    }

    [[nodiscard]] bool smokeSourceSnapshotValidAndDeterministic() {
        const std::filesystem::path root = smokeRoot("asharia-asset-pipeline-smoke-snapshot-valid");
        if (root.empty() || !prepareWorkspace(root)) {
            return false;
        }

        const std::filesystem::path crateSource = root / "crate.png";
        const std::filesystem::path decalSource = root / "decal.png";
        if (!writeTextFile(crateSource, "crate source bytes") ||
            !writeTextFile(decalSource, "decal source bytes")) {
            return false;
        }

        const std::array entries{
            asharia::asset::AssetSourceSnapshotEntry{
                .sourcePath = "Content/Textures/Crate.png",
                .sourceFilePath = crateSource,
            },
            asharia::asset::AssetSourceSnapshotEntry{
                .sourcePath = "Content/Textures/Decal.png",
                .sourceFilePath = decalSource,
            },
        };

        const asharia::asset::AssetSourceSnapshotResult first =
            asharia::asset::snapshotAssetSourceFiles(entries);
        const asharia::asset::AssetSourceSnapshotResult second =
            asharia::asset::snapshotAssetSourceFiles(entries);

        if (!first.succeeded() || !second.succeeded()) {
            logFailure(first.diagnostics.empty() ? "Asset pipeline source snapshot smoke failed."
                                                 : first.diagnostics.front().message);
            return false;
        }

        if (first.snapshots.size() != 2 || first.snapshots != second.snapshots ||
            first.snapshots[0].sourcePath != "Content/Textures/Crate.png" ||
            first.snapshots[0].sourceFilePath != crateSource ||
            first.snapshots[0].sourceHash == 0 || first.snapshots[1].sourceHash == 0 ||
            first.snapshots[0].sourceHash == first.snapshots[1].sourceHash) {
            logFailure("Asset pipeline source snapshot smoke failed deterministic hashing.");
            return false;
        }

        std::cout << "Asset pipeline source snapshots: " << first.snapshots.size() << '\n';
        return true;
    }

    [[nodiscard]] bool smokeSourceSnapshotContentChange() {
        const std::filesystem::path root =
            smokeRoot("asharia-asset-pipeline-smoke-snapshot-change");
        if (root.empty() || !prepareWorkspace(root)) {
            return false;
        }

        const std::filesystem::path crateSource = root / "crate.png";
        const std::array entries{
            asharia::asset::AssetSourceSnapshotEntry{
                .sourcePath = "Content/Textures/Crate.png",
                .sourceFilePath = crateSource,
            },
        };

        if (!writeTextFile(crateSource, "crate source bytes v1")) {
            return false;
        }
        const asharia::asset::AssetSourceSnapshotResult before =
            asharia::asset::snapshotAssetSourceFiles(entries);

        if (!writeTextFile(crateSource, "crate source bytes v2")) {
            return false;
        }
        const asharia::asset::AssetSourceSnapshotResult after =
            asharia::asset::snapshotAssetSourceFiles(entries);

        if (!before.succeeded() || !after.succeeded() || before.snapshots.size() != 1 ||
            after.snapshots.size() != 1 ||
            before.snapshots.front().sourceHash == after.snapshots.front().sourceHash) {
            logFailure("Asset pipeline source snapshot smoke missed a content hash change.");
            return false;
        }

        return true;
    }

    [[nodiscard]] bool smokeSourceSnapshotMissingFile() {
        const std::filesystem::path root =
            smokeRoot("asharia-asset-pipeline-smoke-snapshot-missing");
        if (root.empty() || !prepareWorkspace(root)) {
            return false;
        }

        const std::array entries{
            asharia::asset::AssetSourceSnapshotEntry{
                .sourcePath = "Content/Textures/Missing.png",
                .sourceFilePath = root / "missing.png",
            },
        };
        const asharia::asset::AssetSourceSnapshotResult result =
            asharia::asset::snapshotAssetSourceFiles(entries);
        return result.snapshots.empty() &&
               expectSingleSnapshotDiagnostic(
                   result, asharia::asset::AssetSourceSnapshotDiagnosticCode::MissingSourceFile,
                   "could not find source file");
    }

    [[nodiscard]] bool smokeSourceSnapshotDirectory() {
        const std::filesystem::path root =
            smokeRoot("asharia-asset-pipeline-smoke-snapshot-directory");
        if (root.empty() || !prepareWorkspace(root)) {
            return false;
        }

        const std::filesystem::path sourceDirectory = root / "crate-directory";
        std::error_code createError;
        std::filesystem::create_directories(sourceDirectory, createError);
        if (createError) {
            logFailure("Asset pipeline smoke could not create source directory: " +
                       createError.message());
            return false;
        }

        const std::array entries{
            asharia::asset::AssetSourceSnapshotEntry{
                .sourcePath = "Content/Textures/Crate.png",
                .sourceFilePath = sourceDirectory,
            },
        };
        const asharia::asset::AssetSourceSnapshotResult result =
            asharia::asset::snapshotAssetSourceFiles(entries);
        return result.snapshots.empty() &&
               expectSingleSnapshotDiagnostic(
                   result, asharia::asset::AssetSourceSnapshotDiagnosticCode::SourceFileNotRegular,
                   "regular file");
    }

    [[nodiscard]] bool smokeSourceSnapshotInvalidSourcePath() {
        const std::filesystem::path root =
            smokeRoot("asharia-asset-pipeline-smoke-snapshot-invalid-source-path");
        if (root.empty() || !prepareWorkspace(root)) {
            return false;
        }

        const std::filesystem::path crateSource = root / "crate.png";
        if (!writeTextFile(crateSource, "crate source bytes")) {
            return false;
        }

        const std::array entries{
            asharia::asset::AssetSourceSnapshotEntry{
                .sourcePath = "Content\\Textures\\Crate.png",
                .sourceFilePath = crateSource,
            },
        };
        const asharia::asset::AssetSourceSnapshotResult result =
            asharia::asset::snapshotAssetSourceFiles(entries);
        return result.snapshots.empty() &&
               expectSingleSnapshotDiagnostic(
                   result, asharia::asset::AssetSourceSnapshotDiagnosticCode::InvalidEntry,
                   "'/' separators");
    }

    [[nodiscard]] bool smokeSourceSnapshotDuplicateSourcePath() {
        const std::filesystem::path root =
            smokeRoot("asharia-asset-pipeline-smoke-snapshot-duplicate-path");
        if (root.empty() || !prepareWorkspace(root)) {
            return false;
        }

        const std::filesystem::path crateA = root / "crate-a.png";
        const std::filesystem::path crateB = root / "crate-b.png";
        if (!writeTextFile(crateA, "crate source bytes a") ||
            !writeTextFile(crateB, "crate source bytes b")) {
            return false;
        }

        const std::array entries{
            asharia::asset::AssetSourceSnapshotEntry{
                .sourcePath = "Content/Textures/Crate.png",
                .sourceFilePath = crateA,
            },
            asharia::asset::AssetSourceSnapshotEntry{
                .sourcePath = "Content/Textures/Crate.png",
                .sourceFilePath = crateB,
            },
        };
        const asharia::asset::AssetSourceSnapshotResult result =
            asharia::asset::snapshotAssetSourceFiles(entries);
        return result.snapshots.size() == 1 &&
               expectSingleSnapshotDiagnostic(
                   result, asharia::asset::AssetSourceSnapshotDiagnosticCode::DuplicateSourcePath,
                   "duplicate source path");
    }

    [[nodiscard]] bool smokeDiscoveryValidAndDeterministic() {
        const std::filesystem::path root = smokeRoot("asharia-asset-pipeline-smoke-valid");
        if (root.empty() || !prepareWorkspace(root)) {
            return false;
        }

        const std::filesystem::path crateMetadata = root / "crate.png.ameta";
        const std::filesystem::path decalMetadata = root / "decal.png.ameta";
        const auto crateDocument =
            makeDocument("9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c21", "Content/Textures/Crate.png",
                         0x1000F00D1234CAFEULL);
        const auto decalDocument =
            makeDocument("785e2474-65c4-4f28-a8fb-ff8a21449a61", "Content/Textures/Decal.png",
                         0x2000F00D1234CAFEULL);
        if (!writeDocument(crateMetadata, crateDocument) ||
            !writeDocument(decalMetadata, decalDocument)) {
            return false;
        }

        const std::array entries{
            asharia::asset::AssetSourceDiscoveryEntry{
                .sourcePath = "Content/Textures/Crate.png",
                .metadataPath = crateMetadata,
            },
            asharia::asset::AssetSourceDiscoveryEntry{
                .sourcePath = "Content/Textures/Decal.png",
                .metadataPath = decalMetadata,
            },
        };

        const asharia::asset::AssetSourceDiscoveryResult first =
            asharia::asset::discoverAssetSources(entries);
        const asharia::asset::AssetSourceDiscoveryResult second =
            asharia::asset::discoverAssetSources(entries);

        if (!first.succeeded() || !second.succeeded()) {
            logFailure(first.diagnostics.empty() ? "Asset pipeline smoke discovery failed."
                                                 : first.diagnostics.front().message);
            return false;
        }

        if (first.manifest.records.size() != 2 || first.manifest.catalog.sources().size() != 2 ||
            first.manifest.records != second.manifest.records ||
            first.manifest.records[0].source != crateDocument.source ||
            first.manifest.records[0].settings != crateDocument.settings ||
            first.manifest.records[1].source != decalDocument.source) {
            logFailure("Asset pipeline smoke failed deterministic manifest discovery.");
            return false;
        }

        std::cout << "Asset pipeline discovered sources: " << first.manifest.records.size() << '\n';
        return true;
    }

    [[nodiscard]] bool smokeMissingMetadata() {
        const std::filesystem::path root = smokeRoot("asharia-asset-pipeline-smoke-missing");
        if (root.empty() || !prepareWorkspace(root)) {
            return false;
        }

        const std::array entries{
            asharia::asset::AssetSourceDiscoveryEntry{
                .sourcePath = "Content/Textures/Missing.png",
                .metadataPath = root / "missing.png.ameta",
            },
        };
        const asharia::asset::AssetSourceDiscoveryResult result =
            asharia::asset::discoverAssetSources(entries);
        return result.manifest.records.empty() &&
               expectSingleDiagnostic(
                   result, asharia::asset::AssetSourceDiscoveryDiagnosticCode::MissingMetadata,
                   "could not find metadata");
    }

    [[nodiscard]] bool smokeMalformedMetadata() {
        const std::filesystem::path root = smokeRoot("asharia-asset-pipeline-smoke-malformed");
        if (root.empty() || !prepareWorkspace(root)) {
            return false;
        }

        const std::filesystem::path metadataPath = root / "broken.png.ameta";
        if (!writeTextFile(metadataPath, "{")) {
            return false;
        }

        const std::array entries{
            asharia::asset::AssetSourceDiscoveryEntry{
                .sourcePath = "Content/Textures/Broken.png",
                .metadataPath = metadataPath,
            },
        };
        const asharia::asset::AssetSourceDiscoveryResult result =
            asharia::asset::discoverAssetSources(entries);
        return result.manifest.records.empty() &&
               expectSingleDiagnostic(
                   result, asharia::asset::AssetSourceDiscoveryDiagnosticCode::MetadataReadFailed,
                   "failed to read metadata");
    }

    [[nodiscard]] bool smokeSourcePathMismatch() {
        const std::filesystem::path root = smokeRoot("asharia-asset-pipeline-smoke-mismatch");
        if (root.empty() || !prepareWorkspace(root)) {
            return false;
        }

        const std::filesystem::path metadataPath = root / "crate.png.ameta";
        const auto document = makeDocument("9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c21",
                                           "Content/Textures/Crate.png", 0x1000F00D1234CAFEULL);
        if (!writeDocument(metadataPath, document)) {
            return false;
        }

        const std::array entries{
            asharia::asset::AssetSourceDiscoveryEntry{
                .sourcePath = "Content/Props/Crate.png",
                .metadataPath = metadataPath,
            },
        };
        const asharia::asset::AssetSourceDiscoveryResult result =
            asharia::asset::discoverAssetSources(entries);
        return result.manifest.records.empty() &&
               expectSingleDiagnostic(
                   result, asharia::asset::AssetSourceDiscoveryDiagnosticCode::SourcePathMismatch,
                   "source path mismatch");
    }

    [[nodiscard]] bool smokeDuplicateGuid() {
        const std::filesystem::path root = smokeRoot("asharia-asset-pipeline-smoke-duplicate-guid");
        if (root.empty() || !prepareWorkspace(root)) {
            return false;
        }

        const std::filesystem::path crateMetadata = root / "crate.png.ameta";
        const std::filesystem::path crateCopyMetadata = root / "crate-copy.png.ameta";
        const auto crateDocument =
            makeDocument("9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c21", "Content/Textures/Crate.png",
                         0x1000F00D1234CAFEULL);
        const auto crateCopyDocument =
            makeDocument("9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c21", "Content/Textures/CrateCopy.png",
                         0x2000F00D1234CAFEULL);
        if (!writeDocument(crateMetadata, crateDocument) ||
            !writeDocument(crateCopyMetadata, crateCopyDocument)) {
            return false;
        }

        const std::array entries{
            asharia::asset::AssetSourceDiscoveryEntry{
                .sourcePath = "Content/Textures/Crate.png",
                .metadataPath = crateMetadata,
            },
            asharia::asset::AssetSourceDiscoveryEntry{
                .sourcePath = "Content/Textures/CrateCopy.png",
                .metadataPath = crateCopyMetadata,
            },
        };
        const asharia::asset::AssetSourceDiscoveryResult result =
            asharia::asset::discoverAssetSources(entries);
        return result.manifest.records.size() == 1 &&
               expectSingleDiagnostic(
                   result, asharia::asset::AssetSourceDiscoveryDiagnosticCode::DuplicateGuid,
                   "duplicate GUID");
    }

    [[nodiscard]] bool smokeDuplicateSourcePath() {
        const std::filesystem::path root = smokeRoot("asharia-asset-pipeline-smoke-duplicate-path");
        if (root.empty() || !prepareWorkspace(root)) {
            return false;
        }

        const std::filesystem::path crateAPath = root / "crate-a.png.ameta";
        const std::filesystem::path crateBPath = root / "crate-b.png.ameta";
        const auto crateA = makeDocument("9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c21",
                                         "Content/Textures/Crate.png", 0x1000F00D1234CAFEULL);
        const auto crateB = makeDocument("785e2474-65c4-4f28-a8fb-ff8a21449a61",
                                         "Content/Textures/Crate.png", 0x2000F00D1234CAFEULL);
        if (!writeDocument(crateAPath, crateA) || !writeDocument(crateBPath, crateB)) {
            return false;
        }

        const std::array entries{
            asharia::asset::AssetSourceDiscoveryEntry{
                .sourcePath = "Content/Textures/Crate.png",
                .metadataPath = crateAPath,
            },
            asharia::asset::AssetSourceDiscoveryEntry{
                .sourcePath = "Content/Textures/Crate.png",
                .metadataPath = crateBPath,
            },
        };
        const asharia::asset::AssetSourceDiscoveryResult result =
            asharia::asset::discoverAssetSources(entries);
        return result.manifest.records.size() == 1 &&
               expectSingleDiagnostic(
                   result, asharia::asset::AssetSourceDiscoveryDiagnosticCode::DuplicateSourcePath,
                   "duplicate source path");
    }

    [[nodiscard]] bool smokeInvalidEntry() {
        const std::array entries{
            asharia::asset::AssetSourceDiscoveryEntry{
                .sourcePath = "",
                .metadataPath = {},
            },
        };
        const asharia::asset::AssetSourceDiscoveryResult result =
            asharia::asset::discoverAssetSources(entries);
        return result.manifest.records.empty() &&
               expectSingleDiagnostic(
                   result, asharia::asset::AssetSourceDiscoveryDiagnosticCode::InvalidEntry,
                   "source path is missing");
    }

    [[nodiscard]] bool smokeInvalidEntrySourcePath() {
        const std::array entries{
            asharia::asset::AssetSourceDiscoveryEntry{
                .sourcePath = "Content\\Textures\\Crate.png",
                .metadataPath = "unused-crate.png.ameta",
            },
        };
        const asharia::asset::AssetSourceDiscoveryResult result =
            asharia::asset::discoverAssetSources(entries);
        return result.manifest.records.empty() &&
               expectSingleDiagnostic(
                   result, asharia::asset::AssetSourceDiscoveryDiagnosticCode::InvalidEntry,
                   "'/' separators");
    }

    [[nodiscard]] bool smokeInvalidMetadataSourcePath() {
        const std::filesystem::path root =
            smokeRoot("asharia-asset-pipeline-smoke-invalid-metadata-source-path");
        if (root.empty() || !prepareWorkspace(root)) {
            return false;
        }

        const std::filesystem::path metadataPath = root / "crate.png.ameta";
        const std::string invalidMetadata = R"json({
  "schema": "com.asharia.asset.metadata",
  "schemaVersion": 1,
  "guid": "9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c21",
  "assetType": "com.asharia.asset.Texture2D",
  "sourcePath": "Content//Textures/Crate.png",
  "sourceHash": "1000f00d1234cafe",
  "settingsHash": "1111111111111111",
  "importer": {"id": "com.asharia.importer.texture", "version": 1},
  "settings": {}
}
)json";
        if (!writeTextFile(metadataPath, invalidMetadata)) {
            return false;
        }

        const std::array entries{
            asharia::asset::AssetSourceDiscoveryEntry{
                .sourcePath = "Content/Textures/Crate.png",
                .metadataPath = metadataPath,
            },
        };
        const asharia::asset::AssetSourceDiscoveryResult result =
            asharia::asset::discoverAssetSources(entries);
        return result.manifest.records.empty() &&
               expectSingleDiagnostic(
                   result, asharia::asset::AssetSourceDiscoveryDiagnosticCode::MetadataReadFailed,
                   "empty segment");
    }

} // namespace

int main() {
    const bool passed = smokeSourceSnapshotValidAndDeterministic() &&
                        smokeSourceSnapshotContentChange() && smokeSourceSnapshotMissingFile() &&
                        smokeSourceSnapshotDirectory() && smokeSourceSnapshotInvalidSourcePath() &&
                        smokeSourceSnapshotDuplicateSourcePath() &&
                        smokeDiscoveryValidAndDeterministic() && smokeMissingMetadata() &&
                        smokeMalformedMetadata() && smokeSourcePathMismatch() &&
                        smokeDuplicateGuid() && smokeDuplicateSourcePath() && smokeInvalidEntry() &&
                        smokeInvalidEntrySourcePath() && smokeInvalidMetadataSourcePath();
    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
