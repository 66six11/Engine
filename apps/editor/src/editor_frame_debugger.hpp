#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "asharia/renderer_basic_vulkan/basic_triangle_renderer.hpp"

#include "editor_viewport.hpp"

namespace asharia::editor {

    enum class EditorFrameDebuggerState {
        Running,
        CaptureRequested,
        CapturingFrame,
        WaitingGpuFence,
        PausedFrameDebug,
        Resume,
    };

    struct EditorFrameDebuggerStats {
        std::uint64_t captureRequests{};
        std::uint64_t ignoredCaptureRequests{};
        std::uint64_t framesCaptured{};
        std::uint64_t completedCaptures{};
        std::uint64_t resumeRequests{};
        std::uint64_t ignoredResumeRequests{};
        std::uint64_t framesResumed{};
        std::uint64_t renderViewFramesSkipped{};
        std::uint64_t renderGraphPanelFrames{};
        std::uint64_t renderGraphPanelSnapshotFrames{};
    };

    struct EditorFrameDebugCapture {
        int frameIndex{};
        std::uint64_t submittedFrameEpoch{};
        EditorViewportKind viewKind{EditorViewportKind::Scene};
        EditorExtent2D requestedExtent;
        asharia::BasicRenderViewDiagnostics diagnostics;
    };

    struct EditorFrameDebugCaptureDesc {
        int frameIndex{};
        std::uint64_t submittedFrameEpoch{};
        EditorViewportKind viewKind{EditorViewportKind::Scene};
        EditorExtent2D requestedExtent;
        asharia::BasicRenderViewDiagnostics diagnostics;
    };

    class EditorFrameDebugger {
    public:
        [[nodiscard]] bool requestCapture();
        [[nodiscard]] bool requestResume();

        void beginFrame(int frameIndex);
        void captureRecordedView(EditorFrameDebugCaptureDesc desc);
        void endSubmittedFrame(std::uint64_t completedFrameEpoch);
        void notifyRenderViewSkipped();
        void notifyRenderGraphPanelDrawn(bool snapshotVisible);

        [[nodiscard]] bool shouldRecordRenderViews() const;
        [[nodiscard]] bool isCapturingFrame() const;
        [[nodiscard]] EditorFrameDebuggerState state() const;
        [[nodiscard]] std::string_view stateName() const;
        [[nodiscard]] const std::optional<EditorFrameDebugCapture>& pausedCapture() const;
        [[nodiscard]] const std::optional<EditorFrameDebugCapture>& latestCapture() const;
        [[nodiscard]] EditorFrameDebuggerStats stats() const;

    private:
        void transitionTo(EditorFrameDebuggerState state);

        EditorFrameDebuggerState state_{EditorFrameDebuggerState::Running};
        std::optional<EditorFrameDebugCapture> pausedCapture_;
        std::optional<EditorFrameDebugCapture> latestCapture_;
        EditorFrameDebuggerStats stats_;
    };

    [[nodiscard]] std::string_view editorFrameDebuggerStateName(EditorFrameDebuggerState state);

} // namespace asharia::editor
