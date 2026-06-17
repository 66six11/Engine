#include "asharia/asset_pipeline/asset_product_blob.hpp"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <ios>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "asharia/asset_core/asset_guid.hpp"
#include "asharia/core/error.hpp"
#include "asharia/material_instance/amat_io.hpp"

namespace asharia::asset {
    namespace {

        constexpr std::uint64_t kFnv1a64Offset = 14695981039346656037ULL;
        constexpr std::uint64_t kFnv1a64Prime = 1099511628211ULL;
        constexpr std::string_view kTextureProductSchema = "com.asharia.asset.texture2d-product.v1";
        constexpr std::string_view kMaterialInstanceProductSchema =
            "com.asharia.asset.material-instance-product.v1";
        constexpr std::string_view kShaderAuthoringProductSchema =
            "com.asharia.asset.shader-authoring-product.v1";

        [[nodiscard]] constexpr std::uint64_t hashByte(std::uint64_t hash,
                                                       std::uint8_t byte) noexcept {
            hash ^= byte;
            hash *= kFnv1a64Prime;
            return hash;
        }

        [[nodiscard]] std::uint64_t hashBytes(std::span<const std::uint8_t> bytes) noexcept {
            std::uint64_t hash = kFnv1a64Offset;
            for (const std::uint8_t byte : bytes) {
                hash = hashByte(hash, byte);
            }
            return hash;
        }

        [[nodiscard]] Error blobError(AssetProductBlobDiagnosticCode code,
                                      std::string relativeProductPath, std::string message) {
            const std::string product = relativeProductPath.empty()
                                            ? std::string{"<unspecified-product>"}
                                            : std::move(relativeProductPath);
            return Error{ErrorDomain::Asset, static_cast<int>(code),
                         "Asset product blob " + product + " " + std::move(message) + "."};
        }

        [[nodiscard]] Result<std::vector<std::uint8_t>>
        readProductFileBytes(const AssetProductBlobReadRequest& request) {
            if (request.productFilePath.empty()) {
                return std::unexpected{blobError(AssetProductBlobDiagnosticCode::MissingProduct,
                                                 request.relativeProductPath,
                                                 "has no product file path")};
            }

            std::ifstream file(request.productFilePath, std::ios::binary);
            if (!file) {
                return std::unexpected{blobError(AssetProductBlobDiagnosticCode::MissingProduct,
                                                 request.relativeProductPath,
                                                 "is missing from the product cache")};
            }

            std::vector<std::uint8_t> bytes;
            char byte{};
            while (file.get(byte)) {
                bytes.push_back(static_cast<std::uint8_t>(static_cast<unsigned char>(byte)));
            }
            if (!file.eof()) {
                return std::unexpected{blobError(AssetProductBlobDiagnosticCode::ProductReadFailed,
                                                 request.relativeProductPath,
                                                 "could not be read completely")};
            }

            return bytes;
        }

        struct HeaderLookupRequest {
            std::string_view header;
            std::string_view key;
        };

        [[nodiscard]] std::optional<std::string_view>
        findHeaderValue(const HeaderLookupRequest& request) {
            std::size_t lineBegin = 0;
            while (lineBegin <= request.header.size()) {
                std::size_t lineEnd = request.header.find('\n', lineBegin);
                if (lineEnd == std::string_view::npos) {
                    lineEnd = request.header.size();
                }

                const std::string_view line = request.header.substr(lineBegin, lineEnd - lineBegin);
                const std::size_t equals = line.find('=');
                if (equals != std::string_view::npos && line.substr(0, equals) == request.key) {
                    return line.substr(equals + 1);
                }

                if (lineEnd == request.header.size()) {
                    break;
                }
                lineBegin = lineEnd + 1;
            }

            return std::nullopt;
        }

        [[nodiscard]] Result<std::string> requireStringField(std::string_view header,
                                                             std::string_view key,
                                                             std::string_view relativeProductPath) {
            const std::optional<std::string_view> value = findHeaderValue({
                .header = header,
                .key = key,
            });
            if (!value || value->empty()) {
                return std::unexpected{blobError(AssetProductBlobDiagnosticCode::InvalidProductBlob,
                                                 std::string{relativeProductPath},
                                                 "is missing field '" + std::string{key} + "'")};
            }

            return std::string{*value};
        }

        [[nodiscard]] Result<std::string>
        requirePresentStringField(std::string_view header, std::string_view key,
                                  std::string_view relativeProductPath) {
            const std::optional<std::string_view> value = findHeaderValue({
                .header = header,
                .key = key,
            });
            if (!value) {
                return std::unexpected{blobError(AssetProductBlobDiagnosticCode::InvalidProductBlob,
                                                 std::string{relativeProductPath},
                                                 "is missing field '" + std::string{key} + "'")};
            }

            return std::string{*value};
        }

        [[nodiscard]] Result<std::uint64_t>
        requireUint64Field(std::string_view header, std::string_view key,
                           std::string_view relativeProductPath) {
            const std::optional<std::string_view> value = findHeaderValue({
                .header = header,
                .key = key,
            });
            if (!value || value->empty()) {
                return std::unexpected{blobError(AssetProductBlobDiagnosticCode::InvalidProductBlob,
                                                 std::string{relativeProductPath},
                                                 "is missing field '" + std::string{key} + "'")};
            }

            std::uint64_t parsed{};
            const char* begin = value->data();
            const char* end = value->data() + value->size();
            const auto result = std::from_chars(begin, end, parsed);
            if (result.ec != std::errc{} || result.ptr != end) {
                return std::unexpected{
                    blobError(AssetProductBlobDiagnosticCode::InvalidProductBlob,
                              std::string{relativeProductPath},
                              "has invalid integer field '" + std::string{key} + "'")};
            }

            return parsed;
        }

