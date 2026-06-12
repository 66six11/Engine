#include "asharia/asset_pipeline/asset_texture_import.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstring>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "asharia/asset_pipeline/asset_texture_import_profile.hpp"
#include "asharia/core/error.hpp"

namespace asharia::asset {
    namespace {

        [[nodiscard]] std::string sourceLabel(const SourceAssetRecord& source) {
            return source.sourcePath.empty() ? std::string{"<unspecified-source>"}
                                             : source.sourcePath;
        }

        [[nodiscard]] Error textureImportError(AssetTextureImportDiagnosticCode code,
                                               const SourceAssetRecord& source,
                                               std::string message) {
            return Error{ErrorDomain::Asset, static_cast<int>(code),
                         "Texture import " + sourceLabel(source) + " " + std::move(message) + "."};
        }

        [[nodiscard]] const AssetImportSetting*
        findSetting(std::span<const AssetImportSetting> settings, std::string_view key) {
            const auto found = std::ranges::find_if(
                settings, [key](const AssetImportSetting& setting) { return setting.key == key; });
            return found == settings.end() ? nullptr : &*found;
        }

        struct TextureImportExtent {
            std::uint32_t width{};
            std::uint32_t height{};
        };

        struct StbiImageDeleter {
            void operator()(stbi_uc* pixels) const noexcept {
                stbi_image_free(pixels);
            }
        };

        using StbiImage = std::unique_ptr<stbi_uc, StbiImageDeleter>;

        [[nodiscard]] std::string lowerAscii(std::string_view value) {
            std::string text;
            text.reserve(value.size());
            for (const char character : value) {
                if (character >= 'A' && character <= 'Z') {
                    text.push_back(static_cast<char>(character - 'A' + 'a'));
                    continue;
                }
                text.push_back(character);
            }
            return text;
        }

        [[nodiscard]] std::string sourceExtension(std::string_view sourcePath) {
            const std::size_t slash = sourcePath.rfind('/');
            const std::size_t dot = sourcePath.rfind('.');
            if (dot == std::string_view::npos || (slash != std::string_view::npos && dot < slash) ||
                dot + 1U >= sourcePath.size()) {
                return {};
            }
            return lowerAscii(sourcePath.substr(dot));
        }

        [[nodiscard]] std::string joinSupportedValues(std::span<const std::string> values) {
            std::string text;
            for (std::size_t index = 0; index < values.size(); ++index) {
                if (index != 0U) {
                    text += ", ";
                }
                text += "\"";
                text += values[index];
                text += "\"";
            }
            return text.empty() ? std::string{"<none>"} : text;
        }

        [[nodiscard]] bool containsNormalized(std::span<const std::string> values,
                                              std::string_view value) {
            const std::string normalizedValue = lowerAscii(value);
            return std::ranges::any_of(values, [&normalizedValue](const std::string& item) {
                return lowerAscii(item) == normalizedValue;
            });
        }

        [[nodiscard]] bool containsProfile(std::span<const std::string> profiles,
                                           std::string_view profile) {
            return std::ranges::any_of(profiles, [profile](const std::string& item) {
                return normalizeTextureImportProfileName(item) == profile;
            });
        }

        [[nodiscard]] Result<std::uint32_t>
        parseUint32Setting(const AssetTextureImportRequest& request, std::string_view key,
                           AssetTextureImportDiagnosticCode code, std::string_view label) {
            const AssetImportSetting* setting = findSetting(request.settings, key);
            if (setting == nullptr || setting->value.empty()) {
                return std::unexpected{textureImportError(code, request.source,
                                                          "requires " + std::string{key} + " for " +
                                                              std::string{label})};
            }

            std::uint32_t value{};
            const char* const begin = setting->value.data();
            // std::from_chars consumes a contiguous pointer pair from std::string storage.
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
            const char* const end = begin + setting->value.size();
            const std::from_chars_result parsed = std::from_chars(begin, end, value);
            if (parsed.ec != std::errc{} || parsed.ptr != end || value == 0U) {
                return std::unexpected{textureImportError(
                    code, request.source, "requires a positive integer " + std::string{key})};
            }

            return value;
        }

