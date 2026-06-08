#include "editor_input_router.hpp"

#include <cmath>

namespace asharia::editor {

    namespace {

        [[nodiscard]] bool hasDelta(float deltaX, float deltaY) {
            return std::fabs(deltaX) > 0.0F || std::fabs(deltaY) > 0.0F;
        }

    } // namespace

    void EditorInputRouter::beginFrame(EditorInputCapture capture) {
        snapshot_ = EditorInputSnapshot{
            .imguiWantsMouse = capture.imguiWantsMouse,
            .imguiWantsKeyboard = capture.imguiWantsKeyboard,
            .imguiWantsTextInput = capture.imguiWantsTextInput,
            .rightMouseDragging = capture.rightMouseDragging,
            .rightMouseDragDeltaX = capture.rightMouseDragDeltaX,
            .rightMouseDragDeltaY = capture.rightMouseDragDeltaY,
            .middleMouseDragging = capture.middleMouseDragging,
            .middleMouseDragDeltaX = capture.middleMouseDragDeltaX,
            .middleMouseDragDeltaY = capture.middleMouseDragDeltaY,
            .mouseWheel = capture.mouseWheel,
        };
        ++stats_.capturedFrames;
        updateDerivedState();
    }

    void EditorInputRouter::reportSceneView(EditorSceneViewInputState state) {
        snapshot_.sceneViewHovered = state.hovered;
        snapshot_.sceneViewFocused = state.focused;
        ++stats_.sceneViewReports;
        updateDerivedState();
    }

    void EditorInputRouter::finalizeFrame() {
        updateDerivedState();
    }

    const EditorInputSnapshot& EditorInputRouter::snapshot() const {
        return snapshot_;
    }

    EditorInputRouterStats EditorInputRouter::stats() const {
        return stats_;
    }

    void EditorInputRouter::updateDerivedState() {
        snapshot_.sceneViewCanReceiveMouse =
            snapshot_.sceneViewHovered && snapshot_.sceneViewFocused &&
            !snapshot_.imguiWantsTextInput;

        snapshot_.sceneViewCameraOrbitActive =
            snapshot_.sceneViewCanReceiveMouse && snapshot_.rightMouseDragging &&
            hasDelta(snapshot_.rightMouseDragDeltaX, snapshot_.rightMouseDragDeltaY);
        snapshot_.sceneViewCameraOrbitDeltaX =
            snapshot_.sceneViewCameraOrbitActive ? snapshot_.rightMouseDragDeltaX : 0.0F;
        snapshot_.sceneViewCameraOrbitDeltaY =
            snapshot_.sceneViewCameraOrbitActive ? snapshot_.rightMouseDragDeltaY : 0.0F;

        snapshot_.sceneViewCameraPanActive =
            snapshot_.sceneViewCanReceiveMouse && snapshot_.middleMouseDragging &&
            hasDelta(snapshot_.middleMouseDragDeltaX, snapshot_.middleMouseDragDeltaY);
        snapshot_.sceneViewCameraPanDeltaX =
            snapshot_.sceneViewCameraPanActive ? snapshot_.middleMouseDragDeltaX : 0.0F;
        snapshot_.sceneViewCameraPanDeltaY =
            snapshot_.sceneViewCameraPanActive ? snapshot_.middleMouseDragDeltaY : 0.0F;

        snapshot_.sceneViewCameraDollyActive =
            snapshot_.sceneViewCanReceiveMouse && std::fabs(snapshot_.mouseWheel) > 0.0F;
        snapshot_.sceneViewCameraDollyDelta =
            snapshot_.sceneViewCameraDollyActive ? snapshot_.mouseWheel : 0.0F;
        snapshot_.sceneViewCameraInputActive = snapshot_.sceneViewCameraOrbitActive ||
                                               snapshot_.sceneViewCameraPanActive ||
                                               snapshot_.sceneViewCameraDollyActive;
        snapshot_.shortcutsEnabled =
            !snapshot_.imguiWantsKeyboard && !snapshot_.imguiWantsTextInput;
    }

} // namespace asharia::editor
