#pragma once

#include "editor_panel.hpp"

namespace asharia::editor {

    class FrameDebuggerPanel final : public ImGuiEditorPanel {
    public:
        [[nodiscard]] const EditorPanelDesc& desc() const override;
        void prepareWindow(EditorPanelWindowContext& context, EditorPanelState& state) override;
        void draw(EditorPanelDrawContext& context, EditorPanelState& state) override;

    private:
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
