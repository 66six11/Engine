#pragma once

namespace asharia::editor {

    enum class EditorRunMode {
        Interactive,
        SmokeShell,
        SmokeAssetBrowser,
        SmokeViewport,
        SmokeViewportResize,
        SmokeFrameDebugger,
    };

    int runEditor(EditorRunMode mode);

} // namespace asharia::editor
