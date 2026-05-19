#include "editor_context.hpp"

namespace asharia::editor {

    EditorContext::EditorContext(EditorPanelRegistry& panelRegistry, EditorEventQueue& eventQueue)
        : panelRegistry_(panelRegistry), eventQueue_(eventQueue) {}

    EditorPanelRegistry& EditorContext::panelRegistry() {
        return panelRegistry_;
    }

    const EditorPanelRegistry& EditorContext::panelRegistry() const {
        return panelRegistry_;
    }

    EditorEventQueue& EditorContext::eventQueue() {
        return eventQueue_;
    }

    const EditorEventQueue& EditorContext::eventQueue() const {
        return eventQueue_;
    }

} // namespace asharia::editor
