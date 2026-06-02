#include "panels/editor_settings_panel.hpp"

#include <array>
#include <cstdint>
#include <imgui.h>
#include <string>
#include <string_view>

#include "editor_i18n.hpp"
#include "editor_settings.hpp"
#include "editor_ui.hpp"

namespace asharia::editor {

    namespace {

        struct LocaleOption {
            EditorLocale locale{};
            std::string_view labelKey;
            std::string_view fallback;
        };

        enum class SettingsCategory : std::uint8_t {
            General,
            Viewport,
        };

        struct SettingsCategoryOption {
            SettingsCategory category{};
            std::string_view labelKey;
            std::string_view stableId;
            std::string_view fallback;
        };

        constexpr std::array<LocaleOption, 2> kLocaleOptions{
            LocaleOption{.locale = EditorLocale::EnUs,
                         .labelKey = "settings.language.enUs",
                         .fallback = "English (US)"},
            LocaleOption{.locale = EditorLocale::ZhHans,
                         .labelKey = "settings.language.zhHans",
                         .fallback = "Chinese (Simplified)"},
        };

        constexpr std::array<SettingsCategoryOption, 2> kSettingsCategories{
            SettingsCategoryOption{.category = SettingsCategory::General,
                                   .labelKey = "settings.general",
                                   .stableId = "editor-settings-category-general",
                                   .fallback = "General"},
            SettingsCategoryOption{.category = SettingsCategory::Viewport,
                                   .labelKey = "settings.viewport",
                                   .stableId = "editor-settings-category-viewport",
                                   .fallback = "Viewport"},
        };

        struct EditorSettingsPanelContext {
            EditorSettingsController* settings{};
            const EditorI18n* i18n{};
        };

        void text(std::string_view value) {
            ImGui::TextUnformatted(value.data(), value.data() + value.size());
        }

        [[nodiscard]] const SettingsCategoryOption&
        settingsCategoryOption(SettingsCategory category) {
            for (const SettingsCategoryOption& option : kSettingsCategories) {
                if (option.category == category) {
                    return option;
                }
            }
            return kSettingsCategories.front();
        }

        [[nodiscard]] std::string settingsCategoryLabel(const EditorI18n& i18n,
                                                        const SettingsCategoryOption& option) {
            return i18n.label(EditorI18nLabelDesc{
                .key = option.labelKey,
                .stableId = option.stableId,
                .fallback = option.fallback,
            });
        }

        [[nodiscard]] std::string settingsCategoryTitle(const EditorI18n& i18n,
                                                        SettingsCategory category) {
            const SettingsCategoryOption& option = settingsCategoryOption(category);
            return std::string{i18n.text(EditorI18nTextQuery{
                .key = option.labelKey,
                .fallback = option.fallback,
            })};
        }

        [[nodiscard]] std::string localeLabel(const EditorI18n& i18n, const LocaleOption& option) {
            return std::string{i18n.text(EditorI18nTextQuery{
                .key = option.labelKey,
                .fallback = option.fallback,
            })};
        }

        [[nodiscard]] std::string currentLocaleLabel(const EditorI18n& i18n, EditorLocale locale) {
            for (const LocaleOption& option : kLocaleOptions) {
                if (option.locale == locale) {
                    return localeLabel(i18n, option);
                }
            }
            return std::string{editorLocaleName(locale)};
        }

