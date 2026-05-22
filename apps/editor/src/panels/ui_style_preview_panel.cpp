#include "panels/ui_style_preview_panel.hpp"

#include <algorithm>
#include <array>
#include <imgui.h>
#include <string>
#include <string_view>

#include "editor_settings.hpp"
#include "editor_ui.hpp"

namespace asharia::editor {

    namespace {

        constexpr ImGuiTableFlags kPreviewTableFlags =
            ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuter |
            ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;

        constexpr ImGuiTableFlags kTokenTableFlags =
            ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuter |
            ImGuiTableFlags_SizingStretchProp;

        struct PreviewRow {
            std::string_view component;
            std::string_view token;
        };

        [[nodiscard]] std::string hexColor(ColorSrgba8 color) {
            static constexpr std::array<char, 16> kHexDigits{
                '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
            const auto hexByte = [](const int value, const bool high) {
                return kHexDigits.at(static_cast<std::size_t>((value >> (high ? 4 : 0)) & 0xF));
            };
            return std::string{'#',
                               hexByte(color.r, true),
                               hexByte(color.r, false),
                               hexByte(color.g, true),
                               hexByte(color.g, false),
                               hexByte(color.b, true),
                               hexByte(color.b, false)};
        }

        void text(std::string_view value) {
            ImGui::TextUnformatted(value.data(), value.data() + value.size());
        }

        void mutedText(std::string_view value) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            text(value);
            ImGui::PopStyleColor();
        }

        void beginPreviewRow(const PreviewRow& row) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            text(row.component);
            ImGui::TableSetColumnIndex(2);
            mutedText(row.token);
            ImGui::TableSetColumnIndex(1);
            ImGui::PushID(row.component.data(), row.component.data() + row.component.size());
        }

        void endPreviewRow() {
            ImGui::PopID();
        }

        void drawColorChip(ColorSrgba8 color) {
            const ImVec2 min = ImGui::GetCursorScreenPos();
            const float side = ImGui::GetTextLineHeight();
            const ImVec2 max{min.x + (side * 1.6F), min.y + side};
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(min, max, toImGuiEncodedSrgbU32(color), 2.0F);
            drawList->AddRect(min, max, ImGui::GetColorU32(ImGuiCol_Border), 2.0F);
            ImGui::Dummy(ImVec2{side * 1.6F, side});
        }

        void drawTokenPalette() {
            const std::string title = std::string{editorUiTheme().name} + " Tokens";
            drawEditorUiSectionHeader(title);
            if (!ImGui::BeginTable("ui-style-token-table", 4, kTokenTableFlags)) {
                return;
            }

            ImGui::TableSetupColumn("Token", ImGuiTableColumnFlags_WidthFixed, 112.0F);
            ImGui::TableSetupColumn("Color", ImGuiTableColumnFlags_WidthFixed, 58.0F);
            ImGui::TableSetupColumn("Hex", ImGuiTableColumnFlags_WidthFixed, 76.0F);
            ImGui::TableSetupColumn("Usage", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            for (const EditorUiColorToken& token : editorUiColorTokens()) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                text(token.name);
                ImGui::TableSetColumnIndex(1);
                drawColorChip(token.color);
                ImGui::TableSetColumnIndex(2);
                const std::string hex = hexColor(token.color);
                text(hex);
                ImGui::TableSetColumnIndex(3);
                mutedText(token.usage);
            }

            ImGui::EndTable();
        }

