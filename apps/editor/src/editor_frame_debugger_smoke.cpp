#include "editor_frame_debugger_smoke.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "asharia/core/log.hpp"

#include "editor_frame_debugger.hpp"
#include "editor_smoke.hpp"
#include "editor_viewport_overlay_provider.hpp"

namespace asharia::editor {
    namespace {

        [[nodiscard]] bool
        capturedSourceOverlayId(const asharia::BasicRenderViewOverlayDiagnostics& overlay,
                                std::string_view sourceOverlayId) {
            return std::ranges::find(overlay.sourceOverlayIds, sourceOverlayId) !=
                   overlay.sourceOverlayIds.end();
        }

        [[nodiscard]] const asharia::BasicRenderViewExecutionEvent*
        capturedExecutionEvent(const asharia::BasicRenderViewDiagnostics& diagnostics,
                               std::uint64_t eventId) {
            for (const asharia::BasicRenderViewExecutionEvent& event :
                 diagnostics.executionEvents) {
                if (event.id.value == eventId) {
                    return &event;
                }
            }
            return nullptr;
        }

        [[nodiscard]] bool
        isInspectableExecutionEvent(asharia::BasicRenderViewExecutionEventKind kind) {
            return kind != asharia::BasicRenderViewExecutionEventKind::BeginPass &&
                   kind != asharia::BasicRenderViewExecutionEventKind::EndPass;
        }

        [[nodiscard]] bool closeFloat(float lhs, float rhs) {
            return std::fabs(lhs - rhs) < 0.0001F;
        }

        [[nodiscard]] bool
        capturedWorldGridSettings(const asharia::BasicRenderViewOverlayDiagnostics& overlay) {
            const asharia::BasicRenderViewWorldGridDesc& worldGrid = overlay.worldGrid;
            const EditorViewportWorldGridSettings expected = defaultEditorSceneGridSettings();
            return overlay.worldGridEnabled && worldGrid.enabled &&
                   closeFloat(worldGrid.planeY, expected.planeY) &&
                   closeFloat(worldGrid.minorSpacing, expected.minorSpacing) &&
                   closeFloat(worldGrid.majorSpacing, expected.majorSpacing) &&
                   closeFloat(worldGrid.fadeStart, expected.fadeStart) &&
                   closeFloat(worldGrid.fadeEnd, expected.fadeEnd) &&
                   closeFloat(worldGrid.opacity, expected.opacity) &&
                   closeFloat(worldGrid.color[0], expected.color[0]) &&
                   closeFloat(worldGrid.color[1], expected.color[1]) &&
                   closeFloat(worldGrid.color[2], expected.color[2]) &&
                   closeFloat(worldGrid.color[3], expected.color[3]);
        }

    } // namespace

