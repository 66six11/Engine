#include "editor_ui.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>

namespace asharia::editor {

    namespace {

        [[nodiscard]] ColorSrgba8 rgb(const int red, const int green, const int blue,
                                      const int alpha = 255) {
            const auto byte = [](const int value) {
                return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
            };
            return ColorSrgba8{
                .r = byte(red),
                .g = byte(green),
                .b = byte(blue),
                .a = byte(alpha),
            };
        }

        [[nodiscard]] ImVec4 withAlpha(ColorSrgba8 color, const float alpha) {
            ImVec4 converted = toImGuiEncodedSrgbVec4(color);
            converted.w = alpha;
            return converted;
        }

        [[nodiscard]] const std::array<EditorUiTheme, 9>& themes() {
            static const std::array<EditorUiTheme, 9> kThemes{
                EditorUiTheme{
                    .id = EditorUiThemeId::Unity6Dark,
                    .storageName = "unity-6-dark",
                    .name = "Unity 6 Dark",
                    .appBackground = rgb(48, 48, 48),
                    .panelBackground = rgb(32, 32, 32),
                    .floatingBackground = rgb(44, 44, 44),
                    .panelBackgroundAlt = rgb(36, 36, 36),
                    .titleBackground = rgb(36, 36, 36),
                    .menuBackground = rgb(42, 42, 42),
                    .inputBackground = rgb(26, 26, 26),
                    .surface = rgb(42, 42, 42),
                    .surfaceHover = rgb(68, 68, 68),
                    .surfaceActive = rgb(67, 95, 123),
                    .border = rgb(48, 48, 48),
                    .borderStrong = rgb(48, 48, 48),
                    .divider = rgb(48, 48, 48),
                    .text = rgb(220, 220, 220),
                    .textSecondary = rgb(188, 188, 188),
                    .textMuted = rgb(142, 142, 142),
                    .textDisabled = rgb(104, 104, 104),
                    .accent = rgb(64, 128, 194),
                    .accentHover = rgb(78, 146, 216),
                    .accentActive = rgb(104, 170, 234),
                    .selection = rgb(62, 95, 128),
                    .viewportBackground = rgb(48, 48, 48),
                    .info = rgb(91, 158, 223),
                    .success = rgb(105, 173, 93),
                    .warning = rgb(210, 164, 72),
                    .danger = rgb(210, 92, 92),
                    .windowRounding = 4.0F,
                    .childRounding = 3.0F,
                    .frameRounding = 2.0F,
                    .popupRounding = 4.0F,
                    .tabRounding = 3.0F,
                },
                EditorUiTheme{
                    .id = EditorUiThemeId::BlackDefault,
                    .storageName = "black-default",
                    .name = "Black Default",
                    .appBackground = rgb(17, 18, 20),
                    .panelBackground = rgb(24, 25, 29),
                    .floatingBackground = rgb(30, 31, 36),
                    .panelBackgroundAlt = rgb(28, 29, 33),
                    .titleBackground = rgb(20, 21, 24),
                    .menuBackground = rgb(18, 19, 22),
                    .inputBackground = rgb(22, 23, 26),
                    .surface = rgb(29, 30, 34),
                    .surfaceHover = rgb(39, 41, 47),
                    .surfaceActive = rgb(44, 64, 80),
                    .border = rgb(49, 52, 60),
                    .borderStrong = rgb(66, 70, 80),
                    .divider = rgb(39, 41, 48),
                    .text = rgb(232, 236, 241),
                    .textSecondary = rgb(177, 185, 193),
                    .textMuted = rgb(125, 135, 146),
                    .textDisabled = rgb(82, 90, 101),
                    .accent = rgb(92, 176, 221),
                    .accentHover = rgb(121, 199, 235),
                    .accentActive = rgb(151, 218, 248),
                    .selection = rgb(38, 65, 84),
                    .viewportBackground = rgb(36, 36, 39),
                    .info = rgb(121, 199, 235),
                    .success = rgb(113, 203, 137),
                    .warning = rgb(226, 183, 89),
                    .danger = rgb(232, 102, 113),
                    .windowRounding = 2.0F,
                    .childRounding = 2.0F,
                    .frameRounding = 3.0F,
                    .popupRounding = 4.0F,
                    .tabRounding = 3.0F,
                },
                EditorUiTheme{
                    .id = EditorUiThemeId::ClassicBlueGray,
                    .storageName = "classic-blue-gray-2",
                    .name = "Classic Blue Gray 2.0",
                    .appBackground = rgb(23, 29, 36),
                    .panelBackground = rgb(32, 40, 51),
                    .floatingBackground = rgb(38, 49, 64),
                    .panelBackgroundAlt = rgb(36, 48, 59),
                    .titleBackground = rgb(28, 56, 84),
                    .menuBackground = rgb(21, 27, 34),
                    .inputBackground = rgb(24, 33, 43),
                    .surface = rgb(32, 42, 53),
                    .surfaceHover = rgb(43, 55, 69),
                    .surfaceActive = rgb(49, 95, 134),
                    .border = rgb(58, 71, 86),
                    .borderStrong = rgb(58, 71, 86),
                    .divider = rgb(45, 55, 67),
                    .text = rgb(232, 238, 245),
                    .textSecondary = rgb(181, 192, 204),
                    .textMuted = rgb(134, 147, 160),
                    .textDisabled = rgb(94, 107, 120),
                    .accent = rgb(114, 183, 232),
                    .accentHover = rgb(114, 183, 232),
                    .accentActive = rgb(114, 183, 232),
                    .selection = rgb(49, 95, 134),
                    .viewportBackground = rgb(62, 105, 156),
                    .info = rgb(142, 205, 241),
                    .success = rgb(114, 216, 138),
                    .warning = rgb(233, 201, 106),
                    .danger = rgb(242, 124, 133),
                    .windowRounding = 2.0F,
                    .childRounding = 2.0F,
                    .frameRounding = 3.0F,
                    .popupRounding = 4.0F,
                    .tabRounding = 3.0F,
                },
                EditorUiTheme{
                    .id = EditorUiThemeId::WarmGraphiteAmber,
                    .storageName = "warm-graphite-amber",
                    .name = "Warm Graphite Amber",
                    .appBackground = rgb(32, 31, 28),
                    .panelBackground = rgb(42, 41, 37),
                    .floatingBackground = rgb(52, 50, 45),
                    .panelBackgroundAlt = rgb(52, 50, 45),
                    .titleBackground = rgb(27, 26, 24),
                    .menuBackground = rgb(27, 26, 24),
                    .inputBackground = rgb(27, 26, 24),
                    .surface = rgb(27, 26, 24),
                    .surfaceHover = rgb(52, 50, 45),
                    .surfaceActive = rgb(111, 75, 31),
                    .border = rgb(74, 70, 62),
                    .borderStrong = rgb(74, 70, 62),
                    .divider = rgb(74, 70, 62),
                    .text = rgb(239, 233, 221),
                    .textSecondary = rgb(185, 176, 162),
                    .textMuted = rgb(129, 121, 109),
                    .textDisabled = rgb(129, 121, 109),
                    .accent = rgb(216, 154, 58),
                    .accentHover = rgb(216, 154, 58),
                    .accentActive = rgb(216, 154, 58),
                    .selection = rgb(111, 75, 31),
                    .viewportBackground = rgb(74, 59, 43),
                    .info = rgb(216, 154, 58),
                    .success = rgb(127, 176, 105),
                    .warning = rgb(224, 177, 90),
                    .danger = rgb(214, 106, 94),
                    .windowRounding = 2.0F,
                    .childRounding = 2.0F,
                    .frameRounding = 3.0F,
                    .popupRounding = 4.0F,
                    .tabRounding = 3.0F,
                },
                EditorUiTheme{
                    .id = EditorUiThemeId::ForestGreen,
                    .storageName = "forest-green-slate",
                    .name = "Forest Green Slate",
                    .appBackground = rgb(24, 32, 28),
                    .panelBackground = rgb(34, 43, 38),
                    .floatingBackground = rgb(43, 54, 48),
                    .panelBackgroundAlt = rgb(43, 54, 48),
                    .titleBackground = rgb(21, 27, 24),
                    .menuBackground = rgb(21, 27, 24),
                    .inputBackground = rgb(21, 27, 24),
                    .surface = rgb(21, 27, 24),
                    .surfaceHover = rgb(43, 54, 48),
                    .surfaceActive = rgb(53, 102, 74),
                    .border = rgb(62, 75, 67),
                    .borderStrong = rgb(62, 75, 67),
                    .divider = rgb(62, 75, 67),
                    .text = rgb(229, 236, 231),
                    .textSecondary = rgb(170, 184, 175),
                    .textMuted = rgb(116, 128, 120),
                    .textDisabled = rgb(116, 128, 120),
                    .accent = rgb(111, 191, 138),
                    .accentHover = rgb(111, 191, 138),
                    .accentActive = rgb(111, 191, 138),
                    .selection = rgb(53, 102, 74),
                    .viewportBackground = rgb(48, 68, 55),
                    .info = rgb(111, 191, 138),
                    .success = rgb(111, 207, 151),
                    .warning = rgb(215, 182, 92),
                    .danger = rgb(212, 106, 106),
                    .windowRounding = 2.0F,
                    .childRounding = 2.0F,
                    .frameRounding = 3.0F,
                    .popupRounding = 4.0F,
                    .tabRounding = 3.0F,
                },
                EditorUiTheme{
                    .id = EditorUiThemeId::PurpleElectric,
                    .storageName = "purple-electric",
                    .name = "Purple Electric",
                    .appBackground = rgb(30, 27, 36),
                    .panelBackground = rgb(41, 37, 50),
                    .floatingBackground = rgb(51, 46, 62),
                    .panelBackgroundAlt = rgb(51, 46, 62),
                    .titleBackground = rgb(26, 23, 32),
                    .menuBackground = rgb(26, 23, 32),
                    .inputBackground = rgb(26, 23, 32),
                    .surface = rgb(26, 23, 32),
                    .surfaceHover = rgb(51, 46, 62),
                    .surfaceActive = rgb(90, 62, 138),
                    .border = rgb(74, 67, 88),
                    .borderStrong = rgb(74, 67, 88),
                    .divider = rgb(74, 67, 88),
                    .text = rgb(238, 234, 246),
                    .textSecondary = rgb(185, 176, 200),
                    .textMuted = rgb(129, 119, 143),
                    .textDisabled = rgb(129, 119, 143),
                    .accent = rgb(167, 139, 250),
                    .accentHover = rgb(167, 139, 250),
                    .accentActive = rgb(167, 139, 250),
                    .selection = rgb(90, 62, 138),
                    .viewportBackground = rgb(58, 48, 77),
                    .info = rgb(167, 139, 250),
                    .success = rgb(119, 201, 141),
                    .warning = rgb(227, 182, 90),
                    .danger = rgb(224, 108, 117),
                    .windowRounding = 2.0F,
                    .childRounding = 2.0F,
                    .frameRounding = 3.0F,
                    .popupRounding = 4.0F,
                    .tabRounding = 3.0F,
                },
                EditorUiTheme{
                    .id = EditorUiThemeId::CarbonCopper,
                    .storageName = "carbon-copper",
                    .name = "Carbon Copper",
                    .appBackground = rgb(23, 23, 23),
                    .panelBackground = rgb(36, 35, 34),
                    .floatingBackground = rgb(46, 44, 42),
                    .panelBackgroundAlt = rgb(46, 44, 42),
                    .titleBackground = rgb(20, 20, 20),
                    .menuBackground = rgb(20, 20, 20),
                    .inputBackground = rgb(20, 20, 20),
                    .surface = rgb(20, 20, 20),
                    .surfaceHover = rgb(46, 44, 42),
                    .surfaceActive = rgb(112, 69, 31),
                    .border = rgb(68, 64, 58),
                    .borderStrong = rgb(68, 64, 58),
                    .divider = rgb(68, 64, 58),
                    .text = rgb(240, 236, 230),
                    .textSecondary = rgb(183, 175, 163),
                    .textMuted = rgb(123, 116, 107),
                    .textDisabled = rgb(123, 116, 107),
                    .accent = rgb(201, 130, 59),
                    .accentHover = rgb(201, 130, 59),
                    .accentActive = rgb(201, 130, 59),
                    .selection = rgb(112, 69, 31),
                    .viewportBackground = rgb(61, 52, 44),
                    .info = rgb(201, 130, 59),
                    .success = rgb(117, 166, 106),
                    .warning = rgb(213, 166, 76),
                    .danger = rgb(201, 95, 95),
                    .windowRounding = 2.0F,
                    .childRounding = 2.0F,
                    .frameRounding = 3.0F,
                    .popupRounding = 4.0F,
                    .tabRounding = 3.0F,
                },
                EditorUiTheme{
                    .id = EditorUiThemeId::CoolGrayTeal,
                    .storageName = "cool-gray-teal",
                    .name = "Cool Gray Teal",
                    .appBackground = rgb(27, 32, 32),
                    .panelBackground = rgb(37, 44, 44),
                    .floatingBackground = rgb(48, 56, 56),
                    .panelBackgroundAlt = rgb(48, 56, 56),
                    .titleBackground = rgb(23, 28, 28),
                    .menuBackground = rgb(23, 28, 28),
                    .inputBackground = rgb(23, 28, 28),
                    .surface = rgb(23, 28, 28),
                    .surfaceHover = rgb(48, 56, 56),
                    .surfaceActive = rgb(46, 111, 105),
                    .border = rgb(67, 80, 80),
                    .borderStrong = rgb(67, 80, 80),
                    .divider = rgb(67, 80, 80),
                    .text = rgb(231, 238, 238),
                    .textSecondary = rgb(174, 186, 186),
                    .textMuted = rgb(117, 128, 128),
                    .textDisabled = rgb(117, 128, 128),
                    .accent = rgb(77, 182, 172),
                    .accentHover = rgb(77, 182, 172),
                    .accentActive = rgb(77, 182, 172),
                    .selection = rgb(46, 111, 105),
                    .viewportBackground = rgb(46, 74, 72),
                    .info = rgb(77, 182, 172),
                    .success = rgb(118, 200, 147),
                    .warning = rgb(221, 185, 103),
                    .danger = rgb(217, 112, 112),
                    .windowRounding = 2.0F,
                    .childRounding = 2.0F,
                    .frameRounding = 3.0F,
                    .popupRounding = 4.0F,
                    .tabRounding = 3.0F,
                },
                EditorUiTheme{
                    .id = EditorUiThemeId::LightGraphiteOrange,
                    .storageName = "light-graphite-orange",
                    .name = "Light Graphite Orange",
                    .appBackground = rgb(231, 227, 220),
                    .panelBackground = rgb(243, 240, 234),
                    .floatingBackground = rgb(255, 255, 255),
                    .panelBackgroundAlt = rgb(255, 255, 255),
                    .titleBackground = rgb(236, 232, 225),
                    .menuBackground = rgb(236, 232, 225),
                    .inputBackground = rgb(236, 232, 225),
                    .surface = rgb(236, 232, 225),
                    .surfaceHover = rgb(255, 255, 255),
                    .surfaceActive = rgb(240, 216, 188),
                    .border = rgb(201, 193, 183),
                    .borderStrong = rgb(201, 193, 183),
                    .divider = rgb(201, 193, 183),
                    .text = rgb(36, 33, 29),
                    .textSecondary = rgb(95, 87, 78),
                    .textMuted = rgb(139, 129, 118),
                    .textDisabled = rgb(139, 129, 118),
                    .accent = rgb(200, 121, 42),
                    .accentHover = rgb(200, 121, 42),
                    .accentActive = rgb(200, 121, 42),
                    .selection = rgb(240, 216, 188),
                    .viewportBackground = rgb(200, 183, 159),
                    .info = rgb(200, 121, 42),
                    .success = rgb(77, 154, 100),
                    .warning = rgb(185, 130, 46),
                    .danger = rgb(185, 91, 85),
                    .windowRounding = 2.0F,
                    .childRounding = 2.0F,
                    .frameRounding = 3.0F,
                    .popupRounding = 4.0F,
                    .tabRounding = 3.0F,
                },
            };
            return kThemes;
        }

