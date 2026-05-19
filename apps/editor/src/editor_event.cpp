#include "editor_event.hpp"

#include <utility>

namespace asharia::editor {

    std::string_view editorEventKindName(EditorEventKind kind) {
        switch (kind) {
        case EditorEventKind::PanelOpened:
            return "PanelOpened";
        case EditorEventKind::PanelClosed:
            return "PanelClosed";
        case EditorEventKind::PanelFocused:
            return "PanelFocused";
        case EditorEventKind::MenuActionInvoked:
            return "MenuActionInvoked";
        case EditorEventKind::ViewportResized:
            return "ViewportResized";
        case EditorEventKind::SelectionChanged:
            return "SelectionChanged";
        }
        return "Unknown";
    }

    void EditorEventQueue::push(EditorEvent event) {
        events_.push_back(std::move(event));
    }

    void EditorEventQueue::clear() {
        events_.clear();
    }

    bool EditorEventQueue::empty() const {
        return events_.empty();
    }

    std::span<const EditorEvent> EditorEventQueue::events() const {
        return events_;
    }

} // namespace asharia::editor
