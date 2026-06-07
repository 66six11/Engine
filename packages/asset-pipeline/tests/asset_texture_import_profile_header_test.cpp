#include <string>

#include "asharia/asset_pipeline/asset_texture_import_profile.hpp"

namespace asharia::asset_pipeline_header_tests {

    void touchTextureImportProfileHeader() {
        [[maybe_unused]] const std::string profile =
            asharia::asset::normalizeTextureImportProfileName("Texture 2D");
    }

} // namespace asharia::asset_pipeline_header_tests
