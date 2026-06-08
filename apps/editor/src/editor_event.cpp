#include "editor_event.hpp"

#include <cstddef>
#include <string>
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
        case EditorEventKind::DirtyStateChanged:
            return "DirtyStateChanged";
        case EditorEventKind::CommandHistoryChanged:
            return "CommandHistoryChanged";
        case EditorEventKind::ValidationReported:
            return "ValidationReported";
        }
        return "Unknown";
    }

    std::string_view editorEventSeverityName(EditorEventSeverity severity) {
        switch (severity) {
        case EditorEventSeverity::Info:
            return "Info";
        case EditorEventSeverity::Warning:
            return "Warning";
        case EditorEventSeverity::Error:
            return "Error";
        }
        return "Unknown";
    }

    std::string_view editorEventOutcomeName(EditorEventOutcome outcome) {
        switch (outcome) {
        case EditorEventOutcome::None:
            return "None";
        case EditorEventOutcome::Succeeded:
            return "Succeeded";
        case EditorEventOutcome::Failed:
            return "Failed";
        case EditorEventOutcome::Noop:
            return "Noop";
        }
        return "Unknown";
    }

    std::string editorEventDisplayText(const EditorEvent& event) {
        std::string text =
            std::string{editorEventKindName(event.kind)} + ": " + event.sourceId.value;
        if (event.metadata.revision != 0U) {
            text += " rev=" + std::to_string(event.metadata.revision);
        }
        if (!event.metadata.label.empty()) {
            text += " " + event.metadata.label;
        }
        if (!event.metadata.subjectId.empty()) {
            text += " subject=" + event.metadata.subjectId;
        }
        if (event.metadata.outcome != EditorEventOutcome::None) {
            text += " outcome=" + std::string{editorEventOutcomeName(event.metadata.outcome)};
        }
        if (event.metadata.severity != EditorEventSeverity::Info) {
            text += " severity=" + std::string{editorEventSeverityName(event.metadata.severity)};
        }
        if (!event.metadata.message.empty()) {
            text += " - " + event.metadata.message;
        }
        return text;
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
