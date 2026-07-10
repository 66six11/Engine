#include "asset_product_publication.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

#include "asharia/asset_pipeline/asset_product_manifest_io.hpp"
#include "asharia/core/error.hpp"

namespace asharia::asset::detail {
    namespace {

        constexpr std::uint64_t kFnv1a64Offset = 14695981039346656037ULL;
        constexpr std::uint64_t kFnv1a64Prime = 1099511628211ULL;

        [[nodiscard]] std::string pathText(const std::filesystem::path& path) {
            const std::u8string text = path.generic_u8string();
            return std::string{text.begin(), text.end()};
        }

        [[nodiscard]] std::uint64_t processId() noexcept {
#if defined(_WIN32)
            return static_cast<std::uint64_t>(_getpid());
#else
            return static_cast<std::uint64_t>(getpid());
#endif
        }

        [[nodiscard]] std::uint64_t hashBytes(std::span<const std::byte> bytes) noexcept {
            std::uint64_t hash = kFnv1a64Offset;
            for (const std::byte byte : bytes) {
                hash ^= static_cast<std::uint8_t>(byte);
                hash *= kFnv1a64Prime;
            }
            return hash;
        }

        [[nodiscard]] Error publicationError(AssetProductExecutionDiagnosticCode code,
                                             std::string_view phase,
                                             const std::filesystem::path& stagingPath,
                                             const std::filesystem::path& finalPath,
                                             std::string_view reason,
                                             bool manifestCommitted = false) {
            return Error{ErrorDomain::Asset, static_cast<int>(code),
                         "Asset product publication failed phase=" + std::string{phase} +
                             " stagingPath=\"" + pathText(stagingPath) + "\" finalPath=\"" +
                             pathText(finalPath) + "\" manifestCommitted=" +
                             (manifestCommitted ? "true" : "false") + ": " + std::string{reason}};
        }

        [[nodiscard]] Error cleanupAfterFailure(AssetProductPublicationOperations& operations,
                                                const std::filesystem::path& stagingPath,
                                                Error error) {
            if (auto cleaned = operations.removeStagingDirectory(stagingPath); !cleaned) {
                error.message += " cleanupPhase=cleanup-after-failure cleanupError=\"" +
                                 cleaned.error().message + "\"";
            }
            return error;
        }

        [[nodiscard]] std::uint64_t boundedReadLimit(std::uint64_t expectedBytes) noexcept {
            return std::max<std::uint64_t>(expectedBytes, 1U);
        }

        class NativeAssetProductPublicationOperations final
            : public AssetProductPublicationOperations {
        public:
            [[nodiscard]] Result<std::filesystem::path>
            createUniqueStagingDirectory(const std::filesystem::path& outputRoot) override {
                const std::filesystem::path stagingRoot = outputRoot / ".asharia-product-staging";
                std::error_code createRootError;
                std::filesystem::create_directories(stagingRoot, createRootError);
                if (createRootError) {
                    return std::unexpected{Error{ErrorDomain::Asset, 0,
                                                 "Could not create asset product staging root '" +
                                                     pathText(stagingRoot) +
                                                     "': " + createRootError.message() + "."}};
                }

                for (;;) {
                    const std::uint64_t stagingId = nextStagingId_.fetch_add(1U);
                    std::filesystem::path stagingPath =
                        stagingRoot /
                        (std::to_string(processId()) + "-" + std::to_string(stagingId));
                    std::error_code createError;
                    if (std::filesystem::create_directory(stagingPath, createError)) {
                        return stagingPath;
                    }
                    if (createError) {
                        return std::unexpected{
                            Error{ErrorDomain::Asset, 0,
                                  "Could not create unique asset product staging directory '" +
                                      pathText(stagingPath) + "': " + createError.message() + "."}};
                    }
                }
            }

            [[nodiscard]] VoidResult
            writeFileAtomically(const std::filesystem::path& path,
                                std::span<const std::byte> bytes) override {
                if (auto parentCreated = createParent(path); !parentCreated) {
                    return parentCreated;
                }
                return core::writeFileBytesAtomically(path, bytes);
            }

            [[nodiscard]] Result<std::vector<std::byte>>
            readFileBytes(const std::filesystem::path& path, core::FileReadLimits limits) override {
                return core::readFileBytes(path, limits);
            }

            [[nodiscard]] VoidResult
            // The package operation contract intentionally names both publication endpoints.
            // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
            publishFileAtomically(const std::filesystem::path& stagingPath,
                                  const std::filesystem::path& finalPath,
                                  std::span<const std::byte> verifiedBytes) override {
                (void)stagingPath;
                return writeFileAtomically(finalPath, verifiedBytes);
            }

            [[nodiscard]] VoidResult
            removeStagingDirectory(const std::filesystem::path& stagingPath) override {
                std::error_code removeError;
                std::filesystem::remove_all(stagingPath, removeError);
                if (removeError) {
                    return std::unexpected{
                        Error{ErrorDomain::Asset, 0,
                              "Could not remove asset product staging directory '" +
                                  pathText(stagingPath) + "': " + removeError.message() + "."}};
                }
                return {};
            }

        private:
            [[nodiscard]] static VoidResult createParent(const std::filesystem::path& path) {
                const std::filesystem::path parent = path.parent_path();
                if (parent.empty()) {
                    return {};
                }
                std::error_code createError;
                std::filesystem::create_directories(parent, createError);
                if (createError) {
                    return std::unexpected{Error{ErrorDomain::Asset, 0,
                                                 "Could not create publication directory '" +
                                                     pathText(parent) +
                                                     "': " + createError.message() + "."}};
                }
                return {};
            }

            std::atomic<std::uint64_t> nextStagingId_{1U};
        };

