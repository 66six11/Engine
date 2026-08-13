#include "asharia/asset_core/asset_catalog_view.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <map>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace asharia::asset {
    namespace {

        using GuidBytes = std::array<std::uint8_t, 16>;

        struct AssetProductKeyLess {
            [[nodiscard]] bool operator()(const AssetProductKey& lhs,
                                          const AssetProductKey& rhs) const noexcept {
                return std::tie(lhs.guid.bytes, lhs.assetType.value, lhs.importerId.value,
                                lhs.importerVersion.value, lhs.sourceHash, lhs.settingsHash,
                                lhs.dependencyHash, lhs.targetProfileHash) <
                       std::tie(rhs.guid.bytes, rhs.assetType.value, rhs.importerId.value,
                                rhs.importerVersion.value, rhs.sourceHash, rhs.settingsHash,
                                rhs.dependencyHash, rhs.targetProfileHash);
            }
        };

        struct IndexedSourceFacet {
            std::size_t index{};
            const AssetCatalogSourceFacet* facet{};
        };

        struct SourceFacetGroups {
            std::vector<IndexedSourceFacet> wildcard;
            std::map<std::string, std::vector<IndexedSourceFacet>, std::less<>> bySourcePath;
        };

        using ProductsByGuid = std::map<GuidBytes, std::vector<const AssetProductRecord*>>;
        using ExpectedProductKeys = std::set<AssetProductKey, AssetProductKeyLess>;
        using FacetsByGuid = std::map<GuidBytes, SourceFacetGroups>;

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

        [[nodiscard]] bool productMatchesActiveView(const AssetProductRecord& product,
                                                    const ExpectedProductKeys& expectedProductKeys,
                                                    const AssetCatalogViewOptions& options) {
            if (!options.expectedProductKeys.empty()) {
                return expectedProductKeys.contains(product.key);
            }
            return false;
        }

        [[nodiscard]] std::string_view
        staleProductMessage(const AssetCatalogViewOptions& options) noexcept {
            if (!options.expectedProductKeys.empty()) {
                return "Asset catalog source has product records, but none match the active "
                       "target profile and expected product key.";
            }
            return "Asset catalog source has product records, but the active view did not provide "
                   "expected product keys, so no product can be reported as ready.";
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
        sourceMetadataDiagnostic(const SourceAssetRecord& source, std::string_view message) {
            return diagnostic(AssetCatalogDiagnosticCode::SourceMetadata,
                              AssetCatalogDiagnosticSeverity::Warning, source, message);
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

        [[nodiscard]] const AssetCatalogSourceFacet*
        firstSourceFacet(const FacetsByGuid& facetsByGuid, const SourceAssetRecord& source,
                         std::size_t& matchCount) {
            const auto groups = facetsByGuid.find(source.guid.bytes);
            if (groups == facetsByGuid.end()) {
                matchCount = 0U;
                return nullptr;
            }

            const auto exact = groups->second.bySourcePath.find(source.sourcePath);
            const std::span<const IndexedSourceFacet> exactMatches =
                exact == groups->second.bySourcePath.end()
                    ? std::span<const IndexedSourceFacet>{}
                    : std::span<const IndexedSourceFacet>{exact->second};
            const std::span<const IndexedSourceFacet> wildcardMatches{groups->second.wildcard};
            matchCount = exactMatches.size() + wildcardMatches.size();
            if (exactMatches.empty()) {
                return wildcardMatches.empty() ? nullptr : wildcardMatches.front().facet;
            }
            if (wildcardMatches.empty()) {
                return exactMatches.front().facet;
            }
            return exactMatches.front().index < wildcardMatches.front().index
                       ? exactMatches.front().facet
                       : wildcardMatches.front().facet;
        }

        void applySourceFacet(AssetCatalogViewEntry& entry, const AssetCatalogSourceFacet& facet) {
            entry.importProfileName = facet.importProfileName;
            entry.assetRoleName = facet.assetRoleName;
            entry.subAssets = facet.subAssets;
            entry.diagnostics.insert(entry.diagnostics.end(), facet.diagnostics.begin(),
                                     facet.diagnostics.end());
        }

        [[nodiscard]] AssetCatalogViewEntry
        makeEntry(const SourceAssetRecord& source, const ProductsByGuid& productsByGuid,
                  const ExpectedProductKeys& expectedProductKeys, const FacetsByGuid& facetsByGuid,
                  const AssetCatalogViewOptions& options) {
            AssetCatalogViewEntry entry{
                .guid = source.guid,
                .guidText = formatAssetGuid(source.guid),
                .assetType = source.assetType,
                .assetTypeName = source.assetTypeName,
                .sourcePath = source.sourcePath,
                .displayName = displayNameForPath(source.sourcePath),
                .extension = {},
                .importProfileName = {},
                .assetRoleName = {},
                .importerId = source.importerId,
                .importerName = source.importerName,
                .importerVersion = source.importerVersion,
                .productState = AssetCatalogProductState::NotTracked,
                .currentProductCount = 0U,
                .staleProductCount = 0U,
                .subAssets = {},
                .diagnostics = {},
            };
            entry.extension = extensionForName(entry.displayName);

            std::size_t sourceFacetMatchCount = 0U;
            if (const AssetCatalogSourceFacet* facet =
                    firstSourceFacet(facetsByGuid, source, sourceFacetMatchCount)) {
                applySourceFacet(entry, *facet);
            }
            if (sourceFacetMatchCount > 1U) {
                entry.diagnostics.push_back(sourceMetadataDiagnostic(
                    source, "Asset catalog source has multiple matching metadata facets."));
            }

            std::size_t invalidProductCount = 0U;
            const auto sourceProducts = productsByGuid.find(source.guid.bytes);
            const std::span<const AssetProductRecord* const> products =
                sourceProducts == productsByGuid.end()
                    ? std::span<const AssetProductRecord* const>{}
                    : std::span<const AssetProductRecord* const>{sourceProducts->second};
            for (const AssetProductRecord* productPointer : products) {
                const AssetProductRecord& product = *productPointer;
                if (!product) {
                    AssetCatalogDiagnostic invalid = invalidProductDiagnostic(product);
                    ++invalidProductCount;
                    entry.diagnostics.push_back(std::move(invalid));
                    continue;
                }

                if (productMatchesActiveView(product, expectedProductKeys, options)) {
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
                entry.diagnostics.push_back(diagnostic(AssetCatalogDiagnosticCode::StaleProduct,
                                                       AssetCatalogDiagnosticSeverity::Warning,
                                                       source, staleProductMessage(options)));
            } else if (options.requireProducts) {
                entry.productState = AssetCatalogProductState::MissingProduct;
                entry.diagnostics.push_back(
                    diagnostic(AssetCatalogDiagnosticCode::MissingProduct,
                               AssetCatalogDiagnosticSeverity::Warning, source,
                               "Asset catalog source has no product record for the active view."));
            }

            return entry;
        }

        void populateCatalogViewIndex(std::span<const AssetProductRecord> products,
                                      const AssetCatalogViewOptions& options,
                                      ProductsByGuid& productsByGuid,
                                      ExpectedProductKeys& expectedProductKeys,
                                      FacetsByGuid& facetsByGuid) {
            for (const AssetProductRecord& product : products) {
                productsByGuid[product.key.guid.bytes].push_back(&product);
            }
            expectedProductKeys.insert(options.expectedProductKeys.begin(),
                                       options.expectedProductKeys.end());
            for (std::size_t facetIndex = 0U; facetIndex < options.sourceFacets.size();
                 ++facetIndex) {
                const AssetCatalogSourceFacet& facet = options.sourceFacets[facetIndex];
                IndexedSourceFacet indexed{.index = facetIndex, .facet = &facet};
                SourceFacetGroups& groups = facetsByGuid[facet.guid.bytes];
                if (facet.sourcePath.empty()) {
                    groups.wildcard.push_back(indexed);
                } else {
                    groups.bySourcePath[facet.sourcePath].push_back(indexed);
                }
            }
        }

        [[nodiscard]] bool loweredTextLess(std::string_view left, std::string_view right) {
            const std::size_t sharedSize = (std::min)(left.size(), right.size());
            for (std::size_t index = 0U; index < sharedSize; ++index) {
                const char leftCharacter =
                    static_cast<char>(std::tolower(static_cast<unsigned char>(left[index])));
                const char rightCharacter =
                    static_cast<char>(std::tolower(static_cast<unsigned char>(right[index])));
                if (leftCharacter != rightCharacter) {
                    return leftCharacter < rightCharacter;
                }
            }
            return left.size() < right.size();
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
        case AssetCatalogDiagnosticCode::SourceMetadata:
            return "source-metadata";
        }
        return "missing-product";
    }

    AssetCatalogView buildAssetCatalogView(const AssetCatalog& catalog,
                                           std::span<const AssetProductRecord> products,
                                           AssetCatalogViewOptions options) {
        AssetCatalogView view;
        ProductsByGuid productsByGuid;
        ExpectedProductKeys expectedProductKeys;
        FacetsByGuid facetsByGuid;
        populateCatalogViewIndex(products, options, productsByGuid, expectedProductKeys,
                                 facetsByGuid);
        view.entries.reserve(catalog.sources().size());
        for (const SourceAssetRecord& source : catalog.sources()) {
            view.entries.push_back(
                makeEntry(source, productsByGuid, expectedProductKeys, facetsByGuid, options));
        }
        for (const AssetProductRecord& product : products) {
            if (!product) {
                view.diagnostics.push_back(invalidProductDiagnostic(product));
            }
        }

        std::ranges::sort(view.entries, [](const AssetCatalogViewEntry& left,
                                           const AssetCatalogViewEntry& right) {
            if (loweredTextLess(left.sourcePath, right.sourcePath)) {
                return true;
            }
            if (loweredTextLess(right.sourcePath, left.sourcePath)) {
                return false;
            }
            return left.guid.bytes < right.guid.bytes;
        });
        return view;
    }

} // namespace asharia::asset
