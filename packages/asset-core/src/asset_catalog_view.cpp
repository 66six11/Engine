#include "asharia/asset_core/asset_catalog_view.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <utility>

namespace asharia::asset {
    namespace {

        [[nodiscard]] std::string lower(std::string_view value) {
            std::string lowered;
            lowered.reserve(value.size());
            for (const char character : value) {
                lowered.push_back(
                    static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
            }
            return lowered;
        }

        [[nodiscard]] std::string displayNameForPath(std::string_view sourcePath) {
            const std::size_t slash = sourcePath.find_last_of('/');
            const std::string_view name =
                slash == std::string_view::npos ? sourcePath : sourcePath.substr(slash + 1U);
            return std::string{name};
        }

        [[nodiscard]] std::string extensionForName(std::string_view displayName) {
            const std::size_t dot = displayName.find_last_of('.');
            if (dot == std::string_view::npos || dot == 0U || dot + 1U >= displayName.size()) {
                return {};
            }
            return std::string{displayName.substr(dot)};
        }

        [[nodiscard]] bool productBelongsToSource(const AssetProductRecord& product,
                                                  const SourceAssetRecord& source) noexcept {
            return product.key.guid == source.guid;
        }

        [[nodiscard]] bool productMatchesCurrentSource(const AssetProductRecord& product,
                                                       const SourceAssetRecord& source) noexcept {
            return product.key.guid == source.guid && product.key.assetType == source.assetType &&
                   product.key.importerId == source.importerId &&
                   product.key.importerVersion == source.importerVersion &&
                   product.key.sourceHash == source.sourceHash &&
                   product.key.settingsHash == source.settingsHash;
        }

        [[nodiscard]] AssetCatalogDiagnostic diagnostic(AssetCatalogDiagnosticCode code,
                                                        AssetCatalogDiagnosticSeverity severity,
                                                        const SourceAssetRecord& source,
                                                        std::string_view message) {
            return AssetCatalogDiagnostic{
                .code = code,
                .severity = severity,
                .guid = source.guid,
                .sourcePath = source.sourcePath,
                .message = std::string{message},
            };
        }

        [[nodiscard]] AssetCatalogDiagnostic
        invalidProductDiagnostic(const AssetProductRecord& product) {
            return AssetCatalogDiagnostic{
                .code = AssetCatalogDiagnosticCode::InvalidProductRecord,
                .severity = AssetCatalogDiagnosticSeverity::Error,
                .guid = product.key.guid,
                .sourcePath = {},
                .message = "Invalid asset product record guid=\"" +
                           formatAssetGuid(product.key.guid) + "\" path=\"" +
                           product.relativeProductPath + "\".",
            };
        }

        [[nodiscard]] AssetCatalogViewEntry makeEntry(const SourceAssetRecord& source,
                                                      std::span<const AssetProductRecord> products,
                                                      const AssetCatalogViewOptions& options) {
            AssetCatalogViewEntry entry{
                .guid = source.guid,
                .guidText = formatAssetGuid(source.guid),
                .assetType = source.assetType,
                .assetTypeName = source.assetTypeName,
                .sourcePath = source.sourcePath,
                .displayName = displayNameForPath(source.sourcePath),
                .extension = {},
                .importerId = source.importerId,
                .importerName = source.importerName,
                .importerVersion = source.importerVersion,
                .productState = AssetCatalogProductState::NotTracked,
                .currentProductCount = 0U,
                .staleProductCount = 0U,
                .diagnostics = {},
            };
            entry.extension = extensionForName(entry.displayName);

            std::size_t invalidProductCount = 0U;
            for (const AssetProductRecord& product : products) {
                if (!product) {
                    AssetCatalogDiagnostic invalid = invalidProductDiagnostic(product);
                    if (productBelongsToSource(product, source)) {
                        ++invalidProductCount;
                        entry.diagnostics.push_back(std::move(invalid));
                    }
                    continue;
                }

                if (!productBelongsToSource(product, source)) {
                    continue;
                }

                if (productMatchesCurrentSource(product, source)) {
                    ++entry.currentProductCount;
                } else {
                    ++entry.staleProductCount;
                }
            }

            if (entry.currentProductCount > 0U) {
                entry.productState = AssetCatalogProductState::Ready;
            } else if (invalidProductCount > 0U) {
                entry.productState = AssetCatalogProductState::InvalidProduct;
            } else if (entry.staleProductCount > 0U) {
                entry.productState = AssetCatalogProductState::StaleProduct;
                entry.diagnostics.push_back(diagnostic(
                    AssetCatalogDiagnosticCode::StaleProduct,
                    AssetCatalogDiagnosticSeverity::Warning, source,
                    "Asset catalog source has product records, but none match the current source "
                    "or settings hash."));
            } else if (options.requireProducts) {
                entry.productState = AssetCatalogProductState::MissingProduct;
                entry.diagnostics.push_back(
                    diagnostic(AssetCatalogDiagnosticCode::MissingProduct,
                               AssetCatalogDiagnosticSeverity::Warning, source,
                               "Asset catalog source has no product record for the active view."));
            }

            return entry;
        }

    } // namespace

    std::string_view assetCatalogProductStateName(AssetCatalogProductState state) noexcept {
        switch (state) {
        case AssetCatalogProductState::NotTracked:
            return "not-tracked";
        case AssetCatalogProductState::Ready:
            return "ready";
        case AssetCatalogProductState::MissingProduct:
            return "missing-product";
        case AssetCatalogProductState::StaleProduct:
            return "stale-product";
        case AssetCatalogProductState::InvalidProduct:
            return "invalid-product";
        }
        return "not-tracked";
    }

    std::string_view assetCatalogDiagnosticCodeName(AssetCatalogDiagnosticCode code) noexcept {
        switch (code) {
        case AssetCatalogDiagnosticCode::MissingProduct:
            return "missing-product";
        case AssetCatalogDiagnosticCode::StaleProduct:
            return "stale-product";
        case AssetCatalogDiagnosticCode::InvalidProductRecord:
            return "invalid-product-record";
        }
        return "missing-product";
    }

    AssetCatalogView buildAssetCatalogView(const AssetCatalog& catalog,
                                           std::span<const AssetProductRecord> products,
                                           AssetCatalogViewOptions options) {
        AssetCatalogView view;
        view.entries.reserve(catalog.sources().size());
        for (const SourceAssetRecord& source : catalog.sources()) {
            view.entries.push_back(makeEntry(source, products, options));
        }
        for (const AssetProductRecord& product : products) {
            if (!product) {
                view.diagnostics.push_back(invalidProductDiagnostic(product));
            }
        }

        std::ranges::sort(view.entries, [](const AssetCatalogViewEntry& left,
                                           const AssetCatalogViewEntry& right) {
            const std::string leftPath = lower(left.sourcePath);
            const std::string rightPath = lower(right.sourcePath);
            if (leftPath != rightPath) {
                return leftPath < rightPath;
            }
            return formatAssetGuid(left.guid) < formatAssetGuid(right.guid);
        });
        return view;
    }

} // namespace asharia::asset
