#include "imgui_editor_shell.hpp"

#include <imgui.h>
#include <string_view>

namespace asharia::editor {

    void drawEditorDockspace() {
        ImGui::DockSpaceOverViewport();
    }

    namespace {

        void drawActionMenuItem(EditorActionRegistry& actionRegistry, EditorContext& editorContext,
                                std::string_view actionId, bool selected = false) {
            const EditorActionDesc* action = actionRegistry.findAction(actionId);
            if (action == nullptr) {
                return;
            }

            const char* shortcut = action->shortcut.empty() ? nullptr : action->shortcut.c_str();
            if (ImGui::MenuItem(action->label.c_str(), shortcut, selected, action->enabled)) {
                static_cast<void>(actionRegistry.invoke(action->id.value, editorContext));
            }
        }

    } // namespace

    void drawEditorMainMenu(EditorActionRegistry& actionRegistry, EditorContext& editorContext) {
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                drawActionMenuItem(actionRegistry, editorContext, "file.new-scene");
                drawActionMenuItem(actionRegistry, editorContext, "file.open");
                ImGui::Separator();
                drawActionMenuItem(actionRegistry, editorContext, "file.exit");
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("View")) {
                drawActionMenuItem(actionRegistry, editorContext, "view.scene-view",
                                   editorContext.panelRegistry().isOpen("scene-view"));
                drawActionMenuItem(actionRegistry, editorContext, "view.render-graph",
                                   editorContext.panelRegistry().isOpen("render-graph"));
                drawActionMenuItem(actionRegistry, editorContext, "view.log",
                                   editorContext.panelRegistry().isOpen("log"));
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Debug")) {
                drawActionMenuItem(actionRegistry, editorContext, "debug.capture-frame");
                drawActionMenuItem(actionRegistry, editorContext, "debug.resume-frame");
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }
    }

} // namespace asharia::editor
