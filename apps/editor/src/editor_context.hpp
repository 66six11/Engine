#pragma once

#include "editor_event.hpp"
#include "editor_panel.hpp"

namespace asharia::editor {

    class EditorFrameDebugger;
    class EditorI18n;
    class EditorSettingsController;

    class EditorContext {
    public:
        EditorContext(EditorPanelRegistry& panelRegistry, EditorEventQueue& eventQueue,
                      EditorDiagnosticsLog& diagnosticsLog, EditorFrameDebugger& frameDebugger,
                      EditorI18n& i18n, EditorSettingsController& settings);

        [[nodiscard]] EditorPanelRegistry& panelRegistry();
        [[nodiscard]] const EditorPanelRegistry& panelRegistry() const;
        [[nodiscard]] EditorEventQueue& eventQueue();
        [[nodiscard]] const EditorEventQueue& eventQueue() const;
        [[nodiscard]] EditorDiagnosticsLog& diagnosticsLog();
        [[nodiscard]] const EditorDiagnosticsLog& diagnosticsLog() const;
        [[nodiscard]] EditorFrameDebugger& frameDebugger();
        [[nodiscard]] const EditorFrameDebugger& frameDebugger() const;
        [[nodiscard]] EditorI18n& i18n();
        [[nodiscard]] const EditorI18n& i18n() const;
        [[nodiscard]] EditorSettingsController& settings();
        [[nodiscard]] const EditorSettingsController& settings() const;

    private:
        EditorPanelRegistry& panelRegistry_;
        EditorEventQueue& eventQueue_;
        EditorDiagnosticsLog& diagnosticsLog_;
        EditorFrameDebugger& frameDebugger_;
        EditorI18n& i18n_;
        EditorSettingsController& settings_;
    };

} // namespace asharia::editor
