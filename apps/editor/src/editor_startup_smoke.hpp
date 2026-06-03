#pragma once

#include "editor_app.hpp"
#include "editor_i18n.hpp"
#include "editor_ui.hpp"

namespace asharia::editor {
    class ImGuiRuntime;

    [[nodiscard]] bool validateEditorStartupSmoke(EditorRunMode mode, const ImGuiRuntime& imgui,
                                                  EditorLocale locale, EditorUiThemeId theme);
    [[nodiscard]] bool validateImguiLayoutSavedSmoke(EditorRunMode mode, const ImGuiRuntime& imgui);

} // namespace asharia::editor
