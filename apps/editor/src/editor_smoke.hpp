#pragma once

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <optional>

#include "editor_app.hpp"
#include "editor_input_router.hpp"
#include "editor_inspected_world.hpp"
#include "editor_shortcut_router.hpp"

namespace asharia {
    class GlfwWindow;
}

namespace asharia::editor {
    class EditorActionRegistry;
    class EditorFrameDebugger;
    class EditorViewportCoordinator;
    struct EditorActionServices;

    struct EditorSmokeRunResult {
        int renderedFrames{};
        bool resizeRequested{};
        bool resizedViewportPresented{};
        bool frameDebugCaptureRequested{};
        bool frameDebugReplayPassRequested{};
        bool frameDebugPreviewRequested{};
        bool frameDebugPreviewVisible{};
        std::optional<std::size_t> frameDebugPreviewSelectedPassIndex;
        std::optional<std::uint64_t> frameDebugPreviewSelectedExecutionEventId;
        std::optional<std::size_t> frameDebugPreviewCopiedAfterPassIndex;
        bool frameDebugResumeRequested{};
        bool frameDebugRenderedAfterResume{};
        VkExtent2D viewportExtentBeforeResize{};
        VkExtent2D viewportExtentAfterResize{};
        std::uint64_t textureFramesBeforeResize{};
        std::uint64_t viewportFramesAtFrameDebugPause{};
        std::uint64_t viewportFramesAtFrameDebugPreview{};
        std::uint64_t viewportFramesAfterFrameDebugResume{};
        std::uint64_t inspectedWorldFramesAtFrameDebugPause{};
        std::uint64_t inspectedWorldFramesAtFrameDebugPreview{};
        std::uint64_t inspectedWorldFramesAfterFrameDebugResume{};
        EditorInspectedWorldSchedulerStats inspectedWorldStats;
        EditorInputRouterStats inputStats;
        EditorShortcutRouterStats shortcutStats;
    };

    struct EditorViewportResizeSmokeState {
        bool requested{};
        bool presentedAfterResize{};
        VkExtent2D extentBeforeResize{};
        VkExtent2D extentAfterResize{};
        std::uint64_t textureFramesBeforeResize{};
    };

    struct EditorFrameDebuggerSmokeState {
        bool captureRequested{};
        bool replayPassRequested{};
        bool previewRequested{};
        bool previewVisible{};
        std::optional<std::size_t> previewSelectedPassIndex;
        std::optional<std::uint64_t> previewSelectedExecutionEventId;
        std::optional<std::size_t> previewCopiedAfterPassIndex;
        bool resumeRequested{};
        bool renderedAfterResume{};
        std::uint64_t viewportFramesAtPause{};
        std::uint64_t viewportFramesAtPreview{};
        std::uint64_t viewportFramesAfterResume{};
        std::uint64_t inspectedWorldFramesAtPause{};
        std::uint64_t inspectedWorldFramesAtPreview{};
        std::uint64_t inspectedWorldFramesAfterResume{};
    };

    [[nodiscard]] bool isEditorSmokeMode(EditorRunMode mode);
    [[nodiscard]] bool isEditorViewportSmokeMode(EditorRunMode mode);
    [[nodiscard]] bool isEditorViewportResizeSmokeMode(EditorRunMode mode);
    [[nodiscard]] bool isEditorFrameDebuggerSmokeMode(EditorRunMode mode);
    [[nodiscard]] int editorSmokeFrameCount(EditorRunMode mode);

    void requestSyntheticMultiViewSmoke(EditorRunMode mode,
                                        EditorViewportCoordinator& viewportHost);
    void updateViewportResizeSmoke(GlfwWindow& window,
                                   const EditorViewportCoordinator& viewportHost,
                                   EditorViewportResizeSmokeState& state);
    void updateFrameDebuggerSmoke(EditorFrameDebugger& frameDebugger,
                                  EditorActionRegistry& actionRegistry,
                                  EditorActionServices& actionServices,
                                  const EditorViewportCoordinator& viewportHost,
                                  const EditorInspectedWorldScheduler& inspectedWorldScheduler,
                                  EditorFrameDebuggerSmokeState& state);

} // namespace asharia::editor