        [[nodiscard]] EditorUiThemeId& currentThemeIdStorage() {
            static EditorUiThemeId currentThemeId = defaultEditorUiThemeId();
            return currentThemeId;
        }

        [[nodiscard]] const EditorUiTheme& fallbackTheme() {
            return themes().front();
        }

    } // namespace

    EditorUiThemeId defaultEditorUiThemeId() {
        return EditorUiThemeId::Unity6Dark;
    }

    EditorUiThemeId currentEditorUiThemeId() {
        return currentThemeIdStorage();
    }

    std::string_view editorUiThemeName(EditorUiThemeId themeId) {
        return editorUiTheme(themeId).storageName;
    }

    std::optional<EditorUiThemeId> editorUiThemeIdFromName(std::string_view name) {
        if (name == "unity-6-dark" || name == "Unity 6 Dark" || name == "unity-dark") {
            return EditorUiThemeId::Unity6Dark;
        }
        if (name == "dark-black" || name == "Dark Black") {
            return EditorUiThemeId::BlackDefault;
        }
        if (name == "classic-blue-gray" || name == "Classic Blue Gray" || name == "night-slate" ||
            name == "Night Slate") {
            return EditorUiThemeId::ClassicBlueGray;
        }
        for (const EditorUiTheme& theme : themes()) {
            if (theme.storageName == name || theme.name == name) {
                return theme.id;
            }
        }
        return std::nullopt;
    }

