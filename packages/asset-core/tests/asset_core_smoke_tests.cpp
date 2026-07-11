#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "asharia/asset_core/asset_catalog.hpp"
#include "asharia/asset_core/asset_catalog_view.hpp"
#include "asharia/asset_core/asset_guid.hpp"
#include "asharia/asset_core/asset_handle.hpp"
#include "asharia/asset_core/asset_metadata.hpp"
#include "asharia/asset_core/asset_metadata_io.hpp"
#include "asharia/asset_core/asset_product.hpp"
#include "asharia/asset_core/asset_reference.hpp"
#include "asharia/asset_core/asset_type.hpp"

namespace {

    void logFailure(std::string_view message) {
        std::cerr << message << '\n';
    }

    bool expectInvalidGuid(std::string_view text) {
        auto parsed = asharia::asset::parseAssetGuid(text);
        if (parsed) {
            logFailure("Asset GUID smoke accepted an invalid GUID.");
            return false;
        }

        if (parsed.error().domain != asharia::ErrorDomain::Asset ||
            parsed.error().message.find(text) == std::string::npos) {
            logFailure("Asset GUID smoke produced an incomplete parse diagnostic.");
            return false;
        }

        return true;
    }

    bool smokeAssetGuid() {
        constexpr std::string_view kLowercaseGuid = "9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c21";
        auto parsed = asharia::asset::parseAssetGuid(kLowercaseGuid);
        if (!parsed) {
            logFailure(parsed.error().message);
            return false;
        }

        if (!*parsed || asharia::asset::formatAssetGuid(*parsed) != kLowercaseGuid) {
            logFailure("Asset GUID smoke failed lowercase UUID round-trip.");
            return false;
        }

        constexpr std::string_view kUppercaseGuid = "9F7A31A0-0B63-4D4C-9F18-BD9A0D2E9C21";
        auto parsedUppercase = asharia::asset::parseAssetGuid(kUppercaseGuid);
        if (!parsedUppercase ||
            asharia::asset::formatAssetGuid(*parsedUppercase) != kLowercaseGuid) {
            logFailure("Asset GUID smoke failed uppercase UUID canonicalization.");
            return false;
        }

        if (!expectInvalidGuid("9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c2") ||
            !expectInvalidGuid("9f7a31a00-0b63-4d4c-9f18-bd9a0d2e9c21") ||
            !expectInvalidGuid("9f7a31a0_0b63-4d4c-9f18-bd9a0d2e9c21") ||
            !expectInvalidGuid("9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c2x") ||
            !expectInvalidGuid("00000000-0000-0000-0000-000000000000")) {
            return false;
        }

        std::cout << "Asset GUID canonical: " << asharia::asset::formatAssetGuid(*parsed) << '\n';
        return true;
    }

    bool smokeAssetType() {
        constexpr std::string_view kTextureTypeName = "com.asharia.asset.Texture2D";
        constexpr std::string_view kMeshTypeName = "com.asharia.asset.Mesh";

        constexpr asharia::asset::AssetTypeId textureTypeA =
            asharia::asset::makeAssetTypeId(kTextureTypeName);
        constexpr asharia::asset::AssetTypeId textureTypeB =
            asharia::asset::makeAssetTypeId(kTextureTypeName);
        constexpr asharia::asset::AssetTypeId meshType =
            asharia::asset::makeAssetTypeId(kMeshTypeName);
        constexpr asharia::asset::AssetTypeId emptyType = asharia::asset::makeAssetTypeId("");

        if (!textureTypeA || textureTypeA != textureTypeB || textureTypeA == meshType ||
            emptyType) {
            logFailure("Asset type smoke saw unstable or invalid type identity.");
            return false;
        }

        std::cout << "Asset type id: " << textureTypeA.value << '\n';
        return true;
    }

    struct SmokeTexture {};
    struct SmokeMesh {};

    bool messageContains(std::string_view message, std::string_view token) {
        return message.find(token) != std::string_view::npos;
    }

    bool expectInvalidSourcePath(std::string_view sourcePath, std::string_view expectedReason) {
        auto validated = asharia::asset::validateAssetSourcePath(sourcePath);
        if (validated) {
            logFailure("Asset source path smoke accepted an invalid path.");
            return false;
        }

        const std::string& message = validated.error().message;
        if (validated.error().domain != asharia::ErrorDomain::Asset ||
            !messageContains(message, sourcePath) || !messageContains(message, expectedReason)) {
            logFailure("Asset source path smoke produced an incomplete diagnostic.");
            return false;
        }

        return true;
    }

    bool smokeAssetSourcePath() {
        if (!asharia::asset::validateAssetSourcePath("Content/Textures/Crate.png") ||
            !asharia::asset::validateAssetSourcePath("Content/Meshes/Crate.v1.fbx") ||
            !asharia::asset::validateAssetSourcePath("Packages/Test.Asset/Asset.file")) {
            logFailure("Asset source path smoke rejected a valid canonical path.");
            return false;
        }

        if (!expectInvalidSourcePath("", "source path is missing") ||
            !expectInvalidSourcePath("Content\\Textures\\Crate.png", "'/' separators") ||
            !expectInvalidSourcePath("/Content/Textures/Crate.png", "project-relative") ||
            !expectInvalidSourcePath("//Server/Share/Crate.png", "project-relative") ||
            !expectInvalidSourcePath("C:/Content/Textures/Crate.png", "drive prefix") ||
            !expectInvalidSourcePath("Content/./Crate.png", "'.' or '..'") ||
            !expectInvalidSourcePath("Content/../Crate.png", "'.' or '..'") ||
            !expectInvalidSourcePath("Content//Textures/Crate.png", "empty segment") ||
            !expectInvalidSourcePath("Content/Textures/", "empty segment")) {
            return false;
        }

        std::cout << "Asset source path canonical contract validated.\n";
        return true;
    }

