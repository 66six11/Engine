#pragma once

#include "editor_panel.hpp"

namespace asharia::editor {

    class LogPanel final : public ImGuiLogEditorPanel {
    public:
        [[nodiscard]] const EditorPanelDesc& desc() const override;

    private:
        void drawLogPanel(EditorLogPanelDrawContext& context, EditorPanelState& state) override;

        EditorPanelDesc desc_{
            .id = EditorId{.value = "log"},
            .title = "Log",
            .titleKey = "panel.log",
            .defaultOpen = true,
            .singleton = true,
            .category = EditorPanelCategory::Diagnostics,
            .preferredDock = EditorDockSlot::Bottom,
        };
    };

} // namespace asharia::editor