        [[nodiscard]] std::string currentThemeLabel(EditorUiThemeId themeId) {
            return std::string{editorUiTheme(themeId).name};
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

        void drawLanguageSetting(EditorSettingsController& settings, const EditorI18n& i18n) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            text(i18n.text("settings.language"));
            ImGui::TableSetColumnIndex(1);

            const EditorLocale currentLocale = settings.settings().locale;
            const std::string selectedLabel = currentLocaleLabel(i18n, currentLocale);
            ImGui::SetNextItemWidth(-1.0F);
            const std::string comboLabel = i18n.label(EditorI18nLabelDesc{
                .key = "settings.language",
                .stableId = "editor-settings-language",
                .fallback = "Language",
            });
            if (ImGui::BeginCombo(comboLabel.c_str(), selectedLabel.c_str())) {
                for (const LocaleOption& option : kLocaleOptions) {
                    const std::string optionLabel = localeLabel(i18n, option);
                    const bool selected = option.locale == currentLocale;
                    if (ImGui::Selectable(optionLabel.c_str(), selected)) {
                        if (auto changed = settings.setLocale(option.locale); !changed) {
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

        void drawThemeSetting(EditorSettingsController& settings, const EditorI18n& i18n) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            text(i18n.text(EditorI18nTextQuery{
                .key = "settings.theme",
                .fallback = "Theme",
            }));
            ImGui::TableSetColumnIndex(1);

            const EditorUiThemeId currentTheme = settings.settings().theme;
            const std::string selectedThemeLabel = currentThemeLabel(currentTheme);
            ImGui::SetNextItemWidth(-1.0F);
            const std::string themeComboLabel = i18n.label(EditorI18nLabelDesc{
                .key = "settings.theme",
                .stableId = "editor-settings-theme",
                .fallback = "Theme",
            });
            if (ImGui::BeginCombo(themeComboLabel.c_str(), selectedThemeLabel.c_str())) {
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

        [[nodiscard]] bool drawSceneGridFloatSetting(const EditorI18n& i18n, std::string_view key,
                                                     std::string_view stableId,
                                                     std::string_view fallback, float& value,
                                                     float speed, float minValue, float maxValue) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            text(i18n.text(EditorI18nTextQuery{
                .key = key,
                .fallback = fallback,
            }));
            ImGui::TableSetColumnIndex(1);

            ImGui::SetNextItemWidth(-1.0F);
            const std::string label = i18n.label(EditorI18nLabelDesc{
                .key = key,
                .stableId = stableId,
                .fallback = fallback,
            });
            return ImGui::DragFloat(label.c_str(), &value, speed, minValue, maxValue, "%.3f",
                                    ImGuiSliderFlags_AlwaysClamp);
        }

        [[nodiscard]] bool drawSceneGridOpacitySetting(const EditorI18n& i18n, float& value) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            text(i18n.text(EditorI18nTextQuery{
                .key = "settings.sceneGrid.opacity",
                .fallback = "Opacity",
            }));
            ImGui::TableSetColumnIndex(1);

            ImGui::SetNextItemWidth(-1.0F);
            const std::string label = i18n.label(EditorI18nLabelDesc{
                .key = "settings.sceneGrid.opacity",
                .stableId = "editor-settings-scene-grid-opacity",
                .fallback = "Opacity",
            });
            return ImGui::SliderFloat(label.c_str(), &value, 0.0F, 1.0F, "%.2f",
                                      ImGuiSliderFlags_AlwaysClamp);
        }

        [[nodiscard]] bool drawSceneGridColorSetting(const EditorI18n& i18n,
                                                     std::array<float, 4>& color) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            text(i18n.text(EditorI18nTextQuery{
                .key = "settings.sceneGrid.color",
                .fallback = "Grid Color",
            }));
            ImGui::TableSetColumnIndex(1);

            ImGui::SetNextItemWidth(-1.0F);
            const std::string label = i18n.label(EditorI18nLabelDesc{
                .key = "settings.sceneGrid.color",
                .stableId = "editor-settings-scene-grid-color",
                .fallback = "Grid Color",
            });
            constexpr ImGuiColorEditFlags kColorFlags =
                ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf;
            return ImGui::ColorEdit4(label.c_str(), color.data(), kColorFlags);
        }

        void drawSettingsCategoryList(const EditorI18n& i18n, SettingsCategory& selectedCategory) {
            drawEditorUiSectionHeader(i18n.text(EditorI18nTextQuery{
                .key = "settings.categories",
                .fallback = "Categories",
            }));

            for (const SettingsCategoryOption& option : kSettingsCategories) {
                const bool selected = option.category == selectedCategory;
                const std::string label = settingsCategoryLabel(i18n, option);
                if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_None,
                                      ImVec2{0.0F, 0.0F})) {
                    selectedCategory = option.category;
                }
            }
        }

        void drawGeneralSettings(EditorSettingsController& settings, const EditorI18n& i18n) {
            drawEditorUiSectionHeader(settingsCategoryTitle(i18n, SettingsCategory::General));
            if (beginEditorUiPropertyTable("editor-settings-general", 128.0F)) {
                drawLanguageSetting(settings, i18n);
                drawThemeSetting(settings, i18n);
                endEditorUiPropertyTable();
            }
        }

        void drawSceneGridSettings(EditorSettingsController& settings, const EditorI18n& i18n) {
            drawEditorUiSectionHeader(i18n.text(EditorI18nTextQuery{
                .key = "settings.sceneGrid",
                .fallback = "Scene Grid",
            }));

            EditorViewportWorldGridSettings sceneGrid = settings.settings().sceneGrid;
            bool changed = false;
            if (beginEditorUiPropertyTable("editor-settings-scene-grid", 148.0F)) {
                changed |= drawSceneGridFloatSetting(
                    i18n, "settings.sceneGrid.planeY", "editor-settings-scene-grid-plane-y",
                    "Plane Y", sceneGrid.planeY, 0.05F, -1000.0F, 1000.0F);
                changed |= drawSceneGridFloatSetting(i18n, "settings.sceneGrid.minorSpacing",
                                                     "editor-settings-scene-grid-minor-spacing",
                                                     "Minor Spacing", sceneGrid.minorSpacing, 0.05F,
                                                     0.0001F, 1000.0F);
                changed |= drawSceneGridFloatSetting(i18n, "settings.sceneGrid.majorSpacing",
                                                     "editor-settings-scene-grid-major-spacing",
                                                     "Major Spacing", sceneGrid.majorSpacing, 0.25F,
                                                     0.0001F, 10000.0F);
                changed |= drawSceneGridFloatSetting(
                    i18n, "settings.sceneGrid.fadeStart", "editor-settings-scene-grid-fade-start",
                    "Fade Start", sceneGrid.fadeStart, 0.25F, 0.0F, 10000.0F);
                changed |= drawSceneGridFloatSetting(
                    i18n, "settings.sceneGrid.fadeEnd", "editor-settings-scene-grid-fade-end",
                    "Fade End", sceneGrid.fadeEnd, 0.25F, 0.0F, 10000.0F);
                changed |= drawSceneGridOpacitySetting(i18n, sceneGrid.opacity);
                changed |= drawSceneGridColorSetting(i18n, sceneGrid.color);
                endEditorUiPropertyTable();
            }

            if (changed) {
                if (auto saved = settings.setSceneGrid(sceneGrid); !saved) {
                    static_cast<void>(saved.error());
                }
            }
        }

        void drawViewportSettings(EditorSettingsController& settings, const EditorI18n& i18n) {
            drawEditorUiSectionHeader(settingsCategoryTitle(i18n, SettingsCategory::Viewport));
            drawSceneGridSettings(settings, i18n);
        }

        void drawSelectedSettingsCategory(const EditorSettingsPanelContext& context,
                                          SettingsCategory selectedCategory) {
            switch (selectedCategory) {
            case SettingsCategory::Viewport:
                drawViewportSettings(*context.settings, *context.i18n);
                return;
            case SettingsCategory::General:
            default:
                drawGeneralSettings(*context.settings, *context.i18n);
                return;
            }
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

        static SettingsCategory selectedCategory = SettingsCategory::General;

        ImGui::BeginChild("editor-settings-category-pane", ImVec2{156.0F, 0.0F},
                          ImGuiChildFlags_Borders);
        drawSettingsCategoryList(*panelContext.i18n, selectedCategory);
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("editor-settings-content-pane", ImVec2{0.0F, 0.0F},
                          ImGuiChildFlags_Borders);
        drawSelectedSettingsCategory(panelContext, selectedCategory);

        drawSettingsStatus(*panelContext.i18n, *panelContext.settings);
        ImGui::EndChild();
    }

} // namespace asharia::editor
