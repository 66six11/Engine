#include "asharia/asset_pipeline/asset_product_execution.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

#include "asharia/asset_core/asset_guid.hpp"
#include "asharia/asset_pipeline/asset_product_blob.hpp"
#include "asharia/asset_pipeline/asset_texture_import.hpp"
#include "asharia/core/error.hpp"
#include "asharia/material_instance/amat_io.hpp"
#include "asharia/shader_authoring/ashader_generated_slang.hpp"
#include "asharia/shader_authoring/ashader_parser.hpp"

namespace asharia::asset {
    namespace {

        constexpr std::uint64_t kFnv1a64Offset = 14695981039346656037ULL;
        constexpr std::uint64_t kFnv1a64Prime = 1099511628211ULL;
        constexpr std::string_view kMaterialInstanceImporterName =
            "com.asharia.importer.material-instance";
        constexpr std::uint32_t kMaterialInstanceImporterVersion = 1;
        constexpr std::string_view kShaderAuthoringImporterName =
            "com.asharia.importer.shader-authoring";
        constexpr std::uint32_t kShaderAuthoringImporterVersion = 1;
        constexpr std::string_view kShaderCompileReflectionImporterName =
            "com.asharia.importer.shader-compile-reflection";
        constexpr std::uint32_t kShaderCompileReflectionImporterVersion = 1;
        constexpr std::string_view kShaderAuthoringProductPathSettingKey =
            "shader.authoringProductPath";
        constexpr std::string_view kSlangProfile = "glsl_450";
        constexpr std::string_view kSlangTarget = "spirv";

        struct PreparedProduct {
            const AssetImportRequest* request{};
            AssetProductRecord product;
            std::filesystem::path outputPath;
            std::vector<std::uint8_t> bytes;
        };

        struct ShaderCompileReflectionWorkPaths {
            std::filesystem::path workDir;
            std::filesystem::path generatedSourcePath;
        };

        struct ShaderCompileReflectionToolPaths {
            std::filesystem::path slangcPath;
            std::filesystem::path spirvValPath;
        };

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

        [[nodiscard]] std::string formatHash64(std::uint64_t value) {
            constexpr std::string_view kHexDigits = "0123456789abcdef";
            std::string text(16, '0');
            for (std::size_t index = 0; index < text.size(); ++index) {
                const auto shift = static_cast<std::uint32_t>((text.size() - index - 1) * 4);
                text[index] = kHexDigits[(value >> shift) & 0xFU];
            }
            return text;
        }

        [[nodiscard]] Error shaderAuthoringImportError(std::string message) {
            return Error{
                ErrorDomain::Asset,
                static_cast<int>(AssetProductExecutionDiagnosticCode::ShaderAuthoringImportFailed),
                std::move(message)};
        }

        [[nodiscard]] Error shaderCompileReflectionImportError(std::string message) {
            return Error{
                ErrorDomain::Asset,
                static_cast<int>(
                    AssetProductExecutionDiagnosticCode::ShaderCompileReflectionImportFailed),
                std::move(message)};
        }

        [[nodiscard]] std::string
        ashaderDiagnosticSummary(std::span<const shader_authoring::AshaderDiagnostic> diagnostics) {
            if (diagnostics.empty()) {
                return "unknown .ashader diagnostic";
            }

            const shader_authoring::AshaderDiagnostic& diagnostic = diagnostics.front();
            return std::string{shader_authoring::toString(diagnostic.code)} + ": " +
                   diagnostic.message;
        }

        [[nodiscard]] std::string pathText(const std::filesystem::path& path) {
            const std::u8string text = path.generic_u8string();
            return std::string{text.begin(), text.end()};
        }

        [[nodiscard]] std::filesystem::path pathFromUtf8(std::string_view text) {
            std::u8string utf8;
            utf8.reserve(text.size());
            for (const char value : text) {
                utf8.push_back(static_cast<char8_t>(static_cast<unsigned char>(value)));
            }
            return std::filesystem::path{utf8};
        }

        [[nodiscard]] char asciiLower(char value) noexcept {
            return (value >= 'A' && value <= 'Z') ? static_cast<char>(value - 'A' + 'a') : value;
        }

        [[nodiscard]] std::string sourceExtension(std::string_view sourcePath) {
            const std::size_t slash = sourcePath.find_last_of('/');
            const std::size_t dot = sourcePath.find_last_of('.');
            if (dot == std::string_view::npos || (slash != std::string_view::npos && dot < slash) ||
                dot + 1 >= sourcePath.size()) {
                return {};
            }

            std::string extension{sourcePath.substr(dot)};
            for (char& value : extension) {
                value = asciiLower(value);
            }
            return extension;
        }

        [[nodiscard]] bool isSupportedSlangStage(std::string_view stage) noexcept {
            return stage == "vertex" || stage == "fragment" || stage == "compute";
        }

        [[nodiscard]] bool isSlangIdentifierHeadChar(char value) noexcept {
            return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') || value == '_';
        }

        [[nodiscard]] bool isSlangIdentifierBodyChar(char value) noexcept {
            return isSlangIdentifierHeadChar(value) || (value >= '0' && value <= '9');
        }

        [[nodiscard]] bool isSafeSlangIdentifier(std::string_view value) noexcept {
            if (value.empty()) {
                return false;
            }
            if (!isSlangIdentifierHeadChar(value.front())) {
                return false;
            }
            return std::ranges::all_of(value.substr(1), isSlangIdentifierBodyChar);
        }

        [[nodiscard]] std::string quotePath(const std::filesystem::path& path) {
            return "\"" + path.string() + "\"";
        }

        [[nodiscard]] std::string hexEncode(std::span<const std::uint8_t> bytes) {
            constexpr std::string_view kHexDigits = "0123456789abcdef";
            std::string text;
            text.reserve(bytes.size() * 2U);
            for (const std::uint8_t byte : bytes) {
                text.push_back(kHexDigits[(byte >> 4U) & 0x0FU]);
                text.push_back(kHexDigits[byte & 0x0FU]);
            }
            return text;
        }

        [[nodiscard]] std::vector<std::uint8_t> bytesFromString(std::string_view text) {
            std::vector<std::uint8_t> bytes;
            bytes.reserve(text.size());
            for (const char value : text) {
                bytes.push_back(static_cast<std::uint8_t>(static_cast<unsigned char>(value)));
            }
            return bytes;
        }

        [[nodiscard]] bool writeTextFile(const std::filesystem::path& path, std::string_view text) {
            const std::filesystem::path parent = path.parent_path();
            if (!parent.empty()) {
                std::error_code createError;
                std::filesystem::create_directories(parent, createError);
                if (createError) {
                    return false;
                }
            }

            std::ofstream file(path, std::ios::binary);
            if (!file) {
                return false;
            }
            file.write(text.data(), static_cast<std::streamsize>(text.size()));
            return static_cast<bool>(file);
        }

