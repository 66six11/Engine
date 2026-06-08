#pragma once

#include <array>

#include "editor_panel.hpp"

namespace asharia::editor {

    class SceneTreePanel final : public ImGuiSceneTreeEditorPanel {
    public:
        [[nodiscard]] const EditorPanelDesc& desc() const override;

    private:
        void drawSceneTreePanel(EditorSceneTreePanelDrawContext& context,
                                EditorPanelState& state) override;

        EditorPanelDesc desc_{
            .id = EditorId{.value = "scene-tree"},
            .title = "Hierarchy",
            .titleKey = "panel.sceneTree",
            .defaultOpen = true,
            .singleton = true,
            .category = EditorPanelCategory::Tools,
            .preferredDock = EditorDockSlot::Left,
        };
        std::array<char, 96> filter_{};
    };

} // namespace asharia::editor