        [[nodiscard]] Result<std::uint32_t>
        requireUint32Field(std::string_view header, std::string_view key,
                           std::string_view relativeProductPath) {
            auto value = requireUint64Field(header, key, relativeProductPath);
            if (!value) {
                return std::unexpected{std::move(value.error())};
            }
            if (*value > UINT32_MAX) {
                return std::unexpected{
                    blobError(AssetProductBlobDiagnosticCode::InvalidProductBlob,
                              std::string{relativeProductPath},
                              "has out-of-range integer field '" + std::string{key} + "'")};
            }
            return static_cast<std::uint32_t>(*value);
        }

        [[nodiscard]] Result<bool> requireBoolField(std::string_view header, std::string_view key,
                                                    std::string_view relativeProductPath) {
            auto value = requireStringField(header, key, relativeProductPath);
            if (!value) {
                return std::unexpected{std::move(value.error())};
            }
            if (*value == "true") {
                return true;
            }
            if (*value == "false") {
                return false;
            }

            return std::unexpected{
                blobError(AssetProductBlobDiagnosticCode::InvalidProductBlob,
                          std::string{relativeProductPath},
                          "has invalid boolean field '" + std::string{key} + "'")};
        }

        [[nodiscard]] Result<std::uint64_t>
        requireHexUint64Field(std::string_view header, std::string_view key,
                              std::string_view relativeProductPath) {
            const std::optional<std::string_view> value = findHeaderValue({
                .header = header,
                .key = key,
            });
            if (!value || value->empty()) {
                return std::unexpected{blobError(AssetProductBlobDiagnosticCode::InvalidProductBlob,
                                                 std::string{relativeProductPath},
                                                 "is missing field '" + std::string{key} + "'")};
            }

            std::uint64_t parsed{};
            const char* begin = value->data();
            const char* end = value->data() + value->size();
            const auto result = std::from_chars(begin, end, parsed, 16);
            if (result.ec != std::errc{} || result.ptr != end) {
                return std::unexpected{
                    blobError(AssetProductBlobDiagnosticCode::InvalidProductBlob,
                              std::string{relativeProductPath},
                              "has invalid hex field '" + std::string{key} + "'")};
            }

            return parsed;
        }

        [[nodiscard]] Result<AssetGuid>
        requireAssetGuidField(std::string_view header, std::string_view key,
                              std::string_view relativeProductPath) {
            auto value = requireStringField(header, key, relativeProductPath);
            if (!value) {
                return std::unexpected{std::move(value.error())};
            }

            auto guid = parseAssetGuid(*value);
            if (!guid) {
                return std::unexpected{blobError(AssetProductBlobDiagnosticCode::InvalidProductBlob,
                                                 std::string{relativeProductPath},
                                                 "has invalid GUID field '" + std::string{key} +
                                                     "': " + guid.error().message)};
            }
            return *guid;
        }

        [[nodiscard]] Result<AssetTextureImportFormat>
        requireTextureFormatField(std::string_view header, std::string_view relativeProductPath) {
            auto value = requireStringField(header, "format", relativeProductPath);
            if (!value) {
                return std::unexpected{std::move(value.error())};
            }
            if (*value == kTextureImportFormatRgba8Unorm) {
                return AssetTextureImportFormat::Rgba8Unorm;
            }
            if (*value == kTextureImportFormatRgba8Srgb) {
                return AssetTextureImportFormat::Rgba8Srgb;
            }

            return std::unexpected{blobError(AssetProductBlobDiagnosticCode::InvalidProductBlob,
                                             std::string{relativeProductPath},
                                             "has unsupported texture format '" + *value + "'")};
        }

        [[nodiscard]] Result<void> validateTextureSchema(std::string_view header,
                                                         std::string_view relativeProductPath) {
            auto schema = requireStringField(header, "schema", relativeProductPath);
            if (!schema) {
                return std::unexpected{std::move(schema.error())};
            }
            if (*schema != kTextureProductSchema) {
                return std::unexpected{blobError(AssetProductBlobDiagnosticCode::InvalidProductBlob,
                                                 std::string{relativeProductPath},
                                                 "has unsupported schema '" + *schema + "'")};
            }

            return {};
        }

        [[nodiscard]] Result<void>
        validateMaterialInstanceSchema(std::string_view header,
                                       std::string_view relativeProductPath) {
            auto schema = requireStringField(header, "schema", relativeProductPath);
            if (!schema) {
                return std::unexpected{std::move(schema.error())};
            }
            if (*schema != kMaterialInstanceProductSchema) {
                return std::unexpected{blobError(AssetProductBlobDiagnosticCode::InvalidProductBlob,
                                                 std::string{relativeProductPath},
                                                 "has unsupported schema '" + *schema + "'")};
            }

            return {};
        }

        [[nodiscard]] Result<void>
        validateShaderAuthoringSchema(std::string_view header,
                                      std::string_view relativeProductPath) {
            auto schema = requireStringField(header, "schema", relativeProductPath);
            if (!schema) {
                return std::unexpected{std::move(schema.error())};
            }
            if (*schema != kShaderAuthoringProductSchema) {
                return std::unexpected{blobError(AssetProductBlobDiagnosticCode::InvalidProductBlob,
                                                 std::string{relativeProductPath},
                                                 "has unsupported schema '" + *schema + "'")};
            }

            return {};
        }

        struct TextureProductHeaderFields {
            std::string productTypeName;
            std::string importProfileName;
            std::uint32_t settingsVersion{};
            AssetTextureImportFormat format{AssetTextureImportFormat::Rgba8Unorm};
            std::uint32_t width{};
            std::uint32_t height{};
            std::uint64_t mipCount{};
            std::uint64_t payloadSize{};
            std::uint64_t payloadHash{};
        };

