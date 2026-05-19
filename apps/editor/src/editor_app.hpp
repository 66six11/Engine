#pragma once

namespace asharia::editor {

    enum class EditorRunMode {
        Interactive,
        SmokeShell,
        SmokeViewport,
        SmokeViewportResize,
    };

    int runEditor(EditorRunMode mode);

} // namespace asharia::editor
