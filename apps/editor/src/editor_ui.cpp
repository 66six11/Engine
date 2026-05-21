#include "editor_ui.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <string>

namespace asharia::editor {

    namespace {

        [[nodiscard]] ImVec4 rgb(const int red, const int green, const int blue,
                                 const float alpha = 1.0F) {
            return ImVec4{static_cast<float>(red) / 255.0F, static_cast<float>(green) / 255.0F,
                          static_cast<float>(blue) / 255.0F, alpha};
        }

        [[nodiscard]] ImVec4 withAlpha(ImVec4 color, const float alpha) {
            color.w = alpha;
            return color;
        }

        struct EditorUiToneColors {
            ImU32 background{};
            ImU32 border{};
            ImU32 text{};
        };

        [[nodiscard]] EditorUiToneColors toneColors(EditorUiTone tone) {
            const EditorUiTheme& theme = editorUiTheme();
            switch (tone) {
            case EditorUiTone::Info:
                return EditorUiToneColors{
                    .background = ImGui::GetColorU32(withAlpha(theme.info, 0.26F)),
                    .border = ImGui::GetColorU32(withAlpha(theme.info, 0.92F)),
                    .text = ImGui::GetColorU32(theme.text),
                };
            case EditorUiTone::Success:
                return EditorUiToneColors{
                    .background = ImGui::GetColorU32(withAlpha(theme.success, 0.22F)),
                    .border = ImGui::GetColorU32(withAlpha(theme.success, 0.88F)),
                    .text = ImGui::GetColorU32(theme.text),
                };
            case EditorUiTone::Warning:
                return EditorUiToneColors{
                    .background = ImGui::GetColorU32(withAlpha(theme.warning, 0.24F)),
                    .border = ImGui::GetColorU32(withAlpha(theme.warning, 0.92F)),
                    .text = ImGui::GetColorU32(theme.text),
                };
            case EditorUiTone::Danger:
                return EditorUiToneColors{
                    .background = ImGui::GetColorU32(withAlpha(theme.danger, 0.24F)),
                    .border = ImGui::GetColorU32(withAlpha(theme.danger, 0.92F)),
                    .text = ImGui::GetColorU32(theme.text),
                };
            case EditorUiTone::Muted:
                return EditorUiToneColors{
                    .background = ImGui::GetColorU32(ImGuiCol_FrameBg),
                    .border = ImGui::GetColorU32(ImGuiCol_Border),
                    .text = ImGui::GetColorU32(ImGuiCol_TextDisabled),
                };
            case EditorUiTone::Neutral:
            default:
                return EditorUiToneColors{
                    .background = ImGui::GetColorU32(ImGuiCol_Button),
                    .border = ImGui::GetColorU32(ImGuiCol_Border),
                    .text = ImGui::GetColorU32(ImGuiCol_Text),
                };
            }
        }

        void textUnformatted(std::string_view text) {
            ImGui::TextUnformatted(text.data(), text.data() + text.size());
        }