    std::span<const EditorUiTheme> editorUiThemes() {
        return themes();
    }

    const EditorUiTheme& editorUiTheme(EditorUiThemeId themeId) {
        for (const EditorUiTheme& theme : themes()) {
            if (theme.id == themeId) {
                return theme;
            }
        }
        return fallbackTheme();
    }

    const EditorUiTheme& editorUiTheme() {
        return editorUiTheme(currentEditorUiThemeId());
    }

    const EditorUiMetrics& editorUiMetrics() {
        static constexpr EditorUiMetrics kMetrics{};
        return kMetrics;
    }

    std::span<const EditorUiColorToken> editorUiColorTokens() {
        const EditorUiTheme& theme = editorUiTheme();
        static std::array<EditorUiColorToken, 22> tokens{};
        tokens = std::array<EditorUiColorToken, 22>{
            EditorUiColorToken{.name = "App BG",
                               .color = theme.appBackground,
                               .usage = "root window and dockspace"},
            EditorUiColorToken{
                .name = "Panel BG", .color = theme.panelBackground, .usage = "tool panels"},
            EditorUiColorToken{.name = "Floating BG",
                               .color = theme.floatingBackground,
                               .usage = "popups and floating surfaces"},
            EditorUiColorToken{.name = "Panel Alt",
                               .color = theme.panelBackgroundAlt,
                               .usage = "headers and alternating rows"},
            EditorUiColorToken{
                .name = "Title BG", .color = theme.titleBackground, .usage = "window titles"},
            EditorUiColorToken{
                .name = "Menu BG", .color = theme.menuBackground, .usage = "main menu"},
            EditorUiColorToken{
                .name = "Input BG", .color = theme.inputBackground, .usage = "input fields"},
            EditorUiColorToken{
                .name = "Surface", .color = theme.surface, .usage = "low-emphasis buttons"},
            EditorUiColorToken{
                .name = "Surface Hover", .color = theme.surfaceHover, .usage = "hovered controls"},
            EditorUiColorToken{
                .name = "Surface Active", .color = theme.surfaceActive, .usage = "active controls"},
            EditorUiColorToken{.name = "Border", .color = theme.border, .usage = "thin dividers"},
            EditorUiColorToken{
                .name = "Divider", .color = theme.divider, .usage = "weak separators"},
            EditorUiColorToken{.name = "Text", .color = theme.text, .usage = "primary text"},
            EditorUiColorToken{.name = "Secondary Text",
                               .color = theme.textSecondary,
                               .usage = "secondary labels"},
            EditorUiColorToken{
                .name = "Weak Text", .color = theme.textMuted, .usage = "low-priority metadata"},
            EditorUiColorToken{
                .name = "Disabled Text", .color = theme.textDisabled, .usage = "disabled controls"},
            EditorUiColorToken{.name = "Accent", .color = theme.accent, .usage = "focus/select"},
            EditorUiColorToken{
                .name = "Selection", .color = theme.selection, .usage = "selected rows"},
            EditorUiColorToken{.name = "Viewport",
                               .color = theme.viewportBackground,
                               .usage = "viewport placeholder"},
            EditorUiColorToken{.name = "Success", .color = theme.success, .usage = "ready state"},
            EditorUiColorToken{
                .name = "Warning", .color = theme.warning, .usage = "recoverable issues"},
            EditorUiColorToken{
                .name = "Danger", .color = theme.danger, .usage = "blocking or destructive state"},
        };
        return tokens;
    }

