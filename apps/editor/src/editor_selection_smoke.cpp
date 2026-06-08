#include "editor_selection_smoke.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "asharia/core/log.hpp"
#include "asharia/scene/entity_id.hpp"

#include "editor_event.hpp"
#include "editor_selection.hpp"
#include "editor_smoke.hpp"
#include "editor_workspace.hpp"

namespace asharia::editor {

    namespace {

        [[nodiscard]] EditorSceneEntitySelectionId target(std::uint32_t index,
                                                          std::uint32_t generation = 1U,
                                                          std::string sceneId = std::string{
                                                              kEditorInspectedWorldSceneId}) {
            return EditorSceneEntitySelectionId{
                .sceneId = std::move(sceneId),
                .entity = asharia::EntityId{.index = index, .generation = generation},
            };
        }

        [[nodiscard]] EditorSelectionItem item(
            EditorSceneEntitySelectionId targetId,
            EditorSelectionTargetState state = EditorSelectionTargetState::Resolved,
            bool primary = false, std::string displayName = {}) {
            return EditorSelectionItem{
                .target = std::move(targetId),
                .state = state,
                .primary = primary,
                .displayName = std::move(displayName),
            };
        }

        [[nodiscard]] bool hasSelectionChangedEvent(std::span<const EditorEvent> events) {
            return std::ranges::any_of(events, [](const EditorEvent& event) {
                return event.kind == EditorEventKind::SelectionChanged &&
                       event.sourceId.value == kEditorSelectionOwnerId;
            });
        }

        [[nodiscard]] bool expectSelectionEvent(EditorEventQueue& eventQueue,
                                                EditorDiagnosticsLog& diagnosticsLog,
                                                std::uint64_t expectedSequence) {
            if (!hasSelectionChangedEvent(eventQueue.events())) {
                asharia::logError("Editor selection smoke missed SelectionChanged event.");
                return false;
            }
            diagnosticsLog.appendEvents(eventQueue.events());
            const std::span<const EditorDiagnosticEvent> recent = diagnosticsLog.recentEvents();
            if (recent.empty() || recent.back().sequence != expectedSequence ||
                recent.back().event.kind != EditorEventKind::SelectionChanged ||
                recent.back().event.sourceId.value != kEditorSelectionOwnerId) {
                asharia::logError(
                    "Editor selection smoke did not route SelectionChanged to diagnostics.");
                return false;
            }
            eventQueue.clear();
            return true;
        }

        [[nodiscard]] bool expectLatestChange(const EditorSelectionSet& selection,
                                              EditorSelectionChangeReason reason,
                                              std::size_t previousCount,
                                              std::size_t selectedCount, bool hasMissing,
                                              bool hasStale) {
            const std::optional<EditorSelectionChangedFact>& change = selection.latestChange();
            if (!change || change->reason != reason || change->previousCount != previousCount ||
                change->selectedCount != selectedCount || change->hasMissing != hasMissing ||
                change->hasStale != hasStale ||
                change->revision != selection.snapshot().revision) {
                asharia::logError("Editor selection smoke saw an invalid change fact.");
                return false;
            }
            return true;
        }

        [[nodiscard]] bool validateSingleSelection(EditorSelectionSet& selection,
                                                   EditorEventQueue& eventQueue,
                                                   EditorDiagnosticsLog& diagnosticsLog) {
            if (!selection.snapshot().empty() || selection.snapshot().revision != 0U) {
                asharia::logError("Editor selection smoke did not start empty.");
                return false;
            }

            const EditorSceneEntitySelectionId cameraTarget = target(1U);
            if (!selection.replace(
                    std::vector<EditorSelectionItem>{
                        item(cameraTarget, EditorSelectionTargetState::Resolved, false,
                             "Camera")},
                    EditorId{.value = "scene-tree"})) {
                asharia::logError("Editor selection smoke did not replace empty selection.");
                return false;
            }
            const EditorSelectionSnapshot& snapshot = selection.snapshot();
            const EditorSelectionItem* primary = snapshot.primary();
            const std::optional<EditorSelectionChangedFact>& change = selection.latestChange();
            if (snapshot.revision != 1U || snapshot.size() != 1U || primary == nullptr ||
                primary->target != cameraTarget || !primary->primary || snapshot.hasMissing() ||
                snapshot.hasStale() ||
                !expectLatestChange(selection, EditorSelectionChangeReason::Replace, 0U, 1U,
                                    false, false) ||
                !change || change->sourceId.value != "scene-tree" ||
                !expectSelectionEvent(eventQueue, diagnosticsLog, 1U)) {
                asharia::logError("Editor selection smoke single selection contract failed.");
                return false;
            }

            if (selection.replace(
                    std::vector<EditorSelectionItem>{
                        item(cameraTarget, EditorSelectionTargetState::Resolved, true,
                             "Camera")},
                    EditorId{.value = "scene-tree"}) ||
                !eventQueue.empty() || selection.snapshot().revision != 1U) {
                asharia::logError("Editor selection smoke emitted a no-op selection change.");
                return false;
            }
            return true;
        }

