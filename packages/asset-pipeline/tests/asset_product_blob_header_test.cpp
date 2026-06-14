#include "asharia/asset_pipeline/asset_product_blob.hpp"

namespace asharia::asset_pipeline_header_tests {

    void touchProductBlobHeader() {
        [[maybe_unused]] asharia::asset::AssetProductBlobReadRequest request{};
        [[maybe_unused]] asharia::asset::AssetProductBlobPayload payload{};
        [[maybe_unused]] asharia::asset::AssetTextureProductPayload texturePayload{};
        [[maybe_unused]] asharia::asset::AssetMaterialInstanceProductPayload materialPayload{};
        [[maybe_unused]] asharia::asset::AssetShaderAuthoringProductPayload shaderPayload{};
        [[maybe_unused]] asharia::asset::AssetShaderAuthoringProductEntry shaderEntry{};
        [[maybe_unused]] asharia::asset::AssetShaderCompileReflectionProductPayload
            shaderCompilePayload{};
        [[maybe_unused]] asharia::asset::AssetShaderCompileReflectionProductEntry
            shaderCompileEntry{};
        [[maybe_unused]] const asharia::asset::AssetProductBlobDiagnosticCode diagnostic =
            asharia::asset::AssetProductBlobDiagnosticCode::MissingProduct;
    }

} // namespace asharia::asset_pipeline_header_tests
