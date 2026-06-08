#include "panels/inspector_panel.hpp"

#include <imgui.h>
#include <string>
#include <string_view>

#include "editor_command.hpp"
#include "editor_i18n.hpp"
#include "editor_ui.hpp"

namespace {

    [[nodiscard]] std::string textValue(const asharia::editor::EditorI18n& i18n,
                                        std::string_view key, std::string_view fallback) {
        return std::string{i18n.text(asharia::editor::EditorI18nTextQuery{
            .key = key,
            .fallback = fallback,
        })};
    }

    void mutedText(std::string_view value) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextUnformatted(value.data(), value.data() + value.size());
        ImGui::PopStyleColor();
    }

} // namespace

namespace asharia::editor {

    const EditorPanelDesc& InspectorPanel::desc() const {
        return desc_;
    }

    void InspectorPanel::drawInspectorPanel(EditorInspectorPanelDrawContext& context,
                                            EditorPanelState& state) {
        static_cast<void>(state);

        const EditorI18n& i18n = context.ui.i18n;
        ImGui::TextUnformatted(textValue(i18n, "inspector.noSelection", "No Selection").c_str());
        mutedText(textValue(i18n, "inspector.noSelection.detail",
                            "Scene selection will drive this panel when SelectionSet lands."));
        ImGui::Spacing();
        drawEditorUiStatusPill(textValue(i18n, "inspector.state.shell", "Shell"),
                               EditorUiTone::Muted);
        ImGui::SameLine();
        drawEditorUiStatusPill(textValue(i18n, "inspector.state.noMutation", "No mutation"),
                               EditorUiTone::Warning);
        ImGui::Separator();

        if (beginEditorUiPropertyTable("inspector-shell-state", 116.0F)) {
            drawEditorUiProperty(EditorUiProperty{
                .label = textValue(i18n, "inspector.selection", "Selection"),
                .value = textValue(i18n, "inspector.selection.none", "None"),
            });
            drawEditorUiProperty(EditorUiProperty{
                .label = textValue(i18n, "inspector.undoDepth", "Undo"),
                .value = std::to_string(context.commandHistory.undoDepth()),
            });
            drawEditorUiProperty(EditorUiProperty{
                .label = textValue(i18n, "inspector.redoDepth", "Redo"),
                .value = std::to_string(context.commandHistory.redoDepth()),
            });
            drawEditorUiProperty(EditorUiProperty{
                .label = textValue(i18n, "inspector.model", "Model"),
                .value = textValue(i18n, "inspector.model.pending", "Inspector data model pending"),
            });
            endEditorUiPropertyTable();
        }

        ImGui::BeginDisabled();
        ImGui::Button(textValue(i18n, "inspector.addComponent", "Add Component").c_str(),
                      ImVec2{-1.0F, 0.0F});
        ImGui::EndDisabled();
    }

} // namespace asharia::editor
