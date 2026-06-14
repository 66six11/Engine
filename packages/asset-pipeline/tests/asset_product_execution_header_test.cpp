#include "asharia/asset_pipeline/asset_product_execution.hpp"

namespace asharia::asset_pipeline_header_tests {

    void touchProductExecutionHeader() {
        [[maybe_unused]] asharia::asset::AssetProductExecutionRequest request{};
        [[maybe_unused]] asharia::asset::AssetProductExecutionResult result{};
        [[maybe_unused]] asharia::asset::AssetProductDependencyBytes dependencyBytes{};
        [[maybe_unused]] const asharia::asset::AssetProductExecutionDiagnosticCode diagnostic =
            asharia::asset::AssetProductExecutionDiagnosticCode::InvalidPlan;
        [[maybe_unused]] const asharia::asset::AssetProductExecutionDiagnosticCode
            textureDiagnostic =
                asharia::asset::AssetProductExecutionDiagnosticCode::TextureImportFailed;
        [[maybe_unused]] const asharia::asset::AssetProductExecutionDiagnosticCode
            shaderDiagnostic =
                asharia::asset::AssetProductExecutionDiagnosticCode::ShaderAuthoringImportFailed;
        [[maybe_unused]] const asharia::asset::AssetProductExecutionDiagnosticCode
            dependencyDiagnostic = asharia::asset::AssetProductExecutionDiagnosticCode::
                DependencyProductBytesHashMismatch;
    }

} // namespace asharia::asset_pipeline_header_tests
