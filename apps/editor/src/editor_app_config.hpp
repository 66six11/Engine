#pragma once

#include <filesystem>

#include "editor_i18n.hpp"
#include "editor_settings.hpp"

namespace asharia::editor {

    struct EditorSettingsRunState {
        EditorSettings settings;
        std::filesystem::path path;
    };

    [[nodiscard]] std::filesystem::path editorI18nDirectory();
    [[nodiscard]] std::filesystem::path editorLayoutIniPathForRun(bool smokeMode);
    [[nodiscard]] EditorSettingsRunState loadEditorSettingsForRun(bool smokeMode,
                                                                  EditorLocale fallbackLocale);
    [[nodiscard]] EditorLocale editorLocaleFromEnvironment();

} // namespace asharia::editor
