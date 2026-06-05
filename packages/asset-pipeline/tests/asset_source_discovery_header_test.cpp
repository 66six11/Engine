#include "asharia/asset_pipeline/asset_source_discovery.hpp"

namespace asharia::asset_pipeline_header_tests {

    void touchSourceDiscoveryHeader() {
        [[maybe_unused]] const asharia::asset::AssetSourceDiscoveryEntry entry{
            .sourcePath = "Content/Textures/Crate.png",
            .metadataPath = "Content/Textures/Crate.png.ameta",
        };
        [[maybe_unused]] asharia::asset::AssetSourceDiscoveryResult result{};
    }

} // namespace asharia::asset_pipeline_header_tests
