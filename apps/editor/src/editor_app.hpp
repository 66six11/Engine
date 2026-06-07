#pragma once

#include <filesystem>
#include <string>

namespace asharia::editor {

    enum class EditorRunMode {
        Interactive,
        SmokeShell,
        SmokeAssetBrowser,
        SmokeViewport,
        SmokeViewportResize,
        SmokeFrameDebugger,
    };

    struct EditorAssetCatalogRunConfig {
        std::filesystem::path projectFile;
        std::filesystem::path productManifestFile;
        std::string targetProfile;

        [[nodiscard]] explicit operator bool() const noexcept {
            return !projectFile.empty();
        }
    };

    struct EditorRunConfig {
        EditorRunMode mode{EditorRunMode::Interactive};
        EditorAssetCatalogRunConfig assetCatalog;
    };

    int runEditor(EditorRunMode mode);
    int runEditor(const EditorRunConfig& config);

} // namespace asharia::editor
