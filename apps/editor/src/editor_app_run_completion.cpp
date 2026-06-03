#include "editor_app_run_completion.hpp"

#include <iostream>

#include "asharia/core/log.hpp"
#include "asharia/window_glfw/glfw_window.hpp"

#include "editor_frame_debugger.hpp"
#include "editor_frame_debugger_smoke.hpp"
#include "editor_input_smoke.hpp"
#include "editor_shortcut_smoke.hpp"
#include "editor_smoke.hpp"
#include "editor_smoke_validation.hpp"
#include "editor_startup_smoke.hpp"
#include "editor_viewport_coordinator.hpp"
#include "editor_viewport_smoke.hpp"
#include "editor_workspace.hpp"
#include "imgui_runtime.hpp"

namespace asharia::editor {

    [[nodiscard]] bool finishEditorRun(EditorRunMode mode, const EditorSmokeRunResult& runResult,
                                       GlfwWindow& window, ImGuiRuntime& imgui,
                                       EditorViewportCoordinator& viewportHost,
                                       const EditorFrameDebugger& frameDebugger,
                                       const EditorWorkspaceController& workspaceController) {
        if (isEditorSmokeMode(mode) && workspaceController.layoutApplyCount() == 0) {
            asharia::logError("Editor workspace smoke did not apply a dock layout preset.");
            return false;
        }

        const EditorViewportCoordinatorStats viewportStats = viewportHost.stats();
        const ImGuiTextureRegistryStats textureRegistryStats = viewportHost.textureRegistryStats();
        if (!validateViewportSmokePresentation(mode, runResult, viewportHost,
                                               textureRegistryStats) ||
            !validateViewportFlagsSmoke(mode, runResult, viewportHost, viewportStats) ||
            !validateViewportResizeSmoke(mode, runResult, viewportStats, textureRegistryStats) ||
            !validateFrameDebuggerSmoke(mode, runResult, frameDebugger) ||
            !validateInputRouterSmoke(mode, runResult) ||
            !validateShortcutRouterRunSmoke(mode, runResult)) {
            return false;
        }

        imgui.saveLayoutNow();
        if (!validateImguiLayoutSavedSmoke(mode, imgui)) {
            return false;
        }

        const auto viewportExtent = viewportHost.descriptorExtent();
        viewportHost.shutdown();
        imgui.shutdown();
        window.requestClose();

        std::cout << "Editor shell frames: " << runResult.renderedFrames
                  << ", viewport frames: " << viewportHost.viewportFramesRendered()
                  << ", texture frames: " << viewportHost.textureFramesSubmitted()
                  << ", input frames: " << runResult.inputStats.capturedFrames
                  << ", scene input reports: " << runResult.inputStats.sceneViewReports
                  << ", shortcut frames: " << runResult.shortcutStats.evaluatedFrames
                  << ", shortcut invocations: " << runResult.shortcutStats.shortcutInvocations
                  << ", overlay texture frames: " << viewportStats.overlayFlagTextureFramesAcquired
                  << ", render view diagnostics frames: "
                  << viewportStats.renderViewDiagnosticsFramesRecorded
                  << ", idle scene skips: " << viewportStats.idleSceneViewFramesSkipped
                  << ", frame debugger: " << frameDebugger.stateName()
                  << ", live texture descriptors peak: " << textureRegistryStats.peakLiveDescriptors
                  << ", viewport: " << viewportExtent.width << 'x' << viewportExtent.height << '\n';
        if (isEditorViewportResizeSmokeMode(mode)) {
            std::cout << "Editor viewport resize: " << runResult.viewportExtentBeforeResize.width
                      << 'x' << runResult.viewportExtentBeforeResize.height << " -> "
                      << runResult.viewportExtentAfterResize.width << 'x'
                      << runResult.viewportExtentAfterResize.height
                      << ", retired render targets: " << viewportStats.renderTargetsRetired
                      << ", deferred render targets: " << viewportStats.renderTargetsDeferred
                      << ", retired texture descriptors: "
                      << textureRegistryStats.descriptorsRetired << '\n';
        }
        return true;
    }

} // namespace asharia::editor
