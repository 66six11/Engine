#pragma once

#include "editor_panel.hpp"

namespace asharia::editor {

    class FrameDebuggerPanel final : public ImGuiDiagnosticsEditorPanel {
    public:
        [[nodiscard]] const EditorPanelDesc& desc() const override;
        void prepareWindow(EditorPanelWindowContext& context, EditorPanelState& state) override;

    private:
        void drawDiagnosticsPanel(EditorDiagnosticsPanelDrawContext& context,
                                  EditorPanelState& state) override;

        EditorPanelDesc desc_{
            .id = EditorId{.value = "frame-debugger"},
            .title = "Frame Debugger",
            .titleKey = "panel.frameDebugger",
            .defaultOpen = true,
            .singleton = true,
            .category = EditorPanelCategory::Diagnostics,
            .preferredDock = EditorDockSlot::RightTop,
        };
    };

} // namespace asharia::editor
