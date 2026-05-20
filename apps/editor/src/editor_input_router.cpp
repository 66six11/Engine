#include "editor_input_router.hpp"

namespace asharia::editor {

    void EditorInputRouter::beginFrame(EditorInputCapture capture) {
        snapshot_ = EditorInputSnapshot{
            .imguiWantsMouse = capture.imguiWantsMouse,
            .imguiWantsKeyboard = capture.imguiWantsKeyboard,
            .imguiWantsTextInput = capture.imguiWantsTextInput,
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
            snapshot_.sceneViewHovered && snapshot_.sceneViewFocused && !snapshot_.imguiWantsMouse;
        snapshot_.shortcutsEnabled =
            !snapshot_.imguiWantsKeyboard && !snapshot_.imguiWantsTextInput;
    }

} // namespace asharia::editor
