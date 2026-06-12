#include "editor_viewport_tool_state_smoke.hpp"

#include "asharia/core/log.hpp"

#include "editor_event.hpp"
#include "editor_smoke.hpp"
#include "editor_viewport_tool_state.hpp"

namespace asharia::editor {
    namespace {

        [[nodiscard]] bool expectEvent(const EditorEventQueue& queue, std::uint64_t revision,
                                       std::string_view subjectId,
                                       EditorEventOutcome outcome = EditorEventOutcome::Succeeded,
                                       EditorEventSeverity severity = EditorEventSeverity::Info) {
            const auto events = queue.events();
            if (events.empty()) {
                asharia::logError("Editor viewport tool state smoke expected an event.");
                return false;
            }

            const EditorEvent& event = events.back();
            const bool valid =
                event.kind == EditorEventKind::ViewportToolStateChanged &&
                event.sourceId.value == "scene-view" && event.metadata.revision == revision &&
                event.metadata.subjectId == subjectId && event.metadata.outcome == outcome &&
                event.metadata.severity == severity;
            if (!valid) {
                asharia::logError("Editor viewport tool state smoke found invalid event metadata.");
            }
            return valid;
        }

        [[nodiscard]] bool validateDefaultState(const EditorViewportToolState& state) {
            const EditorViewportToolStateSnapshot snapshot = state.snapshot();
            const bool valid =
                snapshot.revision == 0U && snapshot.activeTool == EditorViewportActiveTool::View &&
                snapshot.transformSpace == EditorViewportTransformSpace::Local &&
                snapshot.pivotMode == EditorViewportPivotMode::Pivot && !snapshot.snapEnabled &&
                snapshot.overlayFlags.gridVisible && !snapshot.overlayFlags.gizmoVisible &&
                !snapshot.overlayFlags.selectionOutlineVisible &&
                snapshot.viewMode == EditorViewportViewMode::Shaded &&
                snapshot.playPreviewState == EditorViewportPlayPreviewState::Edit &&
                !snapshot.availability.selectionToolAvailable &&
                !snapshot.availability.transformToolsAvailable;
            if (!valid) {
                asharia::logError("Editor viewport tool state smoke found invalid default state.");
            }
            return valid;
        }

    } // namespace

    bool validateEditorViewportToolStateSmoke(EditorRunMode mode) {
        if (!isEditorSmokeMode(mode)) {
            return true;
        }

        EditorEventQueue eventQueue;
        EditorViewportToolState state{eventQueue};
        if (!validateDefaultState(state)) {
            return false;
        }

        if (state.setActiveTool(EditorViewportActiveTool::Move, "smoke")) {
            asharia::logError(
                "Editor viewport tool state smoke activated a pending transform tool.");
            return false;
        }
        if (!expectEvent(eventQueue, 0U, "activeTool", EditorEventOutcome::Noop,
                         EditorEventSeverity::Warning)) {
            return false;
        }

        if (!state.setSnapEnabled(true, "smoke") || state.snapshot().revision != 1U ||
            !state.snapshot().snapEnabled || !expectEvent(eventQueue, 1U, "snap")) {
            return false;
        }
        if (!state.setTransformSpace(EditorViewportTransformSpace::World, "smoke") ||
            state.snapshot().revision != 2U || !expectEvent(eventQueue, 2U, "transformSpace")) {
            return false;
        }
        if (!state.setPivotMode(EditorViewportPivotMode::Center, "smoke") ||
            state.snapshot().revision != 3U || !expectEvent(eventQueue, 3U, "pivotMode")) {
            return false;
        }
        if (!state.setGridVisible(false, "smoke") || state.snapshot().revision != 4U ||
            state.snapshot().overlayFlags.gridVisible ||
            !expectEvent(eventQueue, 4U, "overlayFlags")) {
            return false;
        }
        if (!state.setViewMode(EditorViewportViewMode::Wireframe, "smoke") ||
            state.snapshot().revision != 5U || !expectEvent(eventQueue, 5U, "viewMode")) {
            return false;
        }
        if (!state.setPlayPreviewState(EditorViewportPlayPreviewState::PlayPreview, "smoke") ||
            state.snapshot().revision != 6U || !expectEvent(eventQueue, 6U, "playPreviewState")) {
            return false;
        }
        if (state.setOverlayFlags(state.snapshot().overlayFlags, "smoke")) {
            asharia::logError(
                "Editor viewport tool state smoke emitted a duplicate overlay update.");
            return false;
        }

        return true;
    }

} // namespace asharia::editor
