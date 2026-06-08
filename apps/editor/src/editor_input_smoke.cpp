#include "editor_input_smoke.hpp"

#include <cstdint>

#include "asharia/core/log.hpp"

#include "editor_smoke.hpp"

namespace asharia::editor {
    namespace {

        [[nodiscard]] bool validateInputRouterSnapshotSmoke() {
            EditorInputRouter router;
            router.beginFrame(EditorInputCapture{
                .imguiWantsMouse = true,
                .rightMouseDragging = true,
                .rightMouseDragDeltaX = 8.0F,
                .rightMouseDragDeltaY = -4.0F,
                .middleMouseDragging = true,
                .middleMouseDragDeltaX = 3.0F,
                .middleMouseDragDeltaY = 2.0F,
                .mouseWheel = 1.0F,
            });
            router.reportSceneView(EditorSceneViewInputState{.hovered = true, .focused = true});
            const EditorInputSnapshot& active = router.snapshot();
            if (!active.imguiWantsMouse || !active.sceneViewCanReceiveMouse ||
                !active.sceneViewCameraOrbitActive ||
                active.sceneViewCameraOrbitDeltaX != 8.0F ||
                active.sceneViewCameraOrbitDeltaY != -4.0F ||
                !active.sceneViewCameraPanActive || active.sceneViewCameraPanDeltaX != 3.0F ||
                active.sceneViewCameraPanDeltaY != 2.0F ||
                !active.sceneViewCameraDollyActive || active.sceneViewCameraDollyDelta != 1.0F ||
                !active.sceneViewCameraInputActive || !active.shortcutsEnabled) {
                asharia::logError(
                    "Editor input router smoke did not derive active Scene View camera input.");
                return false;
            }

            router.beginFrame(EditorInputCapture{
                .rightMouseDragging = true,
                .rightMouseDragDeltaX = 5.0F,
            });
            router.reportSceneView(EditorSceneViewInputState{.hovered = false, .focused = true});
            const EditorInputSnapshot& unhovered = router.snapshot();
            if (unhovered.sceneViewCanReceiveMouse ||
                unhovered.sceneViewCameraInputActive ||
                unhovered.sceneViewCameraOrbitDeltaX != 0.0F) {
                asharia::logError(
                    "Editor input router smoke allowed unhovered Scene View camera input.");
                return false;
            }

            router.beginFrame(EditorInputCapture{
                .imguiWantsTextInput = true,
                .rightMouseDragging = true,
                .rightMouseDragDeltaX = 5.0F,
            });
            router.reportSceneView(EditorSceneViewInputState{.hovered = true, .focused = true});
            const EditorInputSnapshot& textCaptured = router.snapshot();
            if (textCaptured.sceneViewCanReceiveMouse ||
                textCaptured.sceneViewCameraInputActive || textCaptured.shortcutsEnabled) {
                asharia::logError(
                    "Editor input router smoke leaked text-input capture into editor input.");
                return false;
            }

            router.beginFrame(EditorInputCapture{
                .imguiWantsKeyboard = true,
            });
            router.reportSceneView(EditorSceneViewInputState{.hovered = true, .focused = true});
            if (router.snapshot().shortcutsEnabled) {
                asharia::logError(
                    "Editor input router smoke allowed shortcuts during keyboard capture.");
                return false;
            }
            return true;
        }

    } // namespace

    bool validateInputRouterSmoke(EditorRunMode mode, const EditorSmokeRunResult& runResult) {
        if (!isEditorSmokeMode(mode)) {
            return true;
        }
        if (!validateInputRouterSnapshotSmoke()) {
            return false;
        }
        if (runResult.inputStats.capturedFrames <
            static_cast<std::uint64_t>(runResult.renderedFrames)) {
            asharia::logError("Editor input router smoke did not capture every rendered frame.");
            return false;
        }
        if (runResult.inputStats.sceneViewReports == 0) {
            asharia::logError("Editor input router smoke did not receive Scene View input state.");
            return false;
        }
        return true;
    }
} // namespace asharia::editor
