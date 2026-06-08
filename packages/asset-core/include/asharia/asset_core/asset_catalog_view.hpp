#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "asharia/asset_core/asset_catalog.hpp"
#include "asharia/asset_core/asset_product.hpp"

namespace asharia::asset {

    enum class AssetCatalogProductState : std::uint8_t {
        NotTracked,
        Ready,
        MissingProduct,
        StaleProduct,
        InvalidProduct,
    };

    enum class AssetCatalogDiagnosticSeverity : std::uint8_t {
        Info,
        Warning,
        Error,
    };

    enum class AssetCatalogDiagnosticCode : std::uint8_t {
        MissingProduct,
        StaleProduct,
        InvalidProductRecord,
        SourceMetadata,
    };

    struct AssetCatalogDiagnostic {
        AssetCatalogDiagnosticCode code{AssetCatalogDiagnosticCode::MissingProduct};
        AssetCatalogDiagnosticSeverity severity{AssetCatalogDiagnosticSeverity::Info};
        AssetGuid guid{};
        std::string sourcePath;
        std::string message;

        [[nodiscard]] friend bool operator==(const AssetCatalogDiagnostic&,
                                             const AssetCatalogDiagnostic&) = default;
    };

    struct AssetCatalogSubAssetViewEntry {
        std::string stableId;
        std::string displayName;
        std::string assetRoleName;

        [[nodiscard]] friend bool operator==(const AssetCatalogSubAssetViewEntry&,
                                             const AssetCatalogSubAssetViewEntry&) = default;
    };

    struct AssetCatalogSourceFacet {
        AssetGuid guid{};
        std::string sourcePath;
        std::string importProfileName;
        std::string assetRoleName;
        std::vector<AssetCatalogSubAssetViewEntry> subAssets;
        std::vector<AssetCatalogDiagnostic> diagnostics;

        [[nodiscard]] friend bool operator==(const AssetCatalogSourceFacet&,
                                             const AssetCatalogSourceFacet&) = default;
    };

    struct AssetCatalogViewEntry {
        AssetGuid guid{};
        std::string guidText;
        AssetTypeId assetType{};
        std::string assetTypeName;
        std::string sourcePath;
        std::string displayName;
        std::string extension;
        std::string importProfileName;
        std::string assetRoleName;
        ImporterId importerId{};
        std::string importerName;
        ImporterVersion importerVersion{};
        AssetCatalogProductState productState{AssetCatalogProductState::NotTracked};
        std::size_t currentProductCount{};
        std::size_t staleProductCount{};
        std::vector<AssetCatalogSubAssetViewEntry> subAssets;
        std::vector<AssetCatalogDiagnostic> diagnostics;

        [[nodiscard]] friend bool operator==(const AssetCatalogViewEntry&,
                                             const AssetCatalogViewEntry&) = default;
    };

    struct AssetCatalogView {
        std::vector<AssetCatalogViewEntry> entries;
        std::vector<AssetCatalogDiagnostic> diagnostics;

        [[nodiscard]] friend bool operator==(const AssetCatalogView&,
                                             const AssetCatalogView&) = default;
    };

    struct AssetCatalogViewOptions {
        bool requireProducts{false};
        // Ready product state requires the active view's expected full product keys.
        // Without these keys, product records are visible but cannot prove readiness.
        std::span<const AssetProductKey> expectedProductKeys{};
        std::span<const AssetCatalogSourceFacet> sourceFacets{};
    };

    [[nodiscard]] std::string_view
    assetCatalogProductStateName(AssetCatalogProductState state) noexcept;
    [[nodiscard]] std::string_view
    assetCatalogDiagnosticCodeName(AssetCatalogDiagnosticCode code) noexcept;
    [[nodiscard]] AssetCatalogView
    buildAssetCatalogView(const AssetCatalog& catalog, std::span<const AssetProductRecord> products,
                          AssetCatalogViewOptions options = {});

} // namespace asharia::asset
