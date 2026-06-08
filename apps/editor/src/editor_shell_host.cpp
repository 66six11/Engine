#include "editor_shell_host.hpp"

#include "editor_action.hpp"
#include "editor_dirty_state.hpp"
#include "editor_frame_debugger.hpp"
#include "editor_i18n.hpp"
#include "editor_panel.hpp"
#include "editor_tool.hpp"
#include "editor_workspace.hpp"
#include "imgui_editor_shell.hpp"

namespace asharia::editor {

    void drawEditorShellFrame(EditorActionRegistry& actionRegistry,
                              EditorActionServices& actionServices,
                              EditorFrameDebugger& frameDebugger,
                              const EditorDirtyState& dirtyState, EditorI18n& i18n,
                              EditorPanelRegistry& panelRegistry, EditorToolRegistry& toolRegistry,
                              EditorWorkspaceController& workspace,
                              const EditorFrameUiContext& uiContext) {
        const EditorActionInvokeContext actionInvoke =
            makeEditorActionInvokeContext(actionServices);
        auto dockspaceContext = EditorDockspaceContext{
            .panels = panelRegistry,
            .i18n = i18n,
            .workspace = workspace,
        };
        const auto menuContext = EditorMenuContext{
            .panels = panelRegistry,
            .i18n = i18n,
            .actionInvoke = actionInvoke,
        };
        const auto commandBarContext = EditorCommandBarContext{
            .i18n = i18n,
            .tools = toolRegistry,
            .actionInvoke = actionInvoke,
        };
        const auto statusBarContext = EditorStatusBarContext{
            .ui = uiContext,
            .panels = panelRegistry,
            .frameDebugger = frameDebugger,
            .dirtyState = dirtyState,
        };

        drawEditorMainMenu(actionRegistry, menuContext);
        drawEditorCommandBar(actionRegistry, commandBarContext);
        drawEditorStatusBar(statusBarContext);
        drawEditorDockspace(dockspaceContext);
    }

} // namespace asharia::editor
