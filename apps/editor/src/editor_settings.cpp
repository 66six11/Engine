#include "editor_settings.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
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

#include "editor_viewport_overlay_provider.hpp"

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

        [[nodiscard]] std::string editorSmokeRunDirectoryName() {
            static const std::string kRunId =
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
            return kRunId;
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

        [[nodiscard]] std::optional<EditorUiThemeId>
        readTheme(const asharia::archive::ArchiveValue& root) {
            const asharia::archive::ArchiveValue* themeValue = nullptr;
            const asharia::archive::ArchiveValue* uiValue = root.findMemberValue("ui");
            if (uiValue != nullptr && uiValue->kind == asharia::archive::ArchiveValueKind::Object) {
                themeValue = uiValue->findMemberValue("theme");
            }
            if (themeValue == nullptr) {
                themeValue = root.findMemberValue("theme");
            }
            if (themeValue == nullptr ||
                themeValue->kind != asharia::archive::ArchiveValueKind::String) {
                return std::nullopt;
            }

            return editorUiThemeIdFromName(themeValue->stringValue);
        }

        [[nodiscard]] std::optional<float>
        archiveFiniteFloat(const asharia::archive::ArchiveValue& value) {
            double number = 0.0;
            if (value.kind == asharia::archive::ArchiveValueKind::Float) {
                number = value.floatValue;
            } else if (value.kind == asharia::archive::ArchiveValueKind::Integer) {
                number = static_cast<double>(value.integerValue);
            } else {
                return std::nullopt;
            }
            if (!std::isfinite(number)) {
                return std::nullopt;
            }
            return static_cast<float>(number);
        }

        void readFiniteFloatMember(const asharia::archive::ArchiveValue& object,
                                   std::string_view key, float& output) {
            const asharia::archive::ArchiveValue* value = object.findMemberValue(key);
            if (value == nullptr) {
                return;
            }
            if (const std::optional<float> number = archiveFiniteFloat(*value); number) {
                output = *number;
            }
        }

        void readPositiveFloatMember(const asharia::archive::ArchiveValue& object,
                                     std::string_view key, float& output) {
            const asharia::archive::ArchiveValue* value = object.findMemberValue(key);
            if (value == nullptr) {
                return;
            }
            const std::optional<float> number = archiveFiniteFloat(*value);
            if (number && *number > 0.0F) {
                output = *number;
            }
        }

        void readNonNegativeFloatMember(const asharia::archive::ArchiveValue& object,
                                        std::string_view key, float& output) {
            const asharia::archive::ArchiveValue* value = object.findMemberValue(key);
            if (value == nullptr) {
                return;
            }
            const std::optional<float> number = archiveFiniteFloat(*value);
            if (number && *number >= 0.0F) {
                output = *number;
            }
        }

        void readColorFloat4Member(const asharia::archive::ArchiveValue& object,
                                   std::string_view key, std::array<float, 4>& output) {
            const asharia::archive::ArchiveValue* value = object.findMemberValue(key);
            if (value == nullptr || value->kind != asharia::archive::ArchiveValueKind::Array) {
                return;
            }
            const std::size_t componentCount =
                std::min<std::size_t>(output.size(), value->arrayValue.size());
            for (std::size_t index = 0; index < componentCount; ++index) {
                const std::optional<float> component = archiveFiniteFloat(value->arrayValue[index]);
                if (component) {
                    output.at(index) = std::clamp(*component, 0.0F, 1.0F);
                }
            }
        }

        [[nodiscard]] EditorViewportWorldGridSettings
        normalizeSceneGridSettings(EditorViewportWorldGridSettings settings) {
            constexpr float kMinimumSpacing = 0.0001F;
            settings.minorSpacing = std::max(settings.minorSpacing, kMinimumSpacing);
            settings.majorSpacing = std::max(settings.majorSpacing, settings.minorSpacing);
            settings.fadeStart = std::max(settings.fadeStart, 0.0F);
            settings.fadeEnd = std::max(settings.fadeEnd, 0.0F);
            settings.opacity = std::clamp(settings.opacity, 0.0F, 1.0F);
            for (float& component : settings.color) {
                component = std::clamp(component, 0.0F, 1.0F);
            }
            return settings;
        }

        [[nodiscard]] EditorViewportWorldGridSettings
        readSceneGridSettings(const asharia::archive::ArchiveValue& root,
                              EditorViewportWorldGridSettings defaults) {
            const asharia::archive::ArchiveValue* viewportValue = root.findMemberValue("viewport");
            const asharia::archive::ArchiveValue* sceneGridValue =
                viewportValue == nullptr ? nullptr : viewportValue->findMemberValue("sceneGrid");
            if (sceneGridValue == nullptr ||
                sceneGridValue->kind != asharia::archive::ArchiveValueKind::Object) {
                return defaults;
            }

            EditorViewportWorldGridSettings settings = defaults;
            readFiniteFloatMember(*sceneGridValue, "planeY", settings.planeY);
            readPositiveFloatMember(*sceneGridValue, "minorSpacing", settings.minorSpacing);
            readPositiveFloatMember(*sceneGridValue, "majorSpacing", settings.majorSpacing);
            readNonNegativeFloatMember(*sceneGridValue, "fadeStart", settings.fadeStart);
            readNonNegativeFloatMember(*sceneGridValue, "fadeEnd", settings.fadeEnd);
            readNonNegativeFloatMember(*sceneGridValue, "opacity", settings.opacity);
            readColorFloat4Member(*sceneGridValue, "color", settings.color);
            return normalizeSceneGridSettings(settings);
        }

        [[nodiscard]] asharia::archive::ArchiveValue
        sceneGridSettingsArchive(EditorViewportWorldGridSettings settings) {
            settings = normalizeSceneGridSettings(settings);
            return asharia::archive::ArchiveValue::object({
                asharia::archive::ArchiveMember{
                    .key = "planeY",
                    .value = asharia::archive::ArchiveValue::floating(settings.planeY),
                },
                asharia::archive::ArchiveMember{
                    .key = "minorSpacing",
                    .value = asharia::archive::ArchiveValue::floating(settings.minorSpacing),
                },
                asharia::archive::ArchiveMember{
                    .key = "majorSpacing",
                    .value = asharia::archive::ArchiveValue::floating(settings.majorSpacing),
                },
                asharia::archive::ArchiveMember{
                    .key = "fadeStart",
                    .value = asharia::archive::ArchiveValue::floating(settings.fadeStart),
                },
                asharia::archive::ArchiveMember{
                    .key = "fadeEnd",
                    .value = asharia::archive::ArchiveValue::floating(settings.fadeEnd),
                },
                asharia::archive::ArchiveMember{
                    .key = "opacity",
                    .value = asharia::archive::ArchiveValue::floating(settings.opacity),
                },
                asharia::archive::ArchiveMember{
                    .key = "color",
                    .value = asharia::archive::ArchiveValue::array({
                        asharia::archive::ArchiveValue::floating(settings.color[0]),
                        asharia::archive::ArchiveValue::floating(settings.color[1]),
                        asharia::archive::ArchiveValue::floating(settings.color[2]),
                        asharia::archive::ArchiveValue::floating(settings.color[3]),
                    }),
                },
            });
        }

    } // namespace

    std::filesystem::path editorUserSettingsPath() {
        return editorAppStateDirectory() / "settings.json";
    }

    std::filesystem::path editorSmokeSettingsPath() {
        return fallbackWritableDirectory() / "Asharia" / "EditorSmoke" /
               editorSmokeRunDirectoryName() / "settings.json";
    }

    asharia::Result<EditorSettings> loadEditorSettings(const std::filesystem::path& path,
                                                       EditorLocale fallbackLocale) {
        EditorSettings settings{
            .locale = fallbackLocale,
            .theme = defaultEditorUiThemeId(),
            .sceneGrid = defaultEditorSceneGridSettings(),
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
        const std::optional<EditorUiThemeId> theme = readTheme(*archive);
        if (theme) {
            settings.theme = *theme;
        }
        settings.sceneGrid = readSceneGridSettings(*archive, settings.sceneGrid);
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
                .key = "version",
                .value = asharia::archive::ArchiveValue::integer(1),
            },
            asharia::archive::ArchiveMember{
                .key = "locale",
                .value = asharia::archive::ArchiveValue::string(
                    std::string{editorLocaleName(settings.locale)}),
            },
            asharia::archive::ArchiveMember{
                .key = "ui",
                .value = asharia::archive::ArchiveValue::object({
                    asharia::archive::ArchiveMember{
                        .key = "theme",
                        .value = asharia::archive::ArchiveValue::string(
                            std::string{editorUiThemeName(settings.theme)}),
                    },
                }),
            },
            asharia::archive::ArchiveMember{
                .key = "viewport",
                .value = asharia::archive::ArchiveValue::object({
                    asharia::archive::ArchiveMember{
                        .key = "sceneGrid",
                        .value = sceneGridSettingsArchive(settings.sceneGrid),
                    },
                }),
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

    asharia::VoidResult EditorSettingsController::setTheme(EditorUiThemeId theme) {
        settings_.theme = theme;
        applyEditorUiTheme(theme);

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

    asharia::VoidResult
    EditorSettingsController::setSceneGrid(EditorViewportWorldGridSettings sceneGrid) {
        settings_.sceneGrid = normalizeSceneGridSettings(sceneGrid);

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