        [[nodiscard]] Result<std::vector<std::uint8_t>>
        readBinaryFile(const std::filesystem::path& path) {
            std::ifstream file(path, std::ios::binary);
            if (!file) {
                return std::unexpected{shaderCompileReflectionImportError(
                    "Could not open tool output file '" + pathText(path) + "'.")};
            }

            std::vector<std::uint8_t> bytes;
            char byte{};
            while (file.get(byte)) {
                bytes.push_back(static_cast<std::uint8_t>(static_cast<unsigned char>(byte)));
            }
            if (!file.eof()) {
                return std::unexpected{shaderCompileReflectionImportError(
                    "Could not read tool output file '" + pathText(path) + "'.")};
            }
            return bytes;
        }

        [[nodiscard]] std::optional<std::string> environmentVariable(std::string_view name) {
            const std::string nameText{name};
#if defined(_WIN32)
            const DWORD requiredSize = GetEnvironmentVariableA(nameText.c_str(), nullptr, 0);
            if (requiredSize == 0) {
                return std::nullopt;
            }
            std::string text(requiredSize, '\0');
            const DWORD writtenSize =
                GetEnvironmentVariableA(nameText.c_str(), text.data(), requiredSize);
            if (writtenSize == 0 || writtenSize >= requiredSize) {
                return std::nullopt;
            }
            text.resize(writtenSize);
            return text;
#else
            const char* value = std::getenv(nameText.c_str());
            if (value == nullptr) {
                return std::nullopt;
            }
            return std::string{value};
#endif
        }

        [[nodiscard]] std::optional<std::filesystem::path>
        findToolExecutable(std::string_view toolName) {
            std::vector<std::filesystem::path> directories;
            if (const std::optional<std::string> pathValue = environmentVariable("PATH");
                pathValue) {
                const std::string& pathTextValue = *pathValue;
#if defined(_WIN32)
                constexpr char kSeparator = ';';
#else
                constexpr char kSeparator = ':';
#endif
                std::size_t segmentBegin = 0;
                while (segmentBegin <= pathTextValue.size()) {
                    std::size_t segmentEnd = pathTextValue.find(kSeparator, segmentBegin);
                    if (segmentEnd == std::string::npos) {
                        segmentEnd = pathTextValue.size();
                    }
                    if (segmentEnd > segmentBegin) {
                        directories.emplace_back(
                            pathTextValue.substr(segmentBegin, segmentEnd - segmentBegin));
                    }
                    if (segmentEnd == pathTextValue.size()) {
                        break;
                    }
                    segmentBegin = segmentEnd + 1U;
                }
            }
            if (const std::optional<std::string> sdkPath = environmentVariable("VULKAN_SDK");
                sdkPath) {
                directories.emplace_back(std::filesystem::path{*sdkPath} / "Bin");
            }

            std::vector<std::string> candidateNames;
#if defined(_WIN32)
            candidateNames.push_back(std::string{toolName} + ".exe");
#endif
            candidateNames.emplace_back(toolName);

            for (const std::filesystem::path& directory : directories) {
                for (const std::string& candidateName : candidateNames) {
                    std::filesystem::path candidate = directory / candidateName;
                    std::error_code statusError;
                    const std::filesystem::file_status status =
                        std::filesystem::status(candidate, statusError);
                    if (!statusError && std::filesystem::is_regular_file(status)) {
                        return candidate;
                    }
                }
            }
            return std::nullopt;
        }

        [[nodiscard]] Result<void> runToolCommand(std::string_view label,
                                                  const std::string& command) {
#if defined(_WIN32)
            const std::string shellCommand = "cmd /S /C \"" + command + "\"";
#else
            const std::string shellCommand = command;
#endif
            const int exitCode = std::system(shellCommand.c_str());
            if (exitCode != 0) {
                return std::unexpected{shaderCompileReflectionImportError(
                    std::string{label} + " failed with exit code " + std::to_string(exitCode) +
                    ".")};
            }
            return {};
        }

        [[nodiscard]] std::string productLabel(const AssetImportRequest& request) {
            return "guid=\"" + formatAssetGuid(request.source.guid) + "\" source=\"" +
                   request.source.sourcePath + "\" assetType=\"" + request.source.assetTypeName +
                   "\" importer=\"" + request.source.importerName + "\" product=\"" +
                   request.relativeProductPath + "\" productKeyHash=\"" +
                   formatHash64(hashAssetProductKey(request.productKey)) + "\"";
        }

        void addDiagnostic(AssetProductExecutionResult& result,
                           AssetProductExecutionDiagnosticCode code, std::string sourcePath,
                           std::string productPath, std::string message) {
            result.diagnostics.push_back(AssetProductExecutionDiagnostic{
                .code = code,
                .sourcePath = std::move(sourcePath),
                .relativeProductPath = std::move(productPath),
                .message = std::move(message),
            });
        }

        [[nodiscard]] std::string textureImportDiagnosticLabel(const Error& error) {
            if (error.domain != ErrorDomain::Asset) {
                return "unknown";
            }

            switch (static_cast<AssetTextureImportDiagnosticCode>(error.code)) {
            case AssetTextureImportDiagnosticCode::InvalidRequest:
            case AssetTextureImportDiagnosticCode::UnsupportedSourceExtension:
            case AssetTextureImportDiagnosticCode::UnsupportedProfile:
            case AssetTextureImportDiagnosticCode::UnsupportedSettingsVersion:
            case AssetTextureImportDiagnosticCode::InvalidDimensions:
            case AssetTextureImportDiagnosticCode::UnsupportedFormat:
            case AssetTextureImportDiagnosticCode::PayloadSizeMismatch:
            case AssetTextureImportDiagnosticCode::DecodeFailed:
                return std::string{assetTextureImportDiagnosticCodeName(
                    static_cast<AssetTextureImportDiagnosticCode>(error.code))};
            }

            return "unknown";
        }

        void addRequestDiagnostic(AssetProductExecutionResult& result,
                                  AssetProductExecutionDiagnosticCode code,
                                  const AssetImportRequest& request, std::string message) {
            addDiagnostic(result, code, request.source.sourcePath, request.relativeProductPath,
                          std::move(message));
        }

        [[nodiscard]] const AssetProductSourceBytes*
        findSourceBytes(std::span<const AssetProductSourceBytes> sources,
                        std::string_view sourcePath) {
            for (const AssetProductSourceBytes& source : sources) {
                if (source.sourcePath == sourcePath) {
                    return &source;
                }
            }
            return nullptr;
        }

        [[nodiscard]] const AssetProductDependencyBytes*
        findDependencyProductBytes(std::span<const AssetProductDependencyBytes> products,
                                   std::string_view relativeProductPath) {
            for (const AssetProductDependencyBytes& product : products) {
                if (product.relativeProductPath == relativeProductPath) {
                    return &product;
                }
            }
            return nullptr;
        }

        [[nodiscard]] const AssetImportSetting*
        findImportSetting(std::span<const AssetImportSetting> settings, std::string_view key) {
            for (const AssetImportSetting& setting : settings) {
                if (setting.key == key) {
                    return &setting;
                }
            }
            return nullptr;
        }

