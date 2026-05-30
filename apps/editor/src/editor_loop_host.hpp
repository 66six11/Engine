#pragma once

#include "asharia/core/result.hpp"

#include "editor_smoke.hpp"

namespace asharia {
    class BasicFullscreenTextureRenderer;
    class GlfwWindow;
    class VulkanFrameLoop;
} // namespace asharia

namespace asharia::editor {
    class EditorActionRegistry;
    class EditorDiagnosticsLog;
    class EditorEventQueue;
    class EditorFrameDebugger;
    class EditorI18n;
    class EditorPanelRegistry;
    class EditorSettingsController;
    class EditorToolRegistry;
    class EditorViewportCoordinator;
    class EditorWorkspaceController;
    struct EditorActionServices;

    [[nodiscard]] Result<EditorSmokeRunResult> runEditorLoop(
        GlfwWindow& window, VulkanFrameLoop& frameLoop, BasicFullscreenTextureRenderer& renderer,
        EditorViewportCoordinator& viewportHost, EditorFrameDebugger& frameDebugger,
        EditorActionRegistry& actionRegistry, EditorActionServices& actionServices,
        EditorEventQueue& eventQueue, EditorDiagnosticsLog& diagnosticsLog, EditorI18n& i18n,
        EditorSettingsController& settingsController, EditorPanelRegistry& panelRegistry,
        EditorToolRegistry& toolRegistry, EditorWorkspaceController& workspace, EditorRunMode mode);

} // namespace asharia::editor
