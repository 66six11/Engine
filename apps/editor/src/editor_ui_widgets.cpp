#include <algorithm>
#include <string>
#include <string_view>

#include "editor_ui.hpp"

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

        struct EditorUiToneColors {
            ImU32 background{};
            ImU32 border{};
            ImU32 text{};
        };

        [[nodiscard]] EditorUiToneColors toneColors(EditorUiTone tone) {
            const EditorUiTheme& theme = editorUiTheme();
            const bool isClassicBlueGray = theme.id == EditorUiThemeId::ClassicBlueGray;
            switch (tone) {
            case EditorUiTone::Info:
                return EditorUiToneColors{
                    .background = ImGui::GetColorU32(isClassicBlueGray
                                                         ? toImGuiEncodedSrgbVec4(rgb(33, 58, 75))
                                                         : withAlpha(theme.info, 0.26F)),
                    .border = toImGuiEncodedSrgbU32(theme.info),
                    .text = toImGuiEncodedSrgbU32(theme.info),
                };
            case EditorUiTone::Success:
                return EditorUiToneColors{
                    .background = ImGui::GetColorU32(isClassicBlueGray
                                                         ? toImGuiEncodedSrgbVec4(rgb(31, 69, 50))
                                                         : withAlpha(theme.success, 0.22F)),
                    .border = toImGuiEncodedSrgbU32(theme.success),
                    .text = toImGuiEncodedSrgbU32(theme.success),
                };
            case EditorUiTone::Warning:
                return EditorUiToneColors{
                    .background = ImGui::GetColorU32(isClassicBlueGray
                                                         ? toImGuiEncodedSrgbVec4(rgb(74, 61, 34))
                                                         : withAlpha(theme.warning, 0.24F)),
                    .border = toImGuiEncodedSrgbU32(theme.warning),
                    .text = toImGuiEncodedSrgbU32(theme.warning),
                };
            case EditorUiTone::Danger:
                return EditorUiToneColors{
                    .background = ImGui::GetColorU32(isClassicBlueGray
                                                         ? toImGuiEncodedSrgbVec4(rgb(77, 37, 45))
                                                         : withAlpha(theme.danger, 0.24F)),
                    .border = toImGuiEncodedSrgbU32(theme.danger),
                    .text = toImGuiEncodedSrgbU32(theme.danger),
                };
            case EditorUiTone::Muted:
                return EditorUiToneColors{
                    .background = toImGuiEncodedSrgbU32(isClassicBlueGray ? rgb(43, 55, 69)
                                                                          : theme.surfaceHover),
                    .border = toImGuiEncodedSrgbU32(theme.textSecondary),
                    .text = toImGuiEncodedSrgbU32(theme.textSecondary),
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

    void drawEditorUiColorSwatch(std::string_view label, ColorSrgba8 color) {
        const std::string text{label};
        const ImVec2 min = ImGui::GetCursorScreenPos();
        const float side = ImGui::GetTextLineHeight();
        const ImVec2 max{min.x + side, min.y + side};
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(min, max, toImGuiEncodedSrgbU32(color), 2.0F);
        drawList->AddRect(min, max, ImGui::GetColorU32(ImGuiCol_Border), 2.0F);
        ImGui::Dummy(ImVec2{side, side});
        ImGui::SameLine();
        ImGui::TextUnformatted(text.c_str());
    }

} // namespace asharia::editor
