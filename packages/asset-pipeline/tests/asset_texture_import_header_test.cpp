#include "asharia/asset_pipeline/asset_texture_import.hpp"

namespace asharia::asset_pipeline_header_tests {

    void touchTextureImportHeader() {
        [[maybe_unused]] asharia::asset::AssetTextureImportRequest request{};
        [[maybe_unused]] asharia::asset::AssetTextureImportResult result{};
        [[maybe_unused]] const asharia::asset::AssetTextureImporterDescriptor descriptor =
            asharia::asset::makeRawRgba8TextureImporterDescriptor();
        [[maybe_unused]] const asharia::asset::AssetTextureImporterDescriptor pngDescriptor =
            asharia::asset::makePngTextureImporterDescriptor();
        [[maybe_unused]] const asharia::asset::AssetTextureImportDiagnosticCode diagnostic =
            asharia::asset::AssetTextureImportDiagnosticCode::DecodeFailed;
    }

} // namespace asharia::asset_pipeline_header_tests
