#pragma once

#include "editor_app.hpp"

namespace asharia::editor {
    class EditorActionRegistry;
    class EditorI18n;
    class EditorSettingsController;
    class EditorToolRegistry;
    struct EditorActionServices;

    [[nodiscard]] bool validateEditorRegistrationSmoke(EditorRunMode mode,
                                                       EditorActionRegistry& actionRegistry,
                                                       EditorActionServices& actionServices,
                                                       EditorSettingsController& settings,
                                                       EditorI18n& i18n,
                                                       const EditorToolRegistry& toolRegistry);

} // namespace asharia::editor