    [[nodiscard]] bool validateFrameDebuggerSmoke(EditorRunMode mode,
                                                  const EditorSmokeRunResult& runResult,
                                                  const EditorFrameDebugger& frameDebugger) {
        if (!isEditorFrameDebuggerSmokeMode(mode)) {
            return true;
        }

        if (!runResult.frameDebugCaptureRequested || !runResult.frameDebugReplayPassRequested ||
            !runResult.frameDebugPreviewRequested || !runResult.frameDebugPreviewVisible ||
            !runResult.frameDebugResumeRequested || !runResult.frameDebugRenderedAfterResume) {
            asharia::logError(
                "Editor frame debugger smoke did not complete capture/preview/resume flow.");
            return false;
        }
        if (frameDebugger.state() != EditorFrameDebuggerState::Running) {
            asharia::logError("Editor frame debugger smoke did not return to Running state.");
            return false;
        }

        const EditorFrameDebuggerStats stats = frameDebugger.stats();
        if (stats.captureRequests != 1 || stats.framesCaptured != 1 ||
            stats.completedCaptures != 1 || stats.resumeRequests != 1 || stats.framesResumed != 1 ||
            stats.renderViewFramesSkipped == 0 || stats.previewRequests == 0 ||
            stats.previewFramesRecorded == 0 || stats.previewTextureFramesPublished == 0 ||
            stats.previewTextureFramesDrawn == 0 || stats.replayPassRequests == 0 ||
            stats.replayPassSelections == 0 || stats.replayEventRequests == 0 ||
            stats.replayEventSelections == 0 || stats.frameDebugRenderGraphViewFrames == 0 ||
            stats.frameDebugRenderGraphSnapshotFrames == 0) {
            asharia::logError("Editor frame debugger smoke recorded unexpected state counts.");
            return false;
        }
        if (runResult.viewportFramesAtFrameDebugPreview !=
            runResult.viewportFramesAtFrameDebugPause) {
            asharia::logError(
                "Editor frame debugger smoke recorded a normal RenderView while previewing.");
            return false;
        }
        if (!runResult.frameDebugPreviewSelectedPassIndex ||
            !runResult.frameDebugPreviewSelectedExecutionEventId ||
            !runResult.frameDebugPreviewCopiedAfterPassIndex ||
            *runResult.frameDebugPreviewSelectedPassIndex !=
                *runResult.frameDebugPreviewCopiedAfterPassIndex) {
            asharia::logError(
                "Editor frame debugger smoke did not copy preview after the selected pass.");
            return false;
        }
        if (runResult.inspectedWorldFramesAtFrameDebugPreview !=
            runResult.inspectedWorldFramesAtFrameDebugPause) {
            asharia::logError("Editor frame debugger smoke advanced inspected-world safe points "
                              "while previewing.");
            return false;
        }
        if (runResult.viewportFramesAfterFrameDebugResume <=
            runResult.viewportFramesAtFrameDebugPause) {
            asharia::logError("Editor frame debugger smoke did not resume RenderView recording.");
            return false;
        }
        if (runResult.inspectedWorldFramesAfterFrameDebugResume <=
            runResult.inspectedWorldFramesAtFrameDebugPause) {
            asharia::logError(
                "Editor frame debugger smoke did not resume inspected-world safe points.");
            return false;
        }
        const EditorInspectedWorldSchedulerStats& inspectedWorldStats =
            runResult.inspectedWorldStats;
        if (inspectedWorldStats.frameAdvanceSafePoints == 0 ||
            inspectedWorldStats.gameUpdateSafePoints !=
                inspectedWorldStats.frameAdvanceSafePoints ||
            inspectedWorldStats.scriptUpdateSafePoints !=
                inspectedWorldStats.frameAdvanceSafePoints ||
            inspectedWorldStats.skippedFrameAdvanceSafePoints == 0 ||
            inspectedWorldStats.skippedGameUpdateSafePoints !=
                inspectedWorldStats.skippedFrameAdvanceSafePoints ||
            inspectedWorldStats.skippedScriptUpdateSafePoints !=
                inspectedWorldStats.skippedFrameAdvanceSafePoints) {
            asharia::logError(
                "Editor frame debugger smoke recorded invalid inspected-world safe-point counts.");
            return false;
        }
        const std::optional<EditorFrameDebugCapture>& capture = frameDebugger.latestCapture();
        if (!capture) {
            asharia::logError("Editor frame debugger smoke did not keep a captured snapshot.");
            return false;
        }
        if (capture->diagnostics.renderGraph.passes.size() != 3 ||
            capture->diagnostics.renderGraph.resources.size() != 2 ||
            capture->diagnostics.renderGraph.accessEdges.size() != 4 ||
            capture->diagnostics.renderGraph.dependencyEdges.size() != 2 ||
            capture->diagnostics.renderGraph.transitions.size() != 4) {
            asharia::logError(
                "Editor frame debugger smoke captured unexpected RenderGraph diagnostics: passes " +
                std::to_string(capture->diagnostics.renderGraph.passes.size()) + ", resources " +
                std::to_string(capture->diagnostics.renderGraph.resources.size()) +
                ", access edges " +
                std::to_string(capture->diagnostics.renderGraph.accessEdges.size()) +
                ", dependency edges " +
                std::to_string(capture->diagnostics.renderGraph.dependencyEdges.size()) +
                ", transitions " +
                std::to_string(capture->diagnostics.renderGraph.transitions.size()) + ".");
            return false;
        }
        if (capture->diagnostics.executionEvents.empty()) {
            asharia::logError("Editor frame debugger smoke captured no renderer execution events.");
            return false;
        }
        const asharia::BasicRenderViewExecutionEvent* selectedEvent = capturedExecutionEvent(
            capture->diagnostics, *runResult.frameDebugPreviewSelectedExecutionEventId);
        if (selectedEvent == nullptr) {
            asharia::logError(
                "Editor frame debugger smoke selected an event missing from the capture.");
            return false;
        }
        if (!isInspectableExecutionEvent(selectedEvent->kind) ||
            selectedEvent->passIndex != *runResult.frameDebugPreviewSelectedPassIndex) {
            asharia::logError(
                "Editor frame debugger smoke selected an event that does not map to the "
                "previewed pass.");
            return false;
        }
        if (!capturedSourceOverlayId(capture->diagnostics.overlay, kEditorSceneGridOverlayId) ||
            !capturedSourceOverlayId(capture->diagnostics.overlay,
                                     kEditorSceneTransformGizmoOverlayId) ||
            !capturedSourceOverlayId(capture->diagnostics.overlay,
                                     kEditorSceneSelectionOutlineOverlayId)) {
            asharia::logError(
                "Editor frame debugger smoke did not preserve overlay source diagnostics.");
            return false;
        }
        if (!capturedWorldGridSettings(capture->diagnostics.overlay)) {
            asharia::logError(
                "Editor frame debugger smoke did not preserve world-grid diagnostics.");
            return false;
        }
        if (frameDebugger.pausedCapture()) {
            asharia::logError("Editor frame debugger smoke kept a paused capture after resume.");
            return false;
        }
        return true;
    }
} // namespace asharia::editor
