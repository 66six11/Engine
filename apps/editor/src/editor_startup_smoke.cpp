#include "editor_startup_smoke.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <imgui.h>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "asharia/core/log.hpp"

#include "editor_i18n.hpp"
#include "editor_smoke.hpp"
#include "editor_ui.hpp"
#include "imgui_runtime.hpp"

namespace asharia::editor {

    namespace {

        [[nodiscard]] bool validateImguiLayoutPersistenceSmoke(EditorRunMode mode,
                                                               const ImGuiRuntime& imgui) {
            if (!isEditorSmokeMode(mode)) {
                return true;
            }
            if (!imgui.layoutPersistenceEnabled()) {
                asharia::logError("Editor ImGui layout persistence is disabled.");
                return false;
            }
            return true;
        }

        [[nodiscard]] bool validateI18nSmoke(EditorRunMode mode) {
            if (!isEditorSmokeMode(mode)) {
                return true;
            }

            if (editorI18nCatalog().empty()) {
                asharia::logError("Editor i18n catalog is empty.");
                return false;
            }

            const EditorI18n enUs{EditorLocale::EnUs};
            const EditorI18n zhHans{EditorLocale::ZhHans};
            const std::string_view enFile = enUs.text("menu.file");
            const std::string_view zhFile = zhHans.text("menu.file");
            if (enFile != "File" || zhFile.empty() || zhFile == enFile) {
                asharia::logError("Editor i18n smoke failed locale text lookup.");
                return false;
            }
            if (zhHans.text(EditorI18nTextQuery{
                    .key = "missing.editor.key",
                    .fallback = "Fallback",
                }) != "Fallback") {
                asharia::logError("Editor i18n smoke failed missing-key fallback.");
                return false;
            }

            const std::string captureLabel = zhHans.label(EditorI18nLabelDesc{
                .key = "action.debug.captureFrame",
                .stableId = "debug.capture-frame",
                .fallback = "Capture Frame",
            });
            if (!captureLabel.ends_with("###debug.capture-frame")) {
                asharia::logError("Editor i18n smoke failed stable ImGui label id generation.");
                return false;
            }

            return true;
        }

        [[nodiscard]] bool validateEditorFontSmoke(EditorRunMode mode, const ImGuiRuntime& imgui,
                                                   EditorLocale locale) {
            if (!isEditorSmokeMode(mode) || locale != EditorLocale::ZhHans) {
                return true;
            }

            const ImGuiRuntimeFontStatus& status = imgui.fontStatus();
            if (!status.cjkRequested) {
                asharia::logError("Editor zh-Hans smoke did not request CJK glyph coverage.");
                return false;
            }
            if (status.cjkFontPathExplicit && !status.cjkLoaded) {
                asharia::logError(
                    "Editor zh-Hans smoke failed to load the explicit CJK font path.");
                return false;
            }
            if (status.cjkCandidateFound && !status.cjkLoaded) {
                asharia::logError(
                    "Editor zh-Hans smoke found a CJK font candidate but did not load it.");
                return false;
            }

            return true;
        }

        [[nodiscard]] bool colorMatches(ColorSrgba8 color, std::uint8_t red, std::uint8_t green,
                                        std::uint8_t blue, std::uint8_t alpha) {
            return color.r == red && color.g == green && color.b == blue && color.a == alpha;
        }

        [[nodiscard]] bool validateEditorThemeCatalogSmoke(std::span<const EditorUiTheme> themes) {
            if (themes.size() != 8U) {
                asharia::logError("Editor theme smoke found an unexpected theme count.");
                return false;
            }
            if (defaultEditorUiThemeId() != EditorUiThemeId::BlackDefault ||
                editorUiThemeName(defaultEditorUiThemeId()) != "black-default") {
                asharia::logError("Editor theme smoke found an invalid default theme.");
                return false;
            }
            for (std::size_t index = 0; index < themes.size(); ++index) {
                if (themes[index].storageName.empty() || themes[index].name.empty()) {
                    asharia::logError("Editor theme smoke found an unnamed theme.");
                    return false;
                }
                for (std::size_t otherIndex = index + 1U; otherIndex < themes.size();
                     ++otherIndex) {
                    if (themes[index].storageName == themes[otherIndex].storageName) {
                        asharia::logError(
                            "Editor theme smoke found duplicate theme storage names.");
                        return false;
                    }
                }
            }
            return true;
        }