        struct TextureProductMipParseRequest {
            std::string_view header;
            std::uint64_t mipCount{};
            std::uint64_t payloadSize{};
            std::string_view relativeProductPath;
        };

        [[nodiscard]] Result<TextureProductHeaderFields>
        parseTextureProductHeaderFields(std::string_view header,
                                        std::string_view relativeProductPath) {
            auto validSchema = validateTextureSchema(header, relativeProductPath);
            if (!validSchema) {
                return std::unexpected{std::move(validSchema.error())};
            }

            auto productType = requireStringField(header, "productType", relativeProductPath);
            auto profile = requireStringField(header, "importProfile", relativeProductPath);
            auto settingsVersion =
                requireUint32Field(header, "settingsVersion", relativeProductPath);
            auto format = requireTextureFormatField(header, relativeProductPath);
            auto width = requireUint32Field(header, "width", relativeProductPath);
            auto height = requireUint32Field(header, "height", relativeProductPath);
            auto mipCount = requireUint64Field(header, "mip.count", relativeProductPath);
            auto payloadSize = requireUint64Field(header, "payload.size", relativeProductPath);
            auto payloadHash = requireHexUint64Field(header, "payloadHash", relativeProductPath);
            if (!productType) {
                return std::unexpected{std::move(productType.error())};
            }
            if (!profile) {
                return std::unexpected{std::move(profile.error())};
            }
            if (!settingsVersion) {
                return std::unexpected{std::move(settingsVersion.error())};
            }
            if (!format) {
                return std::unexpected{std::move(format.error())};
            }
            if (!width) {
                return std::unexpected{std::move(width.error())};
            }
            if (!height) {
                return std::unexpected{std::move(height.error())};
            }
            if (!mipCount) {
                return std::unexpected{std::move(mipCount.error())};
            }
            if (!payloadSize) {
                return std::unexpected{std::move(payloadSize.error())};
            }
            if (!payloadHash) {
                return std::unexpected{std::move(payloadHash.error())};
            }
            if (*mipCount == 0) {
                return std::unexpected{blobError(AssetProductBlobDiagnosticCode::InvalidProductBlob,
                                                 std::string{relativeProductPath},
                                                 "has no mip payload records")};
            }

            return TextureProductHeaderFields{
                .productTypeName = std::move(*productType),
                .importProfileName = std::move(*profile),
                .settingsVersion = *settingsVersion,
                .format = *format,
                .width = *width,
                .height = *height,
                .mipCount = *mipCount,
                .payloadSize = *payloadSize,
                .payloadHash = *payloadHash,
            };
        }

        [[nodiscard]] Result<std::vector<AssetTextureMipPayload>>
        parseTextureProductMips(const TextureProductMipParseRequest& request) {
            std::vector<AssetTextureMipPayload> mips;
            mips.reserve(static_cast<std::size_t>(request.mipCount));
            for (std::uint64_t mipIndex = 0; mipIndex < request.mipCount; ++mipIndex) {
                const std::string prefix = "mip." + std::to_string(mipIndex) + ".";
                auto level = requireUint32Field(request.header, prefix + "level",
                                                request.relativeProductPath);
                auto mipWidth = requireUint32Field(request.header, prefix + "width",
                                                   request.relativeProductPath);
                auto mipHeight = requireUint32Field(request.header, prefix + "height",
                                                    request.relativeProductPath);
                auto byteOffset = requireUint64Field(request.header, prefix + "byteOffset",
                                                     request.relativeProductPath);
                auto byteSize = requireUint64Field(request.header, prefix + "byteSize",
                                                   request.relativeProductPath);
                if (!level) {
                    return std::unexpected{std::move(level.error())};
                }
                if (!mipWidth) {
                    return std::unexpected{std::move(mipWidth.error())};
                }
                if (!mipHeight) {
                    return std::unexpected{std::move(mipHeight.error())};
                }
                if (!byteOffset) {
                    return std::unexpected{std::move(byteOffset.error())};
                }
                if (!byteSize) {
                    return std::unexpected{std::move(byteSize.error())};
                }
                if (*byteOffset > request.payloadSize ||
                    *byteSize > request.payloadSize - *byteOffset) {
                    return std::unexpected{
                        blobError(AssetProductBlobDiagnosticCode::InvalidProductBlob,
                                  std::string{request.relativeProductPath},
                                  "has an out-of-range mip payload record")};
                }

                mips.push_back(AssetTextureMipPayload{
                    .level = *level,
                    .width = *mipWidth,
                    .height = *mipHeight,
                    .byteOffset = *byteOffset,
                    .byteSize = *byteSize,
                });
            }
            return mips;
        }

        struct MaterialInstanceProductHeaderFields {
            AssetGuid materialTypeAssetGuid{};
            std::string stableTypeId;
            std::uint64_t expectedTypeHash{};
            std::uint64_t lastCookedSignatureHash{};
            std::uint64_t amatSize{};
            std::uint64_t amatHash{};
        };

