#include "editor_event.hpp"

#include <cstddef>
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
        case EditorEventKind::ActionInvoked:
            return "ActionInvoked";
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

    void EditorDiagnosticsLog::appendEvents(std::span<const EditorEvent> events) {
        for (const EditorEvent& event : events) {
            recentEvents_.push_back(EditorDiagnosticEvent{
                .sequence = nextSequence_++,
                .event = event,
            });
        }
        if (recentEvents_.size() > kMaxRecentEvents) {
            recentEvents_.erase(recentEvents_.begin(),
                                recentEvents_.end() -
                                    static_cast<std::ptrdiff_t>(kMaxRecentEvents));
        }
    }

    std::span<const EditorDiagnosticEvent> EditorDiagnosticsLog::recentEvents() const {
        return recentEvents_;
    }

} // namespace asharia::editor