        [[nodiscard]] bool validateSourceBytes(AssetProductExecutionResult& result,
                                               std::span<const AssetProductSourceBytes> sources) {
            bool valid = true;
            for (std::size_t index = 0; index < sources.size(); ++index) {
                const AssetProductSourceBytes& source = sources[index];
                if (auto validPath = validateAssetSourcePath(source.sourcePath); !validPath) {
                    addDiagnostic(result, AssetProductExecutionDiagnosticCode::InvalidSourceBytes,
                                  source.sourcePath, {},
                                  "Asset product execution rejected source bytes[" +
                                      std::to_string(index) + "]: " + validPath.error().message);
                    valid = false;
                }

                for (std::size_t otherIndex = index + 1; otherIndex < sources.size();
                     ++otherIndex) {
                    if (source.sourcePath == sources[otherIndex].sourcePath) {
                        addDiagnostic(
                            result, AssetProductExecutionDiagnosticCode::DuplicateSourceBytes,
                            source.sourcePath, {},
                            "Asset product execution source bytes[" + std::to_string(index) +
                                "] duplicates source bytes[" + std::to_string(otherIndex) +
                                "] source=\"" + source.sourcePath + "\".");
                        valid = false;
                    }
                }
            }
            return valid;
        }

        [[nodiscard]] bool validateDependencyProductBytes(
            AssetProductExecutionResult& result,
            std::span<const AssetProductDependencyBytes> dependencyProducts) {
            bool valid = true;
            for (std::size_t index = 0; index < dependencyProducts.size(); ++index) {
                const AssetProductDependencyBytes& product = dependencyProducts[index];
                if (auto validPath = validateAssetProductPath(product.relativeProductPath);
                    !validPath) {
                    addDiagnostic(
                        result, AssetProductExecutionDiagnosticCode::InvalidDependencyProductBytes,
                        {}, product.relativeProductPath,
                        "Asset product execution rejected dependency product bytes[" +
                            std::to_string(index) + "]: " + validPath.error().message);
                    valid = false;
                }

                for (std::size_t otherIndex = index + 1; otherIndex < dependencyProducts.size();
                     ++otherIndex) {
                    if (product.relativeProductPath ==
                        dependencyProducts[otherIndex].relativeProductPath) {
                        addDiagnostic(
                            result,
                            AssetProductExecutionDiagnosticCode::DuplicateDependencyProductBytes,
                            {}, product.relativeProductPath,
                            "Asset product execution dependency product bytes[" +
                                std::to_string(index) + "] duplicates dependency product bytes[" +
                                std::to_string(otherIndex) + "] product=\"" +
                                product.relativeProductPath + "\".");
                        valid = false;
                    }
                }

                const std::uint64_t actualProductHash = hashBytes(product.bytes);
                if (actualProductHash != product.productHash) {
                    addDiagnostic(
                        result,
                        AssetProductExecutionDiagnosticCode::DependencyProductBytesHashMismatch, {},
                        product.relativeProductPath,
                        "Asset product execution dependency product bytes[" +
                            std::to_string(index) + "] hash mismatch for product=\"" +
                            product.relativeProductPath + "\" expected=\"" +
                            formatHash64(product.productHash) + "\" actual=\"" +
                            formatHash64(actualProductHash) + "\".");
                    valid = false;
                }
            }
            return valid;
        }

        void appendLine(std::vector<std::uint8_t>& bytes, std::string_view line) {
            bytes.insert(bytes.end(), line.begin(), line.end());
            bytes.push_back(static_cast<std::uint8_t>('\n'));
        }

        [[nodiscard]] std::vector<std::uint8_t>
        makePlaceholderProductBytes(const AssetImportRequest& request,
                                    std::span<const std::uint8_t> sourceBytes) {
            std::vector<std::uint8_t> bytes;
            bytes.reserve(512 + sourceBytes.size());
            appendLine(bytes, "schema=com.asharia.asset.placeholder-product.v1");
            appendLine(bytes, "guid=" + formatAssetGuid(request.source.guid));
            appendLine(bytes, "sourcePath=" + request.source.sourcePath);
            appendLine(bytes, "assetType=" + request.source.assetTypeName);
            appendLine(bytes, "importer=" + request.source.importerName);
            appendLine(bytes,
                       "importerVersion=" + std::to_string(request.source.importerVersion.value));
            appendLine(bytes, "sourceHash=" + formatHash64(request.source.sourceHash));
            appendLine(bytes, "settingsHash=" + formatHash64(request.source.settingsHash));
            appendLine(bytes, "dependencyHash=" + formatHash64(request.productKey.dependencyHash));
            appendLine(bytes,
                       "targetProfileHash=" + formatHash64(request.productKey.targetProfileHash));
            appendLine(bytes,
                       "productKeyHash=" + formatHash64(hashAssetProductKey(request.productKey)));
            appendLine(bytes, "settings.count=" + std::to_string(request.settings.size()));
            for (const AssetImportSetting& setting : request.settings) {
                appendLine(bytes, "setting." + setting.key + "=" + setting.value);
            }
            appendLine(bytes, "sourceBytes.size=" + std::to_string(sourceBytes.size()));
            appendLine(bytes, "sourceBytesHash=" + formatHash64(hashBytes(sourceBytes)));
            appendLine(bytes, "sourceBytes.begin");
            bytes.insert(bytes.end(), sourceBytes.begin(), sourceBytes.end());
            appendLine(bytes, "");
            appendLine(bytes, "sourceBytes.end");
            return bytes;
        }

        [[nodiscard]] bool isPngTextureProductRequest(const AssetImportRequest& request) {
            const AssetTextureImporterDescriptor importer = makePngTextureImporterDescriptor();
            return request.source.importerName == importer.importerName &&
                   request.source.importerVersion == importer.importerVersion &&
                   sourceExtension(request.source.sourcePath) == kTextureImportPngExtension;
        }