        struct VerifiedProduct {
            const AssetProductPublicationItem* item{};
            std::filesystem::path stagingPath;
            std::vector<std::byte> bytes;
        };

        [[nodiscard]] Result<std::vector<VerifiedProduct>>
        stageProducts(const AssetProductPublicationRequest& request,
                      const std::filesystem::path& stagingDirectory,
                      AssetProductPublicationOperations& operations) {
            std::vector<VerifiedProduct> verifiedProducts;
            verifiedProducts.reserve(request.products.size());
            for (std::size_t productIndex = 0; productIndex < request.products.size();
                 ++productIndex) {
                const AssetProductPublicationItem& item = request.products[productIndex];
                const std::filesystem::path stagingPath =
                    stagingDirectory / "products" /
                    ("product-" + std::to_string(productIndex) + ".bin");
                const auto sourceBytes = std::as_bytes(
                    std::span<const std::uint8_t>{item.bytes.data(), item.bytes.size()});
                if (auto written = operations.writeFileAtomically(stagingPath, sourceBytes);
                    !written) {
                    return std::unexpected{
                        publicationError(AssetProductExecutionDiagnosticCode::ProductWriteFailed,
                                         "write-product-staging", stagingPath, item.finalPath,
                                         written.error().message)};
                }

                auto stagedBytes = operations.readFileBytes(
                    stagingPath, core::FileReadLimits{
                                     .maxBytes = boundedReadLimit(item.product.productSizeBytes),
                                 });
                if (!stagedBytes) {
                    return std::unexpected{
                        publicationError(AssetProductExecutionDiagnosticCode::ProductWriteFailed,
                                         "read-product-staging", stagingPath, item.finalPath,
                                         stagedBytes.error().message)};
                }
                if (stagedBytes->size() != item.product.productSizeBytes) {
                    return std::unexpected{publicationError(
                        AssetProductExecutionDiagnosticCode::ProductWriteFailed,
                        "validate-product-staging", stagingPath, item.finalPath,
                        "staged product size mismatch expected=\"" +
                            std::to_string(item.product.productSizeBytes) + "\" actual=\"" +
                            std::to_string(stagedBytes->size()) + "\"")};
                }
                if (hashBytes(*stagedBytes) != item.product.productHash) {
                    return std::unexpected{
                        publicationError(AssetProductExecutionDiagnosticCode::ProductWriteFailed,
                                         "validate-product-staging", stagingPath, item.finalPath,
                                         "staged product hash mismatch")};
                }
                verifiedProducts.push_back(VerifiedProduct{
                    .item = &item,
                    .stagingPath = stagingPath,
                    .bytes = std::move(*stagedBytes),
                });
            }
            return verifiedProducts;
        }

        [[nodiscard]] Result<std::vector<std::byte>>
        stageManifest(const AssetProductPublicationRequest& request,
                      const std::filesystem::path& stagedManifestPath,
                      AssetProductPublicationOperations& operations) {
            auto manifestText = writeAssetProductManifestText(request.manifest);
            if (!manifestText) {
                return std::unexpected{
                    publicationError(AssetProductExecutionDiagnosticCode::ManifestWriteFailed,
                                     "serialize-manifest-staging", stagedManifestPath,
                                     request.manifestPath, manifestText.error().message)};
            }
            const auto manifestBytes =
                std::as_bytes(std::span<const char>{manifestText->data(), manifestText->size()});
            if (auto written = operations.writeFileAtomically(stagedManifestPath, manifestBytes);
                !written) {
                return std::unexpected{
                    publicationError(AssetProductExecutionDiagnosticCode::ManifestWriteFailed,
                                     "write-manifest-staging", stagedManifestPath,
                                     request.manifestPath, written.error().message)};
            }
            auto stagedBytes = operations.readFileBytes(
                stagedManifestPath,
                core::FileReadLimits{
                    .maxBytes = boundedReadLimit(static_cast<std::uint64_t>(manifestText->size())),
                });
            if (!stagedBytes) {
                return std::unexpected{
                    publicationError(AssetProductExecutionDiagnosticCode::ManifestWriteFailed,
                                     "read-manifest-staging", stagedManifestPath,
                                     request.manifestPath, stagedBytes.error().message)};
            }

            std::string stagedText(stagedBytes->size(), '\0');
            std::ranges::transform(*stagedBytes, stagedText.begin(),
                                   [](std::byte byte) { return static_cast<char>(byte); });
            auto stagedManifest = readAssetProductManifestText(stagedText);
            if (!stagedManifest) {
                return std::unexpected{
                    publicationError(AssetProductExecutionDiagnosticCode::ManifestWriteFailed,
                                     "validate-manifest-staging", stagedManifestPath,
                                     request.manifestPath, stagedManifest.error().message)};
            }
            if (*stagedManifest != request.manifest) {
                return std::unexpected{
                    publicationError(AssetProductExecutionDiagnosticCode::ManifestWriteFailed,
                                     "validate-manifest-staging", stagedManifestPath,
                                     request.manifestPath, "staged manifest document mismatch")};
            }
            return stagedBytes;
        }