    ImVec4 toImGuiEncodedSrgbVec4(ColorSrgba8 color) {
        constexpr float kByteToFloat = 1.0F / 255.0F;
        return ImVec4{
            static_cast<float>(color.r) * kByteToFloat, static_cast<float>(color.g) * kByteToFloat,
            static_cast<float>(color.b) * kByteToFloat, static_cast<float>(color.a) * kByteToFloat};
    }

    ImU32 toImGuiEncodedSrgbU32(ColorSrgba8 color) {
        return IM_COL32(color.r, color.g, color.b, color.a);
    }

    void applyEditorUiTheme(EditorUiThemeId themeId) {
        const EditorUiTheme& theme = editorUiTheme(themeId);
        currentThemeIdStorage() = theme.id;
        const bool isClassicBlueGray = theme.id == EditorUiThemeId::ClassicBlueGray;
        const ColorSrgba8 inputHoverBackground =
            isClassicBlueGray ? theme.surface : theme.surfaceHover;
        const ColorSrgba8 inputFocusBackground =
            isClassicBlueGray ? rgb(29, 43, 55) : theme.surfaceActive;
        const ColorSrgba8 caret = isClassicBlueGray ? rgb(158, 220, 255) : theme.accent;
        ImGuiStyle& style = ImGui::GetStyle();
        const EditorUiMetrics& metrics = editorUiMetrics();
        const bool isUnity6Dark = theme.id == EditorUiThemeId::Unity6Dark;
        style.WindowPadding =
            isUnity6Dark ? ImVec2{8.0F, 8.0F} : ImVec2{metrics.panelPadding, metrics.panelPadding};
        style.FramePadding = ImVec2{6.0F, 2.0F};
        style.CellPadding = ImVec2{5.0F, 2.0F};
        style.ItemSpacing = ImVec2{6.0F, 4.0F};
        style.ItemInnerSpacing = ImVec2{4.0F, 3.0F};
        style.IndentSpacing = 15.0F;
        style.ScrollbarSize = 12.0F;
        style.GrabMinSize = 8.0F;
        style.WindowBorderSize = isUnity6Dark ? 0.0F : 1.0F;
        style.ChildBorderSize = isUnity6Dark ? 0.0F : 1.0F;
        style.PopupBorderSize = 1.0F;
        style.FrameBorderSize = 1.0F;
        style.TabBorderSize = 0.0F;
        style.WindowRounding = theme.windowRounding;
        style.ChildRounding = theme.childRounding;
        style.FrameRounding = theme.frameRounding;
        style.PopupRounding = theme.popupRounding;
        style.ScrollbarRounding = 3.0F;
        style.GrabRounding = 3.0F;
        style.TabRounding = theme.tabRounding;
        style.WindowTitleAlign = ImVec2{0.0F, 0.5F};

        std::span<ImVec4> colors{style.Colors, static_cast<std::size_t>(ImGuiCol_COUNT)};
        const auto setColor = [&colors](const ImGuiCol color, const ImVec4 value) {
            colors[static_cast<std::size_t>(color)] = value;
        };
        const auto setThemeColor = [&setColor](const ImGuiCol color, const ColorSrgba8 value) {
            setColor(color, toImGuiEncodedSrgbVec4(value));
        };
        setThemeColor(ImGuiCol_Text, theme.text);
        setThemeColor(ImGuiCol_TextDisabled, theme.textDisabled);
        setThemeColor(ImGuiCol_WindowBg,
                      isUnity6Dark ? theme.appBackground : theme.panelBackground);
        setThemeColor(ImGuiCol_ChildBg, isUnity6Dark ? theme.panelBackground : theme.appBackground);
        setThemeColor(ImGuiCol_PopupBg, theme.floatingBackground);
        setThemeColor(ImGuiCol_Border, theme.border);
        setColor(ImGuiCol_BorderShadow, ImVec4{0.0F, 0.0F, 0.0F, 0.0F});
        setThemeColor(ImGuiCol_FrameBg, theme.inputBackground);
        setThemeColor(ImGuiCol_FrameBgHovered, inputHoverBackground);
        setThemeColor(ImGuiCol_FrameBgActive, inputFocusBackground);
        setThemeColor(ImGuiCol_TitleBg, theme.titleBackground);
        setThemeColor(ImGuiCol_TitleBgActive, theme.titleBackground);
        setThemeColor(ImGuiCol_TitleBgCollapsed, theme.menuBackground);
        setThemeColor(ImGuiCol_MenuBarBg, theme.menuBackground);
        setThemeColor(ImGuiCol_ScrollbarBg, theme.inputBackground);
        setThemeColor(ImGuiCol_ScrollbarGrab, theme.border);
        setThemeColor(ImGuiCol_ScrollbarGrabHovered, theme.borderStrong);
        setThemeColor(ImGuiCol_ScrollbarGrabActive, theme.accentActive);
        setThemeColor(ImGuiCol_CheckMark, theme.accentActive);
        setThemeColor(ImGuiCol_SliderGrab, theme.accent);
        setThemeColor(ImGuiCol_SliderGrabActive, theme.accentActive);
        setThemeColor(ImGuiCol_Button, theme.surface);
        setThemeColor(ImGuiCol_ButtonHovered, theme.surfaceHover);
        setThemeColor(ImGuiCol_ButtonActive, theme.surfaceActive);
        setThemeColor(ImGuiCol_Header, theme.selection);
        setThemeColor(ImGuiCol_HeaderHovered, theme.surfaceHover);
        setThemeColor(ImGuiCol_HeaderActive, theme.surfaceActive);
        setThemeColor(ImGuiCol_Separator, theme.divider);
        setThemeColor(ImGuiCol_SeparatorHovered, theme.borderStrong);
        setThemeColor(ImGuiCol_SeparatorActive, theme.accent);
        setColor(ImGuiCol_ResizeGrip, withAlpha(theme.accent, 0.28F));
        setColor(ImGuiCol_ResizeGripHovered, withAlpha(theme.accentHover, 0.48F));
        setColor(ImGuiCol_ResizeGripActive, withAlpha(theme.accentActive, 0.68F));
        setThemeColor(ImGuiCol_Tab,
                      isUnity6Dark ? theme.panelBackground : theme.panelBackgroundAlt);
        setThemeColor(ImGuiCol_TabHovered, theme.surfaceHover);
        setThemeColor(ImGuiCol_TabSelected,
                      isUnity6Dark ? theme.panelBackground : theme.titleBackground);
        if (isUnity6Dark) {
            setColor(ImGuiCol_TabSelectedOverline, ImVec4{0.0F, 0.0F, 0.0F, 0.0F});
        } else {
            setThemeColor(ImGuiCol_TabSelectedOverline, theme.accent);
        }
        setThemeColor(ImGuiCol_TabDimmed, theme.panelBackground);
        setThemeColor(ImGuiCol_TabDimmedSelected, theme.surface);
        setColor(ImGuiCol_TabDimmedSelectedOverline, withAlpha(theme.accent, 0.56F));
        setColor(ImGuiCol_DockingPreview, withAlpha(theme.accentActive, 0.55F));
        setThemeColor(ImGuiCol_DockingEmptyBg, theme.appBackground);
        setThemeColor(ImGuiCol_PlotLines, theme.accentHover);
        setThemeColor(ImGuiCol_PlotLinesHovered, theme.accentActive);
        setThemeColor(ImGuiCol_PlotHistogram, theme.warning);
        setThemeColor(ImGuiCol_PlotHistogramHovered, theme.accentHover);
        setThemeColor(ImGuiCol_TableHeaderBg, theme.panelBackgroundAlt);
        setThemeColor(ImGuiCol_TableBorderStrong, theme.borderStrong);
        setThemeColor(ImGuiCol_TableBorderLight, theme.divider);
        setThemeColor(ImGuiCol_TableRowBg, theme.panelBackground);
        setThemeColor(ImGuiCol_TableRowBgAlt, theme.panelBackgroundAlt);
        setColor(ImGuiCol_TextSelectedBg, withAlpha(theme.selection, 0.72F));
        setThemeColor(ImGuiCol_InputTextCursor, caret);
        setThemeColor(ImGuiCol_DragDropTarget, theme.accentActive);
        setThemeColor(ImGuiCol_NavCursor, theme.accentActive);
        setColor(ImGuiCol_NavWindowingHighlight, withAlpha(theme.accentActive, 0.72F));
        setColor(ImGuiCol_NavWindowingDimBg, ImVec4{0.0F, 0.0F, 0.0F, 0.42F});
        setColor(ImGuiCol_ModalWindowDimBg, ImVec4{0.0F, 0.0F, 0.0F, 0.48F});
    }

    void applyEditorUiTheme() {
        applyEditorUiTheme(currentEditorUiThemeId());
    }

} // namespace asharia::editor
