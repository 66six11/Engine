#pragma once

#include "editor_app.hpp"
#include "editor_i18n.hpp"
#include "editor_ui.hpp"

namespace asharia::editor {
    class EditorActionRegistry;
    class EditorContext;
    class EditorFrameDebugger;
    class EditorToolRegistry;
    class EditorViewportCoordinator;
    class ImGuiRuntime;
    struct EditorSmokeRunResult;
    struct EditorActionServices;
    struct EditorViewportCoordinatorStats;
    struct ImGuiTextureRegistryStats;

    [[nodiscard]] bool validateEditorStartupSmoke(EditorRunMode mode, const ImGuiRuntime& imgui,
                                                  EditorLocale locale, EditorUiThemeId theme);
    [[nodiscard]] bool validateEditorRegistrationSmoke(EditorRunMode mode,
                                                       EditorActionRegistry& actionRegistry,
                                                       EditorContext& editorContext,
                                                       EditorActionServices& actionServices,
                                                       const EditorToolRegistry& toolRegistry);
    [[nodiscard]] bool validateEditorCommandSmoke(EditorRunMode mode);
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
    [[nodiscard]] bool validateFrameDebuggerSmoke(EditorRunMode mode,
                                                  const EditorSmokeRunResult& runResult,
                                                  const EditorFrameDebugger& frameDebugger);
    [[nodiscard]] bool validateInputRouterSmoke(EditorRunMode mode,
                                                const EditorSmokeRunResult& runResult);
    [[nodiscard]] bool validateShortcutRouterRunSmoke(EditorRunMode mode,
                                                      const EditorSmokeRunResult& runResult);
    [[nodiscard]] bool validateImguiLayoutSavedSmoke(EditorRunMode mode, const ImGuiRuntime& imgui);

} // namespace asharia::editor
