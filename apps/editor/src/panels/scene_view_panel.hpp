#pragma once

#include "editor_panel.hpp"

namespace asharia::editor {

    class SceneViewPanel final : public ImGuiEditorPanel {
    public:
        [[nodiscard]] const EditorPanelDesc& desc() const override;
        void prepareWindow(EditorFrameContext& context, EditorPanelState& state) override;
        void draw(EditorFrameContext& context, EditorPanelState& state) override;

    private:
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
