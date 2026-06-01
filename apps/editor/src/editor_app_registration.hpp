#pragma once

#include "asharia/core/result.hpp"

namespace asharia::editor {
    class EditorActionRegistry;
    class EditorEventQueue;
    class EditorPanelRegistry;
    class EditorToolRegistry;

    [[nodiscard]] asharia::VoidResult
    registerEditorAppRegistries(EditorPanelRegistry& panelRegistry,
                                EditorActionRegistry& actionRegistry,
                                EditorToolRegistry& toolRegistry,
                                EditorEventQueue& eventQueue);

    [[nodiscard]] asharia::VoidResult
    registerEditorPanels(EditorPanelRegistry& panelRegistry);

    [[nodiscard]] asharia::VoidResult
    registerEditorActions(EditorActionRegistry& actionRegistry);

    [[nodiscard]] asharia::VoidResult
    registerEditorTools(EditorToolRegistry& toolRegistry);
} // namespace asharia::editor