        [[nodiscard]] std::vector<std::uint8_t>
        makeTexture2DProductBytes(const AssetImportRequest& request,
                                  const AssetTextureImportResult& texture) {
            std::vector<std::uint8_t> bytes;
            bytes.reserve(768 + texture.payload.size());
            appendLine(bytes, "schema=com.asharia.asset.texture2d-product.v1");
            appendLine(bytes, "guid=" + formatAssetGuid(request.source.guid));
            appendLine(bytes, "sourcePath=" + request.source.sourcePath);
            appendLine(bytes, "assetType=" + request.source.assetTypeName);
            appendLine(bytes, "importer=" + request.source.importerName);
            appendLine(bytes,
                       "importerVersion=" + std::to_string(request.source.importerVersion.value));
            appendLine(bytes, "sourceExtension=" + texture.sourceExtension);
            appendLine(bytes, "importProfile=" + texture.importProfileName);
            appendLine(bytes, "productType=" + texture.productTypeName);
            appendLine(bytes, "settingsVersion=" + std::to_string(texture.settingsVersion));
            appendLine(bytes,
                       "format=" + std::string{assetTextureImportFormatName(texture.format)});
            appendLine(bytes, "width=" + std::to_string(texture.width));
            appendLine(bytes, "height=" + std::to_string(texture.height));
            appendLine(bytes, "sourceHash=" + formatHash64(request.source.sourceHash));
            appendLine(bytes, "settingsHash=" + formatHash64(request.source.settingsHash));
            appendLine(bytes, "dependencyHash=" + formatHash64(request.productKey.dependencyHash));
            appendLine(bytes,
                       "targetProfileHash=" + formatHash64(request.productKey.targetProfileHash));
            appendLine(bytes,
                       "productKeyHash=" + formatHash64(hashAssetProductKey(request.productKey)));
            appendLine(bytes, "settings.count=" + std::to_string(request.settings.size()));
            for (const AssetImportSetting& setting : request.settings) {
                appendLine(bytes, "setting." + setting.key + "=" + setting.value);
            }
            appendLine(bytes, "mip.count=" + std::to_string(texture.mips.size()));
            for (std::size_t index = 0; index < texture.mips.size(); ++index) {
                const AssetTextureMipPayload& mip = texture.mips[index];
                const std::string prefix = "mip." + std::to_string(index) + ".";
                appendLine(bytes, prefix + "level=" + std::to_string(mip.level));
                appendLine(bytes, prefix + "width=" + std::to_string(mip.width));
                appendLine(bytes, prefix + "height=" + std::to_string(mip.height));
                appendLine(bytes, prefix + "byteOffset=" + std::to_string(mip.byteOffset));
                appendLine(bytes, prefix + "byteSize=" + std::to_string(mip.byteSize));
            }
            appendLine(bytes, "payload.size=" + std::to_string(texture.payload.size()));
            appendLine(bytes, "payloadHash=" + formatHash64(hashBytes(texture.payload)));
            appendLine(bytes, "payload.begin");
            bytes.insert(bytes.end(), texture.payload.begin(), texture.payload.end());
            appendLine(bytes, "");
            appendLine(bytes, "payload.end");
            return bytes;
        }

        [[nodiscard]] bool isMaterialInstanceProductRequest(const AssetImportRequest& request) {
            return request.source.importerName == kMaterialInstanceImporterName &&
                   request.source.importerVersion ==
                       ImporterVersion{kMaterialInstanceImporterVersion} &&
                   sourceExtension(request.source.sourcePath) == ".amat";
        }

        [[nodiscard]] bool isShaderAuthoringProductRequest(const AssetImportRequest& request) {
            return request.source.importerName == kShaderAuthoringImporterName &&
                   request.source.importerVersion ==
                       ImporterVersion{kShaderAuthoringImporterVersion} &&
                   sourceExtension(request.source.sourcePath) == ".ashader";
        }

        [[nodiscard]] bool
        isShaderCompileReflectionProductRequest(const AssetImportRequest& request) {
            return request.source.importerName == kShaderCompileReflectionImporterName &&
                   request.source.importerVersion ==
                       ImporterVersion{kShaderCompileReflectionImporterVersion} &&
                   sourceExtension(request.source.sourcePath) == ".ashader";
        }

        [[nodiscard]] Result<std::vector<std::uint8_t>>
        makeMaterialInstanceProductBytes(const AssetImportRequest& request,
                                         std::span<const std::uint8_t> sourceBytes) {
            std::string sourceText;
            sourceText.reserve(sourceBytes.size());
            for (const std::uint8_t byte : sourceBytes) {
                sourceText.push_back(static_cast<char>(byte));
            }

            auto document = material_instance::readAmatText(sourceText);
            if (!document) {
                return std::unexpected{std::move(document.error())};
            }
            auto canonicalText = material_instance::writeAmatText(*document);
            if (!canonicalText) {
                return std::unexpected{std::move(canonicalText.error())};
            }

            std::vector<std::uint8_t> canonicalBytes;
            canonicalBytes.reserve(canonicalText->size());
            for (const char value : *canonicalText) {
                canonicalBytes.push_back(
                    static_cast<std::uint8_t>(static_cast<unsigned char>(value)));
            }

            std::vector<std::uint8_t> bytes;
            bytes.reserve(1024 + canonicalBytes.size());
            appendLine(bytes, "schema=com.asharia.asset.material-instance-product.v1");
            appendLine(bytes, "guid=" + formatAssetGuid(request.source.guid));
            appendLine(bytes, "sourcePath=" + request.source.sourcePath);
            appendLine(bytes, "assetType=" + request.source.assetTypeName);
            appendLine(bytes, "importer=" + request.source.importerName);
            appendLine(bytes,
                       "importerVersion=" + std::to_string(request.source.importerVersion.value));
            appendLine(bytes, "materialType.assetGuid=" +
                                  formatAssetGuid(document->materialType.assetGuid));
            appendLine(bytes, "materialType.stableTypeId=" + document->materialType.stableTypeId);
            appendLine(bytes, "materialType.expectedTypeHash=" +
                                  formatHash64(document->materialType.expectedTypeHash));
            appendLine(bytes, "import.lastCookedSignatureHash=" +
                                  formatHash64(document->import.lastCookedSignatureHash));
            appendLine(bytes, "sourceHash=" + formatHash64(request.source.sourceHash));
            appendLine(bytes, "settingsHash=" + formatHash64(request.source.settingsHash));
            appendLine(bytes, "dependencyHash=" + formatHash64(request.productKey.dependencyHash));
            appendLine(bytes,
                       "targetProfileHash=" + formatHash64(request.productKey.targetProfileHash));
            appendLine(bytes,
                       "productKeyHash=" + formatHash64(hashAssetProductKey(request.productKey)));
            appendLine(bytes, "settings.count=" + std::to_string(request.settings.size()));
            for (const AssetImportSetting& setting : request.settings) {
                appendLine(bytes, "setting." + setting.key + "=" + setting.value);
            }
            appendLine(bytes, "amat.size=" + std::to_string(canonicalBytes.size()));
            appendLine(bytes, "amatHash=" + formatHash64(hashBytes(canonicalBytes)));
            appendLine(bytes, "amat.begin");
            bytes.insert(bytes.end(), canonicalBytes.begin(), canonicalBytes.end());
            appendLine(bytes, "");
            appendLine(bytes, "amat.end");
            return bytes;
        }

