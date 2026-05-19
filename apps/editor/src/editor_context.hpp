#pragma once

#include "editor_event.hpp"
#include "editor_panel.hpp"

namespace asharia::editor {

    class EditorContext {
    public:
        EditorContext(EditorPanelRegistry& panelRegistry, EditorEventQueue& eventQueue);

        [[nodiscard]] EditorPanelRegistry& panelRegistry();
        [[nodiscard]] const EditorPanelRegistry& panelRegistry() const;
        [[nodiscard]] EditorEventQueue& eventQueue();
        [[nodiscard]] const EditorEventQueue& eventQueue() const;

    private:
        EditorPanelRegistry& panelRegistry_;
        EditorEventQueue& eventQueue_;
    };

} // namespace asharia::editor