    bool expectInvalidReference(asharia::asset::AssetReference reference,
                                asharia::asset::AssetTypeId actualType, std::string_view sourcePath,
                                std::string_view expectedTypeName, std::string_view actualTypeName,
                                std::string_view expectedReason) {
        auto validated = asharia::asset::validateAssetReference(reference, actualType, sourcePath,
                                                                expectedTypeName, actualTypeName);
        if (validated) {
            logFailure("Asset reference smoke accepted an invalid reference.");
            return false;
        }

        const std::string& message = validated.error().message;
        if (validated.error().domain != asharia::ErrorDomain::Asset ||
            !messageContains(message, sourcePath) || !messageContains(message, expectedTypeName) ||
            !messageContains(message, actualTypeName) ||
            !messageContains(message, expectedReason) ||
            !messageContains(message, asharia::asset::formatAssetGuid(reference.guid))) {
            logFailure("Asset reference smoke produced an incomplete diagnostic.");
            return false;
        }

        return true;
    }

    bool smokeAssetHandleAndReference() {
        constexpr std::string_view kTextureGuidText = "9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c21";
        constexpr std::string_view kMeshGuidText = "785e2474-65c4-4f28-a8fb-ff8a21449a61";
        constexpr std::string_view kTextureTypeName = "com.asharia.asset.Texture2D";
        constexpr std::string_view kMeshTypeName = "com.asharia.asset.Mesh";
        constexpr std::string_view kSourcePath = "Content/Textures/Crate.png";

        auto textureGuid = asharia::asset::parseAssetGuid(kTextureGuidText);
        auto meshGuid = asharia::asset::parseAssetGuid(kMeshGuidText);
        if (!textureGuid || !meshGuid) {
            logFailure("Asset handle smoke could not parse fixture GUIDs.");
            return false;
        }

        const asharia::asset::AssetHandle<SmokeTexture> defaultHandle{};
        const asharia::asset::AssetHandle<SmokeTexture> textureHandle{.guid = *textureGuid};
        const asharia::asset::AssetHandle<SmokeTexture> matchingTextureHandle{.guid = *textureGuid};
        const asharia::asset::AssetHandle<SmokeTexture> differentTextureHandle{.guid = *meshGuid};
        const asharia::asset::AssetHandle<SmokeMesh> meshHandle{.guid = *meshGuid};

        if (defaultHandle || !textureHandle || textureHandle != matchingTextureHandle ||
            textureHandle == differentTextureHandle || !meshHandle) {
            logFailure("Asset handle smoke saw unexpected handle identity behavior.");
            return false;
        }

        const asharia::asset::AssetTypeId textureType =
            asharia::asset::makeAssetTypeId(kTextureTypeName);
        const asharia::asset::AssetTypeId meshType = asharia::asset::makeAssetTypeId(kMeshTypeName);
        const asharia::asset::AssetReference textureReference =
            asharia::asset::makeAssetReference(*textureGuid, textureType);

        if (!textureReference || textureReference.guid != *textureGuid ||
            textureReference.expectedType != textureType) {
            logFailure("Asset reference smoke saw unexpected reference construction.");
            return false;
        }

        auto validReference = asharia::asset::validateAssetReference(
            textureReference, textureType, kSourcePath, kTextureTypeName, kTextureTypeName);
        if (!validReference) {
            logFailure(validReference.error().message);
            return false;
        }

        if (!expectInvalidReference(asharia::asset::makeAssetReference({}, textureType),
                                    textureType, kSourcePath, kTextureTypeName, kTextureTypeName,
                                    "asset GUID is invalid") ||
            !expectInvalidReference(asharia::asset::makeAssetReference(*textureGuid, {}),
                                    textureType, kSourcePath, kTextureTypeName, kTextureTypeName,
                                    "expected asset type is invalid") ||
            !expectInvalidReference(textureReference, {}, kSourcePath, kTextureTypeName,
                                    kTextureTypeName, "actual asset type is invalid") ||
            !expectInvalidReference(textureReference, meshType, kSourcePath, kTextureTypeName,
                                    kMeshTypeName, "asset type mismatch")) {
            return false;
        }

        std::cout << "Asset reference validated: "
                  << asharia::asset::formatAssetGuid(textureReference.guid) << '\n';
        return true;
    }

    bool expectInvalidSourceRecords(std::span<const asharia::asset::SourceAssetRecord> records,
                                    std::string_view expectedReason) {
        auto validated = asharia::asset::validateSourceAssetRecords(records);
        if (validated) {
            logFailure("Asset metadata smoke accepted invalid source records.");
            return false;
        }

        const std::string& message = validated.error().message;
        if (validated.error().domain != asharia::ErrorDomain::Asset ||
            !messageContains(message, expectedReason)) {
            logFailure("Asset metadata smoke produced an incomplete diagnostic.");
            return false;
        }

        return true;
    }

