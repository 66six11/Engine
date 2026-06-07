#include "asharia/asset_pipeline/asset_import_planning.hpp"

namespace asharia::asset_pipeline_header_tests {

    void touchImportPlanningHeader() {
        [[maybe_unused]] asharia::asset::AssetImportPlanResult result{};
        [[maybe_unused]] const asharia::asset::AssetImportRequestReason reason =
            asharia::asset::AssetImportRequestReason::MissingProduct;
        [[maybe_unused]] const asharia::asset::AssetImportPlanDiagnosticCode diagnostic =
            asharia::asset::AssetImportPlanDiagnosticCode::InvalidSource;
        [[maybe_unused]] const asharia::asset::AssetImportPlanDiagnosticSeverity severity =
            asharia::asset::AssetImportPlanDiagnosticSeverity::Warning;
    }

} // namespace asharia::asset_pipeline_header_tests
