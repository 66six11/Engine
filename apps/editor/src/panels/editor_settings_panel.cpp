#include "panels/editor_settings_panel.hpp"

#include <array>
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

        constexpr std::array<LocaleOption, 2> kLocaleOptions{
            LocaleOption{.locale = EditorLocale::EnUs,
                         .labelKey = "settings.language.enUs",
                         .fallback = "English (US)"},
            LocaleOption{.locale = EditorLocale::ZhHans,
                         .labelKey = "settings.language.zhHans",
                         .fallback = "Chinese (Simplified)"},
        };

        void text(std::string_view value) {
            ImGui::TextUnformatted(value.data(), value.data() + value.size());
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

    } // namespace

    const EditorPanelDesc& EditorSettingsPanel::desc() const {
        return desc_;
    }

    void EditorSettingsPanel::prepareWindow(EditorFrameContext& context, EditorPanelState& state) {
        static_cast<void>(state);
        const ImGuiCond condition =
            context.ui.smokeMode ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
        ImGui::SetNextWindowSize(ImVec2{440.0F, 180.0F}, condition);
    }

    void EditorSettingsPanel::draw(EditorFrameContext& context, EditorPanelState& state) {
        static_cast<void>(state);

        EditorSettingsController& settings = context.settings.controller;
        const EditorI18n& i18n = context.ui.i18n;

        drawEditorUiSectionHeader(i18n.text("settings.general"));
        if (beginEditorUiPropertyTable("editor-settings-general", 128.0F)) {
            drawLanguageSetting(settings, i18n);
            drawThemeSetting(settings, i18n);
            endEditorUiPropertyTable();
        }

        drawSettingsStatus(i18n, settings);
    }

} // namespace asharia::editor
