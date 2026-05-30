#pragma once

#include "editor_event.hpp"
#include "editor_tool.hpp"

namespace asharia::editor {

    class EditorI18n;
    class EditorSettingsController;
    class EditorWorkspaceController;

    class EditorContext {
    public:
        EditorContext(EditorEventQueue& eventQueue, EditorDiagnosticsLog& diagnosticsLog,
                      EditorI18n& i18n, EditorSettingsController& settings,
                      EditorWorkspaceController& workspace, EditorToolRegistry& tools);

        [[nodiscard]] EditorEventQueue& eventQueue();
        [[nodiscard]] const EditorEventQueue& eventQueue() const;
        [[nodiscard]] EditorDiagnosticsLog& diagnosticsLog();
        [[nodiscard]] const EditorDiagnosticsLog& diagnosticsLog() const;
        [[nodiscard]] EditorI18n& i18n();
        [[nodiscard]] const EditorI18n& i18n() const;
        [[nodiscard]] EditorSettingsController& settings();
        [[nodiscard]] const EditorSettingsController& settings() const;
        [[nodiscard]] EditorWorkspaceController& workspace();
        [[nodiscard]] const EditorWorkspaceController& workspace() const;
        [[nodiscard]] EditorToolRegistry& tools();
        [[nodiscard]] const EditorToolRegistry& tools() const;

    private:
        EditorEventQueue& eventQueue_;
        EditorDiagnosticsLog& diagnosticsLog_;
        EditorI18n& i18n_;
        EditorSettingsController& settings_;
        EditorWorkspaceController& workspace_;
        EditorToolRegistry& tools_;
    };

} // namespace asharia::editor