        [[nodiscard]] Result<std::vector<AssetProductWrite>>
        publishProducts(std::span<const VerifiedProduct> products,
                        AssetProductPublicationOperations& operations) {
            std::vector<AssetProductWrite> writes;
            writes.reserve(products.size());
            for (const VerifiedProduct& product : products) {
                if (auto published = operations.publishFileAtomically(
                        product.stagingPath, product.item->finalPath, product.bytes);
                    !published) {
                    return std::unexpected{
                        publicationError(AssetProductExecutionDiagnosticCode::ProductWriteFailed,
                                         "publish-product-final", product.stagingPath,
                                         product.item->finalPath, published.error().message)};
                }
                writes.push_back(AssetProductWrite{
                    .source = product.item->source,
                    .product = product.item->product,
                    .productFilePath = product.item->finalPath,
                });
            }
            return writes;
        }

    } // namespace

    Result<AssetProductPublicationResult>
    publishAssetProducts(const AssetProductPublicationRequest& request,
                         AssetProductPublicationOperations& operations) {
        auto stagingDirectory = operations.createUniqueStagingDirectory(request.outputRoot);
        if (!stagingDirectory) {
            const bool productPhase = !request.products.empty();
            const std::filesystem::path finalPath =
                productPhase ? request.products.front().finalPath : request.manifestPath;
            return std::unexpected{publicationError(
                productPhase ? AssetProductExecutionDiagnosticCode::ProductWriteFailed
                             : AssetProductExecutionDiagnosticCode::ManifestWriteFailed,
                "create-staging", request.outputRoot / ".asharia-product-staging", finalPath,
                stagingDirectory.error().message)};
        }

        auto verifiedProducts = stageProducts(request, *stagingDirectory, operations);
        if (!verifiedProducts) {
            return std::unexpected{cleanupAfterFailure(operations, *stagingDirectory,
                                                       std::move(verifiedProducts.error()))};
        }

        std::filesystem::path stagedManifestPath;
        std::vector<std::byte> verifiedManifestBytes;
        if (!request.manifestPath.empty()) {
            stagedManifestPath = *stagingDirectory / "manifest.json";
            auto stagedManifest = stageManifest(request, stagedManifestPath, operations);
            if (!stagedManifest) {
                return std::unexpected{cleanupAfterFailure(operations, *stagingDirectory,
                                                           std::move(stagedManifest.error()))};
            }
            verifiedManifestBytes = std::move(*stagedManifest);
        }

        AssetProductPublicationResult result;
        auto publishedProducts = publishProducts(*verifiedProducts, operations);
        if (!publishedProducts) {
            return std::unexpected{cleanupAfterFailure(operations, *stagingDirectory,
                                                       std::move(publishedProducts.error()))};
        }
        result.writes = std::move(*publishedProducts);

        if (!request.manifestPath.empty()) {
            if (auto published = operations.publishFileAtomically(
                    stagedManifestPath, request.manifestPath, verifiedManifestBytes);
                !published) {
                return std::unexpected{cleanupAfterFailure(
                    operations, *stagingDirectory,
                    publicationError(AssetProductExecutionDiagnosticCode::ManifestWriteFailed,
                                     "publish-manifest-final", stagedManifestPath,
                                     request.manifestPath, published.error().message))};
            }
            result.manifestWritten = true;
        }

        if (auto cleaned = operations.removeStagingDirectory(*stagingDirectory); !cleaned) {
            const bool manifestCommitted = result.manifestWritten;
            return std::unexpected{publicationError(
                manifestCommitted ? AssetProductExecutionDiagnosticCode::ManifestWriteFailed
                                  : AssetProductExecutionDiagnosticCode::ProductWriteFailed,
                manifestCommitted ? "cleanup-after-manifest-commit"
                                  : "cleanup-after-products-published",
                *stagingDirectory, request.manifestPath, cleaned.error().message,
                manifestCommitted)};
        }

        return result;
    }

    AssetProductPublicationOperations& assetProductPublicationOperations() {
        static NativeAssetProductPublicationOperations operations;
        return operations;
    }

} // namespace asharia::asset::detail