        [[nodiscard]] Result<MaterialInstanceProductHeaderFields>
        parseMaterialInstanceProductHeaderFields(std::string_view header,
                                                 std::string_view relativeProductPath) {
            auto validSchema = validateMaterialInstanceSchema(header, relativeProductPath);
            if (!validSchema) {
                return std::unexpected{std::move(validSchema.error())};
            }

            auto materialTypeAssetGuid =
                requireAssetGuidField(header, "materialType.assetGuid", relativeProductPath);
            auto stableTypeId =
                requireStringField(header, "materialType.stableTypeId", relativeProductPath);
            auto expectedTypeHash =
                requireHexUint64Field(header, "materialType.expectedTypeHash", relativeProductPath);
            auto lastCookedSignatureHash = requireHexUint64Field(
                header, "import.lastCookedSignatureHash", relativeProductPath);
            auto amatSize = requireUint64Field(header, "amat.size", relativeProductPath);
            auto amatHash = requireHexUint64Field(header, "amatHash", relativeProductPath);
            if (!materialTypeAssetGuid) {
                return std::unexpected{std::move(materialTypeAssetGuid.error())};
            }
            if (!stableTypeId) {
                return std::unexpected{std::move(stableTypeId.error())};
            }
            if (!expectedTypeHash) {
                return std::unexpected{std::move(expectedTypeHash.error())};
            }
            if (!lastCookedSignatureHash) {
                return std::unexpected{std::move(lastCookedSignatureHash.error())};
            }
            if (!amatSize) {
                return std::unexpected{std::move(amatSize.error())};
            }
            if (!amatHash) {
                return std::unexpected{std::move(amatHash.error())};
            }
            if (stableTypeId->empty()) {
                return std::unexpected{blobError(AssetProductBlobDiagnosticCode::InvalidProductBlob,
                                                 std::string{relativeProductPath},
                                                 "has an empty material type id")};
            }

            return MaterialInstanceProductHeaderFields{
                .materialTypeAssetGuid = *materialTypeAssetGuid,
                .stableTypeId = std::move(*stableTypeId),
                .expectedTypeHash = *expectedTypeHash,
                .lastCookedSignatureHash = *lastCookedSignatureHash,
                .amatSize = *amatSize,
                .amatHash = *amatHash,
            };
        }

        struct ShaderAuthoringProductHeaderFields {
            std::string sourcePath;
            std::string stableTypeId;
            std::uint32_t schemaVersion{};
            std::uint64_t propertyCount{};
            std::uint64_t passCount{};
            std::uint64_t bindingCount{};
            std::uint64_t entryCount{};
            std::uint64_t generatedSlangSize{};
            std::uint64_t generatedSlangHash{};
        };

        [[nodiscard]] Result<ShaderAuthoringProductHeaderFields>
        parseShaderAuthoringProductHeaderFields(std::string_view header,
                                                std::string_view relativeProductPath) {
            auto validSchema = validateShaderAuthoringSchema(header, relativeProductPath);
            if (!validSchema) {
                return std::unexpected{std::move(validSchema.error())};
            }

            auto sourcePath = requireStringField(header, "sourcePath", relativeProductPath);
            auto stableTypeId =
                requireStringField(header, "shader.stableTypeId", relativeProductPath);
            auto schemaVersion =
                requireUint32Field(header, "ashader.schemaVersion", relativeProductPath);
            auto propertyCount = requireUint64Field(header, "property.count", relativeProductPath);
            auto passCount = requireUint64Field(header, "pass.count", relativeProductPath);
            auto bindingCount = requireUint64Field(header, "binding.count", relativeProductPath);
            auto entryCount = requireUint64Field(header, "entry.count", relativeProductPath);
            auto generatedSlangSize =
                requireUint64Field(header, "generatedSlang.size", relativeProductPath);
            auto generatedSlangHash =
                requireHexUint64Field(header, "generatedSlangHash", relativeProductPath);
            if (!sourcePath) {
                return std::unexpected{std::move(sourcePath.error())};
            }
            if (!stableTypeId) {
                return std::unexpected{std::move(stableTypeId.error())};
            }
            if (!schemaVersion) {
                return std::unexpected{std::move(schemaVersion.error())};
            }
            if (!propertyCount) {
                return std::unexpected{std::move(propertyCount.error())};
            }
            if (!passCount) {
                return std::unexpected{std::move(passCount.error())};
            }
            if (!bindingCount) {
                return std::unexpected{std::move(bindingCount.error())};
            }
            if (!entryCount) {
                return std::unexpected{std::move(entryCount.error())};
            }
            if (!generatedSlangSize) {
                return std::unexpected{std::move(generatedSlangSize.error())};
            }
            if (!generatedSlangHash) {
                return std::unexpected{std::move(generatedSlangHash.error())};
            }
            if (stableTypeId->empty()) {
                return std::unexpected{blobError(AssetProductBlobDiagnosticCode::InvalidProductBlob,
                                                 std::string{relativeProductPath},
                                                 "has an empty shader type id")};
            }
            if (*schemaVersion != 2U) {
                return std::unexpected{blobError(AssetProductBlobDiagnosticCode::InvalidProductBlob,
                                                 std::string{relativeProductPath},
                                                 "has unsupported .ashader schema version")};
            }
            if (*passCount == 0 || *entryCount == 0) {
                return std::unexpected{blobError(AssetProductBlobDiagnosticCode::InvalidProductBlob,
                                                 std::string{relativeProductPath},
                                                 "has no generated Slang entry records")};
            }

            return ShaderAuthoringProductHeaderFields{
                .sourcePath = std::move(*sourcePath),
                .stableTypeId = std::move(*stableTypeId),
                .schemaVersion = *schemaVersion,
                .propertyCount = *propertyCount,
                .passCount = *passCount,
                .bindingCount = *bindingCount,
                .entryCount = *entryCount,
                .generatedSlangSize = *generatedSlangSize,
                .generatedSlangHash = *generatedSlangHash,
            };
        }

        [[nodiscard]] Result<std::vector<AssetShaderAuthoringProductProperty>>
        parseShaderAuthoringProperties(std::string_view header, std::uint64_t propertyCount,
                                       std::string_view relativeProductPath) {
            if (propertyCount > SIZE_MAX) {
                return std::unexpected{blobError(AssetProductBlobDiagnosticCode::InvalidProductBlob,
                                                 std::string{relativeProductPath},
                                                 "has too many property records")};
            }

            std::vector<AssetShaderAuthoringProductProperty> properties;
            properties.reserve(static_cast<std::size_t>(propertyCount));
            for (std::uint64_t index = 0; index < propertyCount; ++index) {
                const std::string prefix = "property." + std::to_string(index) + ".";
                auto name = requireStringField(header, prefix + "name", relativeProductPath);
                auto type = requireStringField(header, prefix + "type", relativeProductPath);
                auto defaultText =
                    requirePresentStringField(header, prefix + "default", relativeProductPath);
                if (!name) {
                    return std::unexpected{std::move(name.error())};
                }
                if (!type) {
                    return std::unexpected{std::move(type.error())};
                }
                if (!defaultText) {
                    return std::unexpected{std::move(defaultText.error())};
                }
                properties.push_back(AssetShaderAuthoringProductProperty{
                    .name = std::move(*name),
                    .typeName = std::move(*type),
                    .defaultText = std::move(*defaultText),
                });
            }
            return properties;
        }

