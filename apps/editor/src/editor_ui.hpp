#pragma once

#include <cstdint>
#include <imgui.h>
#include <optional>
#include <span>
#include <string_view>

namespace asharia::editor {

    enum class EditorUiThemeId {
        ClassicBlueGray,
        WarmGraphiteAmber,
        ForestGreen,
        PurpleElectric,
        CarbonCopper,
        CoolGrayTeal,
        LightGraphiteOrange,
    };

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

    struct ColorSrgba8 {
        std::uint8_t r{};
        std::uint8_t g{};
        std::uint8_t b{};
        std::uint8_t a{255};
    };

    struct EditorUiColorToken {
        std::string_view name;
        ColorSrgba8 color;
        std::string_view usage;
    };

    struct EditorUiTheme {
        EditorUiThemeId id{EditorUiThemeId::ClassicBlueGray};
        std::string_view storageName;
        std::string_view name;
        ColorSrgba8 appBackground;
        ColorSrgba8 panelBackground;
        ColorSrgba8 floatingBackground;
        ColorSrgba8 panelBackgroundAlt;
        ColorSrgba8 titleBackground;
        ColorSrgba8 menuBackground;
        ColorSrgba8 inputBackground;
        ColorSrgba8 surface;
        ColorSrgba8 surfaceHover;
        ColorSrgba8 surfaceActive;
        ColorSrgba8 border;
        ColorSrgba8 borderStrong;
        ColorSrgba8 divider;
        ColorSrgba8 text;
        ColorSrgba8 textSecondary;
        ColorSrgba8 textMuted;
        ColorSrgba8 textDisabled;
        ColorSrgba8 accent;
        ColorSrgba8 accentHover;
        ColorSrgba8 accentActive;
        ColorSrgba8 selection;
        ColorSrgba8 viewportBackground;
        ColorSrgba8 info;
        ColorSrgba8 success;
        ColorSrgba8 warning;
        ColorSrgba8 danger;
        float windowRounding{0.0F};
        float childRounding{0.0F};
        float frameRounding{0.0F};
        float popupRounding{0.0F};
        float tabRounding{0.0F};
    };

    [[nodiscard]] EditorUiThemeId defaultEditorUiThemeId();
    [[nodiscard]] EditorUiThemeId currentEditorUiThemeId();
    [[nodiscard]] std::string_view editorUiThemeName(EditorUiThemeId themeId);
    [[nodiscard]] std::optional<EditorUiThemeId> editorUiThemeIdFromName(std::string_view name);
    [[nodiscard]] std::span<const EditorUiTheme> editorUiThemes();
    [[nodiscard]] const EditorUiTheme& editorUiTheme(EditorUiThemeId themeId);
    [[nodiscard]] const EditorUiTheme& editorUiTheme();
    [[nodiscard]] std::span<const EditorUiColorToken> editorUiColorTokens();
    [[nodiscard]] ImVec4 toImGuiEncodedSrgbVec4(ColorSrgba8 color);
    [[nodiscard]] ImU32 toImGuiEncodedSrgbU32(ColorSrgba8 color);
    void applyEditorUiTheme(EditorUiThemeId themeId);
    void applyEditorUiTheme();
    void drawEditorUiSectionHeader(std::string_view label);
    bool beginEditorUiPropertyTable(std::string_view tableIdentifier, float labelWidth);
    void drawEditorUiProperty(const EditorUiProperty& property);
    void endEditorUiPropertyTable();
    void drawEditorUiStatusPill(std::string_view label, EditorUiTone tone);
    void drawEditorUiColorSwatch(std::string_view label, ColorSrgba8 color);

} // namespace asharia::editor