        [[nodiscard]] Result<std::vector<std::uint8_t>>
        makeShaderAuthoringProductBytes(const AssetImportRequest& request,
                                        std::span<const std::uint8_t> sourceBytes) {
            std::string sourceText;
            sourceText.reserve(sourceBytes.size());
            for (const std::uint8_t byte : sourceBytes) {
                sourceText.push_back(static_cast<char>(byte));
            }

            shader_authoring::AshaderParseResult parsed = shader_authoring::parseAshaderDocument(
                sourceText,
                shader_authoring::AshaderParseOptions{.sourceName = request.source.sourcePath});
            if (!parsed.document || shader_authoring::hasErrors(parsed.diagnostics)) {
                return std::unexpected{
                    shaderAuthoringImportError("Failed to parse .ashader source: " +
                                               ashaderDiagnosticSummary(parsed.diagnostics))};
            }

            shader_authoring::GeneratedSlangResult generated =
                shader_authoring::buildGeneratedSlang(
                    *parsed.document,
                    shader_authoring::GeneratedSlangOptions{
                        .sourceName = request.source.sourcePath,
                        .generatedName = request.relativeProductPath + ".generated.slang",
                    });
            if (shader_authoring::hasErrors(generated.diagnostics)) {
                return std::unexpected{
                    shaderAuthoringImportError("Failed to generate Slang from .ashader source: " +
                                               ashaderDiagnosticSummary(generated.diagnostics))};
            }

            std::vector<std::uint8_t> generatedBytes;
            generatedBytes.reserve(generated.source.size());
            for (const char value : generated.source) {
                generatedBytes.push_back(
                    static_cast<std::uint8_t>(static_cast<unsigned char>(value)));
            }

            std::vector<std::uint8_t> bytes;
            bytes.reserve(1536 + generatedBytes.size());
            appendLine(bytes, "schema=com.asharia.asset.shader-authoring-product.v1");
            appendLine(bytes, "guid=" + formatAssetGuid(request.source.guid));
            appendLine(bytes, "sourcePath=" + request.source.sourcePath);
            appendLine(bytes, "assetType=" + request.source.assetTypeName);
            appendLine(bytes, "importer=" + request.source.importerName);
            appendLine(bytes,
                       "importerVersion=" + std::to_string(request.source.importerVersion.value));
            appendLine(bytes, "shader.stableTypeId=" + parsed.document->shaderTypeId);
            appendLine(bytes,
                       "ashader.schemaVersion=" + std::to_string(parsed.document->schemaVersion));
            appendLine(bytes, "sourceHash=" + formatHash64(request.source.sourceHash));
            appendLine(bytes, "settingsHash=" + formatHash64(request.source.settingsHash));
            appendLine(bytes, "dependencyHash=" + formatHash64(request.productKey.dependencyHash));
            appendLine(bytes,
                       "targetProfileHash=" + formatHash64(request.productKey.targetProfileHash));
            appendLine(bytes,
                       "productKeyHash=" + formatHash64(hashAssetProductKey(request.productKey)));
            appendLine(bytes, "settings.count=" + std::to_string(request.settings.size()));
            for (const AssetImportSetting& setting : request.settings) {
                appendLine(bytes, "setting." + setting.key + "=" + setting.value);
            }

            appendLine(bytes,
                       "property.count=" + std::to_string(parsed.document->properties.size()));
            for (std::size_t index = 0; index < parsed.document->properties.size(); ++index) {
                const shader_authoring::AshaderPropertyDecl& property =
                    parsed.document->properties[index];
                const std::string prefix = "property." + std::to_string(index) + ".";
                appendLine(bytes, prefix + "name=" + property.name);
                appendLine(bytes, prefix + "type=" + std::string{toString(property.type)});
                appendLine(bytes, prefix + "default=" + property.defaultValue.text);
            }

            appendLine(bytes, "pass.count=" + std::to_string(parsed.document->passes.size()));
            for (std::size_t index = 0; index < parsed.document->passes.size(); ++index) {
                const shader_authoring::AshaderPassDecl& pass = parsed.document->passes[index];
                const std::string prefix = "pass." + std::to_string(index) + ".";
                appendLine(bytes, prefix + "name=" + pass.name);
                appendLine(bytes, prefix + "tag=" + pass.tag.value_or(""));
                appendLine(bytes, prefix + "vertex=" + pass.vertexEntry.value_or(""));
                appendLine(bytes, prefix + "fragment=" + pass.fragmentEntry.value_or(""));
                appendLine(bytes, prefix + "compute=" + pass.computeEntry.value_or(""));
            }

            appendLine(bytes, "binding.count=" + std::to_string(generated.bindings.size()));
            for (std::size_t index = 0; index < generated.bindings.size(); ++index) {
                const shader_authoring::GeneratedSlangBinding& binding = generated.bindings[index];
                const std::string prefix = "binding." + std::to_string(index) + ".";
                appendLine(bytes, prefix + "name=" + binding.name);
                appendLine(bytes, prefix + "type=" + std::string{toString(binding.type)});
                appendLine(bytes, prefix + "set=" + std::to_string(binding.set));
                appendLine(bytes, prefix + "binding=" + std::to_string(binding.binding));
                appendLine(bytes,
                           prefix + "inMaterialParameterBlock=" +
                               std::string{binding.inMaterialParameterBlock ? "true" : "false"});
            }

            appendLine(bytes, "entry.count=" + std::to_string(generated.entryPoints.size()));
            for (std::size_t index = 0; index < generated.entryPoints.size(); ++index) {
                const shader_authoring::GeneratedSlangEntryPoint& entry =
                    generated.entryPoints[index];
                const std::string prefix = "entry." + std::to_string(index) + ".";
                appendLine(bytes, prefix + "passName=" + entry.passName);
                appendLine(bytes, prefix + "stage=" + std::string{toString(entry.stage)});
                appendLine(bytes, prefix + "sourceEntry=" + entry.sourceEntryName);
                appendLine(bytes, prefix + "compileEntry=" + entry.compileEntryName);
                appendLine(bytes, prefix + "generatedWrapper=" + entry.generatedWrapperName);
            }

            appendLine(bytes, "generatedSlang.size=" + std::to_string(generatedBytes.size()));
            appendLine(bytes, "generatedSlangHash=" + formatHash64(hashBytes(generatedBytes)));
            appendLine(bytes, "generatedSlang.begin");
            bytes.insert(bytes.end(), generatedBytes.begin(), generatedBytes.end());
            appendLine(bytes, "");
            appendLine(bytes, "generatedSlang.end");
            return bytes;
        }

        [[nodiscard]] Result<AssetShaderCompileReflectionProductEntry>
        compileShaderAuthoringEntry(const ShaderCompileReflectionWorkPaths& workPaths,
                                    const ShaderCompileReflectionToolPaths& toolPaths,
                                    const AssetShaderAuthoringProductEntry& entry) {
            if (!isSupportedSlangStage(entry.stage)) {
                return std::unexpected{shaderCompileReflectionImportError(
                    "Unsupported Slang stage '" + entry.stage + "'.")};
            }
            if (!isSafeSlangIdentifier(entry.compileEntryName)) {
                return std::unexpected{shaderCompileReflectionImportError(
                    "Unsafe Slang compile entry name '" + entry.compileEntryName + "'.")};
            }

            const std::string entryFileStem = entry.compileEntryName + "." + entry.stage;
            const std::filesystem::path spirvPath = workPaths.workDir / (entryFileStem + ".spv");
            const std::filesystem::path reflectionPath =
                workPaths.workDir / (entryFileStem + ".reflection.json");

            const std::string compileCommand =
                quotePath(toolPaths.slangcPath) + " " + quotePath(workPaths.generatedSourcePath) +
                " -profile " + std::string{kSlangProfile} + " -target " +
                std::string{kSlangTarget} + " -entry " + entry.compileEntryName + " -stage " +
                entry.stage + " -reflection-json " + quotePath(reflectionPath) + " -o " +
                quotePath(spirvPath);
            if (auto compiled = runToolCommand("slangc " + entry.stage, compileCommand);
                !compiled) {
                return std::unexpected{std::move(compiled.error())};
            }

            if (auto validated =
                    runToolCommand("spirv-val " + entry.stage,
                                   quotePath(toolPaths.spirvValPath) + " " + quotePath(spirvPath));
                !validated) {
                return std::unexpected{std::move(validated.error())};
            }

            auto spirvBytes = readBinaryFile(spirvPath);
            if (!spirvBytes) {
                return std::unexpected{std::move(spirvBytes.error())};
            }
            auto reflectionBytes = readBinaryFile(reflectionPath);
            if (!reflectionBytes) {
                return std::unexpected{std::move(reflectionBytes.error())};
            }
            if (spirvBytes->empty()) {
                return std::unexpected{
                    shaderCompileReflectionImportError("slangc produced empty SPIR-V bytes.")};
            }
            if (reflectionBytes->empty()) {
                return std::unexpected{shaderCompileReflectionImportError(
                    "slangc produced empty reflection JSON bytes.")};
            }

            std::string reflectionJsonText;
            reflectionJsonText.reserve(reflectionBytes->size());
            for (const std::uint8_t byte : *reflectionBytes) {
                reflectionJsonText.push_back(static_cast<char>(byte));
            }

            return AssetShaderCompileReflectionProductEntry{
                .passName = entry.passName,
                .stage = entry.stage,
                .sourceEntryName = entry.sourceEntryName,
                .compileEntryName = entry.compileEntryName,
                .generatedWrapperName = entry.generatedWrapperName,
                .spirvHash = hashBytes(*spirvBytes),
                .reflectionJsonHash = hashBytes(*reflectionBytes),
                .spirvBytes = std::move(*spirvBytes),
                .reflectionJsonText = std::move(reflectionJsonText),
            };
        }

