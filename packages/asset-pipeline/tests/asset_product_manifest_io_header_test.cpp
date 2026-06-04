#include "asharia/asset_pipeline/asset_product_manifest_io.hpp"

namespace asharia::asset_pipeline_header_tests {

    void touchProductManifestIoHeader() {
        [[maybe_unused]] const auto schema = asharia::asset::kAssetProductManifestSchema;
        [[maybe_unused]] const auto version = asharia::asset::kAssetProductManifestVersion;
        [[maybe_unused]] asharia::asset::AssetProductManifestDocument document{};
    }

} // namespace asharia::asset_pipeline_header_tests
