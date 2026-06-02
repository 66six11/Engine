#include "panels/editor_settings_panel.hpp"

#include <imgui.h>
#include <string>
#include <string_view>

#include "editor_i18n.hpp"
#include "editor_settings.hpp"
#include "editor_settings_contribution.hpp"
#include "editor_ui.hpp"

namespace asharia::editor {

    namespace {

        struct EditorSettingsPanelContext {
            EditorSettingsController* settings{};
            const EditorI18n* i18n{};
        };

        void text(std::string_view value) {
            ImGui::TextUnformatted(value.data(), value.data() + value.size());
        }

        [[nodiscard]] std::string
        settingsCategoryLabel(const EditorI18n& i18n,
                              const EditorSettingsCategoryContribution& contribution) {
            return i18n.label(EditorI18nLabelDesc{
                .key = contribution.labelKey,
                .stableId = contribution.stableId,
                .fallback = contribution.fallback,
            });
        }

        void drawSettingsStatus(const EditorI18n& i18n, const EditorSettingsController& settings) {
            if (!settings.lastSaveAttempted()) {
                return;
            }

            ImGui::Spacing();
            drawEditorUiStatusPill(settings.lastSaveFailed() ? i18n.text("settings.saveFailed")
                                                             : i18n.text("settings.saved"),
                                   settings.lastSaveFailed() ? EditorUiTone::Danger
                                                             : EditorUiTone::Success);
            if (settings.lastSaveFailed()) {
                ImGui::SameLine();
                text(settings.lastSaveError());
            }
        }

        void drawSettingsCategoryList(const EditorI18n& i18n, std::string& selectedCategoryId) {
            drawEditorUiSectionHeader(i18n.text(EditorI18nTextQuery{
                .key = "settings.categories",
                .fallback = "Categories",
            }));

            for (const EditorSettingsCategoryContribution& contribution :
                 builtInEditorSettingsCategoryContributions()) {
                const bool selected = contribution.id == selectedCategoryId;
                const std::string label = settingsCategoryLabel(i18n, contribution);
                if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_None,
                                      ImVec2{0.0F, 0.0F})) {
                    selectedCategoryId = contribution.id;
                }
            }
        }

        void drawSelectedSettingsCategory(const EditorSettingsPanelContext& context,
                                          std::string& selectedCategoryId) {
            const EditorSettingsCategoryContribution* selectedCategory =
                findBuiltInEditorSettingsCategoryContribution(selectedCategoryId);
            if (selectedCategory == nullptr) {
                selectedCategory = builtInEditorSettingsCategoryContributions().data();
                selectedCategoryId = selectedCategory->id;
            }

            EditorSettingsContributionDrawContext drawContext{
                .settings = context.settings,
                .i18n = context.i18n,
            };
            selectedCategory->draw(drawContext);
        }

    } // namespace

    const EditorPanelDesc& EditorSettingsPanel::desc() const {
        return desc_;
    }

    void EditorSettingsPanel::prepareWindow(EditorPanelWindowContext& context,
                                            EditorPanelState& state) {
        static_cast<void>(state);
        const ImGuiCond condition =
            context.ui.smokeMode ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
        ImGui::SetNextWindowSize(ImVec2{640.0F, 420.0F}, condition);
    }

    void EditorSettingsPanel::drawEditorSettingsPanel(EditorSettingsPanelDrawContext& context,
                                                      EditorPanelState& state) {
        static_cast<void>(state);

        EditorSettingsPanelContext panelContext{
            .settings = &context.settings,
            .i18n = &context.ui.i18n,
        };

        ImGui::BeginChild("editor-settings-category-pane", ImVec2{156.0F, 0.0F},
                          ImGuiChildFlags_Borders);
        drawSettingsCategoryList(*panelContext.i18n, selectedCategoryId_);
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("editor-settings-content-pane", ImVec2{0.0F, 0.0F},
                          ImGuiChildFlags_Borders);
        drawSelectedSettingsCategory(panelContext, selectedCategoryId_);
        drawSettingsStatus(*panelContext.i18n, *panelContext.settings);
        ImGui::EndChild();
    }

} // namespace asharia::editor