        [[nodiscard]] Result<std::vector<AssetShaderCompileReflectionProductEntry>>
        compileShaderAuthoringEntries(const AssetImportRequest& request,
                                      const std::filesystem::path& outputRoot,
                                      const AssetShaderAuthoringProductPayload& authoringPayload) {
            const std::optional<std::filesystem::path> slangcPath = findToolExecutable("slangc");
            if (!slangcPath) {
                return std::unexpected{shaderCompileReflectionImportError(
                    "Could not find required tool 'slangc' on PATH or under VULKAN_SDK/Bin.")};
            }

            const std::optional<std::filesystem::path> spirvValPath =
                findToolExecutable("spirv-val");
            if (!spirvValPath) {
                return std::unexpected{shaderCompileReflectionImportError(
                    "Could not find required tool 'spirv-val' on PATH or under VULKAN_SDK/Bin.")};
            }

            const std::filesystem::path workDir =
                outputRoot / ".asharia-product-work" /
                formatHash64(hashAssetProductKey(request.productKey));
            std::error_code removeError;
            std::filesystem::remove_all(workDir, removeError);
            std::error_code createError;
            std::filesystem::create_directories(workDir, createError);
            if (createError) {
                return std::unexpected{shaderCompileReflectionImportError(
                    "Could not create shader compile work directory '" + pathText(workDir) +
                    "': " + createError.message() + ".")};
            }

            const std::filesystem::path generatedSourcePath = workDir / "generated-authoring.slang";
            if (!writeTextFile(generatedSourcePath, authoringPayload.generatedSlangText)) {
                return std::unexpected{
                    shaderCompileReflectionImportError("Could not write generated Slang source '" +
                                                       pathText(generatedSourcePath) + "'.")};
            }

            std::vector<AssetShaderCompileReflectionProductEntry> compiledEntries;
            compiledEntries.reserve(authoringPayload.entries.size());
            const ShaderCompileReflectionWorkPaths workPaths{
                .workDir = workDir,
                .generatedSourcePath = generatedSourcePath,
            };
            const ShaderCompileReflectionToolPaths toolPaths{
                .slangcPath = *slangcPath,
                .spirvValPath = *spirvValPath,
            };
            for (const AssetShaderAuthoringProductEntry& entry : authoringPayload.entries) {
                auto compiledEntry = compileShaderAuthoringEntry(workPaths, toolPaths, entry);
                if (!compiledEntry) {
                    return std::unexpected{std::move(compiledEntry.error())};
                }
                compiledEntries.push_back(std::move(*compiledEntry));
            }

            std::filesystem::remove_all(workDir, removeError);
            return compiledEntries;
        }

        [[nodiscard]] std::vector<std::uint8_t> makeShaderCompileReflectionProductBytes(
            const AssetImportRequest& request, const AssetProductDependencyBytes& authoringProduct,
            const AssetShaderAuthoringProductPayload& authoringPayload,
            std::span<const AssetShaderCompileReflectionProductEntry> compiledEntries) {
            std::vector<std::uint8_t> bytes;
            bytes.reserve(4096);
            appendLine(bytes, "schema=com.asharia.asset.shader-compile-reflection-product.v1");
            appendLine(bytes, "guid=" + formatAssetGuid(request.source.guid));
            appendLine(bytes, "sourcePath=" + request.source.sourcePath);
            appendLine(bytes, "assetType=" + request.source.assetTypeName);
            appendLine(bytes, "importer=" + request.source.importerName);
            appendLine(bytes,
                       "importerVersion=" + std::to_string(request.source.importerVersion.value));
            appendLine(bytes, "shader.stableTypeId=" + authoringPayload.stableTypeId);
            appendLine(bytes, "authoringProductPath=" + authoringProduct.relativeProductPath);
            appendLine(bytes, "authoringProductHash=" + formatHash64(authoringProduct.productHash));
            appendLine(bytes,
                       "generatedSlangHash=" + formatHash64(authoringPayload.generatedSlangHash));
            appendLine(bytes, "profile=" + std::string{kSlangProfile});
            appendLine(bytes, "target=" + std::string{kSlangTarget});
            appendLine(bytes, "sourceHash=" + formatHash64(request.source.sourceHash));
            appendLine(bytes, "settingsHash=" + formatHash64(request.source.settingsHash));
            appendLine(bytes, "dependencyHash=" + formatHash64(request.productKey.dependencyHash));
            appendLine(bytes,
                       "targetProfileHash=" + formatHash64(request.productKey.targetProfileHash));
            appendLine(bytes,
                       "productKeyHash=" + formatHash64(hashAssetProductKey(request.productKey)));
            appendLine(bytes, "settings.count=" + std::to_string(request.settings.size()));
            for (const AssetImportSetting& setting : request.settings) {
                appendLine(bytes, "setting." + setting.key + "=" + setting.value);
            }

            appendLine(bytes, "entry.count=" + std::to_string(compiledEntries.size()));
            for (std::size_t index = 0; index < compiledEntries.size(); ++index) {
                const AssetShaderCompileReflectionProductEntry& entry = compiledEntries[index];
                const std::vector<std::uint8_t> reflectionBytes =
                    bytesFromString(entry.reflectionJsonText);
                const std::string prefix = "entry." + std::to_string(index) + ".";
                appendLine(bytes, prefix + "passName=" + entry.passName);
                appendLine(bytes, prefix + "stage=" + entry.stage);
                appendLine(bytes, prefix + "sourceEntry=" + entry.sourceEntryName);
                appendLine(bytes, prefix + "compileEntry=" + entry.compileEntryName);
                appendLine(bytes, prefix + "generatedWrapper=" + entry.generatedWrapperName);
                appendLine(bytes, prefix + "spirvHash=" + formatHash64(entry.spirvHash));
                appendLine(bytes, prefix + "spirvSize=" + std::to_string(entry.spirvBytes.size()));
                appendLine(bytes, prefix + "spirvHex=" + hexEncode(entry.spirvBytes));
                appendLine(bytes,
                           prefix + "reflectionJsonHash=" + formatHash64(entry.reflectionJsonHash));
                appendLine(bytes,
                           prefix + "reflectionJsonSize=" + std::to_string(reflectionBytes.size()));
                appendLine(bytes, prefix + "reflectionJsonHex=" + hexEncode(reflectionBytes));
            }
            return bytes;
        }

