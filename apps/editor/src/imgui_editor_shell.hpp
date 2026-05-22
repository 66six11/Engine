#pragma once

#include "editor_action.hpp"
#include "editor_context.hpp"
#include "editor_panel.hpp"

namespace asharia::editor {

    void drawEditorDockspace(const EditorContext& editorContext);
    void drawEditorMainMenu(EditorActionRegistry& actionRegistry, EditorContext& editorContext);
    void drawEditorCommandBar(EditorActionRegistry& actionRegistry, EditorContext& editorContext);
    void drawEditorStatusBar(const EditorFrameContext& frameContext,
                             const EditorContext& editorContext);

} // namespace asharia::editor
