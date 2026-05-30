#pragma once

#include "editor_panel.hpp"

namespace asharia::editor {

    class EditorSettingsPanel final : public ImGuiEditorPanel {
    public:
        [[nodiscard]] const EditorPanelDesc& desc() const override;
        void prepareWindow(EditorPanelWindowContext& context, EditorPanelState& state) override;
        void draw(EditorPanelDrawContext& context, EditorPanelState& state) override;

    private:
        EditorPanelDesc desc_{
            .id = EditorId{.value = "editor-settings"},
            .title = "Editor Settings",
            .titleKey = "panel.editorSettings",
            .defaultOpen = false,
            .singleton = true,
            .category = EditorPanelCategory::Settings,
            .preferredDock = EditorDockSlot::RightTop,
        };
    };

} // namespace asharia::editor
