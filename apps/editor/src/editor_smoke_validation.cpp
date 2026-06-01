#include "editor_smoke_validation.hpp"

#include "editor_command_smoke.hpp"
#include "editor_registration_smoke.hpp"
#include "editor_startup_smoke.hpp"

namespace asharia::editor {
    [[nodiscard]] bool validateEditorStartupGates(EditorRunMode mode, const ImGuiRuntime& imgui,
                                                  EditorLocale locale, EditorUiThemeId theme,
                                                  EditorActionRegistry& actionRegistry,
                                                  EditorActionServices& actionServices,
                                                  EditorSettingsController& settings,
                                                  EditorI18n& i18n,
                                                  const EditorToolRegistry& toolRegistry) {
        return validateEditorStartupSmoke(mode, imgui, locale, theme) &&
               validateEditorRegistrationSmoke(mode, actionRegistry, actionServices, settings, i18n,
                                               toolRegistry) &&
               validateEditorCommandSmoke(mode);
    }
} // namespace asharia::editor
