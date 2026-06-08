#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "editor_selection.hpp"

namespace asharia::editor {

    enum class EditorInspectorValueKind {
        Empty,
        Text,
        Mixed,
    };

    enum class EditorInspectorValidationSeverity {
        Info,
        Warning,
        Error,
    };

    struct EditorInspectorText {
        std::string key;
        std::string fallback;
    };

    struct EditorInspectorValue {
        EditorInspectorValueKind kind{EditorInspectorValueKind::Empty};
        std::string text;
    };

    struct EditorInspectorValidationMessage {
        EditorInspectorValidationSeverity severity{EditorInspectorValidationSeverity::Info};
        EditorInspectorText message;
    };

    struct EditorInspectorRow {
        std::string stableId;
        EditorInspectorText label;
        EditorInspectorValue value;
        bool readOnly{true};
        std::vector<EditorInspectorValidationMessage> validation;
    };

    struct EditorInspectorSection {
        std::string stableId;
        EditorInspectorText title;
        bool readOnly{true};
        std::vector<EditorInspectorRow> rows;
        std::vector<EditorInspectorValidationMessage> validation;
    };

    struct EditorInspectorModel {
        EditorInspectorText title;
        EditorInspectorText summary;
        bool readOnly{true};
        std::size_t selectionCount{};
        std::uint64_t selectionRevision{};
        std::vector<EditorInspectorSection> sections;

        [[nodiscard]] bool empty() const noexcept;
        [[nodiscard]] bool hasMixedValues() const noexcept;
        [[nodiscard]] bool hasValidation() const noexcept;
    };

    struct EditorInspectorModelBuildInput {
        const EditorSelectionSnapshot& selection;
        std::size_t undoDepth{};
        std::size_t redoDepth{};
    };

    [[nodiscard]] EditorInspectorModel
    buildEditorInspectorModel(const EditorInspectorModelBuildInput& input);
    [[nodiscard]] const EditorInspectorSection*
    findEditorInspectorSection(const EditorInspectorModel& model, std::string_view stableId);
    [[nodiscard]] const EditorInspectorRow*
    findEditorInspectorRow(const EditorInspectorSection& section, std::string_view stableId);

} // namespace asharia::editor
