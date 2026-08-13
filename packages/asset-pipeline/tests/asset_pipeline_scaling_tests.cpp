#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "asharia/asset_core/asset_catalog.hpp"
#include "asharia/asset_core/asset_catalog_view.hpp"
#include "asharia/asset_core/asset_metadata.hpp"
#include "asharia/asset_core/asset_product.hpp"
#include "asharia/asset_core/asset_type.hpp"
#include "asharia/asset_pipeline/asset_import_planning.hpp"
#include "asharia/asset_pipeline/asset_source_discovery.hpp"
#include "asharia/asset_pipeline/asset_source_snapshot.hpp"

namespace {

    constexpr std::size_t kAssetCount = 12'000U;
    constexpr std::string_view kAssetTypeName = "com.asharia.asset.ScaleFixture";
    constexpr std::string_view kImporterName = "com.asharia.importer.scale-fixture";
    constexpr std::string_view kTargetProfile = "scale-test";

    template <std::size_t... ByteIndices>
    [[nodiscard]] asharia::asset::AssetGuid
    guidForValue(std::uint64_t value, std::index_sequence<ByteIndices...> indices) {
        static_cast<void>(indices);
        asharia::asset::AssetGuid guid{};
        ((guid.bytes[ByteIndices] =
              static_cast<std::uint8_t>((value >> (ByteIndices * 8U)) & 0xFFU)),
         ...);
        return guid;
    }

    [[nodiscard]] asharia::asset::AssetGuid guidFor(std::size_t index) {
        return guidForValue(static_cast<std::uint64_t>(index) + 1U,
                            std::make_index_sequence<sizeof(std::uint64_t)>{});
    }

    [[nodiscard]] bool smokeIndexedCatalogPlanningScale() {
        const asharia::asset::AssetTypeId assetType =
            asharia::asset::makeAssetTypeId(kAssetTypeName);
        const asharia::asset::ImporterId importer = asharia::asset::makeImporterId(kImporterName);
        const std::uint64_t targetProfile =
            asharia::asset::makeAssetTargetProfileHash(kTargetProfile);
        const std::vector<asharia::asset::AssetImportSetting> settings{};
        const std::uint64_t settingsHash = asharia::asset::hashAssetImportSettings(settings);

        std::vector<asharia::asset::DiscoveredSourceAsset> sources;
        std::vector<asharia::asset::AssetSourceSnapshot> snapshots;
        asharia::asset::AssetProductManifestDocument manifest;
        std::vector<asharia::asset::AssetProductKey> expectedProductKeys;
        sources.reserve(kAssetCount);
        snapshots.reserve(kAssetCount);
        manifest.products.reserve(kAssetCount);
        expectedProductKeys.reserve(kAssetCount);

        asharia::asset::AssetCatalog catalog;
        for (std::size_t index = 0U; index < kAssetCount; ++index) {
            const std::string sourcePath =
                "Content/Scale/Asset-" + std::to_string(index) + ".fixture";
            asharia::asset::SourceAssetRecord source{
                .guid = guidFor(index),
                .assetType = assetType,
                .assetTypeName = std::string{kAssetTypeName},
                .sourcePath = sourcePath,
                .importerId = importer,
                .importerName = std::string{kImporterName},
                .importerVersion = asharia::asset::ImporterVersion{1U},
                .sourceHash = static_cast<std::uint64_t>(index) + 1U,
                .settingsHash = settingsHash,
            };
            const std::array dependencies{
                asharia::asset::AssetDependency{
                    .owner = source.guid,
                    .kind = asharia::asset::AssetDependencyKind::SourceFile,
                    .path = source.sourcePath,
                    .hash = source.sourceHash,
                },
                asharia::asset::AssetDependency{
                    .owner = source.guid,
                    .kind = asharia::asset::AssetDependencyKind::ImportSettings,
                    .path = {},
                    .hash = source.settingsHash,
                },
            };
            const asharia::asset::AssetProductKey productKey = asharia::asset::makeAssetProductKey(
                source, asharia::asset::hashAssetDependencies(dependencies), targetProfile);

            if (!catalog.addSource(source)) {
                std::cerr << "Scaling test could not add a unique source.\n";
                return false;
            }
            sources.push_back(asharia::asset::DiscoveredSourceAsset{
                .entry = {.sourcePath = sourcePath, .metadataPath = {}},
                .source = source,
                .settings = settings,
            });
            snapshots.push_back(asharia::asset::AssetSourceSnapshot{
                .sourcePath = sourcePath,
                .sourceFilePath = sourcePath,
                .sourceHash = source.sourceHash,
            });
            manifest.products.push_back(asharia::asset::AssetProductRecord{
                .key = productKey,
                .relativeProductPath = "scale-test/products/" + std::to_string(index) + ".aproduct",
                .productSizeBytes = 1U,
                .productHash = asharia::asset::hashAssetProductKey(productKey),
            });
            expectedProductKeys.push_back(productKey);
        }

        const asharia::asset::AssetImportPlanResult plan = asharia::asset::planAssetImports(
            sources, snapshots, manifest, kTargetProfile,
            asharia::asset::AssetImportPlanOptions{
                .toolDependencyPolicy =
                    asharia::asset::AssetImportToolDependencyPolicy::DeclaredOnly,
            });
        const asharia::asset::AssetCatalogView view =
            asharia::asset::buildAssetCatalogView(catalog, manifest.products,
                                                  asharia::asset::AssetCatalogViewOptions{
                                                      .requireProducts = true,
                                                      .expectedProductKeys = expectedProductKeys,
                                                  });
        if (!plan.succeeded() || !plan.requests.empty() || !plan.diagnostics.empty() ||
            plan.cacheHits.size() != kAssetCount || view.entries.size() != kAssetCount ||
            !view.diagnostics.empty()) {
            std::cerr << "Scaling test produced incomplete indexed planning results.\n";
            return false;
        }
        for (const asharia::asset::AssetCatalogViewEntry& entry : view.entries) {
            if (entry.productState != asharia::asset::AssetCatalogProductState::Ready ||
                entry.currentProductCount != 1U || entry.staleProductCount != 0U) {
                std::cerr << "Scaling test produced an incorrect catalog product state.\n";
                return false;
            }
        }
        return true;
    }

} // namespace

// The exhaustive catch boundary converts all failures to the scale-test exit protocol.
// NOLINTNEXTLINE(bugprone-exception-escape)
int main() noexcept {
    try {
        return smokeIndexedCatalogPlanningScale() ? EXIT_SUCCESS : EXIT_FAILURE;
    } catch (const std::exception& exception) {
        std::cerr << "Asset pipeline scaling test threw: " << exception.what() << '\n';
        return EXIT_FAILURE;
    } catch (...) {
        std::cerr << "Asset pipeline scaling test caught an unknown exception.\n";
        return EXIT_FAILURE;
    }
}
