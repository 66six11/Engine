#pragma once

#include <string>
#include <vector>

#include "editor_panel.hpp"

namespace asharia::editor {

    class LogPanel final : public ImGuiEditorPanel {
    public:
        [[nodiscard]] const EditorPanelDesc& desc() const override;
        void draw(EditorFrameContext& context, EditorPanelState& state) override;

    private:
        void appendFrameEvents(const EditorFrameContext& context);

        EditorPanelDesc desc_{
            .id = EditorId{.value = "log"},
            .title = "Log",
            .defaultOpen = true,
            .singleton = true,
        };
        std::vector<std::string> recentEvents_;
    };

} // namespace asharia::editor