        [[nodiscard]] Result<std::vector<AssetShaderAuthoringProductPass>>
        parseShaderAuthoringPasses(std::string_view header, std::uint64_t passCount,
                                   std::string_view relativeProductPath) {
            if (passCount > SIZE_MAX) {
                return std::unexpected{blobError(AssetProductBlobDiagnosticCode::InvalidProductBlob,
                                                 std::string{relativeProductPath},
                                                 "has too many pass records")};
            }

            std::vector<AssetShaderAuthoringProductPass> passes;
            passes.reserve(static_cast<std::size_t>(passCount));
            for (std::uint64_t index = 0; index < passCount; ++index) {
                const std::string prefix = "pass." + std::to_string(index) + ".";
                auto name = requireStringField(header, prefix + "name", relativeProductPath);
                auto tag = requirePresentStringField(header, prefix + "tag", relativeProductPath);
                auto vertex =
                    requirePresentStringField(header, prefix + "vertex", relativeProductPath);
                auto fragment =
                    requirePresentStringField(header, prefix + "fragment", relativeProductPath);
                auto compute =
                    requirePresentStringField(header, prefix + "compute", relativeProductPath);
                if (!name) {
                    return std::unexpected{std::move(name.error())};
                }
                if (!tag) {
                    return std::unexpected{std::move(tag.error())};
                }
                if (!vertex) {
                    return std::unexpected{std::move(vertex.error())};
                }
                if (!fragment) {
                    return std::unexpected{std::move(fragment.error())};
                }
                if (!compute) {
                    return std::unexpected{std::move(compute.error())};
                }
                passes.push_back(AssetShaderAuthoringProductPass{
                    .name = std::move(*name),
                    .tag = std::move(*tag),
                    .vertexEntry = std::move(*vertex),
                    .fragmentEntry = std::move(*fragment),
                    .computeEntry = std::move(*compute),
                });
            }
            return passes;
        }

        [[nodiscard]] Result<std::vector<AssetShaderAuthoringProductBinding>>
        parseShaderAuthoringBindings(std::string_view header, std::uint64_t bindingCount,
                                     std::string_view relativeProductPath) {
            if (bindingCount > SIZE_MAX) {
                return std::unexpected{blobError(AssetProductBlobDiagnosticCode::InvalidProductBlob,
                                                 std::string{relativeProductPath},
                                                 "has too many binding records")};
            }

            std::vector<AssetShaderAuthoringProductBinding> bindings;
            bindings.reserve(static_cast<std::size_t>(bindingCount));
            for (std::uint64_t index = 0; index < bindingCount; ++index) {
                const std::string prefix = "binding." + std::to_string(index) + ".";
                auto name = requireStringField(header, prefix + "name", relativeProductPath);
                auto type = requireStringField(header, prefix + "type", relativeProductPath);
                auto set = requireUint32Field(header, prefix + "set", relativeProductPath);
                auto binding = requireUint32Field(header, prefix + "binding", relativeProductPath);
                auto inMaterialParameterBlock = requireBoolField(
                    header, prefix + "inMaterialParameterBlock", relativeProductPath);
                if (!name) {
                    return std::unexpected{std::move(name.error())};
                }
                if (!type) {
                    return std::unexpected{std::move(type.error())};
                }
                if (!set) {
                    return std::unexpected{std::move(set.error())};
                }
                if (!binding) {
                    return std::unexpected{std::move(binding.error())};
                }
                if (!inMaterialParameterBlock) {
                    return std::unexpected{std::move(inMaterialParameterBlock.error())};
                }
                bindings.push_back(AssetShaderAuthoringProductBinding{
                    .name = std::move(*name),
                    .typeName = std::move(*type),
                    .set = *set,
                    .binding = *binding,
                    .inMaterialParameterBlock = *inMaterialParameterBlock,
                });
            }
            return bindings;
        }

        [[nodiscard]] Result<std::vector<AssetShaderAuthoringProductEntry>>
        parseShaderAuthoringEntries(std::string_view header, std::uint64_t entryCount,
                                    std::string_view relativeProductPath) {
            if (entryCount > SIZE_MAX) {
                return std::unexpected{blobError(AssetProductBlobDiagnosticCode::InvalidProductBlob,
                                                 std::string{relativeProductPath},
                                                 "has too many entry records")};
            }

            std::vector<AssetShaderAuthoringProductEntry> entries;
            entries.reserve(static_cast<std::size_t>(entryCount));
            for (std::uint64_t index = 0; index < entryCount; ++index) {
                const std::string prefix = "entry." + std::to_string(index) + ".";
                auto passName =
                    requireStringField(header, prefix + "passName", relativeProductPath);
                auto stage = requireStringField(header, prefix + "stage", relativeProductPath);
                auto sourceEntry =
                    requireStringField(header, prefix + "sourceEntry", relativeProductPath);
                auto compileEntry =
                    requireStringField(header, prefix + "compileEntry", relativeProductPath);
                auto wrapper =
                    requireStringField(header, prefix + "generatedWrapper", relativeProductPath);
                if (!passName) {
                    return std::unexpected{std::move(passName.error())};
                }
                if (!stage) {
                    return std::unexpected{std::move(stage.error())};
                }
                if (!sourceEntry) {
                    return std::unexpected{std::move(sourceEntry.error())};
                }
                if (!compileEntry) {
                    return std::unexpected{std::move(compileEntry.error())};
                }
                if (!wrapper) {
                    return std::unexpected{std::move(wrapper.error())};
                }
                entries.push_back(AssetShaderAuthoringProductEntry{
                    .passName = std::move(*passName),
                    .stage = std::move(*stage),
                    .sourceEntryName = std::move(*sourceEntry),
                    .compileEntryName = std::move(*compileEntry),
                    .generatedWrapperName = std::move(*wrapper),
                });
            }
            return entries;
        }

    } // namespace

