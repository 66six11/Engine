#pragma once

#include "editor_app.hpp"

namespace asharia::editor {
    class EditorViewportCoordinator;
    struct EditorSmokeRunResult;
    struct EditorViewportCoordinatorStats;
    struct ImGuiTextureRegistryStats;

    [[nodiscard]] bool
    validateViewportSmokePresentation(EditorRunMode mode, const EditorSmokeRunResult& runResult,
                                      const EditorViewportCoordinator& viewportHost,
                                      const ImGuiTextureRegistryStats& textureRegistryStats);
    [[nodiscard]] bool
    validateViewportFlagsSmoke(EditorRunMode mode, const EditorSmokeRunResult& runResult,
                               const EditorViewportCoordinator& viewportHost,
                               const EditorViewportCoordinatorStats& viewportStats);
    [[nodiscard]] bool
    validateViewportResizeSmoke(EditorRunMode mode, const EditorSmokeRunResult& runResult,
                                const EditorViewportCoordinatorStats& viewportStats,
                                const ImGuiTextureRegistryStats& textureRegistryStats);

} // namespace asharia::editor
