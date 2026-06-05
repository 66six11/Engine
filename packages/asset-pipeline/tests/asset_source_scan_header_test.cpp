#include "asharia/asset_pipeline/asset_source_scan.hpp"

namespace asharia::asset_pipeline_header_tests {

    void touchSourceScanHeader() {
        [[maybe_unused]] const asharia::asset::AssetSourceScanRequest request{
            .sourceRoot = "Content",
            .sourcePathPrefix = "Content",
        };
        [[maybe_unused]] asharia::asset::AssetSourceScanResult result{};
        [[maybe_unused]] const asharia::asset::AssetSourceScanDiagnosticCode code =
            asharia::asset::AssetSourceScanDiagnosticCode::MissingMetadata;
    }

} // namespace asharia::asset_pipeline_header_tests
