#include "imgui_editor_shell.hpp"

#include <imgui.h>
#include <string>
#include <string_view>

#include "editor_i18n.hpp"

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

            const std::string label = editorContext.i18n().label(EditorI18nLabelDesc{
                .key = action->labelKey,
                .stableId = action->id.value,
                .fallback = action->label,
            });
            const char* shortcut = action->shortcut.empty() ? nullptr : action->shortcut.c_str();
            if (ImGui::MenuItem(label.c_str(), shortcut, selected, action->enabled)) {
                static_cast<void>(actionRegistry.invoke(action->id.value, editorContext));
            }
        }

    } // namespace

    void drawEditorMainMenu(EditorActionRegistry& actionRegistry, EditorContext& editorContext) {
        if (ImGui::BeginMainMenuBar()) {
            const EditorI18n& i18n = editorContext.i18n();
            const std::string fileMenu = i18n.label(EditorI18nLabelDesc{
                .key = "menu.file",
                .stableId = "menu.file",
                .fallback = "File",
            });
            const std::string viewMenu = i18n.label(EditorI18nLabelDesc{
                .key = "menu.view",
                .stableId = "menu.view",
                .fallback = "View",
            });
            const std::string debugMenu = i18n.label(EditorI18nLabelDesc{
                .key = "menu.debug",
                .stableId = "menu.debug",
                .fallback = "Debug",
            });

            if (ImGui::BeginMenu(fileMenu.c_str())) {
                drawActionMenuItem(actionRegistry, editorContext, "file.new-scene");
                drawActionMenuItem(actionRegistry, editorContext, "file.open");
                ImGui::Separator();
                drawActionMenuItem(actionRegistry, editorContext, "file.exit");
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu(viewMenu.c_str())) {
                drawActionMenuItem(actionRegistry, editorContext, "view.scene-view",
                                   editorContext.panelRegistry().isOpen("scene-view"));
                drawActionMenuItem(actionRegistry, editorContext, "view.log",
                                   editorContext.panelRegistry().isOpen("log"));
                drawActionMenuItem(actionRegistry, editorContext, "view.render-graph",
                                   editorContext.panelRegistry().isOpen("render-graph"));
                drawActionMenuItem(actionRegistry, editorContext, "view.frame-debugger",
                                   editorContext.panelRegistry().isOpen("frame-debugger"));
                ImGui::Separator();
                drawActionMenuItem(actionRegistry, editorContext, "view.ui-style-preview",
                                   editorContext.panelRegistry().isOpen("ui-style-preview"));
                drawActionMenuItem(actionRegistry, editorContext, "view.editor-settings",
                                   editorContext.panelRegistry().isOpen("editor-settings"));
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu(debugMenu.c_str())) {
                drawActionMenuItem(actionRegistry, editorContext, "debug.capture-frame");
                drawActionMenuItem(actionRegistry, editorContext, "debug.resume-frame");
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }
    }

} // namespace asharia::editor
