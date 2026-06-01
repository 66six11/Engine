#pragma once

#include "editor_action.hpp"

namespace asharia::editor {

    class EditorFrameDebugger;
    class EditorI18n;
    class EditorPanelRegistry;
    class EditorToolRegistry;
    class EditorWorkspaceController;
    struct EditorFrameUiContext;

    struct EditorDockspaceContext {
        const EditorPanelRegistry& panels;
        const EditorI18n& i18n;
        EditorWorkspaceController& workspace;
    };

    struct EditorMenuContext {
        const EditorPanelRegistry& panels;
        const EditorI18n& i18n;
        EditorActionInvokeContext actionInvoke;
    };

    struct EditorCommandBarContext {
        const EditorI18n& i18n;
        const EditorToolRegistry& tools;
        EditorActionInvokeContext actionInvoke;
    };

    struct EditorStatusBarContext {
        const EditorFrameUiContext& ui;
        const EditorPanelRegistry& panels;
        const EditorFrameDebugger& frameDebugger;
    };

    void drawEditorDockspace(EditorDockspaceContext& context);
    void drawEditorMainMenu(EditorActionRegistry& actionRegistry, const EditorMenuContext& context);
    void drawEditorCommandBar(EditorActionRegistry& actionRegistry,
                              const EditorCommandBarContext& context);
    void drawEditorStatusBar(const EditorStatusBarContext& context);

} // namespace asharia::editor
