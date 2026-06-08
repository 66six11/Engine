#include "panels/inspector_panel.hpp"

#include <imgui.h>
#include <string>
#include <string_view>

#include "editor_command.hpp"
#include "editor_i18n.hpp"
#include "editor_inspector_model.hpp"
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

    [[nodiscard]] std::string textValue(const asharia::editor::EditorI18n& i18n,
                                        const asharia::editor::EditorInspectorText& text) {
        return textValue(i18n, text.key, text.fallback);
    }

    void mutedText(std::string_view value) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextUnformatted(value.data(), value.data() + value.size());
        ImGui::PopStyleColor();
    }

    [[nodiscard]] std::string
    inspectorValueText(const asharia::editor::EditorI18n& i18n,
                       const asharia::editor::EditorInspectorValue& value) {
        switch (value.kind) {
        case asharia::editor::EditorInspectorValueKind::Text:
            return value.text;
        case asharia::editor::EditorInspectorValueKind::Mixed:
            return textValue(i18n, "inspector.value.mixed", "Mixed");
        case asharia::editor::EditorInspectorValueKind::Empty:
        default:
            return textValue(i18n, "inspector.value.empty", "-");
        }
    }

    [[nodiscard]] std::string
    validationSeverityText(const asharia::editor::EditorI18n& i18n,
                           asharia::editor::EditorInspectorValidationSeverity severity) {
        switch (severity) {
        case asharia::editor::EditorInspectorValidationSeverity::Error:
            return textValue(i18n, "inspector.validation.error", "Error");
        case asharia::editor::EditorInspectorValidationSeverity::Warning:
            return textValue(i18n, "inspector.validation.warning", "Warning");
        case asharia::editor::EditorInspectorValidationSeverity::Info:
        default:
            return textValue(i18n, "inspector.validation.info", "Info");
        }
    }

    void drawInspectorSection(const asharia::editor::EditorI18n& i18n,
                              const asharia::editor::EditorInspectorSection& section) {
        asharia::editor::drawEditorUiSectionHeader(textValue(i18n, section.title));
        if (asharia::editor::beginEditorUiPropertyTable(section.stableId, 116.0F)) {
            for (const asharia::editor::EditorInspectorRow& row : section.rows) {
                asharia::editor::drawEditorUiProperty(asharia::editor::EditorUiProperty{
                    .label = textValue(i18n, row.label),
                    .value = inspectorValueText(i18n, row.value),
                });
                for (const asharia::editor::EditorInspectorValidationMessage& message :
                     row.validation) {
                    asharia::editor::drawEditorUiProperty(asharia::editor::EditorUiProperty{
                        .label = validationSeverityText(i18n, message.severity),
                        .value = textValue(i18n, message.message),
                    });
                }
            }
            for (const asharia::editor::EditorInspectorValidationMessage& message :
                 section.validation) {
                asharia::editor::drawEditorUiProperty(asharia::editor::EditorUiProperty{
                    .label = validationSeverityText(i18n, message.severity),
                    .value = textValue(i18n, message.message),
                });
            }
            asharia::editor::endEditorUiPropertyTable();
        }
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
        const EditorInspectorModel model = buildEditorInspectorModel(EditorInspectorModelBuildInput{
            .selection = context.selection.snapshot(),
            .dirtySnapshot = &context.dirtyState.snapshot(),
            .undoDepth = static_cast<std::size_t>(context.commandHistory.undoDepth()),
            .redoDepth = static_cast<std::size_t>(context.commandHistory.redoDepth()),
        });
        ImGui::TextUnformatted(textValue(i18n, model.title).c_str());
        mutedText(textValue(i18n, model.summary));
        ImGui::Spacing();
        drawEditorUiStatusPill(textValue(i18n, "inspector.state.shell", "Shell"),
                               EditorUiTone::Muted);
        ImGui::SameLine();
        drawEditorUiStatusPill(textValue(i18n, "inspector.state.readOnly", "Read-only"),
                               EditorUiTone::Info);

        for (const EditorInspectorSection& section : model.sections) {
            drawInspectorSection(i18n, section);
        }

        ImGui::BeginDisabled();
        ImGui::Button(textValue(i18n, "inspector.addComponent", "Add Component").c_str(),
                      ImVec2{-1.0F, 0.0F});
        ImGui::EndDisabled();
    }

} // namespace asharia::editor
