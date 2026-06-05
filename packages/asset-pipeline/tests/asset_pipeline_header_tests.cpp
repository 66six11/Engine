#include <cstdlib>

namespace asharia::asset_pipeline_header_tests {

    void touchImportPlanningHeader();
    void touchProductManifestIoHeader();
    void touchScannedImportPlanningHeader();
    void touchSourceScanHeader();
    void touchSourceDiscoveryHeader();
    void touchSourceSnapshotHeader();

} // namespace asharia::asset_pipeline_header_tests

int main() {
    asharia::asset_pipeline_header_tests::touchImportPlanningHeader();
    asharia::asset_pipeline_header_tests::touchProductManifestIoHeader();
    asharia::asset_pipeline_header_tests::touchScannedImportPlanningHeader();
    asharia::asset_pipeline_header_tests::touchSourceScanHeader();
    asharia::asset_pipeline_header_tests::touchSourceDiscoveryHeader();
    asharia::asset_pipeline_header_tests::touchSourceSnapshotHeader();
    return EXIT_SUCCESS;
}
