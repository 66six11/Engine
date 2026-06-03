#pragma once

#include "editor_app.hpp"

namespace asharia::editor {
    class EditorActionRegistry;
    struct EditorActionServices;
    struct EditorSmokeRunResult;

    [[nodiscard]] bool validateShortcutRouterSmoke(EditorActionRegistry& actionRegistry,
                                                   EditorActionServices& actionServices);
    [[nodiscard]] bool validateShortcutRouterRunSmoke(EditorRunMode mode,
                                                      const EditorSmokeRunResult& runResult);
} // namespace asharia::editor