        [[nodiscard]] Result<std::vector<std::uint8_t>> makeShaderCompileReflectionProductBytes(
            const AssetImportRequest& request, const std::filesystem::path& outputRoot,
            std::span<const AssetProductDependencyBytes> dependencyProductBytes) {
            const AssetImportSetting* authoringProductPathSetting =
                findImportSetting(request.settings, kShaderAuthoringProductPathSettingKey);
            if (authoringProductPathSetting == nullptr) {
                return std::unexpected{shaderCompileReflectionImportError(
                    "Missing required setting '" +
                    std::string{kShaderAuthoringProductPathSettingKey} + "'.")};
            }
            if (auto validPath = validateAssetProductPath(authoringProductPathSetting->value);
                !validPath) {
                return std::unexpected{shaderCompileReflectionImportError(
                    "Invalid shader authoring product dependency path '" +
                    authoringProductPathSetting->value + "': " + validPath.error().message)};
            }

            const AssetProductDependencyBytes* authoringProduct = findDependencyProductBytes(
                dependencyProductBytes, authoringProductPathSetting->value);
            if (authoringProduct == nullptr) {
                return std::unexpected{shaderCompileReflectionImportError(
                    "Missing dependency product bytes for shader authoring product '" +
                    authoringProductPathSetting->value + "'.")};
            }

            auto authoringPayload = readShaderAuthoringProductPayload(
                std::span<const std::uint8_t>{authoringProduct->bytes.data(),
                                              authoringProduct->bytes.size()},
                authoringProduct->relativeProductPath);
            if (!authoringPayload) {
                return std::unexpected{shaderCompileReflectionImportError(
                    "Could not read shader authoring dependency product '" +
                    authoringProduct->relativeProductPath +
                    "': " + authoringPayload.error().message)};
            }
            if (authoringPayload->sourcePath != request.source.sourcePath) {
                return std::unexpected{shaderCompileReflectionImportError(
                    "Shader authoring dependency product source path '" +
                    authoringPayload->sourcePath + "' does not match request source path '" +
                    request.source.sourcePath + "'.")};
            }

            auto compiledEntries =
                compileShaderAuthoringEntries(request, outputRoot, *authoringPayload);
            if (!compiledEntries) {
                return std::unexpected{std::move(compiledEntries.error())};
            }

            return makeShaderCompileReflectionProductBytes(request, *authoringProduct,
                                                           *authoringPayload, *compiledEntries);
        }

        [[nodiscard]] std::filesystem::path makeOutputPath(const std::filesystem::path& outputRoot,
                                                           std::string_view productPath) {
            return outputRoot / pathFromUtf8(productPath);
        }

        [[nodiscard]] bool productOrdersBefore(const AssetProductRecord& left,
                                               const AssetProductRecord& right) {
            if (left.relativeProductPath != right.relativeProductPath) {
                return left.relativeProductPath < right.relativeProductPath;
            }
            return hashAssetProductKey(left.key) < hashAssetProductKey(right.key);
        }

        [[nodiscard]] std::optional<PreparedProduct>
        prepareProduct(AssetProductExecutionResult& result, const std::filesystem::path& outputRoot,
                       const AssetImportRequest& importRequest,
                       std::span<const AssetProductSourceBytes> sourceBytesList,
                       std::span<const AssetProductDependencyBytes> dependencyProductBytes) {
            const AssetProductSourceBytes* sourceBytes =
                findSourceBytes(sourceBytesList, importRequest.source.sourcePath);
            if (sourceBytes == nullptr) {
                addRequestDiagnostic(
                    result, AssetProductExecutionDiagnosticCode::MissingSourceBytes, importRequest,
                    "Asset product execution could not find explicit source bytes for " +
                        productLabel(importRequest) + ".");
                return std::nullopt;
            }

            const std::uint64_t actualSourceHash = hashBytes(sourceBytes->bytes);
            if (actualSourceHash != importRequest.source.sourceHash) {
                addRequestDiagnostic(result,
                                     AssetProductExecutionDiagnosticCode::SourceBytesHashMismatch,
                                     importRequest,
                                     "Asset product execution source bytes hash mismatch for " +
                                         productLabel(importRequest) + " expected=\"" +
                                         formatHash64(importRequest.source.sourceHash) +
                                         "\" actual=\"" + formatHash64(actualSourceHash) + "\".");
                return std::nullopt;
            }

            if (auto validPath = validateAssetProductPath(importRequest.relativeProductPath);
                !validPath) {
                addRequestDiagnostic(
                    result, AssetProductExecutionDiagnosticCode::InvalidProductPath, importRequest,
                    "Asset product execution rejected product path for " +
                        productLabel(importRequest) + ": " + validPath.error().message);
                return std::nullopt;
            }

            std::vector<std::uint8_t> productBytes;
            if (isPngTextureProductRequest(importRequest)) {
                auto texture = importTextureCpuPayload(AssetTextureImportRequest{
                    .source = importRequest.source,
                    .settings = importRequest.settings,
                    .sourceBytes = sourceBytes->bytes,
                    .importer = makePngTextureImporterDescriptor(),
                });
                if (!texture) {
                    addRequestDiagnostic(result,
                                         AssetProductExecutionDiagnosticCode::TextureImportFailed,
                                         importRequest,
                                         "Asset product execution texture import failed for " +
                                             productLabel(importRequest) + " diagnostic=\"" +
                                             textureImportDiagnosticLabel(texture.error()) +
                                             "\": " + texture.error().message);
                    return std::nullopt;
                }

                productBytes = makeTexture2DProductBytes(importRequest, *texture);
            } else if (isMaterialInstanceProductRequest(importRequest)) {
                auto materialProductBytes =
                    makeMaterialInstanceProductBytes(importRequest, sourceBytes->bytes);
                if (!materialProductBytes) {
                    addRequestDiagnostic(
                        result, AssetProductExecutionDiagnosticCode::MaterialInstanceImportFailed,
                        importRequest,
                        "Asset product execution material instance import failed for " +
                            productLabel(importRequest) + ": " +
                            materialProductBytes.error().message);
                    return std::nullopt;
                }

                productBytes = std::move(*materialProductBytes);
            } else if (isShaderAuthoringProductRequest(importRequest)) {
                auto shaderProductBytes =
                    makeShaderAuthoringProductBytes(importRequest, sourceBytes->bytes);
                if (!shaderProductBytes) {
                    addRequestDiagnostic(
                        result, AssetProductExecutionDiagnosticCode::ShaderAuthoringImportFailed,
                        importRequest,
                        "Asset product execution shader authoring import failed for " +
                            productLabel(importRequest) + ": " +
                            shaderProductBytes.error().message);
                    return std::nullopt;
                }

                productBytes = std::move(*shaderProductBytes);
            } else if (isShaderCompileReflectionProductRequest(importRequest)) {
                auto shaderProductBytes = makeShaderCompileReflectionProductBytes(
                    importRequest, outputRoot, dependencyProductBytes);
                if (!shaderProductBytes) {
                    addRequestDiagnostic(
                        result,
                        AssetProductExecutionDiagnosticCode::ShaderCompileReflectionImportFailed,
                        importRequest,
                        "Asset product execution shader compile/reflection import failed for " +
                            productLabel(importRequest) + ": " +
                            shaderProductBytes.error().message);
                    return std::nullopt;
                }

                productBytes = std::move(*shaderProductBytes);
            } else {
                productBytes = makePlaceholderProductBytes(importRequest, sourceBytes->bytes);
            }

            AssetProductRecord product{
                .key = importRequest.productKey,
                .relativeProductPath = importRequest.relativeProductPath,
                .productSizeBytes = static_cast<std::uint64_t>(productBytes.size()),
                .productHash = hashBytes(productBytes),
            };

            return PreparedProduct{
                .request = &importRequest,
                .product = std::move(product),
                .outputPath = makeOutputPath(outputRoot, importRequest.relativeProductPath),
                .bytes = std::move(productBytes),
            };
        }

