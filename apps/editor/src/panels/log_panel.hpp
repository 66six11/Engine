#pragma once

#include "editor_panel.hpp"

namespace asharia::editor {

    class LogPanel final : public ImGuiEditorPanel {
    public:
        [[nodiscard]] const EditorPanelDesc& desc() const override;
        void draw(EditorFrameContext& context, EditorPanelState& state) override;

    private:
        EditorPanelDesc desc_{
            .id = EditorId{.value = "log"},
            .title = "Log",
            .defaultOpen = true,
            .singleton = true,
        };
    };

} // namespace asharia::editor
