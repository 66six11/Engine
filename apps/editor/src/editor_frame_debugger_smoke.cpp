#include "editor_frame_debugger_smoke.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "asharia/archive/archive_value.hpp"
#include "asharia/archive/json_archive.hpp"
#include "asharia/core/log.hpp"

#include "editor_frame_debugger.hpp"
#include "editor_frame_debugger_snapshot_projector.hpp"
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

        [[nodiscard]] const asharia::BasicRenderViewExecutionEvent*
        capturedStructuralExecutionEvent(const asharia::BasicRenderViewDiagnostics& diagnostics) {
            for (const asharia::BasicRenderViewExecutionEvent& event :
                 diagnostics.executionEvents) {
                if (!isInspectableExecutionEvent(event.kind)) {
                    return &event;
                }
            }
            return nullptr;
        }

        [[nodiscard]] bool isPreviewableWriteAccess(asharia::RenderGraphSlotAccess access) {
            return access == asharia::RenderGraphSlotAccess::ColorWrite ||
                   access == asharia::RenderGraphSlotAccess::TransferWrite;
        }

        [[nodiscard]] bool
        capturedWriteAccessForPassResource(const asharia::RenderGraphDiagnosticsSnapshot& snapshot,
                                           std::size_t passIndex, std::uint32_t resourceIndex) {
            return std::ranges::any_of(
                snapshot.accessEdges,
                [passIndex, resourceIndex](const asharia::RenderGraphDiagnosticsAccessEdge& edge) {
                    return edge.passIndex == passIndex &&
                           edge.resourceKind == asharia::RenderGraphResourceKind::Image &&
                           edge.resourceIndex == resourceIndex &&
                           isPreviewableWriteAccess(edge.access);
                });
        }

        [[nodiscard]] bool validateSelectedPreviewEventMapping(
            const EditorSmokeRunResult& runResult,
            const asharia::BasicRenderViewDiagnostics& diagnostics) {
            if (!runResult.frameDebugPreviewSelectedPassIndex ||
                !runResult.frameDebugPreviewSelectedExecutionEventId ||
                !runResult.frameDebugPreviewSelectedImageResourceIndex ||
                !runResult.frameDebugPreviewCopiedAfterPassIndex ||
                *runResult.frameDebugPreviewSelectedPassIndex !=
                    *runResult.frameDebugPreviewCopiedAfterPassIndex) {
                asharia::logError(
                    "Editor frame debugger smoke did not copy preview after the selected pass.");
                return false;
            }

            const asharia::BasicRenderViewExecutionEvent* selectedEvent = capturedExecutionEvent(
                diagnostics, *runResult.frameDebugPreviewSelectedExecutionEventId);
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
            if (!selectedEvent->targetImageResourceIndex ||
                *selectedEvent->targetImageResourceIndex !=
                    *runResult.frameDebugPreviewSelectedImageResourceIndex) {
                asharia::logError(
                    "Editor frame debugger smoke selected an event whose target image does not "
                    "map to the previewed resource.");
                return false;
            }
            if (!capturedWriteAccessForPassResource(diagnostics.renderGraph,
                                                    selectedEvent->passIndex,
                                                    *selectedEvent->targetImageResourceIndex)) {
                asharia::logError(
                    "Editor frame debugger smoke selected an event whose target image is not a "
                    "captured RenderGraph write output for the pass.");
                return false;
            }
            return true;
        }

        [[nodiscard]] bool
        validateStructuralEventReplayUnavailable(const EditorFrameDebugCapture& capture) {
            const asharia::BasicRenderViewExecutionEvent* structuralEvent =
                capturedStructuralExecutionEvent(capture.diagnostics);
            if (structuralEvent == nullptr) {
                asharia::logError(
                    "Editor frame debugger smoke captured no structural execution event.");
                return false;
            }

            EditorFrameDebugger debugger;
            if (!debugger.requestCapture()) {
                asharia::logError(
                    "Editor frame debugger smoke could not request temporary capture.");
                return false;
            }
            debugger.beginFrame(capture.frameIndex);
            debugger.captureRecordedView(EditorFrameDebugCaptureDesc{
                .frameIndex = capture.frameIndex,
                .submittedFrameEpoch = capture.submittedFrameEpoch,
                .viewKind = capture.viewKind,
                .requestedExtent = capture.requestedExtent,
                .diagnostics = capture.diagnostics,
            });
            debugger.endSubmittedFrame(capture.submittedFrameEpoch);
            if (debugger.state() != EditorFrameDebuggerState::PausedFrameDebug ||
                !debugger.selectReplayEvent(structuralEvent->id)) {
                asharia::logError(
                    "Editor frame debugger smoke could not select a structural event.");
                return false;
            }

            const EditorFrameDebugPreview& preview = debugger.preview();
            if (preview.status != EditorFrameDebugPreviewStatus::Unavailable || preview.dirty ||
                preview.selectedImageResourceIndex || debugger.consumePreviewRequest()) {
                asharia::logError(
                    "Editor frame debugger smoke allowed a structural event preview request.");
                return false;
            }
            return true;
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

        [[nodiscard]] const asharia::archive::ArchiveValue*
        requiredObjectMember(const asharia::archive::ArchiveValue& value, std::string_view key) {
            const asharia::archive::ArchiveValue* member = value.findMemberValue(key);
            if (member == nullptr || member->kind != asharia::archive::ArchiveValueKind::Object) {
                return nullptr;
            }
            return member;
        }

        [[nodiscard]] const asharia::archive::ArchiveValue*
        requiredArrayMember(const asharia::archive::ArchiveValue& value, std::string_view key) {
            const asharia::archive::ArchiveValue* member = value.findMemberValue(key);
            if (member == nullptr || member->kind != asharia::archive::ArchiveValueKind::Array) {
                return nullptr;
            }
            return member;
        }

        [[nodiscard]] bool requiredStringMemberEquals(
            const asharia::archive::ArchiveValue& value, std::string_view key,
            const char* expected) {
            const asharia::archive::ArchiveValue* member = value.findMemberValue(key);
            return member != nullptr && member->kind == asharia::archive::ArchiveValueKind::String &&
                   member->stringValue == expected;
        }

        [[nodiscard]] bool requiredIntegerMemberEquals(
            const asharia::archive::ArchiveValue& value, std::string_view key,
            std::int64_t expected) {
            const asharia::archive::ArchiveValue* member = value.findMemberValue(key);
            return member != nullptr &&
                   member->kind == asharia::archive::ArchiveValueKind::Integer &&
                   member->integerValue == expected;
        }

        [[nodiscard]] bool validateStudioFrameDebugSnapshotProjection(
            const EditorFrameDebugCapture& capture) {
            const asharia::BasicRenderViewExecutionEvent* selectedEvent = nullptr;
            for (const asharia::BasicRenderViewExecutionEvent& event :
                 capture.diagnostics.executionEvents) {
                if (isInspectableExecutionEvent(event.kind) && event.targetImageResourceIndex) {
                    selectedEvent = &event;
                    break;
                }
            }
            if (selectedEvent == nullptr) {
                asharia::logError(
                    "Editor frame debugger smoke found no previewable event for Studio snapshot.");
                return false;
            }

            EditorFrameDebugger projectedDebugger;
            if (!projectedDebugger.requestCapture()) {
                asharia::logError(
                    "Editor frame debugger smoke could not request projection capture.");
                return false;
            }
            projectedDebugger.beginFrame(capture.frameIndex);
            projectedDebugger.captureRecordedView(EditorFrameDebugCaptureDesc{
                .frameIndex = capture.frameIndex,
                .submittedFrameEpoch = capture.submittedFrameEpoch,
                .viewKind = capture.viewKind,
                .requestedExtent = capture.requestedExtent,
                .diagnostics = capture.diagnostics,
            });
            projectedDebugger.endSubmittedFrame(capture.submittedFrameEpoch);
            if (projectedDebugger.state() != EditorFrameDebuggerState::PausedFrameDebug ||
                !projectedDebugger.selectReplayEvent(selectedEvent->id) ||
                !projectedDebugger.preview().selectedImageResourceIndex) {
                asharia::logError(
                    "Editor frame debugger smoke could not prepare projected paused snapshot.");
                return false;
            }

            auto json = writeFrameDebuggerSnapshotJson(projectedDebugger);
            if (!json) {
                asharia::logError("Editor frame debugger smoke could not write Studio snapshot.");
                return false;
            }

            auto parsed = asharia::archive::readJsonArchive(*json);
            if (!parsed) {
                asharia::logError("Editor frame debugger smoke wrote invalid Studio snapshot JSON.");
                return false;
            }

            const asharia::archive::ArchiveValue& root = *parsed;
            const asharia::archive::ArchiveValue* captureJson =
                requiredObjectMember(root, "capture");
            const asharia::archive::ArchiveValue* passes = requiredArrayMember(root, "passes");
            const asharia::archive::ArchiveValue* resources =
                requiredArrayMember(root, "resources");
            const asharia::archive::ArchiveValue* events =
                requiredArrayMember(root, "executionEvents");
            const asharia::archive::ArchiveValue* preview =
                requiredObjectMember(root, "preview");
            if (!requiredIntegerMemberEquals(root, "schemaVersion", 1) ||
                !requiredIntegerMemberEquals(root, "version", 1) ||
                !requiredStringMemberEquals(root, "state", "PausedFrameDebug") ||
                captureJson == nullptr || passes == nullptr || resources == nullptr ||
                events == nullptr || preview == nullptr) {
                asharia::logError(
                    "Editor frame debugger smoke wrote an incomplete Studio snapshot root.");
                return false;
            }
            if (!requiredIntegerMemberEquals(*captureJson, "frameIndex", capture.frameIndex) ||
                !requiredIntegerMemberEquals(
                    *captureJson, "submittedFrameEpoch",
                    static_cast<std::int64_t>(capture.submittedFrameEpoch)) ||
                !requiredStringMemberEquals(*captureJson, "viewKind", "Scene")) {
                asharia::logError(
                    "Editor frame debugger smoke wrote an incomplete Studio capture snapshot.");
                return false;
            }
            if (passes->arrayValue.size() != capture.diagnostics.renderGraph.passes.size() ||
                resources->arrayValue.size() !=
                    capture.diagnostics.renderGraph.resources.size() ||
                events->arrayValue.size() != capture.diagnostics.executionEvents.size()) {
                asharia::logError(
                    "Editor frame debugger smoke projected unexpected Studio snapshot counts.");
                return false;
            }
            if (!requiredStringMemberEquals(*preview, "status", "Pending") ||
                !requiredIntegerMemberEquals(
                    *preview, "sourceResourceIndex",
                    static_cast<std::int64_t>(
                        *projectedDebugger.preview().selectedImageResourceIndex))) {
                asharia::logError(
                    "Editor frame debugger smoke did not project preview metadata.");
                return false;
            }
            return true;
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
        if (!validateSelectedPreviewEventMapping(runResult, capture->diagnostics)) {
            return false;
        }
        if (!validateStructuralEventReplayUnavailable(*capture)) {
            return false;
        }
        if (!capturedSourceOverlayId(capture->diagnostics.overlay, kEditorSceneGridOverlayId) ||
            capturedSourceOverlayId(capture->diagnostics.overlay,
                                    kEditorSceneTransformGizmoOverlayId) ||
            capturedSourceOverlayId(capture->diagnostics.overlay,
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
        if (!validateStudioFrameDebugSnapshotProjection(*capture)) {
            return false;
        }
        if (frameDebugger.pausedCapture()) {
            asharia::logError("Editor frame debugger smoke kept a paused capture after resume.");
            return false;
        }
        return true;
    }
} // namespace asharia::editor