        [[nodiscard]] bool writeProductFile(AssetProductExecutionResult& result,
                                            const PreparedProduct& product) {
            const std::filesystem::path parent = product.outputPath.parent_path();
            if (!parent.empty()) {
                std::error_code createError;
                std::filesystem::create_directories(parent, createError);
                if (createError) {
                    addRequestDiagnostic(
                        result, AssetProductExecutionDiagnosticCode::ProductWriteFailed,
                        *product.request,
                        "Asset product execution could not create product output directory '" +
                            pathText(parent) + "': " + createError.message() + ".");
                    return false;
                }
            }

            std::ofstream file(product.outputPath, std::ios::binary);
            if (!file) {
                addRequestDiagnostic(
                    result, AssetProductExecutionDiagnosticCode::ProductWriteFailed,
                    *product.request,
                    "Asset product execution could not open product output file '" +
                        pathText(product.outputPath) + "'.");
                return false;
            }

            for (const std::uint8_t byte : product.bytes) {
                file.put(static_cast<char>(byte));
                if (!file) {
                    addRequestDiagnostic(
                        result, AssetProductExecutionDiagnosticCode::ProductWriteFailed,
                        *product.request,
                        "Asset product execution could not write product output file '" +
                            pathText(product.outputPath) + "'.");
                    return false;
                }
            }

            return true;
        }

        [[nodiscard]] bool writeManifestFile(AssetProductExecutionResult& result,
                                             const AssetProductExecutionRequest& request) {
            if (request.productManifestOutputPath.empty()) {
                return true;
            }

            const std::filesystem::path parent = request.productManifestOutputPath.parent_path();
            if (!parent.empty()) {
                std::error_code createError;
                std::filesystem::create_directories(parent, createError);
                if (createError) {
                    addDiagnostic(result, AssetProductExecutionDiagnosticCode::ManifestWriteFailed,
                                  {}, {},
                                  "Asset product execution could not create product manifest "
                                  "directory '" +
                                      pathText(parent) + "': " + createError.message() + ".");
                    return false;
                }
            }

            if (auto written = writeAssetProductManifestFile(request.productManifestOutputPath,
                                                             result.manifest);
                !written) {
                addDiagnostic(result, AssetProductExecutionDiagnosticCode::ManifestWriteFailed, {},
                              {},
                              "Asset product execution could not write product manifest '" +
                                  pathText(request.productManifestOutputPath) +
                                  "': " + written.error().message);
                return false;
            }

            result.manifestWritten = true;
            return true;
        }

    } // namespace

    AssetProductExecutionResult executeAssetProducts(const AssetProductExecutionRequest& request) {
        AssetProductExecutionResult result{
            .targetProfile = request.plan.targetProfile,
            .targetProfileHash = request.plan.targetProfileHash,
            .writtenProducts = {},
            .cacheHits = request.plan.cacheHits,
            .manifest = request.existingManifest,
            .diagnostics = {},
            .manifestWritten = false,
        };

        if (request.productOutputRoot.empty()) {
            addDiagnostic(result, AssetProductExecutionDiagnosticCode::InvalidOutputRoot, {}, {},
                          "Asset product execution requires a product output root.");
            return result;
        }

        if (!request.plan.succeeded()) {
            for (const AssetImportPlanDiagnostic& diagnostic : request.plan.diagnostics) {
                addDiagnostic(result, AssetProductExecutionDiagnosticCode::InvalidPlan,
                              diagnostic.sourcePath, {},
                              "Asset product execution rejected import plan diagnostic: " +
                                  diagnostic.message);
            }
            return result;
        }

        if (auto validManifest = validateAssetProductManifestDocument(request.existingManifest);
            !validManifest) {
            addDiagnostic(result, AssetProductExecutionDiagnosticCode::InvalidProductManifest, {},
                          {},
                          "Asset product execution rejected existing manifest: " +
                              validManifest.error().message);
            return result;
        }

        if (!validateSourceBytes(result, request.sourceBytes)) {
            return result;
        }

        if (!validateDependencyProductBytes(result, request.dependencyProductBytes)) {
            return result;
        }

        std::vector<PreparedProduct> preparedProducts;
        preparedProducts.reserve(request.plan.requests.size());
        for (const AssetImportRequest& importRequest : request.plan.requests) {
            if (auto product = prepareProduct(result, request.productOutputRoot, importRequest,
                                              request.sourceBytes, request.dependencyProductBytes);
                product) {
                preparedProducts.push_back(std::move(*product));
            }
        }

        if (!result.diagnostics.empty()) {
            return result;
        }

        for (const PreparedProduct& product : preparedProducts) {
            result.manifest.products.push_back(product.product);
        }
        std::ranges::sort(result.manifest.products, productOrdersBefore);
        if (auto validManifest = validateAssetProductManifestDocument(result.manifest);
            !validManifest) {
            addDiagnostic(result, AssetProductExecutionDiagnosticCode::InvalidProductManifest, {},
                          {},
                          "Asset product execution produced invalid product manifest: " +
                              validManifest.error().message);
            return result;
        }

        for (const PreparedProduct& product : preparedProducts) {
            if (!writeProductFile(result, product)) {
                return result;
            }
            result.writtenProducts.push_back(AssetProductWrite{
                .source = product.request->source,
                .product = product.product,
                .productFilePath = product.outputPath,
            });
        }

        (void)writeManifestFile(result, request);
        return result;
    }

} // namespace asharia::asset
