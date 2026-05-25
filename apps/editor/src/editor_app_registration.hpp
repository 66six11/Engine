#pragma once

#include "asharia/core/result.hpp"

namespace asharia::editor {
    class EditorPanelRegistry;
    class EditorActionRegistry;
    class EditorToolRegistry;

    [[nodiscard]] asharia::VoidResult
    registerEditorPanels(EditorPanelRegistry& panelRegistry);

    [[nodiscard]] asharia::VoidResult
    registerEditorActions(EditorActionRegistry& actionRegistry);

    [[nodiscard]] asharia::VoidResult
    registerEditorTools(EditorToolRegistry& toolRegistry);
} // namespace asharia::editor
