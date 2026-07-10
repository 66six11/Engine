#include "asset_product_publication.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cwctype>
#include <expected>
#include <filesystem>
#include <limits>
#include <optional>
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

        [[nodiscard]] std::string formatHash64(std::uint64_t value) {
            constexpr std::string_view kHexDigits = "0123456789abcdef";
            std::string text(16, '0');
            for (std::size_t index = 0; index < text.size(); ++index) {
                const auto shift = static_cast<std::uint32_t>((text.size() - index - 1U) * 4U);
                text[index] = kHexDigits[(value >> shift) & 0xFU];
            }
            return text;
        }

        [[nodiscard]] Error
        publicationError(AssetProductExecutionDiagnosticCode code, std::string_view phase,
                         const std::filesystem::path& stagingPath,
                         const std::filesystem::path& finalPath, std::string_view reason,
                         bool manifestCommitted = false,
                         const AssetProductPublicationItem* item = nullptr,
                         std::optional<std::size_t> productIndex = std::nullopt) {
            std::string message = "Asset product publication failed phase=" + std::string{phase} +
                                  " stagingPath=\"" + pathText(stagingPath) + "\" finalPath=\"" +
                                  pathText(finalPath) +
                                  "\" manifestCommitted=" + (manifestCommitted ? "true" : "false");
            if (item != nullptr && productIndex) {
                message += " productIndex=\"" + std::to_string(*productIndex) + "\" sourcePath=\"" +
                           item->source.sourcePath + "\" relativeProductPath=\"" +
                           item->product.relativeProductPath + "\" productKeyHash=\"" +
                           formatHash64(hashAssetProductKey(item->product.key)) +
                           "\" productHash=\"" + formatHash64(item->product.productHash) + "\"";
            }
            message += ": " + std::string{reason};
            return Error{ErrorDomain::Asset, static_cast<int>(code), std::move(message)};
        }

        [[nodiscard]] Error
        productPublicationError(AssetProductPublicationResult& outcome, std::size_t productIndex,
                                std::string_view phase, const std::filesystem::path& stagingPath,
                                const AssetProductPublicationItem& item, std::string_view reason) {
            outcome.failingProductIndex = productIndex;
            return publicationError(AssetProductExecutionDiagnosticCode::ProductWriteFailed, phase,
                                    stagingPath, item.finalPath, reason, false, &item,
                                    productIndex);
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

        [[nodiscard]] bool pathComponentEquals(const std::filesystem::path& left,
                                               const std::filesystem::path& right) {
#if defined(_WIN32)
            std::wstring leftText = left.native();
            std::wstring rightText = right.native();
            std::ranges::transform(leftText, leftText.begin(),
                                   [](wchar_t value) { return std::towlower(value); });
            std::ranges::transform(rightText, rightText.begin(),
                                   [](wchar_t value) { return std::towlower(value); });
            return leftText == rightText;
#else
            return left == right;
#endif
        }

        [[nodiscard]] bool endpointEquals(const std::filesystem::path& left,
                                          const std::filesystem::path& right) {
            auto leftComponent = left.begin();
            auto rightComponent = right.begin();
            while (leftComponent != left.end() && rightComponent != right.end()) {
                if (!pathComponentEquals(*leftComponent, *rightComponent)) {
                    return false;
                }
                ++leftComponent;
                ++rightComponent;
            }
            return leftComponent == left.end() && rightComponent == right.end();
        }

        [[nodiscard]] bool isStrictDescendant(const std::filesystem::path& candidate,
                                              const std::filesystem::path& root) {
            auto candidateComponent = candidate.begin();
            for (auto rootComponent = root.begin(); rootComponent != root.end(); ++rootComponent) {
                if (candidateComponent == candidate.end() ||
                    !pathComponentEquals(*candidateComponent, *rootComponent)) {
                    return false;
                }
                ++candidateComponent;
            }
            return candidateComponent != candidate.end();
        }

        [[nodiscard]] Result<std::filesystem::path>
        canonicalEndpoint(const std::filesystem::path& path, std::string_view endpointLabel) {
            std::error_code canonicalError;
            std::filesystem::path canonical =
                std::filesystem::weakly_canonical(path, canonicalError).lexically_normal();
            if (canonicalError) {
                return std::unexpected{Error{ErrorDomain::Asset, 0,
                                             "Could not resolve " + std::string{endpointLabel} +
                                                 " endpoint '" + pathText(path) +
                                                 "': " + canonicalError.message() + "."}};
            }
            return canonical;
        }

        [[nodiscard]] VoidResult
        preflightPublicationEndpoints(const AssetProductPublicationRequest& request,
                                      AssetProductPublicationResult& outcome) {
            auto canonicalRoot = canonicalEndpoint(request.outputRoot, "output root");
            if (!canonicalRoot) {
                return std::unexpected{publicationError(
                    request.products.empty()
                        ? AssetProductExecutionDiagnosticCode::ManifestWriteFailed
                        : AssetProductExecutionDiagnosticCode::ProductWriteFailed,
                    "preflight-output-root", request.outputRoot / ".asharia-product-staging",
                    request.outputRoot, canonicalRoot.error().message)};
            }

            std::vector<std::filesystem::path> canonicalProducts;
            canonicalProducts.reserve(request.products.size());
            for (std::size_t productIndex = 0; productIndex < request.products.size();
                 ++productIndex) {
                const AssetProductPublicationItem& item = request.products[productIndex];
                auto canonicalProduct = canonicalEndpoint(item.finalPath, "product final");
                if (!canonicalProduct) {
                    return std::unexpected{
                        productPublicationError(outcome, productIndex, "preflight-product-endpoint",
                                                request.outputRoot / ".asharia-product-staging",
                                                item, canonicalProduct.error().message)};
                }
                if (!isStrictDescendant(*canonicalProduct, *canonicalRoot)) {
                    return std::unexpected{productPublicationError(
                        outcome, productIndex, "preflight-product-containment",
                        request.outputRoot / ".asharia-product-staging", item,
                        "product final endpoint is not a descendant of the output root")};
                }
                for (std::size_t priorIndex = 0; priorIndex < canonicalProducts.size();
                     ++priorIndex) {
                    if (endpointEquals(*canonicalProduct, canonicalProducts[priorIndex])) {
                        return std::unexpected{productPublicationError(
                            outcome, productIndex, "preflight-product-alias",
                            request.outputRoot / ".asharia-product-staging", item,
                            "product final endpoint aliases product index " +
                                std::to_string(priorIndex))};
                    }
                }
                canonicalProducts.push_back(std::move(*canonicalProduct));
            }

            if (request.manifestPath.empty()) {
                return {};
            }

            // A caller-owned manifest endpoint may intentionally live outside outputRoot. It is
            // still resolved under platform filesystem semantics and must not alias a product.
            auto canonicalManifest = canonicalEndpoint(request.manifestPath, "manifest final");
            if (!canonicalManifest) {
                return std::unexpected{publicationError(
                    AssetProductExecutionDiagnosticCode::ManifestWriteFailed,
                    "preflight-manifest-endpoint", request.outputRoot / ".asharia-product-staging",
                    request.manifestPath, canonicalManifest.error().message)};
            }
            for (const std::filesystem::path& canonicalProduct : canonicalProducts) {
                if (endpointEquals(*canonicalManifest, canonicalProduct)) {
                    return std::unexpected{publicationError(
                        AssetProductExecutionDiagnosticCode::ManifestWriteFailed,
                        "preflight-manifest-product-alias",
                        request.outputRoot / ".asharia-product-staging", request.manifestPath,
                        "manifest final endpoint aliases a product final endpoint")};
                }
            }
            return {};
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
                      AssetProductPublicationOperations& operations,
                      AssetProductPublicationResult& outcome) {
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
                        productPublicationError(outcome, productIndex, "write-product-staging",
                                                stagingPath, item, written.error().message)};
                }

                auto stagedBytes = operations.readFileBytes(
                    stagingPath, core::FileReadLimits{
                                     .maxBytes = boundedReadLimit(item.product.productSizeBytes),
                                 });
                if (!stagedBytes) {
                    return std::unexpected{
                        productPublicationError(outcome, productIndex, "read-product-staging",
                                                stagingPath, item, stagedBytes.error().message)};
                }
                if (stagedBytes->size() != item.product.productSizeBytes) {
                    return std::unexpected{productPublicationError(
                        outcome, productIndex, "validate-product-staging", stagingPath, item,
                        "staged product size mismatch expected=\"" +
                            std::to_string(item.product.productSizeBytes) + "\" actual=\"" +
                            std::to_string(stagedBytes->size()) + "\"")};
                }
                if (hashBytes(*stagedBytes) != item.product.productHash) {
                    return std::unexpected{
                        productPublicationError(outcome, productIndex, "validate-product-staging",
                                                stagingPath, item, "staged product hash mismatch")};
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

        [[nodiscard]] VoidResult publishProducts(std::span<const VerifiedProduct> products,
                                                 AssetProductPublicationOperations& operations,
                                                 AssetProductPublicationResult& outcome) {
            outcome.writes.reserve(products.size());
            for (std::size_t productIndex = 0; productIndex < products.size(); ++productIndex) {
                const VerifiedProduct& product = products[productIndex];
                if (auto published = operations.publishFileAtomically(
                        product.stagingPath, product.item->finalPath, product.bytes);
                    !published) {
                    return std::unexpected{productPublicationError(
                        outcome, productIndex, "publish-product-final", product.stagingPath,
                        *product.item, published.error().message)};
                }
                outcome.writes.push_back(AssetProductWrite{
                    .source = product.item->source,
                    .product = product.item->product,
                    .productFilePath = product.item->finalPath,
                });
            }
            return {};
        }

    } // namespace

    VoidResult publishAssetProducts(const AssetProductPublicationRequest& request,
                                    AssetProductPublicationOperations& operations,
                                    AssetProductPublicationResult& outcome) {
        outcome = {};
        if (auto preflight = preflightPublicationEndpoints(request, outcome); !preflight) {
            return preflight;
        }

        auto stagingDirectory = operations.createUniqueStagingDirectory(request.outputRoot);
        if (!stagingDirectory) {
            const bool productPhase = !request.products.empty();
            const std::filesystem::path finalPath =
                productPhase ? request.products.front().finalPath : request.manifestPath;
            if (productPhase) {
                return std::unexpected{productPublicationError(
                    outcome, 0U, "create-staging", request.outputRoot / ".asharia-product-staging",
                    request.products.front(), stagingDirectory.error().message)};
            }
            return std::unexpected{
                publicationError(AssetProductExecutionDiagnosticCode::ManifestWriteFailed,
                                 "create-staging", request.outputRoot / ".asharia-product-staging",
                                 finalPath, stagingDirectory.error().message)};
        }

        auto verifiedProducts = stageProducts(request, *stagingDirectory, operations, outcome);
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

        auto publishedProducts = publishProducts(*verifiedProducts, operations, outcome);
        if (!publishedProducts) {
            return std::unexpected{cleanupAfterFailure(operations, *stagingDirectory,
                                                       std::move(publishedProducts.error()))};
        }

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
            outcome.manifestWritten = true;
        }

        if (auto cleaned = operations.removeStagingDirectory(*stagingDirectory); !cleaned) {
            const bool manifestCommitted = outcome.manifestWritten;
            return std::unexpected{publicationError(
                manifestCommitted ? AssetProductExecutionDiagnosticCode::ManifestWriteFailed
                                  : AssetProductExecutionDiagnosticCode::ProductWriteFailed,
                manifestCommitted ? "cleanup-after-manifest-commit"
                                  : "cleanup-after-products-published",
                *stagingDirectory, request.manifestPath, cleaned.error().message,
                manifestCommitted)};
        }

        return {};
    }

    AssetProductPublicationOperations& assetProductPublicationOperations() {
        static NativeAssetProductPublicationOperations operations;
        return operations;
    }

} // namespace asharia::asset::detail
