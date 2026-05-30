#include "editor_context.hpp"

#include "editor_i18n.hpp"
#include "editor_settings.hpp"
#include "editor_workspace.hpp"

namespace asharia::editor {

    EditorContext::EditorContext(EditorEventQueue& eventQueue, EditorDiagnosticsLog& diagnosticsLog,
                                 EditorI18n& i18n, EditorSettingsController& settings,
                                 EditorWorkspaceController& workspace, EditorToolRegistry& tools)
        : eventQueue_(eventQueue), diagnosticsLog_(diagnosticsLog), i18n_(i18n),
          settings_(settings), workspace_(workspace), tools_(tools) {}

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
