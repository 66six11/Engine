#pragma once

#include <span>
#include <string_view>
#include <vector>

#include "editor_id.hpp"

namespace asharia::editor {

    enum class EditorEventKind {
        PanelOpened,
        PanelClosed,
        PanelFocused,
        MenuActionInvoked,
        ViewportResized,
        SelectionChanged,
    };

    struct EditorEvent {
        EditorEventKind kind{};
        EditorId sourceId;
    };

    [[nodiscard]] std::string_view editorEventKindName(EditorEventKind kind);

    class EditorEventQueue {
    public:
        void push(EditorEvent event);
        void clear();
        [[nodiscard]] bool empty() const;
        [[nodiscard]] std::span<const EditorEvent> events() const;

    private:
        std::vector<EditorEvent> events_;
    };

} // namespace asharia::editor
