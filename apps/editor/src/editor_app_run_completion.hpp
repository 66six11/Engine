#pragma once

#include "editor_app.hpp"

namespace asharia {
    class GlfwWindow;
}

namespace asharia::editor {
    class EditorFrameDebugger;
    class EditorViewportCoordinator;
    class EditorWorkspaceController;
    class ImGuiRuntime;
    struct EditorSmokeRunResult;

    [[nodiscard]] bool finishEditorRun(EditorRunMode mode, const EditorSmokeRunResult& runResult,
                                       GlfwWindow& window, ImGuiRuntime& imgui,
                                       EditorViewportCoordinator& viewportHost,
                                       const EditorFrameDebugger& frameDebugger,
                                       const EditorWorkspaceController& workspaceController);

} // namespace asharia::editor
