#include "asset_product_publication.hpp"

#include <algorithm>
#include <atomic>
#include <climits>
#include <cstddef>
#include <cstdint>
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
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
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

        [[nodiscard]] Error sharedBoundaryPublicationError(
            const AssetProductPublicationRequest& request, AssetProductPublicationResult& outcome,
            std::string_view phase, const std::filesystem::path& stagingPath,
            const std::filesystem::path& finalPath, std::string_view reason) {
            if (request.products.empty()) {
                return publicationError(AssetProductExecutionDiagnosticCode::ManifestWriteFailed,
                                        phase, stagingPath, finalPath, reason);
            }
            constexpr std::size_t kAffectedProductIndex = 0U;
            outcome.failingProductIndex = kAffectedProductIndex;
            return publicationError(AssetProductExecutionDiagnosticCode::ProductWriteFailed, phase,
                                    stagingPath, finalPath, reason, false,
                                    &request.products[kAffectedProductIndex],
                                    kAffectedProductIndex);
        }

        [[nodiscard]] Error cleanupPublicationError(const AssetProductPublicationRequest& request,
                                                    AssetProductPublicationResult& outcome,
                                                    const std::filesystem::path& stagingPath,
                                                    std::string_view reason) {
            if (outcome.manifestWritten || request.products.empty()) {
                return publicationError(
                    AssetProductExecutionDiagnosticCode::ManifestWriteFailed,
                    outcome.manifestWritten ? "cleanup-after-manifest-commit"
                                            : "cleanup-after-products-published",
                    stagingPath, request.manifestPath, reason, outcome.manifestWritten);
            }
            return productPublicationError(outcome, 0U, "cleanup-after-products-published",
                                           stagingPath, request.products.front(), reason);
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

        [[nodiscard]] Result<bool> pathComponentEquals(const std::filesystem::path& left,
                                                       const std::filesystem::path& right) {
#if defined(_WIN32)
            const std::wstring& leftText = left.native();
            const std::wstring& rightText = right.native();
            constexpr auto kMaxOrdinalLength = static_cast<std::size_t>(INT_MAX);
            if (leftText.size() > kMaxOrdinalLength || rightText.size() > kMaxOrdinalLength) {
                return std::unexpected{
                    Error{ErrorDomain::Asset, 0,
                          "Could not compare publication path components: component exceeds the "
                          "Windows ordinal comparison length."}};
            }
            const int comparison =
                CompareStringOrdinal(leftText.data(), static_cast<int>(leftText.size()),
                                     rightText.data(), static_cast<int>(rightText.size()), TRUE);
            if (comparison == 0) {
                const DWORD errorCode = GetLastError();
                return std::unexpected{
                    Error{ErrorDomain::Asset, static_cast<int>(errorCode),
                          "Could not compare publication path components with Windows ordinal "
                          "semantics: " +
                              std::system_category().message(static_cast<int>(errorCode)) + "."}};
            }
            return comparison == CSTR_EQUAL;
#else
            return left == right;
#endif
        }

        [[nodiscard]] Result<bool> endpointEquals(const std::filesystem::path& left,
                                                  const std::filesystem::path& right) {
            auto leftComponent = left.begin();
            auto rightComponent = right.begin();
            while (leftComponent != left.end() && rightComponent != right.end()) {
                auto equal = pathComponentEquals(*leftComponent, *rightComponent);
                if (!equal) {
                    return std::unexpected{std::move(equal.error())};
                }
                if (!*equal) {
                    return false;
                }
                ++leftComponent;
                ++rightComponent;
            }
            return leftComponent == left.end() && rightComponent == right.end();
        }

        [[nodiscard]] Result<bool> isStrictDescendant(const std::filesystem::path& candidate,
                                                      const std::filesystem::path& root) {
            auto candidateComponent = candidate.begin();
            for (auto rootComponent = root.begin(); rootComponent != root.end(); ++rootComponent) {
                if (candidateComponent == candidate.end()) {
                    return false;
                }
                auto equal = pathComponentEquals(*candidateComponent, *rootComponent);
                if (!equal) {
                    return std::unexpected{std::move(equal.error())};
                }
                if (!*equal) {
                    return false;
                }
                ++candidateComponent;
            }
            return candidateComponent != candidate.end();
        }

        [[nodiscard]] Result<bool> isEqualOrDescendant(const std::filesystem::path& candidate,
                                                       const std::filesystem::path& root) {
            auto equal = endpointEquals(candidate, root);
            if (!equal) {
                return std::unexpected{std::move(equal.error())};
            }
            if (*equal) {
                return true;
            }
            return isStrictDescendant(candidate, root);
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

        struct PublicationBoundaries {
            std::filesystem::path outputRoot;
            std::filesystem::path stagingRoot;
        };

        [[nodiscard]] Result<PublicationBoundaries>
        preflightPublicationEndpoints(const AssetProductPublicationRequest& request,
                                      AssetProductPublicationResult& outcome) {
            auto canonicalRoot = canonicalEndpoint(request.outputRoot, "output root");
            if (!canonicalRoot) {
                return std::unexpected{sharedBoundaryPublicationError(
                    request, outcome, "preflight-output-root",
                    request.outputRoot / ".asharia-product-staging", request.outputRoot,
                    canonicalRoot.error().message)};
            }

            auto canonicalStagingRoot =
                canonicalEndpoint(request.outputRoot / ".asharia-product-staging", "staging root");
            if (!canonicalStagingRoot) {
                return std::unexpected{sharedBoundaryPublicationError(
                    request, outcome, "preflight-staging-root",
                    request.outputRoot / ".asharia-product-staging", request.outputRoot,
                    canonicalStagingRoot.error().message)};
            }
            auto stagingContained = isStrictDescendant(*canonicalStagingRoot, *canonicalRoot);
            if (!stagingContained) {
                return std::unexpected{sharedBoundaryPublicationError(
                    request, outcome, "preflight-staging-root", *canonicalStagingRoot,
                    request.outputRoot, stagingContained.error().message)};
            }
            if (!*stagingContained) {
                return std::unexpected{sharedBoundaryPublicationError(
                    request, outcome, "preflight-staging-root", *canonicalStagingRoot,
                    request.outputRoot,
                    "reserved staging root is not a descendant of the output root")};
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
                auto productContained = isStrictDescendant(*canonicalProduct, *canonicalRoot);
                if (!productContained) {
                    return std::unexpected{productPublicationError(
                        outcome, productIndex, "preflight-product-comparison",
                        request.outputRoot / ".asharia-product-staging", item,
                        productContained.error().message)};
                }
                if (!*productContained) {
                    return std::unexpected{productPublicationError(
                        outcome, productIndex, "preflight-product-containment",
                        request.outputRoot / ".asharia-product-staging", item,
                        "product final endpoint is not a descendant of the output root")};
                }
                auto productReserved =
                    isEqualOrDescendant(*canonicalProduct, *canonicalStagingRoot);
                if (!productReserved) {
                    return std::unexpected{productPublicationError(
                        outcome, productIndex, "preflight-product-comparison",
                        request.outputRoot / ".asharia-product-staging", item,
                        productReserved.error().message)};
                }
                if (*productReserved) {
                    return std::unexpected{productPublicationError(
                        outcome, productIndex, "preflight-product-staging-namespace",
                        request.outputRoot / ".asharia-product-staging", item,
                        "product final endpoint overlaps the reserved staging namespace")};
                }
                for (std::size_t priorIndex = 0; priorIndex < canonicalProducts.size();
                     ++priorIndex) {
                    auto duplicate =
                        endpointEquals(*canonicalProduct, canonicalProducts[priorIndex]);
                    if (!duplicate) {
                        return std::unexpected{productPublicationError(
                            outcome, productIndex, "preflight-product-comparison",
                            request.outputRoot / ".asharia-product-staging", item,
                            duplicate.error().message)};
                    }
                    if (*duplicate) {
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
                return PublicationBoundaries{.outputRoot = std::move(*canonicalRoot),
                                             .stagingRoot = std::move(*canonicalStagingRoot)};
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
            auto manifestReserved = isEqualOrDescendant(*canonicalManifest, *canonicalStagingRoot);
            if (!manifestReserved) {
                return std::unexpected{
                    publicationError(AssetProductExecutionDiagnosticCode::ManifestWriteFailed,
                                     "preflight-manifest-comparison",
                                     request.outputRoot / ".asharia-product-staging",
                                     request.manifestPath, manifestReserved.error().message)};
            }
            if (*manifestReserved) {
                return std::unexpected{publicationError(
                    AssetProductExecutionDiagnosticCode::ManifestWriteFailed,
                    "preflight-manifest-staging-namespace",
                    request.outputRoot / ".asharia-product-staging", request.manifestPath,
                    "manifest final endpoint overlaps the reserved staging namespace")};
            }
            for (const std::filesystem::path& canonicalProduct : canonicalProducts) {
                auto aliasesProduct = endpointEquals(*canonicalManifest, canonicalProduct);
                if (!aliasesProduct) {
                    return std::unexpected{
                        publicationError(AssetProductExecutionDiagnosticCode::ManifestWriteFailed,
                                         "preflight-manifest-comparison",
                                         request.outputRoot / ".asharia-product-staging",
                                         request.manifestPath, aliasesProduct.error().message)};
                }
                if (*aliasesProduct) {
                    return std::unexpected{publicationError(
                        AssetProductExecutionDiagnosticCode::ManifestWriteFailed,
                        "preflight-manifest-product-alias",
                        request.outputRoot / ".asharia-product-staging", request.manifestPath,
                        "manifest final endpoint aliases a product final endpoint")};
                }
            }
            return PublicationBoundaries{.outputRoot = std::move(*canonicalRoot),
                                         .stagingRoot = std::move(*canonicalStagingRoot)};
        }

        class NativeAssetProductPublicationOperations final
            : public AssetProductPublicationOperations {
        public:
            [[nodiscard]] Result<std::filesystem::path>
            createUniqueStagingDirectory(const std::filesystem::path& outputRoot) override {
                const std::filesystem::path stagingRoot = outputRoot / ".asharia-product-staging";
                if (auto valid = validateStagingRoot(outputRoot, stagingRoot); !valid) {
                    return std::unexpected{std::move(valid.error())};
                }
                std::error_code createRootError;
                std::filesystem::create_directories(stagingRoot, createRootError);
                if (createRootError) {
                    return std::unexpected{Error{ErrorDomain::Asset, 0,
                                                 "Could not create asset product staging root '" +
                                                     pathText(stagingRoot) +
                                                     "': " + createRootError.message() + "."}};
                }
                auto canonicalStagingRoot = validateStagingRoot(outputRoot, stagingRoot);
                if (!canonicalStagingRoot) {
                    return std::unexpected{std::move(canonicalStagingRoot.error())};
                }

                for (;;) {
                    const std::uint64_t stagingId = nextStagingId_.fetch_add(1U);
                    std::filesystem::path stagingPath =
                        *canonicalStagingRoot /
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
            [[nodiscard]] static Result<std::filesystem::path>
            validateStagingRoot(const std::filesystem::path& outputRoot,
                                const std::filesystem::path& stagingRoot) {
                auto canonicalOutputRoot = canonicalEndpoint(outputRoot, "output root");
                if (!canonicalOutputRoot) {
                    return std::unexpected{std::move(canonicalOutputRoot.error())};
                }
                auto canonicalStagingRoot = canonicalEndpoint(stagingRoot, "staging root");
                if (!canonicalStagingRoot) {
                    return std::unexpected{std::move(canonicalStagingRoot.error())};
                }
                auto contained = isStrictDescendant(*canonicalStagingRoot, *canonicalOutputRoot);
                if (!contained) {
                    return std::unexpected{std::move(contained.error())};
                }
                if (!*contained) {
                    return std::unexpected{Error{ErrorDomain::Asset, 0,
                                                 "Asset product staging root '" +
                                                     pathText(stagingRoot) +
                                                     "' does not resolve beneath output root '" +
                                                     pathText(outputRoot) + "'."}};
                }
                return canonicalStagingRoot;
            }

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
        if (request.products.empty() && request.manifestPath.empty()) {
            return {};
        }
        auto boundaries = preflightPublicationEndpoints(request, outcome);
        if (!boundaries) {
            return std::unexpected{std::move(boundaries.error())};
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

        auto canonicalOwnedStaging = canonicalEndpoint(*stagingDirectory, "owned staging");
        if (!canonicalOwnedStaging) {
            return std::unexpected{sharedBoundaryPublicationError(
                request, outcome, "validate-owned-staging", *stagingDirectory, request.outputRoot,
                canonicalOwnedStaging.error().message)};
        }
        auto ownedByStagingRoot =
            isStrictDescendant(*canonicalOwnedStaging, boundaries->stagingRoot);
        if (!ownedByStagingRoot) {
            return std::unexpected{sharedBoundaryPublicationError(
                request, outcome, "validate-owned-staging", *stagingDirectory, request.outputRoot,
                ownedByStagingRoot.error().message)};
        }
        if (!*ownedByStagingRoot) {
            return std::unexpected{sharedBoundaryPublicationError(
                request, outcome, "validate-owned-staging", *stagingDirectory, request.outputRoot,
                "operations returned a staging path outside the reserved staging root")};
        }
        auto ownedByOutputRoot = isStrictDescendant(*canonicalOwnedStaging, boundaries->outputRoot);
        if (!ownedByOutputRoot) {
            return std::unexpected{sharedBoundaryPublicationError(
                request, outcome, "validate-owned-staging", *stagingDirectory, request.outputRoot,
                ownedByOutputRoot.error().message)};
        }
        if (!*ownedByOutputRoot) {
            return std::unexpected{sharedBoundaryPublicationError(
                request, outcome, "validate-owned-staging", *stagingDirectory, request.outputRoot,
                "operations returned a staging path outside the output root")};
        }
        *stagingDirectory = std::move(*canonicalOwnedStaging);

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
            return std::unexpected{cleanupPublicationError(request, outcome, *stagingDirectory,
                                                           cleaned.error().message)};
        }

        return {};
    }

    AssetProductPublicationOperations& assetProductPublicationOperations() {
        static NativeAssetProductPublicationOperations operations;
        return operations;
    }

} // namespace asharia::asset::detail
