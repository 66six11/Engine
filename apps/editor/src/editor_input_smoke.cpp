#include "editor_input_smoke.hpp"

#include <cstdint>

#include "asharia/core/log.hpp"

#include "editor_smoke.hpp"

namespace asharia::editor {
    bool validateInputRouterSmoke(EditorRunMode mode, const EditorSmokeRunResult& runResult) {
        if (!isEditorSmokeMode(mode)) {
            return true;
        }
        if (runResult.inputStats.capturedFrames <
            static_cast<std::uint64_t>(runResult.renderedFrames)) {
            asharia::logError("Editor input router smoke did not capture every rendered frame.");
            return false;
        }
        if (runResult.inputStats.sceneViewReports == 0) {
            asharia::logError("Editor input router smoke did not receive Scene View input state.");
            return false;
        }
        return true;
    }
} // namespace asharia::editor
