#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "asharia/asset_core/asset_guid.hpp"
#include "asharia/asset_core/asset_metadata_io.hpp"
#include "asharia/asset_pipeline/asset_import_planning.hpp"
#include "asharia/asset_pipeline/asset_product_blob.hpp"
#include "asharia/asset_pipeline/asset_product_execution.hpp"
#include "asharia/asset_pipeline/asset_product_manifest_io.hpp"
#include "asharia/asset_pipeline/asset_scanned_import_planning.hpp"
#include "asharia/asset_pipeline/asset_source_discovery.hpp"
#include "asharia/asset_pipeline/asset_source_scan.hpp"
#include "asharia/asset_pipeline/asset_source_snapshot.hpp"
#include "asharia/asset_pipeline/asset_texture_import.hpp"
#include "asharia/asset_pipeline/asset_texture_import_profile.hpp"

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

    [[nodiscard]] std::vector<asharia::asset::AssetImportSetting>
    textureSettings(std::string_view compression) {
        return {
            asharia::asset::AssetImportSetting{.key = "colorSpace", .value = "srgb"},
            asharia::asset::AssetImportSetting{.key = "generateMipmaps", .value = "true"},
            asharia::asset::AssetImportSetting{
                .key = "compression",
                .value = std::string{compression},
            },
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

    [[nodiscard]] asharia::asset::DiscoveredSourceAsset
    makeDiscoveredSource(std::string_view guidText, std::string_view sourcePath,
                         std::uint64_t sourceHash, std::string_view compression = "auto") {
        auto document = makeDocument(guidText, sourcePath, sourceHash);
        document.settings = textureSettings(compression);
        document.source.settingsHash = asharia::asset::hashAssetImportSettings(document.settings);
        return asharia::asset::DiscoveredSourceAsset{
            .entry =
                asharia::asset::AssetSourceDiscoveryEntry{
                    .sourcePath = std::string{sourcePath},
                    .metadataPath = {},
                },
            .source = std::move(document.source),
            .settings = std::move(document.settings),
        };
    }

    [[nodiscard]] asharia::asset::SourceAssetRecord
    makeTextureProfileSmokeRecord(std::string_view guidText, std::string_view sourcePath) {
        auto guid = asharia::asset::parseAssetGuid(guidText);
        constexpr std::string_view kTextureTypeName = "com.asharia.asset.Texture";
        constexpr std::string_view kTextureImporterName = "com.asharia.importer.texture";
        return asharia::asset::SourceAssetRecord{
            .guid = guid ? *guid : asharia::asset::AssetGuid{},
            .assetType = asharia::asset::makeAssetTypeId(kTextureTypeName),
            .assetTypeName = std::string{kTextureTypeName},
            .sourcePath = std::string{sourcePath},
            .importerId = asharia::asset::makeImporterId(kTextureImporterName),
            .importerName = std::string{kTextureImporterName},
            .importerVersion = asharia::asset::ImporterVersion{1},
            .sourceHash = 0x9000ULL,
            .settingsHash = 0xA000ULL,
        };
    }

    [[nodiscard]] std::vector<asharia::asset::AssetImportSetting> textureImportContractSettings(
        std::string_view width, std::string_view height, std::string_view profile = "Texture 2D",
        std::string_view format = "rgba8-unorm", std::string_view settingsVersion = "1") {
        return {
            asharia::asset::AssetImportSetting{
                .key = std::string{asharia::asset::kTextureImportProfileSettingKey},
                .value = std::string{profile},
            },
            asharia::asset::AssetImportSetting{
                .key = std::string{asharia::asset::kTextureImportSettingsVersionSettingKey},
                .value = std::string{settingsVersion},
            },
            asharia::asset::AssetImportSetting{
                .key = std::string{asharia::asset::kTextureImportWidthSettingKey},
                .value = std::string{width},
            },
            asharia::asset::AssetImportSetting{
                .key = std::string{asharia::asset::kTextureImportHeightSettingKey},
                .value = std::string{height},
            },
            asharia::asset::AssetImportSetting{
                .key = std::string{asharia::asset::kTextureImportFormatSettingKey},
                .value = std::string{format},
            },
        };
    }

    [[nodiscard]] std::vector<asharia::asset::AssetImportSetting>
    textureImportPngSettings(std::string_view profile = "Texture 2D",
                             std::string_view format = "rgba8-unorm",
                             std::string_view settingsVersion = "1") {
        return {
            asharia::asset::AssetImportSetting{
                .key = std::string{asharia::asset::kTextureImportProfileSettingKey},
                .value = std::string{profile},
            },
            asharia::asset::AssetImportSetting{
                .key = std::string{asharia::asset::kTextureImportSettingsVersionSettingKey},
                .value = std::string{settingsVersion},
            },
            asharia::asset::AssetImportSetting{
                .key = std::string{asharia::asset::kTextureImportFormatSettingKey},
                .value = std::string{format},
            },
        };
    }

    [[nodiscard]] asharia::asset::SourceAssetRecord makeTextureImportContractRecord(
        std::string_view sourcePath, std::span<const asharia::asset::AssetImportSetting> settings,
        const asharia::asset::AssetTextureImporterDescriptor& importer) {
        auto guid = asharia::asset::parseAssetGuid("5f690533-517e-4b52-9bf0-1c0f210527ab");
        return asharia::asset::SourceAssetRecord{
            .guid = guid ? *guid : asharia::asset::AssetGuid{},
            .assetType = asharia::asset::makeAssetTypeId(asharia::asset::kTextureRoleTexture2D),
            .assetTypeName = std::string{asharia::asset::kTextureRoleTexture2D},
            .sourcePath = std::string{sourcePath},
            .importerId = asharia::asset::makeImporterId(importer.importerName),
            .importerName = importer.importerName,
            .importerVersion = importer.importerVersion,
            .sourceHash = 0xBEEFF00D12345678ULL,
            .settingsHash = asharia::asset::hashAssetImportSettings(settings),
        };
    }

    [[nodiscard]] std::vector<std::uint8_t> rawRgba8TextureBytes(std::uint32_t width,
                                                                 std::uint32_t height) {
        std::vector<std::uint8_t> bytes;
        bytes.reserve(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U);
        for (std::uint32_t row = 0; row < height; ++row) {
            for (std::uint32_t column = 0; column < width; ++column) {
                bytes.push_back(static_cast<std::uint8_t>(10U + column));
                bytes.push_back(static_cast<std::uint8_t>(20U + row));
                bytes.push_back(static_cast<std::uint8_t>(30U + column + row));
                bytes.push_back(255U);
            }
        }
        return bytes;
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

    [[nodiscard]] asharia::asset::AssetTextureImportRequest
    makeTextureImportContractRequest(std::string_view sourcePath,
                                     std::vector<asharia::asset::AssetImportSetting> settings,
                                     std::vector<std::uint8_t> bytes,
                                     asharia::asset::AssetTextureImporterDescriptor importer =
                                         asharia::asset::makeRawRgba8TextureImporterDescriptor()) {
        const asharia::asset::SourceAssetRecord source =
            makeTextureImportContractRecord(sourcePath, settings, importer);
        return asharia::asset::AssetTextureImportRequest{
            .source = source,
            .settings = std::move(settings),
            .sourceBytes = std::move(bytes),
            .importer = std::move(importer),
        };
    }

    [[nodiscard]] bool expectTextureImportError(
        const asharia::Result<asharia::asset::AssetTextureImportResult>& result,
        asharia::asset::AssetTextureImportDiagnosticCode expectedCode,
        std::string_view expectedToken) {
        if (result || result.error().domain != asharia::ErrorDomain::Asset ||
            result.error().code != static_cast<int>(expectedCode) ||
            !messageContains(result.error().message, expectedToken)) {
            logFailure("Texture import contract smoke did not find the expected diagnostic.");
            return false;
        }

        return true;
    }

    [[nodiscard]] bool smokeTextureImportContractRawRgba8() {
        constexpr std::uint32_t kWidth = 2U;
        constexpr std::uint32_t kHeight = 2U;
        std::vector<asharia::asset::AssetImportSetting> settings =
            textureImportContractSettings("2", "2", "Texture 2D", "rgba8-srgb");
        std::vector<std::uint8_t> bytes = rawRgba8TextureBytes(kWidth, kHeight);
        const std::vector<std::uint8_t> expectedBytes = bytes;

        auto result = asharia::asset::importTextureCpuPayload(makeTextureImportContractRequest(
            "Content/Textures/Crate.rgba8", std::move(settings), std::move(bytes)));
        if (!result || result->sourceExtension != asharia::asset::kTextureImportRawRgba8Extension ||
            result->importProfileName != asharia::asset::kTextureImportProfileTexture2D ||
            result->settingsVersion != asharia::asset::kTextureImportContractSettingsVersion ||
            result->productTypeName != asharia::asset::kTextureRoleTexture2D ||
            result->format != asharia::asset::AssetTextureImportFormat::Rgba8Srgb ||
            asharia::asset::assetTextureImportFormatName(result->format) !=
                asharia::asset::kTextureImportFormatRgba8Srgb ||
            result->width != kWidth || result->height != kHeight || result->mips.size() != 1U ||
            result->mips[0].level != 0U || result->mips[0].byteOffset != 0U ||
            result->mips[0].byteSize != expectedBytes.size() || result->payload != expectedBytes) {
            logFailure("Texture import contract smoke rejected valid raw RGBA8 payload.");
            return false;
        }

        return true;
    }

    [[nodiscard]] bool smokeTextureImportContractPng() {
        auto result = asharia::asset::importTextureCpuPayload(makeTextureImportContractRequest(
            "Content/Textures/Crate.png", textureImportPngSettings("Texture 2D", "rgba8-srgb"),
            validPngTextureBytes(), asharia::asset::makePngTextureImporterDescriptor()));
        const std::vector<std::uint8_t> expectedBytes{0x10U, 0x20U, 0x30U, 0xFFU};
        if (!result || result->sourceExtension != asharia::asset::kTextureImportPngExtension ||
            result->importProfileName != asharia::asset::kTextureImportProfileTexture2D ||
            result->settingsVersion != asharia::asset::kTextureImportContractSettingsVersion ||
            result->productTypeName != asharia::asset::kTextureRoleTexture2D ||
            result->format != asharia::asset::AssetTextureImportFormat::Rgba8Srgb ||
            result->width != 1U || result->height != 1U || result->mips.size() != 1U ||
            result->mips[0].level != 0U || result->mips[0].byteOffset != 0U ||
            result->mips[0].byteSize != expectedBytes.size() || result->payload != expectedBytes) {
            logFailure("Texture import contract smoke rejected valid PNG payload.");
            return false;
        }

        return true;
    }

    [[nodiscard]] bool smokeTextureImportContractDiagnostics() {
        auto unsupportedExtension =
            asharia::asset::importTextureCpuPayload(makeTextureImportContractRequest(
                "Content/Textures/Crate.png", textureImportContractSettings("2", "2"),
                rawRgba8TextureBytes(2U, 2U)));
        if (!expectTextureImportError(
                unsupportedExtension,
                asharia::asset::AssetTextureImportDiagnosticCode::UnsupportedSourceExtension,
                "supported extensions")) {
            return false;
        }

        auto unsupportedProfile =
            asharia::asset::importTextureCpuPayload(makeTextureImportContractRequest(
                "Content/Textures/Crate.rgba8", textureImportContractSettings("2", "2", "skybox"),
                rawRgba8TextureBytes(2U, 2U)));
        if (!expectTextureImportError(
                unsupportedProfile,
                asharia::asset::AssetTextureImportDiagnosticCode::UnsupportedProfile,
                "supported profiles")) {
            return false;
        }

        auto unsupportedSettingsVersion =
            asharia::asset::importTextureCpuPayload(makeTextureImportContractRequest(
                "Content/Textures/Crate.rgba8",
                textureImportContractSettings("2", "2", "Texture 2D", "rgba8-unorm", "2"),
                rawRgba8TextureBytes(2U, 2U)));
        if (!expectTextureImportError(
                unsupportedSettingsVersion,
                asharia::asset::AssetTextureImportDiagnosticCode::UnsupportedSettingsVersion,
                "settings version")) {
            return false;
        }

        auto invalidDimensions =
            asharia::asset::importTextureCpuPayload(makeTextureImportContractRequest(
                "Content/Textures/Crate.rgba8", textureImportContractSettings("0", "2"),
                rawRgba8TextureBytes(2U, 2U)));
        if (!expectTextureImportError(
                invalidDimensions,
                asharia::asset::AssetTextureImportDiagnosticCode::InvalidDimensions,
                "positive integer")) {
            return false;
        }

        auto unsupportedFormat =
            asharia::asset::importTextureCpuPayload(makeTextureImportContractRequest(
                "Content/Textures/Crate.rgba8",
                textureImportContractSettings("2", "2", "Texture 2D", "bc7-unorm"),
                rawRgba8TextureBytes(2U, 2U)));
        if (!expectTextureImportError(
                unsupportedFormat,
                asharia::asset::AssetTextureImportDiagnosticCode::UnsupportedFormat,
                "texture format")) {
            return false;
        }

        auto payloadMismatch =
            asharia::asset::importTextureCpuPayload(makeTextureImportContractRequest(
                "Content/Textures/Crate.rgba8", textureImportContractSettings("2", "2"),
                rawRgba8TextureBytes(1U, 1U)));
        if (!expectTextureImportError(
                payloadMismatch,
                asharia::asset::AssetTextureImportDiagnosticCode::PayloadSizeMismatch,
                "raw RGBA8 payload")) {
            return false;
        }

        auto decodeFailed =
            asharia::asset::importTextureCpuPayload(makeTextureImportContractRequest(
                "Content/Textures/Broken.png", textureImportPngSettings(),
                std::vector<std::uint8_t>{0x89U, 0x50U, 0x4EU, 0x47U},
                asharia::asset::makePngTextureImporterDescriptor()));
        if (!expectTextureImportError(
                decodeFailed, asharia::asset::AssetTextureImportDiagnosticCode::DecodeFailed,
                "decode PNG")) {
            return false;
        }

        auto pngUnsupportedFormat =
            asharia::asset::importTextureCpuPayload(makeTextureImportContractRequest(
                "Content/Textures/Crate.png", textureImportPngSettings("Texture 2D", "bc7-unorm"),
                validPngTextureBytes(), asharia::asset::makePngTextureImporterDescriptor()));
        if (!expectTextureImportError(
                pngUnsupportedFormat,
                asharia::asset::AssetTextureImportDiagnosticCode::UnsupportedFormat,
                "texture format")) {
            return false;
        }

        if (std::string_view{asharia::asset::assetTextureImportDiagnosticCodeName(
                asharia::asset::AssetTextureImportDiagnosticCode::PayloadSizeMismatch)} !=
            "payload-size-mismatch") {
            logFailure("Texture import contract smoke saw an unstable diagnostic label.");
            return false;
        }

        if (std::string_view{asharia::asset::assetTextureImportDiagnosticCodeName(
                asharia::asset::AssetTextureImportDiagnosticCode::DecodeFailed)} !=
            "decode-failed") {
            logFailure("Texture import contract smoke saw an unstable PNG diagnostic label.");
            return false;
        }

        return true;
    }

    [[nodiscard]] bool smokeTextureImportProfiles() {
        const asharia::asset::SourceAssetRecord texture2d = makeTextureProfileSmokeRecord(
            "cd9c0f3d-20e2-4028-a3e9-c3f42d3fd515", "Content/Textures/Albedo.png");
        const asharia::asset::SourceAssetRecord spriteSheet = makeTextureProfileSmokeRecord(
            "8d660642-764b-4403-88f2-0853208e098c", "Content/Sprites/Hero.png");
        const asharia::asset::SourceAssetRecord textureCube = makeTextureProfileSmokeRecord(
            "d15d28a5-c3cf-4876-a9c1-09220d516c53", "Content/Textures/SkyCube.png");
        const asharia::asset::SourceAssetRecord skybox = makeTextureProfileSmokeRecord(
            "a72082bc-26e3-47df-b17d-337626633212", "Content/Textures/Sunset.exr");
        const asharia::asset::SourceAssetRecord unknown = makeTextureProfileSmokeRecord(
            "f99ea6e7-1623-48d2-bb9e-a33703373de2", "Content/Textures/Mystery.png");

        const std::array texture2dSettings{
            asharia::asset::AssetImportSetting{
                .key = std::string{asharia::asset::kTextureImportProfileSettingKey},
                .value = "Texture 2D"},
        };
        const std::array spriteSheetSettings{
            asharia::asset::AssetImportSetting{
                .key = std::string{asharia::asset::kTextureImportProfileSettingKey},
                .value = std::string{asharia::asset::kTextureImportProfileSpriteSheet}},
            asharia::asset::AssetImportSetting{.key = "texture.subAsset.0.id",
                                               .value = "hero-idle-0"},
            asharia::asset::AssetImportSetting{.key = "texture.subAsset.0.name",
                                               .value = "Hero Idle 0"},
            asharia::asset::AssetImportSetting{.key = "texture.subAsset.1.id",
                                               .value = "hero-idle-1"},
            asharia::asset::AssetImportSetting{.key = "texture.subAsset.2.id",
                                               .value = "hero-idle-1"},
            asharia::asset::AssetImportSetting{.key = "texture.subAsset.2.name",
                                               .value = "Duplicate Hero Idle 1"},
        };
        const std::array textureCubeSettings{
            asharia::asset::AssetImportSetting{
                .key = std::string{asharia::asset::kTextureImportProfileSettingKey},
                .value = "cubemap"},
        };
        const std::array skyboxSettings{
            asharia::asset::AssetImportSetting{
                .key = std::string{asharia::asset::kTextureImportProfileSettingKey},
                .value = "SkyBox"},
        };
        const std::array unknownSettings{
            asharia::asset::AssetImportSetting{
                .key = std::string{asharia::asset::kTextureImportProfileSettingKey},
                .value = "array-texture"},
        };

        const asharia::asset::AssetCatalogSourceFacet texture2dFacet =
            asharia::asset::makeTextureImportCatalogSourceFacet(texture2d, texture2dSettings);
        const asharia::asset::AssetCatalogSourceFacet spriteSheetFacet =
            asharia::asset::makeTextureImportCatalogSourceFacet(spriteSheet, spriteSheetSettings);
        const asharia::asset::AssetCatalogSourceFacet textureCubeFacet =
            asharia::asset::makeTextureImportCatalogSourceFacet(textureCube, textureCubeSettings);
        const asharia::asset::AssetCatalogSourceFacet skyboxFacet =
            asharia::asset::makeTextureImportCatalogSourceFacet(skybox, skyboxSettings);
        const asharia::asset::AssetCatalogSourceFacet unknownFacet =
            asharia::asset::makeTextureImportCatalogSourceFacet(unknown, unknownSettings);

        if (asharia::asset::normalizeTextureImportProfileName("sprite_sheet") !=
                asharia::asset::kTextureImportProfileSpriteSheet ||
            asharia::asset::normalizeTextureImportProfileName("TextureCube") !=
                asharia::asset::kTextureImportProfileTextureCube ||
            asharia::asset::normalizeTextureImportProfileName(" array texture ") !=
                "array texture") {
            logFailure("Texture import profile smoke saw unstable profile normalization.");
            return false;
        }

        if (texture2dFacet.importProfileName != asharia::asset::kTextureImportProfileTexture2D ||
            texture2dFacet.assetRoleName != asharia::asset::kTextureRoleTexture2D ||
            !texture2dFacet.subAssets.empty() || !texture2dFacet.diagnostics.empty()) {
            logFailure("Texture import profile smoke rejected Texture2D metadata.");
            return false;
        }

        if (spriteSheetFacet.importProfileName !=
                asharia::asset::kTextureImportProfileSpriteSheet ||
            spriteSheetFacet.assetRoleName != asharia::asset::kTextureRoleSpriteSheet ||
            spriteSheetFacet.subAssets.size() != 2U ||
            spriteSheetFacet.subAssets[0].stableId != "hero-idle-0" ||
            spriteSheetFacet.subAssets[0].displayName != "Hero Idle 0" ||
            spriteSheetFacet.subAssets[1].stableId != "hero-idle-1" ||
            spriteSheetFacet.subAssets[1].displayName != "hero-idle-1" ||
            spriteSheetFacet.diagnostics.size() != 1U ||
            spriteSheetFacet.diagnostics[0].code !=
                asharia::asset::AssetCatalogDiagnosticCode::SourceMetadata ||
            spriteSheetFacet.diagnostics[0].message.find("duplicate stable id") ==
                std::string::npos) {
            logFailure("Texture import profile smoke rejected SpriteSheet sub-assets.");
            return false;
        }

        if (textureCubeFacet.importProfileName !=
                asharia::asset::kTextureImportProfileTextureCube ||
            textureCubeFacet.assetRoleName != asharia::asset::kTextureRoleTextureCube ||
            textureCubeFacet.sourcePath != "Content/Textures/SkyCube.png" ||
            skyboxFacet.importProfileName != asharia::asset::kTextureImportProfileSkybox ||
            skyboxFacet.assetRoleName != asharia::asset::kTextureRoleSkybox) {
            logFailure(
                "Texture import profile smoke rejected profile-driven cube or skybox metadata.");
            return false;
        }

        if (unknownFacet.importProfileName != "array-texture" ||
            unknownFacet.assetRoleName != "unknown" || unknownFacet.diagnostics.size() != 1U ||
            unknownFacet.diagnostics[0].code !=
                asharia::asset::AssetCatalogDiagnosticCode::SourceMetadata) {
            logFailure("Texture import profile smoke missed unknown profile diagnostic.");
            return false;
        }

        asharia::asset::AssetCatalog catalog;
        if (!catalog.addSource(unknown)) {
            logFailure("Texture import profile smoke failed to build catalog.");
            return false;
        }
        const std::array facets{unknownFacet};
        const asharia::asset::AssetCatalogView view = asharia::asset::buildAssetCatalogView(
            catalog, {}, asharia::asset::AssetCatalogViewOptions{.sourceFacets = facets});
        if (view.entries.size() != 1U || view.entries[0].assetRoleName != "unknown" ||
            view.entries[0].diagnostics.size() != 1U ||
            asharia::asset::assetCatalogDiagnosticCodeName(view.entries[0].diagnostics[0].code) !=
                "source-metadata") {
            logFailure("Texture import profile smoke missed catalog-visible diagnostics.");
            return false;
        }

        std::cout << "Texture import profile sub-assets: " << spriteSheetFacet.subAssets.size()
                  << '\n';
        return true;
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

    [[nodiscard]] std::vector<std::uint8_t> bytesFromText(std::string_view text) {
        std::vector<std::uint8_t> bytes;
        bytes.reserve(text.size());
        for (const char character : text) {
            bytes.push_back(static_cast<std::uint8_t>(character));
        }
        return bytes;
    }

    [[nodiscard]] bool createDirectories(const std::filesystem::path& path) {
        std::error_code createError;
        std::filesystem::create_directories(path, createError);
        if (createError) {
            logFailure("Asset pipeline smoke could not create directory: " + createError.message());
            return false;
        }

        return true;
    }

    [[nodiscard]] std::filesystem::path
    metadataSidecarPath(const std::filesystem::path& sourcePath) {
        std::filesystem::path metadataPath = sourcePath;
        metadataPath += asharia::asset::kAssetMetadataSidecarSuffix;
        return metadataPath;
    }

    [[nodiscard]] bool
    expectScanDiagnostic(const asharia::asset::AssetSourceScanResult& result,
                         asharia::asset::AssetSourceScanDiagnosticCode expectedCode,
                         std::string_view expectedToken) {
        for (const asharia::asset::AssetSourceScanDiagnostic& diagnostic : result.diagnostics) {
            if (diagnostic.code == expectedCode &&
                messageContains(diagnostic.message, expectedToken)) {
                return true;
            }
        }

        logFailure("Asset source scan smoke did not find the expected diagnostic.");
        return false;
    }

    [[nodiscard]] bool smokeSourceScanValidAndDeterministic() {
        const std::filesystem::path root = smokeRoot("asharia-asset-pipeline-smoke-source-scan");
        if (root.empty() || !prepareWorkspace(root)) {
            return false;
        }

        const std::filesystem::path contentRoot = root / "Content";
        const std::filesystem::path textureRoot = contentRoot / "Textures";
        const std::filesystem::path ignoredRoot = contentRoot / "_Generated";
        if (!createDirectories(textureRoot) || !createDirectories(ignoredRoot)) {
            return false;
        }

        const std::filesystem::path crateSource = textureRoot / "Crate.png";
        const std::filesystem::path decalSource = textureRoot / "Decal.png";
        const std::filesystem::path ignoredSource = ignoredRoot / "Ignored.png";
        if (!writeTextFile(decalSource, "decal") ||
            !writeTextFile(metadataSidecarPath(decalSource), "decal metadata") ||
            !writeTextFile(crateSource, "crate") ||
            !writeTextFile(metadataSidecarPath(crateSource), "crate metadata") ||
            !writeTextFile(ignoredSource, "ignored")) {
            return false;
        }

        const asharia::asset::AssetSourceScanRequest request{
            .sourceRoot = contentRoot,
            .sourcePathPrefix = "Content",
            .metadataSuffix = std::string{asharia::asset::kAssetMetadataSidecarSuffix},
            .ignoredDirectoryNames = {"_Generated"},
        };
        const asharia::asset::AssetSourceScanResult first =
            asharia::asset::scanAssetSourceTree(request);
        const asharia::asset::AssetSourceScanResult second =
            asharia::asset::scanAssetSourceTree(request);

        if (!first.succeeded() || first != second || first.entries.size() != 2 ||
            first.entries[0].sourcePath != "Content/Textures/Crate.png" ||
            first.entries[0].sourceFilePath != crateSource ||
            first.entries[0].metadataPath != metadataSidecarPath(crateSource) ||
            first.entries[1].sourcePath != "Content/Textures/Decal.png" ||
            first.entries[1].sourceFilePath != decalSource ||
            first.entries[1].metadataPath != metadataSidecarPath(decalSource)) {
            logFailure("Asset source scan smoke failed deterministic source scanning.");
            return false;
        }

        std::cout << "Asset source scan entries: " << first.entries.size() << '\n';
        return true;
    }

    [[nodiscard]] bool smokeSourceScanMissingMetadata() {
        const std::filesystem::path root =
            smokeRoot("asharia-asset-pipeline-smoke-source-scan-missing-metadata");
        if (root.empty() || !prepareWorkspace(root)) {
            return false;
        }

        const std::filesystem::path contentRoot = root / "Content";
        const std::filesystem::path textureRoot = contentRoot / "Textures";
        if (!createDirectories(textureRoot)) {
            return false;
        }

        const std::filesystem::path crateSource = textureRoot / "Crate.png";
        if (!writeTextFile(crateSource, "crate")) {
            return false;
        }

        const asharia::asset::AssetSourceScanResult result =
            asharia::asset::scanAssetSourceTree(asharia::asset::AssetSourceScanRequest{
                .sourceRoot = contentRoot,
                .sourcePathPrefix = "Content",
            });
        return result.entries.empty() &&
               expectScanDiagnostic(result,
                                    asharia::asset::AssetSourceScanDiagnosticCode::MissingMetadata,
                                    "missing metadata");
    }

    [[nodiscard]] bool smokeSourceScanOrphanMetadata() {
        const std::filesystem::path root =
            smokeRoot("asharia-asset-pipeline-smoke-source-scan-orphan-metadata");
        if (root.empty() || !prepareWorkspace(root)) {
            return false;
        }

        const std::filesystem::path contentRoot = root / "Content";
        const std::filesystem::path textureRoot = contentRoot / "Textures";
        if (!createDirectories(textureRoot)) {
            return false;
        }

        const std::filesystem::path ghostSource = textureRoot / "Ghost.png";
        if (!writeTextFile(metadataSidecarPath(ghostSource), "ghost metadata")) {
            return false;
        }

        const asharia::asset::AssetSourceScanResult result =
            asharia::asset::scanAssetSourceTree(asharia::asset::AssetSourceScanRequest{
                .sourceRoot = contentRoot,
                .sourcePathPrefix = "Content",
            });
        return result.entries.empty() &&
               expectScanDiagnostic(result,
                                    asharia::asset::AssetSourceScanDiagnosticCode::OrphanMetadata,
                                    "orphan metadata");
    }

    [[nodiscard]] bool smokeSourceScanInvalidRoot() {
        const std::filesystem::path root =
            smokeRoot("asharia-asset-pipeline-smoke-source-scan-invalid-root");
        if (root.empty() || !prepareWorkspace(root)) {
            return false;
        }

        const asharia::asset::AssetSourceScanResult result =
            asharia::asset::scanAssetSourceTree(asharia::asset::AssetSourceScanRequest{
                .sourceRoot = root / "MissingContent",
                .sourcePathPrefix = "Content",
            });
        return result.entries.empty() &&
               expectScanDiagnostic(result,
                                    asharia::asset::AssetSourceScanDiagnosticCode::InvalidRoot,
                                    "does not exist");
    }

    [[nodiscard]] bool smokeSourceScanInvalidPrefix() {
        const std::filesystem::path root =
            smokeRoot("asharia-asset-pipeline-smoke-source-scan-invalid-prefix");
        if (root.empty() || !prepareWorkspace(root)) {
            return false;
        }

        const std::filesystem::path contentRoot = root / "Content";
        if (!createDirectories(contentRoot)) {
            return false;
        }

        const asharia::asset::AssetSourceScanResult result =
            asharia::asset::scanAssetSourceTree(asharia::asset::AssetSourceScanRequest{
                .sourceRoot = contentRoot,
                .sourcePathPrefix = "Content\\Bad",
            });
        return result.entries.empty() &&
               expectScanDiagnostic(result,
                                    asharia::asset::AssetSourceScanDiagnosticCode::InvalidRequest,
                                    "'/' separators");
    }

    [[nodiscard]] asharia::asset::AssetProductRecord
    makeProductRecord(std::string_view guidText, std::string_view productPath,
                      std::uint64_t sourceHash, std::string_view targetProfile) {
        constexpr std::string_view kTextureTypeName = "com.asharia.asset.Texture2D";
        constexpr std::string_view kTextureImporterName = "com.asharia.importer.texture";

        auto guid = asharia::asset::parseAssetGuid(guidText);
        const auto settings = defaultSettings();
        const asharia::asset::SourceAssetRecord source{
            .guid = guid ? *guid : asharia::asset::AssetGuid{},
            .assetType = asharia::asset::makeAssetTypeId(kTextureTypeName),
            .assetTypeName = std::string{kTextureTypeName},
            .sourcePath = "Content/Textures/Crate.png",
            .importerId = asharia::asset::makeImporterId(kTextureImporterName),
            .importerName = std::string{kTextureImporterName},
            .importerVersion = asharia::asset::ImporterVersion{1},
            .sourceHash = sourceHash,
            .settingsHash = asharia::asset::hashAssetImportSettings(settings),
        };
        const std::array dependencies{
            asharia::asset::AssetDependency{
                .owner = source.guid,
                .kind = asharia::asset::AssetDependencyKind::SourceFile,
                .path = source.sourcePath,
                .hash = source.sourceHash,
            },
        };
        const std::uint64_t dependencyHash = asharia::asset::hashAssetDependencies(dependencies);
        const std::uint64_t targetProfileHash =
            asharia::asset::makeAssetTargetProfileHash(targetProfile);
        const asharia::asset::AssetProductKey productKey =
            asharia::asset::makeAssetProductKey(source, dependencyHash, targetProfileHash);

        return asharia::asset::AssetProductRecord{
            .key = productKey,
            .relativeProductPath = std::string{productPath},
            .productSizeBytes = 4096,
            .productHash = asharia::asset::hashAssetProductKey(productKey),
        };
    }

    [[nodiscard]] bool expectInvalidProductManifestRead(std::string_view text,
                                                        std::string_view expectedToken) {
        auto document = asharia::asset::readAssetProductManifestText(text);
        if (document) {
            logFailure("Asset product manifest smoke accepted invalid manifest text.");
            return false;
        }

        if (document.error().domain != asharia::ErrorDomain::Asset ||
            !messageContains(document.error().message, expectedToken)) {
            logFailure("Asset product manifest smoke produced an incomplete read diagnostic.");
            return false;
        }

        return true;
    }

    [[nodiscard]] bool
    expectInvalidProductManifestWrite(const asharia::asset::AssetProductManifestDocument& document,
                                      std::string_view expectedToken) {
        auto text = asharia::asset::writeAssetProductManifestText(document);
        if (text) {
            logFailure("Asset product manifest smoke accepted invalid manifest document.");
            return false;
        }

        if (text.error().domain != asharia::ErrorDomain::Asset ||
            !messageContains(text.error().message, expectedToken)) {
            logFailure("Asset product manifest smoke produced an incomplete write diagnostic.");
            return false;
        }

        return true;
    }

    [[nodiscard]] bool corruptFirstProductKeyHash(std::string& text) {
        constexpr std::string_view kKey = "\"productKeyHash\": \"";
        const std::size_t keyOffset = text.find(kKey);
        if (keyOffset == std::string::npos) {
            logFailure("Asset product manifest smoke could not find productKeyHash.");
            return false;
        }

        const std::size_t hashOffset = keyOffset + kKey.size();
        text.replace(hashOffset, 16, "0000000000000001");
        return true;
    }

    [[nodiscard]] asharia::asset::AssetSourceSnapshot
    makeSourceSnapshot(std::string_view sourcePath, std::uint64_t sourceHash) {
        return asharia::asset::AssetSourceSnapshot{
            .sourcePath = std::string{sourcePath},
            .sourceFilePath = std::filesystem::path{sourcePath},
            .sourceHash = sourceHash,
        };
    }

    [[nodiscard]] asharia::asset::AssetProductRecord
    makeProductFromImportRequest(const asharia::asset::AssetImportRequest& request) {
        return asharia::asset::AssetProductRecord{
            .key = request.productKey,
            .relativeProductPath = request.relativeProductPath,
            .productSizeBytes = 4096,
            .productHash = asharia::asset::hashAssetProductKey(request.productKey),
        };
    }

    [[nodiscard]] asharia::asset::AssetImportRequest
    makePlannedImportRequest(const asharia::asset::DiscoveredSourceAsset& source,
                             const asharia::asset::AssetSourceSnapshot& snapshot,
                             std::string_view targetProfile) {
        const std::array sources{source};
        const std::array snapshots{snapshot};
        const auto plan = asharia::asset::planAssetImports(
            sources, snapshots, asharia::asset::AssetProductManifestDocument{}, targetProfile);
        return plan.requests.empty() ? asharia::asset::AssetImportRequest{} : plan.requests.front();
    }

    [[nodiscard]] bool
    expectPlanDiagnostic(const asharia::asset::AssetImportPlanResult& result,
                         asharia::asset::AssetImportPlanDiagnosticCode expectedCode,
                         std::string_view expectedToken) {
        for (const asharia::asset::AssetImportPlanDiagnostic& diagnostic : result.diagnostics) {
            if (diagnostic.code == expectedCode &&
                messageContains(diagnostic.message, expectedToken)) {
                return true;
            }
        }

        logFailure("Asset import planning smoke did not find the expected diagnostic.");
        return false;
    }

    [[nodiscard]] bool
    expectPlanDiagnosticSeverity(const asharia::asset::AssetImportPlanResult& result,
                                 asharia::asset::AssetImportPlanDiagnosticCode expectedCode,
                                 asharia::asset::AssetImportPlanDiagnosticSeverity expectedSeverity,
                                 std::string_view expectedToken) {
        for (const asharia::asset::AssetImportPlanDiagnostic& diagnostic : result.diagnostics) {
            if (diagnostic.code == expectedCode && diagnostic.severity == expectedSeverity &&
                messageContains(diagnostic.message, expectedToken)) {
                return true;
            }
        }

        logFailure("Asset import planning smoke did not find the expected diagnostic severity.");
        return false;
    }

    [[nodiscard]] bool smokeImportPlanningCacheHitAndMiss() {
        const auto crate =
            makeDiscoveredSource("9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c21",
                                 "Content/Textures/Crate.png", 0x1000F00D1234CAFEULL);
        const auto decal =
            makeDiscoveredSource("785e2474-65c4-4f28-a8fb-ff8a21449a61",
                                 "Content/Textures/Decal.png", 0x2000F00D1234CAFEULL);
        const auto crateSnapshot =
            makeSourceSnapshot(crate.source.sourcePath, 0x1000F00D1234CAFEULL);
        const auto decalSnapshot =
            makeSourceSnapshot(decal.source.sourcePath, 0x2000F00D1234CAFEULL);
        const auto crateRequest =
            makePlannedImportRequest(crate, crateSnapshot, "windows-msvc-debug");
        const asharia::asset::AssetProductManifestDocument manifest{
            .products = {makeProductFromImportRequest(crateRequest)},
        };

        const std::array sources{crate, decal};
        const std::array snapshots{crateSnapshot, decalSnapshot};
        const auto first =
            asharia::asset::planAssetImports(sources, snapshots, manifest, "windows-msvc-debug");
        const auto second =
            asharia::asset::planAssetImports(sources, snapshots, manifest, "windows-msvc-debug");

        if (!first.succeeded() || first != second || first.cacheHits.size() != 1 ||
            first.requests.size() != 1 ||
            first.cacheHits.front().product.key != crateRequest.productKey ||
            first.requests.front().source.sourcePath != decal.source.sourcePath ||
            first.requests.front().reason !=
                asharia::asset::AssetImportRequestReason::MissingProduct ||
            first.requests.front().relativeProductPath.find("windows-msvc-debug/products/") != 0) {
            logFailure("Asset import planning smoke failed cache hit/miss planning.");
            return false;
        }

        std::cout << "Asset import planning hits: " << first.cacheHits.size()
                  << " requests: " << first.requests.size() << '\n';
        return true;
    }

    [[nodiscard]] bool smokeImportPlanningSourceChanged() {
        const auto source =
            makeDiscoveredSource("9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c21",
                                 "Content/Textures/Crate.png", 0x1000F00D1234CAFEULL);
        const auto oldSnapshot =
            makeSourceSnapshot(source.source.sourcePath, 0x1000F00D1234CAFEULL);
        const auto changedSnapshot =
            makeSourceSnapshot(source.source.sourcePath, 0x1000F00D1234CAFFULL);
        const auto oldRequest = makePlannedImportRequest(source, oldSnapshot, "windows-msvc-debug");
        const asharia::asset::AssetProductManifestDocument manifest{
            .products = {makeProductFromImportRequest(oldRequest)},
        };
        const std::array sources{source};
        const std::array snapshots{changedSnapshot};
        const auto plan =
            asharia::asset::planAssetImports(sources, snapshots, manifest, "windows-msvc-debug");

        if (!plan.succeeded() || !plan.cacheHits.empty() || plan.requests.size() != 1 ||
            plan.requests.front().reason !=
                asharia::asset::AssetImportRequestReason::SourceChanged ||
            plan.requests.front().source.sourceHash != changedSnapshot.sourceHash) {
            logFailure("Asset import planning smoke missed a source hash change.");
            return false;
        }

        return true;
    }

    [[nodiscard]] bool smokeImportPlanningMetadataSourceHashDriftWarning() {
        const auto source =
            makeDiscoveredSource("9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c21",
                                 "Content/Textures/Crate.png", 0x1000F00D1234CAFEULL);
        const auto currentSnapshot =
            makeSourceSnapshot(source.source.sourcePath, 0x2000F00D1234CAFEULL);
        const std::array sources{source};
        const std::array snapshots{currentSnapshot};
        const asharia::asset::AssetImportPlanResult plan = asharia::asset::planAssetImports(
            sources, snapshots, asharia::asset::AssetProductManifestDocument{},
            "windows-msvc-debug");

        if (!plan.succeeded() || plan.requests.size() != 1U ||
            plan.requests.front().source.sourceHash != currentSnapshot.sourceHash ||
            plan.requests.front().productKey.sourceHash != currentSnapshot.sourceHash ||
            plan.diagnostics.size() != 1U ||
            !expectPlanDiagnosticSeverity(
                plan, asharia::asset::AssetImportPlanDiagnosticCode::MetadataSourceHashDrift,
                asharia::asset::AssetImportPlanDiagnosticSeverity::Warning,
                "current snapshot hash")) {
            logFailure("Asset import planning smoke missed metadata sourceHash drift warning.");
            return false;
        }

        return true;
    }

    [[nodiscard]] bool smokeImportPlanningSettingsChanged() {
        const auto base =
            makeDiscoveredSource("9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c21",
                                 "Content/Textures/Crate.png", 0x1000F00D1234CAFEULL, "auto");
        const auto changed =
            makeDiscoveredSource("9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c21",
                                 "Content/Textures/Crate.png", 0x1000F00D1234CAFEULL, "bc7");
        const auto snapshot = makeSourceSnapshot(base.source.sourcePath, 0x1000F00D1234CAFEULL);
        const auto baseRequest = makePlannedImportRequest(base, snapshot, "windows-msvc-debug");
        const asharia::asset::AssetProductManifestDocument manifest{
            .products = {makeProductFromImportRequest(baseRequest)},
        };
        const std::array sources{changed};
        const std::array snapshots{snapshot};
        const auto plan =
            asharia::asset::planAssetImports(sources, snapshots, manifest, "windows-msvc-debug");

        if (!plan.succeeded() || !plan.cacheHits.empty() || plan.requests.size() != 1 ||
            plan.requests.front().reason !=
                asharia::asset::AssetImportRequestReason::SettingsChanged) {
            logFailure("Asset import planning smoke missed an import settings change.");
            return false;
        }

        return true;
    }

    [[nodiscard]] bool smokeImportPlanningMissingSnapshot() {
        const auto source =
            makeDiscoveredSource("9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c21",
                                 "Content/Textures/Crate.png", 0x1000F00D1234CAFEULL);
        const std::array sources{source};
        const std::array<asharia::asset::AssetSourceSnapshot, 0> snapshots{};
        const auto plan = asharia::asset::planAssetImports(
            sources, snapshots, asharia::asset::AssetProductManifestDocument{},
            "windows-msvc-debug");
        return plan.requests.empty() && plan.cacheHits.empty() &&
               expectPlanDiagnostic(
                   plan, asharia::asset::AssetImportPlanDiagnosticCode::MissingSourceSnapshot,
                   "missing a source snapshot");
    }

    [[nodiscard]] bool smokeImportPlanningDuplicateSource() {
        const auto source =
            makeDiscoveredSource("9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c21",
                                 "Content/Textures/Crate.png", 0x1000F00D1234CAFEULL);
        const std::array sources{source, source};
        const std::array snapshots{
            makeSourceSnapshot(source.source.sourcePath, 0x1000F00D1234CAFEULL),
        };
        const auto plan = asharia::asset::planAssetImports(
            sources, snapshots, asharia::asset::AssetProductManifestDocument{},
            "windows-msvc-debug");
        return plan.requests.empty() && plan.cacheHits.empty() &&
               expectPlanDiagnostic(plan,
                                    asharia::asset::AssetImportPlanDiagnosticCode::DuplicateSource,
                                    "duplicates source");
    }

    [[nodiscard]] bool smokeImportPlanningDuplicateSnapshot() {
        const auto source =
            makeDiscoveredSource("9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c21",
                                 "Content/Textures/Crate.png", 0x1000F00D1234CAFEULL);
        const std::array sources{source};
        const std::array snapshots{
            makeSourceSnapshot(source.source.sourcePath, 0x1000F00D1234CAFEULL),
            makeSourceSnapshot(source.source.sourcePath, 0x2000F00D1234CAFEULL),
        };
        const auto plan = asharia::asset::planAssetImports(
            sources, snapshots, asharia::asset::AssetProductManifestDocument{},
            "windows-msvc-debug");
        return plan.requests.empty() && plan.cacheHits.empty() &&
               expectPlanDiagnostic(
                   plan, asharia::asset::AssetImportPlanDiagnosticCode::DuplicateSourceSnapshot,
                   "duplicates source path");
    }

    [[nodiscard]] bool smokeImportPlanningInvalidTargetProfile() {
        const auto source =
            makeDiscoveredSource("9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c21",
                                 "Content/Textures/Crate.png", 0x1000F00D1234CAFEULL);
        const std::array sources{source};
        const std::array snapshots{
            makeSourceSnapshot(source.source.sourcePath, 0x1000F00D1234CAFEULL),
        };
        const auto plan = asharia::asset::planAssetImports(
            sources, snapshots, asharia::asset::AssetProductManifestDocument{},
            "windows-msvc-debug\\bad");
        return plan.requests.empty() && plan.cacheHits.empty() &&
               expectPlanDiagnostic(
                   plan, asharia::asset::AssetImportPlanDiagnosticCode::InvalidTargetProfile,
                   "single path segment");
    }

    [[nodiscard]] bool smokeProductManifestRoundTrip() {
        const std::filesystem::path root =
            smokeRoot("asharia-asset-pipeline-smoke-product-manifest-roundtrip");
        if (root.empty() || !prepareWorkspace(root)) {
            return false;
        }

        const asharia::asset::AssetProductManifestDocument document{
            .products =
                {
                    makeProductRecord("9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c21",
                                      "windows-msvc-debug/textures/crate.texture.bin",
                                      0x1000F00D1234CAFEULL, "windows-msvc-debug"),
                    makeProductRecord("785e2474-65c4-4f28-a8fb-ff8a21449a61",
                                      "windows-msvc-debug/textures/decal.texture.bin",
                                      0x2000F00D1234CAFEULL, "windows-msvc-debug"),
                },
        };

        const auto firstText = asharia::asset::writeAssetProductManifestText(document);
        const auto secondText = asharia::asset::writeAssetProductManifestText(document);
        if (!firstText || !secondText || *firstText != *secondText) {
            logFailure(firstText ? "Asset product manifest smoke failed deterministic write."
                                 : firstText.error().message);
            return false;
        }

        const auto parsed = asharia::asset::readAssetProductManifestText(*firstText);
        if (!parsed || *parsed != document) {
            logFailure(parsed ? "Asset product manifest smoke failed text round-trip."
                              : parsed.error().message);
            return false;
        }

        const std::filesystem::path manifestPath = root / "products.aproducts";
        if (auto written = asharia::asset::writeAssetProductManifestFile(manifestPath, document);
            !written) {
            logFailure(written.error().message);
            return false;
        }
        const auto fileParsed = asharia::asset::readAssetProductManifestFile(manifestPath);
        if (!fileParsed || *fileParsed != document) {
            logFailure(fileParsed ? "Asset product manifest smoke failed file round-trip."
                                  : fileParsed.error().message);
            return false;
        }

        std::cout << "Asset product manifest products: " << document.products.size() << '\n';
        return true;
    }

    [[nodiscard]] bool smokeProductManifestMalformedInput() {
        return expectInvalidProductManifestRead("{", "Failed to read asset product manifest");
    }

    [[nodiscard]] bool smokeProductManifestDuplicateField() {
        const std::string duplicateSchema = R"json({
  "schema": "com.asharia.asset.product-manifest",
  "schema": "com.asharia.asset.product-manifest",
  "schemaVersion": 1,
  "products": []
}
)json";
        return expectInvalidProductManifestRead(duplicateSchema, "duplicate key");
    }

    [[nodiscard]] bool smokeProductManifestMissingField() {
        const std::string missingGuid = R"json({
  "schema": "com.asharia.asset.product-manifest",
  "schemaVersion": 1,
  "products": [
    {
      "assetType": "1111111111111111",
      "importerId": "2222222222222222",
      "importerVersion": 1,
      "sourceHash": "3333333333333333",
      "settingsHash": "4444444444444444",
      "dependencyHash": "5555555555555555",
      "targetProfileHash": "6666666666666666",
      "productKeyHash": "7777777777777777",
      "productPath": "windows-msvc-debug/textures/crate.texture.bin",
      "productSizeBytes": 4096,
      "productHash": "8888888888888888"
    }
  ]
}
)json";
        return expectInvalidProductManifestRead(missingGuid, "guid");
    }

    [[nodiscard]] bool smokeProductManifestUnknownField() {
        const auto product = makeProductRecord("9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c21",
                                               "windows-msvc-debug/textures/crate.texture.bin",
                                               0x1000F00D1234CAFEULL, "windows-msvc-debug");
        auto text = asharia::asset::writeAssetProductManifestText(
            asharia::asset::AssetProductManifestDocument{.products = {product}});
        if (!text) {
            logFailure(text.error().message);
            return false;
        }

        const std::size_t fieldOffset = text->find("\"productPath\"");
        if (fieldOffset == std::string::npos) {
            logFailure("Asset product manifest smoke could not find productPath.");
            return false;
        }
        text->replace(fieldOffset, std::string_view{"\"productPath\""}.size(),
                      "\"runtimePointer\"");
        return expectInvalidProductManifestRead(*text, "unknown member");
    }

    [[nodiscard]] bool smokeProductManifestDuplicateProductKey() {
        const auto product = makeProductRecord("9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c21",
                                               "windows-msvc-debug/textures/crate.texture.bin",
                                               0x1000F00D1234CAFEULL, "windows-msvc-debug");
        auto duplicate = product;
        duplicate.relativeProductPath = "windows-msvc-debug/textures/crate-copy.texture.bin";
        return expectInvalidProductManifestWrite(
            asharia::asset::AssetProductManifestDocument{.products = {product, duplicate}},
            "duplicates product key");
    }

    [[nodiscard]] bool smokeProductManifestDuplicateProductPath() {
        const auto productA = makeProductRecord("9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c21",
                                                "windows-msvc-debug/textures/crate.texture.bin",
                                                0x1000F00D1234CAFEULL, "windows-msvc-debug");
        auto productB = makeProductRecord("785e2474-65c4-4f28-a8fb-ff8a21449a61",
                                          "windows-msvc-debug/textures/decal.texture.bin",
                                          0x2000F00D1234CAFEULL, "windows-msvc-debug");
        productB.relativeProductPath = productA.relativeProductPath;
        return expectInvalidProductManifestWrite(
            asharia::asset::AssetProductManifestDocument{.products = {productA, productB}},
            "duplicates product path");
    }

    [[nodiscard]] bool smokeProductManifestInvalidProductPath() {
        auto product = makeProductRecord("9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c21",
                                         "windows-msvc-debug\\textures\\crate.texture.bin",
                                         0x1000F00D1234CAFEULL, "windows-msvc-debug");
        return expectInvalidProductManifestWrite(
            asharia::asset::AssetProductManifestDocument{.products = {product}}, "'/' separators");
    }

    [[nodiscard]] bool smokeProductManifestProductKeyHashMismatch() {
        const auto product = makeProductRecord("9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c21",
                                               "windows-msvc-debug/textures/crate.texture.bin",
                                               0x1000F00D1234CAFEULL, "windows-msvc-debug");
        auto text = asharia::asset::writeAssetProductManifestText(
            asharia::asset::AssetProductManifestDocument{.products = {product}});
        if (!text) {
            logFailure(text.error().message);
            return false;
        }

        if (!corruptFirstProductKeyHash(*text)) {
            return false;
        }
        return expectInvalidProductManifestRead(*text, "product key hash mismatch");
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

    [[nodiscard]] bool writeScannedPlanningSource(const std::filesystem::path& contentRoot,
                                                  std::string_view relativePath,
                                                  std::string_view bytes, std::string_view guidText,
                                                  std::uint64_t sourceHash) {
        const std::filesystem::path sourceFile =
            contentRoot / std::filesystem::path{std::string{relativePath}};
        if (!createDirectories(sourceFile.parent_path()) || !writeTextFile(sourceFile, bytes)) {
            return false;
        }

        const std::string sourcePath =
            "Content/" + std::filesystem::path{std::string{relativePath}}.generic_string();
        const asharia::asset::AssetMetadataDocument document =
            makeDocument(guidText, sourcePath, sourceHash);
        return writeDocument(metadataSidecarPath(sourceFile), document);
    }

    [[nodiscard]] asharia::asset::AssetScannedImportPlanRequest
    makeScannedPlanningRequest(const std::filesystem::path& contentRoot,
                               asharia::asset::AssetProductManifestDocument productManifest,
                               std::string_view targetProfile) {
        return asharia::asset::AssetScannedImportPlanRequest{
            .scan =
                asharia::asset::AssetSourceScanRequest{
                    .sourceRoot = contentRoot,
                    .sourcePathPrefix = "Content",
                    .metadataSuffix = std::string{asharia::asset::kAssetMetadataSidecarSuffix},
                },
            .productManifest = std::move(productManifest),
            .targetProfile = std::string{targetProfile},
        };
    }

    [[nodiscard]] bool smokeScannedImportPlanningRequestsAndCacheHits() {
        const std::filesystem::path root =
            smokeRoot("asharia-asset-pipeline-smoke-scanned-planning");
        if (root.empty() || !prepareWorkspace(root)) {
            return false;
        }

        const std::filesystem::path contentRoot = root / "Content";
        if (!writeScannedPlanningSource(contentRoot, "Textures/Decal.png", "decal bytes",
                                        "785e2474-65c4-4f28-a8fb-ff8a21449a61",
                                        0x2000F00D1234CAFEULL) ||
            !writeScannedPlanningSource(contentRoot, "Textures/Crate.png", "crate bytes",
                                        "9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c21",
                                        0x1000F00D1234CAFEULL)) {
            return false;
        }

        const asharia::asset::AssetScannedImportPlanResult first =
            asharia::asset::planScannedAssetImports(makeScannedPlanningRequest(
                contentRoot, asharia::asset::AssetProductManifestDocument{}, "windows-msvc-debug"));
        if (!first.succeeded() || first.scan.entries.size() != 2 ||
            first.discovery.manifest.records.size() != 2 || first.snapshot.snapshots.size() != 2 ||
            first.plan.requests.size() != 2 || !first.plan.cacheHits.empty() ||
            first.plan.requests[0].source.sourcePath != "Content/Textures/Crate.png" ||
            first.plan.requests[1].source.sourcePath != "Content/Textures/Decal.png" ||
            first.plan.requests[0].reason !=
                asharia::asset::AssetImportRequestReason::MissingProduct ||
            first.plan.requests[1].reason !=
                asharia::asset::AssetImportRequestReason::MissingProduct) {
            logFailure("Asset scanned import planning smoke failed request planning.");
            return false;
        }

        const asharia::asset::AssetProductManifestDocument manifest{
            .products = {makeProductFromImportRequest(first.plan.requests.front())},
        };
        const asharia::asset::AssetScannedImportPlanResult second =
            asharia::asset::planScannedAssetImports(
                makeScannedPlanningRequest(contentRoot, manifest, "windows-msvc-debug"));
        if (!second.succeeded() || second.scan != first.scan || second.plan.cacheHits.size() != 1 ||
            second.plan.requests.size() != 1 ||
            second.plan.cacheHits.front().source.sourcePath != "Content/Textures/Crate.png" ||
            second.plan.requests.front().source.sourcePath != "Content/Textures/Decal.png") {
            logFailure("Asset scanned import planning smoke failed cache-hit planning.");
            return false;
        }

        std::cout << "Asset scanned import plan requests: " << first.plan.requests.size() << '\n';
        return true;
    }

    [[nodiscard]] bool smokeScannedImportPlanningStopsOnScanDiagnostics() {
        const std::filesystem::path root =
            smokeRoot("asharia-asset-pipeline-smoke-scanned-planning-scan-diagnostic");
        if (root.empty() || !prepareWorkspace(root)) {
            return false;
        }

        const std::filesystem::path contentRoot = root / "Content";
        const std::filesystem::path sourceFile = contentRoot / "Textures" / "Crate.png";
        if (!createDirectories(sourceFile.parent_path()) || !writeTextFile(sourceFile, "crate")) {
            return false;
        }

        const asharia::asset::AssetScannedImportPlanResult result =
            asharia::asset::planScannedAssetImports(makeScannedPlanningRequest(
                contentRoot, asharia::asset::AssetProductManifestDocument{}, "windows-msvc-debug"));
        return !result.succeeded() && result.discovery.manifest.records.empty() &&
               result.snapshot.snapshots.empty() && result.plan.requests.empty() &&
               result.plan.cacheHits.empty() && result.plan.diagnostics.empty() &&
               expectScanDiagnostic(result.scan,
                                    asharia::asset::AssetSourceScanDiagnosticCode::MissingMetadata,
                                    "missing metadata");
    }

    [[nodiscard]] bool smokeScannedImportPlanningStopsOnDiscoveryDiagnostics() {
        const std::filesystem::path root =
            smokeRoot("asharia-asset-pipeline-smoke-scanned-planning-discovery-diagnostic");
        if (root.empty() || !prepareWorkspace(root)) {
            return false;
        }

        const std::filesystem::path contentRoot = root / "Content";
        const std::filesystem::path sourceFile = contentRoot / "Textures" / "Broken.png";
        if (!createDirectories(sourceFile.parent_path()) || !writeTextFile(sourceFile, "broken") ||
            !writeTextFile(metadataSidecarPath(sourceFile), "{")) {
            return false;
        }

        const asharia::asset::AssetScannedImportPlanResult result =
            asharia::asset::planScannedAssetImports(makeScannedPlanningRequest(
                contentRoot, asharia::asset::AssetProductManifestDocument{}, "windows-msvc-debug"));
        return !result.succeeded() && result.scan.succeeded() &&
               result.snapshot.snapshots.empty() && result.plan.requests.empty() &&
               result.plan.cacheHits.empty() && result.plan.diagnostics.empty() &&
               expectSingleDiagnostic(
                   result.discovery,
                   asharia::asset::AssetSourceDiscoveryDiagnosticCode::MetadataReadFailed,
                   "failed to read metadata");
    }

    [[nodiscard]] bool smokeScannedImportPlanningPlanDiagnostics() {
        const std::filesystem::path root =
            smokeRoot("asharia-asset-pipeline-smoke-scanned-planning-plan-diagnostic");
        if (root.empty() || !prepareWorkspace(root)) {
            return false;
        }

        const std::filesystem::path contentRoot = root / "Content";
        if (!writeScannedPlanningSource(contentRoot, "Textures/Crate.png", "crate bytes",
                                        "9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c21",
                                        0x1000F00D1234CAFEULL)) {
            return false;
        }

        const asharia::asset::AssetScannedImportPlanResult result =
            asharia::asset::planScannedAssetImports(makeScannedPlanningRequest(
                contentRoot, asharia::asset::AssetProductManifestDocument{},
                "windows-msvc-debug\\bad"));
        return !result.succeeded() && result.scan.succeeded() && result.discovery.succeeded() &&
               result.snapshot.succeeded() &&
               expectPlanDiagnostic(
                   result.plan, asharia::asset::AssetImportPlanDiagnosticCode::InvalidTargetProfile,
                   "target profile must be a single path segment");
    }

    [[nodiscard]] bool
    expectExecutionDiagnostic(const asharia::asset::AssetProductExecutionResult& result,
                              asharia::asset::AssetProductExecutionDiagnosticCode expectedCode,
                              std::string_view expectedToken) {
        for (const asharia::asset::AssetProductExecutionDiagnostic& diagnostic :
             result.diagnostics) {
            if (diagnostic.code == expectedCode &&
                messageContains(diagnostic.message, expectedToken)) {
                return true;
            }
        }

        logFailure("Asset product execution smoke did not find the expected diagnostic.");
        return false;
    }

    [[nodiscard]] bool
    expectProductBlobError(const asharia::Result<asharia::asset::AssetProductBlobPayload>& result,
                           asharia::asset::AssetProductBlobDiagnosticCode expectedCode,
                           std::string_view expectedToken) {
        if (result || result.error().domain != asharia::ErrorDomain::Asset ||
            result.error().code != static_cast<int>(expectedCode) ||
            !messageContains(result.error().message, expectedToken)) {
            logFailure("Asset product blob smoke did not find the expected diagnostic.");
            return false;
        }

        return true;
    }

    [[nodiscard]] bool smokeProductExecutionWritesDeterministicProducts() {
        const std::filesystem::path root =
            smokeRoot("asharia-asset-pipeline-smoke-product-execution");
        if (root.empty() || !prepareWorkspace(root)) {
            return false;
        }

        const std::filesystem::path contentRoot = root / "Content";
        if (!writeScannedPlanningSource(contentRoot, "Textures/Decal.png", "decal bytes",
                                        "785e2474-65c4-4f28-a8fb-ff8a21449a61",
                                        0x2000F00D1234CAFEULL) ||
            !writeScannedPlanningSource(contentRoot, "Textures/Crate.png", "crate bytes",
                                        "9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c21",
                                        0x1000F00D1234CAFEULL)) {
            return false;
        }

        const asharia::asset::AssetScannedImportPlanResult plan =
            asharia::asset::planScannedAssetImports(makeScannedPlanningRequest(
                contentRoot, asharia::asset::AssetProductManifestDocument{}, "windows-msvc-debug"));
        if (!plan.succeeded() || plan.plan.requests.size() != 2) {
            logFailure("Asset product execution smoke could not build import requests.");
            return false;
        }

        const std::vector<asharia::asset::AssetProductSourceBytes> sourceBytes{
            asharia::asset::AssetProductSourceBytes{
                .sourcePath = "Content/Textures/Decal.png",
                .bytes = bytesFromText("decal bytes"),
            },
            asharia::asset::AssetProductSourceBytes{
                .sourcePath = "Content/Textures/Crate.png",
                .bytes = bytesFromText("crate bytes"),
            },
        };
        const std::filesystem::path outputRoot = root / "ProductCache";
        const std::filesystem::path manifestPath = outputRoot / "product-manifest.json";

        const asharia::asset::AssetProductExecutionResult first =
            asharia::asset::executeAssetProducts(asharia::asset::AssetProductExecutionRequest{
                .plan = plan.plan,
                .existingManifest = {},
                .sourceBytes = sourceBytes,
                .productOutputRoot = outputRoot,
                .productManifestOutputPath = manifestPath,
            });
        const asharia::asset::AssetProductExecutionResult second =
            asharia::asset::executeAssetProducts(asharia::asset::AssetProductExecutionRequest{
                .plan = plan.plan,
                .existingManifest = {},
                .sourceBytes = sourceBytes,
                .productOutputRoot = outputRoot,
                .productManifestOutputPath = manifestPath,
            });

        auto firstText = asharia::asset::writeAssetProductManifestText(first.manifest);
        auto secondText = asharia::asset::writeAssetProductManifestText(second.manifest);
        if (!first.succeeded() || !second.succeeded() || first.writtenProducts.size() != 2 ||
            second.writtenProducts.size() != 2 || first.manifest.products.size() != 2 ||
            !first.manifestWritten || !second.manifestWritten || !firstText || !secondText ||
            *firstText != *secondText) {
            logFailure("Asset product execution smoke failed deterministic product writes.");
            return false;
        }

        for (const asharia::asset::AssetProductWrite& product : first.writtenProducts) {
            std::error_code existsError;
            if (!std::filesystem::exists(product.productFilePath, existsError) || existsError) {
                logFailure("Asset product execution smoke did not write product file.");
                return false;
            }

            const auto expectedSource = std::ranges::find_if(
                sourceBytes, [&product](const asharia::asset::AssetProductSourceBytes& source) {
                    return source.sourcePath == product.source.sourcePath;
                });
            auto payload = asharia::asset::readPlaceholderProductSourceBytes(
                asharia::asset::AssetProductBlobReadRequest{
                    .productFilePath = product.productFilePath,
                    .relativeProductPath = product.product.relativeProductPath,
                });
            if (expectedSource == sourceBytes.end() || !payload ||
                payload->sourceBytes != expectedSource->bytes) {
                logFailure("Asset product execution smoke could not read product payload.");
                return false;
            }
        }

        auto parsedManifest = asharia::asset::readAssetProductManifestFile(manifestPath);
        if (!parsedManifest || *parsedManifest != first.manifest) {
            logFailure(parsedManifest ? "Asset product execution smoke manifest mismatch."
                                      : parsedManifest.error().message);
            return false;
        }

        const asharia::asset::AssetScannedImportPlanResult cachePlan =
            asharia::asset::planScannedAssetImports(
                makeScannedPlanningRequest(contentRoot, first.manifest, "windows-msvc-debug"));
        const asharia::asset::AssetProductExecutionResult cacheExecution =
            asharia::asset::executeAssetProducts(asharia::asset::AssetProductExecutionRequest{
                .plan = cachePlan.plan,
                .existingManifest = first.manifest,
                .sourceBytes = sourceBytes,
                .productOutputRoot = outputRoot,
                .productManifestOutputPath = manifestPath,
            });

        if (!cacheExecution.succeeded() || !cacheExecution.writtenProducts.empty() ||
            cacheExecution.cacheHits.size() != 2 || cacheExecution.manifest != first.manifest) {
            logFailure("Asset product execution smoke failed unchanged-input cache hit rerun.");
            return false;
        }

        std::cout << "Asset product execution products: " << first.writtenProducts.size() << '\n';
        return true;
    }

    [[nodiscard]] bool smokeProductBlobReadDiagnostics() {
        const std::filesystem::path root =
            smokeRoot("asharia-asset-pipeline-smoke-product-blob-read");
        if (root.empty() || !prepareWorkspace(root)) {
            return false;
        }

        const std::filesystem::path missingProduct = root / "missing.product";
        auto missing = asharia::asset::readPlaceholderProductSourceBytes(
            asharia::asset::AssetProductBlobReadRequest{
                .productFilePath = missingProduct,
                .relativeProductPath = "textures/missing.product",
            });
        if (!expectProductBlobError(missing,
                                    asharia::asset::AssetProductBlobDiagnosticCode::MissingProduct,
                                    "missing from the product cache")) {
            return false;
        }

        auto empty =
            asharia::asset::readPlaceholderProductSourceBytes({}, "textures/empty.product");
        if (!expectProductBlobError(
                empty, asharia::asset::AssetProductBlobDiagnosticCode::InvalidProductBlob,
                "is empty")) {
            return false;
        }

        const std::vector<std::uint8_t> noPayload = bytesFromText("schema=placeholder\n");
        auto missingPayload = asharia::asset::readPlaceholderProductSourceBytes(
            std::span<const std::uint8_t>{noPayload.data(), noPayload.size()},
            "textures/no-payload.product");
        if (!expectProductBlobError(missingPayload,
                                    asharia::asset::AssetProductBlobDiagnosticCode::MissingPayload,
                                    "sourceBytes.begin")) {
            return false;
        }

        const std::vector<std::uint8_t> unterminated =
            bytesFromText("schema=placeholder\nsourceBytes.begin\nabc");
        auto unterminatedPayload = asharia::asset::readPlaceholderProductSourceBytes(
            std::span<const std::uint8_t>{unterminated.data(), unterminated.size()},
            "textures/unterminated.product");
        if (!expectProductBlobError(
                unterminatedPayload,
                asharia::asset::AssetProductBlobDiagnosticCode::UnterminatedPayload,
                "unterminated sourceBytes payload")) {
            return false;
        }

        if (std::string_view{asharia::asset::assetProductBlobDiagnosticCodeName(
                asharia::asset::AssetProductBlobDiagnosticCode::ProductReadFailed)} !=
            "product-read-failed") {
            logFailure("Asset product blob smoke saw an unstable diagnostic label.");
            return false;
        }

        return true;
    }

    [[nodiscard]] bool smokeProductExecutionSourceBytesHashMismatch() {
        const std::filesystem::path root =
            smokeRoot("asharia-asset-pipeline-smoke-product-execution-hash-mismatch");
        if (root.empty() || !prepareWorkspace(root)) {
            return false;
        }

        const std::filesystem::path contentRoot = root / "Content";
        if (!writeScannedPlanningSource(contentRoot, "Textures/Crate.png", "crate bytes",
                                        "9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c21",
                                        0x1000F00D1234CAFEULL)) {
            return false;
        }

        const asharia::asset::AssetScannedImportPlanResult plan =
            asharia::asset::planScannedAssetImports(makeScannedPlanningRequest(
                contentRoot, asharia::asset::AssetProductManifestDocument{}, "windows-msvc-debug"));
        if (!plan.succeeded() || plan.plan.requests.size() != 1) {
            logFailure("Asset product execution mismatch smoke could not build import request.");
            return false;
        }

        const std::vector<asharia::asset::AssetProductSourceBytes> sourceBytes{
            asharia::asset::AssetProductSourceBytes{
                .sourcePath = "Content/Textures/Crate.png",
                .bytes = bytesFromText("different crate bytes"),
            },
        };
        const asharia::asset::AssetProductExecutionResult execution =
            asharia::asset::executeAssetProducts(asharia::asset::AssetProductExecutionRequest{
                .plan = plan.plan,
                .existingManifest = {},
                .sourceBytes = sourceBytes,
                .productOutputRoot = root / "ProductCache",
                .productManifestOutputPath = root / "ProductCache" / "product-manifest.json",
            });

        return execution.writtenProducts.empty() &&
               expectExecutionDiagnostic(
                   execution,
                   asharia::asset::AssetProductExecutionDiagnosticCode::SourceBytesHashMismatch,
                   "hash mismatch");
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
    const bool passed =
        smokeSourceScanValidAndDeterministic() && smokeSourceScanMissingMetadata() &&
        smokeSourceScanOrphanMetadata() && smokeSourceScanInvalidRoot() &&
        smokeSourceScanInvalidPrefix() && smokeScannedImportPlanningRequestsAndCacheHits() &&
        smokeScannedImportPlanningStopsOnScanDiagnostics() &&
        smokeScannedImportPlanningStopsOnDiscoveryDiagnostics() &&
        smokeScannedImportPlanningPlanDiagnostics() &&
        smokeProductExecutionWritesDeterministicProducts() && smokeProductBlobReadDiagnostics() &&
        smokeProductExecutionSourceBytesHashMismatch() && smokeImportPlanningCacheHitAndMiss() &&
        smokeImportPlanningSourceChanged() && smokeImportPlanningMetadataSourceHashDriftWarning() &&
        smokeImportPlanningSettingsChanged() && smokeImportPlanningMissingSnapshot() &&
        smokeImportPlanningDuplicateSource() && smokeImportPlanningDuplicateSnapshot() &&
        smokeImportPlanningInvalidTargetProfile() && smokeTextureImportContractRawRgba8() &&
        smokeTextureImportContractPng() && smokeTextureImportContractDiagnostics() &&
        smokeTextureImportProfiles() && smokeProductManifestRoundTrip() &&
        smokeProductManifestMalformedInput() && smokeProductManifestDuplicateField() &&
        smokeProductManifestMissingField() && smokeProductManifestUnknownField() &&
        smokeProductManifestDuplicateProductKey() && smokeProductManifestDuplicateProductPath() &&
        smokeProductManifestInvalidProductPath() && smokeProductManifestProductKeyHashMismatch() &&
        smokeSourceSnapshotValidAndDeterministic() && smokeSourceSnapshotContentChange() &&
        smokeSourceSnapshotMissingFile() && smokeSourceSnapshotDirectory() &&
        smokeSourceSnapshotInvalidSourcePath() && smokeSourceSnapshotDuplicateSourcePath() &&
        smokeDiscoveryValidAndDeterministic() && smokeMissingMetadata() &&
        smokeMalformedMetadata() && smokeSourcePathMismatch() && smokeDuplicateGuid() &&
        smokeDuplicateSourcePath() && smokeInvalidEntry() && smokeInvalidEntrySourcePath() &&
        smokeInvalidMetadataSourcePath();
    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