    bool smokeAssetMetadata() {
        constexpr std::string_view kTextureGuidText = "9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c21";
        constexpr std::string_view kMeshGuidText = "785e2474-65c4-4f28-a8fb-ff8a21449a61";
        constexpr std::string_view kTextureTypeName = "com.asharia.asset.Texture2D";
        constexpr std::string_view kTextureImporterName = "com.asharia.importer.texture";
        constexpr std::string_view kMeshImporterName = "com.asharia.importer.mesh";

        auto textureGuid = asharia::asset::parseAssetGuid(kTextureGuidText);
        auto meshGuid = asharia::asset::parseAssetGuid(kMeshGuidText);
        if (!textureGuid || !meshGuid) {
            logFailure("Asset metadata smoke could not parse fixture GUIDs.");
            return false;
        }

        const asharia::asset::ImporterId textureImporterA =
            asharia::asset::makeImporterId(kTextureImporterName);
        const asharia::asset::ImporterId textureImporterB =
            asharia::asset::makeImporterId(kTextureImporterName);
        const asharia::asset::ImporterId meshImporter =
            asharia::asset::makeImporterId(kMeshImporterName);
        const asharia::asset::ImporterId emptyImporter = asharia::asset::makeImporterId("");

        if (asharia::asset::kAssetMetadataSchema.empty() ||
            asharia::asset::kAssetMetadataVersion == 0 || !textureImporterA ||
            textureImporterA != textureImporterB || textureImporterA == meshImporter ||
            emptyImporter) {
            logFailure("Asset metadata smoke saw invalid schema or importer identity.");
            return false;
        }

        const std::array settingsA{
            asharia::asset::AssetImportSetting{.key = "colorSpace", .value = "srgb"},
            asharia::asset::AssetImportSetting{.key = "generateMipmaps", .value = "true"},
            asharia::asset::AssetImportSetting{.key = "compression", .value = "auto"},
        };
        const std::array settingsB{
            asharia::asset::AssetImportSetting{.key = "colorSpace", .value = "srgb"},
            asharia::asset::AssetImportSetting{.key = "generateMipmaps", .value = "true"},
            asharia::asset::AssetImportSetting{.key = "compression", .value = "auto"},
        };
        const std::array settingsChanged{
            asharia::asset::AssetImportSetting{.key = "colorSpace", .value = "srgb"},
            asharia::asset::AssetImportSetting{.key = "generateMipmaps", .value = "true"},
            asharia::asset::AssetImportSetting{.key = "compression", .value = "bc7"},
        };

        const std::uint64_t settingsHashA = asharia::asset::hashAssetImportSettings(settingsA);
        const std::uint64_t settingsHashB = asharia::asset::hashAssetImportSettings(settingsB);
        const std::uint64_t changedSettingsHash =
            asharia::asset::hashAssetImportSettings(settingsChanged);
        if (settingsHashA == 0 || settingsHashA != settingsHashB ||
            settingsHashA == changedSettingsHash) {
            logFailure("Asset metadata smoke saw unstable settings hash behavior.");
            return false;
        }

        const asharia::asset::AssetTypeId textureType =
            asharia::asset::makeAssetTypeId(kTextureTypeName);
        const asharia::asset::SourceAssetRecord textureRecord{
            .guid = *textureGuid,
            .assetType = textureType,
            .assetTypeName = std::string{kTextureTypeName},
            .sourcePath = "Content/Textures/Crate.png",
            .importerId = textureImporterA,
            .importerName = std::string{kTextureImporterName},
            .importerVersion = asharia::asset::ImporterVersion{1},
            .sourceHash = 0xF00DCAFEULL,
            .settingsHash = settingsHashA,
        };

        if (!textureRecord || !asharia::asset::validateSourceAssetRecord(textureRecord) ||
            !asharia::asset::validateSourceAssetRecords(std::array{textureRecord})) {
            logFailure("Asset metadata smoke rejected a valid source record.");
            return false;
        }

        asharia::asset::SourceAssetRecord missingType = textureRecord;
        missingType.assetType = {};
        missingType.assetTypeName.clear();

        asharia::asset::SourceAssetRecord duplicateGuid = textureRecord;
        duplicateGuid.sourcePath = "Content/Textures/CrateCopy.png";

        asharia::asset::SourceAssetRecord duplicatePath = textureRecord;
        duplicatePath.guid = *meshGuid;

        const std::array missingTypeRecords{missingType};
        const std::array duplicateGuidRecords{textureRecord, duplicateGuid};
        const std::array duplicatePathRecords{textureRecord, duplicatePath};
        if (!expectInvalidSourceRecords(missingTypeRecords, "asset type is missing") ||
            !expectInvalidSourceRecords(duplicateGuidRecords, "duplicate asset GUID") ||
            !expectInvalidSourceRecords(duplicatePathRecords, "duplicate source path")) {
            return false;
        }

        std::cout << "Asset metadata settings hash: " << settingsHashA << '\n';
        return true;
    }

    struct InvalidMetadataTextCase {
        std::string_view text;
        std::string_view expectedReason;
    };

    bool expectInvalidMetadataRead(InvalidMetadataTextCase testCase) {
        auto document = asharia::asset::readAssetMetadataText(testCase.text);
        if (document) {
            logFailure("Asset metadata IO smoke accepted invalid .ameta text.");
            return false;
        }

        const std::string& message = document.error().message;
        if (document.error().domain != asharia::ErrorDomain::Asset ||
            !messageContains(message, testCase.expectedReason)) {
            logFailure("Asset metadata IO smoke produced an incomplete diagnostic.");
            return false;
        }

        return true;
    }