        [[nodiscard]] bool validateMultiSelection(EditorSelectionSet& selection,
                                                  EditorEventQueue& eventQueue,
                                                  EditorDiagnosticsLog& diagnosticsLog) {
            const EditorSceneEntitySelectionId lightTarget = target(2U);
            const EditorSceneEntitySelectionId staleTarget = target(3U, 7U);
            const EditorSceneEntitySelectionId missingTarget = target(4U, 2U);

            if (!selection.refresh(
                    std::vector<EditorSelectionItem>{
                        item(lightTarget, EditorSelectionTargetState::Resolved, true, "Light"),
                        item(lightTarget, EditorSelectionTargetState::Stale, true,
                             "Duplicate Light"),
                        item(staleTarget, EditorSelectionTargetState::Stale, false,
                             "Deleted Mesh"),
                        item(missingTarget, EditorSelectionTargetState::Missing, false,
                             "Missing Camera"),
                        item(EditorSceneEntitySelectionId{
                                 .sceneId = std::string{kEditorInspectedWorldSceneId},
                                 .entity = asharia::kInvalidEntityId,
                             },
                             EditorSelectionTargetState::Missing, false, "Invalid")},
                    EditorId{.value = "selection-refresh"})) {
                asharia::logError("Editor selection smoke did not refresh selection.");
                return false;
            }

            const EditorSelectionSnapshot& snapshot = selection.snapshot();
            const EditorSelectionItem* primary = snapshot.primary();
            const std::optional<EditorSelectionChangedFact>& change = selection.latestChange();
            if (snapshot.revision != 2U || snapshot.size() != 3U || primary == nullptr ||
                primary->target != lightTarget || !snapshot.hasMissing() || !snapshot.hasStale() ||
                !expectLatestChange(selection, EditorSelectionChangeReason::Refresh, 1U, 3U,
                                    true, true) ||
                !change || change->sourceId.value != "selection-refresh" ||
                !change->primaryTarget || *change->primaryTarget != lightTarget ||
                !expectSelectionEvent(eventQueue, diagnosticsLog, 2U)) {
                asharia::logError("Editor selection smoke multi selection contract failed.");
                return false;
            }

            EditorWorkspaceController workspace;
            const std::uint64_t revisionBeforeLayoutReset = selection.snapshot().revision;
            const std::size_t countBeforeLayoutReset = selection.snapshot().size();
            workspace.requestLayoutReset();
            if (!workspace.consumeLayoutResetRequest() ||
                selection.snapshot().revision != revisionBeforeLayoutReset ||
                selection.snapshot().size() != countBeforeLayoutReset) {
                asharia::logError(
                    "Editor selection smoke lost selection across layout reset state.");
                return false;
            }
            return true;
        }

        [[nodiscard]] bool validateClearSelection(EditorSelectionSet& selection,
                                                  EditorEventQueue& eventQueue,
                                                  EditorDiagnosticsLog& diagnosticsLog) {
            if (!selection.clear(EditorId{.value = "scene-tree"}) ||
                !selection.snapshot().empty() || selection.snapshot().revision != 3U ||
                !expectLatestChange(selection, EditorSelectionChangeReason::Clear, 3U, 0U, false,
                                    false) ||
                !expectSelectionEvent(eventQueue, diagnosticsLog, 3U)) {
                asharia::logError("Editor selection smoke did not clear selection.");
                return false;
            }

            if (selection.clear(EditorId{.value = "scene-tree"}) || !eventQueue.empty() ||
                selection.snapshot().revision != 3U) {
                asharia::logError("Editor selection smoke emitted a no-op clear event.");
                return false;
            }
            return true;
        }

    } // namespace

    bool validateEditorSelectionSmoke(EditorRunMode mode) {
        if (!isEditorSmokeMode(mode)) {
            return true;
        }

        EditorEventQueue eventQueue;
        EditorDiagnosticsLog diagnosticsLog;
        EditorSelectionSet selection{eventQueue};
        return validateSingleSelection(selection, eventQueue, diagnosticsLog) &&
               validateMultiSelection(selection, eventQueue, diagnosticsLog) &&
               validateClearSelection(selection, eventQueue, diagnosticsLog);
    }

} // namespace asharia::editor
