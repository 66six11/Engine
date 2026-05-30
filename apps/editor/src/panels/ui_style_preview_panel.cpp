#include "panels/ui_style_preview_panel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <imgui.h>
#include <span>
#include <string>
#include <string_view>

#include "editor_settings.hpp"
#include "editor_ui.hpp"

namespace asharia::editor {

    namespace {

        constexpr ImGuiTableFlags kWorkbenchTableFlags = ImGuiTableFlags_BordersInnerV |
                                                         ImGuiTableFlags_Resizable |
                                                         ImGuiTableFlags_SizingStretchProp;

        constexpr ImGuiTableFlags kPreviewTableFlags = ImGuiTableFlags_RowBg |
                                                       ImGuiTableFlags_BordersInnerH |
                                                       ImGuiTableFlags_SizingStretchProp;

        struct FormPreviewState {
            std::array<char, 64>* textBuffer{};
            bool* checkboxValue{};
            float* sliderValue{};
            int* comboIndex{};
        };

        struct UiStylePreviewPanelContext {
            EditorSettingsController* settings{};
        };

        struct TablePreviewRow {
            std::string_view pass;
            std::string_view type;
            std::string_view gpu;
            EditorUiTone tone{};
            std::string_view state;
        };

        [[nodiscard]] std::string hexColor(ColorSrgba8 color) {
            static constexpr std::array<char, 16> kHexDigits{
                '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
            const auto hexByte = [](const int value, const bool high) {
                return kHexDigits.at(static_cast<std::size_t>((value >> (high ? 4 : 0)) & 0xF));
            };
            std::string value{'#',
                              hexByte(color.r, true),
                              hexByte(color.r, false),
                              hexByte(color.g, true),
                              hexByte(color.g, false),
                              hexByte(color.b, true),
                              hexByte(color.b, false)};
            if (color.a != 0xFFU) {
                value.push_back(hexByte(color.a, true));
                value.push_back(hexByte(color.a, false));
            }
            return value;
        }

        [[nodiscard]] std::string srgbBytes(ColorSrgba8 color) {
            return std::to_string(color.r) + ", " + std::to_string(color.g) + ", " +
                   std::to_string(color.b) + ", " + std::to_string(color.a);
        }

        [[nodiscard]] ImU32 colorWithAlpha(ColorSrgba8 color, const float alpha) {
            ImVec4 encoded = toImGuiEncodedSrgbVec4(color);
            encoded.w *= std::clamp(alpha, 0.0F, 1.0F);
            return ImGui::GetColorU32(encoded);
        }

        void text(std::string_view value) {
            ImGui::TextUnformatted(value.data(), value.data() + value.size());
        }

        void mutedText(std::string_view value) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            text(value);
            ImGui::PopStyleColor();
        }

        void drawColorChip(ColorSrgba8 color, ImVec2 size = ImVec2{22.0F, 14.0F}) {
            const ImVec2 min = ImGui::GetCursorScreenPos();
            const ImVec2 max{min.x + size.x, min.y + size.y};
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(min, max, toImGuiEncodedSrgbU32(color), 2.0F);
            drawList->AddRect(min, max, ImGui::GetColorU32(ImGuiCol_Border), 2.0F);
            ImGui::Dummy(size);
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

        void drawWorkbenchHeader(EditorSettingsController& settings) {
            text("Theme Workbench");
            ImGui::SameLine();
            drawEditorUiStatusPill("compact editor tooling", EditorUiTone::Muted);
            ImGui::SameLine();
            drawThemeSelector(settings);
            ImGui::Separator();
        }

        void drawTokenList(std::size_t& selectedTokenIndex) {
            const std::span<const EditorUiColorToken> tokens = editorUiColorTokens();
            if (tokens.empty()) {
                mutedText("No tokens");
                return;
            }
            selectedTokenIndex = std::min(selectedTokenIndex, tokens.size() - 1U);

            drawEditorUiSectionHeader("Tokens");
            if (!ImGui::BeginChild("theme-workbench-token-list", ImVec2{0.0F, 0.0F},
                                   ImGuiChildFlags_Borders)) {
                ImGui::EndChild();
                return;
            }

            for (std::size_t index = 0; index < tokens.size(); ++index) {
                const EditorUiColorToken& token = tokens[index];
                const bool selected = selectedTokenIndex == index;
                ImGui::PushID(static_cast<int>(index));
                drawColorChip(token.color);
                ImGui::SameLine();
                const std::string label{token.name};
                if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_None,
                                      ImVec2{0.0F, 0.0F})) {
                    selectedTokenIndex = index;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    text(token.usage);
                    ImGui::EndTooltip();
                }
                ImGui::PopID();
            }

