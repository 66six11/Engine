#include "editor_inspector_model.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace asharia::editor {

    namespace {

        [[nodiscard]] EditorInspectorText text(std::string_view key, std::string_view fallback) {
            return EditorInspectorText{
                .key = std::string{key},
                .fallback = std::string{fallback},
            };
        }

        [[nodiscard]] EditorInspectorValue emptyValue() {
            return EditorInspectorValue{};
        }

        [[nodiscard]] EditorInspectorValue mixedValue() {
            return EditorInspectorValue{
                .kind = EditorInspectorValueKind::Mixed,
                .text = {},
            };
        }

        [[nodiscard]] EditorInspectorValue textValue(std::string value) {
            return EditorInspectorValue{
                .kind = EditorInspectorValueKind::Text,
                .text = std::move(value),
            };
        }

        [[nodiscard]] EditorInspectorValidationMessage
        validation(EditorInspectorValidationSeverity severity, std::string_view key,
                   std::string_view fallback) {
            return EditorInspectorValidationMessage{
                .severity = severity,
                .message = text(key, fallback),
            };
        }

        [[nodiscard]] std::string selectionItemName(const EditorSelectionItem& item) {
            if (!item.displayName.empty()) {
                return item.displayName;
            }
            return editorSelectionTargetLabel(item.target);
        }

        [[nodiscard]] std::string selectionSummary(const EditorSelectionSnapshot& selection) {
            if (selection.empty()) {
                return "None";
            }

            const EditorSelectionItem* primary = selection.primary();
            if (selection.size() == 1U && primary != nullptr) {
                return selectionItemName(*primary);
            }

            return std::to_string(selection.size()) + " entities";
        }

        [[nodiscard]] EditorInspectorValue targetValue(const EditorSelectionSnapshot& selection) {
            if (selection.empty()) {
                return emptyValue();
            }
            if (selection.size() > 1U) {
                return mixedValue();
            }
            return textValue(editorSelectionTargetLabel(selection.items.front().target));
        }

        [[nodiscard]] EditorInspectorValue primaryValue(const EditorSelectionSnapshot& selection) {
            const EditorSelectionItem* primary = selection.primary();
            if (primary == nullptr) {
                return emptyValue();
            }
            return textValue(selectionItemName(*primary));
        }

        [[nodiscard]] EditorInspectorValue stateValue(const EditorSelectionSnapshot& selection) {
            if (selection.empty()) {
                return emptyValue();
            }

            const EditorSelectionTargetState state = selection.items.front().state;
            const bool allSameState =
                std::ranges::all_of(selection.items, [state](const EditorSelectionItem& item) {
                    return item.state == state;
                });
            if (!allSameState) {
                return mixedValue();
            }
            return textValue(std::string{editorSelectionTargetStateName(state)});
        }

        [[nodiscard]] EditorInspectorRow row(std::string_view stableId, std::string_view labelKey,
                                             std::string_view labelFallback,
                                             EditorInspectorValue value) {
            return EditorInspectorRow{
                .stableId = std::string{stableId},
                .label = text(labelKey, labelFallback),
                .value = std::move(value),
                .readOnly = true,
                .validation = {},
            };
        }

        void appendSelectionValidation(EditorInspectorSection& section,
                                       const EditorSelectionSnapshot& selection) {
            EditorInspectorRow* missingRow = nullptr;
            EditorInspectorRow* staleRow = nullptr;
            for (EditorInspectorRow& candidate : section.rows) {
                if (candidate.stableId == "selection.missing") {
                    missingRow = &candidate;
                } else if (candidate.stableId == "selection.stale") {
                    staleRow = &candidate;
                }
            }

            if (selection.hasMissing() && missingRow != nullptr) {
                missingRow->validation.push_back(
                    validation(EditorInspectorValidationSeverity::Warning,
                               "inspector.validation.selectionMissing",
                               "Selection contains missing scene/entity targets."));
            }
            if (selection.hasStale() && staleRow != nullptr) {
                staleRow->validation.push_back(
                    validation(EditorInspectorValidationSeverity::Warning,
                               "inspector.validation.selectionStale",
                               "Selection contains stale scene/entity targets."));
            }
        }

        [[nodiscard]] EditorInspectorSection
        selectionSection(const EditorSelectionSnapshot& selection) {
            EditorInspectorSection section{
                .stableId = "selection",
                .title = text("inspector.selection", "Selection"),
                .readOnly = true,
                .rows = {},
                .validation = {},
            };
            section.rows.push_back(row("selection.summary", "inspector.selection.summary",
                                       "Summary", textValue(selectionSummary(selection))));
            section.rows.push_back(row("selection.count", "inspector.selection.count", "Selected",
                                       textValue(std::to_string(selection.size()))));
            section.rows.push_back(row("selection.primary", "inspector.selection.primary",
                                       "Primary", primaryValue(selection)));
            section.rows.push_back(row("selection.target", "inspector.selection.target", "Target",
                                       targetValue(selection)));
            section.rows.push_back(row("selection.state", "inspector.selection.state", "State",
                                       stateValue(selection)));
            section.rows.push_back(row("selection.missing", "inspector.selection.missing",
                                       "Missing",
                                       textValue(selection.hasMissing() ? "yes" : "no")));
            section.rows.push_back(row("selection.stale", "inspector.selection.stale", "Stale",
                                       textValue(selection.hasStale() ? "yes" : "no")));
            section.rows.push_back(row("selection.revision", "inspector.selection.revision",
                                       "Revision", textValue(std::to_string(selection.revision))));
            appendSelectionValidation(section, selection);
            return section;
        }

        [[nodiscard]] EditorInspectorSection commandSection(std::size_t undoDepth,
                                                            std::size_t redoDepth) {
            EditorInspectorSection section{
                .stableId = "command-history",
                .title = text("inspector.commandHistory", "Command History"),
                .readOnly = true,
                .rows = {},
                .validation = {},
            };
            section.rows.push_back(row("command.undoDepth", "inspector.undoDepth", "Undo",
                                       textValue(std::to_string(undoDepth))));
            section.rows.push_back(row("command.redoDepth", "inspector.redoDepth", "Redo",
                                       textValue(std::to_string(redoDepth))));
            return section;
        }

        [[nodiscard]] bool rowHasMixedValue(const EditorInspectorRow& row) noexcept {
            return row.value.kind == EditorInspectorValueKind::Mixed;
        }

        [[nodiscard]] bool rowHasValidation(const EditorInspectorRow& row) noexcept {
            return !row.validation.empty();
        }

        [[nodiscard]] bool sectionHasValidation(const EditorInspectorSection& section) noexcept {
            if (!section.validation.empty()) {
                return true;
            }
            return std::ranges::any_of(section.rows, rowHasValidation);
        }

    } // namespace

    bool EditorInspectorModel::empty() const noexcept {
        return selectionCount == 0U;
    }

    bool EditorInspectorModel::hasMixedValues() const noexcept {
        return std::ranges::any_of(sections, [](const EditorInspectorSection& section) {
            return std::ranges::any_of(section.rows, rowHasMixedValue);
        });
    }

    bool EditorInspectorModel::hasValidation() const noexcept {
        return std::ranges::any_of(sections, sectionHasValidation);
    }

    EditorInspectorModel buildEditorInspectorModel(const EditorInspectorModelBuildInput& input) {
        EditorInspectorModel model{
            .title = input.selection.empty() ? text("inspector.noSelection", "No Selection")
                                             : text("inspector.selection", "Selection"),
            .summary = text("inspector.selection.detail",
                            "Inspector data model is read-only until scene schema and "
                            "transactions land."),
            .readOnly = true,
            .selectionCount = input.selection.size(),
            .selectionRevision = input.selection.revision,
            .sections = {},
        };
        model.sections.push_back(selectionSection(input.selection));
        model.sections.push_back(commandSection(input.undoDepth, input.redoDepth));
        return model;
    }

    const EditorInspectorSection* findEditorInspectorSection(const EditorInspectorModel& model,
                                                             std::string_view stableId) {
        const auto found =
            std::ranges::find_if(model.sections, [stableId](const EditorInspectorSection& section) {
                return section.stableId == stableId;
            });
        if (found == model.sections.end()) {
            return nullptr;
        }
        return &(*found);
    }

    const EditorInspectorRow* findEditorInspectorRow(const EditorInspectorSection& section,
                                                     std::string_view stableId) {
        const auto found =
            std::ranges::find_if(section.rows, [stableId](const EditorInspectorRow& row) {
                return row.stableId == stableId;
            });
        if (found == section.rows.end()) {
            return nullptr;
        }
        return &(*found);
    }

} // namespace asharia::editor
