#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "asharia/asset_core/asset_metadata.hpp"
#include "asharia/core/result.hpp"

namespace asharia::asset {

    inline constexpr std::string_view kTextureImportSettingsVersionSettingKey =
        "texture.settingsVersion";
    inline constexpr std::string_view kTextureImportWidthSettingKey = "texture.width";
    inline constexpr std::string_view kTextureImportHeightSettingKey = "texture.height";
    inline constexpr std::string_view kTextureImportFormatSettingKey = "texture.format";

    inline constexpr std::string_view kTextureImportFormatRgba8Unorm = "rgba8-unorm";
    inline constexpr std::string_view kTextureImportFormatRgba8Srgb = "rgba8-srgb";
    inline constexpr std::string_view kTextureImportRawRgba8Extension = ".rgba8";
    inline constexpr std::string_view kTextureImportPngExtension = ".png";
    inline constexpr std::uint32_t kTextureImportContractSettingsVersion = 1;

    enum class AssetTextureImportDiagnosticCode {
        InvalidRequest,
        UnsupportedSourceExtension,
        UnsupportedProfile,
        UnsupportedSettingsVersion,
        InvalidDimensions,
        UnsupportedFormat,
        PayloadSizeMismatch,
        DecodeFailed,
    };

    enum class AssetTextureImportFormat {
        Rgba8Unorm,
        Rgba8Srgb,
    };

    struct AssetTextureImporterDescriptor {
        std::string importerName;
        ImporterVersion importerVersion{};
        std::uint32_t settingsVersion{};
        std::vector<std::string> supportedSourceExtensions;
        std::vector<std::string> supportedProfiles;
        std::string productTypeName;

        [[nodiscard]] friend bool operator==(const AssetTextureImporterDescriptor&,
                                             const AssetTextureImporterDescriptor&) = default;
    };

    struct AssetTextureMipPayload {
        std::uint32_t level{};
        std::uint32_t width{};
        std::uint32_t height{};
        std::uint64_t byteOffset{};
        std::uint64_t byteSize{};

        [[nodiscard]] friend bool operator==(const AssetTextureMipPayload&,
                                             const AssetTextureMipPayload&) = default;
    };

    struct AssetTextureImportRequest {
        SourceAssetRecord source;
        std::vector<AssetImportSetting> settings;
        std::vector<std::uint8_t> sourceBytes;
        AssetTextureImporterDescriptor importer;

        [[nodiscard]] friend bool operator==(const AssetTextureImportRequest&,
                                             const AssetTextureImportRequest&) = default;
    };

    struct AssetTextureImportResult {
        SourceAssetRecord source;
        std::string sourceExtension;
        std::string importProfileName;
        std::uint32_t settingsVersion{};
        std::string productTypeName;
        AssetTextureImportFormat format{AssetTextureImportFormat::Rgba8Unorm};
        std::uint32_t width{};
        std::uint32_t height{};
        std::vector<AssetTextureMipPayload> mips;
        std::vector<std::uint8_t> payload;

        [[nodiscard]] friend bool operator==(const AssetTextureImportResult&,
                                             const AssetTextureImportResult&) = default;
    };

    [[nodiscard]] AssetTextureImporterDescriptor makeRawRgba8TextureImporterDescriptor();
    [[nodiscard]] AssetTextureImporterDescriptor makePngTextureImporterDescriptor();

    [[nodiscard]] const char*
    assetTextureImportDiagnosticCodeName(AssetTextureImportDiagnosticCode code) noexcept;

    [[nodiscard]] std::string_view
    assetTextureImportFormatName(AssetTextureImportFormat format) noexcept;

    [[nodiscard]] Result<AssetTextureImportResult>
    importTextureCpuPayload(const AssetTextureImportRequest& request);

} // namespace asharia::asset
