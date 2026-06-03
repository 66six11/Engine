#pragma once

#include "editor_app.hpp"

namespace asharia::editor {
    class EditorFrameDebugger;
    struct EditorSmokeRunResult;

    [[nodiscard]] bool validateFrameDebuggerSmoke(EditorRunMode mode,
                                                  const EditorSmokeRunResult& runResult,
                                                  const EditorFrameDebugger& frameDebugger);
} // namespace asharia::editor