    Result<AssetProductBlobPayload>
    readPlaceholderProductSourceBytes(const AssetProductBlobReadRequest& request) {
        auto bytes = readProductFileBytes(request);
        if (!bytes) {
            return std::unexpected{std::move(bytes.error())};
        }

        return readPlaceholderProductSourceBytes(
            std::span<const std::uint8_t>{bytes->data(), bytes->size()},
            request.relativeProductPath);
    }

    Result<AssetProductBlobPayload>
    readPlaceholderProductSourceBytes(std::span<const std::uint8_t> productBytes,
                                      std::string_view relativeProductPath) {
        if (productBytes.empty()) {
            return std::unexpected{blobError(AssetProductBlobDiagnosticCode::InvalidProductBlob,
                                             std::string{relativeProductPath}, "is empty")};
        }

        std::string text;
        text.reserve(productBytes.size());
        for (const std::uint8_t byte : productBytes) {
            text.push_back(static_cast<char>(byte));
        }
        constexpr std::string_view kBegin = "sourceBytes.begin\n";
        constexpr std::string_view kEnd = "\nsourceBytes.end\n";

        const std::size_t markerOffset = text.find(kBegin);
        if (markerOffset == std::string_view::npos) {
            return std::unexpected{blobError(AssetProductBlobDiagnosticCode::MissingPayload,
                                             std::string{relativeProductPath},
                                             "does not contain sourceBytes.begin")};
        }

        const std::size_t payloadBegin = markerOffset + kBegin.size();
        const std::string headerText = text.substr(0, markerOffset);
        auto sourceBytesSize =
            requireUint64Field(headerText, "sourceBytes.size", relativeProductPath);
        auto sourceBytesHash =
            requireHexUint64Field(headerText, "sourceBytesHash", relativeProductPath);
        if (!sourceBytesSize) {
            return std::unexpected{std::move(sourceBytesSize.error())};
        }
        if (!sourceBytesHash) {
            return std::unexpected{std::move(sourceBytesHash.error())};
        }
        if (*sourceBytesSize > SIZE_MAX || payloadBegin > productBytes.size() ||
            *sourceBytesSize > productBytes.size() - payloadBegin) {
            return std::unexpected{blobError(AssetProductBlobDiagnosticCode::UnterminatedPayload,
                                             std::string{relativeProductPath},
                                             "has an unterminated sourceBytes payload")};
        }

        const auto payloadByteCount = static_cast<std::size_t>(*sourceBytesSize);
        const std::size_t payloadEnd = payloadBegin + payloadByteCount;
        if (productBytes.size() < payloadEnd + kEnd.size() ||
            text.substr(payloadEnd, kEnd.size()) != kEnd) {
            return std::unexpected{blobError(AssetProductBlobDiagnosticCode::UnterminatedPayload,
                                             std::string{relativeProductPath},
                                             "has an unterminated sourceBytes payload")};
        }

        const std::span<const std::uint8_t> sourceByteSpan{
            productBytes.data() + payloadBegin,
            payloadByteCount,
        };
        if (hashBytes(sourceByteSpan) != *sourceBytesHash) {
            return std::unexpected{blobError(AssetProductBlobDiagnosticCode::InvalidProductBlob,
                                             std::string{relativeProductPath},
                                             "has a sourceBytes hash mismatch")};
        }

        std::vector<std::uint8_t> sourceBytes{sourceByteSpan.begin(), sourceByteSpan.end()};
        return AssetProductBlobPayload{.sourceBytes = std::move(sourceBytes)};
    }

    Result<AssetTextureProductPayload>
    readTexture2DProductPayload(const AssetProductBlobReadRequest& request) {
        auto bytes = readProductFileBytes(request);
        if (!bytes) {
            return std::unexpected{std::move(bytes.error())};
        }

        return readTexture2DProductPayload(
            std::span<const std::uint8_t>{bytes->data(), bytes->size()},
            request.relativeProductPath);
    }