        void drawThemeSelector(EditorSettingsController& settings) {
            const EditorUiThemeId currentTheme = settings.settings().theme;
            const EditorUiTheme& selectedTheme = editorUiTheme(currentTheme);
            const std::string selectedThemeLabel{selectedTheme.name};
            ImGui::SetNextItemWidth(260.0F);
            if (ImGui::BeginCombo("Theme###ui-style-theme", selectedThemeLabel.c_str())) {
                for (const EditorUiTheme& theme : editorUiThemes()) {
                    const std::string themeLabel{theme.name};
                    const bool selected = theme.id == currentTheme;
                    if (ImGui::Selectable(themeLabel.c_str(), selected)) {
                        if (auto changed = settings.setTheme(theme.id); !changed) {
                            static_cast<void>(changed.error());
                        }
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        }

        void drawStateList() {
            ImGui::Selectable("Selected pass: builtin.debug-image-copy", true);
            ImGui::Selectable("Hovered resource: #4 SceneColor", false);
            ImGui::BeginDisabled();
            ImGui::Selectable("Unavailable preview: depth target", false);
            ImGui::EndDisabled();
        }

        void drawToolbarPreview() {
            ImGui::SmallButton("Capture");
            ImGui::SameLine();
            ImGui::SmallButton("Resume");
            ImGui::SameLine();
            ImGui::BeginDisabled();
            ImGui::SmallButton("Step");
            ImGui::EndDisabled();
            ImGui::SameLine();
            drawEditorUiStatusPill("Paused", EditorUiTone::Warning);
        }

        void drawSwatchStrip() {
            const EditorUiTheme& theme = editorUiTheme();
            drawEditorUiColorSwatch("App", theme.appBackground);
            ImGui::SameLine();
            drawEditorUiColorSwatch("Panel", theme.panelBackground);
            ImGui::SameLine();
            drawEditorUiColorSwatch("Surface", theme.surface);
            ImGui::SameLine();
            drawEditorUiColorSwatch("Accent", theme.accent);
            ImGui::SameLine();
            drawEditorUiColorSwatch("Viewport", theme.viewportBackground);
        }

    } // namespace

    const EditorPanelDesc& UiStylePreviewPanel::desc() const {
        return desc_;
    }

    void UiStylePreviewPanel::prepareWindow(EditorFrameContext& context, EditorPanelState& state) {
        static_cast<void>(state);
        const ImGuiCond condition = context.smokeMode ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
        ImGui::SetNextWindowSize(ImVec2{760.0F, 620.0F}, condition);
    }

    void UiStylePreviewPanel::draw(EditorFrameContext& context, EditorPanelState& state) {
        static_cast<void>(state);

        const EditorUiTheme& theme = editorUiTheme();
        drawEditorUiSectionHeader("Theme");
        drawThemeSelector(context.settings);
        if (beginEditorUiPropertyTable("ui-style-summary", 128.0F)) {
            drawEditorUiProperty(EditorUiProperty{.label = "Palette", .value = theme.name});
            drawEditorUiProperty(EditorUiProperty{.label = "Storage", .value = theme.storageName});
            drawEditorUiProperty(
                EditorUiProperty{.label = "Density", .value = "compact editor tooling"});
            drawEditorUiProperty(EditorUiProperty{.label = "Rounding", .value = "2 / 2 / 3 / 4"});
            drawEditorUiProperty(
                EditorUiProperty{.label = "Motion", .value = "static states, hover feedback only"});
            drawEditorUiProperty(
                EditorUiProperty{.label = "Scope", .value = "apps/editor ImGui helpers"});
            endEditorUiPropertyTable();
        }

        drawTokenPalette();

        drawEditorUiSectionHeader("Components");
        if (!ImGui::BeginTable("ui-style-preview-table", 3, kPreviewTableFlags)) {
            return;
        }
        ImGui::TableSetupColumn("Component", ImGuiTableColumnFlags_WidthFixed, 132.0F);
        ImGui::TableSetupColumn("Preview", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Token", ImGuiTableColumnFlags_WidthFixed, 132.0F);
        ImGui::TableHeadersRow();

        beginPreviewRow(PreviewRow{.component = "Text", .token = "typography"});
        ImGui::TextUnformatted("Primary");
        ImGui::SameLine();
        mutedText("Secondary");
        ImGui::SameLine();
        drawEditorUiStatusPill("Info", EditorUiTone::Info);
        endPreviewRow();

        beginPreviewRow(PreviewRow{.component = "Toolbar", .token = "command row"});
        drawToolbarPreview();
        endPreviewRow();

        beginPreviewRow(PreviewRow{.component = "Buttons", .token = "action"});
        ImGui::Button("Apply", ImVec2{86.0F, 0.0F});
        ImGui::SameLine();
        ImGui::SmallButton("Reset");
        ImGui::SameLine();
        ImGui::BeginDisabled();
        ImGui::Button("Disabled", ImVec2{86.0F, 0.0F});
        ImGui::EndDisabled();
        endPreviewRow();

        beginPreviewRow(PreviewRow{.component = "Toggles", .token = "boolean"});
        ImGui::Checkbox("Enabled", &checkboxValue_);
        ImGui::SameLine();
        ImGui::RadioButton("Mode A", comboIndex_ == 0);
        ImGui::SameLine();
        ImGui::RadioButton("Mode B", comboIndex_ == 1);
        endPreviewRow();

        beginPreviewRow(PreviewRow{.component = "Inputs", .token = "field"});
        ImGui::SetNextItemWidth(160.0F);
        ImGui::InputText("Name", textBuffer_.data(), textBuffer_.size());
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140.0F);
        ImGui::SliderFloat("Weight", &sliderValue_, 0.0F, 1.0F);
        endPreviewRow();

        beginPreviewRow(PreviewRow{.component = "Combos", .token = "choice"});
        constexpr std::array<const char*, 3> kItems{"Scene", "Frame Debug", "RG View"};
        ImGui::SetNextItemWidth(180.0F);
        comboIndex_ = std::clamp(comboIndex_, 0, static_cast<int>(kItems.size() - 1U));
        if (ImGui::BeginCombo("View", kItems.at(static_cast<std::size_t>(comboIndex_)))) {
            for (int itemIndex = 0; itemIndex < static_cast<int>(kItems.size()); ++itemIndex) {
                const bool selected = comboIndex_ == itemIndex;
                if (ImGui::Selectable(kItems.at(static_cast<std::size_t>(itemIndex)), selected)) {
                    comboIndex_ = itemIndex;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        endPreviewRow();

        beginPreviewRow(PreviewRow{.component = "Tabs", .token = "view switch"});
        if (ImGui::BeginTabBar("style-preview-tabs")) {
            if (ImGui::BeginTabItem("Frame Debug")) {
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("RG View")) {
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Preview")) {
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        endPreviewRow();

        beginPreviewRow(PreviewRow{.component = "Rows", .token = "selection"});
        drawStateList();
        endPreviewRow();

        beginPreviewRow(PreviewRow{.component = "Status", .token = "feedback"});
        drawEditorUiStatusPill("Ready", EditorUiTone::Success);
        ImGui::SameLine();
        drawEditorUiStatusPill("Pending", EditorUiTone::Warning);
        ImGui::SameLine();
        drawEditorUiStatusPill("Blocked", EditorUiTone::Danger);
        ImGui::SameLine();
        drawEditorUiStatusPill("Idle", EditorUiTone::Muted);
        endPreviewRow();

        beginPreviewRow(PreviewRow{.component = "Swatches", .token = "palette"});
        drawSwatchStrip();
        endPreviewRow();

        beginPreviewRow(PreviewRow{.component = "Progress", .token = "meter"});
        ImGui::ProgressBar(sliderValue_, ImVec2{220.0F, 0.0F});
        endPreviewRow();

        ImGui::EndTable();
    }

} // namespace asharia::editor