        [[nodiscard]] Result<AssetTextureImportFormat>
        parseTextureFormat(const AssetTextureImportRequest& request) {
            const AssetImportSetting* setting =
                findSetting(request.settings, kTextureImportFormatSettingKey);
            const std::string value = setting == nullptr || setting->value.empty()
                                          ? std::string{kTextureImportFormatRgba8Unorm}
                                          : lowerAscii(setting->value);

            if (value == kTextureImportFormatRgba8Unorm) {
                return AssetTextureImportFormat::Rgba8Unorm;
            }
            if (value == kTextureImportFormatRgba8Srgb) {
                return AssetTextureImportFormat::Rgba8Srgb;
            }

            return std::unexpected{textureImportError(
                AssetTextureImportDiagnosticCode::UnsupportedFormat, request.source,
                "does not support texture format \"" + value + "\"")};
        }

        [[nodiscard]] Result<std::uint64_t>
        expectedRgba8Bytes(const AssetTextureImportRequest& request, TextureImportExtent extent) {
            constexpr std::uint64_t kBytesPerPixel = 4U;
            constexpr std::uint64_t kMax = std::numeric_limits<std::uint64_t>::max();
            const std::uint64_t width64 = extent.width;
            const std::uint64_t height64 = extent.height;
            if (height64 != 0U && width64 > kMax / height64) {
                return std::unexpected{textureImportError(
                    AssetTextureImportDiagnosticCode::InvalidDimensions, request.source,
                    "dimensions overflow the CPU texture payload size")};
            }

            const std::uint64_t pixels = width64 * height64;
            if (pixels > kMax / kBytesPerPixel) {
                return std::unexpected{textureImportError(
                    AssetTextureImportDiagnosticCode::InvalidDimensions, request.source,
                    "dimensions overflow the CPU texture payload size")};
            }

            return pixels * kBytesPerPixel;
        }

        [[nodiscard]] Result<std::size_t>
        expectedRgba8ByteCount(const AssetTextureImportRequest& request,
                               TextureImportExtent extent) {
            auto bytes = expectedRgba8Bytes(request, extent);
            if (!bytes) {
                return std::unexpected{std::move(bytes.error())};
            }

            if (*bytes > std::numeric_limits<std::size_t>::max()) {
                return std::unexpected{textureImportError(
                    AssetTextureImportDiagnosticCode::InvalidDimensions, request.source,
                    "dimensions overflow the CPU texture payload size")};
            }

            return static_cast<std::size_t>(*bytes);
        }

        [[nodiscard]] Result<std::uint32_t>
        requestedSettingsVersion(const AssetTextureImportRequest& request) {
            const AssetImportSetting* setting =
                findSetting(request.settings, kTextureImportSettingsVersionSettingKey);
            if (setting == nullptr || setting->value.empty()) {
                return request.importer.settingsVersion;
            }

            return parseUint32Setting(request, kTextureImportSettingsVersionSettingKey,
                                      AssetTextureImportDiagnosticCode::UnsupportedSettingsVersion,
                                      "texture settings version");
        }

        [[nodiscard]] Result<std::string>
        requestedTextureProfile(const AssetTextureImportRequest& request) {
            const AssetImportSetting* setting =
                findSetting(request.settings, kTextureImportProfileSettingKey);
            if (setting == nullptr || setting->value.empty()) {
                return std::unexpected{textureImportError(
                    AssetTextureImportDiagnosticCode::UnsupportedProfile, request.source,
                    "requires " + std::string{kTextureImportProfileSettingKey})};
            }

            std::string profile = normalizeTextureImportProfileName(setting->value);
            if (profile.empty() || !containsProfile(request.importer.supportedProfiles, profile)) {
                return std::unexpected{textureImportError(
                    AssetTextureImportDiagnosticCode::UnsupportedProfile, request.source,
                    "does not support texture profile \"" + setting->value +
                        "\"; supported profiles are " +
                        joinSupportedValues(request.importer.supportedProfiles))};
            }

            return profile;
        }

        [[nodiscard]] VoidResult
        validateTextureImportRequest(const AssetTextureImportRequest& request) {
            if (auto validSource = validateSourceAssetRecord(request.source); !validSource) {
                return std::unexpected{textureImportError(
                    AssetTextureImportDiagnosticCode::InvalidRequest, request.source,
                    "has an invalid source record: " + validSource.error().message)};
            }

            if (request.importer.importerName.empty() || !request.importer.importerVersion ||
                request.importer.settingsVersion == 0U ||
                request.importer.supportedSourceExtensions.empty() ||
                request.importer.supportedProfiles.empty() ||
                request.importer.productTypeName.empty()) {
                return std::unexpected{textureImportError(
                    AssetTextureImportDiagnosticCode::InvalidRequest, request.source,
                    "has an incomplete texture importer descriptor")};
            }

            return {};
        }