    Result<AssetTextureProductPayload>
    readTexture2DProductPayload(std::span<const std::uint8_t> productBytes,
                                std::string_view relativeProductPath) {
        if (productBytes.empty()) {
            return std::unexpected{blobError(AssetProductBlobDiagnosticCode::InvalidProductBlob,
                                             std::string{relativeProductPath}, "is empty")};
        }

        constexpr std::string_view kBegin = "payload.begin\n";
        constexpr std::string_view kEnd = "\npayload.end\n";
        std::string productText;
        productText.reserve(productBytes.size());
        for (const std::uint8_t byte : productBytes) {
            productText.push_back(static_cast<char>(byte));
        }

        const std::size_t markerOffset = productText.find(kBegin);
        if (markerOffset == std::string_view::npos) {
            return std::unexpected{blobError(AssetProductBlobDiagnosticCode::MissingPayload,
                                             std::string{relativeProductPath},
                                             "does not contain payload.begin")};
        }

        const std::size_t payloadBegin = markerOffset + kBegin.size();
        const std::string headerText = productText.substr(0, markerOffset);
        auto header = parseTextureProductHeaderFields(headerText, relativeProductPath);
        if (!header) {
            return std::unexpected{std::move(header.error())};
        }
        if (header->payloadSize > SIZE_MAX || payloadBegin > productBytes.size() ||
            header->payloadSize > productBytes.size() - payloadBegin) {
            return std::unexpected{blobError(AssetProductBlobDiagnosticCode::UnterminatedPayload,
                                             std::string{relativeProductPath},
                                             "has an unterminated texture payload")};
        }

        const auto payloadByteCount = static_cast<std::size_t>(header->payloadSize);
        const std::size_t payloadEnd = payloadBegin + payloadByteCount;
        if (productBytes.size() < payloadEnd + kEnd.size() ||
            productText.substr(payloadEnd, kEnd.size()) != kEnd) {
            return std::unexpected{blobError(AssetProductBlobDiagnosticCode::UnterminatedPayload,
                                             std::string{relativeProductPath},
                                             "has an unterminated texture payload")};
        }

        std::vector<std::uint8_t> payload;
        payload.reserve(payloadByteCount);
        for (std::size_t index = payloadBegin; index < payloadEnd; ++index) {
            payload.push_back(productBytes[index]);
        }
        if (hashBytes(payload) != header->payloadHash) {
            return std::unexpected{blobError(AssetProductBlobDiagnosticCode::InvalidProductBlob,
                                             std::string{relativeProductPath},
                                             "has a texture payload hash mismatch")};
        }

        auto mips = parseTextureProductMips({
            .header = headerText,
            .mipCount = header->mipCount,
            .payloadSize = header->payloadSize,
            .relativeProductPath = relativeProductPath,
        });
        if (!mips) {
            return std::unexpected{std::move(mips.error())};
        }

        return AssetTextureProductPayload{
            .productTypeName = std::move(header->productTypeName),
            .importProfileName = std::move(header->importProfileName),
            .settingsVersion = header->settingsVersion,
            .format = header->format,
            .width = header->width,
            .height = header->height,
            .mips = std::move(*mips),
            .payload = std::move(payload),
        };
    }

    Result<AssetMaterialInstanceProductPayload>
    readMaterialInstanceProductPayload(const AssetProductBlobReadRequest& request) {
        auto bytes = readProductFileBytes(request);
        if (!bytes) {
            return std::unexpected{std::move(bytes.error())};
        }

        return readMaterialInstanceProductPayload(
            std::span<const std::uint8_t>{bytes->data(), bytes->size()},
            request.relativeProductPath);
    }

    Result<AssetMaterialInstanceProductPayload>
    readMaterialInstanceProductPayload(std::span<const std::uint8_t> productBytes,
                                       std::string_view relativeProductPath) {
        if (productBytes.empty()) {
            return std::unexpected{blobError(AssetProductBlobDiagnosticCode::InvalidProductBlob,
                                             std::string{relativeProductPath}, "is empty")};
        }

        constexpr std::string_view kBegin = "amat.begin\n";
        constexpr std::string_view kEnd = "\namat.end\n";
        std::string productText;
        productText.reserve(productBytes.size());
        for (const std::uint8_t byte : productBytes) {
            productText.push_back(static_cast<char>(byte));
        }

        const std::size_t markerOffset = productText.find(kBegin);
        if (markerOffset == std::string_view::npos) {
            return std::unexpected{blobError(AssetProductBlobDiagnosticCode::MissingPayload,
                                             std::string{relativeProductPath},
                                             "does not contain amat.begin")};
        }

        const std::size_t payloadBegin = markerOffset + kBegin.size();
        const std::string headerText = productText.substr(0, markerOffset);
        auto header = parseMaterialInstanceProductHeaderFields(headerText, relativeProductPath);
        if (!header) {
            return std::unexpected{std::move(header.error())};
        }
        if (header->amatSize > SIZE_MAX || payloadBegin > productBytes.size() ||
            header->amatSize > productBytes.size() - payloadBegin) {
            return std::unexpected{blobError(AssetProductBlobDiagnosticCode::UnterminatedPayload,
                                             std::string{relativeProductPath},
                                             "has an unterminated .amat payload")};
        }

        const auto payloadByteCount = static_cast<std::size_t>(header->amatSize);
        const std::size_t payloadEnd = payloadBegin + payloadByteCount;
        if (productBytes.size() < payloadEnd + kEnd.size() ||
            productText.substr(payloadEnd, kEnd.size()) != kEnd) {
            return std::unexpected{blobError(AssetProductBlobDiagnosticCode::UnterminatedPayload,
                                             std::string{relativeProductPath},
                                             "has an unterminated .amat payload")};
        }

        const std::span<const std::uint8_t> payloadBytes{
            productBytes.data() + payloadBegin,
            payloadByteCount,
        };
        if (hashBytes(payloadBytes) != header->amatHash) {
            return std::unexpected{blobError(AssetProductBlobDiagnosticCode::InvalidProductBlob,
                                             std::string{relativeProductPath},
                                             "has an .amat payload hash mismatch")};
        }

        std::string canonicalAmatText;
        canonicalAmatText.reserve(payloadByteCount);
        for (const std::uint8_t byte : payloadBytes) {
            canonicalAmatText.push_back(static_cast<char>(byte));
        }

        auto document = material_instance::readAmatText(canonicalAmatText);
        if (!document) {
            return std::unexpected{
                blobError(AssetProductBlobDiagnosticCode::InvalidProductBlob,
                          std::string{relativeProductPath},
                          "contains invalid canonical .amat payload: " + document.error().message)};
        }
        if (document->materialType.assetGuid != header->materialTypeAssetGuid ||
            document->materialType.stableTypeId != header->stableTypeId ||
            document->materialType.expectedTypeHash != header->expectedTypeHash ||
            document->import.lastCookedSignatureHash != header->lastCookedSignatureHash) {
            return std::unexpected{blobError(AssetProductBlobDiagnosticCode::InvalidProductBlob,
                                             std::string{relativeProductPath},
                                             "has mismatched material instance header fields")};
        }

        return AssetMaterialInstanceProductPayload{
            .materialTypeAssetGuid = header->materialTypeAssetGuid,
            .stableTypeId = std::move(header->stableTypeId),
            .expectedTypeHash = header->expectedTypeHash,
            .lastCookedSignatureHash = header->lastCookedSignatureHash,
            .canonicalAmatText = std::move(canonicalAmatText),
        };
    }

