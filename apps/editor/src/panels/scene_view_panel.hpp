#pragma once

#include "editor_panel.hpp"

namespace asharia::editor {

    class SceneViewPanel final : public ImGuiEditorPanel {
    public:
        [[nodiscard]] const EditorPanelDesc& desc() const override;
        void prepareWindow(EditorPanelWindowContext& context, EditorPanelState& state) override;
        void draw(EditorPanelDrawContext& context, EditorPanelState& state) override;

    private:
        void updateCameraForViewportExtent(EditorExtent2D viewportExtent);
        void handleCameraNavigation(EditorExtent2D viewportExtent);

        EditorViewportOverlayFlags overlayFlags_{defaultEditorSceneViewOverlayFlags()};
        EditorViewportCamera camera_;
        EditorExtent2D cameraExtent_;
        bool cameraInitialized_{false};
        bool navigating_{false};

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
