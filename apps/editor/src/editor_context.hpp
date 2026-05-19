#pragma once

#include "editor_event.hpp"
#include "editor_panel.hpp"

namespace asharia::editor {

    class EditorContext {
    public:
        EditorContext(EditorPanelRegistry& panelRegistry, EditorEventQueue& eventQueue,
                      EditorDiagnosticsLog& diagnosticsLog);

        [[nodiscard]] EditorPanelRegistry& panelRegistry();
        [[nodiscard]] const EditorPanelRegistry& panelRegistry() const;
        [[nodiscard]] EditorEventQueue& eventQueue();
        [[nodiscard]] const EditorEventQueue& eventQueue() const;
        [[nodiscard]] EditorDiagnosticsLog& diagnosticsLog();
        [[nodiscard]] const EditorDiagnosticsLog& diagnosticsLog() const;

    private:
        EditorPanelRegistry& panelRegistry_;
        EditorEventQueue& eventQueue_;
        EditorDiagnosticsLog& diagnosticsLog_;
    };

} // namespace asharia::editor