    Result<AssetShaderAuthoringProductPayload>
    readShaderAuthoringProductPayload(const AssetProductBlobReadRequest& request) {
        auto bytes = readProductFileBytes(request);
        if (!bytes) {
            return std::unexpected{std::move(bytes.error())};
        }

        return readShaderAuthoringProductPayload(
            std::span<const std::uint8_t>{bytes->data(), bytes->size()},
            request.relativeProductPath);
    }

    Result<AssetShaderAuthoringProductPayload>
    readShaderAuthoringProductPayload(std::span<const std::uint8_t> productBytes,
                                      std::string_view relativeProductPath) {
        if (productBytes.empty()) {
            return std::unexpected{blobError(AssetProductBlobDiagnosticCode::InvalidProductBlob,
                                             std::string{relativeProductPath}, "is empty")};
        }

        constexpr std::string_view kBegin = "generatedSlang.begin\n";
        constexpr std::string_view kEnd = "\ngeneratedSlang.end\n";
        std::string productText;
        productText.reserve(productBytes.size());
        for (const std::uint8_t byte : productBytes) {
            productText.push_back(static_cast<char>(byte));
        }

        const std::size_t markerOffset = productText.find(kBegin);
        if (markerOffset == std::string_view::npos) {
            return std::unexpected{blobError(AssetProductBlobDiagnosticCode::MissingPayload,
                                             std::string{relativeProductPath},
                                             "does not contain generatedSlang.begin")};
        }

        const std::size_t payloadBegin = markerOffset + kBegin.size();
        const std::string headerText = productText.substr(0, markerOffset);
        auto header = parseShaderAuthoringProductHeaderFields(headerText, relativeProductPath);
        if (!header) {
            return std::unexpected{std::move(header.error())};
        }
        if (header->generatedSlangSize > SIZE_MAX || payloadBegin > productBytes.size() ||
            header->generatedSlangSize > productBytes.size() - payloadBegin) {
            return std::unexpected{blobError(AssetProductBlobDiagnosticCode::UnterminatedPayload,
                                             std::string{relativeProductPath},
                                             "has an unterminated generated Slang payload")};
        }

        const auto payloadByteCount = static_cast<std::size_t>(header->generatedSlangSize);
        const std::size_t payloadEnd = payloadBegin + payloadByteCount;
        if (productBytes.size() < payloadEnd + kEnd.size() ||
            productText.substr(payloadEnd, kEnd.size()) != kEnd) {
            return std::unexpected{blobError(AssetProductBlobDiagnosticCode::UnterminatedPayload,
                                             std::string{relativeProductPath},
                                             "has an unterminated generated Slang payload")};
        }

        const std::span<const std::uint8_t> payloadBytes{
            productBytes.data() + payloadBegin,
            payloadByteCount,
        };
        if (hashBytes(payloadBytes) != header->generatedSlangHash) {
            return std::unexpected{blobError(AssetProductBlobDiagnosticCode::InvalidProductBlob,
                                             std::string{relativeProductPath},
                                             "has a generated Slang payload hash mismatch")};
        }

        auto properties =
            parseShaderAuthoringProperties(headerText, header->propertyCount, relativeProductPath);
        if (!properties) {
            return std::unexpected{std::move(properties.error())};
        }
        auto passes =
            parseShaderAuthoringPasses(headerText, header->passCount, relativeProductPath);
        if (!passes) {
            return std::unexpected{std::move(passes.error())};
        }
        auto bindings =
            parseShaderAuthoringBindings(headerText, header->bindingCount, relativeProductPath);
        if (!bindings) {
            return std::unexpected{std::move(bindings.error())};
        }
        auto entries =
            parseShaderAuthoringEntries(headerText, header->entryCount, relativeProductPath);
        if (!entries) {
            return std::unexpected{std::move(entries.error())};
        }

        std::string generatedSlangText;
        generatedSlangText.reserve(payloadByteCount);
        for (const std::uint8_t byte : payloadBytes) {
            generatedSlangText.push_back(static_cast<char>(byte));
        }

        return AssetShaderAuthoringProductPayload{
            .sourcePath = std::move(header->sourcePath),
            .stableTypeId = std::move(header->stableTypeId),
            .schemaVersion = header->schemaVersion,
            .generatedSlangHash = header->generatedSlangHash,
            .properties = std::move(*properties),
            .passes = std::move(*passes),
            .bindings = std::move(*bindings),
            .entries = std::move(*entries),
            .generatedSlangText = std::move(generatedSlangText),
        };
    }

    const char* assetProductBlobDiagnosticCodeName(AssetProductBlobDiagnosticCode code) noexcept {
        switch (code) {
        case AssetProductBlobDiagnosticCode::MissingProduct:
            return "missing-product";
        case AssetProductBlobDiagnosticCode::ProductReadFailed:
            return "product-read-failed";
        case AssetProductBlobDiagnosticCode::InvalidProductBlob:
            return "invalid-product-blob";
        case AssetProductBlobDiagnosticCode::MissingPayload:
            return "missing-payload";
        case AssetProductBlobDiagnosticCode::UnterminatedPayload:
            return "unterminated-payload";
        }
        return "unknown";
    }

} // namespace asharia::asset
