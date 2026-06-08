#include <algorithm>
#include <cstddef>
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

    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    void drawEditorUiPanelHeader(std::string_view title, std::string_view subtitle) {
        const EditorUiTheme& theme = editorUiTheme();
        const EditorUiMetrics& metrics = editorUiMetrics();
        const std::string titleText{title};
        const std::string subtitleText{subtitle};
        const ImVec2 min = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        const ImVec2 max{min.x + width, min.y + metrics.panelHeaderHeight};
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(min, max, toImGuiEncodedSrgbU32(theme.titleBackground),
                                theme.childRounding);
        drawList->AddRect(min, max, toImGuiEncodedSrgbU32(theme.borderStrong), theme.childRounding);
        drawList->AddText(ImVec2{min.x + metrics.rowPaddingX, min.y + 5.0F},
                          toImGuiEncodedSrgbU32(theme.text), titleText.c_str());
        if (!subtitleText.empty()) {
            const float subtitleWidth = ImGui::CalcTextSize(subtitleText.c_str()).x;
            if (subtitleWidth + 12.0F < width) {
                drawList->AddText(ImVec2{max.x - subtitleWidth - metrics.rowPaddingX, min.y + 5.0F},
                                  toImGuiEncodedSrgbU32(theme.textMuted), subtitleText.c_str());
            }
        }
        ImGui::Dummy(ImVec2{width, metrics.panelHeaderHeight});
    }

    void drawEditorUiSectionHeader(std::string_view label) {
        const EditorUiTheme& theme = editorUiTheme();
        const EditorUiMetrics& metrics = editorUiMetrics();
        const std::string text{label};
        const ImVec2 min = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        const ImVec2 max{min.x + width, min.y + metrics.sectionHeaderHeight};
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(min, max, toImGuiEncodedSrgbU32(theme.panelBackgroundAlt),
                                theme.childRounding);
        drawList->AddRect(min, max, toImGuiEncodedSrgbU32(theme.border), theme.childRounding);
        drawList->AddText(ImVec2{min.x + metrics.rowPaddingX, min.y + 4.0F},
                          toImGuiEncodedSrgbU32(theme.text), text.c_str());
        ImGui::Dummy(ImVec2{width, metrics.sectionHeaderHeight});
    }

    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    bool drawEditorUiComponentHeader(std::string_view tableIdentifier, std::string_view label,
                                     bool defaultOpen) {
        const EditorUiTheme& theme = editorUiTheme();
        std::string stableLabel{label};
        stableLabel += "###component-header-";
        stableLabel.append(tableIdentifier.data(), tableIdentifier.size());
        ImGui::PushStyleColor(ImGuiCol_Header, toImGuiEncodedSrgbVec4(theme.panelBackgroundAlt));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, toImGuiEncodedSrgbVec4(theme.surfaceHover));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, toImGuiEncodedSrgbVec4(theme.surfaceActive));
        const ImGuiTreeNodeFlags flags =
            ImGuiTreeNodeFlags_SpanAvailWidth |
            (defaultOpen ? ImGuiTreeNodeFlags_DefaultOpen : static_cast<ImGuiTreeNodeFlags>(0));
        const bool open = ImGui::CollapsingHeader(stableLabel.c_str(), flags);
        ImGui::PopStyleColor(3);
        return open;
    }

    bool drawEditorUiCompactButton(std::string_view label, bool active) {
        const EditorUiTheme& theme = editorUiTheme();
        const std::string stableLabel{label};
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{6.0F, 2.0F});
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, theme.frameRounding);
        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Button, toImGuiEncodedSrgbVec4(theme.surfaceActive));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                  toImGuiEncodedSrgbVec4(theme.surfaceActive));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                                  toImGuiEncodedSrgbVec4(theme.accentActive));
        }
        const bool pressed = ImGui::Button(stableLabel.c_str());
        if (active) {
            ImGui::PopStyleColor(3);
        }
        ImGui::PopStyleVar(2);
        return pressed;
    }

    bool drawEditorUiToolbarToggle(std::string_view label, bool active, bool enabled,
                                   std::string_view disabledTooltip) {
        if (!enabled) {
            ImGui::BeginDisabled();
        }
        const bool pressed = drawEditorUiCompactButton(label, active);
        if (!enabled) {
            ImGui::EndDisabled();
            if (!disabledTooltip.empty() &&
                ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::BeginTooltip();
                textUnformatted(disabledTooltip);
                ImGui::EndTooltip();
            }
        }
        return enabled && pressed;
    }

    void drawEditorUiToolbarSeparator() {
        const EditorUiTheme& theme = editorUiTheme();
        const ImVec2 cursor = ImGui::GetCursorScreenPos();
        const float height = ImGui::GetFrameHeight();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddLine(ImVec2{cursor.x + 2.0F, cursor.y + 3.0F},
                          ImVec2{cursor.x + 2.0F, cursor.y + height - 3.0F},
                          toImGuiEncodedSrgbU32(theme.borderStrong));
        ImGui::Dummy(ImVec2{5.0F, height});
    }

    bool drawEditorUiSearchField(std::string_view label, char* buffer, std::size_t bufferSize,
                                 bool enabled) {
        const std::string stableLabel{label};
        if (!enabled) {
            ImGui::BeginDisabled();
        }
        ImGui::SetNextItemWidth(-1.0F);
        const bool changed = ImGui::InputText(stableLabel.c_str(), buffer, bufferSize);
        if (!enabled) {
            ImGui::EndDisabled();
        }
        return enabled && changed;
    }

    bool beginEditorUiPropertyTable(std::string_view tableIdentifier, float labelWidth) {
        const std::string tableId{tableIdentifier};
        const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
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
        ImGui::PushTextWrapPos(0.0F);
        textUnformatted(property.value);
        ImGui::PopTextWrapPos();
    }

    void endEditorUiPropertyTable() {
        ImGui::EndTable();
    }

    void drawEditorUiStatusPill(std::string_view label, EditorUiTone tone) {
        const std::string text{label};
        const EditorUiToneColors colors = toneColors(tone);
        const ImVec2 padding{6.0F, 2.0F};
        const ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
        const ImVec2 size{textSize.x + (padding.x * 2.0F), textSize.y + (padding.y * 2.0F)};
        const ImVec2 min = ImGui::GetCursorScreenPos();
        const ImVec2 max{min.x + size.x, min.y + size.y};
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(min, max, colors.background, 2.0F);
        drawList->AddRect(min, max, colors.border, 2.0F);
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
