#pragma once

#include "editor_panel.hpp"

namespace asharia::editor {

    class RenderGraphPanel final : public ImGuiEditorPanel {
    public:
        [[nodiscard]] const EditorPanelDesc& desc() const override;
        void prepareWindow(EditorPanelWindowContext& context, EditorPanelState& state) override;
        void draw(EditorFrameContext& context, EditorPanelState& state) override;

    private:
        EditorPanelDesc desc_{
            .id = EditorId{.value = "render-graph"},
            .title = "Live RG View",
            .titleKey = "panel.renderGraph",
            .defaultOpen = true,
            .singleton = true,
            .category = EditorPanelCategory::Diagnostics,
            .preferredDock = EditorDockSlot::Center,
        };
    };

} // namespace asharia::editor
