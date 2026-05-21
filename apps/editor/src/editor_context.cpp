#include "editor_context.hpp"

namespace asharia::editor {

    EditorContext::EditorContext(EditorPanelRegistry& panelRegistry, EditorEventQueue& eventQueue,
                                 EditorDiagnosticsLog& diagnosticsLog,
                                 EditorFrameDebugger& frameDebugger)
        : panelRegistry_(panelRegistry), eventQueue_(eventQueue), diagnosticsLog_(diagnosticsLog),
          frameDebugger_(frameDebugger) {}

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

    EditorFrameDebugger& EditorContext::frameDebugger() {
        return frameDebugger_;
    }

    const EditorFrameDebugger& EditorContext::frameDebugger() const {
        return frameDebugger_;
    }

} // namespace asharia::editor
