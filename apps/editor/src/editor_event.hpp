#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "editor_id.hpp"

namespace asharia::editor {

    enum class EditorEventKind {
        PanelOpened,
        PanelClosed,
        PanelFocused,
        ActionInvoked,
        ViewportResized,
        SelectionChanged,
        DirtyStateChanged,
        CommandHistoryChanged,
        ViewportToolStateChanged,
        ValidationReported,
    };

    enum class EditorEventSeverity {
        Info,
        Warning,
        Error,
    };

    enum class EditorEventOutcome {
        None,
        Succeeded,
        Failed,
        Noop,
    };

    struct EditorEventMetadata {
        std::uint64_t revision{};
        std::string subjectId;
        std::string label;
        std::string message;
        EditorEventSeverity severity{EditorEventSeverity::Info};
        EditorEventOutcome outcome{EditorEventOutcome::None};
    };

    struct EditorEvent {
        EditorEventKind kind{};
        EditorId sourceId;
        EditorEventMetadata metadata;
    };

    [[nodiscard]] std::string_view editorEventKindName(EditorEventKind kind);
    [[nodiscard]] std::string_view editorEventSeverityName(EditorEventSeverity severity);
    [[nodiscard]] std::string_view editorEventOutcomeName(EditorEventOutcome outcome);
    [[nodiscard]] std::string editorEventDisplayText(const EditorEvent& event);

    class EditorEventQueue {
    public:
        void push(EditorEvent event);
        void clear();
        [[nodiscard]] bool empty() const;
        [[nodiscard]] std::span<const EditorEvent> events() const;

    private:
        std::vector<EditorEvent> events_;
    };

    struct EditorDiagnosticEvent {
        std::uint64_t sequence{};
        EditorEvent event;
    };

    class EditorDiagnosticsLog {
    public:
        void appendEvents(std::span<const EditorEvent> events);
        [[nodiscard]] std::span<const EditorDiagnosticEvent> recentEvents() const;

    private:
        static constexpr std::size_t kMaxRecentEvents = 64;

        std::vector<EditorDiagnosticEvent> recentEvents_;
        std::uint64_t nextSequence_{1};
    };

} // namespace asharia::editor
