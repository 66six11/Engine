#include "panels/inspector_panel.hpp"

#include <imgui.h>
#include <string>
#include <string_view>

#include "editor_command.hpp"
#include "editor_i18n.hpp"
#include "editor_selection.hpp"
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

    [[nodiscard]] std::string yesNo(const asharia::editor::EditorI18n& i18n, bool value) {
        return textValue(i18n, value ? "common.yes" : "common.no", value ? "yes" : "no");
    }

    [[nodiscard]] std::string selectionSummary(
        const asharia::editor::EditorI18n& i18n,
        const asharia::editor::EditorSelectionSnapshot& snapshot) {
        if (snapshot.empty()) {
            return textValue(i18n, "inspector.selection.none", "None");
        }
        const asharia::editor::EditorSelectionItem* primary = snapshot.primary();
        if (snapshot.size() == 1U && primary != nullptr) {
            return primary->displayName.empty()
                       ? asharia::editor::editorSelectionTargetLabel(primary->target)
                       : primary->displayName;
        }
        return std::to_string(snapshot.size()) + " " +
               textValue(i18n, "inspector.selection.entities", "entities");
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
        const EditorSelectionSnapshot& selection = context.selection.snapshot();
        ImGui::TextUnformatted(
            textValue(i18n, selection.empty() ? "inspector.noSelection" : "inspector.selection",
                      selection.empty() ? "No Selection" : "Selection")
                .c_str());
        mutedText(textValue(i18n, "inspector.selection.detail",
                            "SelectionSet is read-only until Inspector data model lands."));
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
                .value = selectionSummary(i18n, selection),
            });
            drawEditorUiProperty(EditorUiProperty{
                .label = textValue(i18n, "inspector.selection.count", "Selected"),
                .value = std::to_string(selection.size()),
            });
            drawEditorUiProperty(EditorUiProperty{
                .label = textValue(i18n, "inspector.selection.missing", "Missing"),
                .value = yesNo(i18n, selection.hasMissing()),
            });
            drawEditorUiProperty(EditorUiProperty{
                .label = textValue(i18n, "inspector.selection.stale", "Stale"),
                .value = yesNo(i18n, selection.hasStale()),
            });
            drawEditorUiProperty(EditorUiProperty{
                .label = textValue(i18n, "inspector.selection.revision", "Revision"),
                .value = std::to_string(selection.revision),
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
