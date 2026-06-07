#include "editor_app_config.hpp"

#include <cstddef>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>

#include "asharia/core/log.hpp"

#include "editor_viewport_overlay_provider.hpp"

namespace asharia::editor {
    namespace {

        [[nodiscard]] std::filesystem::path editorSmokeLayoutIniPath() {
            return editorSmokeSettingsPath().parent_path() / "imgui-layout.ini";
        }

        [[nodiscard]] std::filesystem::path editorSettingsPathForRun(bool smokeMode) {
            return smokeMode ? editorSmokeSettingsPath() : editorUserSettingsPath();
        }

        [[nodiscard]] std::string editorLocaleEnvironmentValue() {
#if defined(_WIN32)
            char* value = nullptr;
            std::size_t valueSize = 0;
            if (_dupenv_s(&value, &valueSize, "ASHARIA_EDITOR_LOCALE") != 0 || value == nullptr) {
                return {};
            }
            const std::unique_ptr<char, decltype(&std::free)> ownedValue{value, &std::free};
            return std::string{ownedValue.get()};
#else
            const char* value = std::getenv("ASHARIA_EDITOR_LOCALE");
            return value == nullptr ? std::string{} : std::string{value};
#endif
        }

    } // namespace

    [[nodiscard]] std::filesystem::path editorI18nDirectory() {
#if defined(ASHARIA_EDITOR_I18N_DIR)
        return std::filesystem::path{ASHARIA_EDITOR_I18N_DIR};
#else
        return std::filesystem::path{"resources/i18n"};
#endif
    }

    [[nodiscard]] std::filesystem::path editorLayoutIniPathForRun(bool smokeMode) {
        if (smokeMode) {
            return editorSmokeLayoutIniPath();
        }
        return {};
    }

    [[nodiscard]] EditorSettingsRunState loadEditorSettingsForRun(bool smokeMode,
                                                                  EditorLocale fallbackLocale) {
        EditorSettingsRunState state{
            .settings =
                EditorSettings{
                    .locale = fallbackLocale,
                    .theme = defaultEditorUiThemeId(),
                    .sceneGrid = defaultEditorSceneGridSettings(),
                },
            .path = editorSettingsPathForRun(smokeMode),
        };
        if (smokeMode) {
            std::error_code removeError;
            std::filesystem::remove(state.path, removeError);
        }

        auto loaded = loadEditorSettings(state.path, fallbackLocale);
        if (loaded) {
            state.settings = *loaded;
        } else {
            asharia::logError(loaded.error().message);
        }
        return state;
    }

    [[nodiscard]] EditorLocale editorLocaleFromEnvironment() {
        const std::string value = editorLocaleEnvironmentValue();
        if (value.empty()) {
            return EditorLocale::EnUs;
        }
        const std::optional<EditorLocale> locale = editorLocaleFromName(value);
        if (locale) {
            return *locale;
        }

        asharia::logError("Unsupported ASHARIA_EDITOR_LOCALE value: " + value);
        return EditorLocale::EnUs;
    }

} // namespace asharia::editor
