#pragma once

#include <cstdint>

namespace asharia::editor {

    struct EditorInspectedWorldSchedulerStats {
        std::uint64_t frameAdvanceSafePoints{};
        std::uint64_t gameUpdateSafePoints{};
        std::uint64_t scriptUpdateSafePoints{};
        std::uint64_t skippedFrameAdvanceSafePoints{};
        std::uint64_t skippedGameUpdateSafePoints{};
        std::uint64_t skippedScriptUpdateSafePoints{};
    };

    class EditorInspectedWorldScheduler {
    public:
        void runFrameSafePoints(bool allowInspectedWorldUpdate);

        [[nodiscard]] EditorInspectedWorldSchedulerStats stats() const;

    private:
        EditorInspectedWorldSchedulerStats stats_;
    };

} // namespace asharia::editor