        [[nodiscard]] bool validateEditorThemeColorSmoke() {
            const EditorUiTheme& blackTheme = editorUiTheme(EditorUiThemeId::BlackDefault);
            if (!colorMatches(blackTheme.appBackground, 0x11U, 0x12U, 0x14U, 0xFFU) ||
                !colorMatches(blackTheme.viewportBackground, 0x24U, 0x24U, 0x27U, 0xFFU) ||
                toImGuiEncodedSrgbU32(blackTheme.appBackground) !=
                    IM_COL32(0x11U, 0x12U, 0x14U, 0xFFU)) {
                asharia::logError("Editor theme smoke found an invalid black default theme byte.");
                return false;
            }

            const EditorUiTheme& classicTheme = editorUiTheme(EditorUiThemeId::ClassicBlueGray);
            if (!colorMatches(classicTheme.appBackground, 0x17U, 0x1DU, 0x24U, 0xFFU) ||
                !colorMatches(classicTheme.accent, 0x72U, 0xB7U, 0xE8U, 0xFFU) ||
                toImGuiEncodedSrgbU32(classicTheme.appBackground) !=
                    IM_COL32(0x17U, 0x1DU, 0x24U, 0xFFU)) {
                asharia::logError("Editor theme smoke found an invalid encoded sRGB theme byte.");
                return false;
            }
            return true;
        }

        [[nodiscard]] bool validateEditorThemeNameResolutionSmoke() {
            const std::optional<EditorUiThemeId> defaultTheme =
                editorUiThemeIdFromName("black-default");
            if (!defaultTheme || *defaultTheme != EditorUiThemeId::BlackDefault) {
                asharia::logError("Editor theme smoke could not resolve the default theme name.");
                return false;
            }
            const std::optional<EditorUiThemeId> defaultThemeAlias =
                editorUiThemeIdFromName("Dark Black");
            if (!defaultThemeAlias || *defaultThemeAlias != EditorUiThemeId::BlackDefault) {
                asharia::logError("Editor theme smoke could not resolve the black theme alias.");
                return false;
            }
            const std::optional<EditorUiThemeId> legacyDefaultTheme =
                editorUiThemeIdFromName("classic-blue-gray");
            if (!legacyDefaultTheme || *legacyDefaultTheme != EditorUiThemeId::ClassicBlueGray) {
                asharia::logError(
                    "Editor theme smoke could not resolve the legacy default theme name.");
                return false;
            }
            return true;
        }

        [[nodiscard]] bool validateEditorThemeSmoke(EditorRunMode mode,
                                                    EditorUiThemeId expectedTheme) {
            if (!isEditorSmokeMode(mode)) {
                return true;
            }

            if (!validateEditorThemeCatalogSmoke(editorUiThemes()) ||
                !validateEditorThemeColorSmoke() || !validateEditorThemeNameResolutionSmoke()) {
                return false;
            }
            if (currentEditorUiThemeId() != expectedTheme) {
                asharia::logError("Editor theme smoke did not apply the startup theme.");
                return false;
            }
            return true;
        }

    } // namespace

    bool validateImguiLayoutSavedSmoke(EditorRunMode mode, const ImGuiRuntime& imgui) {
        if (!isEditorSmokeMode(mode)) {
            return true;
        }
        if (!std::filesystem::exists(imgui.layoutIniPath())) {
            asharia::logError("Editor ImGui layout smoke did not write the layout ini file.");
            return false;
        }
        return true;
    }

    bool validateEditorStartupSmoke(EditorRunMode mode, const ImGuiRuntime& imgui,
                                    EditorLocale locale, EditorUiThemeId theme) {
        return validateImguiLayoutPersistenceSmoke(mode, imgui) && validateI18nSmoke(mode) &&
               validateEditorFontSmoke(mode, imgui, locale) &&
               validateEditorThemeSmoke(mode, theme);
    }
} // namespace asharia::editor
