#pragma once

#include "editor_app.hpp"

namespace asharia::editor {
    struct EditorSmokeRunResult;

    [[nodiscard]] bool validateInputRouterSmoke(EditorRunMode mode,
                                                const EditorSmokeRunResult& runResult);
} // namespace asharia::editor
