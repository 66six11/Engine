#include "asharia/asset_pipeline/asset_product_execution.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <ios>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "asharia/asset_core/asset_guid.hpp"

namespace asharia::asset {
    namespace {

        constexpr std::uint64_t kFnv1a64Offset = 14695981039346656037ULL;
        constexpr std::uint64_t kFnv1a64Prime = 1099511628211ULL;

        struct PreparedProduct {
            const AssetImportRequest* request{};
            AssetProductRecord product;
            std::filesystem::path outputPath;
            std::vector<std::uint8_t> bytes;
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

        [[nodiscard]] std::filesystem::path makeOutputPath(const std::filesystem::path& outputRoot,
                                                           std::string_view productPath) {
            return outputRoot / std::filesystem::path{std::string{productPath}};
        }

        [[nodiscard]] bool productOrdersBefore(const AssetProductRecord& left,
                                               const AssetProductRecord& right) {
            if (left.relativeProductPath != right.relativeProductPath) {
                return left.relativeProductPath < right.relativeProductPath;
            }
            return hashAssetProductKey(left.key) < hashAssetProductKey(right.key);
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
                            parent.generic_string() + "': " + createError.message() + ".");
                    return false;
                }
            }

            std::ofstream file(product.outputPath, std::ios::binary);
            if (!file) {
                addRequestDiagnostic(
                    result, AssetProductExecutionDiagnosticCode::ProductWriteFailed,
                    *product.request,
                    "Asset product execution could not open product output file '" +
                        product.outputPath.generic_string() + "'.");
                return false;
            }

            for (const std::uint8_t byte : product.bytes) {
                file.put(static_cast<char>(byte));
                if (!file) {
                    addRequestDiagnostic(
                        result, AssetProductExecutionDiagnosticCode::ProductWriteFailed,
                        *product.request,
                        "Asset product execution could not write product output file '" +
                            product.outputPath.generic_string() + "'.");
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
                    addDiagnostic(
                        result, AssetProductExecutionDiagnosticCode::ManifestWriteFailed, {}, {},
                        "Asset product execution could not create product manifest "
                        "directory '" +
                            parent.generic_string() + "': " + createError.message() + ".");
                    return false;
                }
            }

            if (auto written = writeAssetProductManifestFile(request.productManifestOutputPath,
                                                             result.manifest);
                !written) {
                addDiagnostic(result, AssetProductExecutionDiagnosticCode::ManifestWriteFailed, {},
                              {},
                              "Asset product execution could not write product manifest '" +
                                  request.productManifestOutputPath.generic_string() +
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

        std::vector<PreparedProduct> preparedProducts;
        preparedProducts.reserve(request.plan.requests.size());
        for (const AssetImportRequest& importRequest : request.plan.requests) {
            const AssetProductSourceBytes* sourceBytes =
                findSourceBytes(request.sourceBytes, importRequest.source.sourcePath);
            if (sourceBytes == nullptr) {
                addRequestDiagnostic(
                    result, AssetProductExecutionDiagnosticCode::MissingSourceBytes, importRequest,
                    "Asset product execution could not find explicit source bytes for " +
                        productLabel(importRequest) + ".");
                continue;
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
                continue;
            }

            if (auto validPath = validateAssetProductPath(importRequest.relativeProductPath);
                !validPath) {
                addRequestDiagnostic(
                    result, AssetProductExecutionDiagnosticCode::InvalidProductPath, importRequest,
                    "Asset product execution rejected product path for " +
                        productLabel(importRequest) + ": " + validPath.error().message);
                continue;
            }

            std::vector<std::uint8_t> productBytes =
                makePlaceholderProductBytes(importRequest, sourceBytes->bytes);
            AssetProductRecord product{
                .key = importRequest.productKey,
                .relativeProductPath = importRequest.relativeProductPath,
                .productSizeBytes = static_cast<std::uint64_t>(productBytes.size()),
                .productHash = hashBytes(productBytes),
            };

            preparedProducts.push_back(PreparedProduct{
                .request = &importRequest,
                .product = std::move(product),
                .outputPath =
                    makeOutputPath(request.productOutputRoot, importRequest.relativeProductPath),
                .bytes = std::move(productBytes),
            });
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
