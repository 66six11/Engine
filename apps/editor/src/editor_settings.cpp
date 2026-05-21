#include "editor_settings.hpp"

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "asharia/archive/archive_value.hpp"
#include "asharia/archive/json_archive.hpp"
#include "asharia/core/error.hpp"

namespace asharia::editor {

    namespace {

        [[nodiscard]] asharia::Error editorSettingsError(std::string message) {
            return asharia::Error{asharia::ErrorDomain::Core, 0, std::move(message)};
        }

        [[nodiscard]] std::string environmentValue(std::string_view name) {
#if defined(_WIN32)
            const std::string nameText{name};
            char* value = nullptr;
            std::size_t valueSize = 0;
            if (_dupenv_s(&value, &valueSize, nameText.c_str()) != 0 || value == nullptr) {
                return {};
            }
            const std::unique_ptr<char, decltype(&std::free)> ownedValue{value, &std::free};
            return std::string{ownedValue.get()};
#else
            const std::string nameText{name};
            const char* value = std::getenv(nameText.c_str());
            return value == nullptr ? std::string{} : std::string{value};
#endif
        }

        [[nodiscard]] std::filesystem::path fallbackWritableDirectory() {
            std::error_code error;
            std::filesystem::path basePath = std::filesystem::temp_directory_path(error);
            if (error) {
                basePath = std::filesystem::current_path(error);
            }
            if (basePath.empty()) {
                basePath = ".";
            }
            return basePath;
        }

        [[nodiscard]] std::filesystem::path editorAppStateDirectory() {
            const std::string localAppData = environmentValue("LOCALAPPDATA");
            if (!localAppData.empty()) {
                return std::filesystem::path{localAppData} / "Asharia" / "Editor";
            }
            return fallbackWritableDirectory() / "Asharia" / "Editor";
        }

        [[nodiscard]] std::optional<EditorLocale>
        readLocale(const asharia::archive::ArchiveValue& root) {
            const asharia::archive::ArchiveValue* localeValue = root.findMemberValue("locale");
            if (localeValue == nullptr) {
                return std::nullopt;
            }
            if (localeValue->kind != asharia::archive::ArchiveValueKind::String) {
                return std::nullopt;
            }

            const std::optional<EditorLocale> locale =
                editorLocaleFromName(localeValue->stringValue);
            if (!locale) {
                return std::nullopt;
            }
            return locale;
        }

    } // namespace

    std::filesystem::path editorUserSettingsPath() {
        return editorAppStateDirectory() / "settings.json";
    }

    std::filesystem::path editorSmokeSettingsPath() {
        return fallbackWritableDirectory() / "Asharia" / "EditorSmoke" / "settings.json";
    }

    asharia::Result<EditorSettings> loadEditorSettings(const std::filesystem::path& path,
                                                       EditorLocale fallbackLocale) {
        EditorSettings settings{
            .locale = fallbackLocale,
        };
        if (path.empty()) {
            return settings;
        }

        std::error_code existsError;
        if (!std::filesystem::exists(path, existsError)) {
            if (existsError) {
                return std::unexpected{
                    editorSettingsError("Failed to check editor settings file '" + path.string() +
                                        "': " + existsError.message())};
            }
            return settings;
        }

        auto archive = asharia::archive::readJsonArchiveFile(path);
        if (!archive) {
            return std::unexpected{editorSettingsError("Failed to read editor settings '" +
                                                       path.string() +
                                                       "': " + archive.error().message)};
        }
        if (archive->kind != asharia::archive::ArchiveValueKind::Object) {
            return std::unexpected{
                editorSettingsError("Editor settings root must be an object: " + path.string())};
        }

        const std::optional<EditorLocale> locale = readLocale(*archive);
        if (locale) {
            settings.locale = *locale;
        }
        return settings;
    }

    asharia::VoidResult saveEditorSettings(const std::filesystem::path& path,
                                           const EditorSettings& settings) {
        if (path.empty()) {
            return std::unexpected{editorSettingsError("Editor settings path must not be empty.")};
        }

        const std::filesystem::path directory = path.parent_path();
        if (!directory.empty()) {
            std::error_code directoryError;
            std::filesystem::create_directories(directory, directoryError);
            if (directoryError) {
                return std::unexpected{
                    editorSettingsError("Failed to create editor settings directory '" +
                                        directory.string() + "': " + directoryError.message())};
            }
        }

        const asharia::archive::ArchiveValue archive = asharia::archive::ArchiveValue::object({
            asharia::archive::ArchiveMember{
                .key = "locale",
                .value = asharia::archive::ArchiveValue::string(
                    std::string{editorLocaleName(settings.locale)}),
            },
        });
        auto written = asharia::archive::writeJsonArchiveFile(path, archive);
        if (!written) {
            return std::unexpected{editorSettingsError("Failed to save editor settings '" +
                                                       path.string() +
                                                       "': " + written.error().message)};
        }
        return {};
    }

    EditorSettingsController::EditorSettingsController(EditorSettings settings,
                                                       std::filesystem::path settingsPath,
                                                       EditorI18n& i18n)
        : settings_(settings), settingsPath_(std::move(settingsPath)), i18n_(i18n) {
        i18n_.setLocale(settings_.locale);
    }

    const EditorSettings& EditorSettingsController::settings() const {
        return settings_;
    }

    const std::filesystem::path& EditorSettingsController::settingsPath() const {
        return settingsPath_;
    }

    std::string_view EditorSettingsController::lastSaveError() const {
        return lastSaveError_;
    }

    bool EditorSettingsController::lastSaveAttempted() const {
        return lastSaveAttempted_;
    }

    bool EditorSettingsController::lastSaveFailed() const {
        return lastSaveFailed_;
    }

    asharia::VoidResult EditorSettingsController::setLocale(EditorLocale locale) {
        settings_.locale = locale;
        i18n_.setLocale(locale);

        auto saved = saveEditorSettings(settingsPath_, settings_);
        lastSaveAttempted_ = true;
        if (!saved) {
            lastSaveFailed_ = true;
            lastSaveError_ = saved.error().message;
            return std::unexpected{std::move(saved.error())};
        }

        lastSaveFailed_ = false;
        lastSaveError_.clear();
        return {};
    }

} // namespace asharia::editor
