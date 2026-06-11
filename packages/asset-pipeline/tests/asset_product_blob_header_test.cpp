#include "asharia/asset_pipeline/asset_product_blob.hpp"

namespace asharia::asset_pipeline_header_tests {

    void touchProductBlobHeader() {
        [[maybe_unused]] asharia::asset::AssetProductBlobReadRequest request{};
        [[maybe_unused]] asharia::asset::AssetProductBlobPayload payload{};
        [[maybe_unused]] const asharia::asset::AssetProductBlobDiagnosticCode diagnostic =
            asharia::asset::AssetProductBlobDiagnosticCode::MissingProduct;
    }

} // namespace asharia::asset_pipeline_header_tests