        void disabledText(std::string_view text) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            textUnformatted(text);
            ImGui::PopStyleColor();
        }

    } // namespace

    const EditorUiTheme& editorUiTheme() {
        static const EditorUiTheme kTheme{
            .name = "Unreal-Like Deep Slate",
            .appBackground = rgb(21, 24, 28),
            .panelBackground = rgb(29, 33, 39),
            .panelBackgroundAlt = rgb(32, 36, 42),
            .surface = rgb(36, 42, 49),
            .surfaceHover = rgb(45, 53, 64),
            .surfaceActive = rgb(51, 64, 77),
            .border = rgb(52, 59, 69),
            .borderStrong = rgb(74, 84, 97),
            .text = rgb(215, 222, 232),
            .textMuted = rgb(141, 153, 168),
            .accent = rgb(45, 125, 210),
            .accentHover = rgb(61, 141, 227),
            .accentActive = rgb(86, 160, 242),
            .info = rgb(80, 145, 220),
            .success = rgb(79, 163, 117),
            .warning = rgb(216, 166, 87),
            .danger = rgb(211, 95, 95),
            .windowRounding = 2.0F,
            .childRounding = 2.0F,
            .frameRounding = 3.0F,
            .popupRounding = 4.0F,
            .tabRounding = 3.0F,
        };
        return kTheme;
    }

    std::span<const EditorUiColorToken> editorUiColorTokens() {
        const EditorUiTheme& theme = editorUiTheme();
        static const std::array<EditorUiColorToken, 12> kTokens{
            EditorUiColorToken{.name = "App BG",
                               .color = theme.appBackground,
                               .usage = "root window and dockspace"},
            EditorUiColorToken{
                .name = "Panel BG", .color = theme.panelBackground, .usage = "tool panels"},
            EditorUiColorToken{.name = "Panel Alt",
                               .color = theme.panelBackgroundAlt,
                               .usage = "headers and alternating rows"},
            EditorUiColorToken{.name = "Surface",
                               .color = theme.surface,
                               .usage = "inputs and low-emphasis buttons"},
            EditorUiColorToken{
                .name = "Surface Hover", .color = theme.surfaceHover, .usage = "hovered controls"},
            EditorUiColorToken{
                .name = "Surface Active", .color = theme.surfaceActive, .usage = "active controls"},
            EditorUiColorToken{.name = "Border", .color = theme.border, .usage = "thin dividers"},
            EditorUiColorToken{.name = "Text", .color = theme.text, .usage = "primary text"},
            EditorUiColorToken{
                .name = "Muted Text", .color = theme.textMuted, .usage = "secondary metadata"},
            EditorUiColorToken{.name = "Accent", .color = theme.accent, .usage = "focus/select"},
            EditorUiColorToken{
                .name = "Warning", .color = theme.warning, .usage = "recoverable issues"},
            EditorUiColorToken{
                .name = "Danger", .color = theme.danger, .usage = "blocking or destructive state"},
        };
        return kTokens;
    }

    void applyEditorUiTheme() {
        const EditorUiTheme& theme = editorUiTheme();
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowPadding = ImVec2{8.0F, 8.0F};
        style.FramePadding = ImVec2{7.0F, 3.0F};
        style.CellPadding = ImVec2{5.0F, 3.0F};
        style.ItemSpacing = ImVec2{7.0F, 5.0F};
        style.ItemInnerSpacing = ImVec2{5.0F, 4.0F};
        style.IndentSpacing = 15.0F;
        style.ScrollbarSize = 13.0F;
        style.GrabMinSize = 8.0F;
        style.WindowBorderSize = 1.0F;
        style.ChildBorderSize = 1.0F;
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
        setColor(ImGuiCol_Text, theme.text);
        setColor(ImGuiCol_TextDisabled, theme.textMuted);
        setColor(ImGuiCol_WindowBg, theme.panelBackground);
        setColor(ImGuiCol_ChildBg, theme.appBackground);
        setColor(ImGuiCol_PopupBg, rgb(24, 28, 34));
        setColor(ImGuiCol_Border, theme.border);
        setColor(ImGuiCol_BorderShadow, ImVec4{0.0F, 0.0F, 0.0F, 0.0F});
        setColor(ImGuiCol_FrameBg, theme.surface);
        setColor(ImGuiCol_FrameBgHovered, theme.surfaceHover);
        setColor(ImGuiCol_FrameBgActive, theme.surfaceActive);
        setColor(ImGuiCol_TitleBg, rgb(19, 22, 27));
        setColor(ImGuiCol_TitleBgActive, rgb(25, 30, 36));
        setColor(ImGuiCol_TitleBgCollapsed, rgb(19, 22, 27));
        setColor(ImGuiCol_MenuBarBg, rgb(24, 28, 34));
        setColor(ImGuiCol_ScrollbarBg, rgb(18, 21, 25));
        setColor(ImGuiCol_ScrollbarGrab, rgb(57, 65, 76));
        setColor(ImGuiCol_ScrollbarGrabHovered, rgb(71, 82, 96));
        setColor(ImGuiCol_ScrollbarGrabActive, rgb(90, 103, 120));
        setColor(ImGuiCol_CheckMark, theme.accentActive);
        setColor(ImGuiCol_SliderGrab, theme.accent);
        setColor(ImGuiCol_SliderGrabActive, theme.accentActive);
        setColor(ImGuiCol_Button, rgb(39, 46, 55));
        setColor(ImGuiCol_ButtonHovered, rgb(49, 61, 73));
        setColor(ImGuiCol_ButtonActive, rgb(58, 76, 92));
        setColor(ImGuiCol_Header, withAlpha(theme.accent, 0.32F));
        setColor(ImGuiCol_HeaderHovered, withAlpha(theme.accentHover, 0.42F));
        setColor(ImGuiCol_HeaderActive, withAlpha(theme.accentActive, 0.52F));
        setColor(ImGuiCol_Separator, theme.border);
        setColor(ImGuiCol_SeparatorHovered, theme.borderStrong);
        setColor(ImGuiCol_SeparatorActive, theme.accent);
        setColor(ImGuiCol_ResizeGrip, withAlpha(theme.accent, 0.28F));
        setColor(ImGuiCol_ResizeGripHovered, withAlpha(theme.accentHover, 0.48F));
        setColor(ImGuiCol_ResizeGripActive, withAlpha(theme.accentActive, 0.68F));
        setColor(ImGuiCol_Tab, rgb(28, 33, 39));
        setColor(ImGuiCol_TabHovered, withAlpha(theme.accentHover, 0.42F));
        setColor(ImGuiCol_TabSelected, rgb(39, 49, 60));
        setColor(ImGuiCol_TabSelectedOverline, theme.accent);
        setColor(ImGuiCol_TabDimmed, rgb(23, 27, 32));
        setColor(ImGuiCol_TabDimmedSelected, rgb(31, 38, 46));
        setColor(ImGuiCol_TabDimmedSelectedOverline, withAlpha(theme.accent, 0.56F));
        setColor(ImGuiCol_DockingPreview, withAlpha(theme.accentActive, 0.55F));
        setColor(ImGuiCol_DockingEmptyBg, theme.appBackground);
        setColor(ImGuiCol_PlotLines, theme.accentHover);
        setColor(ImGuiCol_PlotLinesHovered, theme.accentActive);
        setColor(ImGuiCol_PlotHistogram, theme.warning);
        setColor(ImGuiCol_PlotHistogramHovered, rgb(235, 186, 104));
        setColor(ImGuiCol_TableHeaderBg, theme.panelBackgroundAlt);
        setColor(ImGuiCol_TableBorderStrong, theme.borderStrong);
        setColor(ImGuiCol_TableBorderLight, theme.border);
        setColor(ImGuiCol_TableRowBg, ImVec4{0.0F, 0.0F, 0.0F, 0.0F});
        setColor(ImGuiCol_TableRowBgAlt, withAlpha(theme.panelBackgroundAlt, 0.62F));
        setColor(ImGuiCol_TextSelectedBg, withAlpha(theme.accent, 0.38F));
        setColor(ImGuiCol_DragDropTarget, theme.accentActive);
        setColor(ImGuiCol_NavCursor, theme.accentActive);
        setColor(ImGuiCol_NavWindowingHighlight, withAlpha(theme.accentActive, 0.72F));
        setColor(ImGuiCol_NavWindowingDimBg, ImVec4{0.0F, 0.0F, 0.0F, 0.42F});
        setColor(ImGuiCol_ModalWindowDimBg, ImVec4{0.0F, 0.0F, 0.0F, 0.48F});
    }

    void drawEditorUiSectionHeader(std::string_view label) {
        ImGui::Spacing();
        textUnformatted(label);
        ImGui::Separator();
    }

    bool beginEditorUiPropertyTable(std::string_view tableIdentifier, float labelWidth) {
        const std::string tableId{tableIdentifier};
        const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                      ImGuiTableFlags_SizingStretchProp;
        if (!ImGui::BeginTable(tableId.c_str(), 2, flags)) {
            return false;
        }
        ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed,
                                std::max(1.0F, labelWidth));
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
        return true;
    }

    void drawEditorUiProperty(const EditorUiProperty& property) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        disabledText(property.label);
        ImGui::TableNextColumn();
        textUnformatted(property.value);
    }

    void endEditorUiPropertyTable() {
        ImGui::EndTable();
    }

    void drawEditorUiStatusPill(std::string_view label, EditorUiTone tone) {
        const std::string text{label};
        const EditorUiToneColors colors = toneColors(tone);
        const ImVec2 padding{8.0F, 3.0F};
        const ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
        const ImVec2 size{textSize.x + (padding.x * 2.0F), textSize.y + (padding.y * 2.0F)};
        const ImVec2 min = ImGui::GetCursorScreenPos();
        const ImVec2 max{min.x + size.x, min.y + size.y};
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(min, max, colors.background, 3.0F);
        drawList->AddRect(min, max, colors.border, 3.0F);
        drawList->AddText(ImVec2{min.x + padding.x, min.y + padding.y}, colors.text, text.c_str());
        ImGui::Dummy(size);
    }

    void drawEditorUiColorSwatch(std::string_view label, ImVec4 color) {
        const std::string text{label};
        const ImVec2 min = ImGui::GetCursorScreenPos();
        const float side = ImGui::GetTextLineHeight();
        const ImVec2 max{min.x + side, min.y + side};
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(min, max, ImGui::GetColorU32(color), 2.0F);
        drawList->AddRect(min, max, ImGui::GetColorU32(ImGuiCol_Border), 2.0F);
        ImGui::Dummy(ImVec2{side, side});
        ImGui::SameLine();
        ImGui::TextUnformatted(text.c_str());
    }

} // namespace asharia::editor
