#include "panels/inspector_panel.hpp"

#include <imgui.h>
#include <string>
#include <string_view>

#include "editor_asset_icon.hpp"
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

    [[nodiscard]] asharia::editor::EditorIconDescriptor inspectorIcon(std::string_view iconName,
                                                                      std::string_view tooltip) {
        const asharia::editor::EditorUiTheme& theme = asharia::editor::editorUiTheme();
        return asharia::editor::makeLucideEditorIconDescriptor(
            iconName,
            asharia::editor::editorIconTint(static_cast<float>(theme.textSecondary.r) / 255.0F,
                                            static_cast<float>(theme.textSecondary.g) / 255.0F,
                                            static_cast<float>(theme.textSecondary.b) / 255.0F),
            {}, tooltip);
    }

    void drawInspectorSection(const asharia::editor::EditorI18n& i18n,
                              const asharia::editor::EditorInspectorSection& section) {
        if (!asharia::editor::drawEditorUiComponentHeader(section.stableId,
                                                          textValue(i18n, section.title))) {
            return;
        }
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
        asharia::editor::drawEditorUiPanelHeader(textValue(i18n, model.title),
                                                 textValue(i18n, model.summary));
        drawEditorUiStatusPill(textValue(i18n, "inspector.state.shell", "Shell"),
                               EditorUiTone::Muted);
        ImGui::SameLine();
        drawEditorUiStatusPill(textValue(i18n, "inspector.state.readOnly", "Read-only"),
                               EditorUiTone::Info);
        ImGui::SameLine();
        static_cast<void>(drawEditorUiIconButton(
            inspectorIcon("lock",
                          "Lock Inspector\nDisabled: Inspector lock is pending pinned-object "
                          "support."),
            "inspector-lock", false, false,
            "Lock Inspector\nDisabled: Inspector lock is pending pinned-object support."));
        ImGui::SameLine();
        static_cast<void>(drawEditorUiIconButton(
            inspectorIcon("pin",
                          "Pin Inspector\nDisabled: Inspector pin is pending comparison workflow "
                          "support."),
            "inspector-pin", false, false,
            "Pin Inspector\nDisabled: Inspector pin is pending comparison workflow support."));
        ImGui::Spacing();

        for (const EditorInspectorSection& section : model.sections) {
            drawInspectorSection(i18n, section);
        }

        ImGui::BeginDisabled();
        static_cast<void>(
            drawEditorUiCompactButton(textValue(i18n, "inspector.addComponent", "Add Component")));
        ImGui::EndDisabled();
    }

} // namespace asharia::editor
