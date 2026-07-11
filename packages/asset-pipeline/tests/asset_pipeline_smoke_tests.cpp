#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <span>
#include <sstream>
#include <streambuf>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <process.h>
#else
#include <unistd.h>
#endif

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
#include "asharia/asset_pipeline/asset_tool_fingerprint.hpp"

#include "asset_product_blob_limits.hpp"
#include "asset_product_publication.hpp"
#include "asset_tool_fingerprint_internal.hpp"

namespace {

    void logFailure(std::string_view message) {
        std::cerr << message << '\n';
    }

    [[nodiscard]] bool messageContains(std::string_view message, std::string_view token) {
        return message.find(token) != std::string_view::npos;
    }

    [[nodiscard]] bool messageHasFinalPath(std::string_view message,
                                           const std::filesystem::path& path) {
        const std::u8string utf8 = path.generic_u8string();
        return messageContains(message,
                               "finalPath=\"" + std::string{utf8.begin(), utf8.end()} + "\"");
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

#if defined(_WIN32)
        const auto processId = static_cast<std::uint64_t>(_getpid());
#else
        const auto processId = static_cast<std::uint64_t>(getpid());
#endif
        static std::atomic<std::uint64_t> nextRootId{1U};
        return temp / (std::string{name} + "-" + std::to_string(processId) + "-" +
                       std::to_string(nextRootId.fetch_add(1U)));
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

    struct AssetGuidText {
        std::string_view value;
    };

    [[nodiscard]] constexpr AssetGuidText assetGuidText(std::string_view value) {
        return AssetGuidText{.value = value};
    }

    [[nodiscard]] asharia::asset::AssetMetadataDocument
    makeDocument(AssetGuidText guidText, std::string_view sourcePath, std::uint64_t sourceHash) {
        constexpr std::string_view kTextureTypeName = "com.asharia.asset.Texture2D";
        constexpr std::string_view kTextureImporterName = "com.asharia.importer.texture";

        auto guid = asharia::asset::parseAssetGuid(guidText.value);
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

    [[nodiscard]] asharia::asset::AssetMetadataDocument
    makeDocument(const char* guidText, std::string_view sourcePath, std::uint64_t sourceHash) {
        return makeDocument(assetGuidText(guidText), sourcePath, sourceHash);
    }

    [[nodiscard]] asharia::asset::DiscoveredSourceAsset
    makeDiscoveredSource(std::string_view guidText, std::string_view sourcePath,
                         std::uint64_t sourceHash, std::string_view compression = "auto") {
        auto document = makeDocument(assetGuidText(guidText), sourcePath, sourceHash);
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
    makeTextureProfileSmokeRecord(AssetGuidText guidText, std::string_view sourcePath) {
        auto guid = asharia::asset::parseAssetGuid(guidText.value);
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

    [[nodiscard]] asharia::asset::SourceAssetRecord
    makeTextureProfileSmokeRecord(const char* guidText, std::string_view sourcePath) {
        return makeTextureProfileSmokeRecord(assetGuidText(guidText), sourcePath);
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

    [[nodiscard]] std::vector<std::uint8_t> readFileBytes(const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary);
        std::vector<std::uint8_t> bytes;
        char byte{};
        while (file.get(byte)) {
            bytes.push_back(static_cast<std::uint8_t>(static_cast<unsigned char>(byte)));
        }
        return bytes;
    }

    [[nodiscard]] std::vector<std::uint8_t> bytesFromText(std::string_view text) {
        std::vector<std::uint8_t> bytes;
        bytes.reserve(text.size());
        for (const char character : text) {
            bytes.push_back(static_cast<std::uint8_t>(character));
        }
        return bytes;
    }

    [[nodiscard]] bool replaceFirst(std::vector<std::uint8_t>& bytes, std::string_view from,
                                    std::string_view replacement) {
        if (from.size() != replacement.size()) {
            return false;
        }

        const std::vector<std::uint8_t> fromBytes = bytesFromText(from);
        const std::vector<std::uint8_t> replacementBytes = bytesFromText(replacement);
        const auto found = std::ranges::search(bytes, fromBytes);
        if (found.empty()) {
            return false;
        }

        std::ranges::copy(replacementBytes, found.begin());
        return true;
    }

    [[nodiscard]] std::uint64_t smokeHashBytes(std::span<const std::uint8_t> bytes) noexcept {
        std::uint64_t hash = 14695981039346656037ULL;
        for (const std::uint8_t byte : bytes) {
            hash ^= byte;
            hash *= 1099511628211ULL;
        }
        return hash;
    }

    enum class PublicationFailurePoint : std::uint8_t {
        None,
        ProductStageWrite,
        ProductStageRead,
        ProductFinalPublish,
        ManifestStageWrite,
        ManifestStageRead,
        ManifestFinalPublish,
        Cleanup,
    };

    enum class StagedProductMutation : std::uint8_t {
        None,
        Size,
        Hash,
    };

    struct FakeAssetProductPublicationOperations final
        : public asharia::asset::detail::AssetProductPublicationOperations {
        explicit FakeAssetProductPublicationOperations(std::filesystem::path manifestPath)
            : manifestPath_(std::move(manifestPath)) {}

        [[nodiscard]] asharia::Result<std::filesystem::path>
        createUniqueStagingDirectory(const std::filesystem::path& outputRoot) override {
            events.emplace_back("create-staging");
            const std::filesystem::path requestedPath =
                createdStagingPathOverride.empty()
                    ? outputRoot / ".asharia-product-staging" / "fake-1"
                    : createdStagingPathOverride;
            std::error_code canonicalError;
            ownedStagingPath_ = std::filesystem::weakly_canonical(requestedPath, canonicalError);
            if (canonicalError) {
                return std::unexpected{injectedError("staging path canonicalization")};
            }
            return ownedStagingPath_;
        }

        [[nodiscard]] asharia::VoidResult
        writeFileAtomically(const std::filesystem::path& path,
                            std::span<const std::byte> bytes) override {
            const bool manifest = isManifestStagingPath(path);
            events.emplace_back(manifest ? "write-manifest-staging" : "write-product-staging");
            if (!manifest) {
                productStagingPaths.push_back(path);
            }
            if (failurePoint == (manifest ? PublicationFailurePoint::ManifestStageWrite
                                          : PublicationFailurePoint::ProductStageWrite)) {
                return std::unexpected{injectedError("staging write")};
            }
            files[path] = std::vector<std::byte>{bytes.begin(), bytes.end()};
            return {};
        }

        [[nodiscard]] asharia::Result<std::vector<std::byte>>
        readFileBytes(const std::filesystem::path& path,
                      asharia::core::FileReadLimits limits) override {
            const bool manifest = isManifestStagingPath(path);
            events.emplace_back(manifest ? "read-manifest-staging" : "read-product-staging");
            if (failurePoint == (manifest ? PublicationFailurePoint::ManifestStageRead
                                          : PublicationFailurePoint::ProductStageRead)) {
                return std::unexpected{injectedError("staging read")};
            }

            auto found = files.find(path);
            if (found == files.end()) {
                return std::unexpected{injectedError("missing staged file")};
            }
            if (limits.maxBytes == 0U || found->second.size() > limits.maxBytes) {
                return std::unexpected{injectedError("bounded staging read")};
            }
            std::vector<std::byte> bytes = found->second;
            if (!manifest && stagedProductMutation == StagedProductMutation::Size) {
                bytes.push_back(std::byte{0x5A});
            } else if (!manifest && stagedProductMutation == StagedProductMutation::Hash &&
                       !bytes.empty()) {
                bytes.front() ^= std::byte{0xFF};
            } else if (manifest && corruptStagedManifest) {
                bytes.assign(1, std::byte{'{'});
            }
            return bytes;
        }

        [[nodiscard]] asharia::Result<bool>
        // This override must preserve the symmetric two-path virtual comparison contract.
        // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
        publicationEndpointsEquivalent(const std::filesystem::path& left,
                                       const std::filesystem::path& right) override {
            (void)left;
            (void)right;
            return false;
        }

        [[nodiscard]] asharia::VoidResult
        // This override must preserve the two-path virtual contract; the endpoint names document
        // ordering at the only dynamically dispatched boundary.
        // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
        publishFileAtomically(const std::filesystem::path& stagingFilePath,
                              const std::filesystem::path& finalPath,
                              std::span<const std::byte> verifiedBytes) override {
            (void)stagingFilePath;
            const bool manifest = finalPath == manifestPath_;
            events.emplace_back(manifest ? "publish-manifest-final" : "publish-product-final");
            const std::size_t productIndex = productPublishCount;
            if (!manifest) {
                ++productPublishCount;
                publishedProductPaths.push_back(finalPath);
            }
            if (failurePoint == PublicationFailurePoint::ManifestFinalPublish && manifest) {
                return std::unexpected{injectedError("final publish")};
            }
            if (failurePoint == PublicationFailurePoint::ProductFinalPublish && !manifest &&
                productIndex == failingProductIndex) {
                return std::unexpected{injectedError("final publish")};
            }
            files[finalPath] = std::vector<std::byte>{verifiedBytes.begin(), verifiedBytes.end()};
            return {};
        }

        [[nodiscard]] asharia::VoidResult
        removeStagingDirectory(const std::filesystem::path& ownedStagingPath) override {
            events.emplace_back("cleanup-staging");
            cleanupAttempted = true;
            if (failurePoint == PublicationFailurePoint::Cleanup) {
                return std::unexpected{injectedError("cleanup")};
            }
            const std::string prefix = ownedStagingPath.generic_string() + "/";
            for (auto file = files.begin(); file != files.end();) {
                if (file->first.generic_string().starts_with(prefix)) {
                    file = files.erase(file);
                } else {
                    ++file;
                }
            }
            return {};
        }

        [[nodiscard]] bool manifestMatches(std::span<const std::byte> expected) const {
            const auto found = files.find(manifestPath_);
            return found != files.end() && std::ranges::equal(found->second, expected);
        }

        [[nodiscard]] bool hasStagedFiles() const {
            const std::string prefix = ownedStagingPath_.generic_string() + "/";
            return std::ranges::any_of(files, [&prefix](const auto& file) {
                return file.first.generic_string().starts_with(prefix);
            });
        }

        // Test fakes expose knobs and observations directly to keep failure cases compact.
        // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes)
        PublicationFailurePoint failurePoint{PublicationFailurePoint::None};
        StagedProductMutation stagedProductMutation{StagedProductMutation::None};
        bool corruptStagedManifest{};
        bool cleanupAttempted{};
        std::filesystem::path createdStagingPathOverride;
        std::size_t failingProductIndex{};
        std::size_t productPublishCount{};
        std::vector<std::string> events;
        std::vector<std::filesystem::path> productStagingPaths;
        std::vector<std::filesystem::path> publishedProductPaths;
        std::map<std::filesystem::path, std::vector<std::byte>> files;
        // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)

    private:
        [[nodiscard]] static asharia::Error injectedError(std::string_view phase) {
            return asharia::Error{asharia::ErrorDomain::Asset, 0,
                                  "injected publication " + std::string{phase} + " failure"};
        }

        [[nodiscard]] static bool isManifestStagingPath(const std::filesystem::path& path) {
            return path.filename() == "manifest.json";
        }

        std::filesystem::path manifestPath_;
        std::filesystem::path ownedStagingPath_;
    };

#if defined(_WIN32)
    class HardLinkInjectingPublicationOperations final
        : public asharia::asset::detail::AssetProductPublicationOperations {
    public:
        HardLinkInjectingPublicationOperations(std::filesystem::path triggerEndpoint,
                                               std::filesystem::path aliasEndpoint)
            : triggerEndpoint_(std::move(triggerEndpoint)),
              aliasEndpoint_(std::move(aliasEndpoint)) {}

        [[nodiscard]] asharia::Result<std::filesystem::path>
        createUniqueStagingDirectory(const std::filesystem::path& outputRoot) override {
            return native().createUniqueStagingDirectory(outputRoot);
        }

        [[nodiscard]] asharia::VoidResult
        writeFileAtomically(const std::filesystem::path& path,
                            std::span<const std::byte> bytes) override {
            return native().writeFileAtomically(path, bytes);
        }

        [[nodiscard]] asharia::Result<std::vector<std::byte>>
        readFileBytes(const std::filesystem::path& path,
                      asharia::core::FileReadLimits limits) override {
            return native().readFileBytes(path, limits);
        }

        [[nodiscard]] asharia::Result<bool>
        publicationEndpointsEquivalent(const std::filesystem::path& left,
                                       const std::filesystem::path& right) override {
            return native().publicationEndpointsEquivalent(left, right);
        }

        [[nodiscard]] asharia::VoidResult
        publishFileAtomically(const std::filesystem::path& stagingPath,
                              const std::filesystem::path& finalPath,
                              std::span<const std::byte> verifiedBytes) override {
            auto published = native().publishFileAtomically(stagingPath, finalPath, verifiedBytes);
            if (!published || injected_ || finalPath != triggerEndpoint_) {
                return published;
            }
            injected_ = true;
            if (CreateHardLinkW(aliasEndpoint_.c_str(), finalPath.c_str(), nullptr) == 0) {
                const DWORD errorCode = GetLastError();
                return std::unexpected{asharia::Error{
                    asharia::ErrorDomain::Asset, static_cast<int>(errorCode),
                    "Could not inject publication hard-link alias: " +
                        std::system_category().message(static_cast<int>(errorCode)) + "."}};
            }
            return {};
        }

        [[nodiscard]] asharia::VoidResult
        removeStagingDirectory(const std::filesystem::path& stagingPath) override {
            return native().removeStagingDirectory(stagingPath);
        }

        [[nodiscard]] bool injected() const noexcept {
            return injected_;
        }

    private:
        [[nodiscard]] static asharia::asset::detail::AssetProductPublicationOperations& native() {
            return asharia::asset::detail::assetProductPublicationOperations();
        }

        std::filesystem::path triggerEndpoint_;
        std::filesystem::path aliasEndpoint_;
        bool injected_{};
    };
#endif

    struct PublicationFixture {
        std::filesystem::path outputRoot{"PublicationRoot"};
        std::filesystem::path manifestPath{outputRoot / "product-manifest.json"};
        std::vector<std::vector<std::uint8_t>> productBytes{
            bytesFromText("first staged product bytes"),
            bytesFromText("second staged product bytes"),
        };
        std::vector<asharia::asset::detail::AssetProductPublicationItem> products{2};
        asharia::asset::AssetProductManifestDocument manifest;

        PublicationFixture() {
            const std::vector<std::string_view> guids{
                "9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c21",
                "a2af3d19-5cf9-4851-b28b-cb242a972c0e",
            };
            const std::vector<std::string_view> sourcePaths{
                "Content/Textures/Crate.png",
                "Content/Textures/Barrel.png",
            };
            const std::vector<std::string_view> relativePaths{
                "textures/crate.product",
                "textures/barrel.product",
            };
            for (std::size_t index = 0; index < products.size(); ++index) {
                auto& product = products[index];
                product.source = makeDocument(assetGuidText(guids[index]), sourcePaths[index],
                                              0x1000F00D1234CAFEULL + index)
                                     .source;
                const std::array dependencies{
                    asharia::asset::AssetDependency{
                        .owner = product.source.guid,
                        .kind = asharia::asset::AssetDependencyKind::SourceFile,
                        .path = product.source.sourcePath,
                        .hash = product.source.sourceHash,
                    },
                };
                product.product.key = asharia::asset::makeAssetProductKey(
                    product.source, asharia::asset::hashAssetDependencies(dependencies),
                    asharia::asset::makeAssetTargetProfileHash("windows-msvc-debug"));
                product.product.relativeProductPath = relativePaths[index];
                product.product.productSizeBytes =
                    static_cast<std::uint64_t>(productBytes[index].size());
                product.product.productHash = smokeHashBytes(productBytes[index]);
                product.finalPath = outputRoot / product.product.relativeProductPath;
                product.bytes = productBytes[index];
                manifest.products.push_back(product.product);
            }
        }

        [[nodiscard]] asharia::asset::detail::AssetProductPublicationRequest request() const {
            return asharia::asset::detail::AssetProductPublicationRequest{
                .outputRoot = outputRoot,
                .manifestPath = manifestPath,
                .manifest = manifest,
                .products = products,
            };
        }
    };

    [[nodiscard]] std::vector<std::byte> byteText(std::string_view text) {
        const auto bytes = std::as_bytes(std::span<const char>{text.data(), text.size()});
        return std::vector<std::byte>{bytes.begin(), bytes.end()};
    }

    [[nodiscard]] bool smokeProductPublicationCommitsManifestLast() {
        const PublicationFixture fixture;
        FakeAssetProductPublicationOperations operations{fixture.manifestPath};
        asharia::asset::detail::AssetProductPublicationResult outcome;
        const auto result =
            asharia::asset::detail::publishAssetProducts(fixture.request(), operations, outcome);
        const std::vector<std::string> expectedEvents{
            "create-staging",         "write-product-staging", "read-product-staging",
            "write-product-staging",  "read-product-staging",  "write-manifest-staging",
            "read-manifest-staging",  "publish-product-final", "publish-product-final",
            "publish-manifest-final", "cleanup-staging",
        };
        if (!result || outcome.writes.size() != 2 || !outcome.manifestWritten ||
            operations.events != expectedEvents || !operations.cleanupAttempted ||
            operations.hasStagedFiles() || operations.publishedProductPaths.size() != 2 ||
            operations.productStagingPaths.size() != 2 ||
            operations.productStagingPaths[0] == operations.productStagingPaths[1] ||
            operations.publishedProductPaths[0] != fixture.products[0].finalPath ||
            operations.publishedProductPaths[1] != fixture.products[1].finalPath) {
            logFailure("Asset product publication smoke did not commit manifest last.");
            return false;
        }
        return true;
    }

    [[nodiscard]] bool smokeProductPublicationFakeEnforcesReadLimits() {
        const PublicationFixture fixture;
        FakeAssetProductPublicationOperations operations{fixture.manifestPath};
        const std::filesystem::path path =
            fixture.outputRoot / ".asharia-product-staging" / "fake-limits" / "product.bin";
        const std::vector<std::byte> bytes = byteText("bounded fake bytes");
        if (auto written = operations.writeFileAtomically(path, bytes); !written) {
            logFailure("Asset product publication fake could not prepare bounded read bytes.");
            return false;
        }
        const auto zero = operations.readFileBytes(path, asharia::core::FileReadLimits{});
        const auto undersized = operations.readFileBytes(
            path, asharia::core::FileReadLimits{.maxBytes = bytes.size() - 1U});
        const auto exact =
            operations.readFileBytes(path, asharia::core::FileReadLimits{.maxBytes = bytes.size()});
        return !zero && !undersized && exact && *exact == bytes;
    }

    [[nodiscard]] bool smokeEmptyProductPublicationIsNoOp() {
        const PublicationFixture fixture;
        FakeAssetProductPublicationOperations operations{fixture.manifestPath};
        asharia::asset::detail::AssetProductPublicationResult outcome{
            .writes =
                {
                    asharia::asset::AssetProductWrite{
                        .source = fixture.products.front().source,
                        .product = fixture.products.front().product,
                        .productFilePath = fixture.products.front().finalPath,
                    },
                },
            .manifestWritten = true,
            .failingProductIndex = 0U,
        };
        const auto result = asharia::asset::detail::publishAssetProducts(
            asharia::asset::detail::AssetProductPublicationRequest{
                .outputRoot = "unused-empty-publication-root",
                .manifestPath = {},
                .manifest = {},
                .products = {},
            },
            operations, outcome);
        return result && outcome.writes.empty() && !outcome.manifestWritten &&
               !outcome.failingProductIndex && operations.events.empty() &&
               operations.files.empty() && !operations.cleanupAttempted;
    }

    [[nodiscard]] bool smokeProductPublicationPreservesManifestOnHandledFailures() {
        constexpr std::array failurePoints{
            PublicationFailurePoint::ProductStageWrite,
            PublicationFailurePoint::ProductStageRead,
            PublicationFailurePoint::ProductFinalPublish,
            PublicationFailurePoint::ManifestStageWrite,
            PublicationFailurePoint::ManifestStageRead,
            PublicationFailurePoint::ManifestFinalPublish,
        };
        const std::vector<std::byte> oldManifest = byteText("old manifest");
        for (const PublicationFailurePoint failurePoint : failurePoints) {
            const PublicationFixture fixture;
            FakeAssetProductPublicationOperations operations{fixture.manifestPath};
            operations.failurePoint = failurePoint;
            operations.files[fixture.manifestPath] = oldManifest;
            asharia::asset::detail::AssetProductPublicationResult outcome;
            const auto result = asharia::asset::detail::publishAssetProducts(fixture.request(),
                                                                             operations, outcome);
            const bool productFailure =
                failurePoint == PublicationFailurePoint::ProductStageWrite ||
                failurePoint == PublicationFailurePoint::ProductStageRead ||
                failurePoint == PublicationFailurePoint::ProductFinalPublish;
            const int expectedCode = static_cast<int>(
                productFailure
                    ? asharia::asset::AssetProductExecutionDiagnosticCode::ProductWriteFailed
                    : asharia::asset::AssetProductExecutionDiagnosticCode::ManifestWriteFailed);
            if (result || result.error().code != expectedCode ||
                !messageContains(result.error().message, "phase=") ||
                !messageContains(result.error().message, "stagingPath=") ||
                !messageContains(result.error().message, "finalPath=") ||
                (productFailure && !messageContains(result.error().message, "productKeyHash=")) ||
                !operations.manifestMatches(oldManifest) || !operations.cleanupAttempted ||
                operations.events.back() != "cleanup-staging" || operations.hasStagedFiles()) {
                logFailure("Asset product publication smoke violated handled failure consistency.");
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool smokeProductPublicationRejectsStagedMutation() {
        struct MutationCase {
            StagedProductMutation mutation{StagedProductMutation::None};
            std::string_view expectedToken;
        };
        constexpr std::array mutationCases{
            MutationCase{.mutation = StagedProductMutation::Size, .expectedToken = "size mismatch"},
            MutationCase{.mutation = StagedProductMutation::Hash, .expectedToken = "hash mismatch"},
        };
        for (const MutationCase& mutationCase : mutationCases) {
            const PublicationFixture fixture;
            FakeAssetProductPublicationOperations operations{fixture.manifestPath};
            operations.stagedProductMutation = mutationCase.mutation;
            asharia::asset::detail::AssetProductPublicationResult outcome;
            const auto result = asharia::asset::detail::publishAssetProducts(fixture.request(),
                                                                             operations, outcome);
            if (result ||
                result.error().code !=
                    static_cast<int>(
                        asharia::asset::AssetProductExecutionDiagnosticCode::ProductWriteFailed) ||
                !messageContains(result.error().message, mutationCase.expectedToken) ||
                !operations.cleanupAttempted || operations.hasStagedFiles()) {
                logFailure("Asset product publication smoke accepted staged product mutation.");
                return false;
            }
        }

        const PublicationFixture fixture;
        FakeAssetProductPublicationOperations operations{fixture.manifestPath};
        operations.corruptStagedManifest = true;
        asharia::asset::detail::AssetProductPublicationResult outcome;
        const auto result =
            asharia::asset::detail::publishAssetProducts(fixture.request(), operations, outcome);
        return !result &&
               result.error().code ==
                   static_cast<int>(
                       asharia::asset::AssetProductExecutionDiagnosticCode::ManifestWriteFailed) &&
               messageContains(result.error().message, "validate-manifest-staging") &&
               operations.cleanupAttempted && !operations.hasStagedFiles();
    }

    [[nodiscard]] bool smokeProductPublicationReportsCleanupAfterManifestCommit() {
        const PublicationFixture fixture;
        FakeAssetProductPublicationOperations operations{fixture.manifestPath};
        operations.failurePoint = PublicationFailurePoint::Cleanup;
        const std::vector<std::byte> oldManifest = byteText("old manifest");
        operations.files[fixture.manifestPath] = oldManifest;
        asharia::asset::detail::AssetProductPublicationResult outcome;
        const auto result =
            asharia::asset::detail::publishAssetProducts(fixture.request(), operations, outcome);
        return !result &&
               result.error().code ==
                   static_cast<int>(
                       asharia::asset::AssetProductExecutionDiagnosticCode::ManifestWriteFailed) &&
               messageContains(result.error().message, "cleanup-after-manifest-commit") &&
               messageContains(result.error().message, "manifestCommitted=true") &&
               !messageContains(result.error().message, "productKeyHash=") &&
               !messageContains(result.error().message, "productHash=") &&
               !operations.manifestMatches(oldManifest) && operations.cleanupAttempted &&
               outcome.writes.size() == 2 && outcome.manifestWritten &&
               !outcome.failingProductIndex;
    }

    [[nodiscard]] bool smokeProductPublicationPreservesPartialOutcome() {
        const PublicationFixture fixture;

        FakeAssetProductPublicationOperations productOperations{fixture.manifestPath};
        productOperations.failurePoint = PublicationFailurePoint::ProductFinalPublish;
        productOperations.failingProductIndex = 1U;
        asharia::asset::detail::AssetProductPublicationResult productOutcome;
        const auto productResult = asharia::asset::detail::publishAssetProducts(
            fixture.request(), productOperations, productOutcome);
        if (productResult || productOutcome.writes.size() != 1U || productOutcome.manifestWritten ||
            !productOutcome.failingProductIndex || *productOutcome.failingProductIndex != 1U ||
            productOutcome.writes.front().product != fixture.products.front().product ||
            !messageContains(productResult.error().message,
                             fixture.products[1].source.sourcePath) ||
            !messageContains(productResult.error().message,
                             fixture.products[1].product.relativeProductPath) ||
            !messageContains(productResult.error().message, "productKeyHash=") ||
            !messageContains(productResult.error().message, "productHash=")) {
            logFailure("Asset product publication lost a partial product outcome.");
            return false;
        }

        FakeAssetProductPublicationOperations manifestOperations{fixture.manifestPath};
        manifestOperations.failurePoint = PublicationFailurePoint::ManifestFinalPublish;
        asharia::asset::detail::AssetProductPublicationResult manifestOutcome;
        const auto manifestResult = asharia::asset::detail::publishAssetProducts(
            fixture.request(), manifestOperations, manifestOutcome);
        if (manifestResult || manifestOutcome.writes.size() != 2U ||
            manifestOutcome.manifestWritten || manifestOutcome.failingProductIndex) {
            logFailure("Asset product publication lost products before manifest failure.");
            return false;
        }

        return true;
    }

    [[nodiscard]] bool smokeProductPublicationPreflightsEndpoints() {
        const auto expectPreflightFailure = [](const PublicationFixture& fixture) {
            FakeAssetProductPublicationOperations operations{fixture.manifestPath};
            asharia::asset::detail::AssetProductPublicationResult outcome;
            const auto result = asharia::asset::detail::publishAssetProducts(fixture.request(),
                                                                             operations, outcome);
            return !result && operations.events.empty() && operations.files.empty() &&
                   outcome.writes.empty() && !outcome.manifestWritten;
        };

        PublicationFixture manifestCollision;
        manifestCollision.manifestPath = manifestCollision.products.front().finalPath;
        if (!expectPreflightFailure(manifestCollision)) {
            logFailure("Asset product publication staged a manifest/product endpoint collision.");
            return false;
        }

        PublicationFixture duplicateProducts;
        duplicateProducts.products[1].finalPath = duplicateProducts.products[0].finalPath;
        if (!expectPreflightFailure(duplicateProducts)) {
            logFailure("Asset product publication staged duplicate product endpoints.");
            return false;
        }

#if defined(_WIN32)
        PublicationFixture windowsAlias;
        windowsAlias.products[1].finalPath = windowsAlias.outputRoot / "TEXTURES" / "CRATE.PRODUCT";
        if (!expectPreflightFailure(windowsAlias)) {
            logFailure("Asset product publication staged Windows-cased endpoint aliases.");
            return false;
        }

        PublicationFixture windowsUnicodeProducts;
        windowsUnicodeProducts.products[0].finalPath = windowsUnicodeProducts.outputRoot /
                                                       "textures" /
                                                       std::filesystem::path{L"\u00C4.product"};
        windowsUnicodeProducts.products[1].finalPath = windowsUnicodeProducts.outputRoot /
                                                       "textures" /
                                                       std::filesystem::path{L"\u00E4.product"};
        if (!expectPreflightFailure(windowsUnicodeProducts)) {
            logFailure("Asset product publication staged Windows Unicode product aliases.");
            return false;
        }

        PublicationFixture windowsUnicodeManifest;
        windowsUnicodeManifest.products[0].finalPath = windowsUnicodeManifest.outputRoot /
                                                       "textures" /
                                                       std::filesystem::path{L"\u00C4.product"};
        windowsUnicodeManifest.manifestPath = windowsUnicodeManifest.outputRoot / "textures" /
                                              std::filesystem::path{L"\u00E4.product"};
        if (!expectPreflightFailure(windowsUnicodeManifest)) {
            logFailure("Asset product publication staged a Windows Unicode manifest alias.");
            return false;
        }
#endif

        PublicationFixture outsideRoot;
        outsideRoot.products[1].finalPath =
            outsideRoot.outputRoot.parent_path() / "outside.product";
        if (!expectPreflightFailure(outsideRoot)) {
            logFailure("Asset product publication staged an out-of-root product endpoint.");
            return false;
        }

        PublicationFixture reservedProduct;
        reservedProduct.products[0].finalPath =
            reservedProduct.outputRoot / ".asharia-product-staging";
        if (!expectPreflightFailure(reservedProduct)) {
            logFailure("Asset product publication accepted a product in the staging namespace.");
            return false;
        }

        PublicationFixture reservedManifest;
        reservedManifest.manifestPath = reservedManifest.outputRoot / ".asharia-product-staging" /
                                        "predicted-1" / "manifest.json";
        if (!expectPreflightFailure(reservedManifest)) {
            logFailure("Asset product publication accepted a manifest in the staging namespace.");
            return false;
        }

        PublicationFixture externalManifest;
        externalManifest.manifestPath =
            externalManifest.outputRoot.parent_path() / "external-manifest.json";
        FakeAssetProductPublicationOperations externalOperations{externalManifest.manifestPath};
        asharia::asset::detail::AssetProductPublicationResult externalOutcome;
        const auto externalResult = asharia::asset::detail::publishAssetProducts(
            externalManifest.request(), externalOperations, externalOutcome);
        if (!externalResult || !externalOutcome.manifestWritten) {
            logFailure("Asset product publication rejected the supported external manifest.");
            return false;
        }

        return true;
    }

    [[nodiscard]] asharia::asset::AssetImportPlanResult
    makeSingleProductExecutionPlan(const asharia::asset::SourceAssetRecord& source,
                                   std::span<const asharia::asset::AssetImportSetting> settings);

#if defined(_WIN32)
    [[nodiscard]] bool expectWindowsProductPreflightFailure(const PublicationFixture& fixture,
                                                            std::size_t productIndex) {
        FakeAssetProductPublicationOperations operations{fixture.manifestPath};
        asharia::asset::detail::AssetProductPublicationResult outcome;
        const auto result =
            asharia::asset::detail::publishAssetProducts(fixture.request(), operations, outcome);
        const bool rejected =
            !result && operations.events.empty() && outcome.failingProductIndex &&
            *outcome.failingProductIndex == productIndex &&
            result.error().code ==
                static_cast<int>(
                    asharia::asset::AssetProductExecutionDiagnosticCode::ProductWriteFailed) &&
            messageHasFinalPath(result.error().message, fixture.products[productIndex].finalPath);
        if (!rejected) {
            logFailure("Asset product publication accepted an ambiguous Windows product endpoint.");
        }
        return rejected;
    }

    [[nodiscard]] bool expectWindowsManifestPreflightFailure(const PublicationFixture& fixture) {
        FakeAssetProductPublicationOperations operations{fixture.manifestPath};
        asharia::asset::detail::AssetProductPublicationResult outcome;
        const auto result =
            asharia::asset::detail::publishAssetProducts(fixture.request(), operations, outcome);
        const bool rejected =
            !result && operations.events.empty() && !outcome.failingProductIndex &&
            result.error().code ==
                static_cast<int>(
                    asharia::asset::AssetProductExecutionDiagnosticCode::ManifestWriteFailed) &&
            messageHasFinalPath(result.error().message, fixture.manifestPath);
        if (!rejected) {
            logFailure(
                "Asset product publication accepted an ambiguous Windows manifest endpoint.");
        }
        return rejected;
    }
#endif

    [[nodiscard]] bool smokeWindowsPublicationEndpointSemantics() {
#if !defined(_WIN32)
        return true;
#else
        bool rejectedAmbiguousEndpoints = true;
        for (const std::wstring_view suffix : {std::wstring_view{L"."}, std::wstring_view{L" "}}) {
            PublicationFixture duplicateProducts;
            duplicateProducts.products[1].finalPath =
                duplicateProducts.products[0].finalPath.native() + std::wstring{suffix};
            rejectedAmbiguousEndpoints &=
                expectWindowsProductPreflightFailure(duplicateProducts, 1U);

            PublicationFixture manifestProductAlias;
            manifestProductAlias.manifestPath =
                manifestProductAlias.products[0].finalPath.native() + std::wstring{suffix};
            rejectedAmbiguousEndpoints &=
                expectWindowsManifestPreflightFailure(manifestProductAlias);

            PublicationFixture reservedProduct;
            reservedProduct.products[0].finalPath =
                reservedProduct.outputRoot /
                (std::wstring{L".asharia-product-staging"} + std::wstring{suffix}) / L"escape.bin";
            rejectedAmbiguousEndpoints &= expectWindowsProductPreflightFailure(reservedProduct, 0U);

            PublicationFixture ambiguousRoot;
            ambiguousRoot.outputRoot = ambiguousRoot.outputRoot.native() + std::wstring{suffix};
            for (asharia::asset::detail::AssetProductPublicationItem& item :
                 ambiguousRoot.products) {
                item.finalPath = ambiguousRoot.outputRoot / item.product.relativeProductPath;
            }
            ambiguousRoot.manifestPath = ambiguousRoot.outputRoot / "product-manifest.json";
            rejectedAmbiguousEndpoints &= expectWindowsProductPreflightFailure(ambiguousRoot, 0U);
        }

        for (const std::filesystem::path& ambiguousLeaf :
             {std::filesystem::path{L"stream.product:payload"}, std::filesystem::path{L"NUL.txt"},
              std::filesystem::path{L"COM\u00B9.bin"}, std::filesystem::path{L"LPT\u00B2.bin"}}) {
            PublicationFixture invalidProduct;
            invalidProduct.products[0].finalPath =
                invalidProduct.outputRoot / "textures" / ambiguousLeaf;
            rejectedAmbiguousEndpoints &= expectWindowsProductPreflightFailure(invalidProduct, 0U);
        }

        PublicationFixture invalidManifest;
        invalidManifest.manifestPath = invalidManifest.outputRoot / L"AUX.manifest";
        rejectedAmbiguousEndpoints &= expectWindowsManifestPreflightFailure(invalidManifest);

        PublicationFixture ambiguousOwnedPath;
        FakeAssetProductPublicationOperations ambiguousOwnedOperations{
            ambiguousOwnedPath.manifestPath};
        ambiguousOwnedOperations.createdStagingPathOverride =
            ambiguousOwnedPath.outputRoot / ".asharia-product-staging" / L"fake-1.";
        asharia::asset::detail::AssetProductPublicationResult ambiguousOwnedOutcome;
        const auto ambiguousOwnedResult = asharia::asset::detail::publishAssetProducts(
            ambiguousOwnedPath.request(), ambiguousOwnedOperations, ambiguousOwnedOutcome);
        if (ambiguousOwnedResult ||
            ambiguousOwnedOperations.events != std::vector<std::string>{"create-staging"} ||
            !ambiguousOwnedOutcome.failingProductIndex ||
            *ambiguousOwnedOutcome.failingProductIndex != 0U ||
            !messageHasFinalPath(ambiguousOwnedResult.error().message,
                                 ambiguousOwnedPath.products.front().finalPath)) {
            logFailure("Asset product publication accepted an ambiguous owned staging path.");
            rejectedAmbiguousEndpoints = false;
        }

        const std::vector<std::uint8_t> sourceBytes =
            bytesFromText("Windows publication endpoint execution bytes");
        auto document =
            makeDocument("829281e3-c888-4df7-9610-3fbe6db51557",
                         "Content/Textures/WindowsEndpoint.png", smokeHashBytes(sourceBytes));
        const asharia::asset::AssetImportPlanResult basePlan =
            makeSingleProductExecutionPlan(document.source, document.settings);
        for (const std::string_view suffix : {std::string_view{"."}, std::string_view{" "}}) {
            const std::filesystem::path executionRoot =
                std::filesystem::path{"WindowsEndpointExecution"} /
                (suffix == "." ? "Dot" : "Space");
            const std::filesystem::path productFinal =
                executionRoot / basePlan.requests.front().relativeProductPath;
            std::filesystem::path manifestAlias = productFinal;
            manifestAlias += suffix;
            const asharia::asset::AssetProductExecutionRequest manifestAliasRequest{
                .plan = basePlan,
                .existingManifest = {},
                .sourceBytes = {{.sourcePath = document.source.sourcePath, .bytes = sourceBytes}},
                .dependencyProductBytes = {},
                .productOutputRoot = executionRoot,
                .productManifestOutputPath = manifestAlias,
            };
            FakeAssetProductPublicationOperations manifestAliasOperations{manifestAlias};
            const auto manifestAliasExecution =
                asharia::asset::detail::executeAssetProductsWithPublicationOperations(
                    manifestAliasRequest, manifestAliasOperations);
            if (manifestAliasExecution.diagnostics.size() != 1U ||
                manifestAliasExecution.diagnostics.front().code !=
                    asharia::asset::AssetProductExecutionDiagnosticCode::ManifestWriteFailed ||
                !messageHasFinalPath(manifestAliasExecution.diagnostics.front().message,
                                     manifestAlias) ||
                !manifestAliasOperations.events.empty()) {
                logFailure("Asset product execution accepted a Windows-normalized manifest "
                           "alias.");
                rejectedAmbiguousEndpoints = false;
            }

            asharia::asset::AssetImportPlanResult ambiguousProductPlan = basePlan;
            ambiguousProductPlan.requests.front().relativeProductPath += suffix;
            const std::filesystem::path ambiguousProductFinal =
                executionRoot / ambiguousProductPlan.requests.front().relativeProductPath;
            const asharia::asset::AssetProductExecutionRequest ambiguousProductRequest{
                .plan = std::move(ambiguousProductPlan),
                .existingManifest = {},
                .sourceBytes = {{.sourcePath = document.source.sourcePath, .bytes = sourceBytes}},
                .dependencyProductBytes = {},
                .productOutputRoot = executionRoot,
                .productManifestOutputPath = {},
            };
            FakeAssetProductPublicationOperations ambiguousProductOperations{
                std::filesystem::path{}};
            const auto ambiguousProductExecution =
                asharia::asset::detail::executeAssetProductsWithPublicationOperations(
                    ambiguousProductRequest, ambiguousProductOperations);
            if (ambiguousProductExecution.diagnostics.size() != 1U ||
                ambiguousProductExecution.diagnostics.front().code !=
                    asharia::asset::AssetProductExecutionDiagnosticCode::ProductWriteFailed ||
                !messageHasFinalPath(ambiguousProductExecution.diagnostics.front().message,
                                     ambiguousProductFinal) ||
                !ambiguousProductOperations.events.empty()) {
                logFailure("Asset product execution accepted an ambiguous Windows product leaf.");
                rejectedAmbiguousEndpoints = false;
            }

            asharia::asset::AssetImportPlanResult reservedPlan = basePlan;
            reservedPlan.requests.front().relativeProductPath =
                ".asharia-product-staging" + std::string{suffix} + "/escape.product";
            const std::filesystem::path reservedFinal =
                executionRoot / reservedPlan.requests.front().relativeProductPath;
            const asharia::asset::AssetProductExecutionRequest reservedRequest{
                .plan = std::move(reservedPlan),
                .existingManifest = {},
                .sourceBytes = {{.sourcePath = document.source.sourcePath, .bytes = sourceBytes}},
                .dependencyProductBytes = {},
                .productOutputRoot = executionRoot,
                .productManifestOutputPath = {},
            };
            FakeAssetProductPublicationOperations reservedOperations{std::filesystem::path{}};
            const auto reservedExecution =
                asharia::asset::detail::executeAssetProductsWithPublicationOperations(
                    reservedRequest, reservedOperations);
            if (reservedExecution.diagnostics.size() != 1U ||
                reservedExecution.diagnostics.front().code !=
                    asharia::asset::AssetProductExecutionDiagnosticCode::ProductWriteFailed ||
                !messageHasFinalPath(reservedExecution.diagnostics.front().message,
                                     reservedFinal) ||
                !reservedOperations.events.empty()) {
                logFailure("Asset product execution accepted an ambiguous Windows staging "
                           "namespace.");
                rejectedAmbiguousEndpoints = false;
            }
        }

        const std::filesystem::path probeRoot =
            smokeRoot("asharia-asset-pipeline-smoke-win32-spelling-alias");
        if (probeRoot.empty() || !prepareWorkspace(probeRoot)) {
            return false;
        }
        const std::filesystem::path probeFile = probeRoot / "item.bin";
        {
            std::ofstream stream{probeFile, std::ios::binary};
            stream << "win32 alias probe";
        }
        const std::filesystem::path dottedProbe = probeFile.native() + std::wstring{L"."};
        const std::filesystem::path spacedProbe = probeFile.native() + std::wstring{L" "};
        const bool dottedAlias = GetFileAttributesW(dottedProbe.c_str()) != INVALID_FILE_ATTRIBUTES;
        const bool spacedAlias = GetFileAttributesW(spacedProbe.c_str()) != INVALID_FILE_ATTRIBUTES;
        std::error_code probeCountError;
        const auto probeEntries = static_cast<std::size_t>(
            std::distance(std::filesystem::directory_iterator{probeRoot, probeCountError},
                          std::filesystem::directory_iterator{}));
        if (!dottedAlias || !spacedAlias || probeCountError || probeEntries != 1U) {
            logFailure("Win32 spelling alias probe did not confirm trailing dot/space identity.");
            rejectedAmbiguousEndpoints = false;
        }
        std::error_code cleanupError;
        std::filesystem::remove_all(probeRoot, cleanupError);
        if (cleanupError) {
            logFailure("Win32 spelling alias probe could not clean its exclusive root.");
            rejectedAmbiguousEndpoints = false;
        }
        return rejectedAmbiguousEndpoints;
#endif
    }

#if defined(_WIN32)
    [[nodiscard]] bool writeWindowsIdentityProbe(const std::filesystem::path& path,
                                                 std::string_view bytes) {
        std::ofstream stream{path, std::ios::binary};
        stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        return stream.good();
    }

    [[nodiscard]] bool smokeWindowsShortPathIdentity(const std::filesystem::path& root) {
        std::error_code createError;
        if (!prepareWorkspace(root)) {
            return false;
        }
        std::filesystem::create_directories(root / "textures", createError);
        if (createError) {
            return false;
        }
        const std::filesystem::path longPath =
            root / "textures" / "LongPublicationEndpointIdentity.product";
        if (!writeWindowsIdentityProbe(longPath, "short-name identity probe")) {
            return false;
        }
        const DWORD shortLength = GetShortPathNameW(longPath.c_str(), nullptr, 0U);
        std::filesystem::path shortPath;
        if (shortLength != 0U) {
            std::vector<wchar_t> shortBuffer(shortLength);
            const DWORD written =
                GetShortPathNameW(longPath.c_str(), shortBuffer.data(), shortLength);
            if (written != 0U && written < shortLength) {
                shortPath = std::filesystem::path{shortBuffer.data()};
            }
        }
        if (shortPath.empty() || shortPath == longPath) {
            std::cout << "Win32 8.3 alias probe: disabled\n";
            return true;
        }

        std::cout << "Win32 8.3 alias probe: enabled\n";
        PublicationFixture shortAliasFixture;
        shortAliasFixture.outputRoot = root;
        shortAliasFixture.manifestPath.clear();
        shortAliasFixture.products[0].finalPath = longPath;
        shortAliasFixture.products[1].finalPath = shortPath;
        asharia::asset::detail::AssetProductPublicationResult shortAliasOutcome;
        const auto shortAliasResult = asharia::asset::detail::publishAssetProducts(
            shortAliasFixture.request(),
            asharia::asset::detail::assetProductPublicationOperations(), shortAliasOutcome);
        const bool rejected = !shortAliasResult && shortAliasOutcome.failingProductIndex &&
                              *shortAliasOutcome.failingProductIndex == 1U &&
                              messageHasFinalPath(shortAliasResult.error().message, shortPath);
        if (!rejected) {
            logFailure("Asset product publication accepted an enabled 8.3 endpoint alias.");
        }
        return rejected;
    }
#endif

    [[nodiscard]] bool smokeWindowsPublicationFileIdentity() {
#if !defined(_WIN32)
        return true;
#else
        bool rejectedAliases = true;
        const std::filesystem::path root =
            smokeRoot("asharia-asset-pipeline-smoke-win32-file-identity");
        const auto createProbeDirectories = [](const std::filesystem::path& path) {
            std::error_code error;
            std::filesystem::create_directories(path, error);
            return !error;
        };
        if (root.empty() || !prepareWorkspace(root) || !createProbeDirectories(root / "textures")) {
            return false;
        }

        const auto expectHardLink = [](const std::filesystem::path& alias,
                                       const std::filesystem::path& existing) {
            return CreateHardLinkW(alias.c_str(), existing.c_str(), nullptr) != 0;
        };

        const std::filesystem::path productFile = root / "textures" / "identity-a.product";
        const std::filesystem::path productAlias = root / "textures" / "identity-b.product";
        if (!writeWindowsIdentityProbe(productFile, "existing product identity") ||
            !expectHardLink(productAlias, productFile)) {
            logFailure("Asset product publication could not prepare existing hard-link aliases.");
            return false;
        }
        PublicationFixture existingProducts;
        existingProducts.outputRoot = root;
        existingProducts.manifestPath.clear();
        existingProducts.products[0].finalPath = productFile;
        existingProducts.products[1].finalPath = productAlias;
        asharia::asset::detail::AssetProductPublicationResult existingProductOutcome;
        const auto existingProductResult = asharia::asset::detail::publishAssetProducts(
            existingProducts.request(), asharia::asset::detail::assetProductPublicationOperations(),
            existingProductOutcome);
        if (existingProductResult || !existingProductOutcome.failingProductIndex ||
            *existingProductOutcome.failingProductIndex != 1U ||
            !messageHasFinalPath(existingProductResult.error().message, productAlias) ||
            !messageContains(existingProductResult.error().message, "preflight-product-alias")) {
            logFailure("Asset product publication accepted existing product hard-link aliases.");
            rejectedAliases = false;
        }

        if (!prepareWorkspace(root) || !createProbeDirectories(root / "textures")) {
            return false;
        }
        const std::filesystem::path manifestProduct = root / "textures" / "manifest-source.product";
        const std::filesystem::path manifestAlias = root / "product-manifest.json";
        if (!writeWindowsIdentityProbe(manifestProduct, "existing manifest identity") ||
            !expectHardLink(manifestAlias, manifestProduct)) {
            logFailure("Asset product publication could not prepare a manifest hard-link alias.");
            return false;
        }
        PublicationFixture existingManifest;
        existingManifest.outputRoot = root;
        existingManifest.products.resize(1U);
        existingManifest.manifest.products.resize(1U);
        existingManifest.products.front().finalPath = manifestProduct;
        existingManifest.manifestPath = manifestAlias;
        asharia::asset::detail::AssetProductPublicationResult existingManifestOutcome;
        const auto existingManifestResult = asharia::asset::detail::publishAssetProducts(
            existingManifest.request(), asharia::asset::detail::assetProductPublicationOperations(),
            existingManifestOutcome);
        if (existingManifestResult || existingManifestOutcome.failingProductIndex ||
            !messageHasFinalPath(existingManifestResult.error().message, manifestAlias) ||
            !messageContains(existingManifestResult.error().message,
                             "preflight-manifest-product-alias")) {
            logFailure("Asset product publication accepted an existing manifest hard-link alias.");
            rejectedAliases = false;
        }

        rejectedAliases &= smokeWindowsShortPathIdentity(root);

        if (!prepareWorkspace(root)) {
            return false;
        }
        PublicationFixture dynamicProducts;
        dynamicProducts.outputRoot = root;
        dynamicProducts.manifestPath = root / "product-manifest.json";
        for (asharia::asset::detail::AssetProductPublicationItem& item : dynamicProducts.products) {
            item.finalPath = root / item.product.relativeProductPath;
        }
        HardLinkInjectingPublicationOperations productInjection{
            dynamicProducts.products[0].finalPath, dynamicProducts.products[1].finalPath};
        asharia::asset::detail::AssetProductPublicationResult dynamicProductOutcome;
        const auto dynamicProductResult = asharia::asset::detail::publishAssetProducts(
            dynamicProducts.request(), productInjection, dynamicProductOutcome);
        if (dynamicProductResult || !productInjection.injected() ||
            dynamicProductOutcome.writes.size() != 1U || dynamicProductOutcome.manifestWritten ||
            !dynamicProductOutcome.failingProductIndex ||
            *dynamicProductOutcome.failingProductIndex != 1U ||
            !messageHasFinalPath(dynamicProductResult.error().message,
                                 dynamicProducts.products[1].finalPath) ||
            !messageContains(dynamicProductResult.error().message, "revalidate-product-alias")) {
            logFailure("Asset product publication missed a product alias created after preflight.");
            rejectedAliases = false;
        }

        if (!prepareWorkspace(root)) {
            return false;
        }
        PublicationFixture dynamicManifest;
        dynamicManifest.outputRoot = root;
        dynamicManifest.products.resize(1U);
        dynamicManifest.manifest.products.resize(1U);
        dynamicManifest.products.front().finalPath =
            root / dynamicManifest.products.front().product.relativeProductPath;
        dynamicManifest.manifestPath = root / "product-manifest.json";
        HardLinkInjectingPublicationOperations manifestInjection{
            dynamicManifest.products.front().finalPath, dynamicManifest.manifestPath};
        asharia::asset::detail::AssetProductPublicationResult dynamicManifestOutcome;
        const auto dynamicManifestResult = asharia::asset::detail::publishAssetProducts(
            dynamicManifest.request(), manifestInjection, dynamicManifestOutcome);
        if (dynamicManifestResult || !manifestInjection.injected() ||
            dynamicManifestOutcome.writes.size() != 1U || dynamicManifestOutcome.manifestWritten ||
            dynamicManifestOutcome.failingProductIndex ||
            !messageHasFinalPath(dynamicManifestResult.error().message,
                                 dynamicManifest.manifestPath) ||
            !messageContains(dynamicManifestResult.error().message,
                             "revalidate-manifest-product-alias")) {
            logFailure(
                "Asset product publication missed a manifest alias created after preflight.");
            rejectedAliases = false;
        }

        std::error_code cleanupError;
        std::filesystem::remove_all(root, cleanupError);
        if (cleanupError) {
            logFailure("Asset product publication could not clean the file-identity probe root.");
            rejectedAliases = false;
        }
        return rejectedAliases;
#endif
    }

    [[nodiscard]] bool smokeProductPublicationRejectsUnownedStagingPath() {
        bool preservedEndpointIdentity = true;
        const PublicationFixture fixture;
        FakeAssetProductPublicationOperations operations{fixture.manifestPath};
        operations.createdStagingPathOverride =
            fixture.outputRoot.parent_path() / "unowned-product-staging";
        asharia::asset::detail::AssetProductPublicationResult outcome;
        const auto result =
            asharia::asset::detail::publishAssetProducts(fixture.request(), operations, outcome);
        if (result || operations.events != std::vector<std::string>{"create-staging"} ||
            operations.cleanupAttempted || !operations.files.empty() || !outcome.writes.empty() ||
            outcome.manifestWritten ||
            !messageContains(result.error().message, "validate-owned-staging") ||
            !messageHasFinalPath(result.error().message, fixture.products.front().finalPath) ||
            messageHasFinalPath(result.error().message, fixture.outputRoot)) {
            logFailure("Asset product publication lost owned-staging product endpoint identity.");
            preservedEndpointIdentity = false;
        }

        FakeAssetProductPublicationOperations manifestOperations{fixture.manifestPath};
        manifestOperations.createdStagingPathOverride =
            fixture.outputRoot.parent_path() / "unowned-manifest-staging";
        asharia::asset::detail::AssetProductPublicationResult manifestOutcome;
        const auto manifestResult = asharia::asset::detail::publishAssetProducts(
            asharia::asset::detail::AssetProductPublicationRequest{
                .outputRoot = fixture.outputRoot,
                .manifestPath = fixture.manifestPath,
                .manifest = fixture.manifest,
                .products = {},
            },
            manifestOperations, manifestOutcome);
        if (manifestResult ||
            manifestResult.error().code !=
                static_cast<int>(
                    asharia::asset::AssetProductExecutionDiagnosticCode::ManifestWriteFailed) ||
            manifestOutcome.failingProductIndex ||
            messageContains(manifestResult.error().message, "productKeyHash=") ||
            messageContains(manifestResult.error().message, "productHash=") ||
            !messageHasFinalPath(manifestResult.error().message, fixture.manifestPath) ||
            messageHasFinalPath(manifestResult.error().message, fixture.outputRoot)) {
            logFailure("Asset product publication lost owned-staging manifest endpoint identity.");
            preservedEndpointIdentity = false;
        }
        return preservedEndpointIdentity;
    }

    [[nodiscard]] bool createRedirectedDirectoryLink(const std::filesystem::path& linkPath,
                                                     const std::filesystem::path& targetPath) {
#if defined(_WIN32)
        const std::wstring junctionCommand =
            L"mklink /J \"" + linkPath.native() + L"\" \"" + targetPath.native() + L"\" >nul 2>&1";
        // Paths are agent-owned temporary paths and are quoted; mklink /J is the available
        // unprivileged Windows junction creation mechanism for these native regressions.
        // NOLINTNEXTLINE(cert-env33-c)
        if (_wsystem(junctionCommand.c_str()) == 0) {
            return true;
        }
        return CreateSymbolicLinkW(linkPath.c_str(), targetPath.c_str(),
                                   SYMBOLIC_LINK_FLAG_DIRECTORY |
                                       SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE) != FALSE;
#else
        std::error_code linkError;
        std::filesystem::create_directory_symlink(targetPath, linkPath, linkError);
        return !linkError;
#endif
    }

    [[nodiscard]] bool smokeNativeProductPublicationRejectsRedirectedStagingRoot() {
        const std::filesystem::path root =
            smokeRoot("asharia-asset-pipeline-smoke-publication-staging-link");
        if (root.empty() || !prepareWorkspace(root)) {
            return false;
        }
        const std::filesystem::path outputRoot = root / "ProductCache";
        const std::filesystem::path outsideRoot = root / "Outside";
        const std::filesystem::path stagingRoot = outputRoot / ".asharia-product-staging";
        std::error_code directoryError;
        std::filesystem::create_directories(outputRoot, directoryError);
        if (directoryError) {
            return false;
        }
        std::filesystem::create_directories(outsideRoot, directoryError);
        if (directoryError || !writeTextFile(outsideRoot / "sentinel.txt", "outside sentinel")) {
            return false;
        }

        const bool linkCreated = createRedirectedDirectoryLink(stagingRoot, outsideRoot);
        if (!linkCreated) {
            // The injected operation regression still proves coordinator rejection when the host
            // policy does not permit creating a directory link.
            return true;
        }

        auto staging = asharia::asset::detail::assetProductPublicationOperations()
                           .createUniqueStagingDirectory(outputRoot);
        std::error_code sentinelError;
        const bool sentinelExists =
            std::filesystem::exists(outsideRoot / "sentinel.txt", sentinelError);
        std::error_code emptyError;
        std::size_t outsideEntries{};
        for (std::filesystem::directory_iterator entry(outsideRoot, emptyError);
             !emptyError && entry != std::filesystem::directory_iterator{};
             entry.increment(emptyError)) {
            ++outsideEntries;
        }
        std::error_code unlinkError;
        const bool linkRemoved = std::filesystem::remove(stagingRoot, unlinkError);
        return !staging && sentinelExists && !sentinelError && !emptyError &&
               outsideEntries == 1U && linkRemoved && !unlinkError;
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
                .metadataSuffix = std::string{asharia::asset::kAssetMetadataSidecarSuffix},
                .ignoredDirectoryNames = {},
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
                .metadataSuffix = std::string{asharia::asset::kAssetMetadataSidecarSuffix},
                .ignoredDirectoryNames = {},
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
                .metadataSuffix = std::string{asharia::asset::kAssetMetadataSidecarSuffix},
                .ignoredDirectoryNames = {},
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
                .metadataSuffix = std::string{asharia::asset::kAssetMetadataSidecarSuffix},
                .ignoredDirectoryNames = {},
            });
        return result.entries.empty() &&
               expectScanDiagnostic(result,
                                    asharia::asset::AssetSourceScanDiagnosticCode::InvalidRequest,
                                    "'/' separators");
    }

    [[nodiscard]] asharia::asset::AssetProductRecord
    makeProductRecord(AssetGuidText guidText, std::string_view productPath,
                      std::uint64_t sourceHash, std::string_view targetProfile) {
        constexpr std::string_view kTextureTypeName = "com.asharia.asset.Texture2D";
        constexpr std::string_view kTextureImporterName = "com.asharia.importer.texture";

        auto guid = asharia::asset::parseAssetGuid(guidText.value);
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

    [[nodiscard]] asharia::asset::AssetProductRecord
    makeProductRecord(const char* guidText, std::string_view productPath, std::uint64_t sourceHash,
                      std::string_view targetProfile) {
        return makeProductRecord(assetGuidText(guidText), productPath, sourceHash, targetProfile);
    }

    struct InvalidManifestExpectation {
        std::string_view text;
        std::string_view token;
    };

    [[nodiscard]] bool
    expectInvalidProductManifestRead(const InvalidManifestExpectation& expectation) {
        auto document = asharia::asset::readAssetProductManifestText(expectation.text);
        if (document) {
            logFailure("Asset product manifest smoke accepted invalid manifest text.");
            return false;
        }

        if (document.error().domain != asharia::ErrorDomain::Asset ||
            !messageContains(document.error().message, expectation.token)) {
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
        constexpr std::string_view kKey = R"("productKeyHash": ")";
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
            !first.requests.front().relativeProductPath.starts_with(
                "windows-msvc-debug/products/")) {
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

    [[nodiscard]] bool
    hasToolVersionDependency(std::span<const asharia::asset::AssetDependency> dependencies,
                             const asharia::asset::AssetGuid& owner, std::string_view toolName,
                             std::uint64_t versionHash) {
        return std::ranges::any_of(
            dependencies,
            [&owner, toolName, versionHash](const asharia::asset::AssetDependency& dependency) {
                return dependency.owner == owner &&
                       dependency.kind == asharia::asset::AssetDependencyKind::ToolVersion &&
                       dependency.path == toolName && dependency.hash == versionHash;
            });
    }

    [[nodiscard]] bool
    hasToolVersionDependencyNamed(std::span<const asharia::asset::AssetDependency> dependencies,
                                  const asharia::asset::AssetGuid& owner,
                                  std::string_view toolName) {
        return std::ranges::any_of(
            dependencies, [&owner, toolName](const asharia::asset::AssetDependency& dependency) {
                return dependency.owner == owner &&
                       dependency.kind == asharia::asset::AssetDependencyKind::ToolVersion &&
                       dependency.path == toolName;
            });
    }

    [[nodiscard]] bool smokeAssetToolFingerprintDeterminism() {
        const std::filesystem::path root =
            smokeRoot("asharia-asset-pipeline-smoke-tool-fingerprint");
        if (root.empty() || !prepareWorkspace(root)) {
            return false;
        }
        const std::filesystem::path firstDirectory = root / "first";
        const std::filesystem::path secondDirectory = root / "second";
        std::error_code directoryError;
        std::filesystem::create_directories(firstDirectory, directoryError);
        std::filesystem::create_directories(secondDirectory, directoryError);
        if (directoryError) {
            return false;
        }
        const std::filesystem::path firstPath = firstDirectory / "SLANGC.BIN";
        const std::filesystem::path secondPath = secondDirectory / "slangc.bin";
        if (!writeTextFile(firstPath, "deterministic tool bytes") ||
            !writeTextFile(secondPath, "deterministic tool bytes")) {
            return false;
        }
        std::error_code timestampError;
        const auto timestamp = std::filesystem::last_write_time(firstPath, timestampError);
        if (timestampError) {
            return false;
        }
        std::filesystem::last_write_time(secondPath, timestamp - std::chrono::hours{24},
                                         timestampError);
        if (timestampError) {
            return false;
        }

        const auto first = asharia::asset::fingerprintAssetTool(firstPath, "SLANGC");
        const auto second = asharia::asset::fingerprintAssetTool(secondPath, "slangc");
        if (!first || !second || *first != *second) {
            logFailure("Asset tool fingerprint depends on path, timestamp, or ASCII case.");
            return false;
        }

        if (!writeTextFile(secondPath, "deterministic tool byteS")) {
            return false;
        }
        const auto changed = asharia::asset::fingerprintAssetTool(secondPath, "slangc");
        if (!changed || changed->fileSize != first->fileSize ||
            changed->contentHash == first->contentHash ||
            changed->versionHash == first->versionHash) {
            logFailure("Asset tool fingerprint missed a one-byte content change.");
            return false;
        }

        const auto missing = asharia::asset::fingerprintAssetTool(root / "missing.bin", "slangc");
        const auto unreadable = asharia::asset::fingerprintAssetTool(firstDirectory, "slangc");
        if (missing || missing.error().domain != asharia::ErrorDomain::Asset || unreadable ||
            unreadable.error().domain != asharia::ErrorDomain::Asset) {
            logFailure("Asset tool fingerprint did not return Asset errors for unreadable inputs.");
            return false;
        }
        return true;
    }

    [[nodiscard]] bool smokeAssetToolFingerprintStreamLimits() {
        constexpr asharia::asset::detail::AssetToolFingerprintStreamLimits kLimits{
            .maxBytes = 4U,
            .bufferBytes = 4U,
        };
        std::istringstream emptyStream{std::string{}, std::ios::binary};
        const auto empty = asharia::asset::detail::fingerprintAssetToolStreamForTesting(
            emptyStream, 0U, "tool.bin", "tool", kLimits);
        std::istringstream exactStream{"abcd", std::ios::binary};
        const auto exact = asharia::asset::detail::fingerprintAssetToolStreamForTesting(
            exactStream, 4U, "TOOL.BIN", "TOOL", kLimits);
        std::istringstream grownStream{"abcde", std::ios::binary};
        const auto grown = asharia::asset::detail::fingerprintAssetToolStreamForTesting(
            grownStream, 4U, "tool.bin", "tool", kLimits);
        std::istringstream oversizedMeasuredStream{"", std::ios::binary};
        const auto oversizedMeasured = asharia::asset::detail::fingerprintAssetToolStreamForTesting(
            oversizedMeasuredStream, 5U, "tool.bin", "tool", kLimits);
        std::istringstream invalidNameStream{"a", std::ios::binary};
        const auto invalidName = asharia::asset::detail::fingerprintAssetToolStreamForTesting(
            invalidNameStream, 1U, "tool.bin", "", kLimits);
        if (!empty || empty->fileSize != 0U || !exact || exact->fileSize != 4U || grown ||
            grown.error().domain != asharia::ErrorDomain::Asset || oversizedMeasured ||
            oversizedMeasured.error().domain != asharia::ErrorDomain::Asset || invalidName ||
            invalidName.error().domain != asharia::ErrorDomain::Asset) {
            logFailure("Asset tool fingerprint stream limits lost an edge-case contract.");
            return false;
        }
        return true;
    }

    class MidReadStateBuffer final : public std::streambuf {
    public:
        MidReadStateBuffer(std::string bytes, std::ios::iostate injectedState)
            : bytes_{std::move(bytes)}, injectedState_{injectedState} {}

        void attach(std::istream& stream) noexcept {
            stream_ = &stream;
        }

    protected:
        std::streamsize xsgetn(char* destination, std::streamsize count) override {
            const auto requested = static_cast<std::size_t>(count);
            const std::size_t copied = (std::min)(requested, bytes_.size() - offset_);
            std::copy_n(std::next(bytes_.begin(), static_cast<std::ptrdiff_t>(offset_)), copied,
                        destination);
            offset_ += copied;
            ++readCount_;
            if (readCount_ == 2U && stream_ != nullptr) {
                stream_->setstate(injectedState_);
            }
            return static_cast<std::streamsize>(copied);
        }

    private:
        std::string bytes_;
        std::size_t offset_{};
        std::size_t readCount_{};
        std::ios::iostate injectedState_{};
        std::istream* stream_{};
    };

    [[nodiscard]] bool smokeAssetToolFingerprintStreamFailureContext() {
        constexpr asharia::asset::detail::AssetToolFingerprintStreamLimits kLimits{
            .maxBytes = 8U,
            .bufferBytes = 2U,
        };
        constexpr std::string_view kDisplayPath = "C:/tools/slangc.exe";
        constexpr std::string_view kLogicalToolName = "slangc";

        MidReadStateBuffer badBuffer{"abcd", std::ios::badbit};
        std::istream badStream{&badBuffer};
        badBuffer.attach(badStream);
        const auto bad = asharia::asset::detail::fingerprintAssetToolStreamForTesting(
            badStream, 4U, kDisplayPath, kLogicalToolName, kLimits);

        MidReadStateBuffer failBuffer{"abcd", std::ios::failbit};
        std::istream failStream{&failBuffer};
        failBuffer.attach(failStream);
        const auto failed = asharia::asset::detail::fingerprintAssetToolStreamForTesting(
            failStream, 4U, kDisplayPath, kLogicalToolName, kLimits);

        if (bad || failed || bad.error().domain != asharia::ErrorDomain::Asset ||
            failed.error().domain != asharia::ErrorDomain::Asset || bad.error().code == 0 ||
            failed.error().code == 0 || !messageContains(bad.error().message, kDisplayPath) ||
            !messageContains(bad.error().message, kLogicalToolName) ||
            !messageContains(bad.error().message, "Failed while reading") ||
            !messageContains(failed.error().message, kDisplayPath) ||
            !messageContains(failed.error().message, kLogicalToolName) ||
            !messageContains(failed.error().message, "stopped before end")) {
            logFailure("Asset tool fingerprint stream failures lost tool/path context.");
            return false;
        }
        return true;
    }

    struct FingerprintResolverState {
        std::map<std::string, std::size_t> calls;
        bool failsSpirvVal{};
    };

    [[nodiscard]] FingerprintResolverState& fingerprintResolverState() {
        static FingerprintResolverState state;
        return state;
    }

    [[nodiscard]] asharia::Result<asharia::asset::AssetToolFingerprint>
    changingToolFingerprint(std::string_view logicalToolName) {
        FingerprintResolverState& state = fingerprintResolverState();
        const std::size_t call = ++state.calls[std::string{logicalToolName}];
        if (state.failsSpirvVal && logicalToolName == "spirv-val") {
            return std::unexpected{asharia::Error{asharia::ErrorDomain::Asset, 91,
                                                  "cached injected fingerprint failure for " +
                                                      std::string{logicalToolName}}};
        }
        const std::uint64_t logicalBase = logicalToolName == "slangc" ? 0xA000U : 0xB000U;
        return asharia::asset::AssetToolFingerprint{
            .fileSize = 4U,
            .contentHash = logicalBase,
            .versionHash = logicalBase + call,
        };
    }

    [[nodiscard]] asharia::asset::DiscoveredSourceAsset
    makeShaderSource(std::string_view guidText, std::string_view sourcePath,
                     std::uint64_t sourceHash) {
        constexpr std::string_view kShaderTypeName = "com.asharia.asset.Shader";
        constexpr std::string_view kImporterName = "com.asharia.importer.shader-compile-reflection";
        auto source = makeDiscoveredSource(guidText, sourcePath, sourceHash);
        source.source.assetType = asharia::asset::makeAssetTypeId(kShaderTypeName);
        source.source.assetTypeName = kShaderTypeName;
        source.source.importerId = asharia::asset::makeImporterId(kImporterName);
        source.source.importerName = kImporterName;
        return source;
    }

    [[nodiscard]] bool smokeImportPlanningToolFingerprintBatchCache() {
        const auto firstSource =
            makeShaderSource("69bc6326-c04a-49d8-a4d2-653445a0e423",
                             "Content/Shaders/First.ashader", 0x1000F00D1234CAFEULL);
        const auto secondSource =
            makeShaderSource("79bc6326-c04a-49d8-a4d2-653445a0e424",
                             "Content/Shaders/Second.ashader", 0x2000F00D1234CAFEULL);
        const std::array sources{firstSource, secondSource};
        const std::array snapshots{
            makeSourceSnapshot(firstSource.source.sourcePath, firstSource.source.sourceHash),
            makeSourceSnapshot(secondSource.source.sourcePath, secondSource.source.sourceHash),
        };
        const asharia::asset::AssetImportPlanOptions options{
            .toolVersions = {},
            .toolFingerprintResolver = &changingToolFingerprint,
        };

        FingerprintResolverState& resolverState = fingerprintResolverState();
        resolverState.calls.clear();
        resolverState.failsSpirvVal = false;
        const auto firstPlan = asharia::asset::planAssetImports(
            sources, snapshots, asharia::asset::AssetProductManifestDocument{},
            "windows-msvc-debug", options);
        if (!firstPlan.succeeded() || firstPlan.requests.size() != 2U ||
            resolverState.calls["slangc"] != 1U || resolverState.calls["spirv-val"] != 1U) {
            logFailure(
                "Asset import planning did not cache successful tool fingerprints per batch.");
            return false;
        }
        for (const auto& request : firstPlan.requests) {
            if (!hasToolVersionDependency(request.dependencies, request.source.guid, "slangc",
                                          0xA001U) ||
                !hasToolVersionDependency(request.dependencies, request.source.guid, "spirv-val",
                                          0xB001U)) {
                logFailure("Asset import planning used inconsistent cached tool fingerprints.");
                return false;
            }
        }

        const auto secondPlan = asharia::asset::planAssetImports(
            sources, snapshots, asharia::asset::AssetProductManifestDocument{},
            "windows-msvc-debug", options);
        if (!secondPlan.succeeded() || secondPlan.requests.size() != 2U ||
            resolverState.calls["slangc"] != 2U || resolverState.calls["spirv-val"] != 2U ||
            !hasToolVersionDependency(secondPlan.requests.front().dependencies,
                                      secondPlan.requests.front().source.guid, "slangc", 0xA002U) ||
            !hasToolVersionDependency(secondPlan.requests.front().dependencies,
                                      secondPlan.requests.front().source.guid, "spirv-val",
                                      0xB002U)) {
            logFailure("Asset import planning leaked the tool fingerprint cache across plans.");
            return false;
        }
        return true;
    }

    [[nodiscard]] bool smokeImportPlanningToolFingerprintFailureBatchCache() {
        const auto firstSource =
            makeShaderSource("89bc6326-c04a-49d8-a4d2-653445a0e425",
                             "Content/Shaders/FailFirst.ashader", 0x3000F00D1234CAFEULL);
        const auto secondSource =
            makeShaderSource("99bc6326-c04a-49d8-a4d2-653445a0e426",
                             "Content/Shaders/FailSecond.ashader", 0x4000F00D1234CAFEULL);
        const std::array sources{firstSource, secondSource};
        const std::array snapshots{
            makeSourceSnapshot(firstSource.source.sourcePath, firstSource.source.sourceHash),
            makeSourceSnapshot(secondSource.source.sourcePath, secondSource.source.sourceHash),
        };
        const asharia::asset::AssetImportPlanOptions options{
            .toolVersions = {},
            .toolFingerprintResolver = &changingToolFingerprint,
        };

        FingerprintResolverState& resolverState = fingerprintResolverState();
        resolverState.calls.clear();
        resolverState.failsSpirvVal = true;
        const auto plan = asharia::asset::planAssetImports(
            sources, snapshots, asharia::asset::AssetProductManifestDocument{},
            "windows-msvc-debug", options);
        resolverState.failsSpirvVal = false;
        const auto failureDiagnostics = static_cast<std::size_t>(std::ranges::count_if(
            plan.diagnostics, [](const asharia::asset::AssetImportPlanDiagnostic& diagnostic) {
                return diagnostic.code ==
                           asharia::asset::AssetImportPlanDiagnosticCode::ToolFingerprintFailed &&
                       diagnostic.severity ==
                           asharia::asset::AssetImportPlanDiagnosticSeverity::Error &&
                       messageContains(diagnostic.message,
                                       "cached injected fingerprint failure for spirv-val");
            }));
        if (!plan.requests.empty() || !plan.cacheHits.empty() || failureDiagnostics != 2U ||
            resolverState.calls["slangc"] != 1U || resolverState.calls["spirv-val"] != 1U) {
            logFailure("Asset import planning did not cache failed tool fingerprints per batch.");
            return false;
        }
        return true;
    }

    [[nodiscard]] asharia::Result<asharia::asset::AssetToolFingerprint>
    failToolFingerprint(std::string_view logicalToolName) {
        return std::unexpected{
            asharia::Error{asharia::ErrorDomain::Asset, 77,
                           "injected fingerprint failure for " + std::string{logicalToolName}}};
    }

    [[nodiscard]] bool smokeImportPlanningToolFingerprintFailure() {
        constexpr std::string_view kShaderTypeName = "com.asharia.asset.Shader";
        constexpr std::string_view kImporterName = "com.asharia.importer.shader-compile-reflection";
        auto source = makeDiscoveredSource("69bc6326-c04a-49d8-a4d2-653445a0e423",
                                           "Content/Shaders/Unlit.ashader", 0x1000F00D1234CAFEULL);
        source.source.assetType = asharia::asset::makeAssetTypeId(kShaderTypeName);
        source.source.assetTypeName = kShaderTypeName;
        source.source.importerId = asharia::asset::makeImporterId(kImporterName);
        source.source.importerName = kImporterName;
        const std::array sources{source};
        const std::array snapshots{
            makeSourceSnapshot(source.source.sourcePath, source.source.sourceHash),
        };
        const asharia::asset::AssetImportPlanOptions options{
            .toolVersions = {},
            .toolFingerprintResolver = &failToolFingerprint,
        };
        const auto plan = asharia::asset::planAssetImports(
            sources, snapshots, asharia::asset::AssetProductManifestDocument{},
            "windows-msvc-debug", options);
        return plan.requests.empty() && plan.cacheHits.empty() &&
               expectPlanDiagnosticSeverity(
                   plan, asharia::asset::AssetImportPlanDiagnosticCode::ToolFingerprintFailed,
                   asharia::asset::AssetImportPlanDiagnosticSeverity::Error,
                   "injected fingerprint failure");
    }

    [[nodiscard]] bool smokeImportPlanningShaderToolVersionChanged() {
        constexpr std::string_view kShaderTypeName = "com.asharia.asset.Shader";
        constexpr std::string_view kImporterName = "com.asharia.importer.shader-compile-reflection";
        auto guid = asharia::asset::parseAssetGuid("69bc6326-c04a-49d8-a4d2-653445a0e423");
        const std::vector<asharia::asset::AssetImportSetting> settings{
            asharia::asset::AssetImportSetting{
                .key = "shader.product",
                .value = "compile-reflection-v1",
            },
        };
        const asharia::asset::DiscoveredSourceAsset source{
            .entry =
                asharia::asset::AssetSourceDiscoveryEntry{
                    .sourcePath = "Content/Shaders/Unlit.ashader",
                    .metadataPath = {},
                },
            .source =
                asharia::asset::SourceAssetRecord{
                    .guid = guid ? *guid : asharia::asset::AssetGuid{},
                    .assetType = asharia::asset::makeAssetTypeId(kShaderTypeName),
                    .assetTypeName = std::string{kShaderTypeName},
                    .sourcePath = "Content/Shaders/Unlit.ashader",
                    .importerId = asharia::asset::makeImporterId(kImporterName),
                    .importerName = std::string{kImporterName},
                    .importerVersion = asharia::asset::ImporterVersion{1},
                    .sourceHash = 0x1000F00D1234CAFEULL,
                    .settingsHash = asharia::asset::hashAssetImportSettings(settings),
                },
            .settings = settings,
        };
        const asharia::asset::AssetSourceSnapshot snapshot =
            makeSourceSnapshot(source.source.sourcePath, source.source.sourceHash);
        const std::array sources{source};
        const std::array snapshots{snapshot};
        const asharia::asset::AssetImportPlanOptions toolchainV1{
            .toolVersions =
                {
                    asharia::asset::AssetImportToolVersionDependency{
                        .importerId = source.source.importerId,
                        .toolName = "slangc",
                        .versionHash = 0x1111111111111111ULL,
                    },
                    asharia::asset::AssetImportToolVersionDependency{
                        .importerId = source.source.importerId,
                        .toolName = "spirv-val",
                        .versionHash = 0x2222222222222222ULL,
                    },
                },
        };
        const asharia::asset::AssetImportPlanOptions toolchainV2{
            .toolVersions =
                {
                    asharia::asset::AssetImportToolVersionDependency{
                        .importerId = source.source.importerId,
                        .toolName = "slangc",
                        .versionHash = 0x3333333333333333ULL,
                    },
                    asharia::asset::AssetImportToolVersionDependency{
                        .importerId = source.source.importerId,
                        .toolName = "spirv-val",
                        .versionHash = 0x2222222222222222ULL,
                    },
                },
        };

        const asharia::asset::AssetImportPlanResult first = asharia::asset::planAssetImports(
            sources, snapshots, asharia::asset::AssetProductManifestDocument{},
            "windows-msvc-debug", toolchainV1);
        if (!first.succeeded() || first.requests.size() != 1U ||
            !hasToolVersionDependency(first.requests.front().dependencies, source.source.guid,
                                      "slangc", 0x1111111111111111ULL) ||
            !hasToolVersionDependency(first.requests.front().dependencies, source.source.guid,
                                      "spirv-val", 0x2222222222222222ULL) ||
            first.requests.front().productKey.dependencyHash !=
                asharia::asset::hashAssetDependencies(first.requests.front().dependencies)) {
            logFailure("Asset import planning smoke missed shader tool version dependencies.");
            return false;
        }

        const asharia::asset::AssetImportPlanResult defaultToolchain =
            asharia::asset::planAssetImports(sources, snapshots,
                                             asharia::asset::AssetProductManifestDocument{},
                                             "windows-msvc-debug");
        if (!defaultToolchain.succeeded() || defaultToolchain.requests.size() != 1U ||
            !hasToolVersionDependencyNamed(defaultToolchain.requests.front().dependencies,
                                           source.source.guid, "slangc") ||
            !hasToolVersionDependencyNamed(defaultToolchain.requests.front().dependencies,
                                           source.source.guid, "spirv-val")) {
            logFailure("Asset import planning smoke missed default shader tool dependencies.");
            return false;
        }

        const asharia::asset::AssetProductManifestDocument manifest{
            .products = {makeProductFromImportRequest(first.requests.front())},
        };
        const asharia::asset::AssetImportPlanResult unchanged = asharia::asset::planAssetImports(
            sources, snapshots, manifest, "windows-msvc-debug", toolchainV1);
        const asharia::asset::AssetImportPlanResult changed = asharia::asset::planAssetImports(
            sources, snapshots, manifest, "windows-msvc-debug", toolchainV2);
        if (!unchanged.succeeded() || unchanged.cacheHits.size() != 1U ||
            !unchanged.requests.empty() || !changed.succeeded() || !changed.cacheHits.empty() ||
            changed.requests.size() != 1U ||
            changed.requests.front().reason !=
                asharia::asset::AssetImportRequestReason::DependencyChanged ||
            changed.requests.front().productKey == first.requests.front().productKey ||
            !hasToolVersionDependency(changed.requests.front().dependencies, source.source.guid,
                                      "slangc", 0x3333333333333333ULL)) {
            logFailure("Asset import planning smoke missed a shader tool version cache miss.");
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
        return expectInvalidProductManifestRead(
            {.text = "{", .token = "Failed to read asset product manifest"});
    }

    [[nodiscard]] bool smokeProductManifestDuplicateField() {
        const std::string duplicateSchema = R"json({
  "schema": "com.asharia.asset.product-manifest",
  "schema": "com.asharia.asset.product-manifest",
  "schemaVersion": 1,
  "products": []
}
)json";
        return expectInvalidProductManifestRead(
            {.text = duplicateSchema, .token = "duplicate key"});
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
        return expectInvalidProductManifestRead({.text = missingGuid, .token = "guid"});
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
        return expectInvalidProductManifestRead({.text = *text, .token = "unknown member"});
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
        return expectInvalidProductManifestRead(
            {.text = *text, .token = "product key hash mismatch"});
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

    struct ScannedPlanningSource {
        std::string_view relativePath;
        std::string_view bytes;
        AssetGuidText guid;
        std::uint64_t sourceHash;
    };

    [[nodiscard]] bool writeScannedPlanningSource(const std::filesystem::path& contentRoot,
                                                  const ScannedPlanningSource& source) {
        const std::filesystem::path sourceFile =
            contentRoot / std::filesystem::path{std::string{source.relativePath}};
        if (!createDirectories(sourceFile.parent_path()) ||
            !writeTextFile(sourceFile, source.bytes)) {
            return false;
        }

        const std::string sourcePath =
            "Content/" + std::filesystem::path{std::string{source.relativePath}}.generic_string();
        const asharia::asset::AssetMetadataDocument document =
            makeDocument(source.guid, sourcePath, source.sourceHash);
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
                    .ignoredDirectoryNames = {},
                },
            .productManifest = std::move(productManifest),
            .targetProfile = std::string{targetProfile},
            .toolVersions = {},
        };
    }

    [[nodiscard]] bool smokeScannedImportPlanningRequestsAndCacheHits() {
        const std::filesystem::path root =
            smokeRoot("asharia-asset-pipeline-smoke-scanned-planning");
        if (root.empty() || !prepareWorkspace(root)) {
            return false;
        }

        const std::filesystem::path contentRoot = root / "Content";
        if (!writeScannedPlanningSource(
                contentRoot, {.relativePath = "Textures/Decal.png",
                              .bytes = "decal bytes",
                              .guid = assetGuidText("785e2474-65c4-4f28-a8fb-ff8a21449a61"),
                              .sourceHash = 0x2000F00D1234CAFEULL}) ||
            !writeScannedPlanningSource(
                contentRoot, {.relativePath = "Textures/Crate.png",
                              .bytes = "crate bytes",
                              .guid = assetGuidText("9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c21"),
                              .sourceHash = 0x1000F00D1234CAFEULL})) {
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
        if (!writeScannedPlanningSource(
                contentRoot, {.relativePath = "Textures/Crate.png",
                              .bytes = "crate bytes",
                              .guid = assetGuidText("9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c21"),
                              .sourceHash = 0x1000F00D1234CAFEULL})) {
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

    template <typename Payload>
    [[nodiscard]] bool
    expectProductBlobError(const asharia::Result<Payload>& result,
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

    template <typename Operation>
    [[nodiscard]] bool expectProductBlobErrorWithoutException(
        Operation&& operation, asharia::asset::AssetProductBlobDiagnosticCode expectedCode,
        std::string_view expectedToken) {
        try {
            const auto result = std::forward<Operation>(operation)();
            return expectProductBlobError(result, expectedCode, expectedToken);
        } catch (const std::exception& exception) {
            logFailure("Asset product blob smoke escaped an exception: " +
                       std::string{exception.what()});
        } catch (...) {
            logFailure("Asset product blob smoke escaped a non-standard exception.");
        }
        return false;
    }

    [[nodiscard]] bool smokeProductExecutionWritesDeterministicProducts() {
        const std::filesystem::path root =
            smokeRoot("asharia-asset-pipeline-smoke-product-execution");
        if (root.empty() || !prepareWorkspace(root)) {
            return false;
        }

        const std::filesystem::path contentRoot = root / "Content";
        if (!writeScannedPlanningSource(
                contentRoot, {.relativePath = "Textures/Decal.png",
                              .bytes = "decal bytes",
                              .guid = assetGuidText("785e2474-65c4-4f28-a8fb-ff8a21449a61"),
                              .sourceHash = 0x2000F00D1234CAFEULL}) ||
            !writeScannedPlanningSource(
                contentRoot, {.relativePath = "Textures/Crate.png",
                              .bytes = "crate bytes",
                              .guid = assetGuidText("9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c21"),
                              .sourceHash = 0x1000F00D1234CAFEULL})) {
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
                .dependencyProductBytes = {},
                .productOutputRoot = outputRoot,
                .productManifestOutputPath = manifestPath,
            });
        const asharia::asset::AssetProductExecutionResult second =
            asharia::asset::executeAssetProducts(asharia::asset::AssetProductExecutionRequest{
                .plan = plan.plan,
                .existingManifest = {},
                .sourceBytes = sourceBytes,
                .dependencyProductBytes = {},
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
                .dependencyProductBytes = {},
                .productOutputRoot = outputRoot,
                .productManifestOutputPath = manifestPath,
            });
        const asharia::asset::AssetProductExecutionResult noManifestCacheExecution =
            asharia::asset::executeAssetProducts(asharia::asset::AssetProductExecutionRequest{
                .plan = cachePlan.plan,
                .existingManifest = first.manifest,
                .sourceBytes = sourceBytes,
                .dependencyProductBytes = {},
                .productOutputRoot = outputRoot,
                .productManifestOutputPath = {},
            });

        if (!cacheExecution.succeeded() || !cacheExecution.writtenProducts.empty() ||
            cacheExecution.cacheHits.size() != 2 || cacheExecution.manifest != first.manifest ||
            !noManifestCacheExecution.succeeded() ||
            !noManifestCacheExecution.writtenProducts.empty() ||
            noManifestCacheExecution.cacheHits.size() != 2 ||
            noManifestCacheExecution.manifest != first.manifest ||
            noManifestCacheExecution.manifestWritten ||
            !noManifestCacheExecution.diagnostics.empty()) {
            logFailure("Asset product execution smoke failed unchanged-input cache hit rerun.");
            return false;
        }

        std::cout << "Asset product execution products: " << first.writtenProducts.size() << '\n';
        return true;
    }

    [[nodiscard]] asharia::asset::SourceAssetRecord makeDependencyProductBytesSmokeRecord(
        std::span<const std::uint8_t> sourceBytes,
        std::span<const asharia::asset::AssetImportSetting> settings) {
        auto guid = asharia::asset::parseAssetGuid("8f46fdc4-99d9-48c3-8989-af7ca23690b7");
        constexpr std::string_view kAssetTypeName = "com.asharia.asset.DependencyProductBytesSmoke";
        constexpr std::string_view kImporterName =
            "com.asharia.importer.dependency-product-bytes-smoke";
        return asharia::asset::SourceAssetRecord{
            .guid = guid ? *guid : asharia::asset::AssetGuid{},
            .assetType = asharia::asset::makeAssetTypeId(kAssetTypeName),
            .assetTypeName = std::string{kAssetTypeName},
            .sourcePath = "Content/Generated/Input.asset",
            .importerId = asharia::asset::makeImporterId(kImporterName),
            .importerName = std::string{kImporterName},
            .importerVersion = asharia::asset::ImporterVersion{1},
            .sourceHash = smokeHashBytes(sourceBytes),
            .settingsHash = asharia::asset::hashAssetImportSettings(settings),
        };
    }

    [[nodiscard]] asharia::asset::AssetImportPlanResult
    makeSingleProductExecutionPlan(const asharia::asset::SourceAssetRecord& source,
                                   std::span<const asharia::asset::AssetImportSetting> settings) {
        constexpr std::string_view kTargetProfile = "windows-msvc-debug";
        const std::uint64_t targetProfileHash =
            asharia::asset::makeAssetTargetProfileHash(kTargetProfile);
        const std::array dependencies{
            asharia::asset::AssetDependency{
                .owner = source.guid,
                .kind = asharia::asset::AssetDependencyKind::SourceFile,
                .path = source.sourcePath,
                .hash = source.sourceHash,
            },
            asharia::asset::AssetDependency{
                .owner = source.guid,
                .kind = asharia::asset::AssetDependencyKind::ImportSettings,
                .path = {},
                .hash = source.settingsHash,
            },
        };
        const std::uint64_t dependencyHash = asharia::asset::hashAssetDependencies(dependencies);
        const asharia::asset::AssetProductKey productKey =
            asharia::asset::makeAssetProductKey(source, dependencyHash, targetProfileHash);
        const std::string productPath =
            asharia::asset::makeAssetImportProductPath(productKey, kTargetProfile);
        return asharia::asset::AssetImportPlanResult{
            .targetProfile = std::string{kTargetProfile},
            .targetProfileHash = targetProfileHash,
            .requests =
                {
                    asharia::asset::AssetImportRequest{
                        .source = source,
                        .settings = {settings.begin(), settings.end()},
                        .dependencies = {dependencies.begin(), dependencies.end()},
                        .productKey = productKey,
                        .relativeProductPath = productPath,
                        .reason = asharia::asset::AssetImportRequestReason::MissingProduct,
                    },
                },
            .cacheHits = {},
            .diagnostics = {},
        };
    }

    [[nodiscard]] bool smokeProductCleanupFailurePreservesDiagnosticIdentity() {
        bool preservesIdentity = true;

        const PublicationFixture fixture;
        auto publicationRequest = fixture.request();
        publicationRequest.manifestPath.clear();
        FakeAssetProductPublicationOperations publicationOperations{std::filesystem::path{}};
        publicationOperations.failurePoint = PublicationFailurePoint::Cleanup;
        asharia::asset::detail::AssetProductPublicationResult publicationOutcome;
        const auto publication = asharia::asset::detail::publishAssetProducts(
            publicationRequest, publicationOperations, publicationOutcome);
        const std::string expectedFinalPath =
            "finalPath=\"" + fixture.products.front().finalPath.generic_string() + "\"";
        if (publication ||
            publication.error().code !=
                static_cast<int>(
                    asharia::asset::AssetProductExecutionDiagnosticCode::ProductWriteFailed) ||
            publicationOutcome.writes.size() != fixture.products.size() ||
            publicationOutcome.manifestWritten || !publicationOutcome.failingProductIndex ||
            *publicationOutcome.failingProductIndex != 0U ||
            !messageContains(publication.error().message,
                             fixture.products.front().source.sourcePath) ||
            !messageContains(publication.error().message,
                             fixture.products.front().product.relativeProductPath) ||
            !messageContains(publication.error().message, "productKeyHash=") ||
            !messageContains(publication.error().message, "productHash=") ||
            !messageContains(publication.error().message,
                             "phase=cleanup-after-products-published") ||
            !messageContains(publication.error().message, "stagingPath=") ||
            !messageContains(publication.error().message, expectedFinalPath) ||
            !messageContains(publication.error().message, "injected publication cleanup failure") ||
            !publicationOperations.cleanupAttempted) {
            logFailure("Asset product publication lost product cleanup diagnostic identity.");
            preservesIdentity = false;
        }

        const std::vector<std::uint8_t> sourceBytes =
            bytesFromText("cleanup diagnostic execution bytes");
        auto document =
            makeDocument("3834a790-aec0-4db1-a1ea-ddaa14735132",
                         "Content/Textures/CleanupDiagnostic.png", smokeHashBytes(sourceBytes));
        const asharia::asset::AssetImportPlanResult plan =
            makeSingleProductExecutionPlan(document.source, document.settings);
        const std::filesystem::path outputRoot{"PublicationCleanupExecutionRoot"};
        const asharia::asset::AssetProductExecutionRequest executionRequest{
            .plan = plan,
            .existingManifest = {},
            .sourceBytes =
                {
                    asharia::asset::AssetProductSourceBytes{
                        .sourcePath = document.source.sourcePath,
                        .bytes = sourceBytes,
                    },
                },
            .dependencyProductBytes = {},
            .productOutputRoot = outputRoot,
            .productManifestOutputPath = {},
        };
        FakeAssetProductPublicationOperations executionOperations{std::filesystem::path{}};
        executionOperations.failurePoint = PublicationFailurePoint::Cleanup;
        const asharia::asset::AssetProductExecutionResult execution =
            asharia::asset::detail::executeAssetProductsWithPublicationOperations(
                executionRequest, executionOperations);
        if (execution.writtenProducts.size() != 1U || execution.manifestWritten ||
            execution.diagnostics.size() != 1U ||
            execution.diagnostics.front().code !=
                asharia::asset::AssetProductExecutionDiagnosticCode::ProductWriteFailed ||
            execution.diagnostics.front().sourcePath != document.source.sourcePath ||
            execution.diagnostics.front().relativeProductPath !=
                plan.requests.front().relativeProductPath ||
            !messageContains(execution.diagnostics.front().message, "productKeyHash=") ||
            !messageContains(execution.diagnostics.front().message, "productHash=") ||
            !messageContains(execution.diagnostics.front().message,
                             "phase=cleanup-after-products-published") ||
            !messageContains(execution.diagnostics.front().message, "stagingPath=") ||
            !messageContains(execution.diagnostics.front().message, "finalPath=\"") ||
            messageContains(execution.diagnostics.front().message, "finalPath=\"\"") ||
            !messageContains(execution.diagnostics.front().message,
                             "injected publication cleanup failure")) {
            logFailure("Asset product execution lost product cleanup diagnostic identity.");
            preservesIdentity = false;
        }

        return preservesIdentity;
    }

    [[nodiscard]] bool smokeProductExecutionScopesPublicationDiagnostics() {
        const std::vector<std::uint8_t> sourceBytes = bytesFromText("publication diagnostic bytes");
        auto document =
            makeDocument("89cb02e9-2a61-4af4-a588-063447aac784", "Content/Textures/Diagnostic.png",
                         smokeHashBytes(sourceBytes));
        const asharia::asset::AssetImportPlanResult plan =
            makeSingleProductExecutionPlan(document.source, document.settings);
        const std::filesystem::path outputRoot{"PublicationExecutionRoot"};
        const std::filesystem::path manifestPath = outputRoot / "product-manifest.json";
        const asharia::asset::AssetProductExecutionRequest request{
            .plan = plan,
            .existingManifest = {},
            .sourceBytes =
                {
                    asharia::asset::AssetProductSourceBytes{
                        .sourcePath = document.source.sourcePath,
                        .bytes = sourceBytes,
                    },
                },
            .dependencyProductBytes = {},
            .productOutputRoot = outputRoot,
            .productManifestOutputPath = manifestPath,
        };

        constexpr std::array failurePoints{
            PublicationFailurePoint::ProductStageWrite,
            PublicationFailurePoint::ProductFinalPublish,
        };
        for (const PublicationFailurePoint failurePoint : failurePoints) {
            FakeAssetProductPublicationOperations operations{manifestPath};
            operations.failurePoint = failurePoint;
            const asharia::asset::AssetProductExecutionResult execution =
                asharia::asset::detail::executeAssetProductsWithPublicationOperations(request,
                                                                                      operations);
            if (execution.diagnostics.size() != 1U ||
                execution.diagnostics.front().code !=
                    asharia::asset::AssetProductExecutionDiagnosticCode::ProductWriteFailed ||
                execution.diagnostics.front().sourcePath != document.source.sourcePath ||
                execution.diagnostics.front().relativeProductPath !=
                    plan.requests.front().relativeProductPath ||
                !messageContains(execution.diagnostics.front().message, "productKeyHash=") ||
                !messageContains(execution.diagnostics.front().message, "phase=") ||
                !messageContains(execution.diagnostics.front().message, "stagingPath=") ||
                !messageContains(execution.diagnostics.front().message, "finalPath=")) {
                logFailure("Asset product execution lost publication diagnostic identity.");
                return false;
            }
        }

        bool preservedSharedBoundaryIdentity = true;
        FakeAssetProductPublicationOperations unownedStagingOperations{manifestPath};
        unownedStagingOperations.createdStagingPathOverride =
            outputRoot.parent_path() / "unowned-execution-staging";
        const asharia::asset::AssetProductExecutionResult unownedStagingExecution =
            asharia::asset::detail::executeAssetProductsWithPublicationOperations(
                request, unownedStagingOperations);
        const std::filesystem::path expectedUnownedProductFinal =
            outputRoot / plan.requests.front().relativeProductPath;
        if (unownedStagingExecution.diagnostics.size() != 1U ||
            unownedStagingExecution.diagnostics.front().code !=
                asharia::asset::AssetProductExecutionDiagnosticCode::ProductWriteFailed ||
            unownedStagingExecution.diagnostics.front().sourcePath != document.source.sourcePath ||
            unownedStagingExecution.diagnostics.front().relativeProductPath !=
                plan.requests.front().relativeProductPath ||
            !messageContains(unownedStagingExecution.diagnostics.front().message,
                             "productKeyHash=") ||
            !messageContains(unownedStagingExecution.diagnostics.front().message, "productHash=") ||
            !messageContains(unownedStagingExecution.diagnostics.front().message,
                             "phase=validate-owned-staging") ||
            !messageHasFinalPath(unownedStagingExecution.diagnostics.front().message,
                                 expectedUnownedProductFinal) ||
            messageHasFinalPath(unownedStagingExecution.diagnostics.front().message, outputRoot)) {
            logFailure("Asset product execution lost owned-staging diagnostic identity.");
            preservedSharedBoundaryIdentity = false;
        }

        asharia::asset::AssetImportPlanResult manifestOnlyPlan = plan;
        manifestOnlyPlan.requests.clear();
        const std::filesystem::path manifestOnlyOutputRoot = outputRoot / "ManifestOnly";
        const std::filesystem::path manifestOnlyPath =
            manifestOnlyOutputRoot / "product-manifest.json";
        const asharia::asset::AssetProductExecutionRequest manifestOnlyRequest{
            .plan = std::move(manifestOnlyPlan),
            .existingManifest = {},
            .sourceBytes = {},
            .dependencyProductBytes = {},
            .productOutputRoot = manifestOnlyOutputRoot,
            .productManifestOutputPath = manifestOnlyPath,
        };
        FakeAssetProductPublicationOperations manifestOnlyOperations{manifestOnlyPath};
        manifestOnlyOperations.createdStagingPathOverride =
            manifestOnlyOutputRoot.parent_path() / "unowned-manifest-execution-staging";
        const asharia::asset::AssetProductExecutionResult manifestOnlyExecution =
            asharia::asset::detail::executeAssetProductsWithPublicationOperations(
                manifestOnlyRequest, manifestOnlyOperations);
        if (manifestOnlyExecution.diagnostics.size() != 1U ||
            manifestOnlyExecution.diagnostics.front().code !=
                asharia::asset::AssetProductExecutionDiagnosticCode::ManifestWriteFailed ||
            !manifestOnlyExecution.diagnostics.front().sourcePath.empty() ||
            !manifestOnlyExecution.diagnostics.front().relativeProductPath.empty() ||
            messageContains(manifestOnlyExecution.diagnostics.front().message, "productKeyHash=") ||
            messageContains(manifestOnlyExecution.diagnostics.front().message, "productHash=") ||
            !messageHasFinalPath(manifestOnlyExecution.diagnostics.front().message,
                                 manifestOnlyPath) ||
            messageHasFinalPath(manifestOnlyExecution.diagnostics.front().message,
                                manifestOnlyOutputRoot)) {
            logFailure("Asset product execution lost manifest-only staging endpoint identity.");
            preservedSharedBoundaryIdentity = false;
        }

        const std::filesystem::path root =
            smokeRoot("asharia-asset-pipeline-smoke-publication-preflight-diagnostic");
        if (root.empty() || !prepareWorkspace(root)) {
            return false;
        }
        const std::filesystem::path preflightOutputRoot = root / "ProductCache";
        const std::filesystem::path outsideRoot = root / "Outside";
        const std::filesystem::path stagingRoot = preflightOutputRoot / ".asharia-product-staging";
        if (!createDirectories(preflightOutputRoot) || !createDirectories(outsideRoot) ||
            !createRedirectedDirectoryLink(stagingRoot, outsideRoot)) {
            logFailure("Asset product execution could not create redirected staging fixture.");
            return false;
        }

        PublicationFixture preflightPublicationFixture;
        preflightPublicationFixture.outputRoot = preflightOutputRoot;
        preflightPublicationFixture.manifestPath =
            preflightOutputRoot / "direct-product-manifest.json";
        for (asharia::asset::detail::AssetProductPublicationItem& item :
             preflightPublicationFixture.products) {
            item.finalPath = preflightOutputRoot / item.product.relativeProductPath;
        }
        FakeAssetProductPublicationOperations directProductOperations{
            preflightPublicationFixture.manifestPath};
        asharia::asset::detail::AssetProductPublicationResult directProductOutcome;
        const auto directProductPreflight = asharia::asset::detail::publishAssetProducts(
            preflightPublicationFixture.request(), directProductOperations, directProductOutcome);
        if (directProductPreflight || !directProductOperations.events.empty() ||
            !directProductOutcome.failingProductIndex ||
            *directProductOutcome.failingProductIndex != 0U ||
            !messageHasFinalPath(directProductPreflight.error().message,
                                 preflightPublicationFixture.products.front().finalPath) ||
            messageHasFinalPath(directProductPreflight.error().message, preflightOutputRoot)) {
            logFailure("Asset product publication lost preflight product endpoint identity.");
            preservedSharedBoundaryIdentity = false;
        }

        const std::filesystem::path directManifestPath =
            preflightOutputRoot / "direct-manifest-only.json";
        FakeAssetProductPublicationOperations directManifestOperations{directManifestPath};
        asharia::asset::detail::AssetProductPublicationResult directManifestOutcome;
        const auto directManifestPreflight = asharia::asset::detail::publishAssetProducts(
            asharia::asset::detail::AssetProductPublicationRequest{
                .outputRoot = preflightOutputRoot,
                .manifestPath = directManifestPath,
                .manifest = {},
                .products = {},
            },
            directManifestOperations, directManifestOutcome);
        if (directManifestPreflight || !directManifestOperations.events.empty() ||
            directManifestPreflight.error().code !=
                static_cast<int>(
                    asharia::asset::AssetProductExecutionDiagnosticCode::ManifestWriteFailed) ||
            directManifestOutcome.failingProductIndex ||
            !messageHasFinalPath(directManifestPreflight.error().message, directManifestPath) ||
            messageHasFinalPath(directManifestPreflight.error().message, preflightOutputRoot)) {
            logFailure("Asset product publication lost preflight manifest endpoint identity.");
            preservedSharedBoundaryIdentity = false;
        }

        asharia::asset::AssetProductExecutionRequest preflightRequest = request;
        preflightRequest.productOutputRoot = preflightOutputRoot;
        preflightRequest.productManifestOutputPath = preflightOutputRoot / "product-manifest.json";
        FakeAssetProductPublicationOperations preflightOperations{
            preflightRequest.productManifestOutputPath};
        const asharia::asset::AssetProductExecutionResult preflightExecution =
            asharia::asset::detail::executeAssetProductsWithPublicationOperations(
                preflightRequest, preflightOperations);
        const std::filesystem::path expectedPreflightProductFinal =
            preflightOutputRoot / plan.requests.front().relativeProductPath;
        if (preflightExecution.diagnostics.size() != 1U ||
            preflightExecution.diagnostics.front().code !=
                asharia::asset::AssetProductExecutionDiagnosticCode::ProductWriteFailed ||
            preflightExecution.diagnostics.front().sourcePath != document.source.sourcePath ||
            preflightExecution.diagnostics.front().relativeProductPath !=
                plan.requests.front().relativeProductPath ||
            !messageContains(preflightExecution.diagnostics.front().message, "productKeyHash=") ||
            !messageContains(preflightExecution.diagnostics.front().message, "productHash=") ||
            !messageContains(preflightExecution.diagnostics.front().message,
                             "phase=preflight-staging-root") ||
            !messageHasFinalPath(preflightExecution.diagnostics.front().message,
                                 expectedPreflightProductFinal) ||
            messageHasFinalPath(preflightExecution.diagnostics.front().message,
                                preflightOutputRoot) ||
            !preflightOperations.events.empty()) {
            logFailure("Asset product execution lost preflight-staging diagnostic identity.");
            preservedSharedBoundaryIdentity = false;
        }

        asharia::asset::AssetImportPlanResult preflightManifestPlan = plan;
        preflightManifestPlan.requests.clear();
        const std::filesystem::path preflightManifestPath =
            preflightOutputRoot / "execution-manifest-only.json";
        const asharia::asset::AssetProductExecutionRequest preflightManifestRequest{
            .plan = std::move(preflightManifestPlan),
            .existingManifest = {},
            .sourceBytes = {},
            .dependencyProductBytes = {},
            .productOutputRoot = preflightOutputRoot,
            .productManifestOutputPath = preflightManifestPath,
        };
        FakeAssetProductPublicationOperations preflightManifestOperations{preflightManifestPath};
        const asharia::asset::AssetProductExecutionResult preflightManifestExecution =
            asharia::asset::detail::executeAssetProductsWithPublicationOperations(
                preflightManifestRequest, preflightManifestOperations);
        if (preflightManifestExecution.diagnostics.size() != 1U ||
            preflightManifestExecution.diagnostics.front().code !=
                asharia::asset::AssetProductExecutionDiagnosticCode::ManifestWriteFailed ||
            !preflightManifestExecution.diagnostics.front().sourcePath.empty() ||
            !preflightManifestExecution.diagnostics.front().relativeProductPath.empty() ||
            messageContains(preflightManifestExecution.diagnostics.front().message,
                            "productKeyHash=") ||
            messageContains(preflightManifestExecution.diagnostics.front().message,
                            "productHash=") ||
            !messageHasFinalPath(preflightManifestExecution.diagnostics.front().message,
                                 preflightManifestPath) ||
            messageHasFinalPath(preflightManifestExecution.diagnostics.front().message,
                                preflightOutputRoot) ||
            !preflightManifestOperations.events.empty()) {
            logFailure("Asset product execution lost preflight manifest endpoint identity.");
            preservedSharedBoundaryIdentity = false;
        }

        std::error_code unlinkError;
        const bool linkRemoved = std::filesystem::remove(stagingRoot, unlinkError);
        if (!linkRemoved || unlinkError) {
            logFailure("Asset product execution could not remove redirected staging fixture.");
            preservedSharedBoundaryIdentity = false;
        }

        return preservedSharedBoundaryIdentity;
    }

    [[nodiscard]] bool smokeProductExecutionPreservesPublicationOutcome() {
        const std::array sourcePayloads{
            bytesFromText("first publication outcome bytes"),
            bytesFromText("second publication outcome bytes"),
        };
        auto firstDocument =
            makeDocument("6de6084e-b728-465b-bfa5-12546f720a87", "Content/Textures/OutcomeA.png",
                         smokeHashBytes(sourcePayloads[0]));
        auto secondDocument =
            makeDocument("0333e2d9-7af0-4a31-848a-c8d8486cc882", "Content/Textures/OutcomeB.png",
                         smokeHashBytes(sourcePayloads[1]));
        asharia::asset::AssetImportPlanResult plan =
            makeSingleProductExecutionPlan(firstDocument.source, firstDocument.settings);
        asharia::asset::AssetImportPlanResult secondPlan =
            makeSingleProductExecutionPlan(secondDocument.source, secondDocument.settings);
        plan.requests.push_back(secondPlan.requests.front());

        const std::filesystem::path outputRoot{"PublicationExecutionOutcomeRoot"};
        const std::filesystem::path manifestPath = outputRoot / "product-manifest.json";
        const asharia::asset::AssetProductExecutionRequest request{
            .plan = plan,
            .existingManifest = {},
            .sourceBytes =
                {
                    asharia::asset::AssetProductSourceBytes{
                        .sourcePath = firstDocument.source.sourcePath,
                        .bytes = sourcePayloads[0],
                    },
                    asharia::asset::AssetProductSourceBytes{
                        .sourcePath = secondDocument.source.sourcePath,
                        .bytes = sourcePayloads[1],
                    },
                },
            .dependencyProductBytes = {},
            .productOutputRoot = outputRoot,
            .productManifestOutputPath = manifestPath,
        };

        FakeAssetProductPublicationOperations productOperations{manifestPath};
        productOperations.failurePoint = PublicationFailurePoint::ProductFinalPublish;
        productOperations.failingProductIndex = 1U;
        const asharia::asset::AssetProductExecutionResult productFailure =
            asharia::asset::detail::executeAssetProductsWithPublicationOperations(
                request, productOperations);
        if (productFailure.writtenProducts.size() != 1U || productFailure.manifestWritten ||
            productFailure.diagnostics.size() != 1U ||
            productFailure.diagnostics.front().sourcePath != secondDocument.source.sourcePath ||
            productFailure.diagnostics.front().relativeProductPath !=
                plan.requests[1].relativeProductPath) {
            logFailure("Asset product execution lost a partial publication outcome.");
            return false;
        }

        FakeAssetProductPublicationOperations manifestOperations{manifestPath};
        manifestOperations.failurePoint = PublicationFailurePoint::ManifestFinalPublish;
        const asharia::asset::AssetProductExecutionResult manifestFailure =
            asharia::asset::detail::executeAssetProductsWithPublicationOperations(
                request, manifestOperations);
        if (manifestFailure.writtenProducts.size() != 2U || manifestFailure.manifestWritten ||
            manifestFailure.diagnostics.size() != 1U ||
            !manifestFailure.diagnostics.front().sourcePath.empty() ||
            !manifestFailure.diagnostics.front().relativeProductPath.empty()) {
            logFailure("Asset product execution lost products before manifest failure.");
            return false;
        }

        FakeAssetProductPublicationOperations cleanupOperations{manifestPath};
        cleanupOperations.failurePoint = PublicationFailurePoint::Cleanup;
        const asharia::asset::AssetProductExecutionResult cleanupFailure =
            asharia::asset::detail::executeAssetProductsWithPublicationOperations(
                request, cleanupOperations);
        if (cleanupFailure.writtenProducts.size() != 2U || !cleanupFailure.manifestWritten ||
            cleanupFailure.diagnostics.size() != 1U ||
            cleanupFailure.diagnostics.front().code !=
                asharia::asset::AssetProductExecutionDiagnosticCode::ManifestWriteFailed ||
            !cleanupFailure.diagnostics.front().sourcePath.empty() ||
            !cleanupFailure.diagnostics.front().relativeProductPath.empty() ||
            !messageContains(cleanupFailure.diagnostics.front().message,
                             "manifestCommitted=true") ||
            messageContains(cleanupFailure.diagnostics.front().message, "productKeyHash=") ||
            messageContains(cleanupFailure.diagnostics.front().message, "productHash=")) {
            logFailure("Asset product execution hid a committed manifest after cleanup failure.");
            return false;
        }

        return true;
    }

    [[nodiscard]] bool smokeProductExecutionWritesPngTextureProduct() {
        const std::filesystem::path root =
            smokeRoot("asharia-asset-pipeline-smoke-png-texture-product");
        if (root.empty() || !prepareWorkspace(root)) {
            return false;
        }

        std::vector<asharia::asset::AssetImportSetting> settings =
            textureImportPngSettings("Texture 2D", "rgba8-srgb");
        std::vector<std::uint8_t> sourceBytes = validPngTextureBytes();
        asharia::asset::SourceAssetRecord source =
            makeTextureImportContractRecord("Content/Textures/Crate.png", settings,
                                            asharia::asset::makePngTextureImporterDescriptor());
        source.sourceHash = smokeHashBytes(sourceBytes);
        source.settingsHash = asharia::asset::hashAssetImportSettings(settings);

        const std::filesystem::path outputRoot = root / "ProductCache";
        const asharia::asset::AssetProductExecutionResult execution =
            asharia::asset::executeAssetProducts(asharia::asset::AssetProductExecutionRequest{
                .plan = makeSingleProductExecutionPlan(source, settings),
                .existingManifest = {},
                .sourceBytes =
                    {
                        asharia::asset::AssetProductSourceBytes{
                            .sourcePath = source.sourcePath,
                            .bytes = sourceBytes,
                        },
                    },
                .dependencyProductBytes = {},
                .productOutputRoot = outputRoot,
                .productManifestOutputPath = outputRoot / "product-manifest.json",
            });
        if (!execution.succeeded() || execution.writtenProducts.size() != 1U ||
            execution.manifest.products.size() != 1U || !execution.manifestWritten) {
            logFailure("Asset product execution smoke failed PNG texture product write.");
            return false;
        }

        const asharia::asset::AssetProductWrite& written = execution.writtenProducts.front();
        auto payload =
            asharia::asset::readTexture2DProductPayload(asharia::asset::AssetProductBlobReadRequest{
                .productFilePath = written.productFilePath,
                .relativeProductPath = written.product.relativeProductPath,
            });
        const std::vector<std::uint8_t> expectedBytes{0x10U, 0x20U, 0x30U, 0xFFU};
        if (!payload || payload->sourcePath != source.sourcePath ||
            payload->productTypeName != asharia::asset::kTextureRoleTexture2D ||
            payload->importProfileName != asharia::asset::kTextureImportProfileTexture2D ||
            payload->settingsVersion != asharia::asset::kTextureImportContractSettingsVersion ||
            payload->format != asharia::asset::AssetTextureImportFormat::Rgba8Srgb ||
            payload->width != 1U || payload->height != 1U || payload->mips.size() != 1U ||
            payload->mips[0].level != 0U || payload->mips[0].byteOffset != 0U ||
            payload->mips[0].byteSize != expectedBytes.size() ||
            payload->payload != expectedBytes) {
            logFailure("Asset product execution smoke could not read PNG texture product.");
            return false;
        }

        return true;
    }

    [[nodiscard]] bool smokeProductExecutionPngTextureDiagnostics() {
        std::vector<asharia::asset::AssetImportSetting> settings = textureImportPngSettings();
        std::vector<std::uint8_t> sourceBytes{0x89U, 0x50U, 0x4EU, 0x47U};
        asharia::asset::SourceAssetRecord source =
            makeTextureImportContractRecord("Content/Textures/Broken.png", settings,
                                            asharia::asset::makePngTextureImporterDescriptor());
        source.sourceHash = smokeHashBytes(sourceBytes);
        source.settingsHash = asharia::asset::hashAssetImportSettings(settings);

        const asharia::asset::AssetProductExecutionResult execution =
            asharia::asset::executeAssetProducts(asharia::asset::AssetProductExecutionRequest{
                .plan = makeSingleProductExecutionPlan(source, settings),
                .existingManifest = {},
                .sourceBytes =
                    {
                        asharia::asset::AssetProductSourceBytes{
                            .sourcePath = source.sourcePath,
                            .bytes = sourceBytes,
                        },
                    },
                .dependencyProductBytes = {},
                .productOutputRoot = "unused-product-cache",
                .productManifestOutputPath = {},
            });
        return execution.writtenProducts.empty() &&
               expectExecutionDiagnostic(
                   execution,
                   asharia::asset::AssetProductExecutionDiagnosticCode::TextureImportFailed,
                   "decode-failed");
    }

    [[nodiscard]] std::string validAmatText() {
        return R"json({
  "schemaVersion": 2,
  "materialType": {
    "assetGuid": "11111111-1111-1111-1111-111111111111",
    "stableTypeId": "asharia.material.unlit",
    "expectedTypeHash": "00000000000000aa"
  },
  "variant": {
    "staticSwitches": {}
  },
  "properties": {
    "baseColor": {
      "propertyId": "baseColor",
      "type": "color",
      "value": [1.0, 0.0, 0.0, 1.0]
    },
    "roughness": {
      "propertyId": "roughness",
      "type": "float",
      "value": 0.25
    }
  },
  "import": {
    "lastCookedSignatureHash": "00000000000000bb"
  }
})json";
    }

    [[nodiscard]] asharia::asset::SourceAssetRecord
    makeMaterialInstanceRecord(std::span<const std::uint8_t> sourceBytes,
                               std::span<const asharia::asset::AssetImportSetting> settings) {
        auto guid = asharia::asset::parseAssetGuid("d7f0872a-e7b8-4b58-a4c7-df44c9f0a123");
        constexpr std::string_view kMaterialTypeName = "com.asharia.asset.Material";
        constexpr std::string_view kImporterName = "com.asharia.importer.material-instance";
        return asharia::asset::SourceAssetRecord{
            .guid = guid ? *guid : asharia::asset::AssetGuid{},
            .assetType = asharia::asset::makeAssetTypeId(kMaterialTypeName),
            .assetTypeName = std::string{kMaterialTypeName},
            .sourcePath = "Content/Materials/Red.amat",
            .importerId = asharia::asset::makeImporterId(kImporterName),
            .importerName = std::string{kImporterName},
            .importerVersion = asharia::asset::ImporterVersion{1},
            .sourceHash = smokeHashBytes(sourceBytes),
            .settingsHash = asharia::asset::hashAssetImportSettings(settings),
        };
    }

    [[nodiscard]] std::string validAshaderText() {
        return R"ashader(
schema 2

shader "asharia.material.unlit" {
  properties {
    color baseColor = [1, 1, 1, 1]
    texture2D albedoMap
    sampler linearSampler
  }

  pass "Forward" {
    tag "SceneForward"
    vertex vertexMain
    fragment fragmentMain
    slang "Unlit.slang"
  }
}
)ashader";
    }

    [[nodiscard]] asharia::asset::SourceAssetRecord
    makeShaderAuthoringRecord(std::span<const std::uint8_t> sourceBytes,
                              std::span<const asharia::asset::AssetImportSetting> settings) {
        auto guid = asharia::asset::parseAssetGuid("69bc6326-c04a-49d8-a4d2-653445a0e423");
        constexpr std::string_view kShaderTypeName = "com.asharia.asset.Shader";
        constexpr std::string_view kImporterName = "com.asharia.importer.shader-authoring";
        return asharia::asset::SourceAssetRecord{
            .guid = guid ? *guid : asharia::asset::AssetGuid{},
            .assetType = asharia::asset::makeAssetTypeId(kShaderTypeName),
            .assetTypeName = std::string{kShaderTypeName},
            .sourcePath = "Content/Shaders/Unlit.ashader",
            .importerId = asharia::asset::makeImporterId(kImporterName),
            .importerName = std::string{kImporterName},
            .importerVersion = asharia::asset::ImporterVersion{1},
            .sourceHash = smokeHashBytes(sourceBytes),
            .settingsHash = asharia::asset::hashAssetImportSettings(settings),
        };
    }

    [[nodiscard]] std::string validCompileReflectionAshaderText() {
        return R"ashader(
schema 2

shader "asharia.material.compile_reflection" {
  properties {
    color baseColor = [1, 1, 1, 1]
    texture2D albedoMap
    sampler linearSampler
    float roughness = 0.5
  }

  pass "Forward" {
    tag "SceneForward"
    vertex vertexMain
    fragment fragmentMain
  }

  slang {
    struct VertexOutput {
      float4 position : SV_Position;
    };

    VertexOutput vertexMain() {
      VertexOutput output;
      output.position = float4(0.0, 0.0, 0.0, 1.0);
      return output;
    }

    float4 fragmentMain() : SV_Target {
      return Material.baseColor;
    }
  }
}
)ashader";
    }

    [[nodiscard]] std::string invalidCompileReflectionAshaderText() {
        return R"ashader(
schema 2

shader "asharia.material.compile_reflection" {
  properties {
    color baseColor = [1, 1, 1, 1]
  }

  pass "Forward" {
    tag "SceneForward"
    vertex vertexMain
    fragment fragmentMain
  }

  slang {
    struct VertexOutput {
      float4 position : SV_Position;
    };

    VertexOutput vertexMain() {
      VertexOutput output;
      output.position = float4(0.0, 0.0, 0.0, 1.0);
      return output;
    }

    float4 fragmentMain() : SV_Target {
      return definitelyMissingSymbol;
    }
  }
}
)ashader";
    }

    [[nodiscard]] asharia::asset::SourceAssetRecord makeShaderCompileReflectionRecord(
        std::span<const std::uint8_t> sourceBytes,
        std::span<const asharia::asset::AssetImportSetting> settings) {
        auto guid = asharia::asset::parseAssetGuid("69bc6326-c04a-49d8-a4d2-653445a0e423");
        constexpr std::string_view kShaderTypeName = "com.asharia.asset.Shader";
        constexpr std::string_view kImporterName = "com.asharia.importer.shader-compile-reflection";
        return asharia::asset::SourceAssetRecord{
            .guid = guid ? *guid : asharia::asset::AssetGuid{},
            .assetType = asharia::asset::makeAssetTypeId(kShaderTypeName),
            .assetTypeName = std::string{kShaderTypeName},
            .sourcePath = "Content/Shaders/Unlit.ashader",
            .importerId = asharia::asset::makeImporterId(kImporterName),
            .importerName = std::string{kImporterName},
            .importerVersion = asharia::asset::ImporterVersion{1},
            .sourceHash = smokeHashBytes(sourceBytes),
            .settingsHash = asharia::asset::hashAssetImportSettings(settings),
        };
    }

    [[nodiscard]] asharia::asset::AssetImportPlanResult
    makeShaderCompileReflectionPlan(const asharia::asset::SourceAssetRecord& source,
                                    std::span<const asharia::asset::AssetImportSetting> settings,
                                    std::string_view authoringProductPath,
                                    std::uint64_t authoringProductHash) {
        constexpr std::string_view kTargetProfile = "windows-msvc-debug";
        const std::uint64_t targetProfileHash =
            asharia::asset::makeAssetTargetProfileHash(kTargetProfile);
        const std::array dependencies{
            asharia::asset::AssetDependency{
                .owner = source.guid,
                .kind = asharia::asset::AssetDependencyKind::SourceFile,
                .path = source.sourcePath,
                .hash = source.sourceHash,
            },
            asharia::asset::AssetDependency{
                .owner = source.guid,
                .kind = asharia::asset::AssetDependencyKind::ImportSettings,
                .path = {},
                .hash = source.settingsHash,
            },
            asharia::asset::AssetDependency{
                .owner = source.guid,
                .kind = asharia::asset::AssetDependencyKind::AssetReference,
                .path = std::string{authoringProductPath},
                .hash = authoringProductHash,
            },
        };
        const std::uint64_t dependencyHash = asharia::asset::hashAssetDependencies(dependencies);
        const asharia::asset::AssetProductKey productKey =
            asharia::asset::makeAssetProductKey(source, dependencyHash, targetProfileHash);
        const std::string productPath =
            asharia::asset::makeAssetImportProductPath(productKey, kTargetProfile);
        return asharia::asset::AssetImportPlanResult{
            .targetProfile = std::string{kTargetProfile},
            .targetProfileHash = targetProfileHash,
            .requests =
                {
                    asharia::asset::AssetImportRequest{
                        .source = source,
                        .settings = {settings.begin(), settings.end()},
                        .dependencies = {dependencies.begin(), dependencies.end()},
                        .productKey = productKey,
                        .relativeProductPath = productPath,
                        .reason = asharia::asset::AssetImportRequestReason::DependencyChanged,
                    },
                },
            .cacheHits = {},
            .diagnostics = {},
        };
    }

    [[nodiscard]] bool smokeProductExecutionWritesAmatMaterialProduct() {
        const std::filesystem::path root =
            smokeRoot("asharia-asset-pipeline-smoke-amat-material-product");
        if (root.empty() || !prepareWorkspace(root)) {
            return false;
        }

        const std::vector<asharia::asset::AssetImportSetting> settings{
            asharia::asset::AssetImportSetting{
                .key = "material.product",
                .value = "material-instance-v1",
            },
        };
        const std::vector<std::uint8_t> sourceBytes = bytesFromText(validAmatText());
        const asharia::asset::SourceAssetRecord source =
            makeMaterialInstanceRecord(sourceBytes, settings);
        const std::filesystem::path outputRoot = root / "ProductCache";
        const asharia::asset::AssetProductExecutionRequest request{
            .plan = makeSingleProductExecutionPlan(source, settings),
            .existingManifest = {},
            .sourceBytes =
                {
                    asharia::asset::AssetProductSourceBytes{
                        .sourcePath = source.sourcePath,
                        .bytes = sourceBytes,
                    },
                },
            .dependencyProductBytes = {},
            .productOutputRoot = outputRoot,
            .productManifestOutputPath = outputRoot / "product-manifest.json",
        };

        const asharia::asset::AssetProductExecutionResult first =
            asharia::asset::executeAssetProducts(request);
        const asharia::asset::AssetProductExecutionResult second =
            asharia::asset::executeAssetProducts(request);
        auto firstText = asharia::asset::writeAssetProductManifestText(first.manifest);
        auto secondText = asharia::asset::writeAssetProductManifestText(second.manifest);
        if (!first.succeeded() || !second.succeeded() || first.writtenProducts.size() != 1U ||
            second.writtenProducts.size() != 1U || first.manifest.products.size() != 1U ||
            !first.manifestWritten || !second.manifestWritten || !firstText || !secondText ||
            *firstText != *secondText ||
            first.writtenProducts.front().product.productHash !=
                second.writtenProducts.front().product.productHash) {
            logFailure("Asset product execution smoke failed .amat material product write.");
            return false;
        }

        const asharia::asset::AssetProductWrite& written = first.writtenProducts.front();
        auto payload = asharia::asset::readMaterialInstanceProductPayload(
            asharia::asset::AssetProductBlobReadRequest{
                .productFilePath = written.productFilePath,
                .relativeProductPath = written.product.relativeProductPath,
            });
        if (!payload || payload->sourcePath != source.sourcePath ||
            payload->stableTypeId != "asharia.material.unlit" ||
            payload->expectedTypeHash != 0x00000000000000AAULL ||
            payload->lastCookedSignatureHash != 0x00000000000000BBULL ||
            payload->canonicalAmatText.find(R"("baseColor")") == std::string::npos) {
            logFailure("Asset product execution smoke could not read .amat material product.");
            return false;
        }

        return true;
    }

    [[nodiscard]] bool smokeProductExecutionAmatDiagnostics() {
        const std::vector<asharia::asset::AssetImportSetting> settings{
            asharia::asset::AssetImportSetting{
                .key = "material.product",
                .value = "material-instance-v1",
            },
        };
        const std::vector<std::uint8_t> sourceBytes = bytesFromText("{");
        const asharia::asset::SourceAssetRecord source =
            makeMaterialInstanceRecord(sourceBytes, settings);
        const asharia::asset::AssetProductExecutionResult execution =
            asharia::asset::executeAssetProducts(asharia::asset::AssetProductExecutionRequest{
                .plan = makeSingleProductExecutionPlan(source, settings),
                .existingManifest = {},
                .sourceBytes =
                    {
                        asharia::asset::AssetProductSourceBytes{
                            .sourcePath = source.sourcePath,
                            .bytes = sourceBytes,
                        },
                    },
                .dependencyProductBytes = {},
                .productOutputRoot = "unused-product-cache",
                .productManifestOutputPath = {},
            });

        return execution.writtenProducts.empty() &&
               expectExecutionDiagnostic(execution,
                                         asharia::asset::AssetProductExecutionDiagnosticCode::
                                             MaterialInstanceImportFailed,
                                         "material instance import failed");
    }

    [[nodiscard]] bool smokeProductExecutionWritesAshaderShaderProduct() {
        const std::filesystem::path root =
            smokeRoot("asharia-asset-pipeline-smoke-ashader-shader-product");
        if (root.empty() || !prepareWorkspace(root)) {
            return false;
        }

        const std::vector<asharia::asset::AssetImportSetting> settings{
            asharia::asset::AssetImportSetting{
                .key = "shader.product",
                .value = "generated-slang-v1",
            },
        };
        const std::vector<std::uint8_t> sourceBytes = bytesFromText(validAshaderText());
        const asharia::asset::SourceAssetRecord source =
            makeShaderAuthoringRecord(sourceBytes, settings);
        const std::filesystem::path outputRoot = root / "ProductCache";
        const asharia::asset::AssetProductExecutionRequest request{
            .plan = makeSingleProductExecutionPlan(source, settings),
            .existingManifest = {},
            .sourceBytes =
                {
                    asharia::asset::AssetProductSourceBytes{
                        .sourcePath = source.sourcePath,
                        .bytes = sourceBytes,
                    },
                },
            .dependencyProductBytes = {},
            .productOutputRoot = outputRoot,
            .productManifestOutputPath = outputRoot / "product-manifest.json",
        };

        const asharia::asset::AssetProductExecutionResult first =
            asharia::asset::executeAssetProducts(request);
        const asharia::asset::AssetProductExecutionResult second =
            asharia::asset::executeAssetProducts(request);
        auto firstText = asharia::asset::writeAssetProductManifestText(first.manifest);
        auto secondText = asharia::asset::writeAssetProductManifestText(second.manifest);
        if (!first.succeeded() || !second.succeeded() || first.writtenProducts.size() != 1U ||
            second.writtenProducts.size() != 1U || first.manifest.products.size() != 1U ||
            !first.manifestWritten || !second.manifestWritten || !firstText || !secondText ||
            *firstText != *secondText ||
            first.writtenProducts.front().product.productHash !=
                second.writtenProducts.front().product.productHash) {
            logFailure("Asset product execution smoke failed .ashader shader product write.");
            return false;
        }

        const asharia::asset::AssetProductWrite& written = first.writtenProducts.front();
        auto payload = asharia::asset::readShaderAuthoringProductPayload(
            asharia::asset::AssetProductBlobReadRequest{
                .productFilePath = written.productFilePath,
                .relativeProductPath = written.product.relativeProductPath,
            });
        if (!payload || payload->sourcePath != source.sourcePath ||
            payload->stableTypeId != "asharia.material.unlit" || payload->schemaVersion != 2U ||
            payload->properties.size() != 3U || payload->passes.size() != 1U ||
            payload->bindings.size() != 3U || payload->entries.size() != 2U ||
            payload->properties.front().name != "baseColor" ||
            payload->properties.front().typeName != "color" ||
            payload->passes.front().name != "Forward" ||
            payload->passes.front().tag != "SceneForward" ||
            payload->bindings.front().name != "baseColor" ||
            !payload->bindings.front().inMaterialParameterBlock ||
            payload->entries.front().passName != "Forward" ||
            payload->generatedSlangText.find("struct __AshariaMaterialParams") ==
                std::string::npos ||
            payload->generatedSlangText.find("Texture2D<float4> albedoMap;") == std::string::npos ||
            payload->generatedSlangText.find("fragmentMain();") == std::string::npos) {
            logFailure("Asset product execution smoke could not read .ashader shader product.");
            return false;
        }

        const std::vector<std::uint8_t> generatedBytes = bytesFromText(payload->generatedSlangText);
        if (smokeHashBytes(generatedBytes) != payload->generatedSlangHash) {
            logFailure("Asset product execution smoke found generated Slang hash drift.");
            return false;
        }

        return true;
    }

    [[nodiscard]] bool smokeProductExecutionAshaderDiagnostics() {
        const std::vector<asharia::asset::AssetImportSetting> settings{
            asharia::asset::AssetImportSetting{
                .key = "shader.product",
                .value = "generated-slang-v1",
            },
        };
        const std::vector<std::uint8_t> sourceBytes = bytesFromText("{");
        const asharia::asset::SourceAssetRecord source =
            makeShaderAuthoringRecord(sourceBytes, settings);
        const asharia::asset::AssetProductExecutionResult execution =
            asharia::asset::executeAssetProducts(asharia::asset::AssetProductExecutionRequest{
                .plan = makeSingleProductExecutionPlan(source, settings),
                .existingManifest = {},
                .sourceBytes =
                    {
                        asharia::asset::AssetProductSourceBytes{
                            .sourcePath = source.sourcePath,
                            .bytes = sourceBytes,
                        },
                    },
                .dependencyProductBytes = {},
                .productOutputRoot = "unused-product-cache",
                .productManifestOutputPath = {},
            });

        return execution.writtenProducts.empty() &&
               expectExecutionDiagnostic(
                   execution,
                   asharia::asset::AssetProductExecutionDiagnosticCode::ShaderAuthoringImportFailed,
                   "shader authoring import failed");
    }

    [[nodiscard]] bool smokeProductExecutionWritesShaderCompileReflectionProduct() {
        const std::filesystem::path root =
            smokeRoot("asharia-asset-pipeline-smoke-shader-compile-reflection-product");
        if (root.empty() || !prepareWorkspace(root)) {
            return false;
        }

        const std::vector<std::uint8_t> sourceBytes =
            bytesFromText(validCompileReflectionAshaderText());
        const std::vector<asharia::asset::AssetImportSetting> authoringSettings{
            asharia::asset::AssetImportSetting{
                .key = "shader.product",
                .value = "generated-slang-v1",
            },
        };
        const asharia::asset::SourceAssetRecord authoringSource =
            makeShaderAuthoringRecord(sourceBytes, authoringSettings);
        const std::filesystem::path outputRoot = root / "ProductCache";
        const asharia::asset::AssetProductExecutionResult authoringExecution =
            asharia::asset::executeAssetProducts(asharia::asset::AssetProductExecutionRequest{
                .plan = makeSingleProductExecutionPlan(authoringSource, authoringSettings),
                .existingManifest = {},
                .sourceBytes =
                    {
                        asharia::asset::AssetProductSourceBytes{
                            .sourcePath = authoringSource.sourcePath,
                            .bytes = sourceBytes,
                        },
                    },
                .dependencyProductBytes = {},
                .productOutputRoot = outputRoot,
                .productManifestOutputPath = outputRoot / "authoring-product-manifest.json",
            });
        if (!authoringExecution.succeeded() || authoringExecution.writtenProducts.size() != 1U) {
            logFailure("Asset product execution smoke could not write authoring dependency.");
            return false;
        }

        const asharia::asset::AssetProductWrite& authoringWrite =
            authoringExecution.writtenProducts.front();
        const std::vector<std::uint8_t> authoringProductBytes =
            readFileBytes(authoringWrite.productFilePath);
        if (authoringProductBytes.empty() ||
            smokeHashBytes(authoringProductBytes) != authoringWrite.product.productHash) {
            logFailure("Asset product execution smoke could not read authoring dependency bytes.");
            return false;
        }

        const std::vector<asharia::asset::AssetImportSetting> compileSettings{
            asharia::asset::AssetImportSetting{
                .key = "shader.product",
                .value = "compiled-reflection-v1",
            },
            asharia::asset::AssetImportSetting{
                .key = "shader.authoringProductPath",
                .value = authoringWrite.product.relativeProductPath,
            },
        };
        const asharia::asset::SourceAssetRecord compileSource =
            makeShaderCompileReflectionRecord(sourceBytes, compileSettings);
        const asharia::asset::AssetProductExecutionRequest compileRequest{
            .plan = makeShaderCompileReflectionPlan(compileSource, compileSettings,
                                                    authoringWrite.product.relativeProductPath,
                                                    authoringWrite.product.productHash),
            .existingManifest = authoringExecution.manifest,
            .sourceBytes =
                {
                    asharia::asset::AssetProductSourceBytes{
                        .sourcePath = compileSource.sourcePath,
                        .bytes = sourceBytes,
                    },
                },
            .dependencyProductBytes =
                {
                    asharia::asset::AssetProductDependencyBytes{
                        .relativeProductPath = authoringWrite.product.relativeProductPath,
                        .productHash = authoringWrite.product.productHash,
                        .bytes = authoringProductBytes,
                    },
                },
            .productOutputRoot = outputRoot,
            .productManifestOutputPath = outputRoot / "compile-product-manifest.json",
        };
        const asharia::asset::AssetProductExecutionResult compileExecution =
            asharia::asset::executeAssetProducts(compileRequest);
        if (!compileExecution.succeeded() || compileExecution.writtenProducts.size() != 1U ||
            compileExecution.manifest.products.size() != 2U || !compileExecution.manifestWritten) {
            for (const asharia::asset::AssetProductExecutionDiagnostic& diagnostic :
                 compileExecution.diagnostics) {
                logFailure(diagnostic.message);
            }
            logFailure("Asset product execution smoke failed shader compile/reflection write.");
            return false;
        }

        const asharia::asset::AssetProductWrite& compiledWrite =
            compileExecution.writtenProducts.front();
        auto payload = asharia::asset::readShaderCompileReflectionProductPayload(
            asharia::asset::AssetProductBlobReadRequest{
                .productFilePath = compiledWrite.productFilePath,
                .relativeProductPath = compiledWrite.product.relativeProductPath,
            });
        const asharia::asset::AssetProductExecutionResult secondCompileExecution =
            asharia::asset::executeAssetProducts(compileRequest);
        auto firstCompileManifestText =
            asharia::asset::writeAssetProductManifestText(compileExecution.manifest);
        auto secondCompileManifestText =
            asharia::asset::writeAssetProductManifestText(secondCompileExecution.manifest);
        if (!secondCompileExecution.succeeded() ||
            secondCompileExecution.writtenProducts.size() != 1U ||
            secondCompileExecution.manifest.products.size() != 2U ||
            !secondCompileExecution.manifestWritten || !firstCompileManifestText ||
            !secondCompileManifestText || *firstCompileManifestText != *secondCompileManifestText ||
            compiledWrite.product.productHash !=
                secondCompileExecution.writtenProducts.front().product.productHash) {
            logFailure(
                "Asset product execution smoke found nondeterministic compile/reflection output.");
            return false;
        }
        auto secondPayload = asharia::asset::readShaderCompileReflectionProductPayload(
            asharia::asset::AssetProductBlobReadRequest{
                .productFilePath = secondCompileExecution.writtenProducts.front().productFilePath,
                .relativeProductPath =
                    secondCompileExecution.writtenProducts.front().product.relativeProductPath,
            });
        if (!payload || payload->sourcePath != compileSource.sourcePath ||
            payload->stableTypeId != "asharia.material.compile_reflection" ||
            payload->authoringProductPath != authoringWrite.product.relativeProductPath ||
            payload->authoringProductHash != authoringWrite.product.productHash ||
            payload->productKeyHash != asharia::asset::hashAssetProductKey(
                                           compileRequest.plan.requests.front().productKey) ||
            payload->profile != "glsl_450" || payload->target != "spirv" ||
            payload->entries.size() != 2U) {
            logFailure("Asset product execution smoke could not read compile/reflection product.");
            return false;
        }
        if (!secondPayload || *payload != *secondPayload) {
            logFailure(
                "Asset product execution smoke found nondeterministic compile/reflection payload.");
            return false;
        }

        const auto vertexEntry = std::ranges::find_if(
            payload->entries,
            [](const asharia::asset::AssetShaderCompileReflectionProductEntry& entry) {
                return entry.stage == "vertex" && entry.compileEntryName == "vertexMain";
            });
        const auto fragmentEntry = std::ranges::find_if(
            payload->entries,
            [](const asharia::asset::AssetShaderCompileReflectionProductEntry& entry) {
                return entry.stage == "fragment" && entry.compileEntryName == "fragmentMain";
            });
        if (vertexEntry == payload->entries.end() || fragmentEntry == payload->entries.end()) {
            logFailure("Asset product execution smoke missed compiled shader entries.");
            return false;
        }

        const bool entryPayloadsValid = std::ranges::all_of(
            payload->entries,
            [](const asharia::asset::AssetShaderCompileReflectionProductEntry& entry) {
                const std::vector<std::uint8_t> slangcDiagnosticBytes =
                    bytesFromText(entry.slangcDiagnosticText);
                const std::vector<std::uint8_t> spirvValDiagnosticBytes =
                    bytesFromText(entry.spirvValDiagnosticText);
                const std::vector<std::uint8_t> reflectionBytes =
                    bytesFromText(entry.reflectionJsonText);
                return entry.slangcExitCode == 0U && entry.spirvValExitCode == 0U &&
                       smokeHashBytes(slangcDiagnosticBytes) == entry.slangcDiagnosticHash &&
                       smokeHashBytes(spirvValDiagnosticBytes) == entry.spirvValDiagnosticHash &&
                       !entry.spirvBytes.empty() && (entry.spirvBytes.size() % 4U) == 0U &&
                       !entry.reflectionJsonText.empty() &&
                       smokeHashBytes(entry.spirvBytes) == entry.spirvHash &&
                       smokeHashBytes(reflectionBytes) == entry.reflectionJsonHash;
            });
        if (!entryPayloadsValid) {
            logFailure("Asset product execution smoke found compile/reflection hash drift.");
            return false;
        }

        return true;
    }

    [[nodiscard]] bool smokeProductExecutionShaderCompileReflectionDiagnostics() {
        const std::vector<std::uint8_t> sourceBytes =
            bytesFromText(validCompileReflectionAshaderText());
        const std::vector<asharia::asset::AssetImportSetting> compileSettings{
            asharia::asset::AssetImportSetting{
                .key = "shader.product",
                .value = "compiled-reflection-v1",
            },
            asharia::asset::AssetImportSetting{
                .key = "shader.authoringProductPath",
                .value = "generated/missing-shader-authoring.product",
            },
        };
        const asharia::asset::SourceAssetRecord compileSource =
            makeShaderCompileReflectionRecord(sourceBytes, compileSettings);
        const asharia::asset::AssetProductExecutionResult execution =
            asharia::asset::executeAssetProducts(asharia::asset::AssetProductExecutionRequest{
                .plan = makeShaderCompileReflectionPlan(
                    compileSource, compileSettings, "generated/missing-shader-authoring.product",
                    0x1234ULL),
                .existingManifest = {},
                .sourceBytes =
                    {
                        asharia::asset::AssetProductSourceBytes{
                            .sourcePath = compileSource.sourcePath,
                            .bytes = sourceBytes,
                        },
                    },
                .dependencyProductBytes = {},
                .productOutputRoot = "unused-product-cache",
                .productManifestOutputPath = {},
            });

        return execution.writtenProducts.empty() &&
               expectExecutionDiagnostic(execution,
                                         asharia::asset::AssetProductExecutionDiagnosticCode::
                                             ShaderCompileReflectionImportFailed,
                                         "Missing dependency product bytes");
    }

    [[nodiscard]] bool smokeProductExecutionShaderCompileReflectionCompilerDiagnostics() {
        const std::filesystem::path root =
            smokeRoot("asharia-asset-pipeline-smoke-shader-compile-reflection-diagnostics");
        if (root.empty() || !prepareWorkspace(root)) {
            return false;
        }

        const std::vector<std::uint8_t> sourceBytes =
            bytesFromText(invalidCompileReflectionAshaderText());
        const std::vector<asharia::asset::AssetImportSetting> authoringSettings{
            asharia::asset::AssetImportSetting{
                .key = "shader.product",
                .value = "generated-slang-v1",
            },
        };
        const asharia::asset::SourceAssetRecord authoringSource =
            makeShaderAuthoringRecord(sourceBytes, authoringSettings);
        const std::filesystem::path outputRoot = root / "ProductCache";
        const asharia::asset::AssetProductExecutionResult authoringExecution =
            asharia::asset::executeAssetProducts(asharia::asset::AssetProductExecutionRequest{
                .plan = makeSingleProductExecutionPlan(authoringSource, authoringSettings),
                .existingManifest = {},
                .sourceBytes =
                    {
                        asharia::asset::AssetProductSourceBytes{
                            .sourcePath = authoringSource.sourcePath,
                            .bytes = sourceBytes,
                        },
                    },
                .dependencyProductBytes = {},
                .productOutputRoot = outputRoot,
                .productManifestOutputPath = outputRoot / "authoring-product-manifest.json",
            });
        if (!authoringExecution.succeeded() || authoringExecution.writtenProducts.size() != 1U) {
            logFailure(
                "Asset product execution smoke could not write invalid authoring dependency.");
            return false;
        }

        const asharia::asset::AssetProductWrite& authoringWrite =
            authoringExecution.writtenProducts.front();
        const std::vector<std::uint8_t> authoringProductBytes =
            readFileBytes(authoringWrite.productFilePath);
        if (authoringProductBytes.empty() ||
            smokeHashBytes(authoringProductBytes) != authoringWrite.product.productHash) {
            logFailure(
                "Asset product execution smoke could not read invalid authoring dependency.");
            return false;
        }

        const std::vector<asharia::asset::AssetImportSetting> compileSettings{
            asharia::asset::AssetImportSetting{
                .key = "shader.product",
                .value = "compiled-reflection-v1",
            },
            asharia::asset::AssetImportSetting{
                .key = "shader.authoringProductPath",
                .value = authoringWrite.product.relativeProductPath,
            },
        };
        const asharia::asset::SourceAssetRecord compileSource =
            makeShaderCompileReflectionRecord(sourceBytes, compileSettings);
        const asharia::asset::AssetProductExecutionResult compileExecution =
            asharia::asset::executeAssetProducts(asharia::asset::AssetProductExecutionRequest{
                .plan = makeShaderCompileReflectionPlan(compileSource, compileSettings,
                                                        authoringWrite.product.relativeProductPath,
                                                        authoringWrite.product.productHash),
                .existingManifest = authoringExecution.manifest,
                .sourceBytes =
                    {
                        asharia::asset::AssetProductSourceBytes{
                            .sourcePath = compileSource.sourcePath,
                            .bytes = sourceBytes,
                        },
                    },
                .dependencyProductBytes =
                    {
                        asharia::asset::AssetProductDependencyBytes{
                            .relativeProductPath = authoringWrite.product.relativeProductPath,
                            .productHash = authoringWrite.product.productHash,
                            .bytes = authoringProductBytes,
                        },
                    },
                .productOutputRoot = outputRoot,
                .productManifestOutputPath = outputRoot / "compile-product-manifest.json",
            });

        return compileExecution.writtenProducts.empty() &&
               expectExecutionDiagnostic(compileExecution,
                                         asharia::asset::AssetProductExecutionDiagnosticCode::
                                             ShaderCompileReflectionImportFailed,
                                         "slangc") &&
               expectExecutionDiagnostic(compileExecution,
                                         asharia::asset::AssetProductExecutionDiagnosticCode::
                                             ShaderCompileReflectionImportFailed,
                                         "definitelyMissingSymbol");
    }

    [[nodiscard]] bool smokeProductExecutionShaderCompileReflectionInvalidEntryDiagnostics() {
        const std::filesystem::path root =
            smokeRoot("asharia-asset-pipeline-smoke-shader-compile-reflection-invalid-entry");
        if (root.empty() || !prepareWorkspace(root)) {
            return false;
        }

        const std::vector<std::uint8_t> sourceBytes =
            bytesFromText(validCompileReflectionAshaderText());
        const std::vector<asharia::asset::AssetImportSetting> authoringSettings{
            asharia::asset::AssetImportSetting{
                .key = "shader.product",
                .value = "generated-slang-v1",
            },
        };
        const asharia::asset::SourceAssetRecord authoringSource =
            makeShaderAuthoringRecord(sourceBytes, authoringSettings);
        const std::filesystem::path outputRoot = root / "ProductCache";
        const asharia::asset::AssetProductExecutionResult authoringExecution =
            asharia::asset::executeAssetProducts(asharia::asset::AssetProductExecutionRequest{
                .plan = makeSingleProductExecutionPlan(authoringSource, authoringSettings),
                .existingManifest = {},
                .sourceBytes =
                    {
                        asharia::asset::AssetProductSourceBytes{
                            .sourcePath = authoringSource.sourcePath,
                            .bytes = sourceBytes,
                        },
                    },
                .dependencyProductBytes = {},
                .productOutputRoot = outputRoot,
                .productManifestOutputPath = outputRoot / "authoring-product-manifest.json",
            });
        if (!authoringExecution.succeeded() || authoringExecution.writtenProducts.size() != 1U) {
            logFailure("Asset product execution smoke could not write authoring dependency.");
            return false;
        }

        const asharia::asset::AssetProductWrite& authoringWrite =
            authoringExecution.writtenProducts.front();
        std::vector<std::uint8_t> authoringProductBytes =
            readFileBytes(authoringWrite.productFilePath);
        if (authoringProductBytes.empty() ||
            smokeHashBytes(authoringProductBytes) != authoringWrite.product.productHash) {
            logFailure("Asset product execution smoke could not read authoring dependency bytes.");
            return false;
        }
        if (!replaceFirst(authoringProductBytes, "entry.0.stage=vertex", "entry.0.stage=meshxx")) {
            logFailure("Asset product execution smoke could not mutate shader entry stage.");
            return false;
        }
        const std::uint64_t mutatedAuthoringProductHash = smokeHashBytes(authoringProductBytes);

        const std::vector<asharia::asset::AssetImportSetting> compileSettings{
            asharia::asset::AssetImportSetting{
                .key = "shader.product",
                .value = "compiled-reflection-v1",
            },
            asharia::asset::AssetImportSetting{
                .key = "shader.authoringProductPath",
                .value = authoringWrite.product.relativeProductPath,
            },
        };
        const asharia::asset::SourceAssetRecord compileSource =
            makeShaderCompileReflectionRecord(sourceBytes, compileSettings);
        const asharia::asset::AssetProductExecutionResult compileExecution =
            asharia::asset::executeAssetProducts(asharia::asset::AssetProductExecutionRequest{
                .plan = makeShaderCompileReflectionPlan(compileSource, compileSettings,
                                                        authoringWrite.product.relativeProductPath,
                                                        mutatedAuthoringProductHash),
                .existingManifest = authoringExecution.manifest,
                .sourceBytes =
                    {
                        asharia::asset::AssetProductSourceBytes{
                            .sourcePath = compileSource.sourcePath,
                            .bytes = sourceBytes,
                        },
                    },
                .dependencyProductBytes =
                    {
                        asharia::asset::AssetProductDependencyBytes{
                            .relativeProductPath = authoringWrite.product.relativeProductPath,
                            .productHash = mutatedAuthoringProductHash,
                            .bytes = authoringProductBytes,
                        },
                    },
                .productOutputRoot = outputRoot,
                .productManifestOutputPath = outputRoot / "compile-product-manifest.json",
            });

        return compileExecution.writtenProducts.empty() && !compileExecution.manifestWritten &&
               expectExecutionDiagnostic(compileExecution,
                                         asharia::asset::AssetProductExecutionDiagnosticCode::
                                             ShaderCompileReflectionImportFailed,
                                         "Unsupported Slang stage 'meshxx'");
    }

    [[nodiscard]] std::string smokeHex64(std::uint64_t value) {
        constexpr std::string_view kDigits = "0123456789abcdef";
        std::string text(16U, '0');
        for (std::size_t index = text.size(); index > 0U; --index) {
            text[index - 1U] = kDigits[static_cast<std::size_t>(value & 0x0FULL)];
            value >>= 4U;
        }
        return text;
    }

    [[nodiscard]] std::vector<std::uint8_t>
    makeBoundedTextureProduct(std::uint64_t mipCount, std::uint32_t width, std::uint32_t height,
                              std::uint64_t payloadSize = 0U) {
        std::string text = "schema=com.asharia.asset.texture2d-product.v1\n"
                           "sourcePath=Content/Textures/Bounded.rgba8\n"
                           "productType=Texture2D\n"
                           "importProfile=Texture 2D\n"
                           "settingsVersion=1\n"
                           "format=rgba8-unorm\n"
                           "width=" +
                           std::to_string(width) + "\nheight=" + std::to_string(height) +
                           "\nmip.count=" + std::to_string(mipCount) +
                           "\npayload.size=" + std::to_string(payloadSize) +
                           "\npayloadHash=cbf29ce484222325\n";
        if (mipCount <= 32U) {
            for (std::uint64_t index = 0; index < mipCount; ++index) {
                const std::string prefix = "mip." + std::to_string(index) + ".";
                text += prefix + "level=" + std::to_string(index) + "\n";
                text += prefix + "width=1\n";
                text += prefix + "height=1\n";
                text += prefix + "byteOffset=0\n";
                text += prefix + "byteSize=0\n";
            }
        }
        text += "payload.begin\n\npayload.end\n";
        return bytesFromText(text);
    }

    [[nodiscard]] std::vector<std::uint8_t>
    makeBoundedShaderAuthoringProduct(std::uint64_t propertyCount, std::uint64_t passCount,
                                      std::uint64_t bindingCount, std::uint64_t entryCount,
                                      std::uint64_t generatedSlangSize = 0U) {
        std::string text = "schema=com.asharia.asset.shader-authoring-product.v1\n"
                           "sourcePath=Content/Shaders/Bounded.ashader\n"
                           "shader.stableTypeId=asharia.material.bounded\n"
                           "ashader.schemaVersion=2\n"
                           "property.count=" +
                           std::to_string(propertyCount) +
                           "\npass.count=" + std::to_string(passCount) +
                           "\nbinding.count=" + std::to_string(bindingCount) +
                           "\nentry.count=" + std::to_string(entryCount) +
                           "\ngeneratedSlang.size=" + std::to_string(generatedSlangSize) +
                           "\ngeneratedSlangHash=cbf29ce484222325\n";
        if (propertyCount <= 8U) {
            for (std::uint64_t index = 0; index < propertyCount; ++index) {
                const std::string prefix = "property." + std::to_string(index) + ".";
                text += prefix + "name=value" + std::to_string(index) + "\n";
                text += prefix + "type=float\n";
                text += prefix + "default=0.0\n";
            }
        }
        if (passCount <= 8U) {
            for (std::uint64_t index = 0; index < passCount; ++index) {
                const std::string prefix = "pass." + std::to_string(index) + ".";
                text += prefix + "name=Forward" + std::to_string(index) + "\n";
                text += prefix + "tag=SceneForward\n";
                text += prefix + "vertex=vertexMain\n";
                text += prefix + "fragment=fragmentMain\n";
                text += prefix + "compute=\n";
            }
        }
        if (bindingCount <= 8U) {
            for (std::uint64_t index = 0; index < bindingCount; ++index) {
                const std::string prefix = "binding." + std::to_string(index) + ".";
                text += prefix + "name=resource" + std::to_string(index) + "\n";
                text += prefix + "type=Texture2D\n";
                text += prefix + "set=0\n";
                text += prefix + "binding=" + std::to_string(index) + "\n";
                text += prefix + "inMaterialParameterBlock=false\n";
            }
        }
        if (entryCount <= 8U) {
            for (std::uint64_t index = 0; index < entryCount; ++index) {
                const std::string prefix = "entry." + std::to_string(index) + ".";
                text += prefix + "passName=Forward0\n";
                text += prefix + "stage=vertex\n";
                text += prefix + "sourceEntry=vertexMain\n";
                text += prefix + "compileEntry=vertexMain\n";
                text += prefix + "generatedWrapper=wrapper" + std::to_string(index) + "\n";
            }
        }
        text += "generatedSlang.begin\n\ngeneratedSlang.end\n";
        return bytesFromText(text);
    }

    struct BoundedCompiledPayloadFields {
        std::uint64_t slangcDiagnosticSize{};
        std::string_view slangcDiagnosticHex;
        std::uint64_t spirvValDiagnosticSize{};
        std::string_view spirvValDiagnosticHex;
        std::uint64_t spirvSize{1U};
        std::string_view spirvHex{"00"};
        std::uint64_t reflectionJsonSize{2U};
        std::string_view reflectionJsonHex{"7b7d"};
    };

    [[nodiscard]] std::vector<std::uint8_t>
    makeBoundedShaderCompileProduct(std::uint64_t entryCount,
                                    const BoundedCompiledPayloadFields& payloads = {}) {
        const std::vector<std::uint8_t> spirv{0U};
        const std::vector<std::uint8_t> reflection = bytesFromText("{}");
        std::string text = "schema=com.asharia.asset.shader-compile-reflection-product.v1\n"
                           "sourcePath=Content/Shaders/Bounded.ashader\n"
                           "shader.stableTypeId=asharia.material.bounded\n"
                           "authoringProductPath=generated/Bounded.authoring.product\n"
                           "authoringProductHash=0000000000000001\n"
                           "generatedSlangHash=0000000000000002\n"
                           "productKeyHash=0000000000000003\n"
                           "profile=glsl_450\n"
                           "target=spirv\n"
                           "entry.count=" +
                           std::to_string(entryCount) + "\n";
        if (entryCount <= 8U) {
            for (std::uint64_t index = 0; index < entryCount; ++index) {
                const std::string prefix = "entry." + std::to_string(index) + ".";
                text += prefix + "passName=Forward\n";
                text += prefix + "stage=vertex\n";
                text += prefix + "sourceEntry=vertexMain\n";
                text += prefix + "compileEntry=vertexMain\n";
                text += prefix + "generatedWrapper=wrapper" + std::to_string(index) + "\n";
                text += prefix + "slangcExitCode=0\n";
                text += prefix + "slangcDiagnosticHash=cbf29ce484222325\n";
                text += prefix +
                        "slangcDiagnosticSize=" + std::to_string(payloads.slangcDiagnosticSize) +
                        "\n";
                text += prefix +
                        "slangcDiagnosticHex=" + std::string{payloads.slangcDiagnosticHex} + "\n";
                text += prefix + "spirvValExitCode=0\n";
                text += prefix + "spirvValDiagnosticHash=cbf29ce484222325\n";
                text += prefix + "spirvValDiagnosticSize=" +
                        std::to_string(payloads.spirvValDiagnosticSize) + "\n";
                text += prefix +
                        "spirvValDiagnosticHex=" + std::string{payloads.spirvValDiagnosticHex} +
                        "\n";
                text += prefix + "spirvHash=" + smokeHex64(smokeHashBytes(spirv)) + "\n";
                text += prefix + "spirvSize=" + std::to_string(payloads.spirvSize) + "\n";
                text += prefix + "spirvHex=" + std::string{payloads.spirvHex} + "\n";
                text +=
                    prefix + "reflectionJsonHash=" + smokeHex64(smokeHashBytes(reflection)) + "\n";
                text += prefix +
                        "reflectionJsonSize=" + std::to_string(payloads.reflectionJsonSize) + "\n";
                text +=
                    prefix + "reflectionJsonHex=" + std::string{payloads.reflectionJsonHex} + "\n";
            }
        }
        return bytesFromText(text);
    }

    enum class BoundedCompiledPayloadKind : std::uint8_t {
        SlangcDiagnostic,
        SpirvValDiagnostic,
        Spirv,
        ReflectionJson,
    };

    [[nodiscard]] std::vector<std::uint8_t>
    makeBoundedShaderCompileProductWithDeclaredSize(BoundedCompiledPayloadKind kind,
                                                    std::uint64_t declaredSize) {
        BoundedCompiledPayloadFields payloads;
        switch (kind) {
        case BoundedCompiledPayloadKind::SlangcDiagnostic:
            payloads.slangcDiagnosticSize = declaredSize;
            break;
        case BoundedCompiledPayloadKind::SpirvValDiagnostic:
            payloads.spirvValDiagnosticSize = declaredSize;
            break;
        case BoundedCompiledPayloadKind::Spirv:
            payloads.spirvSize = declaredSize;
            break;
        case BoundedCompiledPayloadKind::ReflectionJson:
            payloads.reflectionJsonSize = declaredSize;
            break;
        }
        return makeBoundedShaderCompileProduct(1U, payloads);
    }

    [[nodiscard]] bool smokeProductBlobPrivateValidation() {
        const std::string header = "payload=0011\n";
        std::string_view expectedPayload{header};
        expectedPayload.remove_prefix(8U);
        expectedPayload.remove_suffix(1U);
        const auto payload = asharia::asset::detail::requirePresentAssetProductHeaderValue(
            header, "payload", "bounded/header-view.product");
        if (!payload || *payload != "0011" || payload->data() != expectedPayload.data()) {
            logFailure("Asset product blob private validation copied a header field value.");
            return false;
        }

        const auto invalidMinimum = asharia::asset::detail::validateAssetProductRecordCount({
            .count = 1U,
            .hardLimit = 1U,
            .headerLineCount = 1U,
            .minimumLinesPerRecord = 0U,
            .recordName = "private test records",
            .relativeProductPath = "bounded/invalid-minimum.product",
        });
        if (invalidMinimum || invalidMinimum.error().domain != asharia::ErrorDomain::Asset ||
            invalidMinimum.error().code !=
                static_cast<int>(
                    asharia::asset::AssetProductBlobDiagnosticCode::InvalidProductBlob) ||
            !messageContains(invalidMinimum.error().message, "no minimum field count") ||
            !messageContains(invalidMinimum.error().message, "invalid-minimum.product")) {
            logFailure("Asset product record validator missed an invalid minimum field count.");
            return false;
        }

        const auto insufficientHeader = asharia::asset::detail::validateAssetProductRecordCount({
            .count = 2U,
            .hardLimit = 2U,
            .headerLineCount = 3U,
            .minimumLinesPerRecord = 2U,
            .recordName = "private test records",
            .relativeProductPath = "bounded/insufficient-header.product",
        });
        if (insufficientHeader ||
            insufficientHeader.error().domain != asharia::ErrorDomain::Asset ||
            insufficientHeader.error().code !=
                static_cast<int>(
                    asharia::asset::AssetProductBlobDiagnosticCode::InvalidProductBlob) ||
            !messageContains(insufficientHeader.error().message,
                             "available product header fields") ||
            !messageContains(insufficientHeader.error().message, "private test records")) {
            logFailure("Asset product record validator missed insufficient header fields.");
            return false;
        }

        return true;
    }

    [[nodiscard]] bool
    smokeTextureProductDimensionValidation(asharia::asset::AssetProductBlobReadLimits limits) {
        const auto impossibleMips = makeBoundedTextureProduct(2U, 1U, 1U);
        if (!expectProductBlobErrorWithoutException(
                [&] {
                    return asharia::asset::readTexture2DProductPayload(
                        std::span<const std::uint8_t>{impossibleMips},
                        "bounded/impossible-mips.product", limits);
                },
                asharia::asset::AssetProductBlobDiagnosticCode::InvalidProductBlob,
                "declared dimensions")) {
            return false;
        }

        for (const auto& invalidDimensions :
             {makeBoundedTextureProduct(1U, 0U, 1U), makeBoundedTextureProduct(1U, 1U, 0U),
              makeBoundedTextureProduct(1U, 0U, 0U)}) {
            if (!expectProductBlobErrorWithoutException(
                    [&] {
                        return asharia::asset::readTexture2DProductPayload(
                            std::span<const std::uint8_t>{invalidDimensions},
                            "bounded/zero-dimension.product", limits);
                    },
                    asharia::asset::AssetProductBlobDiagnosticCode::InvalidProductBlob,
                    "declared dimensions")) {
                return false;
            }
        }

        const auto extremeDimensions =
            makeBoundedTextureProduct(32U, std::numeric_limits<std::uint32_t>::max(),
                                      std::numeric_limits<std::uint32_t>::max());
        limits.maxTextureMipRecords = 32U;
        if (!asharia::asset::readTexture2DProductPayload(
                std::span<const std::uint8_t>{extremeDimensions},
                "bounded/extreme-dimensions.product", limits)) {
            logFailure("Asset product blob limit smoke rejected 32 valid extreme-dimension mips.");
            return false;
        }
        return true;
    }

    [[nodiscard]] bool smokeCompiledPayloadDeclaredSizeValidation(
        const asharia::asset::AssetProductBlobReadLimits& limits, std::uint64_t maxCount) {
        struct DeclaredCompiledPayloadCase {
            BoundedCompiledPayloadKind kind;
            std::string_view payloadName;
        };
        constexpr std::array kDeclaredCompiledPayloadCases{
            DeclaredCompiledPayloadCase{
                .kind = BoundedCompiledPayloadKind::SlangcDiagnostic,
                .payloadName = "slangc diagnostic payload size mismatch",
            },
            DeclaredCompiledPayloadCase{
                .kind = BoundedCompiledPayloadKind::SpirvValDiagnostic,
                .payloadName = "spirv-val diagnostic payload size mismatch",
            },
            DeclaredCompiledPayloadCase{
                .kind = BoundedCompiledPayloadKind::Spirv,
                .payloadName = "SPIR-V payload size mismatch",
            },
            DeclaredCompiledPayloadCase{
                .kind = BoundedCompiledPayloadKind::ReflectionJson,
                .payloadName = "reflection JSON payload size mismatch",
            },
        };
        for (const DeclaredCompiledPayloadCase& payloadCase : kDeclaredCompiledPayloadCases) {
            const auto bytes =
                makeBoundedShaderCompileProductWithDeclaredSize(payloadCase.kind, maxCount);
            if (!expectProductBlobErrorWithoutException(
                    [&] {
                        return asharia::asset::readShaderCompileReflectionProductPayload(
                            std::span<const std::uint8_t>{bytes},
                            "bounded/max-compiled-payload.product", limits);
                    },
                    asharia::asset::AssetProductBlobDiagnosticCode::InvalidProductBlob,
                    payloadCase.payloadName)) {
                return false;
            }
        }

        const std::string largeHex(std::size_t{64U} * 1024U, '0');
        BoundedCompiledPayloadFields largePayloads;
        largePayloads.spirvHex = largeHex;
        const auto mismatchedLargeCompile = makeBoundedShaderCompileProduct(1U, largePayloads);
        return expectProductBlobErrorWithoutException(
            [&] {
                return asharia::asset::readShaderCompileReflectionProductPayload(
                    std::span<const std::uint8_t>{mismatchedLargeCompile},
                    "bounded/large-compiled-payload.product",
                    asharia::asset::AssetProductBlobReadLimits{
                        .maxProductBytes = mismatchedLargeCompile.size(),
                    });
            },
            asharia::asset::AssetProductBlobDiagnosticCode::InvalidProductBlob,
            "SPIR-V payload size mismatch");
    }

    [[nodiscard]] bool smokeProductBlobReadLimits() {
        const auto placeholder =
            bytesFromText("schema=placeholder\nsourceBytes.begin\nabc\nsourceBytes.end\n");
        asharia::asset::AssetProductBlobReadLimits limits{
            .maxProductBytes = placeholder.size(),
            .maxTextureMipRecords = 2U,
            .maxShaderProperties = 2U,
            .maxShaderPasses = 2U,
            .maxShaderBindings = 2U,
            .maxShaderEntries = 2U,
        };
        const auto exactPlaceholder = asharia::asset::readPlaceholderProductSourceBytes(
            std::span<const std::uint8_t>{placeholder}, "bounded/exact.product", limits);
        if (!exactPlaceholder || exactPlaceholder->sourceBytes != bytesFromText("abc")) {
            logFailure("Asset product blob limit smoke rejected the exact byte limit.");
            return false;
        }
        limits.maxProductBytes = placeholder.size() - 1U;
        if (!expectProductBlobErrorWithoutException(
                [&] {
                    return asharia::asset::readPlaceholderProductSourceBytes(
                        std::span<const std::uint8_t>{placeholder}, "bounded/bytes.product",
                        limits);
                },
                asharia::asset::AssetProductBlobDiagnosticCode::InvalidProductBlob,
                "product byte limit")) {
            return false;
        }

        limits.maxProductBytes = 64ULL * 1024ULL;
        const auto texture = makeBoundedTextureProduct(2U, 2U, 2U);
        if (!asharia::asset::readTexture2DProductPayload(std::span<const std::uint8_t>{texture},
                                                         "bounded/texture-exact.product", limits)) {
            logFailure("Asset product blob limit smoke rejected the exact mip limit.");
            return false;
        }
        limits.maxTextureMipRecords = 1U;
        if (!expectProductBlobErrorWithoutException(
                [&] {
                    return asharia::asset::readTexture2DProductPayload(
                        std::span<const std::uint8_t>{texture}, "bounded/mip-limit.product",
                        limits);
                },
                asharia::asset::AssetProductBlobDiagnosticCode::InvalidProductBlob,
                "mip records")) {
            return false;
        }
        limits.maxTextureMipRecords = 2U;
        if (!smokeTextureProductDimensionValidation(limits)) {
            return false;
        }

        const auto authoring = makeBoundedShaderAuthoringProduct(2U, 2U, 2U, 2U);
        if (!asharia::asset::readShaderAuthoringProductPayload(
                std::span<const std::uint8_t>{authoring}, "bounded/authoring-exact.product",
                limits)) {
            logFailure("Asset product blob limit smoke rejected exact shader record limits.");
            return false;
        }
        struct AuthoringLimitCase {
            std::uint32_t asharia::asset::AssetProductBlobReadLimits::* member;
            std::string_view recordName;
        };
        constexpr std::array kAuthoringLimitCases{
            AuthoringLimitCase{.member =
                                   &asharia::asset::AssetProductBlobReadLimits::maxShaderProperties,
                               .recordName = "property records"},
            AuthoringLimitCase{.member =
                                   &asharia::asset::AssetProductBlobReadLimits::maxShaderPasses,
                               .recordName = "pass records"},
            AuthoringLimitCase{.member =
                                   &asharia::asset::AssetProductBlobReadLimits::maxShaderBindings,
                               .recordName = "binding records"},
            AuthoringLimitCase{.member =
                                   &asharia::asset::AssetProductBlobReadLimits::maxShaderEntries,
                               .recordName = "entry records"},
        };
        for (const AuthoringLimitCase& limitCase : kAuthoringLimitCases) {
            auto restricted = limits;
            restricted.*(limitCase.member) = 1U;
            if (!expectProductBlobErrorWithoutException(
                    [&] {
                        return asharia::asset::readShaderAuthoringProductPayload(
                            std::span<const std::uint8_t>{authoring},
                            "bounded/authoring-limit.product", restricted);
                    },
                    asharia::asset::AssetProductBlobDiagnosticCode::InvalidProductBlob,
                    limitCase.recordName)) {
                return false;
            }
        }

        const auto compiled = makeBoundedShaderCompileProduct(2U);
        if (!asharia::asset::readShaderCompileReflectionProductPayload(
                std::span<const std::uint8_t>{compiled}, "bounded/compiled-exact.product",
                limits)) {
            logFailure("Asset product blob limit smoke rejected the exact compiled entry limit.");
            return false;
        }
        limits.maxShaderEntries = 1U;
        if (!expectProductBlobErrorWithoutException(
                [&] {
                    return asharia::asset::readShaderCompileReflectionProductPayload(
                        std::span<const std::uint8_t>{compiled}, "bounded/compiled-limit.product",
                        limits);
                },
                asharia::asset::AssetProductBlobDiagnosticCode::InvalidProductBlob,
                "compiled shader entry records")) {
            return false;
        }

        limits = {};
        const auto maxCount = std::numeric_limits<std::uint64_t>::max();
        const auto maxMipCountProduct = makeBoundedTextureProduct(maxCount, 1U, 1U);
        if (!expectProductBlobErrorWithoutException(
                [&] {
                    return asharia::asset::readTexture2DProductPayload(
                        std::span<const std::uint8_t>{maxMipCountProduct},
                        "bounded/max-mips.product", limits);
                },
                asharia::asset::AssetProductBlobDiagnosticCode::InvalidProductBlob,
                "mip records")) {
            return false;
        }
        const std::array authoringMaxCounts{
            makeBoundedShaderAuthoringProduct(maxCount, 1U, 0U, 1U),
            makeBoundedShaderAuthoringProduct(0U, maxCount, 0U, 1U),
            makeBoundedShaderAuthoringProduct(0U, 1U, maxCount, 1U),
            makeBoundedShaderAuthoringProduct(0U, 1U, 0U, maxCount),
        };
        for (const auto& bytes : authoringMaxCounts) {
            if (!expectProductBlobErrorWithoutException(
                    [&] {
                        return asharia::asset::readShaderAuthoringProductPayload(
                            std::span<const std::uint8_t>{bytes},
                            "bounded/max-authoring-count.product", limits);
                    },
                    asharia::asset::AssetProductBlobDiagnosticCode::InvalidProductBlob,
                    "records")) {
                return false;
            }
        }
        const auto compileMaxCount = makeBoundedShaderCompileProduct(maxCount);
        if (!expectProductBlobErrorWithoutException(
                [&] {
                    return asharia::asset::readShaderCompileReflectionProductPayload(
                        std::span<const std::uint8_t>{compileMaxCount},
                        "bounded/max-compiled-count.product", limits);
                },
                asharia::asset::AssetProductBlobDiagnosticCode::InvalidProductBlob,
                "compiled shader entry records")) {
            return false;
        }

        const auto oversizedTexture = makeBoundedTextureProduct(1U, 1U, 1U, maxCount);
        const auto oversizedMaterial =
            bytesFromText("schema=com.asharia.asset.material-instance-product.v1\n"
                          "sourcePath=Content/Materials/Bounded.amat\n"
                          "materialType.assetGuid=11111111-1111-1111-1111-111111111111\n"
                          "materialType.stableTypeId=asharia.material.bounded\n"
                          "materialType.expectedTypeHash=0000000000000001\n"
                          "import.lastCookedSignatureHash=0000000000000002\n"
                          "amat.size=18446744073709551615\n"
                          "amatHash=cbf29ce484222325\n"
                          "amat.begin\n\namat.end\n");
        const auto oversizedAuthoring = makeBoundedShaderAuthoringProduct(0U, 1U, 0U, 1U, maxCount);
        const auto oversizedCompile = makeBoundedShaderCompileProductWithDeclaredSize(
            BoundedCompiledPayloadKind::Spirv, maxCount);
        if (!expectProductBlobErrorWithoutException(
                [&] {
                    return asharia::asset::readTexture2DProductPayload(
                        std::span<const std::uint8_t>{oversizedTexture},
                        "bounded/oversized-texture.product", limits);
                },
                asharia::asset::AssetProductBlobDiagnosticCode::UnterminatedPayload,
                "texture payload") ||
            !expectProductBlobErrorWithoutException(
                [&] {
                    return asharia::asset::readMaterialInstanceProductPayload(
                        std::span<const std::uint8_t>{oversizedMaterial},
                        "bounded/oversized-material.product", limits);
                },
                asharia::asset::AssetProductBlobDiagnosticCode::UnterminatedPayload,
                ".amat payload") ||
            !expectProductBlobErrorWithoutException(
                [&] {
                    return asharia::asset::readShaderAuthoringProductPayload(
                        std::span<const std::uint8_t>{oversizedAuthoring},
                        "bounded/oversized-authoring.product", limits);
                },
                asharia::asset::AssetProductBlobDiagnosticCode::UnterminatedPayload,
                "generated Slang payload") ||
            !expectProductBlobErrorWithoutException(
                [&] {
                    return asharia::asset::readShaderCompileReflectionProductPayload(
                        std::span<const std::uint8_t>{oversizedCompile},
                        "bounded/oversized-compile.product", limits);
                },
                asharia::asset::AssetProductBlobDiagnosticCode::InvalidProductBlob,
                "SPIR-V payload size mismatch")) {
            return false;
        }

        if (!smokeCompiledPayloadDeclaredSizeValidation(limits, maxCount)) {
            return false;
        }

        const std::filesystem::path root = smokeRoot("asharia-product-blob-bounded-file");
        const std::filesystem::path productPath = root / "bounded.product";
        if (root.empty() || !prepareWorkspace(root) ||
            !writeTextFile(productPath, std::string{placeholder.begin(), placeholder.end()})) {
            return false;
        }
        limits.maxProductBytes = placeholder.size();
        const auto exactFile = asharia::asset::readPlaceholderProductSourceBytes(
            asharia::asset::AssetProductBlobReadRequest{
                .productFilePath = productPath,
                .relativeProductPath = "bounded/file-exact.product",
            },
            limits);
        limits.maxProductBytes = placeholder.size() - 1U;
        return exactFile && expectProductBlobErrorWithoutException(
                                [&] {
                                    return asharia::asset::readPlaceholderProductSourceBytes(
                                        asharia::asset::AssetProductBlobReadRequest{
                                            .productFilePath = productPath,
                                            .relativeProductPath = "bounded/file-too-large.product",
                                        },
                                        limits);
                                },
                                asharia::asset::AssetProductBlobDiagnosticCode::ProductReadFailed,
                                "bounded/file-too-large.product");
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

        const std::vector<std::uint8_t> noAmatPayload =
            bytesFromText("schema=com.asharia.asset.material-instance-product.v1\n");
        auto missingAmatPayload = asharia::asset::readMaterialInstanceProductPayload(
            std::span<const std::uint8_t>{noAmatPayload.data(), noAmatPayload.size()},
            "materials/no-amat.product");
        if (!expectProductBlobError(missingAmatPayload,
                                    asharia::asset::AssetProductBlobDiagnosticCode::MissingPayload,
                                    "amat.begin")) {
            return false;
        }

        const std::vector<std::uint8_t> unterminatedAmatPayload =
            bytesFromText("schema=com.asharia.asset.material-instance-product.v1\n"
                          "sourcePath=Content/Materials/Red.amat\n"
                          "materialType.assetGuid=11111111-1111-1111-1111-111111111111\n"
                          "materialType.stableTypeId=asharia.material.unlit\n"
                          "materialType.expectedTypeHash=00000000000000aa\n"
                          "import.lastCookedSignatureHash=00000000000000bb\n"
                          "amat.size=2\n"
                          "amatHash=0000000000000001\n"
                          "amat.begin\n"
                          "{}");
        auto unterminatedAmat = asharia::asset::readMaterialInstanceProductPayload(
            std::span<const std::uint8_t>{unterminatedAmatPayload.data(),
                                          unterminatedAmatPayload.size()},
            "materials/unterminated-amat.product");
        if (!expectProductBlobError(
                unterminatedAmat,
                asharia::asset::AssetProductBlobDiagnosticCode::UnterminatedPayload,
                "unterminated .amat payload")) {
            return false;
        }

        const std::vector<std::uint8_t> noShaderPayload =
            bytesFromText("schema=com.asharia.asset.shader-authoring-product.v1\n");
        auto missingShaderPayload = asharia::asset::readShaderAuthoringProductPayload(
            std::span<const std::uint8_t>{noShaderPayload.data(), noShaderPayload.size()},
            "shaders/no-generated-slang.product");
        if (!expectProductBlobError(missingShaderPayload,
                                    asharia::asset::AssetProductBlobDiagnosticCode::MissingPayload,
                                    "generatedSlang.begin")) {
            return false;
        }

        const std::vector<std::uint8_t> badShaderPayload =
            bytesFromText("schema=com.asharia.asset.shader-authoring-product.v1\n"
                          "sourcePath=Content/Shaders/Unlit.ashader\n"
                          "shader.stableTypeId=asharia.material.unlit\n"
                          "ashader.schemaVersion=2\n"
                          "property.count=0\n"
                          "pass.count=1\n"
                          "pass.0.name=Forward\n"
                          "pass.0.tag=\n"
                          "pass.0.vertex=vertexMain\n"
                          "pass.0.fragment=fragmentMain\n"
                          "pass.0.compute=\n"
                          "binding.count=0\n"
                          "entry.count=1\n"
                          "entry.0.passName=Forward\n"
                          "entry.0.stage=vertex\n"
                          "entry.0.sourceEntry=vertexMain\n"
                          "entry.0.compileEntry=vertexMain\n"
                          "entry.0.generatedWrapper=__asharia_Forward_vertex\n"
                          "generatedSlang.size=4\n"
                          "generatedSlangHash=0000000000000001\n"
                          "generatedSlang.begin\n"
                          "abcd\n"
                          "generatedSlang.end\n");
        auto badShaderHash = asharia::asset::readShaderAuthoringProductPayload(
            std::span<const std::uint8_t>{badShaderPayload.data(), badShaderPayload.size()},
            "shaders/bad-generated-slang.product");
        if (!expectProductBlobError(
                badShaderHash, asharia::asset::AssetProductBlobDiagnosticCode::InvalidProductBlob,
                "generated Slang payload hash mismatch")) {
            return false;
        }

        const std::vector<std::uint8_t> unterminatedShaderPayload =
            bytesFromText("schema=com.asharia.asset.shader-authoring-product.v1\n"
                          "sourcePath=Content/Shaders/Unlit.ashader\n"
                          "shader.stableTypeId=asharia.material.unlit\n"
                          "ashader.schemaVersion=2\n"
                          "property.count=0\n"
                          "pass.count=1\n"
                          "pass.0.name=Forward\n"
                          "pass.0.tag=\n"
                          "pass.0.vertex=vertexMain\n"
                          "pass.0.fragment=fragmentMain\n"
                          "pass.0.compute=\n"
                          "binding.count=0\n"
                          "entry.count=1\n"
                          "entry.0.passName=Forward\n"
                          "entry.0.stage=vertex\n"
                          "entry.0.sourceEntry=vertexMain\n"
                          "entry.0.compileEntry=vertexMain\n"
                          "entry.0.generatedWrapper=__asharia_Forward_vertex\n"
                          "generatedSlang.size=4\n"
                          "generatedSlangHash=0000000000000001\n"
                          "generatedSlang.begin\n"
                          "abcd");
        auto unterminatedShader = asharia::asset::readShaderAuthoringProductPayload(
            std::span<const std::uint8_t>{unterminatedShaderPayload.data(),
                                          unterminatedShaderPayload.size()},
            "shaders/unterminated-generated-slang.product");
        if (!expectProductBlobError(
                unterminatedShader,
                asharia::asset::AssetProductBlobDiagnosticCode::UnterminatedPayload,
                "unterminated generated Slang payload")) {
            return false;
        }

        const std::vector<std::uint8_t> badCompileReflectionPayload =
            bytesFromText("schema=com.asharia.asset.shader-compile-reflection-product.v1\n"
                          "sourcePath=Content/Shaders/Unlit.ashader\n"
                          "shader.stableTypeId=asharia.material.unlit\n"
                          "authoringProductPath=generated/Unlit.authoring.product\n"
                          "authoringProductHash=0000000000000001\n"
                          "generatedSlangHash=0000000000000002\n"
                          "productKeyHash=0000000000000003\n"
                          "profile=glsl_450\n"
                          "target=spirv\n"
                          "entry.count=1\n"
                          "entry.0.passName=Forward\n"
                          "entry.0.stage=vertex\n"
                          "entry.0.sourceEntry=vertexMain\n"
                          "entry.0.compileEntry=vertexMain\n"
                          "entry.0.generatedWrapper=__asharia_Forward_vertex\n"
                          "entry.0.slangcExitCode=0\n"
                          "entry.0.slangcDiagnosticHash=cbf29ce484222325\n"
                          "entry.0.slangcDiagnosticSize=0\n"
                          "entry.0.slangcDiagnosticHex=\n"
                          "entry.0.spirvValExitCode=0\n"
                          "entry.0.spirvValDiagnosticHash=cbf29ce484222325\n"
                          "entry.0.spirvValDiagnosticSize=0\n"
                          "entry.0.spirvValDiagnosticHex=\n"
                          "entry.0.spirvHash=0000000000000001\n"
                          "entry.0.spirvSize=2\n"
                          "entry.0.spirvHex=abcd\n"
                          "entry.0.reflectionJsonHash=0000000000000001\n"
                          "entry.0.reflectionJsonSize=2\n"
                          "entry.0.reflectionJsonHex=7b7d\n");
        auto badCompileReflectionHash = asharia::asset::readShaderCompileReflectionProductPayload(
            std::span<const std::uint8_t>{badCompileReflectionPayload.data(),
                                          badCompileReflectionPayload.size()},
            "shaders/bad-compile-reflection.product");
        if (!expectProductBlobError(
                badCompileReflectionHash,
                asharia::asset::AssetProductBlobDiagnosticCode::InvalidProductBlob,
                "SPIR-V payload hash mismatch")) {
            return false;
        }

        const std::vector<std::uint8_t> missingCompileReflectionPayload =
            bytesFromText("schema=com.asharia.asset.shader-compile-reflection-product.v1\n"
                          "sourcePath=Content/Shaders/Unlit.ashader\n"
                          "shader.stableTypeId=asharia.material.unlit\n"
                          "authoringProductPath=generated/Unlit.authoring.product\n"
                          "authoringProductHash=0000000000000001\n"
                          "generatedSlangHash=0000000000000002\n"
                          "productKeyHash=0000000000000003\n"
                          "profile=glsl_450\n"
                          "target=spirv\n"
                          "entry.count=1\n"
                          "entry.0.passName=Forward\n"
                          "entry.0.stage=vertex\n"
                          "entry.0.sourceEntry=vertexMain\n"
                          "entry.0.compileEntry=vertexMain\n"
                          "entry.0.generatedWrapper=__asharia_Forward_vertex\n"
                          "entry.0.slangcExitCode=0\n"
                          "entry.0.slangcDiagnosticHash=cbf29ce484222325\n"
                          "entry.0.slangcDiagnosticSize=0\n"
                          "entry.0.slangcDiagnosticHex=\n"
                          "entry.0.spirvValExitCode=0\n"
                          "entry.0.spirvValDiagnosticHash=cbf29ce484222325\n"
                          "entry.0.spirvValDiagnosticSize=0\n"
                          "entry.0.spirvValDiagnosticHex=\n"
                          "entry.0.spirvHash=0000000000000001\n"
                          "entry.0.spirvSize=2\n"
                          "entry.0.spirvHex=abcd\n"
                          "entry.0.reflectionJsonHash=0000000000000001\n"
                          "entry.0.reflectionJsonSize=2\n");
        auto missingCompileReflectionField =
            asharia::asset::readShaderCompileReflectionProductPayload(
                std::span<const std::uint8_t>{missingCompileReflectionPayload.data(),
                                              missingCompileReflectionPayload.size()},
                "shaders/missing-compile-reflection-payload.product");
        if (!expectProductBlobError(
                missingCompileReflectionField,
                asharia::asset::AssetProductBlobDiagnosticCode::InvalidProductBlob,
                "entry.0.reflectionJsonHex")) {
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
        if (!writeScannedPlanningSource(
                contentRoot, {.relativePath = "Textures/Crate.png",
                              .bytes = "crate bytes",
                              .guid = assetGuidText("9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c21"),
                              .sourceHash = 0x1000F00D1234CAFEULL})) {
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
                .dependencyProductBytes = {},
                .productOutputRoot = root / "ProductCache",
                .productManifestOutputPath = root / "ProductCache" / "product-manifest.json",
            });

        return execution.writtenProducts.empty() &&
               expectExecutionDiagnostic(
                   execution,
                   asharia::asset::AssetProductExecutionDiagnosticCode::SourceBytesHashMismatch,
                   "hash mismatch");
    }

    [[nodiscard]] bool smokeProductExecutionDependencyProductBytesDiagnostics() {
        const std::filesystem::path root = smokeRoot(
            "asharia-asset-pipeline-smoke-product-execution-dependency-product-diagnostics");
        if (root.empty() || !prepareWorkspace(root)) {
            return false;
        }

        const std::vector<asharia::asset::AssetImportSetting> settings{
            asharia::asset::AssetImportSetting{
                .key = "product.kind",
                .value = "dependency-product-bytes-smoke",
            },
        };
        const std::vector<std::uint8_t> sourceBytes = bytesFromText("source bytes");
        const asharia::asset::SourceAssetRecord source =
            makeDependencyProductBytesSmokeRecord(sourceBytes, settings);
        const std::vector<std::uint8_t> invalidPathBytes = bytesFromText("invalid path product");
        const std::vector<std::uint8_t> duplicateBytes = bytesFromText("duplicate product");
        const std::vector<std::uint8_t> mismatchBytes = bytesFromText("mismatch product");

        const asharia::asset::AssetProductExecutionResult
            execution =
                asharia::asset::executeAssetProducts(
                    asharia::asset::AssetProductExecutionRequest{
                        .plan = makeSingleProductExecutionPlan(source, settings),
                        .existingManifest = {},
                        .sourceBytes =
                            {
                                asharia::asset::AssetProductSourceBytes{
                                    .sourcePath = source.sourcePath,
                                    .bytes = sourceBytes,
                                },
                            },
                        .dependencyProductBytes =
                            {
                                asharia::asset::AssetProductDependencyBytes{
                                    .relativeProductPath = "generated\\bad.product",
                                    .productHash = smokeHashBytes(invalidPathBytes),
                                    .bytes = invalidPathBytes,
                                },
                                asharia::asset::AssetProductDependencyBytes{
                                    .relativeProductPath = "generated/Unlit.ashader.product",
                                    .productHash = smokeHashBytes(duplicateBytes),
                                    .bytes = duplicateBytes,
                                },
                                asharia::asset::AssetProductDependencyBytes{
                                    .relativeProductPath = "generated/Unlit.ashader.product",
                                    .productHash = smokeHashBytes(duplicateBytes),
                                    .bytes = duplicateBytes,
                                },
                                asharia::asset::AssetProductDependencyBytes{
                                    .relativeProductPath = "generated/Mismatch.ashader.product",
                                    .productHash = 0x1234ULL,
                                    .bytes = mismatchBytes,
                                },
                            },
                        .productOutputRoot = root / "ProductCache",
                        .productManifestOutputPath =
                            root / "ProductCache" / "product-manifest.json",
                    });

        return execution.writtenProducts.empty() && !execution.manifestWritten &&
               expectExecutionDiagnostic(execution,
                                         asharia::asset::AssetProductExecutionDiagnosticCode::
                                             InvalidDependencyProductBytes,
                                         "must use '/' separators") &&
               expectExecutionDiagnostic(execution,
                                         asharia::asset::AssetProductExecutionDiagnosticCode::
                                             DuplicateDependencyProductBytes,
                                         "duplicates dependency product bytes") &&
               expectExecutionDiagnostic(execution,
                                         asharia::asset::AssetProductExecutionDiagnosticCode::
                                             DependencyProductBytesHashMismatch,
                                         "hash mismatch");
    }

    [[nodiscard]] bool smokeProductExecutionAcceptsDependencyProductBytes() {
        const std::filesystem::path root =
            smokeRoot("asharia-asset-pipeline-smoke-product-execution-dependency-product-bytes");
        if (root.empty() || !prepareWorkspace(root)) {
            return false;
        }

        const std::vector<asharia::asset::AssetImportSetting> settings{
            asharia::asset::AssetImportSetting{
                .key = "product.kind",
                .value = "dependency-product-bytes-smoke",
            },
        };
        const std::vector<std::uint8_t> sourceBytes = bytesFromText("source bytes");
        const std::vector<std::uint8_t> dependencyBytes =
            bytesFromText("generated slang product bytes");
        const asharia::asset::SourceAssetRecord source =
            makeDependencyProductBytesSmokeRecord(sourceBytes, settings);

        const asharia::asset::AssetProductExecutionResult execution =
            asharia::asset::executeAssetProducts(asharia::asset::AssetProductExecutionRequest{
                .plan = makeSingleProductExecutionPlan(source, settings),
                .existingManifest = {},
                .sourceBytes =
                    {
                        asharia::asset::AssetProductSourceBytes{
                            .sourcePath = source.sourcePath,
                            .bytes = sourceBytes,
                        },
                    },
                .dependencyProductBytes =
                    {
                        asharia::asset::AssetProductDependencyBytes{
                            .relativeProductPath = "generated/Unlit.ashader.product",
                            .productHash = smokeHashBytes(dependencyBytes),
                            .bytes = dependencyBytes,
                        },
                    },
                .productOutputRoot = root / "ProductCache",
                .productManifestOutputPath = root / "ProductCache" / "product-manifest.json",
            });

        return execution.succeeded() && execution.writtenProducts.size() == 1U &&
               execution.manifest.products.size() == 1U && execution.manifestWritten;
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

// The exhaustive catch boundary converts all failures to the smoke-test exit protocol.
// NOLINTNEXTLINE(bugprone-exception-escape)
int main() noexcept {
    try {
        const bool passed =
            smokeSourceScanValidAndDeterministic() && smokeSourceScanMissingMetadata() &&
            smokeSourceScanOrphanMetadata() && smokeSourceScanInvalidRoot() &&
            smokeSourceScanInvalidPrefix() && smokeScannedImportPlanningRequestsAndCacheHits() &&
            smokeScannedImportPlanningStopsOnScanDiagnostics() &&
            smokeScannedImportPlanningStopsOnDiscoveryDiagnostics() &&
            smokeScannedImportPlanningPlanDiagnostics() &&
            smokeProductPublicationCommitsManifestLast() &&
            smokeProductPublicationFakeEnforcesReadLimits() &&
            smokeEmptyProductPublicationIsNoOp() &&
            smokeProductPublicationPreservesManifestOnHandledFailures() &&
            smokeProductPublicationRejectsStagedMutation() &&
            smokeProductPublicationReportsCleanupAfterManifestCommit() &&
            smokeProductPublicationPreservesPartialOutcome() &&
            smokeProductPublicationPreflightsEndpoints() && smokeWindowsPublicationFileIdentity() &&
            smokeWindowsPublicationEndpointSemantics() &&
            smokeProductExecutionScopesPublicationDiagnostics() &&
            smokeProductPublicationRejectsUnownedStagingPath() &&
            smokeNativeProductPublicationRejectsRedirectedStagingRoot() &&
            smokeProductExecutionWritesDeterministicProducts() &&
            smokeProductBlobPrivateValidation() && smokeProductBlobReadLimits() &&
            smokeProductBlobReadDiagnostics() &&
            smokeProductCleanupFailurePreservesDiagnosticIdentity() &&
            smokeProductExecutionPreservesPublicationOutcome() &&
            smokeProductExecutionWritesPngTextureProduct() &&
            smokeProductExecutionPngTextureDiagnostics() &&
            smokeProductExecutionWritesAmatMaterialProduct() &&
            smokeProductExecutionAmatDiagnostics() &&
            smokeProductExecutionWritesAshaderShaderProduct() &&
            smokeProductExecutionAshaderDiagnostics() &&
            smokeProductExecutionWritesShaderCompileReflectionProduct() &&
            smokeProductExecutionShaderCompileReflectionDiagnostics() &&
            smokeProductExecutionShaderCompileReflectionCompilerDiagnostics() &&
            smokeProductExecutionShaderCompileReflectionInvalidEntryDiagnostics() &&
            smokeProductExecutionSourceBytesHashMismatch() &&
            smokeProductExecutionDependencyProductBytesDiagnostics() &&
            smokeProductExecutionAcceptsDependencyProductBytes() &&
            smokeImportPlanningCacheHitAndMiss() && smokeImportPlanningSourceChanged() &&
            smokeImportPlanningMetadataSourceHashDriftWarning() &&
            smokeImportPlanningSettingsChanged() && smokeAssetToolFingerprintDeterminism() &&
            smokeAssetToolFingerprintStreamLimits() &&
            smokeImportPlanningToolFingerprintBatchCache() &&
            smokeImportPlanningToolFingerprintFailureBatchCache() &&
            smokeAssetToolFingerprintStreamFailureContext() &&
            smokeImportPlanningToolFingerprintFailure() &&
            smokeImportPlanningShaderToolVersionChanged() && smokeImportPlanningMissingSnapshot() &&
            smokeImportPlanningDuplicateSource() && smokeImportPlanningDuplicateSnapshot() &&
            smokeImportPlanningInvalidTargetProfile() && smokeTextureImportContractRawRgba8() &&
            smokeTextureImportContractPng() && smokeTextureImportContractDiagnostics() &&
            smokeTextureImportProfiles() && smokeProductManifestRoundTrip() &&
            smokeProductManifestMalformedInput() && smokeProductManifestDuplicateField() &&
            smokeProductManifestMissingField() && smokeProductManifestUnknownField() &&
            smokeProductManifestDuplicateProductKey() &&
            smokeProductManifestDuplicateProductPath() &&
            smokeProductManifestInvalidProductPath() &&
            smokeProductManifestProductKeyHashMismatch() &&
            smokeSourceSnapshotValidAndDeterministic() && smokeSourceSnapshotContentChange() &&
            smokeSourceSnapshotMissingFile() && smokeSourceSnapshotDirectory() &&
            smokeSourceSnapshotInvalidSourcePath() && smokeSourceSnapshotDuplicateSourcePath() &&
            smokeDiscoveryValidAndDeterministic() && smokeMissingMetadata() &&
            smokeMalformedMetadata() && smokeSourcePathMismatch() && smokeDuplicateGuid() &&
            smokeDuplicateSourcePath() && smokeInvalidEntry() && smokeInvalidEntrySourcePath() &&
            smokeInvalidMetadataSourcePath();
        return passed ? EXIT_SUCCESS : EXIT_FAILURE;
    } catch (const std::exception& exception) {
        logFailure(exception.what());
        return EXIT_FAILURE;
    } catch (...) {
        logFailure("Asset pipeline smoke caught an unknown exception.");
        return EXIT_FAILURE;
    }
}
