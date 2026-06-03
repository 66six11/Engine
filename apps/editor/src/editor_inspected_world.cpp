#include "editor_inspected_world.hpp"

namespace asharia::editor {

    void EditorInspectedWorldScheduler::runFrameSafePoints(bool allowInspectedWorldUpdate) {
        if (!allowInspectedWorldUpdate) {
            ++stats_.skippedFrameAdvanceSafePoints;
            ++stats_.skippedGameUpdateSafePoints;
            ++stats_.skippedScriptUpdateSafePoints;
            return;
        }

        ++stats_.frameAdvanceSafePoints;
        ++stats_.gameUpdateSafePoints;
        ++stats_.scriptUpdateSafePoints;
    }

    EditorInspectedWorldSchedulerStats EditorInspectedWorldScheduler::stats() const {
        return stats_;
    }

} // namespace asharia::editor
