#pragma once

#include "editor_panel.hpp"

namespace asharia::editor {

    class InspectorPanel final : public ImGuiInspectorEditorPanel {
    public:
        [[nodiscard]] const EditorPanelDesc& desc() const override;

    private:
        void drawInspectorPanel(EditorInspectorPanelDrawContext& context,
                                EditorPanelState& state) override;

        EditorPanelDesc desc_{
            .id = EditorId{.value = "inspector"},
            .title = "Inspector",
            .titleKey = "panel.inspector",
            .defaultOpen = true,
            .singleton = true,
            .category = EditorPanelCategory::Tools,
            .preferredDock = EditorDockSlot::RightTop,
        };
    };

} // namespace asharia::editor
