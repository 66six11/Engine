#include "asharia/asset_pipeline/asset_source_snapshot.hpp"

namespace asharia::asset_pipeline_header_tests {

    void touchSourceSnapshotHeader() {
        [[maybe_unused]] const asharia::asset::AssetSourceSnapshotEntry entry{
            .sourcePath = "Content/Textures/Crate.png",
            .sourceFilePath = "Content/Textures/Crate.png",
        };
        [[maybe_unused]] asharia::asset::AssetSourceSnapshotResult result{};
    }

} // namespace asharia::asset_pipeline_header_tests
