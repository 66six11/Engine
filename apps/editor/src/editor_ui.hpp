#pragma once

#include <imgui.h>
#include <span>
#include <string_view>

namespace asharia::editor {

    enum class EditorUiTone {
        Neutral,
        Info,
        Success,
        Warning,
        Danger,
        Muted,
    };

    struct EditorUiProperty {
        std::string_view label;
        std::string_view value;
    };

    struct EditorUiColorToken {
        std::string_view name;
        ImVec4 color;
        std::string_view usage;
    };

    struct EditorUiTheme {
        std::string_view name;
        ImVec4 appBackground;
        ImVec4 panelBackground;
        ImVec4 panelBackgroundAlt;
        ImVec4 surface;
        ImVec4 surfaceHover;
        ImVec4 surfaceActive;
        ImVec4 border;
        ImVec4 borderStrong;
        ImVec4 text;
        ImVec4 textMuted;
        ImVec4 accent;
        ImVec4 accentHover;
        ImVec4 accentActive;
        ImVec4 info;
        ImVec4 success;
        ImVec4 warning;
        ImVec4 danger;
        float windowRounding{0.0F};
        float childRounding{0.0F};
        float frameRounding{0.0F};
        float popupRounding{0.0F};
        float tabRounding{0.0F};
    };

    [[nodiscard]] const EditorUiTheme& editorUiTheme();
    [[nodiscard]] std::span<const EditorUiColorToken> editorUiColorTokens();
    void applyEditorUiTheme();
    void drawEditorUiSectionHeader(std::string_view label);
    bool beginEditorUiPropertyTable(std::string_view tableIdentifier, float labelWidth);
    void drawEditorUiProperty(const EditorUiProperty& property);
    void endEditorUiPropertyTable();
    void drawEditorUiStatusPill(std::string_view label, EditorUiTone tone);
    void drawEditorUiColorSwatch(std::string_view label, ImVec4 color);

} // namespace asharia::editor
