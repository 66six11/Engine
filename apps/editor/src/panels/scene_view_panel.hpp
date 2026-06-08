#pragma once

#include "editor_panel.hpp"

namespace asharia::editor {

    struct EditorInputSnapshot;

    class SceneViewPanel final : public ImGuiSceneViewEditorPanel {
    public:
        [[nodiscard]] const EditorPanelDesc& desc() const override;
        void prepareWindow(EditorPanelWindowContext& context, EditorPanelState& state) override;

    private:
        void drawSceneViewPanel(EditorSceneViewPanelDrawContext& context,
                                EditorPanelState& state) override;
        void updateCameraForViewportExtent(EditorExtent2D viewportExtent);
        [[nodiscard]] bool handleCameraNavigation(EditorExtent2D viewportExtent,
                                                  const EditorInputSnapshot& input);

        EditorViewportOverlayFlags overlayFlags_{defaultEditorSceneViewOverlayFlags()};
        EditorViewportCamera camera_;
        EditorExtent2D cameraExtent_;
        bool cameraInitialized_{false};

        EditorPanelDesc desc_{
            .id = EditorId{.value = "scene-view"},
            .title = "Scene View",
            .titleKey = "panel.sceneView",
            .defaultOpen = true,
            .singleton = true,
            .category = EditorPanelCategory::Viewport,
            .preferredDock = EditorDockSlot::Center,
        };
    };

} // namespace asharia::editor
