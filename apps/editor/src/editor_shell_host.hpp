#pragma once

namespace asharia::editor {

    class EditorActionRegistry;
    class EditorDirtyState;
    class EditorFrameDebugger;
    class EditorI18n;
    class EditorPanelRegistry;
    class EditorToolRegistry;
    class EditorWorkspaceController;
    struct EditorActionServices;
    struct EditorFrameUiContext;

    void drawEditorShellFrame(EditorActionRegistry& actionRegistry,
                              EditorActionServices& actionServices,
                              EditorFrameDebugger& frameDebugger,
                              const EditorDirtyState& dirtyState, EditorI18n& i18n,
                              EditorPanelRegistry& panelRegistry, EditorToolRegistry& toolRegistry,
                              EditorWorkspaceController& workspace,
                              const EditorFrameUiContext& uiContext);

} // namespace asharia::editor
