#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
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
