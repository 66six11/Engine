#include "editor_context.hpp"

#include "editor_i18n.hpp"
#include "editor_settings.hpp"
#include "editor_workspace.hpp"

namespace asharia::editor {

    EditorContext::EditorContext(EditorPanelRegistry& panelRegistry, EditorEventQueue& eventQueue,
                                 EditorDiagnosticsLog& diagnosticsLog,
                                 EditorFrameDebugger& frameDebugger, EditorI18n& i18n,
                                 EditorSettingsController& settings,
                                 EditorWorkspaceController& workspace, EditorToolRegistry& tools)
        : panelRegistry_(panelRegistry), eventQueue_(eventQueue), diagnosticsLog_(diagnosticsLog),
          frameDebugger_(frameDebugger), i18n_(i18n), settings_(settings),
          workspace_(workspace), tools_(tools) {}

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

    EditorI18n& EditorContext::i18n() {
        return i18n_;
    }

    const EditorI18n& EditorContext::i18n() const {
        return i18n_;
    }

    EditorSettingsController& EditorContext::settings() {
        return settings_;
    }

    const EditorSettingsController& EditorContext::settings() const {
        return settings_;
    }

    EditorWorkspaceController& EditorContext::workspace() {
        return workspace_;
    }

    const EditorWorkspaceController& EditorContext::workspace() const {
        return workspace_;
    }

    EditorToolRegistry& EditorContext::tools() {
        return tools_;
    }

    const EditorToolRegistry& EditorContext::tools() const {
        return tools_;
    }

} // namespace asharia::editor
