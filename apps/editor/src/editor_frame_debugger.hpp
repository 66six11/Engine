#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "asharia/renderer_basic_vulkan/basic_renderers.hpp"

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
        std::uint64_t frameDebugRenderGraphViewFrames{};
        std::uint64_t frameDebugRenderGraphSnapshotFrames{};
        std::uint64_t previewSelections{};
        std::uint64_t previewRequests{};
        std::uint64_t previewFramesRecorded{};
        std::uint64_t previewUnavailableFrames{};
        std::uint64_t previewTextureFramesPublished{};
        std::uint64_t previewTextureFramesDrawn{};
        std::uint64_t replayPassSelections{};
        std::uint64_t replayPassRequests{};
        std::uint64_t replayPassUnavailableRequests{};
        std::uint64_t replayEventSelections{};
        std::uint64_t replayEventRequests{};
    };

    enum class EditorFrameDebugPreviewStatus {
        NotRequested,
        Pending,
        Available,
        Unavailable,
    };

    struct EditorFrameDebugPreview {
        EditorFrameDebugPreviewStatus status{EditorFrameDebugPreviewStatus::NotRequested};
        std::optional<std::size_t> selectedPassIndex;
        std::optional<asharia::BasicRenderViewExecutionEventId> selectedExecutionEventId;
        std::optional<std::uint32_t> selectedImageResourceIndex;
        EditorViewportTexture texture;
        std::string message;
        bool dirty{};
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
        void notifyFrameDebugRenderGraphViewDrawn(bool snapshotVisible);
        void notifyFrameDebugPreviewDrawn(bool textureVisible);
        [[nodiscard]] bool selectReplayPass(std::size_t passIndex);
        [[nodiscard]] bool selectReplayEvent(asharia::BasicRenderViewExecutionEventId eventId);
        [[nodiscard]] bool selectPreviewImageResource(std::uint32_t resourceIndex);
        [[nodiscard]] std::optional<std::uint32_t> consumePreviewRequest();
        void publishPreviewTexture(std::uint32_t resourceIndex, EditorViewportTexture texture);
        void markPreviewUnavailable(std::uint32_t resourceIndex, std::string message);

        [[nodiscard]] EditorViewportRepaintReasons consumeRenderViewRepaintReasons();
        [[nodiscard]] bool shouldRecordRenderViews() const;
        [[nodiscard]] bool shouldRunInspectedWorldSafePoints() const;
        [[nodiscard]] bool isCapturingFrame() const;
        [[nodiscard]] EditorFrameDebuggerState state() const;
        [[nodiscard]] std::string_view stateName() const;
        [[nodiscard]] const std::optional<EditorFrameDebugCapture>& pausedCapture() const;
        [[nodiscard]] const std::optional<EditorFrameDebugCapture>& latestCapture() const;
        [[nodiscard]] const EditorFrameDebugPreview& preview() const;
        [[nodiscard]] EditorFrameDebuggerStats stats() const;

    private:
        void transitionTo(EditorFrameDebuggerState state);

        EditorFrameDebuggerState state_{EditorFrameDebuggerState::Running};
        std::optional<EditorFrameDebugCapture> pausedCapture_;
        std::optional<EditorFrameDebugCapture> latestCapture_;
        EditorFrameDebugPreview preview_;
        EditorViewportRepaintReasons pendingRenderViewRepaintReasons_{};
        EditorFrameDebuggerStats stats_;
    };

    [[nodiscard]] std::string_view editorFrameDebuggerStateName(EditorFrameDebuggerState state);
    [[nodiscard]] std::string_view
    editorFrameDebugPreviewStatusName(EditorFrameDebugPreviewStatus status);

} // namespace asharia::editor