        [[nodiscard]] Result<std::vector<std::uint8_t>>
        decodePngRgba8Payload(const AssetTextureImportRequest& request,
                              TextureImportExtent& extent) {
            if (request.sourceBytes.empty()) {
                return std::unexpected{
                    textureImportError(AssetTextureImportDiagnosticCode::DecodeFailed,
                                       request.source, "could not decode PNG source bytes")};
            }

            if (request.sourceBytes.size() >
                static_cast<std::size_t>(std::numeric_limits<int>::max())) {
                return std::unexpected{
                    textureImportError(AssetTextureImportDiagnosticCode::InvalidRequest,
                                       request.source, "is too large for the PNG decoder input")};
            }

            int width = 0;
            int height = 0;
            int sourceChannels = 0;
            StbiImage decoded{stbi_load_from_memory(
                request.sourceBytes.data(), static_cast<int>(request.sourceBytes.size()), &width,
                &height, &sourceChannels, STBI_rgb_alpha)};
            if (!decoded) {
                return std::unexpected{
                    textureImportError(AssetTextureImportDiagnosticCode::DecodeFailed,
                                       request.source, "could not decode PNG source bytes")};
            }

            if (width <= 0 || height <= 0) {
                return std::unexpected{
                    textureImportError(AssetTextureImportDiagnosticCode::InvalidDimensions,
                                       request.source, "decoded non-positive PNG dimensions")};
            }
            if (sourceChannels <= 0) {
                return std::unexpected{textureImportError(
                    AssetTextureImportDiagnosticCode::DecodeFailed, request.source,
                    "decoded PNG without source channel metadata")};
            }

            extent = TextureImportExtent{
                .width = static_cast<std::uint32_t>(width),
                .height = static_cast<std::uint32_t>(height),
            };

            auto expectedBytes = expectedRgba8ByteCount(request, extent);
            if (!expectedBytes) {
                return std::unexpected{std::move(expectedBytes.error())};
            }

            std::vector<std::uint8_t> payload(*expectedBytes);
            std::memcpy(payload.data(), decoded.get(), payload.size());
            return payload;
        }

        [[nodiscard]] Result<AssetTextureImportResult>
        makeTextureImportResult(const AssetTextureImportRequest& request, std::string extension,
                                std::string importProfileName, std::uint32_t settingsVersion,
                                AssetTextureImportFormat format, TextureImportExtent extent,
                                std::vector<std::uint8_t> payload, std::string_view payloadLabel) {
            auto expectedBytes = expectedRgba8ByteCount(request, extent);
            if (!expectedBytes) {
                return std::unexpected{std::move(expectedBytes.error())};
            }

            if (payload.size() != *expectedBytes) {
                return std::unexpected{textureImportError(
                    AssetTextureImportDiagnosticCode::PayloadSizeMismatch, request.source,
                    "expected " + std::to_string(*expectedBytes) + " bytes for " +
                        std::string{payloadLabel} + " payload but received " +
                        std::to_string(payload.size()))};
            }

            return AssetTextureImportResult{
                .source = request.source,
                .sourceExtension = std::move(extension),
                .importProfileName = std::move(importProfileName),
                .settingsVersion = settingsVersion,
                .productTypeName = request.importer.productTypeName,
                .format = format,
                .width = extent.width,
                .height = extent.height,
                .mips =
                    {
                        AssetTextureMipPayload{
                            .level = 0U,
                            .width = extent.width,
                            .height = extent.height,
                            .byteOffset = 0U,
                            .byteSize = *expectedBytes,
                        },
                    },
                .payload = std::move(payload),
            };
        }

    } // namespace

    AssetTextureImporterDescriptor makeRawRgba8TextureImporterDescriptor() {
        return AssetTextureImporterDescriptor{
            .importerName = "com.asharia.importer.texture.raw-rgba8",
            .importerVersion = ImporterVersion{1},
            .settingsVersion = kTextureImportContractSettingsVersion,
            .supportedSourceExtensions = {std::string{kTextureImportRawRgba8Extension}},
            .supportedProfiles = {std::string{kTextureImportProfileTexture2D}},
            .productTypeName = std::string{kTextureRoleTexture2D},
        };
    }

