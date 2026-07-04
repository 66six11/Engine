#pragma once

#include <string>

#include "asharia/core/result.hpp"

namespace asharia::editor {
    class EditorFrameDebugger;

    [[nodiscard]] Result<std::string>
    writeFrameDebuggerSnapshotJson(const EditorFrameDebugger& frameDebugger);

    [[nodiscard]] Result<std::string>
    writeStudioFrameDebuggerSnapshotJson(const EditorFrameDebugger& frameDebugger);
} // namespace asharia::editor
