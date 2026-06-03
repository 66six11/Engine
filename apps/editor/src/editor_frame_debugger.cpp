#include "editor_frame_debugger.hpp"

#include <utility>

#include "editor_frame_debugger_replay.hpp"

namespace asharia::editor {
    namespace {

        void resetPreview(EditorFrameDebugPreview& preview) {
            preview = EditorFrameDebugPreview{};
        }

    } // namespace

    std::string_view editorFrameDebuggerStateName(EditorFrameDebuggerState state) {
        switch (state) {
        case EditorFrameDebuggerState::Running:
            return "Running";
        case EditorFrameDebuggerState::CaptureRequested:
            return "CaptureRequested";
        case EditorFrameDebuggerState::CapturingFrame:
            return "CapturingFrame";
        case EditorFrameDebuggerState::WaitingGpuFence:
            return "WaitingGpuFence";
        case EditorFrameDebuggerState::PausedFrameDebug:
            return "PausedFrameDebug";
        case EditorFrameDebuggerState::Resume:
            return "Resume";
        }
        return "Unknown";
    }

    std::string_view editorFrameDebugPreviewStatusName(EditorFrameDebugPreviewStatus status) {
        switch (status) {
        case EditorFrameDebugPreviewStatus::NotRequested:
            return "NotRequested";
        case EditorFrameDebugPreviewStatus::Pending:
            return "Pending";
        case EditorFrameDebugPreviewStatus::Available:
            return "Available";
        case EditorFrameDebugPreviewStatus::Unavailable:
            return "Unavailable";
        }
        return "Unknown";
    }

    bool EditorFrameDebugger::requestCapture() {
        if (state_ != EditorFrameDebuggerState::Running) {
            ++stats_.ignoredCaptureRequests;
            return false;
        }

        pausedCapture_.reset();
        latestCapture_.reset();
        resetPreview(preview_);
        ++stats_.captureRequests;
        transitionTo(EditorFrameDebuggerState::CaptureRequested);
        return true;
    }

    bool EditorFrameDebugger::requestResume() {
        if (state_ != EditorFrameDebuggerState::PausedFrameDebug) {
            ++stats_.ignoredResumeRequests;
            return false;
        }

        ++stats_.resumeRequests;
        transitionTo(EditorFrameDebuggerState::Resume);
        return true;
    }

    void EditorFrameDebugger::beginFrame(int frameIndex) {
        static_cast<void>(frameIndex);

        if (state_ == EditorFrameDebuggerState::CaptureRequested) {
            transitionTo(EditorFrameDebuggerState::CapturingFrame);
            return;
        }

        if (state_ == EditorFrameDebuggerState::Resume) {
            pausedCapture_.reset();
            resetPreview(preview_);
            ++stats_.framesResumed;
            addEditorViewportRepaintReason(pendingRenderViewRepaintReasons_,
                                           EditorViewportRepaintReason::FrameDebugEventChanged);
            transitionTo(EditorFrameDebuggerState::Running);
        }
    }

    void EditorFrameDebugger::captureRecordedView(EditorFrameDebugCaptureDesc desc) {
        if (state_ != EditorFrameDebuggerState::CapturingFrame) {
            return;
        }

        latestCapture_ = EditorFrameDebugCapture{
            .frameIndex = desc.frameIndex,
            .submittedFrameEpoch = desc.submittedFrameEpoch,
            .viewKind = desc.viewKind,
            .requestedExtent = desc.requestedExtent,
            .diagnostics = std::move(desc.diagnostics),
        };
        if (const auto defaultPass = frame_debugger_replay::defaultReplayPassIndex(
                latestCapture_->diagnostics.renderGraph)) {
            static_cast<void>(selectReplayPass(*defaultPass));
        }
        pausedCapture_ = latestCapture_;
        ++stats_.framesCaptured;
        transitionTo(EditorFrameDebuggerState::WaitingGpuFence);
    }

    void EditorFrameDebugger::endSubmittedFrame(std::uint64_t completedFrameEpoch) {
        if (state_ != EditorFrameDebuggerState::WaitingGpuFence || !pausedCapture_) {
            return;
        }

        if (completedFrameEpoch >= pausedCapture_->submittedFrameEpoch) {
            ++stats_.completedCaptures;
            transitionTo(EditorFrameDebuggerState::PausedFrameDebug);
        }
    }

    void EditorFrameDebugger::notifyRenderViewSkipped() {
        if (state_ == EditorFrameDebuggerState::WaitingGpuFence ||
            state_ == EditorFrameDebuggerState::PausedFrameDebug) {
            ++stats_.renderViewFramesSkipped;
        }
    }

    void EditorFrameDebugger::notifyFrameDebugRenderGraphViewDrawn(bool snapshotVisible) {
        ++stats_.frameDebugRenderGraphViewFrames;
        if (snapshotVisible) {
            ++stats_.frameDebugRenderGraphSnapshotFrames;
        }
    }

    void EditorFrameDebugger::notifyFrameDebugPreviewDrawn(bool textureVisible) {
        if (textureVisible) {
            ++stats_.previewTextureFramesDrawn;
        }
    }

    EditorViewportRepaintReasons EditorFrameDebugger::consumeRenderViewRepaintReasons() {
        const EditorViewportRepaintReasons reasons = pendingRenderViewRepaintReasons_;
        pendingRenderViewRepaintReasons_ = {};
        return reasons;
    }

    bool EditorFrameDebugger::shouldRecordRenderViews() const {
        return state_ != EditorFrameDebuggerState::WaitingGpuFence &&
               state_ != EditorFrameDebuggerState::PausedFrameDebug;
    }

    bool EditorFrameDebugger::shouldRunInspectedWorldSafePoints() const {
        return state_ != EditorFrameDebuggerState::WaitingGpuFence &&
               state_ != EditorFrameDebuggerState::PausedFrameDebug;
    }

    bool EditorFrameDebugger::isCapturingFrame() const {
        return state_ == EditorFrameDebuggerState::CapturingFrame;
    }

    EditorFrameDebuggerState EditorFrameDebugger::state() const {
        return state_;
    }

    std::string_view EditorFrameDebugger::stateName() const {
        return editorFrameDebuggerStateName(state_);
    }

    const std::optional<EditorFrameDebugCapture>& EditorFrameDebugger::pausedCapture() const {
        return pausedCapture_;
    }

    const std::optional<EditorFrameDebugCapture>& EditorFrameDebugger::latestCapture() const {
        return latestCapture_;
    }

    const EditorFrameDebugPreview& EditorFrameDebugger::preview() const {
        return preview_;
    }

    EditorFrameDebuggerStats EditorFrameDebugger::stats() const {
        return stats_;
    }

    void EditorFrameDebugger::transitionTo(EditorFrameDebuggerState state) {
        state_ = state;
    }

} // namespace asharia::editor
