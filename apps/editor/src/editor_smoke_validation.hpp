#pragma once

#include "editor_app.hpp"
#include "editor_i18n.hpp"
#include "editor_ui.hpp"

namespace asharia::editor {
    class EditorActionRegistry;
    class EditorSettingsController;
    class EditorToolRegistry;
    class ImGuiRuntime;
    struct EditorActionServices;

    [[nodiscard]] bool validateEditorStartupGates(EditorRunMode mode, const ImGuiRuntime& imgui,
                                                  EditorLocale locale, EditorUiThemeId theme,
                                                  EditorActionRegistry& actionRegistry,
                                                  EditorActionServices& actionServices,
                                                  EditorSettingsController& settings,
                                                  EditorI18n& i18n,
                                                  const EditorToolRegistry& toolRegistry);

} // namespace asharia::editor
