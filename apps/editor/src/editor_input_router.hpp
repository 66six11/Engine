#pragma once

#include <cstdint>

namespace asharia::editor {

    struct EditorInputCapture {
        bool imguiWantsMouse{};
        bool imguiWantsKeyboard{};
        bool imguiWantsTextInput{};
        bool rightMouseDragging{};
        float rightMouseDragDeltaX{};
        float rightMouseDragDeltaY{};
        bool middleMouseDragging{};
        float middleMouseDragDeltaX{};
        float middleMouseDragDeltaY{};
        float mouseWheel{};
    };

    struct EditorSceneViewInputState {
        bool hovered{};
        bool focused{};
    };

    struct EditorInputSnapshot {
        bool imguiWantsMouse{};
        bool imguiWantsKeyboard{};
        bool imguiWantsTextInput{};
        bool rightMouseDragging{};
        float rightMouseDragDeltaX{};
        float rightMouseDragDeltaY{};
        bool middleMouseDragging{};
        float middleMouseDragDeltaX{};
        float middleMouseDragDeltaY{};
        float mouseWheel{};
        bool sceneViewHovered{};
        bool sceneViewFocused{};
        bool sceneViewCanReceiveMouse{};
        bool sceneViewCameraOrbitActive{};
        float sceneViewCameraOrbitDeltaX{};
        float sceneViewCameraOrbitDeltaY{};
        bool sceneViewCameraPanActive{};
        float sceneViewCameraPanDeltaX{};
        float sceneViewCameraPanDeltaY{};
        bool sceneViewCameraDollyActive{};
        float sceneViewCameraDollyDelta{};
        bool sceneViewCameraInputActive{};
        bool shortcutsEnabled{};
    };

    struct EditorInputRouterStats {
        std::uint64_t capturedFrames{};
        std::uint64_t sceneViewReports{};
    };

    class EditorInputRouter {
    public:
        void beginFrame(EditorInputCapture capture);
        void reportSceneView(EditorSceneViewInputState state);
        void finalizeFrame();

        [[nodiscard]] const EditorInputSnapshot& snapshot() const;
        [[nodiscard]] EditorInputRouterStats stats() const;

    private:
        void updateDerivedState();

        EditorInputSnapshot snapshot_;
        EditorInputRouterStats stats_;
    };

} // namespace asharia::editor
