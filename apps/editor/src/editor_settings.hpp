#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "asharia/core/result.hpp"

#include "editor_i18n.hpp"
#include "editor_ui.hpp"

namespace asharia::editor {

    struct EditorSettings {
        EditorLocale locale{EditorLocale::EnUs};
        EditorUiThemeId theme{EditorUiThemeId::BlackDefault};
    };

    [[nodiscard]] std::filesystem::path editorUserSettingsPath();
    [[nodiscard]] std::filesystem::path editorSmokeSettingsPath();
    [[nodiscard]] asharia::Result<EditorSettings>
    loadEditorSettings(const std::filesystem::path& path, EditorLocale fallbackLocale);
    [[nodiscard]] asharia::VoidResult saveEditorSettings(const std::filesystem::path& path,
                                                         const EditorSettings& settings);

    class EditorSettingsController {
    public:
        EditorSettingsController(EditorSettings settings, std::filesystem::path settingsPath,
                                 EditorI18n& i18n);

        [[nodiscard]] const EditorSettings& settings() const;
        [[nodiscard]] const std::filesystem::path& settingsPath() const;
        [[nodiscard]] std::string_view lastSaveError() const;
        [[nodiscard]] bool lastSaveAttempted() const;
        [[nodiscard]] bool lastSaveFailed() const;
        [[nodiscard]] asharia::VoidResult setLocale(EditorLocale locale);
        [[nodiscard]] asharia::VoidResult setTheme(EditorUiThemeId theme);

    private:
        EditorSettings settings_;
        std::filesystem::path settingsPath_;
        EditorI18n& i18n_;
        std::string lastSaveError_;
        bool lastSaveAttempted_{false};
        bool lastSaveFailed_{false};
    };

} // namespace asharia::editor