    bool smokeAssetMetadataIo() {
        constexpr std::string_view kTextureGuidText = "9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c21";
        constexpr std::string_view kTextureTypeName = "com.asharia.asset.Texture2D";
        constexpr std::string_view kTextureImporterName = "com.asharia.importer.texture";

        auto textureGuid = asharia::asset::parseAssetGuid(kTextureGuidText);
        if (!textureGuid) {
            logFailure("Asset metadata IO smoke could not parse fixture GUID.");
            return false;
        }

        const std::array settings{
            asharia::asset::AssetImportSetting{.key = "colorSpace", .value = "srgb"},
            asharia::asset::AssetImportSetting{.key = "generateMipmaps", .value = "true"},
            asharia::asset::AssetImportSetting{.key = "compression", .value = "auto"},
        };
        const asharia::asset::AssetMetadataDocument document{
            .source =
                asharia::asset::SourceAssetRecord{
                    .guid = *textureGuid,
                    .assetType = asharia::asset::makeAssetTypeId(kTextureTypeName),
                    .assetTypeName = std::string{kTextureTypeName},
                    .sourcePath = "Content/Textures/Crate.png",
                    .importerId = asharia::asset::makeImporterId(kTextureImporterName),
                    .importerName = std::string{kTextureImporterName},
                    .importerVersion = asharia::asset::ImporterVersion{1},
                    .sourceHash = 0x1000F00D1234CAFEULL,
                    .settingsHash = asharia::asset::hashAssetImportSettings(settings),
                },
            .settings =
                std::vector<asharia::asset::AssetImportSetting>{settings.begin(), settings.end()},
        };

        auto firstText = asharia::asset::writeAssetMetadataText(document);
        auto secondText = asharia::asset::writeAssetMetadataText(document);
        if (!firstText || !secondText || *firstText != *secondText) {
            logFailure("Asset metadata IO smoke failed deterministic .ameta write.");
            return false;
        }

        auto parsed = asharia::asset::readAssetMetadataText(*firstText);
        if (!parsed || *parsed != document) {
            logFailure(parsed ? "Asset metadata IO smoke failed .ameta round-trip."
                              : parsed.error().message);
            return false;
        }

        const std::filesystem::path metadataPath =
            std::filesystem::temp_directory_path() / "asharia-asset-core-smoke.ameta";
        if (auto written = asharia::asset::writeAssetMetadataFile(metadataPath, document);
            !written) {
            logFailure(written.error().message);
            return false;
        }
        auto fileParsed = asharia::asset::readAssetMetadataFile(metadataPath);
        std::error_code removeError;
        std::filesystem::remove(metadataPath, removeError);
        if (!fileParsed || *fileParsed != document) {
            logFailure(fileParsed ? "Asset metadata IO smoke failed file round-trip."
                                  : fileParsed.error().message);
            return false;
        }

        asharia::asset::AssetMetadataDocument mismatchedSettings = document;
        mismatchedSettings.source.settingsHash ^= 0x1ULL;
        auto mismatchedWrite = asharia::asset::writeAssetMetadataText(mismatchedSettings);
        if (mismatchedWrite ||
            !messageContains(mismatchedWrite.error().message, "settings hash mismatch")) {
            logFailure("Asset metadata IO smoke accepted mismatched settings hash.");
            return false;
        }

        const std::string missingGuid = R"json({
  "schema": "com.asharia.asset.metadata",
  "schemaVersion": 1,
  "assetType": "com.asharia.asset.Texture2D",
  "sourcePath": "Content/Textures/Crate.png",
  "sourceHash": "1000f00d1234cafe",
  "settingsHash": "1111111111111111",
  "importer": {"id": "com.asharia.importer.texture", "version": 1},
  "settings": {}
}
)json";
        const std::string boolSetting = R"json({
  "schema": "com.asharia.asset.metadata",
  "schemaVersion": 1,
  "guid": "9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c21",
  "assetType": "com.asharia.asset.Texture2D",
  "sourcePath": "Content/Textures/Crate.png",
  "sourceHash": "1000f00d1234cafe",
  "settingsHash": "1111111111111111",
  "importer": {"id": "com.asharia.importer.texture", "version": 1},
  "settings": {"generateMipmaps": true}
}
)json";
        const std::string duplicateGuid = R"json({
  "schema": "com.asharia.asset.metadata",
  "schemaVersion": 1,
  "guid": "9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c21",
  "guid": "785e2474-65c4-4f28-a8fb-ff8a21449a61",
  "assetType": "com.asharia.asset.Texture2D",
  "sourcePath": "Content/Textures/Crate.png",
  "sourceHash": "1000f00d1234cafe",
  "settingsHash": "1111111111111111",
  "importer": {"id": "com.asharia.importer.texture", "version": 1},
  "settings": {}
}
)json";
        const std::string uppercaseHash = R"json({
  "schema": "com.asharia.asset.metadata",
  "schemaVersion": 1,
  "guid": "9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c21",
  "assetType": "com.asharia.asset.Texture2D",
  "sourcePath": "Content/Textures/Crate.png",
  "sourceHash": "1000F00D1234CAFE",
  "settingsHash": "1111111111111111",
  "importer": {"id": "com.asharia.importer.texture", "version": 1},
  "settings": {}
}
)json";
        const std::string unknownMember = R"json({
  "schema": "com.asharia.asset.metadata",
  "schemaVersion": 1,
  "guid": "9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c21",
  "assetType": "com.asharia.asset.Texture2D",
  "sourcePath": "Content/Textures/Crate.png",
  "sourceHash": "1000f00d1234cafe",
  "settingsHash": "1111111111111111",
  "importer": {"id": "com.asharia.importer.texture", "version": 1},
  "settings": {},
  "runtimePointer": "forbidden"
}
)json";
        const std::string invalidSourcePath = R"json({
  "schema": "com.asharia.asset.metadata",
  "schemaVersion": 1,
  "guid": "9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c21",
  "assetType": "com.asharia.asset.Texture2D",
  "sourcePath": "Content\\Textures\\Crate.png",
  "sourceHash": "1000f00d1234cafe",
  "settingsHash": "1111111111111111",
  "importer": {"id": "com.asharia.importer.texture", "version": 1},
  "settings": {}
}
)json";

        if (!expectInvalidMetadataRead({.text = "{", .expectedReason = "byte"}) ||
            !expectInvalidMetadataRead({.text = missingGuid, .expectedReason = "guid"}) ||
            !expectInvalidMetadataRead({.text = boolSetting, .expectedReason = "string value"}) ||
            !expectInvalidMetadataRead(
                {.text = duplicateGuid, .expectedReason = "duplicate key"}) ||
            !expectInvalidMetadataRead(
                {.text = uppercaseHash, .expectedReason = "lowercase hex"}) ||
            !expectInvalidMetadataRead(
                {.text = unknownMember, .expectedReason = "unknown member"}) ||
            !expectInvalidMetadataRead(
                {.text = invalidSourcePath, .expectedReason = "'/' separators"})) {
            return false;
        }

        std::cout << "Asset metadata IO bytes: " << firstText->size() << '\n';
        return true;
    }

    bool smokeAssetProductKeyAndDependency() {
        constexpr std::string_view kTextureGuidText = "9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c21";
        constexpr std::string_view kShaderGuidText = "785e2474-65c4-4f28-a8fb-ff8a21449a61";
        constexpr std::string_view kTextureTypeName = "com.asharia.asset.Texture2D";
        constexpr std::string_view kTextureImporterName = "com.asharia.importer.texture";

        auto textureGuid = asharia::asset::parseAssetGuid(kTextureGuidText);
        auto shaderGuid = asharia::asset::parseAssetGuid(kShaderGuidText);
        if (!textureGuid || !shaderGuid) {
            logFailure("Asset product smoke could not parse fixture GUIDs.");
            return false;
        }

        const std::array settings{
            asharia::asset::AssetImportSetting{.key = "colorSpace", .value = "srgb"},
            asharia::asset::AssetImportSetting{.key = "generateMipmaps", .value = "true"},
        };
        const asharia::asset::SourceAssetRecord source{
            .guid = *textureGuid,
            .assetType = asharia::asset::makeAssetTypeId(kTextureTypeName),
            .assetTypeName = std::string{kTextureTypeName},
            .sourcePath = "Content/Textures/Crate.png",
            .importerId = asharia::asset::makeImporterId(kTextureImporterName),
            .importerName = std::string{kTextureImporterName},
            .importerVersion = asharia::asset::ImporterVersion{1},
            .sourceHash = 0x1000F00DULL,
            .settingsHash = asharia::asset::hashAssetImportSettings(settings),
        };

        const std::array dependencies{
            asharia::asset::AssetDependency{
                .owner = *textureGuid,
                .kind = asharia::asset::AssetDependencyKind::SourceFile,
                .path = "Content/Textures/Crate.png",
                .hash = source.sourceHash,
            },
            asharia::asset::AssetDependency{
                .owner = *textureGuid,
                .kind = asharia::asset::AssetDependencyKind::AssetReference,
                .asset = *shaderGuid,
                .path = "Content/Shaders/TextureImport.slang",
                .hash = 0xCAFE1234ULL,
            },
        };
        const std::uint64_t dependencyHashA = asharia::asset::hashAssetDependencies(dependencies);
        const std::uint64_t dependencyHashB = asharia::asset::hashAssetDependencies(dependencies);
        if (dependencyHashA == 0 || dependencyHashA != dependencyHashB) {
            logFailure("Asset product smoke saw unstable dependency hash behavior.");
            return false;
        }

        const std::uint64_t msvcTargetHash =
            asharia::asset::makeAssetTargetProfileHash("windows-msvc-debug");
        const std::uint64_t clangTargetHash =
            asharia::asset::makeAssetTargetProfileHash("windows-clangcl-debug");
        const asharia::asset::AssetProductKey baseKey =
            asharia::asset::makeAssetProductKey(source, dependencyHashA, msvcTargetHash);
        const asharia::asset::AssetProductKey matchingKey =
            asharia::asset::makeAssetProductKey(source, dependencyHashB, msvcTargetHash);

        if (!baseKey || baseKey != matchingKey ||
            asharia::asset::hashAssetProductKey(baseKey) !=
                asharia::asset::hashAssetProductKey(matchingKey)) {
            logFailure("Asset product smoke saw unstable product key behavior.");
            return false;
        }

        asharia::asset::SourceAssetRecord changedSourceHash = source;
        changedSourceHash.sourceHash ^= 0x1ULL;
        const asharia::asset::AssetProductKey sourceChangedKey =
            asharia::asset::makeAssetProductKey(changedSourceHash, dependencyHashA, msvcTargetHash);

        asharia::asset::SourceAssetRecord changedSettingsHash = source;
        changedSettingsHash.settingsHash ^= 0x1ULL;
        const asharia::asset::AssetProductKey settingsChangedKey =
            asharia::asset::makeAssetProductKey(changedSettingsHash, dependencyHashA,
                                                msvcTargetHash);

        const asharia::asset::AssetProductKey targetChangedKey =
            asharia::asset::makeAssetProductKey(source, dependencyHashA, clangTargetHash);

        if (baseKey == sourceChangedKey || baseKey == settingsChangedKey ||
            baseKey == targetChangedKey ||
            asharia::asset::hashAssetProductKey(baseKey) ==
                asharia::asset::hashAssetProductKey(sourceChangedKey) ||
            asharia::asset::hashAssetProductKey(baseKey) ==
                asharia::asset::hashAssetProductKey(settingsChangedKey) ||
            asharia::asset::hashAssetProductKey(baseKey) ==
                asharia::asset::hashAssetProductKey(targetChangedKey)) {
            logFailure("Asset product smoke did not react to key input changes.");
            return false;
        }

        const asharia::asset::AssetProductRecord record{
            .key = baseKey,
            .relativeProductPath = "textures/crate.texture.bin",
            .productSizeBytes = 4096,
            .productHash = asharia::asset::hashAssetProductKey(baseKey),
        };
        if (!record) {
            logFailure("Asset product smoke rejected a valid product record.");
            return false;
        }

        std::cout << "Asset product key hash: " << asharia::asset::hashAssetProductKey(baseKey)
                  << '\n';
        return true;
    }

    bool expectCatalogFailure(asharia::VoidResult result, std::string_view expectedReason) {
        if (result) {
            logFailure("Asset catalog smoke accepted an invalid catalog command.");
            return false;
        }

        const std::string& message = result.error().message;
        if (result.error().domain != asharia::ErrorDomain::Asset ||
            !messageContains(message, expectedReason)) {
            logFailure("Asset catalog smoke produced an incomplete diagnostic.");
            return false;
        }

        return true;
    }

    bool smokeAssetCatalog() {
        constexpr std::string_view kTextureGuidText = "9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c21";
        constexpr std::string_view kMeshGuidText = "785e2474-65c4-4f28-a8fb-ff8a21449a61";
        constexpr std::string_view kTextureTypeName = "com.asharia.asset.Texture2D";
        constexpr std::string_view kMeshTypeName = "com.asharia.asset.Mesh";
        constexpr std::string_view kTextureImporterName = "com.asharia.importer.texture";
        constexpr std::string_view kMeshImporterName = "com.asharia.importer.mesh";

        auto textureGuid = asharia::asset::parseAssetGuid(kTextureGuidText);
        auto meshGuid = asharia::asset::parseAssetGuid(kMeshGuidText);
        if (!textureGuid || !meshGuid) {
            logFailure("Asset catalog smoke could not parse fixture GUIDs.");
            return false;
        }

        asharia::asset::SourceAssetRecord textureRecord{
            .guid = *textureGuid,
            .assetType = asharia::asset::makeAssetTypeId(kTextureTypeName),
            .assetTypeName = std::string{kTextureTypeName},
            .sourcePath = "Content/Textures/Crate.png",
            .importerId = asharia::asset::makeImporterId(kTextureImporterName),
            .importerName = std::string{kTextureImporterName},
            .importerVersion = asharia::asset::ImporterVersion{1},
            .sourceHash = 0x1000F00DULL,
            .settingsHash = 0x2000F00DULL,
        };
        asharia::asset::SourceAssetRecord meshRecord{
            .guid = *meshGuid,
            .assetType = asharia::asset::makeAssetTypeId(kMeshTypeName),
            .assetTypeName = std::string{kMeshTypeName},
            .sourcePath = "Content/Meshes/Crate.fbx",
            .importerId = asharia::asset::makeImporterId(kMeshImporterName),
            .importerName = std::string{kMeshImporterName},
            .importerVersion = asharia::asset::ImporterVersion{1},
            .sourceHash = 0x3000F00DULL,
            .settingsHash = 0x4000F00DULL,
        };

        asharia::asset::AssetCatalog catalog;
        auto addedTexture = catalog.addSource(textureRecord);
        if (!addedTexture || catalog.sources().size() != 1 ||
            catalog.findByGuid(*textureGuid) == nullptr ||
            catalog.findBySourcePath(textureRecord.sourcePath) == nullptr) {
            logFailure("Asset catalog smoke failed add/query behavior.");
            return false;
        }

        asharia::asset::SourceAssetRecord duplicateGuid = textureRecord;
        duplicateGuid.sourcePath = "Content/Textures/CrateCopy.png";
        asharia::asset::SourceAssetRecord duplicatePath = meshRecord;
        duplicatePath.sourcePath = textureRecord.sourcePath;
        if (!expectCatalogFailure(catalog.addSource(duplicateGuid), "duplicate GUID") ||
            !expectCatalogFailure(catalog.addSource(duplicatePath), "duplicate source path")) {
            return false;
        }

        asharia::asset::SourceAssetRecord relocatedTexture = textureRecord;
        relocatedTexture.sourcePath = "Content/Props/Crate.png";
        if (!expectCatalogFailure(catalog.updateSource(relocatedTexture), "relocation rejected")) {
            return false;
        }

        auto relocated = catalog.updateSource(
            relocatedTexture, asharia::asset::AssetCatalogRelocationPolicy::AllowPathChange);
        if (!relocated || catalog.findBySourcePath(textureRecord.sourcePath) != nullptr ||
            catalog.findBySourcePath(relocatedTexture.sourcePath) == nullptr) {
            logFailure("Asset catalog smoke failed explicit relocation behavior.");
            return false;
        }

        if (!catalog.addSource(meshRecord) || catalog.sources().size() != 2) {
            logFailure("Asset catalog smoke failed second add behavior.");
            return false;
        }

        if (!catalog.removeSource(*textureGuid) || catalog.findByGuid(*textureGuid) != nullptr ||
            catalog.sources().size() != 1 ||
            !expectCatalogFailure(catalog.removeSource(*textureGuid), "missing source")) {
            logFailure("Asset catalog smoke failed remove behavior.");
            return false;
        }

        std::cout << "Asset catalog source count: " << catalog.sources().size() << '\n';
        return true;
    }

    bool smokeAssetCatalogView() {
        struct CatalogViewSourceFixture {
            std::string_view guidText;
            std::string_view typeName;
            std::string_view sourcePath;
            std::string_view importerName;
            std::uint64_t sourceHash{};
            std::uint64_t settingsHash{};
        };

        constexpr std::string_view kMaterialGuidText = "b8373128-8e46-44e1-a5a4-df4c2ef9d2ad";
        constexpr std::string_view kMeshGuidText = "1135c477-65aa-4d44-92f1-f208fc6142ad";
        constexpr std::string_view kShaderGuidText = "13a10d4b-6987-48d1-ad27-ae4055e5a936";
        constexpr std::string_view kTextGuidText = "f98f9d88-237f-4e8a-a4b6-9977d3a1fc2b";
        constexpr std::string_view kTextureGuidText = "cd9c0f3d-20e2-4028-a3e9-c3f42d3fd515";
        constexpr std::string_view kMaterialTypeName = "com.asharia.asset.Material";
        constexpr std::string_view kMeshTypeName = "com.asharia.asset.Mesh";
        constexpr std::string_view kShaderTypeName = "com.asharia.asset.Shader";
        constexpr std::string_view kTextTypeName = "com.asharia.asset.Text";
        constexpr std::string_view kTextureTypeName = "com.asharia.asset.Texture2D";

        const auto makeRecord =
            [](const CatalogViewSourceFixture& fixture) -> asharia::asset::SourceAssetRecord {
            auto guid = asharia::asset::parseAssetGuid(fixture.guidText);
            return asharia::asset::SourceAssetRecord{
                .guid = guid ? *guid : asharia::asset::AssetGuid{},
                .assetType = asharia::asset::makeAssetTypeId(fixture.typeName),
                .assetTypeName = std::string{fixture.typeName},
                .sourcePath = std::string{fixture.sourcePath},
                .importerId = asharia::asset::makeImporterId(fixture.importerName),
                .importerName = std::string{fixture.importerName},
                .importerVersion = asharia::asset::ImporterVersion{1},
                .sourceHash = fixture.sourceHash,
                .settingsHash = fixture.settingsHash,
            };
        };

        const asharia::asset::SourceAssetRecord materialRecord =
            makeRecord(CatalogViewSourceFixture{.guidText = kMaterialGuidText,
                                                .typeName = kMaterialTypeName,
                                                .sourcePath = "Content/Materials/Brushed.amat",
                                                .importerName = "com.asharia.importer.material",
                                                .sourceHash = 0x1100ULL,
                                                .settingsHash = 0x1200ULL});
        const asharia::asset::SourceAssetRecord meshRecord =
            makeRecord(CatalogViewSourceFixture{.guidText = kMeshGuidText,
                                                .typeName = kMeshTypeName,
                                                .sourcePath = "Content/Meshes/Cube.mesh",
                                                .importerName = "com.asharia.importer.mesh",
                                                .sourceHash = 0x2100ULL,
                                                .settingsHash = 0x2200ULL});
        const asharia::asset::SourceAssetRecord shaderRecord =
            makeRecord(CatalogViewSourceFixture{.guidText = kShaderGuidText,
                                                .typeName = kShaderTypeName,
                                                .sourcePath = "Content/Shaders/Grid.slang",
                                                .importerName = "com.asharia.importer.shader",
                                                .sourceHash = 0x3100ULL,
                                                .settingsHash = 0x3200ULL});
        const asharia::asset::SourceAssetRecord textRecord =
            makeRecord(CatalogViewSourceFixture{.guidText = kTextGuidText,
                                                .typeName = kTextTypeName,
                                                .sourcePath = "Content/Text/Readme.txt",
                                                .importerName = "com.asharia.importer.text",
                                                .sourceHash = 0x5100ULL,
                                                .settingsHash = 0x5200ULL});
        const asharia::asset::SourceAssetRecord textureRecord =
            makeRecord(CatalogViewSourceFixture{.guidText = kTextureGuidText,
                                                .typeName = kTextureTypeName,
                                                .sourcePath = "Content/Textures/Zeta.png",
                                                .importerName = "com.asharia.importer.texture",
                                                .sourceHash = 0x4100ULL,
                                                .settingsHash = 0x4200ULL});

        asharia::asset::AssetCatalog catalog;
        if (!catalog.addSource(textureRecord) || !catalog.addSource(shaderRecord) ||
            !catalog.addSource(textRecord) || !catalog.addSource(materialRecord) ||
            !catalog.addSource(meshRecord)) {
            logFailure("Asset catalog view smoke failed to build source catalog.");
            return false;
        }

        const std::uint64_t targetProfile =
            asharia::asset::makeAssetTargetProfileHash("editor-debug");
        const std::uint64_t otherTargetProfile =
            asharia::asset::makeAssetTargetProfileHash("standalone-release");
        const asharia::asset::AssetProductKey currentMaterialKey =
            asharia::asset::makeAssetProductKey(materialRecord, 0x5000ULL, targetProfile);
        const asharia::asset::AssetProductKey currentShaderKey =
            asharia::asset::makeAssetProductKey(shaderRecord, 0x8000ULL, targetProfile);
        const asharia::asset::AssetProductKey wrongDependencyShaderKey =
            asharia::asset::makeAssetProductKey(shaderRecord, 0x8001ULL, targetProfile);
        const asharia::asset::AssetProductKey wrongTargetShaderKey =
            asharia::asset::makeAssetProductKey(shaderRecord, 0x8000ULL, otherTargetProfile);
        const asharia::asset::AssetProductKey currentTextKey =
            asharia::asset::makeAssetProductKey(textRecord, 0x9000ULL, targetProfile);
        asharia::asset::SourceAssetRecord staleTextureRecord = textureRecord;
        staleTextureRecord.sourceHash ^= 0x10ULL;
        const asharia::asset::AssetProductKey currentTextureKey =
            asharia::asset::makeAssetProductKey(textureRecord, 0x6000ULL, targetProfile);
        const asharia::asset::AssetProductKey staleTextureKey =
            asharia::asset::makeAssetProductKey(staleTextureRecord, 0x6000ULL, targetProfile);
        const asharia::asset::AssetProductKey invalidMeshKey =
            asharia::asset::makeAssetProductKey(meshRecord, 0x7000ULL, targetProfile);

        const std::array expectedProductKeys{
            currentMaterialKey, invalidMeshKey, currentShaderKey, currentTextKey, currentTextureKey,
        };
        const std::array<asharia::asset::AssetProductRecord, 5> products{
            asharia::asset::AssetProductRecord{
                .key = currentMaterialKey,
                .relativeProductPath = "materials/brushed.product",
                .productSizeBytes = 128,
                .productHash = asharia::asset::hashAssetProductKey(currentMaterialKey),
            },
            asharia::asset::AssetProductRecord{
                .key = staleTextureKey,
                .relativeProductPath = "textures/zeta.product",
                .productSizeBytes = 256,
                .productHash = asharia::asset::hashAssetProductKey(staleTextureKey),
            },
            asharia::asset::AssetProductRecord{
                .key = wrongDependencyShaderKey,
                .relativeProductPath = "shaders/grid.dependency-stale.product",
                .productSizeBytes = 96,
                .productHash = asharia::asset::hashAssetProductKey(wrongDependencyShaderKey),
            },
            asharia::asset::AssetProductRecord{
                .key = wrongTargetShaderKey,
                .relativeProductPath = "shaders/grid.target-stale.product",
                .productSizeBytes = 96,
                .productHash = asharia::asset::hashAssetProductKey(wrongTargetShaderKey),
            },
            asharia::asset::AssetProductRecord{
                .key = invalidMeshKey,
                .relativeProductPath = {},
                .productSizeBytes = 0,
                .productHash = 0,
            },
        };

        const std::array sourceFacets{
            asharia::asset::AssetCatalogSourceFacet{
                .guid = textureRecord.guid,
                .sourcePath = textureRecord.sourcePath,
                .importProfileName = "generic-profile",
                .assetRoleName = "com.asharia.asset.GenericRole",
                .subAssets = {},
                .diagnostics = {},
            },
        };
        const asharia::asset::AssetCatalogView view = asharia::asset::buildAssetCatalogView(
            catalog, products,
            asharia::asset::AssetCatalogViewOptions{.requireProducts = true,
                                                    .expectedProductKeys = expectedProductKeys,
                                                    .sourceFacets = sourceFacets});
        if (view.entries.size() != 5U || view.diagnostics.size() != 1U) {
            logFailure("Asset catalog view smoke produced the wrong row or diagnostic count.");
            return false;
        }

        const auto entryState = [&](std::size_t index) { return view.entries[index].productState; };
        if (view.entries[0].sourcePath != materialRecord.sourcePath ||
            view.entries[1].sourcePath != meshRecord.sourcePath ||
            view.entries[2].sourcePath != shaderRecord.sourcePath ||
            view.entries[3].sourcePath != textRecord.sourcePath ||
            view.entries[4].sourcePath != textureRecord.sourcePath) {
            logFailure("Asset catalog view smoke did not sort rows by source path.");
            return false;
        }

        if (view.entries[0].guidText != kMaterialGuidText ||
            view.entries[0].displayName != "Brushed.amat" || view.entries[0].extension != ".amat" ||
            view.entries[2].displayName != "Grid.slang" || view.entries[2].extension != ".slang") {
            logFailure("Asset catalog view smoke produced invalid display name or extension.");
            return false;
        }

        if (entryState(0) != asharia::asset::AssetCatalogProductState::Ready ||
            view.entries[0].currentProductCount != 1U || !view.entries[0].diagnostics.empty() ||
            entryState(1) != asharia::asset::AssetCatalogProductState::InvalidProduct ||
            view.entries[1].diagnostics.size() != 1U ||
            view.entries[1].diagnostics[0].code !=
                asharia::asset::AssetCatalogDiagnosticCode::InvalidProductRecord ||
            entryState(2) != asharia::asset::AssetCatalogProductState::StaleProduct ||
            view.entries[2].currentProductCount != 0U || view.entries[2].staleProductCount != 2U ||
            view.entries[2].diagnostics.size() != 1U ||
            view.entries[2].diagnostics[0].code !=
                asharia::asset::AssetCatalogDiagnosticCode::StaleProduct ||
            entryState(3) != asharia::asset::AssetCatalogProductState::MissingProduct ||
            view.entries[3].diagnostics.size() != 1U ||
            view.entries[3].diagnostics[0].code !=
                asharia::asset::AssetCatalogDiagnosticCode::MissingProduct ||
            entryState(4) != asharia::asset::AssetCatalogProductState::StaleProduct ||
            view.entries[4].staleProductCount != 1U || view.entries[4].diagnostics.size() != 1U ||
            view.entries[4].diagnostics[0].code !=
                asharia::asset::AssetCatalogDiagnosticCode::StaleProduct) {
            logFailure("Asset catalog view smoke produced invalid product states.");
            return false;
        }

        if (view.diagnostics[0].code !=
                asharia::asset::AssetCatalogDiagnosticCode::InvalidProductRecord ||
            asharia::asset::assetCatalogProductStateName(entryState(4)) != "stale-product" ||
            asharia::asset::assetCatalogDiagnosticCodeName(view.diagnostics[0].code) !=
                "invalid-product-record") {
            logFailure("Asset catalog view smoke produced invalid diagnostic labels.");
            return false;
        }

        if (view.entries[4].importProfileName != "generic-profile" ||
            view.entries[4].assetRoleName != "com.asharia.asset.GenericRole") {
            logFailure("Asset catalog view smoke missed source facet fields.");
            return false;
        }

        const std::array<asharia::asset::AssetProductRecord, 1> manifestOnlyProducts{
            products[0],
        };
        const asharia::asset::AssetCatalogView manifestOnlyView =
            asharia::asset::buildAssetCatalogView(
                catalog, manifestOnlyProducts,
                asharia::asset::AssetCatalogViewOptions{.requireProducts = true});
        const auto manifestOnlyMaterial = std::ranges::find_if(
            manifestOnlyView.entries,
            [&materialRecord](const asharia::asset::AssetCatalogViewEntry& entry) {
                return entry.sourcePath == materialRecord.sourcePath;
            });
        if (manifestOnlyMaterial == manifestOnlyView.entries.end() ||
            manifestOnlyMaterial->productState !=
                asharia::asset::AssetCatalogProductState::StaleProduct ||
            manifestOnlyMaterial->currentProductCount != 0U ||
            manifestOnlyMaterial->staleProductCount != 1U ||
            manifestOnlyMaterial->diagnostics.size() != 1U ||
            manifestOnlyMaterial->diagnostics[0].message.find("expected product keys") ==
                std::string::npos) {
            logFailure(
                "Asset catalog view smoke allowed Ready without expected product key authority.");
            return false;
        }

        std::cout << "Asset catalog view rows: " << view.entries.size() << '\n';
        return true;
    }

} // namespace

int main() {
    try {
        const bool passed = smokeAssetGuid() && smokeAssetType() && smokeAssetSourcePath() &&
                            smokeAssetHandleAndReference() && smokeAssetMetadata() &&
                            smokeAssetMetadataIo() && smokeAssetProductKeyAndDependency() &&
                            smokeAssetCatalog() && smokeAssetCatalogView();
        return passed ? EXIT_SUCCESS : EXIT_FAILURE;
    } catch (...) {
        return EXIT_FAILURE;
    }
}
