#include "editor_context.hpp"

namespace asharia::editor {

    EditorContext::EditorContext(EditorPanelRegistry& panelRegistry, EditorEventQueue& eventQueue,
                                 EditorDiagnosticsLog& diagnosticsLog)
        : panelRegistry_(panelRegistry), eventQueue_(eventQueue), diagnosticsLog_(diagnosticsLog) {}

    EditorPanelRegistry& EditorContext::panelRegistry() {
        return panelRegistry_;
    }

    const EditorPanelRegistry& EditorContext::panelRegistry() const {
        return panelRegistry_;
    }

    EditorEventQueue& EditorContext::eventQueue() {
        return eventQueue_;
    }

    const EditorEventQueue& EditorContext::eventQueue() const {
        return eventQueue_;
    }

    EditorDiagnosticsLog& EditorContext::diagnosticsLog() {
        return diagnosticsLog_;
    }

    const EditorDiagnosticsLog& EditorContext::diagnosticsLog() const {
        return diagnosticsLog_;
    }

} // namespace asharia::editor
