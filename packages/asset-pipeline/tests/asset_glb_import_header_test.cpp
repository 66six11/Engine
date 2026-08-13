#include "asharia/asset_pipeline/asset_glb_import.hpp"

namespace asharia::asset_pipeline_header_tests {

    void touchGlbImportHeader() {
        [[maybe_unused]] asharia::asset::AssetGlbImportLimits limits{};
        [[maybe_unused]] asharia::asset::AssetGlbImportRequest request{};
        [[maybe_unused]] const asharia::asset::AssetGlbImportDiagnosticCode diagnostic =
            asharia::asset::AssetGlbImportDiagnosticCode::InvalidGlb;
        [[maybe_unused]] constexpr auto candidate =
            &asharia::asset::isRestrictedGlbMeshImportCandidate;
    }

} // namespace asharia::asset_pipeline_header_tests