    AssetTextureImporterDescriptor makePngTextureImporterDescriptor() {
        return AssetTextureImporterDescriptor{
            .importerName = "com.asharia.importer.texture.png",
            .importerVersion = ImporterVersion{1},
            .settingsVersion = kTextureImportContractSettingsVersion,
            .supportedSourceExtensions = {std::string{kTextureImportPngExtension}},
            .supportedProfiles = {std::string{kTextureImportProfileTexture2D}},
            .productTypeName = std::string{kTextureRoleTexture2D},
        };
    }

    const char*
    assetTextureImportDiagnosticCodeName(AssetTextureImportDiagnosticCode code) noexcept {
        switch (code) {
        case AssetTextureImportDiagnosticCode::InvalidRequest:
            return "invalid-request";
        case AssetTextureImportDiagnosticCode::UnsupportedSourceExtension:
            return "unsupported-source-extension";
        case AssetTextureImportDiagnosticCode::UnsupportedProfile:
            return "unsupported-profile";
        case AssetTextureImportDiagnosticCode::UnsupportedSettingsVersion:
            return "unsupported-settings-version";
        case AssetTextureImportDiagnosticCode::InvalidDimensions:
            return "invalid-dimensions";
        case AssetTextureImportDiagnosticCode::UnsupportedFormat:
            return "unsupported-format";
        case AssetTextureImportDiagnosticCode::PayloadSizeMismatch:
            return "payload-size-mismatch";
        case AssetTextureImportDiagnosticCode::DecodeFailed:
            return "decode-failed";
        }
        return "unknown";
    }

    std::string_view assetTextureImportFormatName(AssetTextureImportFormat format) noexcept {
        switch (format) {
        case AssetTextureImportFormat::Rgba8Unorm:
            return kTextureImportFormatRgba8Unorm;
        case AssetTextureImportFormat::Rgba8Srgb:
            return kTextureImportFormatRgba8Srgb;
        }
        return "unknown";
    }

    Result<AssetTextureImportResult>
    importTextureCpuPayload(const AssetTextureImportRequest& request) {
        if (auto validRequest = validateTextureImportRequest(request); !validRequest) {
            return std::unexpected{std::move(validRequest.error())};
        }

        const std::string extension = sourceExtension(request.source.sourcePath);
        if (extension.empty() ||
            !containsNormalized(request.importer.supportedSourceExtensions, extension)) {
            return std::unexpected{textureImportError(
                AssetTextureImportDiagnosticCode::UnsupportedSourceExtension, request.source,
                "does not support source extension \"" +
                    (extension.empty() ? std::string{"<none>"} : extension) +
                    "\"; supported extensions are " +
                    joinSupportedValues(request.importer.supportedSourceExtensions))};
        }

        auto profile = requestedTextureProfile(request);
        if (!profile) {
            return std::unexpected{std::move(profile.error())};
        }

        auto settingsVersion = requestedSettingsVersion(request);
        if (!settingsVersion) {
            return std::unexpected{std::move(settingsVersion.error())};
        }
        if (*settingsVersion != request.importer.settingsVersion) {
            return std::unexpected{textureImportError(
                AssetTextureImportDiagnosticCode::UnsupportedSettingsVersion, request.source,
                "does not support texture settings version \"" + std::to_string(*settingsVersion) +
                    "\"; supported version is \"" +
                    std::to_string(request.importer.settingsVersion) + "\"")};
        }

        auto format = parseTextureFormat(request);
        if (!format) {
            return std::unexpected{std::move(format.error())};
        }

        if (extension == kTextureImportPngExtension) {
            TextureImportExtent extent{};
            auto payload = decodePngRgba8Payload(request, extent);
            if (!payload) {
                return std::unexpected{std::move(payload.error())};
            }

            return makeTextureImportResult(request, extension, std::move(*profile),
                                           *settingsVersion, *format, extent, std::move(*payload),
                                           "decoded PNG RGBA8");
        }

        auto width = parseUint32Setting(request, kTextureImportWidthSettingKey,
                                        AssetTextureImportDiagnosticCode::InvalidDimensions,
                                        "texture width");
        if (!width) {
            return std::unexpected{std::move(width.error())};
        }

        auto height = parseUint32Setting(request, kTextureImportHeightSettingKey,
                                         AssetTextureImportDiagnosticCode::InvalidDimensions,
                                         "texture height");
        if (!height) {
            return std::unexpected{std::move(height.error())};
        }

        return makeTextureImportResult(request, extension, std::move(*profile), *settingsVersion,
                                       *format,
                                       TextureImportExtent{.width = *width, .height = *height},
                                       request.sourceBytes, "raw RGBA8");
    }

} // namespace asharia::asset
