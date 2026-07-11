#include <cstdlib>

namespace asharia::asset_pipeline_header_tests {

    void touchImportPlanningHeader();
    void touchProductBlobHeader();
    void touchProductExecutionHeader();
    void touchProductManifestIoHeader();
    void touchScannedImportPlanningHeader();
    void touchSourceScanHeader();
    void touchSourceDiscoveryHeader();
    void touchSourceSnapshotHeader();
    void touchTextureImportHeader();
    void touchTextureImportProfileHeader();
    void touchToolFingerprintHeader();

} // namespace asharia::asset_pipeline_header_tests

int main() {
    asharia::asset_pipeline_header_tests::touchImportPlanningHeader();
    asharia::asset_pipeline_header_tests::touchProductBlobHeader();
    asharia::asset_pipeline_header_tests::touchProductExecutionHeader();
    asharia::asset_pipeline_header_tests::touchProductManifestIoHeader();
    asharia::asset_pipeline_header_tests::touchScannedImportPlanningHeader();
    asharia::asset_pipeline_header_tests::touchSourceScanHeader();
    asharia::asset_pipeline_header_tests::touchSourceDiscoveryHeader();
    asharia::asset_pipeline_header_tests::touchSourceSnapshotHeader();
    asharia::asset_pipeline_header_tests::touchTextureImportHeader();
    asharia::asset_pipeline_header_tests::touchTextureImportProfileHeader();
    asharia::asset_pipeline_header_tests::touchToolFingerprintHeader();
    return EXIT_SUCCESS;
}
