#include <cstdlib>

namespace asharia::asset_pipeline_header_tests {

    void touchProductManifestIoHeader();
    void touchSourceDiscoveryHeader();
    void touchSourceSnapshotHeader();

} // namespace asharia::asset_pipeline_header_tests

int main() {
    asharia::asset_pipeline_header_tests::touchProductManifestIoHeader();
    asharia::asset_pipeline_header_tests::touchSourceDiscoveryHeader();
    asharia::asset_pipeline_header_tests::touchSourceSnapshotHeader();
    return EXIT_SUCCESS;
}
