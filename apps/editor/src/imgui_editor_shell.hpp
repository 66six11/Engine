#pragma once

#include "editor_action.hpp"
#include "editor_context.hpp"
#include "editor_panel.hpp"

namespace asharia::editor {

    void drawEditorDockspace();
    void drawEditorMainMenu(EditorActionRegistry& actionRegistry, EditorContext& editorContext);

} // namespace asharia::editor
