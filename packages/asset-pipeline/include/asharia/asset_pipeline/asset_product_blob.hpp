#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "asharia/asset_core/asset_guid.hpp"
#include "asharia/asset_pipeline/asset_texture_import.hpp"
#include "asharia/core/result.hpp"

namespace asharia::asset {

    enum class AssetProductBlobDiagnosticCode {
        MissingProduct,
        ProductReadFailed,
        InvalidProductBlob,
        MissingPayload,
        UnterminatedPayload,
    };

    struct AssetProductBlobReadRequest {
        std::filesystem::path productFilePath;
        std::string relativeProductPath;

        [[nodiscard]] friend bool operator==(const AssetProductBlobReadRequest&,
                                             const AssetProductBlobReadRequest&) = default;
    };

    struct AssetProductBlobPayload {
        std::vector<std::uint8_t> sourceBytes;

        [[nodiscard]] friend bool operator==(const AssetProductBlobPayload&,
                                             const AssetProductBlobPayload&) = default;
    };

    struct AssetTextureProductPayload {
        std::string sourcePath;
        std::string productTypeName;
        std::string importProfileName;
        std::uint32_t settingsVersion{};
        AssetTextureImportFormat format{AssetTextureImportFormat::Rgba8Unorm};
        std::uint32_t width{};
        std::uint32_t height{};
        std::vector<AssetTextureMipPayload> mips;
        std::vector<std::uint8_t> payload;

        [[nodiscard]] friend bool operator==(const AssetTextureProductPayload&,
                                             const AssetTextureProductPayload&) = default;
    };

    struct AssetMaterialInstanceProductPayload {
        std::string sourcePath;
        AssetGuid materialTypeAssetGuid{};
        std::string stableTypeId;
        std::uint64_t expectedTypeHash{};
        std::uint64_t lastCookedSignatureHash{};
        std::string canonicalAmatText;

        [[nodiscard]] friend bool operator==(const AssetMaterialInstanceProductPayload&,
                                             const AssetMaterialInstanceProductPayload&) = default;
    };

    struct AssetShaderAuthoringProductProperty {
        std::string name;
        std::string typeName;
        std::string defaultText;

        [[nodiscard]] friend bool operator==(const AssetShaderAuthoringProductProperty&,
                                             const AssetShaderAuthoringProductProperty&) = default;
    };

    struct AssetShaderAuthoringProductPass {
        std::string name;
        std::string tag;
        std::string vertexEntry;
        std::string fragmentEntry;
        std::string computeEntry;

        [[nodiscard]] friend bool operator==(const AssetShaderAuthoringProductPass&,
                                             const AssetShaderAuthoringProductPass&) = default;
    };

    struct AssetShaderAuthoringProductBinding {
        std::string name;
        std::string typeName;
        std::uint32_t set{};
        std::uint32_t binding{};
        bool inMaterialParameterBlock{};

        [[nodiscard]] friend bool operator==(const AssetShaderAuthoringProductBinding&,
                                             const AssetShaderAuthoringProductBinding&) = default;
    };

    struct AssetShaderAuthoringProductEntry {
        std::string passName;
        std::string stage;
        std::string sourceEntryName;
        std::string compileEntryName;
        std::string generatedWrapperName;

        [[nodiscard]] friend bool operator==(const AssetShaderAuthoringProductEntry&,
                                             const AssetShaderAuthoringProductEntry&) = default;
    };

    struct AssetShaderAuthoringProductPayload {
        std::string sourcePath;
        std::string stableTypeId;
        std::uint32_t schemaVersion{};
        std::uint64_t generatedSlangHash{};
        std::vector<AssetShaderAuthoringProductProperty> properties;
        std::vector<AssetShaderAuthoringProductPass> passes;
        std::vector<AssetShaderAuthoringProductBinding> bindings;
        std::vector<AssetShaderAuthoringProductEntry> entries;
        std::string generatedSlangText;

        [[nodiscard]] friend bool operator==(const AssetShaderAuthoringProductPayload&,
                                             const AssetShaderAuthoringProductPayload&) = default;
    };

    struct AssetShaderCompileReflectionProductEntry {
        std::string passName;
        std::string stage;
        std::string sourceEntryName;
        std::string compileEntryName;
        std::string generatedWrapperName;
        std::uint64_t slangcExitCode{};
        std::uint64_t slangcDiagnosticHash{};
        std::string slangcDiagnosticText;
        std::uint64_t spirvValExitCode{};
        std::uint64_t spirvValDiagnosticHash{};
        std::string spirvValDiagnosticText;
        std::uint64_t spirvHash{};
        std::uint64_t reflectionJsonHash{};
        std::vector<std::uint8_t> spirvBytes;
        std::string reflectionJsonText;

        [[nodiscard]] friend bool
        operator==(const AssetShaderCompileReflectionProductEntry&,
                   const AssetShaderCompileReflectionProductEntry&) = default;
    };

    struct AssetShaderCompileReflectionProductPayload {
        std::string sourcePath;
        std::string stableTypeId;
        std::string authoringProductPath;
        std::uint64_t authoringProductHash{};
        std::uint64_t generatedSlangHash{};
        std::uint64_t productKeyHash{};
        std::string profile;
        std::string target;
        std::vector<AssetShaderCompileReflectionProductEntry> entries;

        [[nodiscard]] friend bool
        operator==(const AssetShaderCompileReflectionProductPayload&,
                   const AssetShaderCompileReflectionProductPayload&) = default;
    };

    [[nodiscard]] const char*
    assetProductBlobDiagnosticCodeName(AssetProductBlobDiagnosticCode code) noexcept;

    [[nodiscard]] Result<AssetProductBlobPayload>
    readPlaceholderProductSourceBytes(const AssetProductBlobReadRequest& request);

    [[nodiscard]] Result<AssetProductBlobPayload>
    readPlaceholderProductSourceBytes(std::span<const std::uint8_t> productBytes,
                                      std::string_view relativeProductPath);

    [[nodiscard]] Result<AssetTextureProductPayload>
    readTexture2DProductPayload(const AssetProductBlobReadRequest& request);

    [[nodiscard]] Result<AssetTextureProductPayload>
    readTexture2DProductPayload(std::span<const std::uint8_t> productBytes,
                                std::string_view relativeProductPath);

    [[nodiscard]] Result<AssetMaterialInstanceProductPayload>
    readMaterialInstanceProductPayload(const AssetProductBlobReadRequest& request);

    [[nodiscard]] Result<AssetMaterialInstanceProductPayload>
    readMaterialInstanceProductPayload(std::span<const std::uint8_t> productBytes,
                                       std::string_view relativeProductPath);

    [[nodiscard]] Result<AssetShaderAuthoringProductPayload>
    readShaderAuthoringProductPayload(const AssetProductBlobReadRequest& request);

    [[nodiscard]] Result<AssetShaderAuthoringProductPayload>
    readShaderAuthoringProductPayload(std::span<const std::uint8_t> productBytes,
                                      std::string_view relativeProductPath);

    [[nodiscard]] Result<AssetShaderCompileReflectionProductPayload>
    readShaderCompileReflectionProductPayload(const AssetProductBlobReadRequest& request);

    [[nodiscard]] Result<AssetShaderCompileReflectionProductPayload>
    readShaderCompileReflectionProductPayload(std::span<const std::uint8_t> productBytes,
                                              std::string_view relativeProductPath);

} // namespace asharia::asset
