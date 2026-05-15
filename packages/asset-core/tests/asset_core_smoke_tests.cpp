#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string>
#include <string_view>

#include "asharia/asset_core/asset_guid.hpp"
#include "asharia/asset_core/asset_handle.hpp"
#include "asharia/asset_core/asset_metadata.hpp"
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

} // namespace

int main() {
    const bool passed = smokeAssetGuid() && smokeAssetType() && smokeAssetHandleAndReference() &&
                        smokeAssetMetadata();
    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
