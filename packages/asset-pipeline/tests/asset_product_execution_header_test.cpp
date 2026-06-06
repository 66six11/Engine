#include "asharia/asset_pipeline/asset_product_execution.hpp"

namespace asharia::asset_pipeline_header_tests {

    void touchProductExecutionHeader() {
        [[maybe_unused]] asharia::asset::AssetProductExecutionRequest request{};
        [[maybe_unused]] asharia::asset::AssetProductExecutionResult result{};
        [[maybe_unused]] const asharia::asset::AssetProductExecutionDiagnosticCode diagnostic =
            asharia::asset::AssetProductExecutionDiagnosticCode::InvalidPlan;
    }

} // namespace asharia::asset_pipeline_header_tests
