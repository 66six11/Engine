#pragma once

#include "editor_panel.hpp"

namespace asharia::editor {

    class RenderGraphPanel final : public ImGuiDiagnosticsEditorPanel {
    public:
        [[nodiscard]] const EditorPanelDesc& desc() const override;
        void prepareWindow(EditorPanelWindowContext& context, EditorPanelState& state) override;

    private:
        void drawDiagnosticsPanel(EditorDiagnosticsPanelDrawContext& context,
                                  EditorPanelState& state) override;

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