            ImGui::EndChild();
        }

        void drawTokenPane(std::size_t& selectedTokenIndex) {
            drawEditorUiSectionHeader("Token Groups");
            mutedText("Background");
            mutedText("Surface");
            mutedText("Text");
            mutedText("Accent");
            mutedText("State");
            mutedText("Viewport");
            drawTokenList(selectedTokenIndex);
        }

        void drawButtonsPreview() {
            ImGui::Button("Apply", ImVec2{86.0F, 0.0F});
            ImGui::SameLine();
            ImGui::SmallButton("Reset");
            ImGui::SameLine();
            ImGui::BeginDisabled();
            ImGui::Button("Disabled", ImVec2{92.0F, 0.0F});
            ImGui::EndDisabled();
        }

        void drawFormPreview(FormPreviewState state) {
            ImGui::Checkbox("Enabled", state.checkboxValue);
            ImGui::SameLine();
            ImGui::RadioButton("Mode A", *state.comboIndex == 0);
            ImGui::SameLine();
            ImGui::RadioButton("Mode B", *state.comboIndex == 1);

            ImGui::SetNextItemWidth(180.0F);
            ImGui::InputText("Name", state.textBuffer->data(), state.textBuffer->size());
            ImGui::SameLine();
            ImGui::SetNextItemWidth(160.0F);
            ImGui::SliderFloat("Weight", state.sliderValue, 0.0F, 1.0F);

            constexpr std::array<const char*, 3> kItems{"Scene", "Frame Debug", "RG View"};
            *state.comboIndex =
                std::clamp(*state.comboIndex, 0, static_cast<int>(kItems.size() - 1U));
            ImGui::SetNextItemWidth(180.0F);
            if (ImGui::BeginCombo("View", kItems.at(static_cast<std::size_t>(*state.comboIndex)))) {
                for (int itemIndex = 0; itemIndex < static_cast<int>(kItems.size()); ++itemIndex) {
                    const bool selected = *state.comboIndex == itemIndex;
                    if (ImGui::Selectable(kItems.at(static_cast<std::size_t>(itemIndex)),
                                          selected)) {
                        *state.comboIndex = itemIndex;
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        }

        void drawTabsPreview() {
            if (ImGui::BeginTabBar("style-preview-tabs")) {
                if (ImGui::BeginTabItem("Scene View")) {
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Frame Debug")) {
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Theme")) {
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
        }

        void drawTablePreview() {
            if (!ImGui::BeginTable("theme-workbench-table-preview", 4, kPreviewTableFlags)) {
                return;
            }
            ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 84.0F);
            ImGui::TableSetupColumn("GPU", ImGuiTableColumnFlags_WidthFixed, 58.0F);
            ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 82.0F);
            ImGui::TableHeadersRow();

            const auto row = [](const TablePreviewRow& previewRow) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                text(previewRow.pass);
                ImGui::TableNextColumn();
                mutedText(previewRow.type);
                ImGui::TableNextColumn();
                text(previewRow.gpu);
                ImGui::TableNextColumn();
                drawEditorUiStatusPill(previewRow.state, previewRow.tone);
            };
            row(TablePreviewRow{.pass = "SceneColor",
                                .type = "Color",
                                .gpu = "1.8ms",
                                .tone = EditorUiTone::Success,
                                .state = "Ready"});
            row(TablePreviewRow{.pass = "ShadowAtlas",
                                .type = "Depth",
                                .gpu = "0.7ms",
                                .tone = EditorUiTone::Muted,
                                .state = "Idle"});
            row(TablePreviewRow{.pass = "PostProcess",
                                .type = "Compute",
                                .gpu = "0.4ms",
                                .tone = EditorUiTone::Warning,
                                .state = "Pending"});
            row(TablePreviewRow{.pass = "DebugCopy",
                                .type = "Transfer",
                                .gpu = "0.1ms",
                                .tone = EditorUiTone::Danger,
                                .state = "Blocked"});

            ImGui::EndTable();
        }

        void drawTreePreview() {
            if (ImGui::TreeNodeEx("World", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::TreeNodeEx("CameraRig", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_Bullet);
                ImGui::TreePop();
                ImGui::TreeNodeEx("DirectionalLight", ImGuiTreeNodeFlags_Leaf |
                                                          ImGuiTreeNodeFlags_Bullet |
                                                          ImGuiTreeNodeFlags_Selected);
                ImGui::TreePop();
                if (ImGui::TreeNodeEx("Environment", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::TreeNodeEx("SkyProbe",
                                      ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_Bullet);
                    ImGui::TreePop();
                    ImGui::TreePop();
                }
                ImGui::TreePop();
            }
        }

        void drawViewportMock() {
            const EditorUiTheme& theme = editorUiTheme();
            const float width = std::max(260.0F, ImGui::GetContentRegionAvail().x);
            const ImVec2 size{width, 190.0F};
            const ImVec2 min = ImGui::GetCursorScreenPos();
            const ImVec2 max{min.x + size.x, min.y + size.y};
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(min, max, toImGuiEncodedSrgbU32(theme.viewportBackground),
                                    3.0F);
            drawList->AddRect(min, max, toImGuiEncodedSrgbU32(theme.border), 3.0F);

            const int verticalLines = static_cast<int>(std::floor(size.x / 24.0F));
            for (int lineIndex = 1; lineIndex < verticalLines; ++lineIndex) {
                const float gridX = min.x + (static_cast<float>(lineIndex) * 24.0F);
                drawList->AddLine(ImVec2{gridX, min.y}, ImVec2{gridX, max.y},
                                  colorWithAlpha(theme.divider, 0.52F));
            }
            const int horizontalLines = static_cast<int>(std::floor(size.y / 24.0F));
            for (int lineIndex = 1; lineIndex < horizontalLines; ++lineIndex) {
                const float gridY = min.y + (static_cast<float>(lineIndex) * 24.0F);
                drawList->AddLine(ImVec2{min.x, gridY}, ImVec2{max.x, gridY},
                                  colorWithAlpha(theme.divider, 0.52F));
            }

            const ImVec2 headerMin{min.x + 8.0F, min.y + 8.0F};
            const ImVec2 headerMax{min.x + 218.0F, min.y + 58.0F};
            drawList->AddRectFilled(headerMin, headerMax,
                                    colorWithAlpha(theme.inputBackground, 0.9F), 3.0F);
            drawList->AddRect(headerMin, headerMax, colorWithAlpha(theme.border, 0.82F), 3.0F);
            drawList->AddText(ImVec2{headerMin.x + 8.0F, headerMin.y + 7.0F},
                              toImGuiEncodedSrgbU32(theme.text), "Scene View");
            drawList->AddText(ImVec2{headerMin.x + 8.0F, headerMin.y + 27.0F},
                              toImGuiEncodedSrgbU32(theme.textSecondary),
                              "Lit | Perspective | Grid");

            const ImVec2 selectionMin{min.x + (size.x * 0.45F), min.y + 74.0F};
            const ImVec2 selectionMax{selectionMin.x + 88.0F, selectionMin.y + 54.0F};
            drawList->AddRectFilled(selectionMin, selectionMax, colorWithAlpha(theme.accent, 0.15F),
                                    2.0F);
            drawList->AddRect(selectionMin, selectionMax, toImGuiEncodedSrgbU32(theme.accent), 2.0F,
                              0, 2.0F);
            const ImVec2 pivot{selectionMin.x + 44.0F, selectionMin.y + 27.0F};
            drawList->AddCircleFilled(pivot, 3.0F, toImGuiEncodedSrgbU32(theme.warning));
            drawList->AddLine(pivot, ImVec2{pivot.x + 42.0F, pivot.y},
                              toImGuiEncodedSrgbU32(theme.danger), 2.0F);
            drawList->AddLine(pivot, ImVec2{pivot.x, pivot.y - 42.0F},
                              toImGuiEncodedSrgbU32(theme.success), 2.0F);
            drawList->AddLine(pivot, ImVec2{pivot.x + 30.0F, pivot.y + 30.0F},
                              toImGuiEncodedSrgbU32(theme.accent), 2.0F);

            drawList->AddText(ImVec2{max.x - 170.0F, max.y - 26.0F},
                              toImGuiEncodedSrgbU32(theme.textSecondary), "1920x1080 | GPU 4.2ms");
            ImGui::Dummy(size);
        }

        void drawTextureViewerMock() {
            const EditorUiTheme& theme = editorUiTheme();
            const float width = std::max(260.0F, ImGui::GetContentRegionAvail().x);
            const ImVec2 size{width, 124.0F};
            const ImVec2 min = ImGui::GetCursorScreenPos();
            const ImVec2 max{min.x + size.x, min.y + size.y};
            ImDrawList* drawList = ImGui::GetWindowDrawList();

            constexpr float kCell = 18.0F;
            const int rowCount = static_cast<int>(std::ceil(size.y / kCell));
            const int columnCount = static_cast<int>(std::ceil(size.x / kCell));
            for (int rowIndex = 0; rowIndex < rowCount; ++rowIndex) {
                for (int columnIndex = 0; columnIndex < columnCount; ++columnIndex) {
                    const float cellX = min.x + (static_cast<float>(columnIndex) * kCell);
                    const float cellY = min.y + (static_cast<float>(rowIndex) * kCell);
                    const bool alt = ((columnIndex + rowIndex) % 2) == 0;
                    const ColorSrgba8 color = alt ? theme.menuBackground : theme.panelBackground;
                    drawList->AddRectFilled(
                        ImVec2{cellX, cellY},
                        ImVec2{std::min(cellX + kCell, max.x), std::min(cellY + kCell, max.y)},
                        toImGuiEncodedSrgbU32(color));
                }
            }
            drawList->AddRect(min, max, toImGuiEncodedSrgbU32(theme.border), 3.0F);
            drawList->AddRectFilled(ImVec2{min.x + 26.0F, min.y + 28.0F},
                                    ImVec2{max.x - 26.0F, max.y - 28.0F},
                                    colorWithAlpha(theme.surfaceActive, 0.84F), 2.0F);
            drawList->AddText(ImVec2{min.x + 12.0F, min.y + 8.0F},
                              toImGuiEncodedSrgbU32(theme.text),
                              "SceneColor | RGBA16F | Linear | Mip 0");
            drawList->AddText(ImVec2{min.x + 12.0F, max.y - 23.0F},
                              toImGuiEncodedSrgbU32(theme.textSecondary),
                              "Fit | R G B A | Exposure 0.0 | Pixel inspect");
            ImGui::Dummy(size);
        }

        void drawComponentPreview(std::array<char, 64>& textBuffer, bool& checkboxValue,
                                  float& sliderValue, int& comboIndex) {
            if (!ImGui::BeginChild("theme-workbench-component-preview", ImVec2{0.0F, 0.0F},
                                   ImGuiChildFlags_None,
                                   ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
                ImGui::EndChild();
                return;
            }

            drawEditorUiSectionHeader("Command Bar");
            drawToolbarPreview();

            drawEditorUiSectionHeader("Buttons");
            drawButtonsPreview();

            drawEditorUiSectionHeader("Form Controls");
            drawFormPreview(FormPreviewState{.textBuffer = &textBuffer,
                                             .checkboxValue = &checkboxValue,
                                             .sliderValue = &sliderValue,
                                             .comboIndex = &comboIndex});

            drawEditorUiSectionHeader("Tabs");
            drawTabsPreview();

            drawEditorUiSectionHeader("Tree / Table");
            ImGui::BeginChild("theme-workbench-tree-preview", ImVec2{180.0F, 122.0F},
                              ImGuiChildFlags_Borders);
            drawTreePreview();
            ImGui::EndChild();
            ImGui::SameLine();
            ImGui::BeginChild("theme-workbench-table-pane", ImVec2{0.0F, 122.0F},
                              ImGuiChildFlags_Borders);
            drawTablePreview();
            ImGui::EndChild();

            drawEditorUiSectionHeader("Status Chips");
            drawEditorUiStatusPill("Ready", EditorUiTone::Success);
            ImGui::SameLine();
            drawEditorUiStatusPill("Pending", EditorUiTone::Warning);
            ImGui::SameLine();
            drawEditorUiStatusPill("Blocked", EditorUiTone::Danger);
            ImGui::SameLine();
            drawEditorUiStatusPill("Idle", EditorUiTone::Muted);

            drawEditorUiSectionHeader("Swatches");
            drawSwatchStrip();

            drawEditorUiSectionHeader("Progress");
            ImGui::ProgressBar(sliderValue, ImVec2{220.0F, 0.0F});

            drawEditorUiSectionHeader("Viewport Mock");
            drawViewportMock();

            drawEditorUiSectionHeader("Texture Viewer Mock");
            drawTextureViewerMock();

            ImGui::EndChild();
        }

        void drawInspectorPane(std::size_t& selectedTokenIndex) {
            const std::span<const EditorUiColorToken> tokens = editorUiColorTokens();
            if (tokens.empty()) {
                mutedText("No token selected");
                return;
            }
            selectedTokenIndex = std::min(selectedTokenIndex, tokens.size() - 1U);
            const EditorUiColorToken& token = tokens[selectedTokenIndex];
            const EditorUiTheme& theme = editorUiTheme();

            drawEditorUiSectionHeader("Inspector");
            drawColorChip(token.color, ImVec2{44.0F, 28.0F});
            ImGui::SameLine();
            text(token.name);
            mutedText(token.usage);

            const std::string hex = hexColor(token.color);
            const std::string bytes = srgbBytes(token.color);
            if (beginEditorUiPropertyTable("theme-workbench-token-inspector", 84.0F)) {
                drawEditorUiProperty(EditorUiProperty{.label = "Hex", .value = hex});
                drawEditorUiProperty(EditorUiProperty{.label = "sRGBA", .value = bytes});
                drawEditorUiProperty(EditorUiProperty{.label = "Encoding", .value = "sRGB 8-bit"});
                drawEditorUiProperty(EditorUiProperty{.label = "Usage", .value = token.usage});
                endEditorUiPropertyTable();
            }

            drawEditorUiSectionHeader("Theme");
            if (beginEditorUiPropertyTable("theme-workbench-theme-inspector", 92.0F)) {
                drawEditorUiProperty(EditorUiProperty{.label = "Palette", .value = theme.name});
                drawEditorUiProperty(
                    EditorUiProperty{.label = "Storage", .value = theme.storageName});
                drawEditorUiProperty(
                    EditorUiProperty{.label = "Density", .value = "compact editor tooling"});
                drawEditorUiProperty(
                    EditorUiProperty{.label = "Motion", .value = "static hover states"});
                drawEditorUiProperty(
                    EditorUiProperty{.label = "Scope", .value = "apps/editor ImGui helpers"});
                endEditorUiPropertyTable();
            }

            drawEditorUiSectionHeader("Checks");
            drawEditorUiStatusPill("sRGB source", EditorUiTone::Info);
            ImGui::SameLine();
            drawEditorUiStatusPill("linear UI pass", EditorUiTone::Success);
            mutedText("Theme colors remain design-time sRGB hex; ImGui is only the adapter.");
        }

    } // namespace

    const EditorPanelDesc& UiStylePreviewPanel::desc() const {
        return desc_;
    }

    void UiStylePreviewPanel::prepareWindow(EditorPanelWindowContext& context,
                                            EditorPanelState& state) {
        static_cast<void>(state);
        const ImGuiCond condition =
            context.ui.smokeMode ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
        ImGui::SetNextWindowSize(ImVec2{980.0F, 680.0F}, condition);
    }

    void UiStylePreviewPanel::drawUiStylePreviewPanel(EditorUiStylePreviewPanelDrawContext& context,
                                                      EditorPanelState& state) {
        static_cast<void>(state);

        UiStylePreviewPanelContext panelContext{
            .settings = &context.settings,
        };

        drawWorkbenchHeader(*panelContext.settings);

        if (!ImGui::BeginTable("theme-workbench-layout", 3, kWorkbenchTableFlags)) {
            return;
        }
        ImGui::TableSetupColumn("Tokens", ImGuiTableColumnFlags_WidthFixed, 230.0F);
        ImGui::TableSetupColumn("Component Preview", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Inspector", ImGuiTableColumnFlags_WidthFixed, 270.0F);

        ImGui::TableNextColumn();
        drawTokenPane(selectedTokenIndex_);
        ImGui::TableNextColumn();
        drawComponentPreview(textBuffer_, checkboxValue_, sliderValue_, comboIndex_);
        ImGui::TableNextColumn();
        drawInspectorPane(selectedTokenIndex_);

        ImGui::EndTable();
    }

} // namespace asharia::editor
